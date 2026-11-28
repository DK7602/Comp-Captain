#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "AudioImport.h"

#include "cc/TakesFolder.h"
#include "cc/TextFormat.h"
#include "cc/WavFile.h"

#include <deque>
#include <limits>

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
    std::vector<std::shared_ptr<const CompCaptainProcessor::Scores>> scored;
    struct Sound
    {
        juce::uint32 serial = 0;
        std::unique_ptr<cc::Player::Clip> clip;
        juce::String problem;
    };
    std::vector<Sound> sounds;                           // takes and comps to hear, worked out
    std::vector<CompCaptainProcessor::Built> built;      // Build Performance results
    std::atomic<bool> closed { false };
};

// Everything the comp needs about this track's takes, gathered on the message thread for a background job.
struct CompCaptainProcessor::CompJob
{
    std::deque<std::vector<cc::LinePlace>> places;
    std::vector<std::shared_ptr<const cc::TakeFeatures>> features;
    std::vector<cc::CompSource> sources;
    std::vector<cc::CompFile> files;
    std::map<std::uint64_t, cc::Take> takeOf;
    std::vector<cc::CompChoice> choices;
    cc::Lyrics lyrics;

    const cc::CompSource* source (std::uint64_t key) const
    {
        for (auto& s : sources)
            if (s.key == key)
                return &s;
        return nullptr;
    }
    // song seconds where a take starts singing a line (-1: it didn't)
    double lineStart (std::uint64_t key, int line) const
    {
        if (const auto* s = source (key); s != nullptr && s->places != nullptr)
            for (auto& p : *s->places)
                if (p.line == line)
                    return s->startSeconds + p.start;
        return -1.0;
    }
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
    // A VST3 host may stop calling a plug-in on a quiet track. Comp Captain plays takes and the comp on its own (with
    // the DAW stopped), so it asks to keep being called; what it does on each call costs next to nothing.
    if (wrapperType == wrapperType_VST3)
        return std::numeric_limits<double>::infinity();
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

    // Hearing a take or the comp from the window: it takes the track's place (never while recording).
    const int outs = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    player.process (buffer.getArrayOfWritePointers(), outs, n, getSampleRate(), t.playing, t.recording, t.hasSeconds ? t.seconds : 0.0);
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
        text += "style=" + cc::text::escape (style) + "\n";
        text += "weights=" + cc::text::formatFixed (weights.pitch, 3) + "," + cc::text::formatFixed (weights.timing, 3) + ","
                + cc::text::formatFixed (weights.clarity, 3) + "," + cc::text::formatFixed (weights.energy, 3) + "\n";
        for (auto& [line, pick] : picked)
            text += "pick=" + std::to_string (line) + " " + std::to_string (pick.take) + " " + cc::text::escape (pick.lineText) + "\n";
        text += "picky=" + cc::text::formatFixed (picky, 3) + "\n";
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
        style = "Singing";
        weights = *cc::styleWeights ("Singing");
        picked.clear();
        picky = 0.7;
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
            else if (key == "style")
                style = cc::text::unescape (value);
            else if (key == "picky")
            {
                double v = 0.7;
                if (cc::text::parseDouble (value, v))
                    picky = std::clamp (v, 0.0, 1.0);
            }
            else if (key == "weights")
            {
                double w[4] = { 0.25, 0.25, 0.25, 0.25 };
                std::string_view rest = value;
                for (int k = 0; k < 4; ++k)
                {
                    const auto comma = rest.find (',');
                    cc::text::parseDouble (rest.substr (0, comma), w[k]);
                    rest = comma == std::string_view::npos ? std::string_view {} : rest.substr (comma + 1);
                }
                weights = { w[0], w[1], w[2], w[3] };
            }
            else if (key == "pick")
            {
                // "LINE TAKE text of the line"
                const auto a = value.find (' ');
                const auto b = a == std::string_view::npos ? a : value.find (' ', a + 1);
                std::int64_t line = 0, take = 0;
                if (b != std::string_view::npos && cc::text::parseInt (value.substr (0, a), line) && cc::text::parseInt (value.substr (a + 1, b - a - 1), take) && line >= 0 && take > 0)
                    picked[(int) line] = { (std::uint32_t) take, cc::text::unescape (value.substr (b + 1)) };
            }
        });
        takes = cc::TakeList::fromText (takesPart);
        nextTakeNumber.store (std::max (1, takes.nextTakeNumber));
        overviews.clear();
        missing.clear();
        overviewAsked.clear();
        hearings.clear();
        scored.reset();
        scoringFor = 0;
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
            for (const char* ext : { ".words", ".feat" })
                if (const auto extra = file.withFileExtension (ext); moved && extra.existsAsFile())
                    extra.moveFileTo (dest.withFileExtension (ext));
        }
        takes.remove (id);
        overviews.erase (id);
        missing.erase (id);
        hearings.erase (id);
        for (auto it = picked.begin(); it != picked.end();)
            it = it->second.take == id ? picked.erase (it) : std::next (it);
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
    std::vector<std::shared_ptr<const Scores>> newScores;
    {
        const std::scoped_lock lock (mailbox->lock);
        imported.swap (mailbox->imported);
        pictures.swap (mailbox->overviews);
        heard.swap (mailbox->heard);
        newScores.swap (mailbox->scored);
    }
    if (! newScores.empty())
    {
        {
            const std::scoped_lock lock (stateLock);
            scored = newScores.back();
            scoringFor = 0;
        }
        scoresChanged.fetch_add (1);
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
                h.features = std::move (r.features);
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
    scheduleScoring();
    collectAuditions();
}

