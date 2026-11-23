#include "AraSupport.h"

#if JucePlugin_Enable_ARA

#include <ARA_Library/Utilities/ARATimelineConversion.h>

#include <algorithm>
#include <cmath>

namespace
{
    juce::String fromUtf8 (const char* s) { return s != nullptr ? juce::String::fromUTF8 (s) : juce::String(); }

    // Reads the DAW's audio ahead of playback on its own thread, so the audio thread never waits.
    class AraReadThread final : public juce::TimeSliceThread
    {
    public:
        AraReadThread() : juce::TimeSliceThread ("Comp Captain ARA reader") { startThread (Priority::high); }
        ~AraReadThread() override { stopThread (2000); }
    };

    // Reads a take from the DAW, never asking for samples past its start or end (those are silence).
    // The ARA rules say a DAW should answer such reads with silence; this way we don't depend on it.
    class ClippedSourceReader final : public juce::AudioFormatReader
    {
    public:
        explicit ClippedSourceReader (juce::ARAAudioSource* source)
            : juce::AudioFormatReader (nullptr, "ARA audio source"), inner (source)
        {
            sampleRate = inner.sampleRate;
            bitsPerSample = inner.bitsPerSample;
            lengthInSamples = inner.lengthInSamples;
            numChannels = inner.numChannels;
            usesFloatingPointData = inner.usesFloatingPointData;
        }

        bool readSamples (int* const* dest, int numDest, int destOffset, juce::int64 start, int count) override
        {
            clearSamplesBeyondAvailableLength (dest, numDest, destOffset, start, count, lengthInSamples);
            if (count <= 0)
                return true; // all of it was past the end: silence
            return inner.readSamples (dest, numDest, destOffset, start, count);
        }

    private:
        juce::ARAAudioSourceReader inner;
    };

    // Plays the takes Cubase gave us exactly as they are (no changes in M1), so the track sounds the same
    // with Comp Captain on it as without.
    class PlaybackRenderer final : public juce::ARAPlaybackRenderer
    {
    public:
        PlaybackRenderer (ARA::PlugIn::DocumentController* dc, CompCaptainAraDocument& docIn)
            : juce::ARAPlaybackRenderer (dc), doc (docIn) {}

        void prepareToPlay (double rate, int maxBlock, int channels, juce::AudioProcessor::ProcessingPrecision,
                            AlwaysNonRealtime alwaysNonRealtime) override
        {
            sampleRate = rate;
            maxSamples = maxBlock;
            numChannels = channels;
            buffered = alwaysNonRealtime == AlwaysNonRealtime::no;
            readers.clear();

            int widest = 2;
            for (auto* region : getPlaybackRegions())
            {
                auto* source = region->getAudioModification()->getAudioSource();
                widest = std::max (widest, (int) source->getChannelCount());
                if (readers.count (source) != 0)
                    continue;
                auto reader = std::make_unique<ClippedSourceReader> (source);
                if (buffered)
                {
                    const int ahead = std::max (4 * maxBlock, juce::roundToInt (2.0 * rate));
                    auto bufferedReader = std::make_unique<juce::BufferingAudioReader> (reader.release(), *readThread, ahead);
                    bufferedReaders[source] = bufferedReader.get();
                    readers[source] = std::move (bufferedReader);
                }
                else
                {
                    readers[source] = std::move (reader);
                }
            }
            scratch.setSize (widest, maxBlock);
        }

        void releaseResources() override
        {
            readers.clear();
            bufferedReaders.clear();
        }

