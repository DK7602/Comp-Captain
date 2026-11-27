#include "cc/Scores.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>

namespace cc
{
    const std::vector<Style>& styles()
    {
        // Rap leans on Timing, Clarity and Energy; singing styles lean on Pitch.
        static const std::vector<Style> list {
            { "Rap", { 0.00, 0.35, 0.35, 0.30 } },
            { "Trap", { 0.10, 0.35, 0.25, 0.30 } },
            { "Singing", { 0.35, 0.25, 0.20, 0.20 } },
            { "Pop", { 0.30, 0.25, 0.25, 0.20 } },
            { "Country", { 0.30, 0.25, 0.25, 0.20 } },
            { "Rock", { 0.25, 0.25, 0.20, 0.30 } },
            { "Worship / Gospel", { 0.35, 0.20, 0.25, 0.20 } },
            { "R&B", { 0.35, 0.20, 0.20, 0.25 } },
            { "My Own", { 0.25, 0.25, 0.25, 0.25 } },
        };
        return list;
    }

    std::optional<ScoreWeights> styleWeights (std::string_view name)
    {
        for (auto& s : styles())
            if (s.name == name)
                return s.weights;
        return std::nullopt;
    }

    std::string displayWord (std::string_view w)
    {
        auto keep = [] (char c) { return std::isalnum ((unsigned char) c) || (unsigned char) c >= 0x80; };
        size_t a = 0, b = w.size();
        while (a < b && ! keep (w[a]))
            ++a;
        while (b > a && ! keep (w[b - 1]))
            --b;
        return std::string (w.substr (a, b - a));
    }

    double totalFor (const LineScore& s, const ScoreWeights& w)
    {
        double sum = 0.0, weight = 0.0;
        const std::pair<float, double> parts[] = { { s.pitch, w.pitch }, { s.timing, w.timing }, { s.clarity, w.clarity }, { s.energy, w.energy } };
        for (auto& [score, wt] : parts)
        {
            if (score < 0.0f || wt <= 0.0)
                continue;
            sum += score * wt;
            weight += wt;
        }
        if (weight <= 0.0)
            return 0.0;
        return std::max (0.0, sum / weight - std::min (8.0, 3.0 * (double) s.problems.size()));
    }

    std::uint64_t bestTake (const LineScores& line, const ScoreWeights& w, const std::vector<TakeForScoring>& takes)
    {
        auto favourite = [&] (std::uint64_t key) {
            for (auto& t : takes)
                if (t.key == key)
                    return t.favourite;
            return false;
        };
        const LineScore* best = nullptr;
        double bestTotal = -1.0;
        for (auto& s : line.takes)
        {
            const double total = totalFor (s, w);
            const bool better = best == nullptr || total > bestTotal + 0.5
                                || (std::abs (total - bestTotal) <= 0.5 && favourite (s.take) && ! favourite (best->take))
                                || (std::abs (total - bestTotal) <= 0.5 && favourite (s.take) == favourite (best->take) && total > bestTotal);
            if (better)
            {
                best = &s;
                bestTotal = total;
            }
        }
        return best != nullptr ? best->take : 0;
    }

    namespace
    {
        constexpr double nothing = std::numeric_limits<double>::quiet_NaN();

        double median (std::vector<double> v)
        {
            v.erase (std::remove_if (v.begin(), v.end(), [] (double x) { return std::isnan (x); }), v.end());
            if (v.empty())
                return nothing;
            std::sort (v.begin(), v.end());
            const size_t n = v.size();
            return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
        }

        double fold (double cents, double span = 1200.0) // into -span/2 .. span/2
        {
            cents = std::fmod (cents, span);
            if (cents > span / 2)
                cents -= span;
            if (cents < -span / 2)
                cents += span;
            return cents;
        }

