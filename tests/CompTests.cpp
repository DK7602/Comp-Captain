// The comp: made-up takes with known sounds. Lines in a row from one take make one piece; joins go in the gap
// between the lines; a join never clicks, even in the middle of a note; louder and softer takes are evened out;
// a punch-in that doesn't overlap fades on its own; a rendered comp can start on a bar line.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestFolder.h"
#include "cc/Comp.h"
#include "cc/WavFile.h"

#include <cmath>
#include <functional>

using Catch::Approx;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    struct Tone
    {
        double from = 0.0, to = 0.0; // take seconds
        double freq = 220.0, amp = 0.5, phase = 0.0;
    };

    // A take: tones where it sings, a little hiss everywhere else, written to a WAV and measured every 10 ms.
    struct FakeTake
    {
        std::filesystem::path wav;
        double rate = 48000.0, length = 0.0;
        cc::TakeFeatures features;
        std::vector<cc::LinePlace> places;
        std::vector<float> samples;

        cc::CompSource source (std::uint64_t key, double startSeconds) const
        {
            cc::CompSource s;
            s.key = key;
            s.startSeconds = startSeconds;
            s.lengthSeconds = length;
            s.places = &places;
            s.features = &features;
            return s;
        }
    };

    FakeTake makeTake (const std::filesystem::path& wav, double length, const std::vector<Tone>& tones,
                       const std::vector<std::pair<double, double>>& lines, double rate = 48000.0)
    {
        FakeTake t;
        t.wav = wav;
        t.rate = rate;
        t.length = length;
        const auto n = (size_t) std::llround (length * rate);
        t.samples.assign (n, 0.0f);
        unsigned seed = 12345;
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            t.samples[i] = (float) (((double) (seed >> 8) / 16777216.0 - 0.5) * 0.0004); // hiss, about -75 dB
            const double time = (double) i / rate;
            for (auto& tone : tones)
                if (time >= tone.from && time < tone.to)
                    t.samples[i] += (float) (tone.amp * std::sin (2.0 * pi * tone.freq * time + tone.phase));
        }
        cc::WavWriter w;
        REQUIRE (w.open (wav, rate, 1));
        const float* ch[] = { t.samples.data() };
        REQUIRE (w.write (ch, (int) n));
        REQUIRE (w.close());

        const auto hop = (size_t) std::llround (cc::TakeFeatures::hop * rate);
        for (size_t f = 0; f * hop < n; ++f)
        {
            double sum = 0.0;
            float peak = 0.0f;
            const size_t end = std::min (n, (f + 1) * hop);
            for (size_t i = f * hop; i < end; ++i)
            {
                sum += (double) t.samples[i] * t.samples[i];
                peak = std::max (peak, std::abs (t.samples[i]));
            }
            const double rms = std::sqrt (sum / (double) std::max<size_t> (1, end - f * hop));
            t.features.level.push_back ((float) (20.0 * std::log10 (std::max (rms, 1e-9))));
            t.features.peak.push_back (peak);
            t.features.pitch.push_back (0.0f);
            t.features.periodic.push_back (0.0f);
            t.features.lowBand.push_back (-120.0f);
            t.features.highBand.push_back (-120.0f);
            t.features.clipped.push_back (0.0f);
        }
        for (size_t i = 0; i < lines.size(); ++i)
        {
            cc::LinePlace p;
            p.line = (int) i;
            p.start = lines[i].first;
            p.end = lines[i].second;
            p.sure = 1.0f;
            t.places.push_back (p);
        }
        return t;
    }

    double biggestStep (const std::vector<float>& x, size_t from = 1, size_t to = SIZE_MAX)
    {
        double most = 0.0;
        for (size_t i = std::max<size_t> (1, from); i < std::min (to, x.size()); ++i)
            most = std::max (most, (double) std::abs (x[i] - x[i - 1]));
        return most;
    }

    double rmsDb (const std::vector<float>& x, double rate, double from, double to)
    {
        double sum = 0.0;
        size_t count = 0;
        for (auto i = (size_t) (from * rate); i < (size_t) (to * rate) && i < x.size(); ++i, ++count)
            sum += (double) x[i] * x[i];
        return 20.0 * std::log10 (std::sqrt (sum / (double) std::max<size_t> (1, count)) + 1e-12);
    }
}

