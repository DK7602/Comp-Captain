// WAV files and waveform pictures: what we write reads back exactly, crash-cut files still open,
// and the waveform picture follows the sound.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestFolder.h"
#include "cc/Overview.h"
#include "cc/TakesFolder.h"
#include "cc/WavFile.h"

#include <cmath>
#include <fstream>

using Catch::Approx;

namespace
{
    std::vector<float> sine (int n, float freq, float rate, float gain)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = gain * std::sin (2.0f * 3.14159265f * freq * (float) i / rate);
        return v;
    }

    void writeBytes (const std::filesystem::path& p, const std::vector<unsigned char>& bytes)
    {
        std::ofstream out (p, std::ios::binary);
        out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    }

    void add16 (std::vector<unsigned char>& b, unsigned v) { b.push_back ((unsigned char) v); b.push_back ((unsigned char) (v >> 8)); }
    void add32 (std::vector<unsigned char>& b, unsigned v) { add16 (b, v & 0xffff); add16 (b, v >> 16); }
    void addTag (std::vector<unsigned char>& b, const char* t) { b.insert (b.end(), t, t + 4); }
}

TEST_CASE ("A float WAV reads back exactly as it was written")
{
    cctest::TestFolder dir;
    const auto path = dir.path / cc::pathFromUtf8 ("Take 1 – Sofía.wav");
    const auto left = sine (10000, 440, 48000, 0.5f), right = sine (10000, 660, 48000, 0.25f);

    cc::WavWriter w;
    REQUIRE (w.open (path, 48000, 2));
    const float* chans[] = { left.data(), right.data() };
    REQUIRE (w.write (chans, 4000));
    const float* rest[] = { left.data() + 4000, right.data() + 4000 };
    REQUIRE (w.write (rest, 6000));
    REQUIRE (w.writeSilence (100));
    CHECK (w.framesWritten() == 10100);
    REQUIRE (w.close());

    const auto data = cc::readWavFile (path);
    REQUIRE (data.has_value());
    CHECK (data->info.sampleRate == 48000);
    CHECK (data->info.channels == 2);
    CHECK (data->info.isFloat);
    REQUIRE (data->info.lengthFrames == 10100);
    for (int i = 0; i < 10000; i += 97)
    {
        CHECK (data->channels[0][(size_t) i] == left[(size_t) i]);
        CHECK (data->channels[1][(size_t) i] == right[(size_t) i]);
    }
    CHECK (data->channels[0][10050] == 0.0f);
}

TEST_CASE ("A take cut off by a crash still opens with what was recorded")
{
    cctest::TestFolder dir;
    const auto path = dir.path / "crash.wav";
    const auto mono = sine (48000, 220, 48000, 0.5f);
    {
        cc::WavWriter w;
        REQUIRE (w.open (path, 48000, 1));
        w.writeInterleaved (mono.data(), 24000);
        w.updateHeader();                     // what the writer does every second
        w.writeInterleaved (mono.data() + 24000, 24000);
        // "crash": copy the file as it is now, before close() fixes the header
        std::filesystem::copy_file (path, dir.path / "copy.wav");
    }
    const auto data = cc::readWavFile (dir.path / "copy.wav");
    REQUIRE (data.has_value());
    CHECK (data->info.lengthFrames >= 24000);
}

TEST_CASE ("16 and 24-bit PCM files are read too")
{
    cctest::TestFolder dir;
    for (int bits : { 16, 24 })
    {
        std::vector<unsigned char> b;
        const int frames = 4, ch = 1, bytes = bits / 8;
        addTag (b, "RIFF");
        add32 (b, (unsigned) (36 + frames * ch * bytes));
        addTag (b, "WAVE");
        addTag (b, "fmt ");
        add32 (b, 16);
        add16 (b, 1);
        add16 (b, (unsigned) ch);
        add32 (b, 44100);
        add32 (b, (unsigned) (44100 * ch * bytes));
        add16 (b, (unsigned) (ch * bytes));
        add16 (b, (unsigned) bits);
        addTag (b, "LIST"); // an extra chunk to skip, odd-sized so it's padded
        add32 (b, 3);
        b.push_back ('a'); b.push_back ('b'); b.push_back ('c'); b.push_back (0);
        addTag (b, "data");
        add32 (b, (unsigned) (frames * ch * bytes));
        const int full = bits == 16 ? 32767 : 8388607;
        for (int v : { 0, full, -full - 1, full / 2 })
        {
            for (int k = 0; k < bytes; ++k)
                b.push_back ((unsigned char) ((unsigned) v >> (8 * k)));
        }
        const auto path = dir.path / ("pcm" + std::to_string (bits) + ".wav");
        writeBytes (path, b);

        const auto data = cc::readWavFile (path);
        INFO (bits);
        REQUIRE (data.has_value());
        CHECK (data->info.sampleRate == 44100);
        CHECK (data->info.bitsPerSample == bits);
        REQUIRE (data->info.lengthFrames == 4);
        CHECK (data->channels[0][0] == 0.0f);
        CHECK (data->channels[0][1] == Approx (1.0).margin (0.0001));
        CHECK (data->channels[0][2] == Approx (-1.0));
        CHECK (data->channels[0][3] == Approx (0.5).margin (0.0001));
    }
}