        // Dynamic time warping: the best way to step through two melodies together (one may be sung a little
        // earlier or later). Returns the root-mean-square difference along the way, in cents (octaves folded),
        // leaving out the worst tenth (where one take's line starts or ends a little sooner than the other's),
        // and for each point of a, the point of b it was matched with.
        double warpDifference (const std::vector<double>& a, const std::vector<double>& b, std::vector<int>* matchOfA = nullptr)
        {
            const size_t n = a.size(), m = b.size();
            if (n == 0 || m == 0)
                return nothing;
            const double inf = std::numeric_limits<double>::infinity();
            std::vector<double> cost ((n + 1) * (m + 1), inf);
            std::vector<unsigned char> from ((n + 1) * (m + 1), 0);
            auto at = [m] (size_t i, size_t j) { return i * (m + 1) + j; };
            auto diff = [&] (size_t i, size_t j) { return std::min (300.0, std::abs (fold (a[i] - b[j]))); };
            cost[at (0, 0)] = 0.0;
            for (size_t i = 1; i <= n; ++i)
            {
                for (size_t j = 1; j <= m; ++j)
                {
                    const double d = diff (i - 1, j - 1);
                    double best = cost[at (i - 1, j - 1)];
                    unsigned char f = 0;
                    if (cost[at (i - 1, j)] < best) { best = cost[at (i - 1, j)]; f = 1; }
                    if (cost[at (i, j - 1)] < best) { best = cost[at (i, j - 1)]; f = 2; }
                    cost[at (i, j)] = best + d * d;
                    from[at (i, j)] = f;
                }
            }
            if (matchOfA != nullptr)
                matchOfA->assign (n, 0);
            std::vector<double> steps;
            size_t i = n, j = m;
            while (i > 0 && j > 0)
            {
                if (matchOfA != nullptr)
                    (*matchOfA)[i - 1] = (int) (j - 1);
                steps.push_back (diff (i - 1, j - 1));
                const auto f = from[at (i, j)];
                if (f == 0) { --i; --j; }
                else if (f == 1) --i;
                else --j;
            }
            std::sort (steps.begin(), steps.end());
            const size_t keep = std::max<size_t> (1, steps.size() - steps.size() / 10);
            double sum = 0.0;
            for (size_t k = 0; k < keep; ++k)
                sum += steps[k] * steps[k];
            return std::sqrt (sum / (double) keep);
        }

        // How a take sounds around a line, every 10 ms of song time, for lining takes up with each other: how loud
        // (against the take's own loud parts, in 10 dB steps), how sharply it's rising (syllables starting) and how
        // bright. Before the take started or after it ended counts as quiet.
        struct Shape { float level, rise, tilt; };
        std::vector<Shape> shapeOf (const TakeForScoring& t, double loud, double songFrom, size_t frames)
        {
            const auto& f = *t.features;
            std::vector<Shape> out (frames, Shape { -3.0f, 0.0f, 0.0f });
            for (size_t k = 0; k < frames; ++k)
            {
                const double tt = songFrom + (double) k * TakeFeatures::hop - t.startSeconds;
                if (tt < 0.0)
                    continue;
                const auto i = (size_t) std::floor (tt / TakeFeatures::hop);
                if (i >= f.frames())
                    break;
                const float lv = (float) std::clamp ((f.level[i] - loud) / 10.0, -3.0, 0.5);
                const float before = i >= 2 ? (float) std::clamp ((f.level[i - 2] - loud) / 10.0, -3.0, 0.5) : lv;
                out[k] = { lv, std::clamp (lv - before, -1.0f, 2.0f), lv > -2.5f ? (float) std::clamp ((f.highBand[i] - f.level[i] + 20.0) / 10.0, -2.0, 2.0) : 0.0f };
            }
            return out;
        }