        bool processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                           const juce::AudioPlayHead::PositionInfo& position) noexcept override
        {
            const auto lock = doc.tryProcessingLock();
            buffer.clear();
            if (! lock.isLocked() || ! position.getIsPlaying())
                return true; // the DAW is changing the takes right now, or we're stopped: silence

            const int n = buffer.getNumSamples();
            if (n > maxSamples)
                return false;
            const auto blockStart = position.getTimeInSamples().orFallback (0);
            const auto block = juce::Range<juce::int64>::withStartAndLength (blockStart, n);
            bool ok = true;

            for (auto* region : getPlaybackRegions())
            {
                const auto regionRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
                auto render = block.getIntersectionWith (regionRange);
                if (render.isEmpty())
                    continue;

                const juce::Range<juce::int64> inModification { region->getStartInAudioModificationSamples(),
                                                                region->getEndInAudioModificationSamples() };
                const auto offset = inModification.getStart() - regionRange.getStart();
                render = render.getIntersectionWith (inModification.movedToStartAt (regionRange.getStart()));
                if (render.isEmpty())
                    continue;

                auto* source = region->getAudioModification()->getAudioSource();
                const auto found = readers.find (source);
                if (found == readers.end())
                {
                    ok = false;
                    continue;
                }
                if (std::abs (source->getSampleRate() - sampleRate) > 0.5)
                {
                    doc.rateMismatch.store (true); // shown in the report; plays silent in this version
                    continue;
                }

                if (auto b = bufferedReaders.find (source); b != bufferedReaders.end())
                    b->second->setReadTimeout (realtime == juce::AudioProcessor::Realtime::no ? 100 : 0);

                const int count = (int) render.getLength();
                const int at = (int) (render.getStart() - block.getStart());
                const int sourceChannels = std::min ((int) source->getChannelCount(), scratch.getNumChannels());
                juce::AudioBuffer<float> into (scratch.getArrayOfWritePointers(), sourceChannels, count);
                if (! found->second->read (&into, 0, count, render.getStart() + offset, true, true))
                {
                    ok = false;
                    continue;
                }

                // Mono take on a stereo track: both sides. Stereo take on a mono track: the two mixed.
                const int outs = buffer.getNumChannels();
                for (int c = 0; c < outs; ++c)
                {
                    if (outs == 1 && sourceChannels >= 2)
                    {
                        buffer.addFrom (0, at, into, 0, 0, count, 0.5f);
                        buffer.addFrom (0, at, into, 1, 0, count, 0.5f);
                    }
                    else
                    {
                        buffer.addFrom (c, at, into, std::min (c, sourceChannels - 1), 0, count);
                    }
                }
            }
            return ok;
        }

        using juce::ARAPlaybackRenderer::processBlock;

    private:
        CompCaptainAraDocument& doc;
        juce::SharedResourcePointer<AraReadThread> readThread;
        std::map<juce::ARAAudioSource*, std::unique_ptr<juce::AudioFormatReader>> readers;
        std::map<juce::ARAAudioSource*, juce::BufferingAudioReader*> bufferedReaders;
        juce::AudioBuffer<float> scratch;
        double sampleRate = 48000.0;
        int maxSamples = 512, numChannels = 2;
        bool buffered = true;
    };
}

CompCaptainAraDocument::CompCaptainAraDocument (const ARA::PlugIn::PlugInEntry* entry, const ARA::ARADocumentControllerHostInstance* instance)
    : juce::ARADocumentControllerSpecialisation (entry, instance)
{
    startTimerHz (15);
}

CompCaptainAraDocument::~CompCaptainAraDocument()
{
    stopTimer();
}

void CompCaptainAraDocument::willBeginEditing (juce::ARADocument*)
{
    processLock.enterWrite(); // the renderer goes quiet while the DAW changes the takes
}

void CompCaptainAraDocument::didEndEditing (juce::ARADocument*)
{
    processLock.exitWrite();
    dirty = true;
}

void CompCaptainAraDocument::didNotifyModelUpdates (juce::ARADocument*) { dirty = true; }
void CompCaptainAraDocument::doUpdateMusicalContextContent (juce::ARAMusicalContext*, juce::ARAContentUpdateScopes) { dirty = true; }
void CompCaptainAraDocument::didEnableAudioSourceSamplesAccess (juce::ARAAudioSource*, bool) { dirty = true; }

void CompCaptainAraDocument::willDestroyAudioSource (juce::ARAAudioSource* source)
{
    if (auto it = overviews.find (source); it != overviews.end())
    {
        retired.push_back (std::move (it->second));
        overviews.erase (it);
    }
    dirty = true;
}

juce::ARAPlaybackRenderer* CompCaptainAraDocument::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer (getDocumentController(), *this);
}

bool CompCaptainAraDocument::doStoreObjectsToStream (juce::ARAOutputStream& out, const juce::ARAStoreObjectsFilter* filter) noexcept
{
    // What we remember per take: its role and heart (the DAW keeps the audio and where it sits).
    const auto& mods = filter->getAudioModificationsToStore<juce::ARAAudioModification>();
    std::vector<std::pair<std::string, Prefs>> toStore;
    for (auto* m : mods)
        if (auto it = prefs.find (m->getPersistentID()); it != prefs.end())
            toStore.emplace_back (it->first, it->second);

    if (! out.writeInt (1) || ! out.writeInt64 ((juce::int64) toStore.size()))
        return false;
    for (auto& [id, p] : toStore)
    {
        if (! out.writeString (juce::String::fromUTF8 (id.c_str())) || ! out.writeString (juce::String (std::string (cc::toName (p.role))))
            || ! out.writeBool (p.roleChosen) || ! out.writeBool (p.favourite))
            return false;
    }
    return true;
}

