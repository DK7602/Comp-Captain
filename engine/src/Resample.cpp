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

        std::vector<float> out (outLength);
        for (size_t j = 0; j < outLength; ++j)
        {
            const double t = (double) j / ratio;               // where this output sample sits in the input
            const auto first = (long long) std::ceil (t - halfWidth);
            const auto last = (long long) std::floor (t + halfWidth);
            double sum = 0.0;
            for (long long k = std::max (0ll, first); k <= std::min (n - 1, last); ++k)
            {
                const double x = t - (double) k;
                const double arg = pi * cutoff * x;
                const double sinc = std::abs (arg) < 1e-9 ? 1.0 : std::sin (arg) / arg;
                const double w = x / halfWidth;                // Blackman window, -1 .. 1
                const double window = 0.42 + 0.5 * std::cos (pi * w) + 0.08 * std::cos (2.0 * pi * w);
                sum += (double) input[(size_t) k] * cutoff * sinc * window;
            }
            out[j] = (float) sum;
        }
        return out;
    }
}
