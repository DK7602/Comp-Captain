#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "AudioImport.h"

#include "cc/TakesFolder.h"
#include "cc/TextFormat.h"
#include "cc/WavFile.h"

#include <deque>

namespace
{
    // Saved projects start with this line so we can recognise (and later upgrade) our own state.
    constexpr const char* stateHeader = "CompCaptainState\n";
    constexpr const char* trackMarker = "[cc-track]\n";
    constexpr const char* takesMarker = "[cc-takes]\n";
    constexpr const char* lyricsMarker = "[cc-lyrics]\n";

    std::atomic<juce::uint64> instanceCounter { 1 };

    // Every Comp Captain in this project (same process), so the track picker can show them all.
    struct Registry
    {
        std::recursive_mutex lock; // withTrack() may look at the list again
        std::vector<CompCaptainProcessor*> all;
        juce::uint32 members = 1;  // counts copies coming and going

        // the song's lyrics (the newest edit wins when a project loads)
        std::string lyrics;
        juce::int64 lyricsStamp = 0;
        juce::uint32 lyricsVersion = 1;
        std::shared_ptr<const CompCaptainProcessor::Song> song;

        static Registry& get()
        {
            static Registry r;
            return r;
        }
    };

    std::filesystem::path toPath (const juce::File& f) { return cc::pathFromUtf8 (f.getFullPathName().toStdString()); }
    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

// One shared background thread writes every track's takes to disk (high priority, never the audio thread).
struct CompCaptainProcessor::WriterThread final : public juce::TimeSliceThread
{
    WriterThread() : juce::TimeSliceThread ("Comp Captain takes writer") { startThread (Priority::high); }
    ~WriterThread() override { stopThread (4000); }
};

// One shared background thread for slow file jobs (adding files, drawing waveforms of saved takes).
struct CompCaptainProcessor::JobPool final : public juce::ThreadPool
{
    JobPool() : juce::ThreadPool (juce::ThreadPoolOptions {}.withThreadName ("Comp Captain files").withNumberOfThreads (1)) {}
    ~JobPool() { removeAllJobs (true, 4000); }
};

struct CompCaptainProcessor::Mailbox
{
    struct Imported
    {
        bool ok = false;
        cc::Take take;
        std::vector<std::uint8_t> peaks;
        juce::String message;
    };
    struct Overview
    {
        std::uint32_t id = 0;
        bool found = false;
        std::vector<std::uint8_t> peaks;
    };

    std::mutex lock;
    std::vector<Imported> imported;
    std::vector<Overview> overviews;
    std::vector<Listening::Result> heard;
    std::atomic<bool> closed { false };
};

CompCaptainProcessor::CompCaptainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      mailbox (std::make_shared<Mailbox>()),
      uniqueId (instanceCounter.fetch_add (1))
{
    // CAPTURE armed. Not automatable (it would clutter the automation lanes); the window switches it.
    auto param = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "capture", 1 }, "CAPTURE armed", false,
                                                              juce::AudioParameterBoolAttributes().withAutomatable (false));
    captureParam = param.get();
    addParameter (param.release());

    seed = juce::Random::getSystemRandom().nextInt64();
    writer.setNumbering (&nextTakeNumber);
    writerThread->addTimeSliceClient (this);

    {
        auto& reg = Registry::get();
        const std::scoped_lock lock (reg.lock);
        reg.all.push_back (this);
        ++reg.members;
    }
    startTimerHz (10);
}

CompCaptainProcessor::~CompCaptainProcessor()
{
    stopTimer();
    {
        auto& reg = Registry::get();
        const std::scoped_lock lock (reg.lock);
        reg.all.erase (std::remove (reg.all.begin(), reg.all.end(), this), reg.all.end());
        ++reg.members;
        if (reg.all.empty())
        {
            // the project closed: the next one starts with its own lyrics
            reg.lyrics.clear();
            reg.lyricsStamp = 0;
            ++reg.lyricsVersion;
            reg.song.reset();
        }
    }
    mailbox->closed.store (true);
    listening->cancelFor (this);
    if (wasCapturing)
        listening->recording (false);
    writerThread->removeTimeSliceClient (this);
}

// ============================================================================ audio

void CompCaptainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    meter.readAndReset();
    const int channels = juce::jlimit (1, 2, getTotalNumInputChannels());
    feed.prepare (sampleRate, channels, 8.0);
#if JucePlugin_Enable_ARA
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#else
    juce::ignoreUnused (samplesPerBlock);