bool CompCaptainAraDocument::doRestoreObjectsFromStream (juce::ARAInputStream& in, const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const int formatVersion = in.readInt();
    if (formatVersion != 1)
        return ! in.failed(); // a newer archive we can't read: keep the defaults
    const auto count = in.readInt64();
    for (juce::int64 i = 0; i < count && ! in.failed(); ++i)
    {
        const auto id = in.readString();
        const auto roleName = in.readString();
        Prefs p;
        p.roleChosen = in.readBool();
        p.favourite = in.readBool();
        cc::fromName (roleName.toStdString(), p.role);
        if (auto* m = filter->getAudioModificationToRestoreStateWithID<juce::ARAAudioModification> (id.toRawUTF8()))
            prefs[m->getPersistentID()] = p;
    }
    dirty = true;
    return ! in.failed();
}

void CompCaptainAraDocument::tellHostToSave()
{
    if (auto* dc = getDocumentController())
        if (auto* updates = dc->getHostModelUpdateController())
            updates->notifyDocumentDataChanged();
}

bool CompCaptainAraDocument::setRole (const juce::String& key, cc::Role role)
{
    for (auto& t : snapshot)
    {
        if (t.key == key)
        {
            auto& p = prefs[t.modificationId.toStdString()];
            p.role = role;
            p.roleChosen = true;
            p.favourite = t.take.favourite;
            dirty = true;
            rebuild();
            tellHostToSave();
            return true;
        }
    }
    return false;
}

bool CompCaptainAraDocument::setFavourite (const juce::String& key, bool on)
{
    for (auto& t : snapshot)
    {
        if (t.key == key)
        {
            auto& p = prefs[t.modificationId.toStdString()];
            if (! p.roleChosen)
                p.role = t.take.role;
            p.favourite = on;
            dirty = true;
            rebuild();
            tellHostToSave();
            return true;
        }
    }
    return false;
}

const std::vector<std::uint8_t>* CompCaptainAraDocument::peaks (const juce::String& key) const
{
    const auto s = sourceOfKey.find (key);
    if (s == sourceOfKey.end())
        return nullptr;
    const auto o = overviews.find (s->second);
    if (o == overviews.end() || ! o->second->done)
        return nullptr;
    return &o->second->peaks;
}