// ============================================================================ scores

void CompCaptainProcessor::scheduleScoring()
{
    // Only once the song's lining up is current, one scoring at a time, and only when something changed.
    const auto song = CompCaptainProcessor::song();
    {
        const std::scoped_lock lock (stateLock);
        if (scoringFor != 0 || (scored != nullptr && scored->songVersion == song->version))
            return;
    }
    auto job = std::make_shared<Scores>();
    std::vector<cc::TakeForScoring> takesIn;
    auto places = std::make_shared<std::deque<std::vector<cc::LinePlace>>>();
    auto words = std::make_shared<std::deque<std::vector<cc::HeardWord>>>();
    std::vector<std::shared_ptr<const cc::TakeFeatures>> keepAlive;
    {
        const std::scoped_lock lock (stateLock);
        scoringFor = song->version;
        job->songVersion = song->version;
        for (auto& t : takes.all())
        {
            const auto h = hearings.find (t.id);
            if (t.source == cc::TakeSource::ara || ! cc::isSung (t.role) || h == hearings.end() || ! h->second.done || h->second.features == nullptr)
                continue;
            const std::uint64_t key = (uniqueId << 32) | t.id;
            const auto placed = song->lineUp.takes.find (key);
            if (placed == song->lineUp.takes.end())
                continue;
            places->push_back (placed->second);
            words->push_back (h->second.hearing.words);
            keepAlive.push_back (h->second.features);
            cc::TakeForScoring in;
            in.key = key;
            in.startSeconds = t.start.seconds;
            in.startPpq = t.start.ppq;
            in.bpm = t.start.bpm;
            in.favourite = t.favourite;
            in.features = h->second.features.get();
            in.places = &places->back();
            in.words = &words->back();
            takesIn.push_back (in);
            job->takeOf[key] = t.id;
        }
    }
    // the scoring itself happens on the background thread for files (it takes a moment with many takes)
    jobs->addJob ([box = mailbox, job, takesIn, places, words, keepAlive, lyrics = song->lyrics]
    {
        if (box->closed.load())
            return;
        job->lines = cc::scoreLines (lyrics, takesIn);
        const std::scoped_lock lock (box->lock);
        box->scored.push_back (job);
    });
}

std::shared_ptr<const CompCaptainProcessor::Scores> CompCaptainProcessor::scores() const
{
    const std::scoped_lock lock (stateLock);
    return scored;
}

bool CompCaptainProcessor::scoresReady() const
{
    const auto song = CompCaptainProcessor::song();
    const std::scoped_lock lock (stateLock);
    return scored != nullptr && scored->songVersion == song->version;
}