#endif
}

void CompCaptainProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
}

double CompCaptainProcessor::getTailLengthSeconds() const
{
#if JucePlugin_Enable_ARA
    double tail = 0.0;
    if (getTailLengthSecondsForARA (tail))
        return tail;
#endif
    return 0.0;
}

bool CompCaptainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, same in as out (it sits on a vocal or guitar track).
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void CompCaptainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    blocksPlayed.fetch_add (1, std::memory_order_relaxed);
    const int ins = getTotalNumInputChannels();
    const int n = buffer.getNumSamples();

    juce::AudioPlayHead::PositionInfo pos;
    bool hasPosition = false;
    if (auto* head = getPlayHead())
    {
        if (auto p = head->getPosition())
        {
            pos = *p;
            hasPosition = true;
        }
    }

    // for the window's bar ruler and BPM read-out
    tKnown.store (hasPosition, std::memory_order_relaxed);
    tPlaying.store (pos.getIsPlaying(), std::memory_order_relaxed);
    tRecording.store (pos.getIsRecording(), std::memory_order_relaxed);
    if (auto ppq = pos.getPpqPosition())
        tPpq.store (*ppq, std::memory_order_relaxed);
    if (auto bpm = pos.getBpm())
        tBpm.store (*bpm, std::memory_order_relaxed);
    if (auto secs = pos.getTimeInSeconds())
        tSeconds.store (*secs, std::memory_order_relaxed);
    if (auto sig = pos.getTimeSignature())
    {
        tNum.store (sig->numerator, std::memory_order_relaxed);
        tDen.store (sig->denominator, std::memory_order_relaxed);
    }

#if JucePlugin_Enable_ARA
    // As a Cubase Extension, we play the lane takes ourselves (unchanged).
    if (processBlockForARA (buffer, isNonRealtime() ? juce::AudioProcessor::Realtime::no : juce::AudioProcessor::Realtime::yes, pos))
    {
        meter.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n);
        return;
    }
#endif

    // The audio is never changed: input and output share the same buffer, so leaving it alone
    // is a bit-for-bit pass-through. We only read it, for the meter and for CAPTURE.
    const int readable = juce::jmin (ins, buffer.getNumChannels());
    meter.process (buffer.getArrayOfReadPointers(), readable, n);

    feed.setArmed (captureParam->get());
    cc::Transport t;
    t.playing = pos.getIsPlaying();
    t.recording = pos.getIsRecording();
    if (auto samples = pos.getTimeInSamples())
    {
        t.hasSamples = true;
        t.timeInSamples = *samples;
    }
    if (auto secs = pos.getTimeInSeconds())
    {
        t.hasSeconds = true;
        t.seconds = *secs;
    }
    if (auto ppq = pos.getPpqPosition())
    {
        t.hasPpq = true;
        t.ppq = *ppq;
    }
    t.bpm = pos.getBpm().orFallback (120.0);
    if (auto sig = pos.getTimeSignature())
        t.meter = { sig->numerator, sig->denominator };
    if (auto bar = pos.getPpqPositionOfLastBarStart())
    {
        t.hasBarStart = true;
        t.barStartPpq = *bar;
    }
    feed.process (buffer.getArrayOfReadPointers(), readable, n, t);

    for (int ch = ins; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

int CompCaptainProcessor::useTimeSlice()
{
    const bool busy = writer.pump();
    if (busy || feed.isCapturing())
        return 10;
    return feed.isArmed() ? 25 : 100;
}

juce::AudioProcessorEditor* CompCaptainProcessor::createEditor()
{
    return new CompCaptainEditor (*this);
}

// ============================================================================ ARA

bool CompCaptainProcessor::isAra() const
{
#if JucePlugin_Enable_ARA
    return isBoundToARA();
#else
    return false;
#endif
}

#if JucePlugin_Enable_ARA
CompCaptainAraDocument* CompCaptainProcessor::araDocument() const
{
    if (auto* dc = getDocumentController())
        return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<CompCaptainAraDocument> (dc);
    return nullptr;
}

std::vector<const void*> CompCaptainProcessor::araRegionsPlayedHere() const
{
    std::vector<const void*> out;
    if (auto* renderer = getPlaybackRenderer())
        for (auto* r : renderer->getPlaybackRegions())
            out.push_back (r);
    return out;
}

juce::String CompCaptainProcessor::araRoles() const
{
    juce::StringArray roles;
    if (isPlaybackRenderer())
        roles.add ("plays the takes (playback renderer)");
    if (isEditorView())
        roles.add ("shows them (editor view)");
    if (isEditorRenderer())
        roles.add ("previews them (editor renderer)");
    return roles.isEmpty() ? juce::String ("linked to ARA, with no job given") : roles.joinIntoString (", ");
}
#endif

// ============================================================================ state

void CompCaptainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // The song's lyrics first: the registry's lock is always taken before a copy's own lock (never the
    // other way round), so saving can't get stuck against the window reading every copy's takes.
    std::string lyrics;
    juce::int64 lyricsStamp = 0;
    {
        auto& reg = Registry::get();
        const std::scoped_lock song (reg.lock);
        lyrics = reg.lyrics;
        lyricsStamp = reg.lyricsStamp;
    }
    std::string text;
    {
        const std::scoped_lock lock (stateLock);
        takes.nextTakeNumber = nextTakeNumber.load();
        text = std::string (stateHeader) + uiState.toText();
        text += trackMarker;
        text += "trackName=" + cc::text::escape (track.toStdString()) + "\n";
        text += "trackRole=" + std::string (cc::toName (roleOfTrack)) + "\n";
        text += std::string ("trackRoleChosen=") + (roleOfTrackChosen ? "1" : "0") + "\n";
        text += std::string ("loopTakes=") + (feed.loopTakes() ? "1" : "0") + "\n";
        text += "seed=" + std::to_string (seed) + "\n";
        text += lyricsMarker;
        text += "stamp=" + std::to_string (lyricsStamp) + "\n";
        text += "text=" + cc::text::escape (lyrics) + "\n";
        text += takesMarker;
        text += takes.toText();
    }
    dest.replaceAll (text.data(), text.size());
}

void CompCaptainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const std::string text (static_cast<const char*> (data), (size_t) sizeInBytes);
    if (text.rfind (stateHeader, 0) != 0)
        return; // not ours: keep the defaults

    std::string_view rest = std::string_view (text).substr (std::char_traits<char>::length (stateHeader));
    std::string_view uiPart = rest, trackPart, takesPart, lyricsPart;
    if (const auto t = rest.find (trackMarker); t != std::string_view::npos)
    {
        uiPart = rest.substr (0, t);
        trackPart = rest.substr (t + std::char_traits<char>::length (trackMarker));
        if (const auto k = trackPart.find (takesMarker); k != std::string_view::npos)
        {
            takesPart = trackPart.substr (k + std::char_traits<char>::length (takesMarker));
            trackPart = trackPart.substr (0, k);
        }
        if (const auto l = trackPart.find (lyricsMarker); l != std::string_view::npos)
        {
            lyricsPart = trackPart.substr (l + std::char_traits<char>::length (lyricsMarker));
            trackPart = trackPart.substr (0, l);
        }
    }
    {
        std::int64_t stamp = 0;
        std::string lyricsText;
        bool hasLyrics = false;
        cc::text::forEachKeyValue (lyricsPart, [&] (std::string_view key, std::string_view value) {
            if (key == "stamp")
                cc::text::parseInt (value, stamp);
            else if (key == "text")
            {
                lyricsText = cc::text::unescape (value);
                hasLyrics = true;
            }
        });
        if (hasLyrics)
            adoptSongLyrics (lyricsText, stamp);
    }

    bool loop = true;
    {
        const std::scoped_lock lock (stateLock);
        uiState = cc::UiState::fromText (uiPart);
        cc::text::forEachKeyValue (trackPart, [&] (std::string_view key, std::string_view value) {
            std::int64_t i = 0;
            if (key == "trackName" && track.isEmpty())
                track = fromStd (cc::text::unescape (value));
            else if (key == "trackRole")
                cc::fromName (value, roleOfTrack);
            else if (key == "trackRoleChosen")
                roleOfTrackChosen = value == "1";
            else if (key == "loopTakes")
                loop = value != "0";
            else if (key == "seed" && cc::text::parseInt (value, i))
                seed = i;
        });
        takes = cc::TakeList::fromText (takesPart);
        nextTakeNumber.store (std::max (1, takes.nextTakeNumber));
        overviews.clear();
        missing.clear();
        overviewAsked.clear();
        hearings.clear();
        destinationKey.clear();
    }
    feed.setLoopTakes (loop);
    listening->cancelFor (this);
    heardChanged.fetch_add (1);
    changed (false);
}

