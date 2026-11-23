#include "cc/Phrases.h"

#include <algorithm>
#include <cmath>

namespace cc
{
    std::vector<Phrase> findPhrases (const float* mono, size_t numSamples, double sampleRate, const PhraseOptions& o)
    {
        if (mono == nullptr || numSamples == 0 || sampleRate <= 0)
            return {};

        // loudness every 10 ms (RMS in dB)
        const auto hop = std::max<size_t> (1, (size_t) std::lround (sampleRate * 0.010));
        const size_t frames = numSamples / hop;
        if (frames == 0)
            return {};
        std::vector<double> db (frames);
        for (size_t f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            for (size_t i = f * hop; i < (f + 1) * hop; ++i)
                sum += (double) mono[i] * mono[i];
            db[f] = 10.0 * std::log10 (sum / (double) hop + 1e-12);
        }

        // "loud" is set by the take itself: the level the loudest tenth of it reaches
        std::vector<double> sorted = db;
        std::sort (sorted.begin(), sorted.end());
        const double loud = sorted[(size_t) ((double) (frames - 1) * 0.9)];
        const double floorDb = sorted[(size_t) ((double) (frames - 1) * 0.1)];
        if (loud - floorDb < 10.0 && loud < -50.0)
            return {}; // silence, or nothing but noise
        const double threshold = std::max (loud + o.quietBelowDb, floorDb + 6.0);

        // sound / no sound, then close short gaps and drop short bits
        std::vector<Phrase> raw;
        bool in = false;
        size_t begin = 0;
        for (size_t f = 0; f <= frames; ++f)
        {
            const bool on = f < frames && db[f] > threshold;
            if (on && ! in)
            {
                in = true;
                begin = f;
            }
            else if (! on && in)
            {
                in = false;
                raw.push_back ({ (double) begin * 0.010, (double) f * 0.010 });
            }
        }
        std::vector<Phrase> merged;
        for (auto& p : raw)
        {
            if (! merged.empty() && p.start - merged.back().end < o.shortestGap)
                merged.back().end = p.end;
            else
                merged.push_back (p);
        }
        std::vector<Phrase> out;
        for (auto& p : merged)
            if (p.end - p.start >= o.shortestPhrase)
                out.push_back (p);
        return out;
    }
}
