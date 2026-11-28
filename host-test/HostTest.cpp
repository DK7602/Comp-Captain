// Host test: loads "Comp Captain.vst3" like a DAW and checks its promises: it passes audio through
// untouched, CAPTURE turns a recorded pass into a take on the right bar, everything is saved with the
// project, the song's lyrics are shared by every copy, and a sung take is listened to in the background.
//   CompCaptainHostTest <path to Comp Captain.vst3> [--no-window]
// Exit code 0 = everything passed.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

    // Plays noise through the plug-in in blocks of changing size and checks it comes out bit for bit.
    bool passesAudioUntouched (juce::AudioPluginInstance& plugin, int channels)
    {
        juce::AudioPluginInstance::BusesLayout layout;
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        if (! plugin.setBusesLayout (layout))
            return false;

        plugin.setRateAndBufferSizeDetails (48000.0, 512);
        plugin.prepareToPlay (48000.0, 512);

        juce::Random random (1234);
        juce::MidiBuffer midi;
        bool identical = true;
        for (int block = 0; block < 300 && identical; ++block)
        {
            const int n = 1 + random.nextInt (512);
            juce::AudioBuffer<float> buffer (channels, n), original (channels, n);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);
            original.makeCopyOf (buffer);
            plugin.processBlock (buffer, midi);
            for (int c = 0; c < channels && identical; ++c)
                identical = std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * (size_t) n) == 0;
        }
        plugin.releaseResources();
        return identical;
    }

    // A DAW keeps the plug-in's saved data inside its own wrapper (XML with the component state in
    // base64). These unwrap and rewrap it, so we can read and change our own text inside.
    std::string componentState (juce::AudioPluginInstance& plugin)
    {
        juce::MemoryBlock host;
        plugin.getStateInformation (host);
        juce::MemoryBlock component;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize()))
            if (auto* c = xml->getChildByName ("IComponent"))
                component.fromBase64Encoding (c->getAllSubText());
        return { static_cast<const char*> (component.getData()), component.getSize() };
    }

    void setComponentState (juce::AudioPluginInstance& plugin, const std::string& component)
    {
        juce::MemoryBlock host;
        plugin.getStateInformation (host);
        auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize());
        if (xml == nullptr)
            return;
        if (auto* c = xml->getChildByName ("IComponent"))
        {
            c->deleteAllTextElements();
            c->addTextElement (juce::MemoryBlock (component.data(), component.size()).toBase64Encoding());
        }
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        plugin.setStateInformation (out.getData(), (int) out.getSize());
    }

    bool replace (std::string& s, const std::string& from, const std::string& to)
    {
        const auto at = s.find (from);
        if (at == std::string::npos)
            return false;
        s.replace (at, from.size(), to);
        return true;
    }

    bool has (const std::string& s, const char* what) { return s.find (what) != std::string::npos; }

    // A pretend DAW transport at 120 BPM in 4/4.
    struct FakeTransport final : public juce::AudioPlayHead
    {
        double rate = 48000.0;
        juce::int64 sample = 0;
        bool playing = true, recording = false;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (playing);
            p.setIsRecording (recording);
            p.setTimeInSamples (sample);
            p.setTimeInSeconds ((double) sample / rate);
            const double ppq = (double) sample / rate * 2.0;
            p.setPpqPosition (ppq);
            p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            p.setBpm (120.0);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return p;
        }
    };

    float sung (juce::int64 i) { return 0.4f * (float) std::sin ((double) i * 0.031); }

    // Arms CAPTURE, "records" two seconds from bar 5, and checks the take that comes out.
    void captureTest (juce::AudioPluginInstance& plugin, const juce::File& takesRoot)
    {
        juce::AudioProcessorParameter* capture = nullptr;
        for (auto* p : plugin.getParameters())
            if (p->getName (64) == "CAPTURE armed")
                capture = p;
        check (capture != nullptr, "has the CAPTURE switch");
        if (capture == nullptr)
            return;
        check (! capture->isAutomatable(), "CAPTURE stays out of the DAW's automation lanes");

        juce::AudioPluginInstance::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        plugin.setBusesLayout (layout);
        plugin.setRateAndBufferSizeDetails (48000.0, 512);
        plugin.prepareToPlay (48000.0, 512);

        FakeTransport daw;
        plugin.setPlayHead (&daw);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 512), original (2, 512);
        auto play = [&] (double seconds, bool& untouched) {
            const int blocks = (int) std::ceil (seconds * 48000.0 / 512.0);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, sung (daw.sample + i));
                    buffer.setSample (1, i, -sung (daw.sample + i));
                }
                original.makeCopyOf (buffer);
                plugin.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    untouched = untouched && std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * 512) == 0;
                daw.sample += 512;
            }
        };

        bool untouched = true;
        capture->setValueNotifyingHost (1.0f);
        play (0.1, untouched);                                   // the switch reaches the plug-in
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

        daw.sample = 8 * 48000;                                  // bar 5 at 120 BPM
        const auto firstSample = daw.sample;
        daw.recording = true;
        play (2.0, untouched);
        daw.recording = false;
        play (0.2, untouched);
        check (untouched, "audio still passes through bit for bit while recording a take");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500); // the take is written in the background

        const auto state = componentState (plugin);
        check (has (state, "name=Take 1") && has (state, "source=capture"), "the recorded pass became \"Take 1\"");
        check (has (state, "ppq=16.000000"), "Take 1 is stamped at bar 5 (16 beats in)");

        const auto files = takesRoot.findChildFiles (juce::File::findFiles, true, "Take 1.wav");
        check (files.size() == 1, "Take 1.wav is saved in the takes folder");
        if (files.size() == 1)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (files[0]));
            const juce::int64 expected = (juce::int64) std::ceil (2.0 * 48000.0 / 512.0) * 512;
            check (reader != nullptr && reader->lengthInSamples == expected && reader->numChannels == 2,
                   "Take 1.wav is stereo and exactly as long as the pass (" + juce::String (expected) + " samples)");
            if (reader != nullptr && reader->lengthInSamples == expected)
            {
                juce::AudioBuffer<float> got (2, (int) expected);
                reader->read (&got, 0, (int) expected, 0, true, true);
                bool same = true;
                for (int i = 0; i < (int) expected && same; ++i)
                    same = juce::exactlyEqual (got.getSample (0, i), sung (firstSample + i))
                           && juce::exactlyEqual (got.getSample (1, i), -sung (firstSample + i));
                check (same, "Take 1.wav holds exactly the sound that came through");
            }
        }

        capture->setValueNotifyingHost (0.0f);
        plugin.setPlayHead (nullptr);
        plugin.releaseResources();
    }
}

