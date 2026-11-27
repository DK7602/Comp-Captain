#include "cc/Features.h"

#include "cc/Resample.h"
#include "cc/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace cc
{
    namespace
    {
        constexpr double rate = 16000.0;
        constexpr size_t hopSamples = 160;      // 10 ms
        constexpr size_t pitchWindow = 640;     // 40 ms: two periods of the lowest note
        constexpr double lowestNote = 65.0;     // Hz (C2)
        constexpr double highestNote = 1100.0;  // Hz (about C#6)

        // A two-pole filter (the usual "cookbook" biquad), run over the whole take.
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            static Biquad lowPass (double hz)
            {
                const double w = 2.0 * 3.14159265358979 * hz / rate, c = std::cos (w), s = std::sin (w), q = 0.7071;
                const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
                Biquad f;
                f.b0 = (1.0 - c) / 2.0 / a0; f.b1 = (1.0 - c) / a0; f.b2 = f.b0;
                f.a1 = -2.0 * c / a0; f.a2 = (1.0 - alpha) / a0;
                return f;
            }
            static Biquad highPass (double hz)
            {
                const double w = 2.0 * 3.14159265358979 * hz / rate, c = std::cos (w), s = std::sin (w), q = 0.7071;
                const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
                Biquad f;
                f.b0 = (1.0 + c) / 2.0 / a0; f.b1 = -(1.0 + c) / a0; f.b2 = f.b0;
                f.a1 = -2.0 * c / a0; f.a2 = (1.0 - alpha) / a0;
                return f;
            }
            float run (float x)
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return (float) y;
            }
        };

        float frameDb (const float* x, size_t n)
        {
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i)
                sum += (double) x[i] * x[i];
            return (float) (10.0 * std::log10 (sum / (double) std::max<size_t> (1, n) + 1e-12));
        }
    }

    size_t TakeFeatures::frameAt (double seconds) const noexcept
    {
        if (level.empty())
            return 0;
        const auto f = (long long) std::floor (seconds / hop);
        return (size_t) std::clamp<long long> (f, 0, (long long) level.size() - 1);
    }

    double centsBetween (double a, double b)
    {
        if (a <= 0 || b <= 0)
            return 0.0;
        return 1200.0 * std::log2 (a / b);
    }

    float findPitch (const float* x, size_t length, float& periodic)
    {
        periodic = 0.0f;
        const auto minLag = (size_t) std::floor (rate / highestNote);
        const auto maxLag = (size_t) std::ceil (rate / lowestNote);
        if (x == nullptr || length < maxLag + 64)
            return 0.0f;
        const size_t window = length - maxLag;

        // YIN: how different the sound is from itself one period later, for every possible period
        std::vector<double> d (maxLag + 2, 0.0);
        for (size_t lag = 1; lag <= maxLag + 1 && lag + window <= length; ++lag)
        {
            double sum = 0.0;
            for (size_t j = 0; j < window; ++j)
            {
                const double diff = (double) x[j] - (double) x[j + lag];
                sum += diff * diff;
            }
            d[lag] = sum;
        }
        // normalised by the running mean, so it's 1 on average and dips near 0 at the period
        std::vector<double> nd (d.size(), 1.0);
        double running = 0.0;
        for (size_t lag = 1; lag < d.size(); ++lag)
        {
            running += d[lag];
            nd[lag] = running > 0.0 ? d[lag] * (double) lag / running : 1.0;
        }
        size_t best = 0;
        for (size_t lag = minLag; lag <= maxLag; ++lag)
        {
            if (nd[lag] < 0.15)
            {
                while (lag + 1 <= maxLag && nd[lag + 1] < nd[lag])
                    ++lag;
                best = lag;
                break;
            }
        }
        if (best == 0)
        {
            best = minLag;
            for (size_t lag = minLag; lag <= maxLag; ++lag)
                if (nd[lag] < nd[best])
                    best = lag;
            if (nd[best] > 0.35)
            {
                periodic = (float) std::max (0.0, 1.0 - nd[best]);
                return 0.0f; // not a clear note: breath, a consonant, noise
            }
        }
        // between samples: fit a parabola through the dip
        double lag = (double) best;
        if (best > minLag && best < maxLag)
        {
            const double a = nd[best - 1], b = nd[best], c = nd[best + 1];
            const double denom = a - 2.0 * b + c;
            if (std::abs (denom) > 1e-12)
                lag += 0.5 * (a - c) / denom;
        }
        periodic = (float) std::clamp (1.0 - nd[best], 0.0, 1.0);
        return lag > 0 ? (float) (rate / lag) : 0.0f;
    }

    TakeFeatures analyseTake (const std::vector<float>& audio, const std::vector<float>& peaks, const std::vector<float>& clipped)
    {
        TakeFeatures f;
        const size_t frames = audio.size() / hopSamples;
        if (frames == 0)
            return f;
        f.pitch.assign (frames, 0.0f);
        f.periodic.assign (frames, 0.0f);
        f.level.resize (frames);
        f.lowBand.resize (frames);
        f.highBand.resize (frames);
        f.peak.assign (frames, 0.0f);
        f.clipped.assign (frames, 0.0f);

        // The pop band, 20 to 60 Hz: steep at the top (two filters in a row) so a deep voice's low notes stay out of
        // it, and nothing below 20 Hz (rumble too deep to hear, which some recordings are full of).
        auto lp = Biquad::lowPass (60.0), lp2 = Biquad::lowPass (60.0), sub = Biquad::highPass (20.0), hp = Biquad::highPass (2000.0);
        std::vector<float> low (hopSamples), high (hopSamples);
        for (size_t i = 0; i < frames; ++i)
        {
            const float* x = audio.data() + i * hopSamples;
            float pk = 0.0f;
            int flat = 0, run = 0;
            for (size_t k = 0; k < hopSamples; ++k)
            {
                low[k] = sub.run (lp2.run (lp.run (x[k])));
                high[k] = hp.run (x[k]);
                pk = std::max (pk, std::abs (x[k]));
                run = k > 0 && std::abs (x[k]) >= 0.25f && x[k] == x[k - 1] ? run + 1 : 0;
                flat += run >= 2 ? 1 : 0;
            }
            f.level[i] = frameDb (x, hopSamples);
            f.lowBand[i] = frameDb (low.data(), hopSamples);
            f.highBand[i] = frameDb (high.data(), hopSamples);
            f.peak[i] = i < peaks.size() ? std::max (pk, peaks[i]) : pk;
            f.clipped[i] = i < clipped.size() ? std::max ((float) flat, clipped[i]) : (float) flat;
        }

        // the note, only where something is being sung (the louder part of the take)
        std::vector<float> sorted = f.level;
        std::sort (sorted.begin(), sorted.end());
        const float loud = sorted[(size_t) ((double) (frames - 1) * 0.9)];
        const float floorForNotes = std::max (loud - 35.0f, -65.0f);
        for (size_t i = 0; i < frames; ++i)
        {
            if (f.level[i] < floorForNotes)
                continue;
            const long long centre = (long long) (i * hopSamples + hopSamples / 2);
            const long long start = centre - (long long) pitchWindow / 2;
            if (start < 0 || (size_t) start + pitchWindow > audio.size())
                continue;
            float p = 0.0f;
            f.pitch[i] = findPitch (audio.data() + start, pitchWindow, p);
            f.periodic[i] = p;
        }
        // a lone frame an octave away from both neighbours is the tracker slipping, not the singer
        for (size_t i = 1; i + 1 < frames; ++i)
        {
            const float a = f.pitch[i - 1], b = f.pitch[i], c = f.pitch[i + 1];
            if (a > 0 && b > 0 && c > 0 && std::abs (centsBetween (a, c)) < 100.0)
            {
                const double jump = centsBetween (b, a);
                if (std::abs (std::abs (jump) - 1200.0) < 150.0)
                    f.pitch[i] = jump > 0 ? b / 2.0f : b * 2.0f;
            }
        }
        return f;
    }

    // ------------------------------------------------------------------ the .feat file
    namespace
    {
        constexpr char magic[8] = { 'C', 'C', 'F', 'E', 'A', 'T', '1', 0 };

        template <typename T>
        void put (std::ostream& out, const T& v) { out.write (reinterpret_cast<const char*> (&v), sizeof (T)); }
        template <typename T>
        bool get (std::istream& in, T& v) { return (bool) in.read (reinterpret_cast<char*> (&v), sizeof (T)); }
    }

    std::filesystem::path featuresFileFor (const std::filesystem::path& takeFile)
    {
        auto p = takeFile;
        p.replace_extension (".feat");
        return p;
    }

    bool saveFeatures (const std::filesystem::path& takeFile, const TakeFeatures& f)
    {
        const auto dest = featuresFileFor (takeFile);
        auto temp = dest;
        temp += ".part";
        {
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            out.write (magic, sizeof (magic));
            put (out, (std::int32_t) TakeFeatures::version);
            put (out, (std::uint32_t) f.frames());
            for (const auto* v : { &f.pitch, &f.periodic, &f.level, &f.lowBand, &f.highBand, &f.peak, &f.clipped })
                out.write (reinterpret_cast<const char*> (v->data()), (std::streamsize) (v->size() * sizeof (float)));
            if (! out.good())
                return false;
        }
        std::error_code ec;
        std::filesystem::rename (temp, dest, ec);
        if (ec)
        {
            std::filesystem::remove (temp, ec);
            return false;
        }
        return true;
    }

    std::optional<TakeFeatures> loadFeatures (const std::filesystem::path& takeFile)
    {
        std::ifstream in (featuresFileFor (takeFile), std::ios::binary);
        if (! in.is_open())
            return std::nullopt;
        char m[8] {};
        std::int32_t version = 0;
        std::uint32_t frames = 0;
        if (! in.read (m, sizeof (m)) || std::memcmp (m, magic, sizeof (m)) != 0 || ! get (in, version) || ! get (in, frames))
            return std::nullopt;
        if (version != TakeFeatures::version || frames > 100u * 60u * 60u * 3u) // three hours at most
            return std::nullopt;
        TakeFeatures f;
        for (auto* v : { &f.pitch, &f.periodic, &f.level, &f.lowBand, &f.highBand, &f.peak, &f.clipped })
        {
            v->resize (frames);
            if (! in.read (reinterpret_cast<char*> (v->data()), (std::streamsize) (frames * sizeof (float))))
                return std::nullopt;
        }
        return f;
    }

    std::optional<TakeAudio> readTake (const std::filesystem::path& wav, const std::atomic<bool>* cancel, std::string& error)
    {
        WavReader reader;
        if (! reader.open (wav))
        {
            error = "The take's file couldn't be read: " + utf8FromPath (wav);
            return std::nullopt;
        }
        // in mono, a block at a time (a long stereo take never sits in memory twice)
        const auto& info = reader.info();
        const int channels = std::max (1, std::min (2, info.channels));
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (65536));
        std::vector<float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());
        TakeAudio a;
        a.sampleRate = info.sampleRate;
        if (info.lengthFrames > 0)
            a.mono.reserve ((size_t) info.lengthFrames);
        const auto frameSamples = (size_t) std::max (1.0, std::round (info.sampleRate * TakeFeatures::hop));
        size_t sampleIndex = 0;
        std::vector<int> flatRun ((size_t) channels, 0);
        std::vector<float> previous ((size_t) channels, 0.0f);
        int got = 0;
        while ((got = reader.read (ptrs.data(), channels, 65536)) > 0)
        {
            if (cancel != nullptr && cancel->load())
                return std::nullopt;
            for (int i = 0; i < got; ++i, ++sampleIndex)
            {
                float v = 0.0f, pk = 0.0f;
                bool flat = false;
                for (int c = 0; c < channels; ++c)
                {
                    const float x = block[(size_t) c][(size_t) i];
                    v += x;
                    pk = std::max (pk, std::abs (x));
                    // clipping: the wave stuck flat (3 or more equal samples in a row) at a loud level, at whatever
                    // level it was cut (the DAW's gain may sit between the input and Comp Captain)
                    auto& run = flatRun[(size_t) c];
                    run = std::abs (x) >= 0.25f && x == previous[(size_t) c] ? run + 1 : 0;
                    previous[(size_t) c] = x;
                    flat = flat || run >= 2;
                }
                a.mono.push_back (v / (float) channels);
                const size_t frame = sampleIndex / frameSamples;
                if (frame >= a.peaks.size())
                {
                    a.peaks.push_back (0.0f);
                    a.clipped.push_back (0.0f);
                }
                a.peaks[frame] = std::max (a.peaks[frame], pk);
                a.clipped[frame] += flat ? 1.0f : 0.0f;
            }
        }
        return a;
    }

    std::optional<TakeFeatures> measureTake (const std::filesystem::path& wav, const std::atomic<bool>* cancel, std::string& error)
    {
        auto a = readTake (wav, cancel, error);
        if (! a)
            return std::nullopt;
        const auto audio = resample (a->mono, a->sampleRate, rate);
        if (cancel != nullptr && cancel->load())
            return std::nullopt;
        return analyseTake (audio, a->peaks, a->clipped);
    }
}