TEST_CASE ("Lines in a row from one take play as one piece", "[comp]")
{
    cctest::TestFolder dir;
    const std::vector<std::pair<double, double>> lines { { 1.0, 2.0 }, { 2.5, 3.5 }, { 4.0, 5.0 }, { 5.5, 6.5 } };
    auto a = makeTake (dir.path / "a.wav", 7.5, { { 1.0, 2.0 }, { 2.5, 3.5 }, { 4.0, 5.0 }, { 5.5, 6.5 } }, lines);
    auto b = makeTake (dir.path / "b.wav", 7.5, { { 1.0, 2.0, 330 }, { 2.5, 3.5, 330 }, { 4.0, 5.0, 330 }, { 5.5, 6.5, 330 } }, lines);
    const std::vector<cc::CompSource> sources { a.source (1, 10.0), b.source (2, 10.0) };
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 1 }, { 2, 2 }, { 3, 1 } }, sources);
    REQUIRE (plan.pieces.size() == 3);
    CHECK (plan.pieces[0].take == 1);
    CHECK (plan.pieces[0].firstLine == 0);
    CHECK (plan.pieces[0].lastLine == 1);
    CHECK (plan.pieces[1].take == 2);
    CHECK (plan.pieces[1].firstLine == 2);
    CHECK (plan.pieces[2].take == 1);
    // the joins are in the gaps (song seconds: the take starts at 10 s), with the short fade for a quiet join
    CHECK (plan.pieces[0].to > 13.5);
    CHECK (plan.pieces[0].to < 14.0);
    CHECK (plan.pieces[1].from == plan.pieces[0].to);
    CHECK (plan.pieces[1].fadeIn == Approx (0.015));
    CHECK (plan.pieces[1].joinLevelDb < -55.0f);
    CHECK (plan.pieces[2].from > 15.0);
    CHECK (plan.pieces[2].from < 15.5);
    // it starts just before the first line (the breath in) and ends once the last word has died away
    CHECK (plan.start == Approx (11.0 - 0.35 - 0.0075).margin (0.001));
    CHECK (plan.end > 16.5);
    CHECK (plan.end < 16.8);
}

TEST_CASE ("A join goes in the gap even when the takes are sung a little apart", "[comp]")
{
    cctest::TestFolder dir;
    // take b sings everything 0.2 s earlier than take a
    auto a = makeTake (dir.path / "a.wav", 5.0, { { 1.0, 2.0 }, { 2.6, 3.6 } }, { { 1.0, 2.0 }, { 2.6, 3.6 } });
    auto b = makeTake (dir.path / "b.wav", 5.0, { { 0.8, 1.8, 330 }, { 2.4, 3.4, 330 } }, { { 0.8, 1.8 }, { 2.4, 3.4 } });
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, { a.source (1, 0.0), b.source (2, 0.0) });
    REQUIRE (plan.pieces.size() == 2);
    // a sings until 2.0, b starts again at 2.4: the join is between them, where both are quiet
    CHECK (plan.pieces[1].from > 2.0);
    CHECK (plan.pieces[1].from < 2.4);
    CHECK (plan.pieces[1].joinLevelDb < -55.0f);
}

TEST_CASE ("A join never clicks, even in the middle of a note", "[comp]")
{
    cctest::TestFolder dir;
    // both takes sing without a break, at different pitches and phases: there's no quiet spot to join in
    auto a = makeTake (dir.path / "a.wav", 4.0, { { 0.0, 4.0, 220, 0.5 } }, { { 0.5, 1.9 }, { 2.0, 3.5 } });
    auto b = makeTake (dir.path / "b.wav", 4.0, { { 0.0, 4.0, 233, 0.5, 1.7 } }, { { 0.5, 1.9 }, { 2.0, 3.5 } });
    const std::vector<cc::CompSource> sources { a.source (1, 0.0), b.source (2, 0.0) };
    const std::vector<cc::CompFile> files { { 1, a.wav, 0.0 }, { 2, b.wav, 0.0 } };
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, sources);
    REQUIRE (plan.pieces.size() == 2);
    CHECK (plan.pieces[1].fadeIn == Approx (0.040)); // a longer crossfade where it's loud
    std::string error;
    const auto audio = cc::renderComp (plan, files, 0.0, 4.0, 48000.0, 1, nullptr, error);
    REQUIRE (audio.has_value());
    // a 233 Hz tone at 0.5 moves at most 0.0153 per sample; through the crossfade it stays close to that
    const auto join = (size_t) (plan.pieces[1].from * 48000.0);
    CHECK (biggestStep (audio->channels[0], join - 4800, join + 4800) < 0.03);

    // the same join cut hard (no fade) jumps: the check above would catch a click
    auto hard = plan;
    hard.pieces[0].to = hard.pieces[1].from = 2.0011;
    hard.pieces[0].fadeOut = hard.pieces[1].fadeIn = 0.0;
    const auto clicked = cc::renderComp (hard, files, 0.0, 4.0, 48000.0, 1, nullptr, error);
    REQUIRE (clicked.has_value());
    CHECK (biggestStep (clicked->channels[0], 90000, 102000) > 0.1);
}

