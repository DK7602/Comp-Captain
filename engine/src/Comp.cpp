#include "cc/Comp.h"

#include "cc/Resample.h"
#include "cc/WavFile.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace cc
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr float silentDb = -120.0f;

        const LinePlace* placeOf (const CompSource& s, int line)
        {
            if (s.places == nullptr)
                return nullptr;
            for (auto& p : *s.places)
                if (p.line == line)
                    return &p;
            return nullptr;
        }

        double coverStart (const CompSource& s) { return s.startSeconds; }
        double coverEnd (const CompSource& s) { return s.startSeconds + s.lengthSeconds; }

        // The take's loudness at a song time, the loudest 10 ms within +-20 ms (silence outside the take).
        float levelAt (const CompSource& s, double songSeconds)
        {
            if (s.features == nullptr || s.features->empty())
                return 0.0f; // unknown: treat as loud
            const double t = songSeconds - s.startSeconds;
            if (t < 0.0 || t > s.lengthSeconds)
                return silentDb;
            const auto& lv = s.features->level;
            const auto mid = (long long) std::floor (t / TakeFeatures::hop);
            float most = silentDb;
            for (long long f = mid - 2; f <= mid + 2; ++f)
                if (f >= 0 && f < (long long) lv.size())
                    most = std::max (most, lv[(size_t) f]);
            return most;
        }

        // How loud the take is when it's singing (its 90th percentile), for "quiet" and level matching.
        float loudOf (const CompSource& s)
        {
            if (s.features == nullptr || s.features->empty())
                return 0.0f;
            std::vector<float> lv;
            for (auto v : s.features->level)
                if (v > -100.0f)
                    lv.push_back (v);
            if (lv.empty())
                return silentDb;
            const auto k = (size_t) ((double) (lv.size() - 1) * 0.9);
            std::nth_element (lv.begin(), lv.begin() + (std::ptrdiff_t) k, lv.end());
            return lv[k];
        }

        bool quietAt (const CompSource& s, float loud, double songSeconds)
        {
            if (s.features == nullptr || s.features->empty())
                return false;
            const float v = levelAt (s, songSeconds);
            return v < -55.0f || v < loud - 35.0f;
        }

        // The middle loudness of what's sung between two take times (frames more than 20 dB under its loud level are
        // left out: gaps and breaths), and the loudest sample there.
        struct Loudness
        {
            float level = silentDb, peak = 0.0f;
            int frames = 0;
        };
        Loudness loudnessBetween (const CompSource& s, float loud, double takeFrom, double takeTo)
        {
            Loudness out;
            if (s.features == nullptr || s.features->empty())
                return out;
            std::vector<float> lv;
            const auto f0 = s.features->frameAt (std::max (0.0, takeFrom)), f1 = s.features->frameAt (std::max (0.0, takeTo));
            for (size_t f = f0; f <= f1 && f < s.features->frames(); ++f)
            {
                if (f < s.features->peak.size())
                    out.peak = std::max (out.peak, s.features->peak[f]);
                if (s.features->level[f] >= loud - 20.0f)
                    lv.push_back (s.features->level[f]);
            }
            if (lv.empty())
                return out;
            out.frames = (int) lv.size();
            std::nth_element (lv.begin(), lv.begin() + (std::ptrdiff_t) (lv.size() / 2), lv.end());
            out.level = lv[lv.size() / 2];
            return out;
        }

        // Equal-power ramps: 0..1 over a fade centred on `at`.
        double rampUp (double t, double at, double fade)
        {
            if (fade <= 0.0)
                return t >= at ? 1.0 : 0.0;
            const double x = std::clamp ((t - (at - fade * 0.5)) / fade, 0.0, 1.0);
            return std::sin (x * pi * 0.5);
        }
        double rampDown (double t, double at, double fade)
        {
            if (fade <= 0.0)
                return t < at ? 1.0 : 0.0;
            const double x = std::clamp (((at + fade * 0.5) - t) / fade, 0.0, 1.0);
            return std::sin (x * pi * 0.5);
        }
    }

    CompPlan planComp (const std::vector<CompChoice>& choices, const std::vector<CompSource>& sources, const CompOptions& options)
    {
        CompPlan plan;
        std::map<std::uint64_t, const CompSource*> byKey;
        for (auto& s : sources)
            byKey[s.key] = &s;

        // lines in a row from the same take make one piece
        struct Run
        {
            const CompSource* src = nullptr;
            int firstLine = 0, lastLine = 0;
            double firstStart = 0.0, lastEnd = 0.0; // song seconds of its first line's start and last line's end
        };
        std::vector<Run> runs;
        for (auto& c : choices)
        {
            const auto it = byKey.find (c.take);
            if (it == byKey.end())
                continue;
            const auto* place = placeOf (*it->second, c.line);
            if (place == nullptr)
                continue;
            const double s = it->second->startSeconds + place->start, e = it->second->startSeconds + place->end;
            if (! runs.empty() && runs.back().src == it->second && s >= runs.back().firstStart)
            {
                runs.back().lastLine = c.line;
                runs.back().lastEnd = std::max (runs.back().lastEnd, e);
                continue;
            }
            runs.push_back ({ it->second, c.line, c.line, s, e });
        }
        if (runs.empty())
            return plan;

        for (auto& r : runs)
        {
            CompPiece p;
            p.take = r.src->key;
            p.firstLine = r.firstLine;
            p.lastLine = r.lastLine;
            plan.pieces.push_back (p);
        }

        // the start: a little before the first line (the breath in), never before the take
        {
            auto& first = plan.pieces.front();
            const auto& r = runs.front();
            first.from = std::max (coverStart (*r.src) + options.quietFade * 0.5, r.firstStart - options.before);
            first.fadeIn = options.quietFade;
            first.joinLevelDb = levelAt (*r.src, first.from);
        }
        // the end: once the last word has died away (the first quiet spot after the line), at most `after` later
        {
            auto& last = plan.pieces.back();
            const auto& r = runs.back();
            const float loud = loudOf (*r.src);
            const double latest = std::min (coverEnd (*r.src) - 0.03, r.lastEnd + options.after);
            double end = latest;
            if (r.src->features != nullptr)
                for (double t = r.lastEnd + 0.15; t < latest; t += 0.01)
                    if (quietAt (*r.src, loud, t))
                    {
                        end = std::min (latest, t + 0.05);
                        break;
                    }
            last.to = std::max (end, r.lastEnd);
            last.fadeOut = 0.06;
        }

        // the joins
        for (size_t i = 0; i + 1 < runs.size(); ++i)
        {
            const auto& a = runs[i];
            const auto& b = runs[i + 1];
            auto& pa = plan.pieces[i];
            auto& pb = plan.pieces[i + 1];
            const double fade = options.loudFade;
            const double lo = std::max (coverStart (*a.src), coverStart (*b.src)) + fade;
            const double hi = std::min (coverEnd (*a.src), coverEnd (*b.src)) - fade;
            const double endA = a.lastEnd, startB = b.firstStart;
            if (lo >= hi)
            {
                // the takes don't overlap in time (a punch-in): each fades on its own, silence between
                pa.to = std::min (coverEnd (*a.src) - options.quietFade, std::max (endA, endA + 0.3));
                pa.fadeOut = options.quietFade;
                pb.from = std::max (coverStart (*b.src) + options.quietFade, std::min (startB, startB - options.before));
                pb.fadeIn = options.quietFade;
                pb.joinLevelDb = silentDb;
                continue;
            }
            // look between the end of a's line and the start of b's (a little either side when they overlap)
            double from = std::min (endA, startB), to = std::max (endA, startB);
            if (endA >= startB)
            {
                from -= 0.10;
                to += 0.10;
            }
            else
            {
                from -= 0.03;
                to += 0.03;
            }
            from = std::clamp (from, lo, hi);
            to = std::clamp (to, lo, hi);
            const double mid = 0.5 * (endA + startB);
            double best = std::clamp (mid, from, to);
            float bestLevel = std::max (levelAt (*a.src, best), levelAt (*b.src, best));
            double bestCost = 1e9;
            for (double t = from; t <= to + 1e-9; t += 0.005)
            {
                const float level = std::max (levelAt (*a.src, t), levelAt (*b.src, t));
                const double cost = (double) level + 2.0 * std::abs (t - mid); // the quietest; nearer the middle on a tie
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = t;
                    bestLevel = level;
                }
            }
            const bool quiet = quietAt (*a.src, loudOf (*a.src), best) && quietAt (*b.src, loudOf (*b.src), best);
            pa.to = pb.from = best;
            pa.fadeOut = pb.fadeIn = quiet ? options.quietFade : options.loudFade;
            pb.joinLevelDb = bestLevel;
        }

        // level match: how much louder or softer each take is than the others on the same lines (so a song's soft
        // and loud lines stay as they were sung), and one gain per piece that evens that out
        if (options.levelMatch && runs.size() > 1)
        {
            std::map<std::uint64_t, std::map<int, float>> levels; // take -> line -> loudness
            std::map<int, std::vector<float>> byLine;
            for (auto& src : sources)
            {
                if (src.places == nullptr)
                    continue;
                const float loud = loudOf (src);
                for (auto& place : *src.places)
                {
                    const auto l = loudnessBetween (src, loud, place.start, place.end);
                    if (l.frames < 5)
                        continue;
                    levels[src.key][place.line] = l.level;
                    byLine[place.line].push_back (l.level);
                }
            }
            const auto middle = [] (std::vector<float> v) {
                std::sort (v.begin(), v.end());
                const size_t n = v.size();
                return n % 2 == 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
            };
            std::map<std::uint64_t, float> offset; // dB above the other takes, line by line
            for (auto& [take, lines] : levels)
            {
                std::vector<float> diffs;
                for (auto& [line, level] : lines)
                    if (byLine[line].size() > 1)
                        diffs.push_back (level - middle (byLine[line]));
                if (! diffs.empty())
                    offset[take] = middle (diffs);
            }
            for (size_t i = 0; i < runs.size(); ++i)
            {
                const auto it = offset.find (runs[i].src->key);
                if (it == offset.end())
                    continue;
                double gain = std::clamp (-(double) it->second, -options.maxGainDb, options.maxGainDb);
                const auto* p0 = placeOf (*runs[i].src, runs[i].firstLine);
                const auto* p1 = placeOf (*runs[i].src, runs[i].lastLine);
                if (p0 != nullptr && p1 != nullptr)
                {
                    const auto here = loudnessBetween (*runs[i].src, loudOf (*runs[i].src), p0->start, p1->end);
                    if (here.peak > 0.0f)
                        gain = std::min (gain, std::max (0.0, -0.3 - 20.0 * std::log10 ((double) here.peak))); // never into clipping
                }
                if (std::abs (gain) < 0.5)
                    gain = 0.0;
                plan.pieces[i].gainDb = gain;
            }
        }

        plan.start = plan.pieces.front().from - plan.pieces.front().fadeIn * 0.5;
        plan.end = plan.pieces.back().to + plan.pieces.back().fadeOut * 0.5;
        return plan;
    }

    CompPlan planLine (const CompSource& source, int line, double around)
    {
        CompPlan plan;
        const auto* place = placeOf (source, line);
        if (place == nullptr)
            return plan;
        CompPiece p;
        p.take = source.key;
        p.firstLine = p.lastLine = line;
        p.fadeIn = 0.010;
        p.fadeOut = 0.030;
        p.from = std::max (coverStart (source) + p.fadeIn * 0.5, source.startSeconds + place->start - around);
        p.to = std::min (coverEnd (source) - p.fadeOut * 0.5, source.startSeconds + place->end + around);
        if (p.to <= p.from)
            return plan;
        plan.pieces.push_back (p);
        plan.start = p.from - p.fadeIn * 0.5;
        plan.end = p.to + p.fadeOut * 0.5;
        return plan;
    }

    CompPlan planTake (const CompSource& source)
    {
        CompPlan plan;
        if (source.lengthSeconds <= 0.0)
            return plan;
        CompPiece p;
        p.take = source.key;
        p.fadeIn = 0.005;
        p.fadeOut = 0.010;
        p.from = source.startSeconds + p.fadeIn * 0.5;
        p.to = source.startSeconds + source.lengthSeconds - p.fadeOut * 0.5;
        plan.pieces.push_back (p);
        plan.start = source.startSeconds;
        plan.end = source.startSeconds + source.lengthSeconds;
        return plan;
    }

    std::optional<CompAudio> renderComp (const CompPlan& plan, const std::vector<CompFile>& files, double songFrom, double songTo,
                                         double sampleRate, int channels, const std::atomic<bool>* cancel, std::string& error)
    {
        if (sampleRate <= 0.0 || channels <= 0 || songTo <= songFrom)
        {
            error = "Nothing to play.";
            return std::nullopt;
        }
        CompAudio out;
        out.sampleRate = sampleRate;
        out.start = songFrom;
        const auto frames = (size_t) std::llround ((songTo - songFrom) * sampleRate);
        out.channels.assign ((size_t) channels, std::vector<float> (frames, 0.0f));

        for (auto& piece : plan.pieces)
        {
            if (cancel != nullptr && cancel->load())
            {
                error = "Stopped.";
                return std::nullopt;
            }
            const auto file = std::find_if (files.begin(), files.end(), [&] (const CompFile& f) { return f.key == piece.take; });
            if (file == files.end())
                continue;
            WavReader reader;
            if (! reader.open (file->wav))
            {
                error = "Couldn't read " + utf8FromPath (file->wav.filename()) + " (moved or deleted?).";
                return std::nullopt;
            }
            const auto& info = reader.info();
            const double rate = info.sampleRate;
            if (rate <= 0.0 || info.channels <= 0)
                continue;

            // the stretch of the take this piece needs (with a little extra for resampling)
            const double needFrom = std::max (songFrom, piece.from - piece.fadeIn * 0.5);
            const double needTo = std::min (songTo, piece.to + piece.fadeOut * 0.5);
            if (needTo <= needFrom)
                continue;
            const double pad = 64.0 / rate;
            auto first = (std::int64_t) std::floor ((needFrom - pad - file->startSeconds) * rate);
            auto last = (std::int64_t) std::ceil ((needTo + pad - file->startSeconds) * rate);
            first = std::clamp<std::int64_t> (first, 0, info.lengthFrames);
            last = std::clamp<std::int64_t> (last, 0, info.lengthFrames);
            if (last <= first)
                continue;

            const int takeChannels = info.channels;
            std::vector<std::vector<float>> raw ((size_t) takeChannels, std::vector<float> ((size_t) (last - first)));
            reader.seek (first);
            {
                std::vector<float*> ptrs;
                for (auto& c : raw)
                    ptrs.push_back (c.data());
                const int got = reader.read (ptrs.data(), takeChannels, (int) (last - first));
                if (got <= 0)
                    continue;
                for (auto& c : raw)
                    c.resize ((size_t) got);
            }
            // to the comp's channels: mono goes to every channel, more channels than wanted are mixed down
            std::vector<std::vector<float>> audio ((size_t) channels);
            for (int c = 0; c < channels; ++c)
            {
                if (takeChannels == channels)
                    audio[(size_t) c] = raw[(size_t) c];
                else if (takeChannels == 1)
                    audio[(size_t) c] = raw[0];
                else if (channels == 1)
                    audio[0] = toMono (raw);
                else
                    audio[(size_t) c] = raw[(size_t) std::min (c, takeChannels - 1)];
            }
            if (std::abs (rate - sampleRate) > 0.5)
                for (auto& c : audio)
                    c = resample (c, rate, sampleRate);

            const double pieceStart = file->startSeconds + (double) first / rate; // song seconds of audio[.][0]
            const double gain = std::pow (10.0, piece.gainDb / 20.0);
            const auto i0 = (std::int64_t) std::ceil ((needFrom - songFrom) * sampleRate);
            const auto i1 = (std::int64_t) std::floor ((needTo - songFrom) * sampleRate);
            for (std::int64_t i = std::max<std::int64_t> (0, i0); i <= i1 && i < (std::int64_t) frames; ++i)
            {
                const double t = songFrom + (double) i / sampleRate;
                const auto k = (std::int64_t) std::llround ((t - pieceStart) * sampleRate);
                if (k < 0 || k >= (std::int64_t) audio[0].size())
                    continue;
                const double w = gain * rampUp (t, piece.from, piece.fadeIn) * rampDown (t, piece.to, piece.fadeOut);
                if (w <= 0.0)
                    continue;
                for (int c = 0; c < channels; ++c)
                    out.channels[(size_t) c][(size_t) i] += (float) (w * audio[(size_t) c][(size_t) k]);
            }
        }
        return out;
    }

    BarStart barStartBefore (const SongPosition& ref, double seconds)
    {
        const double bpm = clampBpm (ref.bpm);
        const double qpb = ref.meter.quartersPerBar();
        const double ppq = ref.ppq + (seconds - ref.seconds) * bpm / 60.0;
        const auto refBar = barBeatAt (ref);
        const double refBarPpq = ref.ppq - (refBar.beat - 1.0) * ref.meter.quartersPerBeat();
        const double k = std::floor ((ppq - refBarPpq) / qpb + 1e-6); // a hair under a bar line counts as on it
        BarStart out;
        out.ppq = refBarPpq + k * qpb;
        out.bar = refBar.bar + (int) k;
        out.seconds = ref.seconds + (out.ppq - ref.ppq) * 60.0 / bpm;
        while (out.seconds < -1e-9 && qpb > 0.0)
        {
            out.ppq += qpb;
            out.bar += 1;
            out.seconds = ref.seconds + (out.ppq - ref.ppq) * 60.0 / bpm;
        }
        if (out.seconds < 0.0)
            out.seconds = 0.0;
        return out;
    }
}
