#include "cc/PeakMeter.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using namespace cc;

TEST_CASE ("PeakMeter finds the loudest sample on any channel, then resets")
{
    PeakMeter m;
    std::vector<float> left { 0.1f, -0.2f, 0.05f }, right { 0.0f, 0.3f, -0.75f };
    const float* chans[] = { left.data(), right.data() };
    m.process (chans, 2, 3);
    CHECK (m.peek() == 0.75f);
    CHECK (m.readAndReset() == 0.75f);
    CHECK (m.readAndReset() == 0.0f);
}

TEST_CASE ("PeakMeter keeps the highest peak across blocks until read")
{
    PeakMeter m;
    std::vector<float> a { 0.5f }, b { 0.25f };
    const float* ca[] = { a.data() };
    const float* cb[] = { b.data() };
    m.process (ca, 1, 1);
    m.process (cb, 1, 1);
    CHECK (m.readAndReset() == 0.5f);
}

TEST_CASE ("PeakMeter never changes the audio it reads")
{
    PeakMeter m;
    std::vector<float> data (512);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = std::sin ((float) i * 0.1f) * 0.9f;
    const auto before = data;
    const float* chans[] = { data.data() };
    m.process (chans, 1, (int) data.size());
    CHECK (data == before);
}

TEST_CASE ("PeakMeter shrugs off bad input")
{
    PeakMeter m;
    m.process (nullptr, 2, 64);
    const float* none[] = { nullptr };
    m.process (none, 1, 64);
    std::vector<float> d { std::numeric_limits<float>::quiet_NaN(), 0.2f };
    const float* chans[] = { d.data() };
    m.process (chans, 1, 0);
    CHECK (m.peek() == 0.0f);
    m.process (chans, 1, 2);
    CHECK (m.peek() == 0.2f);
}

TEST_CASE ("PeakMeter is safe with the audio thread and window at the same time")
{
    PeakMeter m;
    std::vector<float> d (256, 0.5f);
    const float* chans[] = { d.data() };
    std::thread audio ([&] { for (int i = 0; i < 20000; ++i) m.process (chans, 1, 256); });
    float seen = 0.0f;
    for (int i = 0; i < 2000; ++i)
        seen = std::max (seen, m.readAndReset());
    audio.join();
    seen = std::max (seen, m.readAndReset());
    CHECK (seen == 0.5f);
}