void CompCaptainAraDocument::rebuild()
{
    dirty = false;
    std::vector<AraTake> next;
    std::map<juce::String, juce::ARAAudioSource*> sources;
    std::map<const void*, juce::String> keys;
    tempoEntries = barSignatures = 0;

    auto* document = getDocument<juce::ARADocument>();
    if (document == nullptr)
    {
        snapshot.clear();
        ++snapshotVersion;
        return;
    }
    documentName = fromUtf8 (document->getName());

    auto sequences = document->getRegionSequences<juce::ARARegionSequence>();
    std::stable_sort (sequences.begin(), sequences.end(), [] (auto* a, auto* b) { return a->getOrderIndex() < b->getOrderIndex(); });

    for (auto* seq : sequences)
    {
        const auto trackName = fromUtf8 (seq->getName());
        for (auto* region : seq->getPlaybackRegions<juce::ARAPlaybackRegion>())
        {
            auto* mod = region->getAudioModification<juce::ARAAudioModification>();
            auto* source = mod->getAudioSource<juce::ARAAudioSource>();

            AraTake a;
            a.region = region;
            if (auto k = regionKeys.find (region); k != regionKeys.end())
                a.key = k->second;
            else
                a.key = "a" + juce::String (nextKey++);
            keys[region] = a.key;
            sources[a.key] = source;

            a.trackName = trackName;
            a.trackOrder = seq->getOrderIndex();
            a.fileName = fromUtf8 (source->getName());
            a.modificationId = juce::String::fromUTF8 (mod->getPersistentID().c_str());
            a.sourceRate = source->getSampleRate();
            a.sourceChannels = (int) source->getChannelCount();
            a.samplesReady = source->isSampleAccessEnabled();

            cc::Take& t = a.take;
            t.source = cc::TakeSource::ara;
            t.track = trackName.toStdString();
            auto name = fromUtf8 (region->getName());
            if (name.isEmpty())
                name = fromUtf8 (mod->getName());
            if (name.isEmpty())
                name = a.fileName.upToLastOccurrenceOf (".", false, false);
            if (name.isEmpty())
                name = "Lane take " + juce::String (next.size() + 1);
            t.name = name.toStdString();
            t.file = a.fileName.toStdString();
            t.sampleRate = source->getSampleRate();
            t.channels = (int) source->getChannelCount();
            t.lengthSamples = (std::int64_t) std::llround (region->getDurationInPlaybackTime() * t.sampleRate);

            // Where it sits: the song's own tempo and bar map (exact, even with tempo changes).
            const double startTime = region->getStartInPlaybackTime();
            t.start.seconds = startTime;
            t.start.bpm = 120.0;
            t.start.ppq = startTime * 2.0;
            if (auto* mc = seq->getMusicalContext<juce::ARAMusicalContext>())
            {
                const ARA::PlugIn::HostContentReader<ARA::kARAContentTypeTempoEntries> tempoReader (mc);
                const ARA::PlugIn::HostContentReader<ARA::kARAContentTypeBarSignatures> barReader (mc);
                if (tempoReader && tempoReader.getEventCount() >= 2)
                {
                    tempoEntries = std::max (tempoEntries, (int) tempoReader.getEventCount());
                    const ARA::TempoConverter<decltype (tempoReader)> tempo (tempoReader);
                    t.start.ppq = tempo.getQuarterForTime (startTime);
                    const double q1 = tempo.getQuarterForTime (startTime + 0.25);
                    t.start.bpm = cc::clampBpm ((q1 - t.start.ppq) * 240.0);
                    a.tempoFromHost = true;
                    if (barReader && barReader.getEventCount() >= 1)
                    {
                        barSignatures = std::max (barSignatures, (int) barReader.getEventCount());
                        const ARA::BarSignaturesConverter<decltype (barReader)> bars (barReader);
                        const auto sig = bars.getBarSignatureForQuarter (t.start.ppq);
                        t.start.meter = cc::Meter { (int) sig.numerator, (int) sig.denominator };
                        if (! t.start.meter.isValid())
                            t.start.meter = {};
                        const int barIndex = bars.getBarIndexForQuarter (t.start.ppq);
                        t.start.barStartPpq = bars.getQuarterForBarIndex (barIndex);
                        t.start.hasBarStart = true;
                        t.start.barNumber = barIndex >= 0 ? barIndex + 1 : 0;
                    }
                }
            }

            // Role: what you picked, else a guess from the track's name, else from the take's name.
            const auto guess = cc::guessRole (t.track);
            t.role = guess.value_or (cc::guessRole (t.name).value_or (cc::Role::lead));
            if (auto p = prefs.find (mod->getPersistentID()); p != prefs.end())
            {
                if (p->second.roleChosen)
                {
                    t.role = p->second.role;
                    t.roleChosen = true;
                }
                t.favourite = p->second.favourite;
            }

            next.push_back (std::move (a));

            if (overviews.count (source) == 0)
                overviews[source] = std::make_unique<SourceOverview>();
        }
    }

    // forget waveform pictures of audio nobody uses any more
    for (auto it = overviews.begin(); it != overviews.end();)
    {
        bool used = false;
        for (auto& [k, s] : sources)
            used = used || s == it->first;
        if (! used)
        {
            retired.push_back (std::move (it->second));
            it = overviews.erase (it);
        }
        else
        {
            ++it;
        }
    }

    for (size_t i = 0; i < next.size(); ++i)
        if (next[i].take.name.empty())
            next[i].take.name = "Lane take " + std::to_string (i + 1);
    for (size_t i = 0; i < next.size(); ++i)
        next[i].take.id = (std::uint32_t) (i + 1);

    snapshot = std::move (next);
    regionKeys = std::move (keys);
    sourceOfKey = std::move (sources);
    ++snapshotVersion;
}

