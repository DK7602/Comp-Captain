#include "cc/Resample.h"

#include <algorithm>
#include <cmath>

namespace cc
{
    std::vector<float> toMono (const std::vector<std::vector<float>>& channels)
    {
        if (channels.empty())
            return {};
        size_t n = channels[0].size();
        for (auto& c : channels)
            n = std::min (n, c.size());
        std::vector<float> out (n, 0.0f);
        const float scale = 1.0f / (float) channels.size();
        for (auto& c : channels)
            for (size_t i = 0; i < n; ++i)
                out[i] += c[i] * scale;
        return out;
    }

    std::vector<float> resample (const std::vector<float>& input, double fromRate, double toRate)
    {
        if (input.empty() || fromRate <= 0 || toRate <= 0)
            return {};
        if (std::abs (fromRate - toRate) < 1e-9)
            return input;

        constexpr double pi = 3.14159265358979323846;
        const double ratio = toRate / fromRate;
        const double cutoff = std::min (1.0, ratio) * 0.95;   // of the source's Nyquist frequency
        const double halfWidth = 12.0 / cutoff;                // filter half-length in source samples
        const auto outLength = (size_t) std::floor ((double) input.size() * ratio);
        const auto n = (long long) input.size();

        // The filter (a windowed sinc) is worked out once, finely, and looked up for each sample.
        constexpr int perSample = 256;
        const auto tableHalf = (size_t) std::ceil (halfWidth * perSample) + 2;
        std::vector<float> table (tableHalf + 1);
        for (size_t i = 0; i <= tableHalf; ++i)
        {
            const double x = (double) i / perSample;
            if (x > halfWidth)
            {
                table[i] = 0.0f;
                continue;
            }
            const double arg = pi * cutoff * x;
            const double sinc = arg < 1e-9 ? 1.0 : std::sin (arg) / arg;
            const double w = x / halfWidth;                    // Blackman window, 0 .. 1
            const double window = 0.42 + 0.5 * std::cos (pi * w) + 0.08 * std::cos (2.0 * pi * w);
            table[i] = (float) (cutoff * sinc * window);
        }
        auto kernel = [&] (double x) {
            const double at = std::abs (x) * perSample;
            const auto i = (size_t) at;
            if (i + 1 > tableHalf)
                return 0.0f;
            const float f = (float) (at - (double) i);
            return table[i] + (table[i + 1] - table[i]) * f;
        };

        std::vector<float> out (outLength);
        for (size_t j = 0; j < outLength; ++j)
        {
            const double t = (double) j / ratio;               // where this output sample sits in the input
            const auto first = std::max (0ll, (long long) std::ceil (t - halfWidth));
            const auto last = std::min (n - 1, (long long) std::floor (t + halfWidth));
            float sum = 0.0f;
            for (long long k = first; k <= last; ++k)
                sum += input[(size_t) k] * kernel (t - (double) k);
            out[j] = sum;
        }
        return out;
    }
}
