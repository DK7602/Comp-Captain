// ARA test: plays a pretend DAW that uses Comp Captain as an ARA Extension (like Audio > Extensions in
// Cubase). It hands over one take on a lane at bar 5, then checks that Comp Captain plays exactly that
// take at exactly that place, follows the take when it's moved, saves its part of the project, and
// shuts down cleanly.
//   CompCaptainAraTest <path to Comp Captain.vst3>
// Exit code 0 = everything passed.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <map>
#include <mutex>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

    constexpr double rate = 48000.0;
    constexpr int takeLength = (int) (3 * rate); // a three-second take
    float takeSample (int channel, int i) { return (channel == 0 ? 0.5f : -0.25f) * (float) std::sin (i * 0.0123 + channel); }

    // ---------------------------------------------------------------- the pretend DAW's side of ARA

    struct TakeAudio
    {
        // Like the ARA rules ask: anything before the start or past the end is silence, not an error.
        bool read (float* const* buffers, juce::int64 start, juce::int64 count) const
        {
            for (int c = 0; c < 2; ++c)
                for (juce::int64 i = 0; i < count; ++i)
                    buffers[c][i] = (start + i) >= 0 && (start + i) < takeLength ? takeSample (c, (int) (start + i)) : 0.0f;
            return true;
        }
    };

    class AudioAccess final : public ARA::Host::AudioAccessControllerInterface
    {
    public:
        explicit AudioAccess (const TakeAudio& t) : take (t) {}

        ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef, bool use64Bit) noexcept override
        {
            const std::scoped_lock lock (mutex);
            auto reader = std::make_unique<bool> (use64Bit);
            auto ref = reinterpret_cast<ARA::ARAAudioReaderHostRef> (reader.get());
            readers[ref] = std::move (reader);
            ++readersMade;
            return ref;
        }

        bool readAudioSamples (ARA::ARAAudioReaderHostRef ref, ARA::ARASamplePosition start, ARA::ARASampleCount count, void* const* buffers) noexcept override
        {
            {
                const std::scoped_lock lock (mutex);
                const auto it = readers.find (ref);
                if (it == readers.end() || *it->second)
                    return false; // unknown reader, or 64-bit samples (we only offer 32-bit)
                ++reads;
                if (start < 0 || start + count > takeLength)
                    ++readsOutside;
            }
            return take.read (reinterpret_cast<float* const*> (buffers), start, count);
        }

        void destroyAudioReader (ARA::ARAAudioReaderHostRef ref) noexcept override
        {
            const std::scoped_lock lock (mutex);
            readers.erase (ref);
        }

        std::atomic<int> readersMade { 0 }, reads { 0 }, readsOutside { 0 };

    private:
        const TakeAudio& take;
        std::mutex mutex;
        std::map<ARA::ARAAudioReaderHostRef, std::unique_ptr<bool>> readers;
    };

    class Archiving final : public ARA::Host::ArchivingControllerInterface
    {
    public:
        ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef ref) noexcept override
        {
            return (ARA::ARASize) reinterpret_cast<juce::MemoryBlock*> (ref)->getSize();
        }
        bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef ref, ARA::ARASize pos, ARA::ARASize len, ARA::ARAByte* buffer) noexcept override
        {
            auto* block = reinterpret_cast<juce::MemoryBlock*> (ref);
            if (pos + len > block->getSize())
                return false;
            std::memcpy (buffer, juce::addBytesToPointer (block->getData(), pos), len);
            return true;
        }
        bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef ref, ARA::ARASize pos, ARA::ARASize len, const ARA::ARAByte* buffer) noexcept override
        {
            auto* out = reinterpret_cast<juce::MemoryOutputStream*> (ref);
            return out->setPosition ((juce::int64) pos) && out->write (buffer, len);
        }
        void notifyDocumentArchivingProgress (float) noexcept override {}
        void notifyDocumentUnarchivingProgress (float) noexcept override {}
        ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef) noexcept override { return nullptr; }
    };

    // The song: 120 BPM in 4/4, so bar 5 starts at 8 seconds.
    class SongMap final : public ARA::Host::ContentAccessControllerInterface
    {
    public:
        bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type) noexcept override
        {
            return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
        }
        ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef, ARA::ARAContentType) noexcept override { return ARA::kARAContentGradeAdjusted; }
        ARA::ARAContentReaderHostRef createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type, const ARA::ARAContentTimeRange*) noexcept override
        {
            return reinterpret_cast<ARA::ARAContentReaderHostRef> ((size_t) type + 1);
        }
        bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return false; }
        ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return ARA::kARAContentGradeInitial; }
        ARA::ARAContentReaderHostRef createAudioSourceContentReader (ARA::ARAAudioSourceHostRef, ARA::ARAContentType, const ARA::ARAContentTimeRange*) noexcept override { return nullptr; }
        ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef ref) noexcept override
        {
            return typeOf (ref) == ARA::kARAContentTypeTempoEntries ? 2 : 1;
        }
        const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef ref, ARA::ARAInt32 index) noexcept override
        {
            if (typeOf (ref) == ARA::kARAContentTypeTempoEntries)
            {
                tempo.timePosition = index == 0 ? 0.0 : 2.0;
                tempo.quarterPosition = index == 0 ? 0.0 : 4.0;
                return &tempo;
            }
            bar.position = 0.0;
            bar.numerator = 4;
            bar.denominator = 4;
            return &bar;
        }
        void destroyContentReader (ARA::ARAContentReaderHostRef) noexcept override {}

    private:
        static ARA::ARAContentType typeOf (ARA::ARAContentReaderHostRef ref) { return (ARA::ARAContentType) ((size_t) ref - 1); }
        ARA::ARAContentTempoEntry tempo {};
        ARA::ARAContentBarSignature bar {};
    };

    class ModelUpdates final : public ARA::Host::ModelUpdateControllerInterface
    {
    public:
        void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState, float) noexcept override {}
        void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
        void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
        void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
        void notifyDocumentDataChanged() noexcept override {}
    };

    struct Transport final : public juce::AudioPlayHead
    {
        juce::int64 sample = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (true);
            p.setTimeInSamples (sample);
            p.setTimeInSeconds ((double) sample / rate);
            p.setPpqPosition ((double) sample / rate * 2.0);
            p.setBpm (120.0);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return p;
        }
    };

    // Plays the song from `from` seconds for `seconds`, returns what Comp Captain put out.
    juce::AudioBuffer<float> play (juce::AudioPluginInstance& plugin, Transport& transport, double from, double seconds)
    {
        const int total = (int) (seconds * rate);
        juce::AudioBuffer<float> out (2, total), block (2, 512);
        juce::MidiBuffer midi;
        transport.sample = (juce::int64) (from * rate);
        for (int at = 0; at < total; at += 512)
        {
            const int n = std::min (512, total - at);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
            view.clear();
            plugin.processBlock (view, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, at, view, c, 0, n);
            transport.sample += n;
        }
        return out;
    }

    // Is the take, exactly, in `out` starting `offset` samples in, with silence before it?
    bool takeIsAt (const juce::AudioBuffer<float>& out, int offset)
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const int t = i - offset;
                const float expected = (t >= 0 && t < takeLength) ? takeSample (c, t) : 0.0f;
                if (! juce::exactlyEqual (out.getSample (c, i), expected))
                {
                    std::cout << "        first difference: channel " << c << ", sample " << i << " (" << out.getSample (c, i) << " vs " << expected << ")" << std::endl;
                    return false;
                }
            }
        }
        return true;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: CompCaptainAraTest <path to Comp Captain.vst3>" << std::endl;
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    std::cout << "Comp Captain ARA test: " << pluginFile.getFullPathName() << std::endl;

    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (found, pluginFile.getFullPathName());
    if (found.isEmpty())
    {
        check (false, "found the plug-in");
        return 1;
    }
    check (found[0]->hasARAExtension, "it offers ARA (shows up under Audio > Extensions)");

    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], rate, 512, error);
    check (plugin != nullptr, "loads");
    if (plugin == nullptr)
        return 1;
    // A DAW keeps the plug-in's file loaded until the song is closed; a second copy does that here.
    auto keepLoaded = formats.createPluginInstance (*found[0], rate, 512, error);

    // 1. Ask for its ARA side, like a DAW does when you pick it from Extensions.
    juce::ARAFactoryWrapper factory;
    bool factoryReady = false;
    juce::createARAFactoryAsync (*plugin, [&] (juce::ARAFactoryWrapper f) { factory = std::move (f); factoryReady = true; });
    for (int i = 0; i < 200 && ! factoryReady; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (factory.get() != nullptr, "hands over its ARA factory");
    if (factory.get() == nullptr)
        return 1;

    TakeAudio takeAudio;
    auto audioAccessOwned = std::make_unique<AudioAccess> (takeAudio);
    auto* audioAccess = audioAccessOwned.get();
    auto document = juce::ARAHostDocumentController::create (std::move (factory), "Test Song", std::move (audioAccessOwned),
                                                             std::make_unique<Archiving>(), std::make_unique<SongMap>(),
                                                             std::make_unique<ModelUpdates>());
    check (document != nullptr, "an ARA document opens");
    if (document == nullptr)
        return 1;
    auto& dc = document->getDocumentController();

    // 2. The song: one track ("Lead Vox") with one take ("Lead Vox_01") at bar 5.
    auto contextProps = juce::ARAHostModel::MusicalContext::getEmptyProperties();
    contextProps.name = "Song";
    auto context = std::make_unique<juce::ARAHostModel::MusicalContext> (reinterpret_cast<ARA::ARAMusicalContextHostRef> (1), dc, contextProps);

    auto sequenceProps = juce::ARAHostModel::RegionSequence::getEmptyProperties();
    sequenceProps.name = "Lead Vox";
    sequenceProps.orderIndex = 0;
    sequenceProps.musicalContextRef = context->getPluginRef();
    auto sequence = std::make_unique<juce::ARAHostModel::RegionSequence> (reinterpret_cast<ARA::ARARegionSequenceHostRef> (2), dc, sequenceProps);

    auto sourceProps = juce::ARAHostModel::AudioSource::getEmptyProperties();
    sourceProps.name = "Lead Vox_01.wav";
    sourceProps.persistentID = "source-1";
    sourceProps.sampleCount = takeLength;
    sourceProps.sampleRate = rate;
    sourceProps.channelCount = 2;
    sourceProps.merits64BitSamples = false;
    auto source = std::make_unique<juce::ARAHostModel::AudioSource> (reinterpret_cast<ARA::ARAAudioSourceHostRef> (3), dc, sourceProps);
    source->enableAudioSourceSamplesAccess (true);

    auto modProps = juce::ARAHostModel::AudioModification::getEmptyProperties();
    modProps.name = "Lead Vox_01";
    modProps.persistentID = "modification-1";
    auto modification = std::make_unique<juce::ARAHostModel::AudioModification> (reinterpret_cast<ARA::ARAAudioModificationHostRef> (4), dc, *source, modProps);

    auto regionProps = juce::ARAHostModel::PlaybackRegion::getEmptyProperties();
    regionProps.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
    regionProps.startInModificationTime = 0.0;
    regionProps.durationInModificationTime = takeLength / rate;
    regionProps.startInPlaybackTime = 8.0; // bar 5
    regionProps.durationInPlaybackTime = takeLength / rate;
    regionProps.musicalContextRef = context->getPluginRef();
    regionProps.regionSequenceRef = sequence->getPluginRef();
    regionProps.name = "Lead Vox_01";
    auto region = std::make_unique<juce::ARAHostModel::PlaybackRegion> (reinterpret_cast<ARA::ARAPlaybackRegionHostRef> (5), dc, *modification, regionProps);

    // 3. Bind the plug-in to the document with every ARA job, and give it the take to play.
    const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
    auto extension = document->bindDocumentToPluginInstance (*plugin, roles, roles);
    check (extension.isValid(), "binds to the document as playback renderer, editor renderer and editor view");
    auto renderer = std::make_unique<juce::ARAHostModel::PlaybackRendererInterface> (extension.getPlaybackRendererInterface());
    renderer->add ((*region));

    // Let its window side read the take (for the waveform picture), like a DAW's idle time.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
    check (audioAccess->readersMade.load() > 0 && audioAccess->reads.load() > 0, "reads the take's audio through the DAW (for its waveform)");

    // 4. Play from bar 4 (6 s) for six seconds, offline (like an export) so every sample is exact.
    juce::AudioPluginInstance::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    plugin->setBusesLayout (layout);
    plugin->setNonRealtime (true);
    plugin->setRateAndBufferSizeDetails (rate, 512);
    Transport transport;
    plugin->setPlayHead (&transport);
    plugin->prepareToPlay (rate, 512);
    {
        const auto out = play (*plugin, transport, 6.0, 6.0);
        check (takeIsAt (out, (int) (2.0 * rate)), "plays the take exactly, starting exactly at bar 5, silent around it");
    }
    plugin->releaseResources();

    // 5. Move the take to bar 9 (16 s): Comp Captain follows.
    regionProps.startInPlaybackTime = 16.0;
    (*region).update (regionProps);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    plugin->prepareToPlay (rate, 512);
    {
        const auto out = play (*plugin, transport, 15.0, 5.0);
        check (takeIsAt (out, (int) (1.0 * rate)), "after the take is moved to bar 9, it plays there instead");
        const auto before = play (*plugin, transport, 7.0, 4.0);
        check (! (before.getMagnitude (0, before.getNumSamples()) > 0.0f), "and nothing plays at bar 5 any more");
    }
    plugin->releaseResources();

    // 6. Real-time playback (like pressing Play): the audio is read ahead in the background.
    plugin->setNonRealtime (false);
    plugin->prepareToPlay (rate, 512);
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300); // the read-ahead fills up
        const auto out = play (*plugin, transport, 16.0, 1.0);
        check (out.getMagnitude (0, out.getNumSamples()) > 0.1f, "plays in real time too");
    }
    plugin->releaseResources();

    // 7. Save the project's ARA part, then open the window in ARA mode.
    {
        juce::MemoryOutputStream archive;
        dc.beginEditing();
        dc.endEditing();
        const bool stored = dc.storeObjectsToArchive (reinterpret_cast<ARA::ARAArchiveWriterHostRef> (&archive), nullptr);
        check (stored, "saves its part of the project (" + juce::String ((int) archive.getDataSize()) + " bytes)");
    }
    {
        // Open on the Record page, where the take list is. CC_ARA_TEST_WAIT=<ms> keeps the window up longer.
        juce::MemoryBlock host;
        plugin->getStateInformation (host);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize()))
        {
            if (auto* c = xml->getChildByName ("IComponent"))
            {
                const juce::String ours = "CompCaptainState\ncompcaptain-ui=1\npage=record\nwindowScale=0.80\n";
                c->deleteAllTextElements();
                c->addTextElement (juce::MemoryBlock (ours.toRawUTF8(), ours.getNumBytesAsUTF8()).toBase64Encoding());
                juce::MemoryBlock out;
                juce::AudioProcessor::copyXmlToBinary (*xml, out);
                plugin->setStateInformation (out.getData(), (int) out.getSize());
            }
        }
        const int wait = juce::SystemStats::getEnvironmentVariable ("CC_ARA_TEST_WAIT", "2500").getIntValue();
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
        check (editor != nullptr && editor->isResizable(), "its window opens in ARA mode and can be resized");
        if (editor != nullptr)
        {
            editor->setTopLeftPosition (40, 40);
            editor->addToDesktop (0);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (wait);
            check (editor->isShowing(), "the window stays open with the lane takes");
        }
        editor.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    }

    check (audioAccess->readsOutside.load() == 0, "never asks the DAW for audio past the start or end of a take");

    // 8. The DAW takes the take away and closes the song: everything shuts down cleanly.
    auto step = [] (const char* what) { std::cout << "        closing: " << what << std::endl; };
    step ("the take leaves the renderer");
    // Take it out explicitly first: JUCE's host helper (PlaybackRegionRegistry) erases from its
    // list while walking it when it's destroyed with takes still in it, which crashes on macOS.
    // That's test-host code, not Comp Captain; a DAW removes its takes before letting go anyway.
    renderer->remove (*region);
    renderer.reset();
    step ("the take");
    region.reset();
    step ("its audio modification");
    modification.reset();
    step ("its audio source");
    source.reset();
    step ("the track");
    sequence.reset();
    step ("the song's tempo map");
    context.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    step ("the plug-in");
    plugin.reset();
    step ("the ARA document");
    document.reset();
    step ("the plug-in file");
    keepLoaded.reset();
    check (true, "closes the song without a crash");

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