TEST_CASE ("A louder take and a softer one are evened out (gain only)", "[comp]")
{
    cctest::TestFolder dir;
    const std::vector<std::pair<double, double>> lines { { 1.0, 2.0 }, { 2.5, 3.5 } };
    auto loud = makeTake (dir.path / "loud.wav", 4.5, { { 1.0, 2.0, 220, 0.5 }, { 2.5, 3.5, 220, 0.5 } }, lines);
    auto soft = makeTake (dir.path / "soft.wav", 4.5, { { 1.0, 2.0, 220, 0.125 }, { 2.5, 3.5, 220, 0.125 } }, lines);
    const std::vector<cc::CompSource> sources { loud.source (1, 0.0), soft.source (2, 0.0) };
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, sources);
    REQUIRE (plan.pieces.size() == 2);
    // 12 dB apart: each moves 6 dB (the most it will), so they meet in the middle
    CHECK (plan.pieces[0].gainDb == Approx (-6.0).margin (0.3));
    CHECK (plan.pieces[1].gainDb == Approx (6.0).margin (0.3));
    std::string error;
    const auto audio = cc::renderComp (plan, { { 1, loud.wav, 0.0 }, { 2, soft.wav, 0.0 } }, 0.0, 4.5, 48000.0, 1, nullptr, error);
    REQUIRE (audio.has_value());
    CHECK (std::abs (rmsDb (audio->channels[0], 48000.0, 1.2, 1.8) - rmsDb (audio->channels[0], 48000.0, 2.7, 3.3)) < 1.0);

    // turned off, nothing changes
    cc::CompOptions off;
    off.levelMatch = false;
    const auto same = cc::planComp ({ { 0, 1 }, { 1, 2 } }, sources, off);
    CHECK (same.pieces[0].gainDb == 0.0);
    CHECK (same.pieces[1].gainDb == 0.0);
}

TEST_CASE ("A soft line stays soft: only one take against another is evened out", "[comp]")
{
    cctest::TestFolder dir;
    const std::vector<std::pair<double, double>> lines { { 1.0, 2.0 }, { 2.5, 3.5 } };
    // both takes sing the second line 12 dB softer (that's the song), at the same level as each other
    auto a = makeTake (dir.path / "a.wav", 4.5, { { 1.0, 2.0, 220, 0.5 }, { 2.5, 3.5, 220, 0.125 } }, lines);
    auto b = makeTake (dir.path / "b.wav", 4.5, { { 1.0, 2.0, 233, 0.5 }, { 2.5, 3.5, 233, 0.125 } }, lines);
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, { a.source (1, 0.0), b.source (2, 0.0) });
    REQUIRE (plan.pieces.size() == 2);
    CHECK (plan.pieces[0].gainDb == 0.0);
    CHECK (plan.pieces[1].gainDb == 0.0);
}

TEST_CASE ("Level matching never pushes a take into clipping", "[comp]")
{
    cctest::TestFolder dir;
    const std::vector<std::pair<double, double>> lines { { 1.0, 2.0 }, { 2.5, 3.5 } };
    // the soft take has one loud spike: raising it 6 dB would clip
    auto loud = makeTake (dir.path / "loud.wav", 4.5, { { 1.0, 2.0, 220, 0.5 }, { 2.5, 3.5, 220, 0.5 } }, lines);
    auto soft = makeTake (dir.path / "soft.wav", 4.5, { { 1.0, 2.0, 220, 0.125 }, { 2.5, 3.5, 220, 0.125 }, { 3.0, 3.02, 1000, 0.8 } }, lines);
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, { loud.source (1, 0.0), soft.source (2, 0.0) });
    REQUIRE (plan.pieces.size() == 2);
    const double peak = 0.925 * std::pow (10.0, plan.pieces[1].gainDb / 20.0);
    CHECK (peak < 1.0);
}

