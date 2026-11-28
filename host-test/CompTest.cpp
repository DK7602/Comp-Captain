// Comp test (M4): the plug-in's own code, without a DAW or a window. Two takes of the same words (whisper.cpp's
// public-domain speech sample, the second one softer) are recorded through CAPTURE and listened to. Then the
// window's buttons are pressed in code: Build Performance writes the comp on its bar, a take is heard with the
// DAW stopped, the comp is heard in time while the DAW plays (never while recording), and Stop gives the track
// back bit for bit.
//   CompCaptainCompTest
// Exit code 0 = everything passed.
#include "../plugin/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <thread>
#include <type_traits>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

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

    // The test runs on its own thread (it plays the DAW's audio thread); everything else the window would do runs
    // on the message thread, like in a DAW.
    template <typename Fn>
    auto mt (Fn&& fn)
    {
        using R = decltype (fn());
        if constexpr (std::is_void_v<R>)
        {
            juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* f) -> void* { (*static_cast<Fn*> (f))(); return nullptr; }, &fn);
        }
        else
        {
            R result {};
            auto call = [&] { result = fn(); };
            juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* f) -> void* { (*static_cast<decltype (call)*> (f))(); return nullptr; }, &call);
            return result;
        }
    }

    template <typename Fn>
    bool waitFor (Fn&& done, int ms)
    {
        for (int waited = 0; waited < ms; waited += 100)
        {
            if (mt (done))
                return true;
            juce::Thread::sleep (100);
        }
        return mt (done);
    }

    void pause (int ms) { juce::Thread::sleep (ms); }

    int runTest();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "Comp Captain comp test" << std::endl;
    int result = 1;
    std::thread test ([&] {
        result = runTest();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    test.join();
    return result;
}

namespace
{
int runTest()
{

#if ! defined (CC_SPEECH_SAMPLE) || ! defined (CC_SPEECH_MODEL)
    std::cout << "  SKIP  (this build has no speech model)" << std::endl;
    return 0;
#else
    const auto takesRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CompCaptainCompTestTakes");
    takesRoot.deleteRecursively();
   #if JUCE_WINDOWS
    _wputenv_s (L"COMP_CAPTAIN_TAKES_FOLDER", takesRoot.getFullPathName().toWideCharPointer());
    _wputenv_s (L"COMP_CAPTAIN_SPEECH_MODEL", juce::String (CC_SPEECH_MODEL).toWideCharPointer());
   #else
    setenv ("COMP_CAPTAIN_TAKES_FOLDER", takesRoot.getFullPathName().toRawUTF8(), 1);
    setenv ("COMP_CAPTAIN_SPEECH_MODEL", CC_SPEECH_MODEL, 1);
   #endif

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (CC_SPEECH_SAMPLE)));
    check (reader != nullptr, "reads the speech sample");
    if (reader == nullptr)
        return 1;
    const double rate = reader->sampleRate;
    juce::AudioBuffer<float> speech (1, (int) reader->lengthInSamples);
    reader->read (&speech, 0, (int) reader->lengthInSamples, 0, true, false);

    std::unique_ptr<CompCaptainProcessor> plugin;
    mt ([&] { plugin = std::make_unique<CompCaptainProcessor>(); });
    auto& p = *plugin;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    mt ([&] {
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, 512);
        p.prepareToPlay (rate, 512);
    });
    FakeTransport daw;
    daw.rate = rate;
    p.setPlayHead (&daw);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 512);

    // plays the speech (at a gain) as the track's input from a song sample, or silence (fromSample < 0)
    auto play = [&] (int fromSample, int samples, float gain) {
        for (int done = 0; done < samples; done += 512)
        {
            buffer.clear();
            for (int i = 0; i < 512 && fromSample >= 0; ++i)
            {
                const int at = fromSample + done + i;
                const float v = at < speech.getNumSamples() ? gain * speech.getSample (0, at) : 0.0f;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p.processBlock (buffer, midi);
            daw.sample += 512;
            if (fromSample >= 0)
                pause (8); // about four times real time, so the takes writer keeps up (a DAW plays in real time)
        }
    };

    mt ([] { CompCaptainProcessor::setSongLyrics ("[Verse 1]\nAnd so my fellow Americans\nask not what your country can do for you\n"
                                         "ask what you can do for your country\n"); });

    // two takes: bar 9 (16 s) and again at bar 9, softer (a second pass of the same part)
    mt ([&] { p.setArmed (true); });
    play (-1, 2048, 0.0f);
    pause (300);
    for (const float gain : { 1.0f, 0.4f })
    {
        daw.playing = true;
        daw.sample = (juce::int64) (16.0 * rate);
        daw.recording = true;
        play (0, speech.getNumSamples(), gain);
        daw.recording = false;
        play (-1, 4096, 0.0f);
        daw.playing = false;
        play (-1, 2048, 0.0f);
        pause (500);
    }
    mt ([&] { p.setArmed (false); });
    check (waitFor ([&] { return p.getTakes().size() == 2; }, 10000), "two takes recorded");
    const auto takes = mt ([&] { return p.getTakes().all(); });
    if (takes.size() != 2)
        return 1;
    const auto t1 = takes[0].id, t2 = takes[1].id;
    // the DAW keeps running the plug-in while it's stopped; the test does too, so listening isn't held up
    check (waitFor ([&] { return p.takeHearingState (t1) == "done" && p.takeHearingState (t2) == "done"; }, 180000),
           "both takes listened to");
    check (waitFor ([&] { return p.scoresReady() && p.scores() != nullptr && ! p.scores()->lines.empty(); }, 60000), "the takes are scored");

    // the window's comp: take 1, take 2, take 1 (a join at every line)
    mt ([&] { p.setComp ({ { 0, t1 }, { 1, t2 }, { 2, t1 } }, 0.4); });
    check (std::abs (mt ([&] { return p.compPicky(); }) - 0.4) < 1e-9, "how picky the comp is, kept");

    // Build Performance
    mt ([&] { p.buildComp(); });
    check (waitFor ([&] { return ! p.buildingComp() && p.lastBuilt().has_value(); }, 30000), "Build Performance finishes");
    const auto built = mt ([&] { return p.lastBuilt(); });
    check (built.has_value() && built->ok, "it makes the comp file" + (built.has_value() ? " (" + built->message + ")" : juce::String()));
    if (built.has_value() && built->ok)
    {
        check (built->file.getFileName() == "Lead Comp - Verse 1 (bar 9).wav", "named for the track, the section and its bar (" + built->file.getFileName() + ")");
        check (built->file.isAChildOf (takesRoot), "saved in the takes folder");
        std::unique_ptr<juce::AudioFormatReader> comp (formats.createReaderFor (built->file));
        check (comp != nullptr && std::abs (comp->sampleRate - rate) < 1.0, "a WAV at the takes' sample rate");
        if (comp != nullptr)
        {
            juce::AudioBuffer<float> audio ((int) comp->numChannels, (int) comp->lengthInSamples);
            comp->read (&audio, 0, (int) comp->lengthInSamples, 0, true, true);
            const double seconds = (double) comp->lengthInSamples / rate;
            check (seconds > 9.0 && seconds < 13.5, "about as long as the speech, from the bar line (" + juce::String (seconds, 2) + " s)");
            float most = 0.0f, step = 0.0f, ownStep = 0.0f;
            for (int i = 1; i < audio.getNumSamples(); ++i)
            {
                most = std::max (most, std::abs (audio.getSample (0, i)));
                step = std::max (step, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
            }
            for (int i = 1; i < speech.getNumSamples(); ++i)
                ownStep = std::max (ownStep, std::abs (speech.getSample (0, i) - speech.getSample (0, i - 1)));
            check (most > 0.05f, "it has the singing in it");
            check (step <= ownStep * 1.6f + 0.002f, "no clicks: no step between samples bigger than the takes' own (" + juce::String (step, 4)
                                                       + " vs " + juce::String (ownStep, 4) + ")");
        }
    }
    mt ([&] { p.buildComp(); });
    waitFor ([&] { return ! p.buildingComp(); }, 30000);
    const auto again = mt ([&] { return p.lastBuilt(); });
    check (again.has_value() && again->file.getFileName() == "Lead Comp - Verse 1 (bar 9) 2.wav",
           "building again never overwrites the first file (the DAW may be using it)");

    // hearing take 2's second line with the DAW stopped: it plays from the plug-in, then stops by itself
    daw.playing = false;
    mt ([&] { p.hearTakeLine (t2, 1); });
    check (waitFor ([&] { return ! p.audition().preparing; }, 10000), "the take is ready to hear");
    float heard = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        heard = std::max (heard, buffer.getMagnitude (0, 0, 512));
        pause (5);
    }
    check (heard > 0.02f, "with the DAW stopped, the take plays from Comp Captain");
    const auto playing = mt ([&] { return p.audition(); });
    check (playing.what == "take" && playing.problem.isEmpty(), "and the window is told what's playing");

    // Stop: the track is back, bit for bit
    mt ([&] { p.stopHearing(); });
    {
        juce::Random random (7);
        juce::AudioBuffer<float> original (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, random.nextFloat() - 0.5f);
        original.makeCopyOf (buffer);
        p.processBlock (buffer, midi);
        bool same = true;
        for (int c = 0; c < 2; ++c)
            same = same && std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * 512) == 0;
        check (same, "after Stop the track's own sound passes through untouched");
    }

    // the comp in time with the song while the DAW plays: it takes the track's place there
    mt ([&] { p.hearInTime (0); });
    check (waitFor ([&] { return ! p.audition().preparing; }, 10000) && mt ([&] { return p.audition().problem.isEmpty(); }), "the comp is ready to hear in time");
    daw.playing = true;
    daw.sample = (juce::int64) (17.0 * rate); // a second into the speech
    buffer.clear();
    p.processBlock (buffer, midi);
    float inTime = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        daw.sample += 512;
        inTime = std::max (inTime, buffer.getMagnitude (0, 0, 512));
    }
    check (inTime > 0.02f, "while the DAW plays, the comp plays in time in place of the track");
    daw.recording = true;
    buffer.clear();
    p.processBlock (buffer, midi);
    check (buffer.getMagnitude (0, 0, 512) == 0.0f, "but never while recording (the singer hears themselves)");
    daw.recording = false;
    mt ([&] { p.stopHearing(); });

    // how picky the comp is, saved with the project
    juce::MemoryBlock state;
    mt ([&] { p.getStateInformation (state); });
    check (state.toString().contains ("picky=0.400"), "saved with the project");

    mt ([&] {
        p.setPlayHead (nullptr);
        p.releaseResources();
        plugin.reset();
    });
    pause (300);
    takesRoot.deleteRecursively();

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
#endif
}
}