namespace
{
    // The song's lyrics: every copy of Comp Captain in the project shows and saves the same ones.
    void lyricsTest (juce::AudioPluginInstance& first, juce::AudioPluginInstance& second)
    {
        auto state = componentState (second);
        check (has (state, "[cc-lyrics]\nstamp="), "saves the song's lyrics with the project");
        const auto from = state.find ("[cc-lyrics]\n");
        const auto to = state.find ("[cc-takes]\n");
        if (from == std::string::npos || to == std::string::npos || to < from)
            return;
        const std::string lyrics = "[cc-lyrics]\nstamp=" + std::to_string (juce::Time::currentTimeMillis())
                                   + "\ntext=[Verse 1]\\nAnd so my fellow Americans\\nask not what your country can do for you\\n\n";
        state.replace (from, to - from, lyrics);
        setComponentState (second, state);
        check (has (componentState (second), "text=[Verse 1]\\nAnd so my fellow Americans"), "restores the song's lyrics");
        check (has (componentState (first), "text=[Verse 1]\\nAnd so my fellow Americans"), "every copy in the song gets the same lyrics");
    }

    // Records a few seconds of speech (whisper.cpp's public-domain sample) through CAPTURE and waits for
    // it to be listened to: the words heard are kept next to the take.
    void listeningTest (juce::AudioPluginInstance& plugin, const juce::File& takesRoot)
    {
#if defined (CC_SPEECH_SAMPLE)
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (CC_SPEECH_SAMPLE)));
        check (reader != nullptr, "reads the speech sample");
        if (reader == nullptr)
            return;
        const double rate = reader->sampleRate;
        juce::AudioBuffer<float> speech (1, (int) reader->lengthInSamples);
        reader->read (&speech, 0, (int) reader->lengthInSamples, 0, true, false);

        juce::AudioProcessorParameter* capture = nullptr;
        for (auto* p : plugin.getParameters())
            if (p->getName (64) == "CAPTURE armed")
                capture = p;
        if (capture == nullptr)
            return;
        juce::AudioPluginInstance::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        plugin.setBusesLayout (layout);
        plugin.setRateAndBufferSizeDetails (rate, 512);
        plugin.prepareToPlay (rate, 512);
        FakeTransport daw;
        daw.rate = rate;
        plugin.setPlayHead (&daw);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 512);
        auto play = [&] (int fromSample, int samples) {
            for (int done = 0; done < samples; done += 512)
            {
                buffer.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const int at = fromSample + done + i;
                    const float v = at >= 0 && at < speech.getNumSamples() ? speech.getSample (0, at) : 0.0f;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }
                plugin.processBlock (buffer, midi);
                daw.sample += 512;
            }
        };
        capture->setValueNotifyingHost (1.0f);
        play (-100000, 2048);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        daw.sample = (juce::int64) (16.0 * rate);               // bar 9
        daw.recording = true;
        play (0, speech.getNumSamples());
        daw.recording = false;
        play (-100000, 4096);
        capture->setValueNotifyingHost (0.0f);
        plugin.setPlayHead (nullptr);
        plugin.releaseResources();

        // the take is written, then listened to in the background (well within a minute)
        juce::File words;
        for (int waited = 0; waited < 120000 && words == juce::File(); waited += 250)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            const auto found = takesRoot.findChildFiles (juce::File::findFiles, true, "Take 2.words");
            if (! found.isEmpty())
                words = found[0];
        }
        check (words.existsAsFile(), "a sung take is listened to in the background (\"Take 2.words\" saved next to it)");
        const auto heard = words.loadFileAsString();
        check (heard.contains ("model=base.en-q5_1"), "the speech model inside the plug-in was found and used");
        check (heard.containsIgnoreCase ("country") && heard.containsIgnoreCase ("americans"), "it heard the words that were said");
        check (heard.contains ("phrase\t"), "it found the sung phrases");
        const auto measured = words.withFileExtension (".feat");
        check (measured.existsAsFile() && measured.getSize() > 1000, "it measured what the take sounds like, for the scores (\"Take 2.feat\")");