void CompCaptainProcessor::updateTrackProperties (const TrackProperties& props)
{
    if (! props.name.has_value())
        return;
    {
        const std::scoped_lock lock (stateLock);
        track = *props.name;
        if (! roleOfTrackChosen)
            roleOfTrack = cc::guessRole (track.toStdString()).value_or (cc::Role::lead);
        destinationKey.clear();
    }
    changed (false);
}

cc::UiState CompCaptainProcessor::getUiState() const
{
    const std::scoped_lock lock (stateLock);
    return uiState;
}

void CompCaptainProcessor::setUiState (const cc::UiState& s)
{
    const std::scoped_lock lock (stateLock);
    uiState = s;
}

// ============================================================================ CAPTURE

void CompCaptainProcessor::setArmed (bool on)
{
    if (on)
        updateCaptureDestination();
    feed.setArmed (on);
    captureParam->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

bool CompCaptainProcessor::isArmed() const
{
    return captureParam->get();
}

void CompCaptainProcessor::setLoopTakes (bool on)
{
    feed.setLoopTakes (on);
    changed();
}

bool CompCaptainProcessor::loopTakes() const
{
    return feed.loopTakes();
}

juce::File CompCaptainProcessor::takesRoot()
{
    const auto custom = juce::SystemStats::getEnvironmentVariable ("COMP_CAPTAIN_TAKES_FOLDER", {});
    if (custom.isNotEmpty())
        return juce::File (custom);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Comp Captain");
}

juce::File CompCaptainProcessor::takesFolder()
{
    bool made = false;
    juce::File folder;
    {
        const std::scoped_lock lock (stateLock);
        if (takes.folder.empty())
        {
            const auto now = juce::Time::getCurrentTime();
            const auto name = cc::trackFolderName ({ now.getYear(), now.getMonth() + 1, now.getDayOfMonth() },
                                                   track.toStdString(), cc::shortHexId ((unsigned long long) seed));
            takes.folder = takesRoot().getChildFile (fromStd (name)).getFullPathName().toStdString();
            made = true;
        }
        folder = juce::File (fromStd (takes.folder));
    }
    if (made)
        changed();
    return folder;
}

juce::File CompCaptainProcessor::takeFile (const cc::Take& t) const
{
    const std::scoped_lock lock (stateLock);
    return juce::File (fromStd (takes.folder)).getChildFile (fromStd (t.file));
}

void CompCaptainProcessor::updateCaptureDestination()
{
    const auto folder = takesFolder();
    juce::String name;
    cc::Role role;
    {
        const std::scoped_lock lock (stateLock);
        name = track;
        role = roleOfTrack;
    }
    const auto key = folder.getFullPathName() + "|" + name + "|" + juce::String (std::string (cc::toName (role)));
    if (key == destinationKey)
        return;
    destinationKey = key;
    writer.setDestination (toPath (folder), name.toStdString(), role);
}

// ============================================================================ the track

juce::String CompCaptainProcessor::trackName() const
{
    const std::scoped_lock lock (stateLock);
    return track;
}

cc::Role CompCaptainProcessor::trackRole() const
{
    const std::scoped_lock lock (stateLock);
    return roleOfTrack;
}

void CompCaptainProcessor::setTrackRole (cc::Role r)
{
    {
        const std::scoped_lock lock (stateLock);
        roleOfTrack = r;
        roleOfTrackChosen = true;
        // takes whose role you never picked follow the track
        for (auto& t : takes.edit())
            if (! t.roleChosen && t.source == cc::TakeSource::capture)
                t.role = r;
        destinationKey.clear();
    }
    changed();
}

// ============================================================================ takes

cc::TakeList CompCaptainProcessor::getTakes() const
{
    const std::scoped_lock lock (stateLock);
    return takes;
}

bool CompCaptainProcessor::renameTake (std::uint32_t id, const juce::String& name)
{
    const auto clean = name.trim().substring (0, 80);
    if (clean.isEmpty())
        return false;
    {
        const std::scoped_lock lock (stateLock);
        auto* t = takes.find (id);
        if (t == nullptr)
            return false;
        t->name = clean.toStdString();
    }
    changed();
    return true;
}

bool CompCaptainProcessor::setTakeRole (std::uint32_t id, cc::Role role)
{
    {
        const std::scoped_lock lock (stateLock);
        auto* t = takes.find (id);
        if (t == nullptr)
            return false;
        t->role = role;
        t->roleChosen = true;
    }
    changed();
    return true;
}

bool CompCaptainProcessor::setTakeFavourite (std::uint32_t id, bool on)
{
    {
        const std::scoped_lock lock (stateLock);
        auto* t = takes.find (id);
        if (t == nullptr)
            return false;
        t->favourite = on;
    }
    changed();
    return true;
}

bool CompCaptainProcessor::removeTake (std::uint32_t id)
{
    juce::String name;
    bool moved = false;
    {
        const std::scoped_lock lock (stateLock);
        auto* t = takes.find (id);
        if (t == nullptr)
            return false;
        name = fromStd (t->name);
        // The file isn't deleted: it moves to "Removed takes" in the same folder, in case you want it back.
        const juce::File folder (fromStd (takes.folder));
        const auto file = folder.getChildFile (fromStd (t->file));
        if (t->file.size() > 0 && file.existsAsFile())
        {
            const auto bin = folder.getChildFile ("Removed takes");
            bin.createDirectory();
            const auto dest = bin.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), true);
            moved = file.moveFileTo (dest);
            // what was heard in it goes along with it
            const auto words = file.withFileExtension (".words");
            if (moved && words.existsAsFile())
                words.moveFileTo (dest.withFileExtension (".words"));
        }
        takes.remove (id);
        overviews.erase (id);
        missing.erase (id);
        hearings.erase (id);
    }
    heardChanged.fetch_add (1);
    changed();
    addNews ("info", name + " removed." + (moved ? juce::String (" Its file is in the \"Removed takes\" folder.") : juce::String()));
    return true;
}

