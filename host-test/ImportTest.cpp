// Adding takes from files: WAV, AIFF and FLAC files are made here, added as takes, and checked
// (length, sample rate, the sound itself, the bar they're placed at, the role guessed from the name).
// Set CC_TEST_AUDIO to a folder of real recordings (e.g. MP3 vocals) to add those too; they're
// never part of the repository.
//   CompCaptainImportTest
// Exit code 0 = everything passed.

#include <juce_audio_formats/juce_audio_formats.h>

#include "../plugin/AudioImport.h"
#include "cc/WavFile.h"

#include <cmath>
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

    float tone (int channel, int i) { return (channel == 0 ? 0.5f : -0.3f) * (float) std::sin (i * 0.02); }

    bool writeTestFile (juce::AudioFormat& format, const juce::File& file, double rate, int channels, int bits, int frames)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = format.createWriterFor (out, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (channels)
                                                       .withBitsPerSample (bits));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> buf (channels, frames);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                buf.setSample (c, i, tone (c, i));
        return writer->writeFromAudioSampleBuffer (buf, 0, frames);
    }

    std::optional<cc::WavData> readTake (const juce::File& folder, const ImportedTake& t)
    {
        return cc::readWavFile (cc::pathFromUtf8 (folder.getChildFile (juce::String::fromUTF8 (t.take.file.c_str())).getFullPathName().toStdString()));
    }
}

int main()
{
    std::cout << "Comp Captain import test" << std::endl;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CompCaptainImportTest");
    dir.deleteRecursively();
    const auto sources = dir.getChildFile ("From another DAW");
    const auto takes = dir.getChildFile ("Takes");
    sources.createDirectory();

    struct Case
    {
        std::unique_ptr<juce::AudioFormat> format;
        juce::String fileName;
        double rate;
        int channels, bits;
        cc::Role expectedRole;
        float tolerance;
    };
    std::vector<Case> cases;
    cases.push_back ({ std::make_unique<juce::WavAudioFormat>(), "Vox Dbl L.wav", 44100.0, 2, 24, cc::Role::doubled, 1.0e-6f });
    cases.push_back ({ std::make_unique<juce::AiffAudioFormat>(), "Harmony 3rd.aiff", 48000.0, 1, 16, cc::Role::harmony, 1.0e-4f });
    cases.push_back ({ std::make_unique<juce::FlacAudioFormat>(), "Lead Vox.flac", 96000.0, 2, 24, cc::Role::lead, 1.0e-6f });

    const int frames = 70000;
    for (auto& c : cases)
    {
        const auto file = sources.getChildFile (c.fileName);
        check (writeTestFile (*c.format, file, c.rate, c.channels, c.bits, frames), "made a test file: " + c.fileName);

        ImportPlan plan;
        plan.source = file;
        plan.folder = takes;
        plan.bar = 9;
        plan.bpm = 92.0;
        plan.fallbackRole = cc::Role::backing;
        plan.trackName = "Vocals";
        const auto t = importAudioFile (plan);
        check (t.ok, "adds " + c.fileName + " (" + t.message + ")");
        if (! t.ok)
            continue;
        check (t.take.source == cc::TakeSource::file && t.take.name == file.getFileNameWithoutExtension().toStdString(), "  named after the file");
        check (t.take.role == c.expectedRole, "  role guessed from the name: " + juce::String (std::string (cc::toLabel (t.take.role))));
        check (t.take.startBarBeat().bar == 9 && std::abs (t.take.start.bpm - 92.0) < 1e-9, "  starts at bar 9 at 92 BPM");
        check (t.take.lengthSamples == frames && std::abs (t.take.sampleRate - c.rate) < 0.5 && t.take.channels == c.channels,
               "  keeps its length, sample rate and channels");
        check (t.peaks.size() == (size_t) std::ceil (frames / (c.rate / 100.0)), "  has its waveform picture");
        check (t.take.originalFile == file.getFullPathName().toStdString(), "  remembers where it came from");

        const auto wav = readTake (takes, t);
        bool same = wav.has_value() && wav->info.isFloat && wav->info.lengthFrames == frames;
        for (int ch = 0; same && ch < c.channels; ++ch)
            for (int i = 0; i < frames && same; i += 7)
                same = std::abs (wav->channels[(size_t) ch][(size_t) i] - tone (ch, i)) <= c.tolerance;
        check (same, "  the take's WAV holds the same sound");
    }

    // The same name twice never overwrites the first take's file
    {
        ImportPlan plan;
        plan.source = sources.getChildFile ("Vox Dbl L.wav");
        plan.folder = takes;
        const auto again = importAudioFile (plan);
        check (again.ok && again.take.file == "Vox Dbl L (2).wav", "adding the same file again makes a second file, not an overwrite");
    }

    // A role you chose wins over the guess
    {
        ImportPlan plan;
        plan.source = sources.getChildFile ("Lead Vox.flac");
        plan.folder = takes;
        plan.role = cc::Role::adlib;
        const auto chosen = importAudioFile (plan);
        check (chosen.ok && chosen.take.role == cc::Role::adlib && chosen.take.roleChosen, "a role picked in the window is kept");
    }

    // Not audio: a plain message, no take
    {
        const auto notAudio = sources.getChildFile ("lyrics.wav");
        notAudio.replaceWithText ("I was lost in the dark");
        ImportPlan plan;
        plan.source = notAudio;
        plan.folder = takes;
        const auto t = importAudioFile (plan);
        check (! t.ok && t.message.contains ("WAV, AIFF, FLAC and MP3"), "a file that isn't audio is turned away politely");
    }

    // Real recordings (local only): CC_TEST_AUDIO=<folder with .mp3 / .wav files>
    const auto real = juce::SystemStats::getEnvironmentVariable ("CC_TEST_AUDIO", {});
    if (real.isNotEmpty())
    {
        for (auto& f : juce::File (real).findChildFiles (juce::File::findFiles, false, importableWildcard()))
        {
            ImportPlan plan;
            plan.source = f;
            plan.folder = takes;
            const auto t = importAudioFile (plan);
            check (t.ok && t.take.lengthSamples > 0, "real recording: " + f.getFileName() + " -> " + t.message + " (role "
                   + juce::String (std::string (cc::toLabel (t.take.role))) + ", " + juce::String (t.take.sampleRate) + " Hz)");
        }
    }
    else
    {
        std::cout << "  SKIP  real recordings (set CC_TEST_AUDIO to a folder of them)" << std::endl;
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