        // Two takes' shapes stepped through together (within half a second of each other). Gives, in seconds, how far
        // apart they are on average where something is sung (signed: positive = a is later) and the average distance.
        void timingBetween (const std::vector<Shape>& a, const std::vector<Shape>& b, double& signedOff, double& absOff)
        {
            signedOff = absOff = nothing;
            const size_t n = std::min (a.size(), b.size());
            if (n < 10)
                return;
            constexpr int band = 50;
            const double inf = std::numeric_limits<double>::infinity();
            auto at = [n] (size_t i, size_t j) { return i * (n + 1) + j; };
            std::vector<double> cost ((n + 1) * (n + 1), inf);
            std::vector<unsigned char> from ((n + 1) * (n + 1), 0);
            auto d = [&] (size_t i, size_t j) {
                const double dl = a[i].level - b[j].level, dr = a[i].rise - b[j].rise, dt = a[i].tilt - b[j].tilt;
                return dl * dl + 2.0 * dr * dr + 0.25 * dt * dt;
            };
            cost[at (0, 0)] = 0.0;
            for (size_t i = 1; i <= n; ++i)
            {
                const size_t j0 = i > (size_t) band ? i - band : 1, j1 = std::min (n, i + band);
                for (size_t j = j0; j <= j1; ++j)
                {
                    double best = cost[at (i - 1, j - 1)];
                    unsigned char f = 0;
                    // a small cost for stepping one take on its own, so the path stays straight where nothing's sung
                    if (cost[at (i - 1, j)] + 0.02 < best) { best = cost[at (i - 1, j)] + 0.02; f = 1; }
                    if (cost[at (i, j - 1)] + 0.02 < best) { best = cost[at (i, j - 1)] + 0.02; f = 2; }
                    cost[at (i, j)] = best + d (i - 1, j - 1);
                    from[at (i, j)] = f;
                }
            }
            double sum = 0.0, sumAbs = 0.0;
            int count = 0;
            size_t i = n, j = n;
            while (i > 0 && j > 0)
            {
                if (a[i - 1].level > -2.0f && b[j - 1].level > -2.0f)
                {
                    const double off = ((double) i - (double) j) * TakeFeatures::hop;
                    sum += off;
                    sumAbs += std::abs (off);
                    ++count;
                }
                const auto f = from[at (i, j)];
                if (f == 0) { --i; --j; }
                else if (f == 1) --i;
                else --j;
            }
            if (count >= 10)
            {
                signedOff = sum / count;
                absOff = sumAbs / count;
            }
        }

        // One take singing one line.
        struct Sung
        {
            const TakeForScoring* take = nullptr;
            const LinePlace* place = nullptr;
            size_t f0 = 0, f1 = 0;                 // its frames
            std::vector<double> melody;            // cents from A440, every 20 ms where a clear note is sung
            std::vector<size_t> melodyFrame;
            double level = nothing, bright = nothing;
            double startSong = 0.0, endSong = 0.0; // the line in the song (seconds)
            LineScore score;
            // working values
            double pitchAgreement = nothing, pitchGrid = nothing;
        };

        double takeLoudness (const TakeFeatures& f)
        {
            std::vector<double> v (f.level.begin(), f.level.end());
            std::sort (v.begin(), v.end());
            return v.empty() ? -100.0 : v[(size_t) ((double) (v.size() - 1) * 0.9)];
        }

        // the take's quiet level: the quietest tenth of it (leaving out digital silence)
        double noiseFloor (const TakeFeatures& f)
        {
            std::vector<double> v;
            for (float l : f.level)
                if (l > -100.0f)
                    v.push_back (l);
            if (v.size() < 20)
                return nothing;
            std::sort (v.begin(), v.end());
            return v[v.size() / 10];
        }

        // frames where the sound jumps up (a word or a syllable starting)
        std::vector<size_t> onsets (const TakeFeatures& f)
        {
            std::vector<size_t> out;
            for (size_t i = 3; i < f.frames(); ++i)
            {
                const float before = std::min ({ f.level[i - 1], f.level[i - 2], f.level[i - 3] });
                if (f.level[i] > -50.0f && f.level[i] - before >= 6.0f && (out.empty() || i - out.back() > 5))
                    out.push_back (i);
            }
            return out;
        }

        std::string quote (const std::vector<std::string>& shown, size_t i)
        {
            return i < shown.size() ? "\"" + displayWord (shown[i]) + "\"" : std::string();
        }

        // the shown word being sung at a frame (nearest by its start), or -1
        int wordAt (const Sung& s, size_t frame)
        {
            const double t = (double) frame * TakeFeatures::hop;
            int best = -1;
            double bestD = 1e9;
            for (size_t i = 0; i < s.place->words.size(); ++i)
            {
                const auto& w = s.place->words[i];
                const double d = t < w.start ? w.start - t : (t > w.end ? t - w.end : 0.0);
                if (d < bestD)
                {
                    bestD = d;
                    best = (int) i;
                }
            }
            return bestD < 0.4 ? best : -1;
        }
    }