bool CompCaptainProcessor::takeFileMissing (std::uint32_t id) const
{
    const std::scoped_lock lock (stateLock);
    const auto it = missing.find (id);
    return it != missing.end() && it->second;
}

std::optional<std::vector<std::uint8_t>> CompCaptainProcessor::takeOverview (std::uint32_t id) const
{
    const std::scoped_lock lock (stateLock);
    const auto it = overviews.find (id);
    if (it == overviews.end())
        return std::nullopt;
    return it->second;
}

void CompCaptainProcessor::changed (bool markProjectDirty)
{
    takesChanged.fetch_add (1);
    if (markProjectDirty)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); // so the DAW asks to save
}

void CompCaptainProcessor::addNews (const juce::String& kind, const juce::String& text)
{
    const std::scoped_lock lock (stateLock);
    news.push_back ({ kind, text });
    while (news.size() > 30)
        news.pop_front();
}

std::vector<CompCaptainProcessor::News> CompCaptainProcessor::takeNews()
{
    const std::scoped_lock lock (stateLock);
    std::vector<News> out (news.begin(), news.end());
    news.clear();
    return out;
}

CompCaptainProcessor::TransportView CompCaptainProcessor::transport() const
{
    TransportView v;
    v.known = tKnown.load (std::memory_order_relaxed);
    v.playing = tPlaying.load (std::memory_order_relaxed);
    v.recording = tRecording.load (std::memory_order_relaxed);
    v.ppq = tPpq.load (std::memory_order_relaxed);
    v.bpm = cc::clampBpm (tBpm.load (std::memory_order_relaxed));
    v.seconds = tSeconds.load (std::memory_order_relaxed);
    v.meter = { tNum.load (std::memory_order_relaxed), tDen.load (std::memory_order_relaxed) };
    if (! v.meter.isValid())
        v.meter = {};
    return v;
}

// ============================================================================ files and waveforms

namespace
{
    CompCaptainProcessor::Placement sanePlacement (CompCaptainProcessor::Placement p)
    {
        p.bar = juce::jlimit (1, 9999, p.bar);
        p.bpm = cc::clampBpm (p.bpm);
        if (! p.meter.isValid())
            p.meter = {};
        return p;
    }
}

