// What a take sounds like, every 10 ms: the note sung, how loud, rumble (pops), brightness, clipping.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestFolder.h"
#include "cc/Features.h"

#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double pi = 3.14159265358979;

    void tone (std::vector<float>& out, double from, double to, double hz, float amp)
    {
        for (size_t i = (size_t) (from * 16000.0); i < (size_t) (to * 16000.0) && i < out.size(); ++i)
            out[i] += amp * (float) std::sin (2.0 * pi * hz * (double) i / 16000.0);
    }
}

TEST_CASE ("The note sung is found")
{
    for (double hz : { 98.0, 220.0, 330.0, 523.25, 880.0 })
    {
        std::vector<float> x (640, 0.0f);
        tone (x, 0.0, 1.0, hz, 0.5f);
        // a sung note has overtones too
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += 0.25f * (float) std::sin (4.0 * pi * hz * (double) i / 16000.0) + 0.1f * (float) std::sin (6.0 * pi * hz * (double) i / 16000.0);
        float periodic = 0.0f;
        const float found = cc::findPitch (x.data(), x.size(), periodic);
        CHECK (std::abs (cc::centsBetween (found, hz)) < 10.0);
        CHECK (periodic > 0.85f);
    }

    // noise isn't a note
    std::vector<float> noise (640);
    unsigned seed = 7;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) ((seed >> 8) / 16777216.0 - 0.5);
    }
    float periodic = 1.0f;
    CHECK (cc::findPitch (noise.data(), noise.size(), periodic) == 0.0f);
    CHECK (periodic < 0.65f);

    CHECK (cc::centsBetween (440.0, 220.0) == Approx (1200.0));
    CHECK (cc::centsBetween (440.0, 440.0 * std::pow (2.0, -50.0 / 1200.0)) == Approx (50.0));
}

TEST_CASE ("A take's sound every 10 ms: notes, loudness, rumble, brightness, clipping")
{
    std::vector<float> take (16000 * 4, 0.0f);
    tone (take, 1.0, 2.0, 220.0, 0.3f);                       // a held note
    tone (take, 2.5, 2.56, 40.0, 0.6f);                       // a pop: a short low thump
    tone (take, 3.0, 3.5, 330.0, 1.4f);                       // too loud: clipped
    for (auto& v : take)
        v = std::clamp (v, -1.0f, 1.0f);

    const auto f = cc::analyseTake (take, {});
    REQUIRE (f.frames() == 400);
    CHECK (f.frameAt (1.5) == 150);
    CHECK (f.frameAt (-3.0) == 0);
    CHECK (f.frameAt (99.0) == 399);

    // the held note: 220 Hz, steady
    for (size_t i = 110; i < 190; ++i)
        CHECK (std::abs (cc::centsBetween (f.pitch[i], 220.0)) < 15.0);
    CHECK (f.pitch[50] == 0.0f);                              // silence: no note
    CHECK (f.level[150] > -15.0f);
    CHECK (f.level[50] < -80.0f);

    // the pop: rumble well above the note's
    CHECK (f.lowBand[252] > f.lowBand[150] + 10.0f);
    // brightness: the clipped note is brighter (clipping adds high overtones)
    CHECK (f.highBand[320] > f.highBand[150] + 10.0f);
    // clipping
    CHECK (f.peak[320] >= 0.999f);
    CHECK (f.peak[150] < 0.35f);
}

TEST_CASE ("A take's sound is kept next to it")
{
    cctest::TestFolder dir;
    const auto take = dir.path / "Take 1.wav";
    std::vector<float> audio (16000, 0.0f);
    tone (audio, 0.2, 0.8, 196.0, 0.4f);
    const auto f = cc::analyseTake (audio, std::vector<float> (100, 0.5f));
    REQUIRE (cc::saveFeatures (take, f));
    CHECK (cc::featuresFileFor (take).filename() == "Take 1.feat");
    const auto back = cc::loadFeatures (take);
    REQUIRE (back.has_value());
    CHECK (*back == f);
    CHECK (back->peak[10] == 0.5f);                           // the louder of the channels' peaks is kept
    CHECK_FALSE (cc::loadFeatures (dir.path / "Take 2.wav").has_value());
}
