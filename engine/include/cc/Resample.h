// Comp Captain engine: changing a take's sample rate (for speech recognition, which listens at 16 kHz).
// A windowed-sinc filter: no aliasing "whistles", good enough to hear every word.
#pragma once

#include <vector>

namespace cc
{
    // Mixes the channels to mono and resamples from `fromRate` to `toRate`.
    std::vector<float> toMono (const std::vector<std::vector<float>>& channels);
    std::vector<float> resample (const std::vector<float>& input, double fromRate, double toRate);
}