TEST_CASE ("A punch-in that doesn't overlap the other take fades on its own", "[comp]")
{
    cctest::TestFolder dir;
    // a: the first line (song 0..3 s); b: punched in for the second line, starting at song 5 s
    auto a = makeTake (dir.path / "a.wav", 3.0, { { 1.0, 2.0 } }, { { 1.0, 2.0 }, { 9.0, 9.5 } });
    auto b = makeTake (dir.path / "b.wav", 3.0, { { 1.0, 2.0, 330 } }, { { -5.0, -4.0 }, { 1.0, 2.0 } });
    const std::vector<cc::CompSource> sources { a.source (1, 0.0), b.source (2, 5.0) };
    const auto plan = cc::planComp ({ { 0, 1 }, { 1, 2 } }, sources);
    REQUIRE (plan.pieces.size() == 2);
    CHECK (plan.pieces[0].to <= 3.0);
    CHECK (plan.pieces[1].from >= 5.0);
    std::string error;
    const auto audio = cc::renderComp (plan, { { 1, a.wav, 0.0 }, { 2, b.wav, 5.0 } }, 0.0, 8.0, 48000.0, 1, nullptr, error);
    REQUIRE (audio.has_value());
    CHECK (rmsDb (audio->channels[0], 48000.0, 3.2, 4.8) < -100.0); // silence between
    CHECK (rmsDb (audio->channels[0], 48000.0, 6.2, 6.8) > -12.0);  // b's line
}

TEST_CASE ("One take of one line, and a whole take, for hearing them", "[comp]")
{
    cctest::TestFolder dir;
    auto a = makeTake (dir.path / "a.wav", 4.0, { { 1.0, 2.0 } }, { { 1.0, 2.0 } });
    const auto src = a.source (1, 10.0);
    const auto line = cc::planLine (src, 0);
    REQUIRE (line.pieces.size() == 1);
    CHECK (line.pieces[0].from == Approx (10.6));
    CHECK (line.pieces[0].to == Approx (12.4));
    CHECK (cc::planLine (src, 3).empty()); // a line it didn't sing
    const auto whole = cc::planTake (src);
    REQUIRE (whole.pieces.size() == 1);
    CHECK (whole.start == Approx (10.0));
    CHECK (whole.end == Approx (14.0));
}

TEST_CASE ("A take at another sample rate is resampled, and a mono take fills both sides", "[comp]")
{
    cctest::TestFolder dir;
    auto a = makeTake (dir.path / "a.wav", 2.0, { { 0.0, 2.0, 440, 0.5 } }, { { 0.2, 1.8 } }, 44100.0);
    const auto plan = cc::planTake (a.source (1, 0.0));
    std::string error;
    const auto audio = cc::renderComp (plan, { { 1, a.wav, 0.0 } }, 0.0, 2.0, 48000.0, 2, nullptr, error);
    REQUIRE (audio.has_value());
    REQUIRE (audio->channels.size() == 2);
    CHECK (audio->frames() == 96000);
    int crossings = 0;
    const auto& x = audio->channels[0];
    for (size_t i = 48000; i < 96000 - 4800; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++crossings;
    CHECK (std::abs (crossings - 792) <= 4); // 440 Hz for 0.9 s: the pitch didn't change
    CHECK (audio->channels[1] == audio->channels[0]);
}

TEST_CASE ("A missing take file says so", "[comp]")
{
    cctest::TestFolder dir;
    auto a = makeTake (dir.path / "a.wav", 2.0, { { 0.5, 1.5 } }, { { 0.5, 1.5 } });
    const auto plan = cc::planTake (a.source (1, 0.0));
    std::string error;
    CHECK_FALSE (cc::renderComp (plan, { { 1, dir.path / "gone.wav", 0.0 } }, 0.0, 2.0, 48000.0, 1, nullptr, error).has_value());
    CHECK (error.find ("gone.wav") != std::string::npos);
}

TEST_CASE ("A rendered comp starts on the bar line before it", "[comp]")
{
    cc::SongPosition ref; // bar 9 at 120 BPM in 4/4: quarter 32, 16 s
    ref.ppq = 32.0;
    ref.seconds = 16.0;
    ref.bpm = 120.0;
    ref.hasBarStart = true;
    ref.barStartPpq = 32.0;
    auto at = cc::barStartBefore (ref, 17.3);
    CHECK (at.bar == 9);
    CHECK (at.seconds == Approx (16.0));
    at = cc::barStartBefore (ref, 18.1);
    CHECK (at.bar == 10);
    CHECK (at.seconds == Approx (18.0));
    at = cc::barStartBefore (ref, 15.9); // before the take: still counted back on the same grid
    CHECK (at.bar == 8);
    CHECK (at.seconds == Approx (14.0));
    // a take that started mid-bar (beat 3 of bar 9)
    ref.ppq = 34.0;
    ref.seconds = 17.0;
    at = cc::barStartBefore (ref, 17.5);
    CHECK (at.bar == 9);
    CHECK (at.seconds == Approx (16.0));
}
