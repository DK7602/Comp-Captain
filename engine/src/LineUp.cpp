#include "cc/LineUp.h"

#include <algorithm>
#include <functional>

namespace cc
{
    bool isSung (Role r)
    {
        return r != Role::guitar;
    }

    namespace
    {
        // ad-libs sing their own words around the lyrics: they're lined up, but never set the song's timing
        bool setsTiming (Role r)
        {
            return isSung (r) && r != Role::adlib;
        }

        void spreadWords (SongLine& l, size_t count)
        {
            l.words.assign (count, {});
            const double step = count > 0 ? std::max (0.0, l.end - l.start) / (double) count : 0.0;
            for (size_t i = 0; i < count; ++i)
                l.words[i] = { l.start + step * (double) i, l.start + step * (double) (i + 1), false };
        }

        std::vector<SongLine> songLines (const Lyrics& lyrics, const std::vector<TakeHeard>& takes,
                                         const std::map<std::uint64_t, std::vector<LinePlace>>& placed,
                                         const std::function<bool (const TakeHeard&)>& use)
        {
            const size_t n = lyrics.lines.size();
            std::vector<SongLine> lines (n);
            std::vector<double> bestScore (n, -1.0);
            for (size_t i = 0; i < n; ++i)
                lines[i].line = (int) i;

            for (auto& t : takes)
            {
                if (! use (t) || ! setsTiming (t.role))
                    continue;
                const auto it = placed.find (t.key);
                if (it == placed.end())
                    continue;
                for (auto& p : it->second)
                {
                    if (p.sure <= 0.0f || p.line < 0 || (size_t) p.line >= n)
                        continue;
                    auto& l = lines[(size_t) p.line];
                    ++l.takes;
                    const double score = p.sure + (t.role == Role::lead ? 0.25 : 0.0) + (t.favourite ? 0.02 : 0.0);
                    if (score <= bestScore[(size_t) p.line])
                        continue;
                    bestScore[(size_t) p.line] = score;
                    const double perSecond = t.bpm / 60.0;
                    l.start = t.startSeconds + p.start;
                    l.end = t.startSeconds + p.end;
                    l.startPpq = t.startPpq + p.start * perSecond;
                    l.endPpq = t.startPpq + p.end * perSecond;
                    l.sure = p.sure;
                    l.from = t.key;
                    l.words.clear();
                    for (auto w : p.words)
                    {
                        w.start += t.startSeconds;
                        w.end += t.startSeconds;
                        l.words.push_back (w);
                    }
                }
            }

            // Lines no take sang: an .lrc file's time if there is one, otherwise shared out between the
            // lines around them. Their quarter notes are worked out from the neighbours' (-1 when unknown).
            for (size_t i = 0; i < n; ++i)
            {
                auto& l = lines[i];
                if (l.from != 0)
                    continue;
                l.startPpq = l.endPpq = -1.0;
                if (lyrics.lines[i].lrcTime >= 0)
                {
                    l.start = lyrics.lines[i].lrcTime;
                    double next = l.start + 3.0;
                    for (size_t k = i + 1; k < n; ++k)
                        if (lyrics.lines[k].lrcTime > l.start) { next = lyrics.lines[k].lrcTime; break; }
                    l.end = next;
                }
            }
            for (size_t i = 0; i < n;)
            {
                if (lines[i].start >= 0)
                {
                    ++i;
                    continue;
                }
                size_t e = i;
                while (e < n && lines[e].start < 0)
                    ++e;
                if (i > 0 && e < n)
                {
                    const auto& a = lines[i - 1];
                    const auto& b = lines[e];
                    const double step = std::max (0.0, b.start - a.end) / (double) (e - i);
                    const bool ppq = a.startPpq >= 0 && b.startPpq >= 0;
                    const double stepPpq = ppq ? std::max (0.0, b.startPpq - a.endPpq) / (double) (e - i) : 0.0;
                    for (size_t k = i; k < e; ++k)
                    {
                        lines[k].start = a.end + step * (double) (k - i);
                        lines[k].end = lines[k].start + step;
                        if (ppq)
                        {
                            lines[k].startPpq = a.endPpq + stepPpq * (double) (k - i);
                            lines[k].endPpq = lines[k].startPpq + stepPpq;
                        }
                    }
                }
                i = e;
            }
            for (size_t i = 0; i < n; ++i)
                if (lines[i].from == 0)
                    spreadWords (lines[i], shownWords (lyrics.lines[i].text).size());
            return lines;
        }
    }

    SongLineUp lineUpSong (const Lyrics& lyrics, const std::vector<TakeHeard>& takes)
    {
        SongLineUp out;
        if (lyrics.empty())
            return out;

        // 1. every take on its own
        for (auto& t : takes)
            if (t.words != nullptr && ! t.words->empty() && isSung (t.role))
                if (auto places = placeLines (lyrics, *t.words); ! places.empty())
                    out.takes[t.key] = std::move (places);

        // 2. a take that sits in a part of the song that longer takes have already placed is lined up
        //    against the lines there (and the line either side), if it landed somewhere else. (A punch-in
        //    on the last chorus matches the first chorus just as well on its own.)
        auto linesIn = [&] (const TakeHeard& t) {
            const auto it = out.takes.find (t.key);
            return it == out.takes.end() ? 0 : (int) it->second.size();
        };
        for (auto& t : takes)
        {
            const auto it = out.takes.find (t.key);
            if (it == out.takes.end() || t.lengthSeconds <= 0.0)
                continue;
            const int mineCount = (int) it->second.size();
            const auto others = songLines (lyrics, takes, out.takes, [&] (const TakeHeard& u) {
                return u.key != t.key && linesIn (u) > mineCount;
            });
            const double from = t.startSeconds, to = t.startSeconds + t.lengthSeconds;
            int a = -1, b = -1;
            for (auto& l : others)
            {
                if (l.from == 0 || l.end <= from || l.start >= to)
                    continue;
                if (a < 0)
                    a = l.line;
                b = l.line;
            }
            if (a < 0)
                continue;
            const int lo = std::max (0, a - 1), hi = std::min ((int) lyrics.lines.size() - 1, b + 1);
            const auto& mine = it->second;
            if (mine.front().line >= lo && mine.back().line <= hi)
                continue; // already there
            if (auto again = placeLines (lyrics, *t.words, lo, hi); ! again.empty())
                it->second = std::move (again);
        }

        out.lines = songLines (lyrics, takes, out.takes, [] (const TakeHeard&) { return true; });
        for (auto& t : takes)
            if (setsTiming (t.role) && out.takes.count (t.key) != 0)
                ++out.takesLinedUp;
        return out;
    }
}