void CompCaptainAraDocument::readSomeAudio()
{
    // About two seconds of audio per tick, spread over whatever still needs a picture.
    int budget = 96000;
    for (auto& [source, ov] : overviews)
    {
        if (ov->done || budget <= 0)
            continue;
        if (! source->isSampleAccessEnabled())
        {
            ov->reader.reset();
            continue;
        }
        if (ov->reader == nullptr)
        {
            ov->reader = std::make_unique<juce::ARAAudioSourceReader> (source);
            ov->builder.reset (source->getSampleRate());
            ov->position = 0;
        }
        if (! ov->reader->isValid())
        {
            ov->reader.reset();
            continue;
        }

        const auto total = (juce::int64) source->getSampleCount();
        const int channels = std::max (1, std::min (2, (int) source->getChannelCount()));
        juce::AudioBuffer<float> buf (channels, 16384);
        while (budget > 0 && ov->position < total)
        {
            const int n = (int) std::min<juce::int64> (16384, total - ov->position);
            if (! ov->reader->read (&buf, 0, n, ov->position, true, channels > 1))
            {
                ov->reader.reset(); // try again later
                break;
            }
            ov->builder.add (buf.getArrayOfReadPointers(), channels, n);
            ov->position += n;
            budget -= n;
        }
        if (ov->reader != nullptr && ov->position >= total)
        {
            ov->builder.finish();
            ov->peaks = ov->builder.peaks();
            ov->done = true;
            ov->reader.reset();
            ++snapshotVersion; // tells the window there's a new picture
        }
    }
}

void CompCaptainAraDocument::timerCallback()
{
    retired.clear();
    if (dirty)
        rebuild();
    readSomeAudio();
}

juce::String CompCaptainAraDocument::report (const std::vector<const void*>& mine, const juce::String& roles) const
{
    auto* document = const_cast<CompCaptainAraDocument*> (this)->getDocument<juce::ARADocument>();
    juce::String r;
    r << "What the DAW sent to Comp Captain (ARA)\n";
    r << "Comp Captain " << JucePlugin_VersionString << ", " << juce::PluginHostType().getHostDescription() << "\n";
    r << "Project: " << (documentName.isNotEmpty() ? documentName : juce::String ("(no name given)")) << "\n";
    r << "This copy of Comp Captain is: " << roles << "\n\n";

    if (document == nullptr)
        return r << "No ARA document.\n";

    const auto& seqs = document->getRegionSequences();
    const auto& sources = document->getAudioSources();
    r << "Tracks / lanes the DAW shared (region sequences): " << (int) seqs.size() << "\n";
    r << "Audio files shared (audio sources): " << (int) sources.size() << "\n";
    r << "Takes (playback regions): " << (int) snapshot.size() << ", of which this copy plays " << (int) mine.size() << "\n";
    r << "Song tempo map: " << (tempoEntries >= 2 ? juce::String (tempoEntries) + " tempo points, " + juce::String (barSignatures) + " time signatures" : juce::String ("not shared (bars are guessed at 120 BPM)")) << "\n";
    if (rateMismatch.load())
        r << "Note: some takes have a different sample rate from the project; they play silent in this version.\n";
    r << "\n";

    juce::String lastTrack = "\x01";
    for (auto& t : snapshot)
    {
        if (t.trackName != lastTrack)
        {
            r << "Track \"" << (t.trackName.isNotEmpty() ? t.trackName : juce::String ("(no name)")) << "\" (order " << t.trackOrder << ")\n";
            lastTrack = t.trackName;
        }
        const auto bb = t.take.startBarBeat();
        const bool plays = std::find (mine.begin(), mine.end(), t.region) != mine.end();
        const int secs = (int) std::lround (t.take.lengthSeconds());
        r << "  - \"" << juce::String (t.take.name) << "\": bar " << juce::String (bb.toText())
          << " (" << juce::String (t.take.start.seconds, 3) << " s), " << secs / 60 << ":" << juce::String (secs % 60).paddedLeft ('0', 2)
          << " long, file \"" << t.fileName << "\", " << juce::String ((int) t.sourceRate) << " Hz, "
          << (t.sourceChannels == 1 ? juce::String ("mono") : juce::String (t.sourceChannels) + " channels")
          << ", audio readable: " << (t.samplesReady ? "yes" : "no") << (plays ? ", played here" : "") << "\n";
    }

    int unused = 0;
    for (auto* s : sources)
    {
        bool used = false;
        for (auto& [k, src] : sourceOfKey)
            used = used || src == s;
        if (! used)
            ++unused;
    }
    if (unused > 0)
        r << "\nAudio files shared but not on any track: " << unused << "\n";
    return r;
}

#endif
