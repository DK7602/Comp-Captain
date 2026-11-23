#include "cc/PeakMeter.h"

#include <cmath>

namespace cc
{
    void PeakMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
            return;

        float block = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float* data = channels[c];
            if (data == nullptr)
                continue;
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = std::fabs (data[i]);
                if (a > block)
                    block = a; // NaN never wins this comparison, so it can't poison the meter
            }
        }

        float current = peak.load (std::memory_order_relaxed);
        while (block > current && ! peak.compare_exchange_weak (current, block, std::memory_order_relaxed))
        {
        }
    }

    float PeakMeter::readAndReset() noexcept
    {
        return peak.exchange (0.0f, std::memory_order_relaxed);
    }
}