#else
        juce::ignoreUnused (plugin, takesRoot);
        std::cout << "  SKIP  listening (this build has no speech model)" << std::endl;
#endif
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: CompCaptainHostTest <path to Comp Captain.vst3> [--no-window]" << std::endl;
        return 2;
    }
    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const bool openWindow = ! (argc > 2 && juce::String (argv[2]) == "--no-window");

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "Comp Captain host test: " << pluginFile.getFullPathName() << std::endl;

    // Takes go to a scratch folder, not the real Documents folder.
    auto takesRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("COMP_CAPTAIN_TAKES_FOLDER", {}));
    if (takesRoot == juce::File())
    {
        takesRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CompCaptainHostTestTakes");
       #if JUCE_WINDOWS
        _wputenv_s (L"COMP_CAPTAIN_TAKES_FOLDER", takesRoot.getFullPathName().toWideCharPointer());
       #else
        setenv ("COMP_CAPTAIN_TAKES_FOLDER", takesRoot.getFullPathName().toRawUTF8(), 1);
       #endif
    }
    takesRoot.deleteRecursively();

    // 1. Scan
    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (found, pluginFile.getFullPathName());
    check (found.size() == 1, "scan finds exactly one plug-in");
    if (found.isEmpty())
        return 1;
    const auto desc = *found.getFirst();
    check (desc.name == "Comp Captain", "it is called Comp Captain (got \"" + desc.name + "\")");
    check (desc.manufacturerName == "DK7602", "made by DK7602");
    check (! desc.isInstrument, "it is an effect, not an instrument");

    // 2. Load
    juce::String error;
    auto plugin = formats.createPluginInstance (desc, 48000.0, 512, error);
    check (plugin != nullptr, "loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
    if (plugin == nullptr)
        return 1;
    check (plugin->getLatencySamples() == 0, "adds no delay");
    plugin->prepareToPlay (48000.0, 512);
    check (std::isinf (plugin->getTailLengthSeconds()), "asks to keep being called while the DAW is stopped (so a take can be heard from it)");
    plugin->releaseResources();

    // 3. Play: untouched, stereo and mono
    check (passesAudioUntouched (*plugin, 2), "stereo audio passes through bit for bit");
    check (passesAudioUntouched (*plugin, 1), "mono audio passes through bit for bit");

    // 4. CAPTURE: a recorded pass becomes a take on its bar
    captureTest (*plugin, takesRoot);

    // 5. Save and restore
    auto saved = componentState (*plugin);
    check (saved.rfind ("CompCaptainState\n", 0) == 0 && has (saved, "page=comp") && has (saved, "theme=dark"),
           "saves its state with the project (opens on the Comp page, dark look)");

    auto changed = saved;
    const bool edited = replace (changed, "theme=dark", "theme=light") && replace (changed, "page=comp", "page=review")
                        && replace (changed, "compView=line", "compView=song") && replace (changed, "windowScale=0.80", "windowScale=1.00");
    check (edited, "found the look, page and window size in the saved state");

    auto second = formats.createPluginInstance (desc, 48000.0, 512, error);
    check (second != nullptr, "a second copy loads");
    if (second != nullptr)
    {
        setComponentState (*second, changed);
        const auto restored = componentState (*second);
        check (has (restored, "theme=light") && has (restored, "page=review") && has (restored, "compView=song") && has (restored, "windowScale=1.00"),
               "restores a saved project (light look, Review page, Song view, full size)");
        check (has (restored, "name=Take 1") && has (restored, "ppq=16.000000"), "restores the takes with the project");

        setComponentState (*second, "not our state at all");
        check (has (componentState (*second), "theme=light"), "ignores state that isn't ours");

        // 5b. Lyrics belong to the song; sung takes are listened to
        lyricsTest (*plugin, *second);
        listeningTest (*plugin, takesRoot);
    }

    // 6. Open the window (the web interface), at the saved size, and let it load
    if (openWindow)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (second != nullptr ? second->createEditorAndMakeActive() : nullptr);
        check (editor != nullptr, "opens its window");
        if (editor != nullptr)
        {
            check (std::abs (editor->getWidth() - 1672) <= 2 && std::abs (editor->getHeight() - 941) <= 2,
                   "window opens at the saved size (got " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) + ")");
            check (editor->isResizable(), "window can be resized");
            editor->setTopLeftPosition (40, 40);
            editor->addToDesktop (0);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5000);
            check (editor->isShowing(), "window stays open while the interface loads");
            editor.reset(); // the editor tells the plug-in it is going away
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        }
    }
    else
    {
        std::cout << "  SKIP  window (--no-window)" << std::endl;
    }

    second.reset();
    plugin.reset();
    takesRoot.deleteRecursively();

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