void CompCaptainProcessor::importFiles (std::vector<ImportRequest> requests, Placement placement)
{
    placement = sanePlacement (placement);
    const auto folder = takesFolder();
    juce::String trackNow;
    cc::Role roleNow;
    {
        const std::scoped_lock lock (stateLock);
        trackNow = track;
        roleNow = roleOfTrack;
    }

    // Each file is decoded into a float WAV in the takes folder, one at a time, in the background.
    for (auto& req : requests)
    {
        importsPending.fetch_add (1);
        jobs->addJob ([box = mailbox, req, folder, placement, trackNow, roleNow]
        {
            ImportPlan plan;
            plan.source = req.file;
            plan.name = req.name;
            plan.folder = folder;
            plan.bar = placement.bar;
            plan.bpm = placement.bpm;
            plan.meter = placement.meter;
            plan.role = req.role;
            plan.fallbackRole = roleNow;
            plan.trackName = trackNow;
            auto imported = importAudioFile (plan, &box->closed);
            if (req.deleteWhenDone)
                req.file.deleteFile();

            Mailbox::Imported result;
            result.ok = imported.ok;
            result.take = std::move (imported.take);
            result.peaks = std::move (imported.peaks);
            result.message = imported.message;
            const std::scoped_lock lock (box->lock);
            box->imported.push_back (std::move (result));
        });
    }
}

void CompCaptainProcessor::scheduleOverviews()
{
    // Waveform pictures for takes loaded with the project (the files are read in the background).
    std::vector<std::pair<std::uint32_t, std::filesystem::path>> todo;
    {
        const std::scoped_lock lock (stateLock);
        const juce::File folder (fromStd (takes.folder));
        for (auto& t : takes.all())
        {
            if (t.source == cc::TakeSource::ara || overviews.count (t.id) != 0 || overviewAsked[t.id])
                continue;
            overviewAsked[t.id] = true;
            todo.emplace_back (t.id, toPath (folder.getChildFile (fromStd (t.file))));
        }
    }
    for (auto& [id, path] : todo)
    {
        jobs->addJob ([box = mailbox, id = id, path = path]
        {
            Mailbox::Overview result;
            result.id = id;
            cc::WavReader reader;
            if (! box->closed.load() && reader.open (path))
            {
                const auto& info = reader.info();
                cc::OverviewBuilder overview (info.sampleRate);
                std::vector<std::vector<float>> chans ((size_t) std::min (2, info.channels), std::vector<float> (65536));
                std::vector<float*> ptrs;
                for (auto& c : chans)
                    ptrs.push_back (c.data());
                int n = 0;
                while ((n = reader.read (ptrs.data(), (int) ptrs.size(), 65536)) > 0 && ! box->closed.load())
                    overview.add (ptrs.data(), (int) ptrs.size(), n);
                overview.finish();
                result.found = true;
                result.peaks = overview.peaks();
            }
            const std::scoped_lock lock (box->lock);
            box->overviews.push_back (std::move (result));
        });
    }
}

void CompCaptainProcessor::collectNews()
{
    // New takes from CAPTURE
    for (auto& n : writer.takeNews())
    {
        if (n.kind == cc::CaptureNews::Kind::saved)
        {
            {
                const std::scoped_lock lock (stateLock);
                const auto id = takes.add (n.take);
                overviews[id] = std::move (n.peaks);
                overviewAsked[id] = true;
            }
            changed();
            addNews ("saved", fromStd (n.message));
        }
        else if (n.kind == cc::CaptureNews::Kind::discarded || n.kind == cc::CaptureNews::Kind::failed)
        {
            addNews (n.kind == cc::CaptureNews::Kind::failed ? "failed" : "discarded", fromStd (n.message));
        }
    }

    // Finished background jobs
    std::vector<Mailbox::Imported> imported;
    std::vector<Mailbox::Overview> pictures;
    std::vector<Listening::Result> heard;
    {
        const std::scoped_lock lock (mailbox->lock);
        imported.swap (mailbox->imported);
        pictures.swap (mailbox->overviews);
        heard.swap (mailbox->heard);
    }
    if (! heard.empty())
    {
        const auto problem = listening->status().problem;
        for (auto& r : heard)
        {
            juce::String name;
            {
                const std::scoped_lock lock (stateLock);
                const auto* t = takes.find (r.takeId);
                if (t == nullptr || toPath (juce::File (fromStd (takes.folder)).getChildFile (fromStd (t->file))) != r.file)
                    continue; // removed, or the project was loaded again meanwhile
                name = fromStd (t->name);
                auto& h = hearings[r.takeId];
                h.done = r.ok;
                h.failed = ! r.ok;
                h.hearing = std::move (r.hearing);
            }
            // a problem with speech itself (no model) shows once in the Lyrics page, not per take
            if (! r.ok && r.message.isNotEmpty() && r.message != problem)
                addNews ("failed", "Couldn't listen to " + name + ": " + r.message);
        }
        heardChanged.fetch_add (1);
    }
    for (auto& r : imported)
    {
        importsPending.fetch_sub (1);
        if (r.ok)
        {
            {
                const std::scoped_lock lock (stateLock);
                const auto id = takes.add (r.take);
                overviews[id] = std::move (r.peaks);
                overviewAsked[id] = true;
            }
            changed();
        }
        addNews (r.ok ? "imported" : "failed", r.message);
    }
    if (! pictures.empty())
    {
        {
            const std::scoped_lock lock (stateLock);
            for (auto& p : pictures)
            {
                if (takes.find (p.id) == nullptr)
                    continue;
                missing[p.id] = ! p.found;
                if (p.found)
                    overviews[p.id] = std::move (p.peaks);
            }
        }
        changed (false);
    }
}