TEST_CASE ("Files that aren't WAV are turned away")
{
    cctest::TestFolder dir;
    writeBytes (dir.path / "not.wav", { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0 });
    CHECK_FALSE (cc::readWavFile (dir.path / "not.wav").has_value());
    CHECK_FALSE (cc::readWavFile (dir.path / "missing.wav").has_value());
}

TEST_CASE ("The waveform picture follows the sound")
{
    cc::OverviewBuilder ov (48000);
    std::vector<float> quiet (4800, 0.0f), loud (4800, 0.0f);
    loud[100] = 0.25f;
    loud[2000] = -1.0f;
    const float* q[] = { quiet.data() };
    const float* l[] = { loud.data() };
    ov.add (q, 1, 4800); // 0.1 s of silence = 10 slices
    ov.add (l, 1, 4800);
    ov.addSilence (480 * 5);
    ov.finish();
    const auto& p = ov.peaks();
    REQUIRE (p.size() == 25);
    CHECK (p[0] == 0);
    CHECK (p[10] == cc::peakToByte (0.25f));
    CHECK (p[14] == 255);
    CHECK (p[24] == 0);
    CHECK (ov.loudest() == Approx (1.0f));
    CHECK (cc::peakToByte (0.25f) == 128); // square-root scale: a quarter of full level shows half height
}

TEST_CASE ("Waveform pictures travel to the window as base64")
{
    const std::vector<std::uint8_t> bytes { 0, 1, 2, 250, 255, 128, 7 };
    const auto text = cc::toBase64 (bytes.data(), bytes.size());
    CHECK (text == "AAEC+v+ABw==");
    CHECK (cc::fromBase64 (text) == bytes);
    CHECK (cc::toBase64 (nullptr, 0).empty());
}

TEST_CASE ("Take and folder names are safe on every computer")
{
    CHECK (cc::safeFileName ("Take 3") == "Take 3");
    CHECK (cc::safeFileName ("Verse 1: \"Lead\" / Dbl?") == "Verse 1- -Lead- - Dbl-");
    CHECK (cc::safeFileName ("  ..hidden.  ") == "hidden");
    CHECK (cc::safeFileName ("") == "Take");
    CHECK (cc::safeFileName ("CON") == "_CON");
    CHECK (cc::safeFileName ("com1.wav") == "_com1.wav");
    CHECK (cc::safeFileName ("Console") == "Console");
    CHECK (cc::safeFileName (std::string (200, 'a')).size() == 80);
    CHECK (cc::safeFileName ("Sofía") == "Sofía");

    CHECK (cc::trackFolderName ({ 2026, 11, 3 }, "Lead Vox", "4f1c") == "2026-11-03 Lead Vox (4f1c)");
    CHECK (cc::trackFolderName ({ 2026, 1, 23 }, "", "0000") == "2026-01-23 Comp Captain (0000)");
    CHECK (cc::shortHexId (1) != cc::shortHexId (2));
    CHECK (cc::shortHexId (12345).size() == 4);
}

TEST_CASE ("A new take never overwrites an old file")
{
    cctest::TestFolder dir;
    const auto first = cc::uniqueFile (dir.path, "Take 1", ".wav");
    CHECK (first.filename() == "Take 1.wav");
    std::ofstream (first).put ('x');
    const auto second = cc::uniqueFile (dir.path, "Take 1", ".wav");
    CHECK (second.filename() == "Take 1 (2).wav");
}
