// Comp Captain engine: a take's waveform picture. The loudest sample in every 1/100 second, stored as
// one byte (0..255, on a square-root scale so quiet singing still shows). About 6 KB per minute.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    class OverviewBuilder
    {
    public:
        static constexpr double defaultSlicesPerSecond = 100.0;

        explicit OverviewBuilder (double sampleRate = 48000.0, double slicesPerSecond = defaultSlicesPerSecond);

        void reset (double sampleRate, double slicesPerSecond = defaultSlicesPerSecond);
        void add (const float* const* channels, int numChannels, int numFrames);
        void addInterleaved (const float* frames, int numChannels, int numFrames);
        void addSilence (std::int64_t numFrames);
        void finish(); // keeps the last part-filled slice

        const std::vector<std::uint8_t>& peaks() const noexcept { return slices; }
        double slicesPerSecond() const noexcept { return perSecond; }
        float loudest() const noexcept { return maxPeak; } // linear, over the whole take

    private:
        void push (float peak);

        std::vector<std::uint8_t> slices;
        double perSecond = defaultSlicesPerSecond;
        double framesPerSlice = 480.0;
        double sliceEnd = 480.0;  // in frames from the start
        std::int64_t frame = 0;
        float current = 0.0f;
        bool hasCurrent = false;
        float maxPeak = 0.0f;
    };

    std::uint8_t peakToByte (float linearPeak) noexcept;

    // Plain base64 (for sending the picture to the window in one short string).
    std::string toBase64 (const std::uint8_t* data, size_t size);
    std::vector<std::uint8_t> fromBase64 (std::string_view);
}