    std::vector<LineScores> scoreLines (const Lyrics& lyrics, const std::vector<TakeForScoring>& takes)
    {
        std::vector<LineScores> out (lyrics.lines.size());
        for (size_t i = 0; i < out.size(); ++i)
            out[i].line = (int) i;

        // per take: how loud it gets, its quiet level, where its syllables start
        struct TakeInfo { double loud = -100, noise = nothing; std::vector<size_t> onsets; };
        std::map<std::uint64_t, TakeInfo> info;
        for (auto& t : takes)
        {
            if (t.features == nullptr || t.features->empty())
                continue;
            auto& ti = info[t.key];
            ti.loud = takeLoudness (*t.features);
            ti.noise = noiseFloor (*t.features);
            ti.onsets = onsets (*t.features);
        }

        // the singer's own tuning (a whole song can sit a few cents off A440): the usual distance from the
        // semitones, over every clear note of every take
        double tuning = 0.0;
        {
            double sx = 0.0, sy = 0.0;
            for (auto& t : takes)
                if (t.features != nullptr)
                    for (size_t i = 0; i < t.features->frames(); ++i)
                        if (t.features->pitch[i] > 0 && t.features->periodic[i] >= 0.6f)
                        {
                            const double c = centsBetween (t.features->pitch[i], 440.0);
                            const double angle = 2.0 * 3.14159265358979 * c / 100.0;
                            sx += std::cos (angle);
                            sy += std::sin (angle);
                        }
            if (sx != 0.0 || sy != 0.0)
                tuning = std::atan2 (sy, sx) * 100.0 / (2.0 * 3.14159265358979);
        }

        for (size_t li = 0; li < lyrics.lines.size(); ++li)
        {
            const auto shown = shownWords (lyrics.lines[li].text);

            // every take that sang this line
            std::vector<Sung> sung;
            for (auto& t : takes)
            {
                if (t.features == nullptr || t.features->empty() || t.places == nullptr || info.count (t.key) == 0)
                    continue;
                for (auto& p : *t.places)
                {
                    if (p.line != (int) li || p.sure <= 0.0f || p.end <= p.start)
                        continue;
                    Sung s;
                    s.take = &t;
                    s.place = &p;
                    s.f0 = t.features->frameAt (p.start);
                    s.f1 = std::max (s.f0 + 1, t.features->frameAt (p.end));
                    s.score.take = t.key;
                    s.score.line = (int) li;
                    s.startSong = t.startSeconds + p.start;
                    s.endSong = t.startSeconds + p.end;
                    sung.push_back (std::move (s));
                    break;
                }
            }
            if (sung.empty())
                continue;

            for (auto& s : sung)
            {
                const auto& f = *s.take->features;
                const auto& ti = info[s.take->key];

                // the melody: clear notes, smoothed, every 20 ms
                std::vector<double> cents (s.f1 - s.f0, nothing);
                for (size_t i = s.f0; i < s.f1; ++i)
                    if (f.pitch[i] > 0 && f.periodic[i] >= 0.6f)
                        cents[i - s.f0] = centsBetween (f.pitch[i], 440.0);
                for (size_t i = 0; i < cents.size(); i += 2)
                {
                    if (std::isnan (cents[i]))
                        continue;
                    std::vector<double> around;
                    for (size_t k = i >= 2 ? i - 2 : 0; k <= i + 2 && k < cents.size(); ++k)
                        around.push_back (cents[k]);
                    s.melody.push_back (median (around));
                    s.melodyFrame.push_back (s.f0 + i);
                }

                // delivery: how loud and how bright, where it's sung
                std::vector<double> lv, br;
                for (size_t i = s.f0; i < s.f1; ++i)
                    if (f.level[i] >= ti.loud - 30.0)
                    {
                        lv.push_back (f.level[i]);
                        br.push_back (f.highBand[i] - f.level[i]);
                    }
                s.level = median (lv);
                s.bright = median (br);
            }

            // ------------------------------------------------------------------ Pitch
            for (auto& s : sung)
            {
                if (s.melody.size() < 8)
                    continue; // hardly any clear notes (spoken, rapped, whispered): no pitch score
                // against the takes that sing it most alike (the nearest half): the melody most of your takes agree on
                std::vector<double> diffs;
                for (auto& o : sung)
                    if (&o != &s && o.melody.size() >= 8)
                        diffs.push_back (warpDifference (s.melody, o.melody));
                diffs.erase (std::remove_if (diffs.begin(), diffs.end(), [] (double d) { return std::isnan (d); }), diffs.end());
                if (! diffs.empty())
                {
                    std::sort (diffs.begin(), diffs.end());
                    const size_t nearest = std::max<size_t> (1, (diffs.size() + 1) / 2);
                    double sum = 0.0;
                    for (size_t k = 0; k < nearest; ++k)
                        sum += diffs[k];
                    s.pitchAgreement = sum / (double) nearest;
                }
                // distance from the semitones (in the singer's own tuning), on steady notes
                std::vector<double> grid;
                for (size_t i = 2; i + 2 < s.melody.size(); ++i)
                    if (std::abs (s.melody[i + 2] - s.melody[i - 2]) < 30.0)
                        grid.push_back (std::abs (fold (s.melody[i] - tuning, 100.0)));
                s.pitchGrid = grid.size() >= 4 ? median (grid) : nothing;
                double off = nothing;
                if (! std::isnan (s.pitchAgreement))
                    off = std::isnan (s.pitchGrid) ? s.pitchAgreement : 0.75 * s.pitchAgreement + 0.25 * s.pitchGrid;
                else if (! std::isnan (s.pitchGrid))
                    off = s.pitchGrid;
                if (std::isnan (off))
                    continue;
                s.score.pitch = (float) (100.0 - std::clamp (off - 8.0, 0.0, 100.0));
            }
            // the take closest to what the others sing is the reference for "sharp / flat on a word"
            const Sung* reference = nullptr;
            for (auto& s : sung)
                if (! std::isnan (s.pitchAgreement) && (reference == nullptr || s.pitchAgreement < reference->pitchAgreement))
                    reference = &s;
            for (auto& s : sung)
            {
                if (s.score.pitch < 0)
                    continue;
                if (&s == reference && sung.size() >= 3)
                    s.score.good.push_back ("On pitch: closest to the melody your takes agree on");
                else if (s.score.pitch >= 88)
                    s.score.good.push_back (reference != nullptr ? "On pitch with your other takes" : "Notes sit right on pitch");
                if (reference == nullptr || &s == reference || reference->melody.size() < 8)
                    continue;
                std::vector<int> match;
                warpDifference (s.melody, reference->melody, &match);
                std::vector<std::vector<double>> perWord (s.place->words.size());
                for (size_t i = 0; i < s.melody.size(); ++i)
                {
                    const int w = wordAt (s, s.melodyFrame[i]);
                    if (w >= 0)
                        perWord[(size_t) w].push_back (fold (s.melody[i] - reference->melody[(size_t) match[i]]));
                }
                int worst = -1;
                double worstOff = 0.0;
                for (size_t w = 0; w < perWord.size(); ++w)
                {
                    if (perWord[w].size() < 3)
                        continue;
                    const double m = median (perWord[w]);
                    if (std::abs (m) > std::abs (worstOff))
                    {
                        worstOff = m;
                        worst = (int) w;
                    }
                }
                // more than a couple of semitones off is the note tracker losing its way, not a word sung out of tune
                if (worst >= 0 && std::abs (worstOff) >= 35.0 && std::abs (worstOff) <= 250.0)
                    s.score.bad.push_back (std::string (worstOff > 0 ? "Sharp" : "Flat") + " on " + quote (shown, (size_t) worst) + " (about "
                                           + std::to_string ((int) std::lround (std::abs (worstOff) / 5.0) * 5) + " cents)");
            }

            // ------------------------------------------------------------------ Timing
            // Each take's sound is lined up with each other take's, in song time around the line: how far apart they
            // sit is how early or late it is. And on the beat: where its syllables start, against the sixteenths.
            {
                std::vector<double> starts, ends;
                for (auto& s : sung)
                {
                    starts.push_back (s.startSong);
                    ends.push_back (s.endSong);
                }
                const double from = median (starts) - 0.4, to = median (ends) + 0.4;
                const auto frames = (size_t) std::max (0.0, std::ceil ((to - from) / TakeFeatures::hop));
                std::vector<std::vector<Shape>> shapes;
                for (auto& s : sung)
                    shapes.push_back (shapeOf (*s.take, info[s.take->key].loud, from, frames));
                const size_t n = sung.size();
                std::vector<double> sOff (n * n, nothing), aOff (n * n, nothing);
                for (size_t x = 0; x < n; ++x)
                    for (size_t y = x + 1; y < n; ++y)
                    {
                        double sg = nothing, ab = nothing;
                        timingBetween (shapes[x], shapes[y], sg, ab);
                        sOff[x * n + y] = sg;
                        sOff[y * n + x] = std::isnan (sg) ? nothing : -sg;
                        aOff[x * n + y] = aOff[y * n + x] = ab;
                    }
                for (size_t x = 0; x < n; ++x)
                {
                    auto& s = sung[x];
                    // Where this take sits against the others: the middle of its (signed) offsets, so takes that are
                    // early and takes that are late don't pull it either way; plus how uneven it is within the line.
                    std::vector<double> sg, uneven;
                    for (size_t y = 0; y < n; ++y)
                        if (y != x && ! std::isnan (sOff[x * n + y]))
                        {
                            sg.push_back (sOff[x * n + y]);
                            uneven.push_back (aOff[x * n + y] - std::abs (sOff[x * n + y]));
                        }
                    const double signedOff = median (sg) * 1000.0;
                    const double agree = std::isnan (signedOff) ? nothing : std::abs (signedOff) + 0.5 * median (uneven) * 1000.0;
                    // syllable starts in the line, against the nearest sixteenth note
                    std::vector<double> grid;
                    const double perQuarter = 60.0 / std::max (20.0, s.take->bpm);
                    for (size_t o : info[s.take->key].onsets)
                    {
                        if (o < s.f0 || o >= s.f1)
                            continue;
                        const double q = s.take->startPpq + (double) o * TakeFeatures::hop / perQuarter;
                        grid.push_back (std::abs (q * 4.0 - std::round (q * 4.0)) / 4.0 * perQuarter);
                    }
                    const double onBeat = grid.size() >= 2 ? median (grid) * 1000.0 : nothing;
                    double off = nothing;
                    if (! std::isnan (agree))
                        off = std::isnan (onBeat) ? agree : 0.75 * agree + 0.25 * onBeat;
                    else if (! std::isnan (onBeat))
                        off = onBeat;
                    if (std::isnan (off))
                        continue;
                    s.score.timing = (float) (100.0 - std::clamp (off - 20.0, 0.0, 200.0) / 3.0);
                    if (! std::isnan (signedOff) && signedOff >= 60.0)
                        s.score.bad.push_back ("A little behind your other takes (about " + std::to_string ((int) std::lround (signedOff / 10.0) * 10) + " ms)");
                    else if (! std::isnan (signedOff) && signedOff <= -60.0)
                        s.score.bad.push_back ("A little ahead of your other takes (rushed, about " + std::to_string ((int) std::lround (-signedOff / 10.0) * 10) + " ms)");
                    else if (off < 40.0)
                        s.score.good.push_back (std::isnan (agree) ? "Words land right on the beat" : "Words land right with your other takes");
                }
            }

            // ------------------------------------------------------------------ Clarity
            for (auto& s : sung)
            {
                double prob = 0.0;
                int n = 0;
                if (s.take->words != nullptr)
                    for (auto& w : *s.take->words)
                    {
                        const double mid = 0.5 * (w.start + w.end);
                        if (mid >= s.place->start - 0.1 && mid <= s.place->end + 0.1)
                        {
                            prob += w.probability;
                            ++n;
                        }
                    }
                prob = n > 0 ? prob / n : 0.0;
                s.score.clarity = (float) std::clamp (100.0 * (0.7 * s.place->sure + 0.3 * prob), 0.0, 100.0);
                std::vector<size_t> missed;
                for (size_t w = 0; w < s.place->words.size(); ++w)
                    if (! s.place->words[w].heard && ! displayWord (w < shown.size() ? shown[w] : "").empty())
                        missed.push_back (w);
                if (missed.size() == 1)
                    s.score.bad.push_back (quote (shown, missed[0]) + " wasn't heard clearly");
                else if (missed.size() == 2)
                    s.score.bad.push_back (quote (shown, missed[0]) + " and " + quote (shown, missed[1]) + " weren't heard clearly");
                else if (missed.size() > 2)
                    s.score.bad.push_back (std::to_string (missed.size()) + " words weren't heard clearly");
                else if (prob >= 0.75)
                    s.score.good.push_back ("Every word comes through clearly");
            }

            // ------------------------------------------------------------------ Energy
            for (auto& s : sung)
            {
                std::vector<double> lv, br;
                for (auto& o : sung)
                    if (&o != &s)
                    {
                        lv.push_back (o.level);
                        br.push_back (o.bright);
                    }
                const double ml = median (lv), mb = median (br);
                if (std::isnan (ml) || std::isnan (s.level))
                    continue; // nothing to compare with
                double z = (s.level - ml) / 4.0;
                if (! std::isnan (mb) && ! std::isnan (s.bright))
                    z += (s.bright - mb) / 3.0;
                s.score.energy = (float) (85.0 + 15.0 * std::tanh (0.8 * z));
                if (z >= 0.5)
                    s.score.good.push_back ("Strong, confident delivery");
                else if (z <= -0.6)
                    s.score.bad.push_back ("Softer than your other takes of this line");
            }

            // ------------------------------------------------------------------ Problems in the recording
            for (auto& s : sung)
            {
                const auto& f = *s.take->features;
                const auto& ti = info[s.take->key];
                const size_t a = s.f0 >= 5 ? s.f0 - 5 : 0, b = std::min (f.frames(), s.f1 + 5);

                for (size_t i = a; i < b; ++i)
                    if (i < f.clipped.size() && f.clipped[i] >= 2.0f)
                    {
                        const int w = wordAt (s, i);
                        s.score.problems.push_back (w >= 0 ? "Clipped on " + quote (shown, (size_t) w) + " (too loud for the input)" : std::string ("Clipped (too loud for the input)"));
                        break;
                    }

                std::vector<double> lows, levels;
                for (size_t i = s.f0; i < s.f1; ++i)
                    if (f.level[i] >= ti.loud - 30.0)
                    {
                        lows.push_back (f.lowBand[i]);
                        levels.push_back (f.level[i]);
                    }
                const double lowUsual = median (lows), levelUsual = median (levels);
                // a pop: a short burst of rumble (20 to 60 Hz) that's most of the sound, not a sung note
                if (! std::isnan (lowUsual) && ! std::isnan (levelUsual))
                    for (size_t i = s.f0; i < s.f1; ++i)
                    {
                        // (and loud: in the quiet between words, leftover rumble is "most of the sound" too)
                        if (! (f.level[i] >= levelUsual - 10.0 && f.lowBand[i] >= lowUsual + 18.0 && f.lowBand[i] >= f.level[i] - 3.0f && f.periodic[i] < 0.5f))
                            continue;
                        size_t len = 0;
                        while (i + len < s.f1 && f.lowBand[i + len] >= lowUsual + 12.0)
                            ++len;
                        if (len <= 8)
                        {
                            const int w = wordAt (s, i);
                            s.score.problems.push_back (w >= 0 ? "A pop on " + quote (shown, (size_t) w) + " (a burst of air hit the mic)" : std::string ("A pop (a burst of air hit the mic)"));
                            break;
                        }
                    }

                // a loud breath in the moment before the line (not inside the line before it)
                if (! std::isnan (levelUsual))
                {
                    double from = s.place->start - 0.7;
                    for (auto& p : *s.take->places)
                        if (p.line == s.place->line - 1 && p.end > from)
                            from = p.end;
                    const size_t ba = f.frameAt (std::max (0.0, from)), bb = f.frameAt (std::max (0.0, s.place->start - 0.05));
                    int run = 0, longest = 0;
                    for (size_t i = ba; i < bb; ++i)
                    {
                        const bool breath = f.pitch[i] == 0.0f && f.level[i] >= levelUsual - 14.0;
                        run = breath ? run + 1 : 0;
                        longest = std::max (longest, run);
                    }
                    if (longest >= 12)
                        s.score.problems.push_back ("Loud breath before the line");
                }

                // background noise, compared with the other takes
                std::vector<double> otherNoise;
                for (auto& o : sung)
                    if (&o != &s)
                        otherNoise.push_back (info[o.take->key].noise);
                const double usualNoise = median (otherNoise);
                if (! std::isnan (ti.noise) && ! std::isnan (usualNoise) && ti.noise > -58.0 && ti.noise > usualNoise + 8.0)
                    s.score.problems.push_back ("More background noise than your other takes");
            }

            for (auto& s : sung)
                out[li].takes.push_back (std::move (s.score));
        }
        return out;
    }
}
