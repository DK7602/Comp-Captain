// CAPTURE: a pretend DAW records through Comp Captain, and we check every pass becomes a take, on the
// right bar, with exactly the sound that came through.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestFolder.h"
#include "cc/Capture.h"

#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    // A DAW at 120 BPM in 4/4: plays blocks of sound through the feed.
    struct FakeDaw
    {
        cc::CaptureFeed& feed;
        std::int64_t sample = 0;
        bool recording = false;
        float level = 0.5f;
        std::int64_t sungFrames = 0; // a running count of samples sung (so each sample is unique)

        cc::Transport transport() const
        {
            cc::Transport t;
            t.playing = true;
            t.recording = recording;
            t.hasSamples = true;
            t.timeInSamples = sample;
            t.hasSeconds = true;
            t.seconds = (double) sample / rate;
            t.hasPpq = true;
            t.ppq = t.seconds * 2.0; // 120 BPM: two beats a second
            t.bpm = 120.0;
            t.meter = { 4, 4 };
            t.hasBarStart = true;
            t.barStartPpq = std::floor (t.ppq / 4.0) * 4.0;
            return t;
        }

        void play (double seconds)
        {
            const auto blocks = (int) std::ceil (seconds * rate / block);
            std::vector<float> l ((size_t) block), r ((size_t) block);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    const float v = level * (float) std::sin ((double) (sungFrames + i) * 0.05);
                    l[(size_t) i] = v;
                    r[(size_t) i] = -v;
                }
                const float* ch[] = { l.data(), r.data() };
                feed.process (ch, 2, block, transport());
                sample += block;
                sungFrames += block;
            }
        }

        void jumpTo (double seconds) { sample = (std::int64_t) (seconds * rate); }
    };

    struct Rig
    {
        cctest::TestFolder dir;
        cc::CaptureFeed feed;
        cc::CaptureWriter writer { feed };
        std::atomic<int> nextNumber { 1 };
        FakeDaw daw { feed };

        Rig()
        {
            feed.prepare (rate, 2, 4.0);
            writer.setDestination (dir.path / "Lead Vox", "Lead Vox", cc::Role::lead);
            writer.setNumbering (&nextNumber);
        }

        std::vector<cc::CaptureNews> drain()
        {
            while (writer.pump()) {}
            return writer.takeNews();
        }

        static std::vector<cc::CaptureNews> saved (const std::vector<cc::CaptureNews>& all)
        {
            std::vector<cc::CaptureNews> out;
            for (auto& n : all)
                if (n.kind == cc::CaptureNews::Kind::saved)
                    out.push_back (n);
            return out;
        }
    };
}

TEST_CASE ("Nothing is captured until CAPTURE is armed and the DAW records")
{
    Rig rig;
    rig.daw.play (1.0);                 // playing, not recording, not armed
    rig.daw.recording = true;
    rig.daw.play (1.0);                 // recording, but not armed
    rig.feed.setArmed (true);
    rig.daw.recording = false;
    rig.daw.play (1.0);                 // armed, but the DAW isn't recording
    CHECK (rig.drain().empty());
    CHECK (rig.feed.framesReadable() == 0);
}

TEST_CASE ("A pass becomes a take, stamped with its bar, with exactly the sound that came through")
{
    Rig rig;
    rig.feed.setArmed (true);
    rig.daw.jumpTo (8.0);               // bar 5 at 120 BPM in 4/4
    rig.daw.recording = true;
    const auto firstSung = rig.daw.sungFrames;
    rig.daw.play (3.0);
    CHECK (rig.feed.isCapturing());
    rig.daw.recording = false;
    rig.daw.play (0.5);
    CHECK_FALSE (rig.feed.isCapturing());

    const auto news = rig.drain();
    const auto saved = Rig::saved (news);
    REQUIRE (saved.size() == 1);
    CHECK (news.front().kind == cc::CaptureNews::Kind::started);

    const auto& take = saved[0].take;
    CHECK (take.name == "Take 1");
    CHECK (take.file == "Take 1.wav");
    CHECK (take.track == "Lead Vox");
    CHECK (take.source == cc::TakeSource::capture);
    CHECK (take.startBarBeat().bar == 5);
    CHECK (take.startBarBeat().beat == Approx (1.0));
    CHECK (take.start.seconds == Approx (8.0));
    CHECK (take.start.bpm == Approx (120.0));
    const auto expectedFrames = (std::int64_t) std::ceil (3.0 * rate / block) * block;
    CHECK (take.lengthSamples == expectedFrames);
    CHECK (saved[0].message.find ("bar 5") != std::string::npos);
    CHECK (rig.nextNumber.load() == 2);

    const auto wav = cc::readWavFile (rig.dir.path / "Lead Vox" / "Take 1.wav");
    REQUIRE (wav.has_value());
    CHECK (wav->info.channels == 2);
    REQUIRE (wav->info.lengthFrames == expectedFrames);
    for (std::int64_t i = 0; i < expectedFrames; i += 1009)
    {
        const float sung = 0.5f * (float) std::sin ((double) (firstSung + i) * 0.05);
        CHECK (wav->channels[0][(size_t) i] == sung);
        CHECK (wav->channels[1][(size_t) i] == -sung);
    }

    // The waveform picture came with it: 100 slices a second.
    CHECK (saved[0].peaks.size() == (size_t) std::ceil ((double) expectedFrames / 480.0));
}

TEST_CASE ("Every time the DAW's loop comes round, a new take")
{
    Rig rig;
    rig.feed.setArmed (true);
    rig.daw.recording = true;
    for (int pass = 0; pass < 3; ++pass)
    {
        rig.daw.jumpTo (16.0);          // cycle from bar 9
        rig.daw.play (2.0);
        rig.writer.pump();              // the writer runs all the time in the plug-in
    }
    rig.daw.recording = false;
    rig.daw.play (0.1);

    const auto saved = Rig::saved (rig.drain());
    REQUIRE (saved.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (saved[(size_t) i].take.name == "Take " + std::to_string (i + 1));
        CHECK (saved[(size_t) i].take.startBarBeat().bar == 9);
    }
}

