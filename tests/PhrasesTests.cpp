// Phrases from gaps and breaths, and changing sample rates for speech recognition.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cc/Phrases.h"
#include "cc/Resample.h"

#include <cmath>

using Catch::Approx;

namespace
{
    // a "singer": tone bursts at the given times, faint breathing noise in between
    std::vector<float> sing (double rate, std::initializer_list<std::pair<double, double>> parts, double length)
    {
        std::vector<float> out ((size_t) (rate * length));
        unsigned seed = 1;
        for (size_t i = 0; i < out.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            out[i] = 0.0005f * ((float) (seed >> 8) / 16777216.0f - 0.5f); // room noise, about -70 dB
        }
        for (auto& [a, b] : parts)
            for (size_t i = (size_t) (a * rate); i < (size_t) (b * rate) && i < out.size(); ++i)
                out[i] += 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / rate);
        return out;
    }
}

TEST_CASE ("Phrases are found between the gaps")
{
    const double rate = 48000;
    const auto take = sing (rate, { { 1.0, 3.0 }, { 3.15, 4.0 }, { 5.0, 7.5 }, { 8.0, 8.1 } }, 10.0);
    const auto phrases = cc::findPhrases (take.data(), take.size(), rate);
    REQUIRE (phrases.size() == 2);                  // the 0.15 s gap is a breath inside a phrase; 0.1 s blip dropped
    CHECK (phrases[0].start == Approx (1.0).margin (0.02));
    CHECK (phrases[0].end == Approx (4.0).margin (0.02));
    CHECK (phrases[1].start == Approx (5.0).margin (0.02));
    CHECK (phrases[1].end == Approx (7.5).margin (0.02));
}

TEST_CASE ("Silence has no phrases")
{
    std::vector<float> quiet (48000 * 3, 0.0f);
    CHECK (cc::findPhrases (quiet.data(), quiet.size(), 48000).empty());
    CHECK (cc::findPhrases (nullptr, 0, 48000).empty());
}

TEST_CASE ("Resampling keeps the length and the pitch")
{
    const double from = 48000, to = 16000;
    std::vector<float> tone ((size_t) from);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = (float) std::sin (2.0 * 3.14159265 * 440.0 * (double) i / from);
    const auto out = cc::resample (tone, from, to);
    CHECK (out.size() == 16000);
    // count zero crossings in the middle: a 440 Hz tone crosses 880 times a second
    int crossings = 0;
    for (size_t i = 4000; i < 12000; ++i)
        crossings += (out[i - 1] < 0) != (out[i] < 0);
    CHECK (crossings == Approx (440).margin (3));
    // level kept (no gain change)
    float peak = 0;
    for (size_t i = 4000; i < 12000; ++i)
        peak = std::max (peak, std::abs (out[i]));
    CHECK (peak == Approx (1.0f).margin (0.02f));

    // a tone above the new Nyquist (10 kHz -> 16 kHz) is filtered out, not folded down as a whistle
    std::vector<float> high ((size_t) from);
    for (size_t i = 0; i < high.size(); ++i)
        high[i] = (float) std::sin (2.0 * 3.14159265 * 10000.0 * (double) i / from);
    const auto gone = cc::resample (high, from, to);
    float rest = 0;
    for (size_t i = 4000; i < 12000; ++i)
        rest = std::max (rest, std::abs (gone[i]));
    CHECK (rest < 0.01f);

    const std::vector<std::vector<float>> stereo { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    CHECK (cc::toMono (stereo) == std::vector<float> { 0.5f, 0.5f });
}