std::string CompCaptainProcessor::styleName() const
{
    const std::scoped_lock lock (stateLock);
    return style;
}

cc::ScoreWeights CompCaptainProcessor::styleWeights() const
{
    const std::scoped_lock lock (stateLock);
    return weights;
}

void CompCaptainProcessor::setStyle (const std::string& name, const cc::ScoreWeights& w)
{
    {
        const std::scoped_lock lock (stateLock);
        style = name.substr (0, 40);
        weights = w;
    }
    scoresChanged.fetch_add (1);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

std::map<int, CompCaptainProcessor::Pick> CompCaptainProcessor::picks() const
{
    const std::scoped_lock lock (stateLock);
    return picked;
}

void CompCaptainProcessor::setPick (int line, std::uint32_t take, const std::string& lineText)
{
    {
        const std::scoped_lock lock (stateLock);
        if (take == 0)
            picked.erase (line);
        else
            picked[line] = { take, lineText };
    }
    scoresChanged.fetch_add (1);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void CompCaptainProcessor::setPicks (std::map<int, Pick> all)
{
    {
        const std::scoped_lock lock (stateLock);
        picked = std::move (all);
    }
    scoresChanged.fetch_add (1);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void CompCaptainProcessor::clearPicks()
{
    {
        const std::scoped_lock lock (stateLock);
        picked.clear();
    }
    scoresChanged.fetch_add (1);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
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

// ============================================================================ the comp (M4)

void CompCaptainProcessor::setComp (std::vector<CompLine> lines, double howPicky)
{
    bool changed = false;
    {
        const std::scoped_lock lock (stateLock);
        std::sort (lines.begin(), lines.end(), [] (const CompLine& a, const CompLine& b) { return a.line < b.line; });
        const auto same = [] (const std::vector<CompLine>& a, const std::vector<CompLine>& b) {
            return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), [] (const CompLine& x, const CompLine& y) { return x.line == y.line && x.take == y.take; });
        };
        changed = ! same (lines, compLines);
        compLines = std::move (lines);
        howPicky = std::clamp (howPicky, 0.0, 1.0);
        if (std::abs (howPicky - picky) > 1e-6)
        {
            picky = howPicky;
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
        }
    }
    // hearing the comp in time while it changes: work it out again (it carries on from the same song position)
    if (changed && auditionState.what == "comp" && auditionState.inTime)
        startAudition ("comp", 0, -1, true);
}

double CompCaptainProcessor::compPicky() const
{
    const std::scoped_lock lock (stateLock);
    return picky;
}

std::shared_ptr<CompCaptainProcessor::CompJob> CompCaptainProcessor::gatherComp (bool withChoices) const
{
    auto job = std::make_shared<CompJob>();
    const auto song = CompCaptainProcessor::song();
    job->lyrics = song->lyrics;
    const std::scoped_lock lock (stateLock);
    const auto folder = juce::File (fromStd (takes.folder));
    for (auto& t : takes.all())
    {
        const auto h = hearings.find (t.id);
        if (t.source == cc::TakeSource::ara || ! cc::isSung (t.role) || h == hearings.end() || ! h->second.done)
            continue;
        const std::uint64_t key = (uniqueId << 32) | t.id;
        const auto placed = song->lineUp.takes.find (key);
        if (placed == song->lineUp.takes.end())
            continue;
        job->places.push_back (placed->second);
        job->features.push_back (h->second.features);
        cc::CompSource s;
        s.key = key;
        s.startSeconds = t.start.seconds;
        s.lengthSeconds = t.lengthSeconds();
        s.places = &job->places.back();
        s.features = h->second.features.get();
        job->sources.push_back (s);
        job->files.push_back ({ key, toPath (folder.getChildFile (fromStd (t.file))), t.start.seconds });
        job->takeOf[key] = t;
    }
    if (withChoices)
        for (auto& c : compLines)
            job->choices.push_back ({ c.line, (uniqueId << 32) | c.take });
    return job;
}

void CompCaptainProcessor::hearTakeLine (std::uint32_t take, int line) { startAudition ("take", take, line, transport().playing); }
void CompCaptainProcessor::hearComp (int fromLine) { startAudition ("comp", 0, fromLine, transport().playing); }
void CompCaptainProcessor::hearInTime (std::uint32_t take) { startAudition (take != 0 ? "take" : "comp", take, -1, true); }

void CompCaptainProcessor::stopHearing()
{
    ++auditionSerial;
    player.stop();
    auditionState = {};
}

void CompCaptainProcessor::startAudition (const juce::String& what, std::uint32_t take, int line, bool inTime)
{
    const auto serial = ++auditionSerial;
    Audition a;
    a.what = what;
    a.take = take;
    a.line = line;
    a.inTime = inTime;
    a.preparing = true;
    a.seconds = auditionState.seconds;
    auditionState = a;
    auditionTicks = 0;

    auto job = gatherComp (what == "comp");
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const int channels = juce::jlimit (1, 2, getTotalNumOutputChannels());
    const std::uint64_t key = (uniqueId << 32) | take;
    jobs->addJob ([box = mailbox, job, what, key, line, inTime, rate, channels, serial]
    {
        if (box->closed.load())
            return;
        cc::CompPlan plan;
        double from = -1.0;
        juce::String problem;
        if (what == "take")
        {
            if (const auto* src = job->source (key))
            {
                plan = inTime ? cc::planTake (*src) : cc::planLine (*src, line);
                if (plan.empty())
                    problem = "That take didn't sing this line.";
            }
            else
                problem = "That take hasn't been listened to yet.";
        }
        else
        {
            plan = cc::planComp (job->choices, job->sources);
            if (plan.empty())
                problem = "No comp yet: once your takes are scored, every line gets its take.";
            else if (line >= 0)
                for (auto& c : job->choices)
                    if (c.line >= line)
                    {
                        from = job->lineStart (c.take, c.line) - 0.4;
                        break;
                    }
        }
        Mailbox::Sound sound;
        sound.serial = serial;
        if (problem.isEmpty() && plan.end - plan.start > 600.0)
            problem = "That's too long to play from here (over 10 minutes).";
        if (problem.isEmpty())
        {
            std::string error;
            auto audio = cc::renderComp (plan, job->files, plan.start, plan.end, rate, channels, &box->closed, error);
            if (! audio)
                problem = fromStd (error);
            else
            {
                auto clip = std::make_unique<cc::Player::Clip>();
                clip->channels = std::move (audio->channels);
                clip->sampleRate = rate;
                clip->songStart = plan.start;
                clip->inTime = inTime;
                clip->id = serial;
                if (! inTime && from > plan.start)
                    clip->playFrom = (std::int64_t) std::llround ((from - plan.start) * rate);
                sound.clip = std::move (clip);
            }
        }
        sound.problem = problem;
        const std::scoped_lock lock (box->lock);
        box->sounds.push_back (std::move (sound));
    });
}

void CompCaptainProcessor::collectAuditions()
{
    std::vector<Mailbox::Sound> sounds;
    std::vector<Built> results;
    {
        const std::scoped_lock lock (mailbox->lock);
        sounds.swap (mailbox->sounds);
        results.swap (mailbox->built);
    }
    for (auto& s : sounds)
    {
        if (s.serial != auditionSerial || auditionState.what.isEmpty())
            continue; // asked for something else since
        auditionState.preparing = false;
        if (s.clip == nullptr)
        {
            auditionState.problem = s.problem;
            player.stop();
            continue;
        }
        player.play (std::move (s.clip));
        auditionBlocks = blocksPlayed.load();
        auditionTicks = 0;
    }
    player.tidy();

    if (! auditionState.what.isEmpty() && ! auditionState.preparing && auditionState.problem.isEmpty())
    {
        const auto s = player.state();
        auditionState.sounding = s.sounding;
        auditionState.seconds = s.seconds;
        if (! auditionState.inTime)
        {
            ++auditionTicks;
            if (s.finished)
                stopHearing(); // it played to the end (or the DAW started)
            else if (! s.sounding && auditionTicks > 8 && blocksPlayed.load() == auditionBlocks)
            {
                // the DAW isn't calling Comp Captain while it's stopped
                player.stop();
                auditionState.problem = "Cubase isn't running Comp Captain while it's stopped. Turn on the track's monitor button "
                                        "(the speaker), or press Play in Cubase: then it plays in time with the song.";
            }
        }
    }

    for (auto& r : results)
    {
        {
            const std::scoped_lock lock (stateLock);
            built = r;
        }
        building.store (false);
        builtChanged.fetch_add (1);
        addNews (r.ok ? "saved" : "failed", r.message);
    }
}

void CompCaptainProcessor::buildComp()
{
    if (building.exchange (true))
        return;
    auto job = gatherComp (true);
    const auto folder = takesFolder();
    const auto role = fromStd (std::string (cc::toLabel (trackRole())));
    jobs->addJob ([box = mailbox, job, folder, role]
    {
        if (box->closed.load())
            return;
        Built result;
        const auto plan = cc::planComp (job->choices, job->sources);
        if (plan.empty())
            result.message = "No comp yet: once your takes are scored, every line gets its take.";
        else
        {
            // start on the bar line before the comp, at the takes' own sample rate and width
            const auto& firstTake = job->takeOf[plan.pieces.front().take];
            const auto bar = cc::barStartBefore (firstTake.start, plan.start);
            double rate = firstTake.sampleRate > 0.0 ? firstTake.sampleRate : 48000.0;
            int channels = 1;
            for (auto& p : plan.pieces)
                channels = std::max (channels, job->takeOf[p.take].channels);
            channels = std::min (channels, 2);
            std::string error;
            const auto audio = cc::renderComp (plan, job->files, bar.seconds, plan.end, rate, channels, &box->closed, error);
            if (! audio)
                result.message = "Couldn't build the comp: " + fromStd (error);
            else
            {
                // "Lead Comp - Verse 1 (bar 9).wav", or "Verse 1 to Chorus"
                const auto sectionOf = [&] (int line) -> juce::String {
                    if (line < 0 || line >= (int) job->lyrics.lines.size())
                        return {};
                    const auto sec = job->lyrics.lines[(size_t) line].section;
                    return sec >= 0 && sec < (int) job->lyrics.sections.size() ? fromStd (job->lyrics.sections[(size_t) sec].name) : juce::String();
                };
                const auto first = sectionOf (plan.pieces.front().firstLine), last = sectionOf (plan.pieces.back().lastLine);
                juce::String part = first.isNotEmpty() && last.isNotEmpty() && first != last ? first + " to " + last : (first.isNotEmpty() ? first : last);
                juce::String base = role + " Comp" + (part.isNotEmpty() ? " - " + part : juce::String()) + " (bar " + juce::String (bar.bar) + ")";
                base = base.removeCharacters ("\\/:*?\"<>|").trim();
                auto file = folder.getChildFile (base + ".wav");
                for (int n = 2; file.exists() && n < 1000; ++n)
                    file = folder.getChildFile (base + " " + juce::String (n) + ".wav");
                folder.createDirectory();
                cc::WavWriter w;
                std::vector<const float*> ptrs;
                for (auto& c : audio->channels)
                    ptrs.push_back (c.data());
                if (w.open (toPath (file), rate, channels) && w.write (ptrs.data(), (int) audio->frames()) && w.close())
                {
                    result.ok = true;
                    result.file = file;
                    result.bar = bar.bar;
                    result.message = file.getFileName() + " is ready: drag it into your DAW at bar " + juce::String (bar.bar) + ".";
                }
                else
                {
                    file.deleteFile();
                    result.message = "Couldn't save the comp in " + folder.getFullPathName() + " (is the disk full?).";
                }
            }
        }
        const std::scoped_lock lock (box->lock);
        box->built.push_back (result);
    });
}

std::optional<CompCaptainProcessor::Built> CompCaptainProcessor::lastBuilt() const
{
    const std::scoped_lock lock (stateLock);
    return built;
}
