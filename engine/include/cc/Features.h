// Comp Captain engine: what a take sounds like, every 10 ms. Worked out once per take in the background (when it's
// listened to) and kept next to it ("Take 3.feat"), so scoring a line is quick.
//  - pitch: the note being sung (Hz, 0 where nothing is sung or it's not a clear note)
//  - level: how loud (dB), lowBand: 20-60 Hz (pops: bursts of air on the mic), highBand: above 2 kHz (brightness)
//  - peak: the loudest sample; clipped: samples squashed flat at full scale (real clipping, not just a loud peak)
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    struct TakeFeatures
    {
        static constexpr double hop = 0.010; // seconds per frame
        static constexpr int version = 2;

        std::vector<float> pitch;     // Hz, 0 = no clear note
        std::vector<float> periodic;  // 0..1: how clearly it's a note (1 = a pure tone)
        std::vector<float> level;     // dB (RMS)
        std::vector<float> lowBand;   // dB, 20 to 60 Hz
        std::vector<float> highBand;  // dB, above 2 kHz
        std::vector<float> peak;      // the loudest sample, 0..1 and more (any channel)
        std::vector<float> clipped;   // samples where the wave was stuck flat (3+ equal loud samples in a row)

        size_t frames() const noexcept { return level.size(); }
        bool empty() const noexcept { return level.empty(); }
        // the frame at a time in the take (clamped)
        size_t frameAt (double seconds) const noexcept;

        bool operator== (const TakeFeatures&) const = default;
    };

    // mono16k: the take in mono at 16 kHz (what speech recognition hears too).
    // peaks / clipped: per 10 ms at the take's own rate, the loudest sample of any channel and how many samples were
    // stuck at full scale (clipping can hide in one channel of a stereo take); may be empty.
    TakeFeatures analyseTake (const std::vector<float>& mono16k, const std::vector<float>& peaks, const std::vector<float>& clipped = {});

    // The note in Hz in one stretch of 16 kHz audio (YIN), 0 if it isn't a clear note. periodic gets 0..1.
    float findPitch (const float* audio16k, size_t length, float& periodic);

    // Cents between two pitches (positive: a is higher).
    double centsBetween (double a, double b);

    // Saved next to the take ("Take 3.wav" -> "Take 3.feat"), small and binary.
    std::filesystem::path featuresFileFor (const std::filesystem::path& takeFile);
    bool saveFeatures (const std::filesystem::path& takeFile, const TakeFeatures&);
    std::optional<TakeFeatures> loadFeatures (const std::filesystem::path& takeFile);
}
