#include "AudioImport.h"

#include "cc/Capture.h"
#include "cc/Overview.h"
#include "cc/TakesFolder.h"
#include "cc/WavFile.h"

namespace
{
    std::filesystem::path toPath (const juce::File& f) { return cc::pathFromUtf8 (f.getFullPathName().toStdString()); }

    juce::String lengthText (double seconds)
    {
        const int s = (int) std::lround (seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

juce::String importableWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.mp3";
}

ImportedTake importAudioFile (const ImportPlan& plan, const std::atomic<bool>* cancel)
{
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };

    ImportedTake result;
    const auto name = plan.name.isNotEmpty() ? plan.name : plan.source.getFileNameWithoutExtension();
    result.take.name = name.toStdString();

    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
    formats.registerFormat (new juce::FlacAudioFormat(), false);
   #if JUCE_USE_MP3AUDIOFORMAT
    formats.registerFormat (new juce::MP3AudioFormat(), false);
   #endif

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (plan.source));
    if (reader == nullptr)
    {
        result.message = "Couldn't open " + name + ". Comp Captain takes WAV, AIFF, FLAC and MP3 files.";
        return result;
    }
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
    {
        result.message = name + " has no sound in it.";
        return result;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    std::error_code ec;
    std::filesystem::create_directories (toPath (plan.folder), ec);
    const auto path = cc::uniqueFile (toPath (plan.folder), name.toStdString(), ".wav");
    cc::WavWriter wav;
    if (ec || ! wav.open (path, reader->sampleRate, channels))
    {
        result.message = "Couldn't save " + name + " in " + plan.folder.getFullPathName() + ". Is the disk full?";
        return result;
    }

    cc::OverviewBuilder overview (reader->sampleRate);
    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buf (channels, chunk);
    bool ok = true;
    for (juce::int64 at = 0; at < reader->lengthInSamples && ok && ! cancelled();)
    {
        const int n = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - at);
        ok = reader->read (&buf, 0, n, at, true, channels > 1) && wav.write (buf.getArrayOfReadPointers(), n);
        overview.add (buf.getArrayOfReadPointers(), channels, n);
        at += n;
    }
    ok = wav.close() && ok;
    overview.finish();

    if (! ok || cancelled())
    {
        std::filesystem::remove (path, ec);
        result.message = "Couldn't read all of " + name + ". The file may be damaged.";
        return result;
    }

    cc::Take& t = result.take;
    t.source = cc::TakeSource::file;
    t.file = cc::utf8FromPath (path.filename());
    t.originalFile = plan.source.getFullPathName().toStdString();
    t.sampleRate = reader->sampleRate;
    t.channels = channels;
    t.lengthSamples = wav.framesWritten();
    t.start = cc::positionAtBar (juce::jlimit (1, 9999, plan.bar), 1.0, plan.bpm, plan.meter);
    t.track = plan.trackName.toStdString();
    if (plan.role.has_value())
    {
        t.role = *plan.role;
        t.roleChosen = true;
    }
    else
    {
        t.role = cc::guessRole (t.name).value_or (plan.fallbackRole);
    }

    result.ok = true;
    result.peaks = overview.peaks();
    result.message = "Added " + name + " at bar " + juce::String (plan.bar) + " (" + lengthText (t.lengthSeconds()) + " long)"
                     + (overview.loudest() < cc::CaptureWriter::silentBelow ? juce::String (". It's silent.") : juce::String ("."));
    return result;
}