void CompCaptainProcessor::timerCallback()
{
    collectNews();
    if (captureParam->get()) // armed from the DAW's side (or a test): make sure the writer knows where to save
        updateCaptureDestination();
    scheduleOverviews();

    // Listening waits while a take is being recorded. If the DAW stops sending audio (Cubase can pause
    // plug-ins on a quiet track), the recording is over even though no block came to say so.
    const auto blocks = blocksPlayed.load (std::memory_order_relaxed);
    quietTicks = blocks == blocksSeen ? quietTicks + 1 : 0;
    blocksSeen = blocks;
    const bool capturing = feed.isCapturing() && quietTicks < 10;
    if (capturing != wasCapturing)
    {
        listening->recording (capturing);
        wasCapturing = capturing;
    }
    scheduleListening();
}

// ============================================================================ listening

void CompCaptainProcessor::scheduleListening()
{
    std::vector<Listening::Job> todo;
    {
        const std::scoped_lock lock (stateLock);
        const juce::File folder (fromStd (takes.folder));
        for (auto& t : takes.all())
        {
            if (t.source == cc::TakeSource::ara || ! cc::isSung (t.role) || t.file.empty())
                continue;
            auto& h = hearings[t.id];
            if (h.asked)
                continue;
            h.asked = true;
            Listening::Job job;
            job.owner = this;
            job.takeId = t.id;
            job.file = toPath (folder.getChildFile (fromStd (t.file)));
            job.name = fromStd (t.name) + (track.isNotEmpty() ? " (" + track + ")" : juce::String());
            job.again = h.again;
            h.again = false;
            job.done = [box = mailbox] (Listening::Result&& r) {
                const std::scoped_lock l (box->lock);
                if (! box->closed.load())
                    box->heard.push_back (std::move (r));
            };
            todo.push_back (std::move (job));
        }
    }
    for (auto& j : todo)
        listening->add (std::move (j));
}

juce::String CompCaptainProcessor::takeHearingState (std::uint32_t id) const
{
    const std::scoped_lock lock (stateLock);
    const auto it = hearings.find (id);
    if (it == hearings.end() || ! it->second.asked)
        return {};
    return it->second.done ? "done" : it->second.failed ? "failed" : "waiting";
}

std::optional<cc::Hearing> CompCaptainProcessor::takeHearing (std::uint32_t id) const
{
    const std::scoped_lock lock (stateLock);
    const auto it = hearings.find (id);
    if (it == hearings.end() || ! it->second.done)
        return std::nullopt;
    return it->second.hearing;
}

void CompCaptainProcessor::listenAgain()
{
    listening->cancelFor (this);
    {
        const std::scoped_lock lock (stateLock);
        for (auto& [id, h] : hearings)
        {
            h = {};
            h.again = true;
        }
    }
    heardChanged.fetch_add (1);
}

Listening::Status CompCaptainProcessor::listeningStatus()
{
    const juce::SharedResourcePointer<Listening> l;
    return l->status();
}

// ============================================================================ the song

juce::String CompCaptainProcessor::songLyrics()
{
    auto& reg = Registry::get();
    const std::scoped_lock lock (reg.lock);
    return fromStd (reg.lyrics);
}