TEST_CASE ("With Loop Takes off, only the first pass is kept")
{
    Rig rig;
    rig.feed.setLoopTakes (false);
    rig.feed.setArmed (true);
    rig.daw.recording = true;
    for (int pass = 0; pass < 3; ++pass)
    {
        rig.daw.jumpTo (16.0);
        rig.daw.play (2.0);
        rig.writer.pump();
    }
    rig.daw.recording = false;
    rig.daw.play (0.1);
    CHECK (Rig::saved (rig.drain()).size() == 1);

    // the next press of Record makes a new take as usual
    rig.daw.recording = true;
    rig.daw.play (1.0);
    rig.daw.recording = false;
    rig.daw.play (0.1);
    CHECK (Rig::saved (rig.drain()).size() == 1);
}

TEST_CASE ("Silent and very short passes aren't kept, and give their number back")
{
    Rig rig;
    rig.feed.setArmed (true);

    rig.daw.level = 0.0f;               // the track wasn't monitoring: nothing came through
    rig.daw.recording = true;
    rig.daw.play (2.0);
    rig.daw.recording = false;
    rig.daw.play (0.1);
    auto news = rig.drain();
    REQUIRE (news.size() == 2);
    CHECK (news[1].kind == cc::CaptureNews::Kind::discarded);
    CHECK (news[1].message.find ("silent") != std::string::npos);
    CHECK (news[1].message.find ("monitoring") != std::string::npos);

    rig.daw.level = 0.5f;
    rig.daw.recording = true;
    rig.daw.play (0.2);                 // a false start
    rig.daw.recording = false;
    rig.daw.play (0.1);
    news = rig.drain();
    REQUIRE (news.size() == 2);
    CHECK (news[1].kind == cc::CaptureNews::Kind::discarded);

    CHECK (rig.nextNumber.load() == 1); // the next kept take is still "Take 1"
    CHECK_FALSE (std::filesystem::exists (rig.dir.path / "Lead Vox" / "Take 1.wav"));

    rig.daw.recording = true;
    rig.daw.play (1.0);
    rig.daw.recording = false;
    rig.daw.play (0.1);
    const auto saved = Rig::saved (rig.drain());
    REQUIRE (saved.size() == 1);
    CHECK (saved[0].take.name == "Take 1");
}

TEST_CASE ("If the disk falls behind, the take keeps its length and stays in time")
{
    Rig rig; // a 4-second buffer
    rig.feed.setArmed (true);
    rig.daw.recording = true;
    rig.daw.play (6.0);                 // the writer doesn't run at all for 6 seconds
    rig.writer.pump();                  // ... then catches up
    rig.daw.play (2.0);
    rig.daw.recording = false;
    rig.daw.play (0.1);

    const auto saved = Rig::saved (rig.drain());
    REQUIRE (saved.size() == 1);
    const auto expected = (std::int64_t) std::ceil (6.0 * rate / block) * block + (std::int64_t) std::ceil (2.0 * rate / block) * block;
    CHECK (saved[0].take.lengthSamples == expected); // the lost part is silence, not missing
}

TEST_CASE ("Takes never overwrite files already in the folder")
{
    Rig rig;
    std::filesystem::create_directories (rig.dir.path / "Lead Vox");
    std::ofstream (rig.dir.path / "Lead Vox" / "Take 1.wav").put ('x');
    rig.feed.setArmed (true);
    rig.daw.recording = true;
    rig.daw.play (1.0);
    rig.daw.recording = false;
    rig.daw.play (0.1);
    const auto saved = Rig::saved (rig.drain());
    REQUIRE (saved.size() == 1);
    CHECK (saved[0].take.name == "Take 1");
    CHECK (saved[0].take.file == "Take 1 (2).wav");
}

TEST_CASE ("The take being recorded can be watched live")
{
    Rig rig;
    rig.feed.setArmed (true);
    rig.daw.recording = true;
    rig.daw.play (1.0);
    rig.drain();
    auto live = rig.writer.live (0);
    CHECK (live.recording);
    CHECK (live.name == "Take 1");
    CHECK (live.peaks.size() >= 99);
    const auto later = rig.writer.live (50);
    CHECK (later.peaksFrom == 50);
    CHECK (later.peaks.size() == live.peaks.size() - 50);

    rig.daw.recording = false;
    rig.daw.play (0.1);
    rig.drain();
    CHECK_FALSE (rig.writer.live (0).recording);
}

TEST_CASE ("Mono tracks make mono takes")
{
    cctest::TestFolder dir;
    cc::CaptureFeed feed;
    feed.prepare (rate, 1);
    cc::CaptureWriter writer (feed);
    writer.setDestination (dir.path, "Vox", cc::Role::lead);
    feed.setArmed (true);
    std::vector<float> l ((size_t) block, 0.25f);
    const float* ch[] = { l.data() };
    cc::Transport t;
    t.playing = t.recording = true;
    for (int b = 0; b < 100; ++b)
    {
        t.hasSamples = true;
        t.timeInSamples = b * block;
        feed.process (ch, 1, block, t);
    }
    t.recording = false;
    feed.process (ch, 1, block, t);
    while (writer.pump()) {}
    const auto news = writer.takeNews();
    REQUIRE (news.size() == 2);
    CHECK (news[1].take.channels == 1);
    CHECK (news[1].take.start.ppq == 0.0); // no tempo from the DAW: starts at the song start
}