void CompCaptainProcessor::setSongLyrics (const juce::String& text)
{
    std::vector<CompCaptainProcessor*> everyone;
    {
        auto& reg = Registry::get();
        const std::scoped_lock lock (reg.lock);
        const auto t = text.toStdString();
        if (t == reg.lyrics)
            return;
        reg.lyrics = t;
        reg.lyricsStamp = juce::Time::currentTimeMillis();
        ++reg.lyricsVersion;
        everyone = reg.all;
    }
    // every copy saves the lyrics, so the DAW should ask to save the project
    for (auto* p : everyone)
        p->updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void CompCaptainProcessor::adoptSongLyrics (const std::string& text, juce::int64 stamp)
{
    // Each copy saved the lyrics as they were when the project was saved; the newest edit wins.
    auto& reg = Registry::get();
    const std::scoped_lock lock (reg.lock);
    if (stamp < reg.lyricsStamp || (stamp == reg.lyricsStamp && text == reg.lyrics))
        return;
    reg.lyrics = text;
    reg.lyricsStamp = stamp;
    ++reg.lyricsVersion;
}

std::shared_ptr<const CompCaptainProcessor::Song> CompCaptainProcessor::song()
{
    auto& reg = Registry::get();
    const std::scoped_lock lock (reg.lock);
    juce::uint64 version = ((juce::uint64) reg.members << 40) + ((juce::uint64) reg.lyricsVersion << 20);
    for (auto* p : reg.all)
        version += p->heardChanged.load() + p->takesChanged.load();
    if (reg.song != nullptr && reg.song->version == version)
        return reg.song;

    auto s = std::make_shared<Song>();
    s->version = version;
    s->text = fromStd (reg.lyrics);
    s->lyrics = cc::Lyrics::parse (reg.lyrics);

    std::deque<std::vector<cc::HeardWord>> words; // deques: the pointers below stay put as they grow
    std::deque<std::vector<cc::Phrase>> phrases;
    std::vector<cc::TakeHeard> heard;
    for (auto* p : reg.all)
    {
        const std::scoped_lock state (p->stateLock);
        for (auto& t : p->takes.all())
        {
            if (t.source == cc::TakeSource::ara || ! cc::isSung (t.role))
                continue;
            ++s->sungTakes;
            const auto h = p->hearings.find (t.id);
            if (h == p->hearings.end() || ! h->second.done)
                continue;
            ++s->heardTakes;
            words.push_back (h->second.hearing.words);
            phrases.push_back (h->second.hearing.phrases);
            cc::TakeHeard th;
            th.key = (p->uniqueId << 32) | t.id;
            th.startSeconds = t.start.seconds;
            th.startPpq = t.start.ppq;
            th.bpm = t.start.bpm;
            th.lengthSeconds = t.lengthSeconds();
            th.role = t.role;
            th.favourite = t.favourite;
            th.words = &words.back();
            th.phrases = &phrases.back();
            heard.push_back (th);
            s->takeNames[th.key] = fromStd (t.name) + (p->track.isNotEmpty() ? " (" + p->track + ")" : juce::String());
        }
    }
    s->lineUp = cc::lineUpSong (s->lyrics, heard);
    // takes that were listened to but didn't match the lyrics: what they heard, for the Lyrics page
    if (! s->lyrics.empty())
    {
        for (auto& th : heard)
        {
            if (s->lineUp.takes.count (th.key) != 0 || th.role == cc::Role::adlib)
                continue;
            Song::Unmatched u;
            u.take = s->takeNames[th.key];
            u.words = (int) th.words->size();
            int n = 0;
            for (auto& w : *th.words)
            {
                if (++n > 14)
                {
                    u.heard << " ...";
                    break;
                }
                u.heard << fromStd (w.text);
            }
            u.heard = u.heard.trim();
            s->unmatched.push_back (u);
        }
    }
    reg.song = s;
    return s;
}

// ============================================================================ all the tracks

std::vector<CompCaptainProcessor::TrackInfo> CompCaptainProcessor::allTracks()
{
    std::vector<TrackInfo> out;
    auto& reg = Registry::get();
    const std::scoped_lock lock (reg.lock);
    for (auto* p : reg.all)
    {
        TrackInfo info;
        info.id = p->uniqueId;
        info.ara = p->isAra();
        {
            const std::scoped_lock state (p->stateLock);
            info.name = p->track;
            info.role = p->roleOfTrack;
            info.takes = (int) p->takes.size();
        }
        out.push_back (info);
    }
    return out;
}

bool CompCaptainProcessor::withTrack (juce::uint64 id, const std::function<void (CompCaptainProcessor&)>& fn)
{
    auto& reg = Registry::get();
    const std::scoped_lock lock (reg.lock);
    for (auto* p : reg.all)
    {
        if (p->uniqueId == id)
        {
            fn (*p);
            return true;
        }
    }
    return false;
}

// ============================================================================ entry points

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompCaptainProcessor();
}

#if JucePlugin_Enable_ARA
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<CompCaptainAraDocument>();
}
#endif
