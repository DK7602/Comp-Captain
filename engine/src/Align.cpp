#include "cc/Align.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace cc
{
    namespace
    {
        void fillWordTimes (LinePlace&);
    }

    double wordLikeness (const std::string& a, const std::string& b)
    {
        if (a == b)
            return 1.0;
        if (a.empty() || b.empty())
            return 0.0;
        // edit distance, as a share of the longer word
        std::vector<int> prev (b.size() + 1), cur (b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j)
            prev[j] = (int) j;
        for (size_t i = 1; i <= a.size(); ++i)
        {
            cur[0] = (int) i;
            for (size_t j = 1; j <= b.size(); ++j)
                cur[j] = std::min ({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
            std::swap (prev, cur);
        }
        const double longest = (double) std::max (a.size(), b.size());
        return std::max (0.0, 1.0 - (double) prev[b.size()] / longest);
    }

    namespace
    {
    void fillWordTimes (LinePlace& p)
    {
        // words that weren't heard share the time between the heard words around them
        auto& w = p.words;
        const size_t n = w.size();
        size_t k = 0;
        while (k < n)
        {
            if (w[k].heard)
            {
                ++k;
                continue;
            }
            size_t e = k;
            while (e < n && ! w[e].heard)
                ++e;
            const double from = k > 0 ? w[k - 1].end : p.start;
            const double to = e < n ? w[e].start : p.end;
            const double step = std::max (0.0, to - from) / (double) (e - k);
            for (size_t i = k; i < e; ++i)
            {
                w[i].start = from + step * (double) (i - k);
                w[i].end = w[i].start + step;
                w[i].heard = false;
            }
            k = e;
        }
    }
    }

    std::vector<LinePlace> placeLines (const Lyrics& lyrics, const std::vector<HeardWord>& heard)
    {
        return placeLines (lyrics, heard, 0, (int) lyrics.lines.size() - 1);
    }

    std::vector<LinePlace> placeLines (const Lyrics& lyrics, const std::vector<HeardWord>& heard, int fromLine, int toLine)
    {
        // the lyric words, each knowing its line and which shown word it's part of ("Pre-chorus" is one
        // shown word but two plain ones)
        struct LyricWord { std::string w; int line; int shown; };
        std::vector<LyricWord> L;
        fromLine = std::max (0, fromLine);
        toLine = std::min ((int) lyrics.lines.size() - 1, toLine);
        for (int li = fromLine; li <= toLine; ++li)
        {
            const auto shown = shownWords (lyrics.lines[(size_t) li].text);
            for (size_t k = 0; k < shown.size(); ++k)
                for (auto& w : plainWords (shown[k]))
                    L.push_back ({ w, li, (int) k });
        }
        std::vector<std::string> H;
        std::vector<int> heardIndex;
        for (size_t i = 0; i < heard.size(); ++i)
        {
            auto p = plainWord (heard[i].text);
            if (! p.empty())
            {
                H.push_back (std::move (p));
                heardIndex.push_back ((int) i);
            }
        }
        if (L.empty() || H.empty())
            return {};

        // Local alignment (the best-matching stretch of lyrics and take): a match scores up to +2,
        // a mismatch, a missed lyric word or an extra heard word costs a little.
        const size_t n = L.size(), m = H.size();
        constexpr double skipLyric = -0.8, skipHeard = -0.6;
        std::vector<double> score ((n + 1) * (m + 1), 0.0);
        std::vector<unsigned char> from ((n + 1) * (m + 1), 0); // 0 start, 1 match, 2 skip lyric, 3 skip heard
        auto at = [m] (size_t i, size_t j) { return i * (m + 1) + j; };
        double best = 0.0;
        size_t bi = 0, bj = 0;
        for (size_t i = 1; i <= n; ++i)
        {
            for (size_t j = 1; j <= m; ++j)
            {
                const double like = wordLikeness (L[i - 1].w, H[j - 1]);
                const double matchScore = like >= 0.5 ? 2.0 * like : like - 1.0;
                double s = 0.0;
                unsigned char f = 0;
                if (const double v = score[at (i - 1, j - 1)] + matchScore; v > s) { s = v; f = 1; }
                if (const double v = score[at (i - 1, j)] + skipLyric; v > s) { s = v; f = 2; }
                if (const double v = score[at (i, j - 1)] + skipHeard; v > s) { s = v; f = 3; }
                score[at (i, j)] = s;
                from[at (i, j)] = f;
                if (s > best)
                {
                    best = s;
                    bi = i;
                    bj = j;
                }
            }
        }
        if (best < 3.0)
            return {}; // fewer than a couple of words in common: this take doesn't sing these lyrics

        // walk back: which heard word each lyric word matched
        std::vector<int> matchOf (n, -1);
        std::vector<double> likeOf (n, 0.0);
        size_t i = bi, j = bj, firstLyric = bi, lastLyric = bi;
        while (i > 0 && j > 0 && from[at (i, j)] != 0)
        {
            const auto f = from[at (i, j)];
            if (f == 1)
            {
                const double like = wordLikeness (L[i - 1].w, H[j - 1]);
                if (like >= 0.5)
                {
                    matchOf[i - 1] = heardIndex[j - 1];
                    likeOf[i - 1] = like;
                }
                firstLyric = i;
                --i;
                --j;
            }
            else if (f == 2)
            {
                firstLyric = i;
                --i;
            }
            else
            {
                --j;
            }
        }
        lastLyric = bi; // 1-based, inclusive

        // each line: from its first heard word to its last
        std::vector<LinePlace> out;
        const int firstLine = L[firstLyric - 1].line, lastLine = L[lastLyric - 1].line;
        for (int li = firstLine; li <= lastLine; ++li)
        {
            LinePlace p;
            p.line = li;
            p.words.resize (shownWords (lyrics.lines[(size_t) li].text).size());
            int words = 0;
            double sure = 0.0;
            bool any = false;
            for (size_t k = 0; k < n; ++k)
            {
                if (L[k].line != li)
                    continue;
                ++words;
                if (matchOf[k] < 0)
                    continue;
                const auto& hw = heard[(size_t) matchOf[k]];
                auto& wt = p.words[(size_t) L[k].shown];
                if (! wt.heard)
                    wt = { hw.start, hw.end, true };
                wt.start = std::min (wt.start, hw.start);
                wt.end = std::max (wt.end, hw.end);
                if (! any)
                {
                    p.start = hw.start;
                    p.end = hw.end;
                    any = true;
                }
                p.start = std::min (p.start, hw.start);
                p.end = std::max (p.end, hw.end);
                sure += likeOf[k];
            }
            if (! any)
            {
                p.start = p.end = -1.0; // filled in from its neighbours below
            }
            p.sure = words > 0 ? (float) (sure / words) : 0.0f;
            out.push_back (std::move (p));
        }

        // lines with no heard words sit between their neighbours
        for (size_t k = 0; k < out.size(); ++k)
        {
            if (out[k].start >= 0)
                continue;
            double before = 0.0, after = -1.0;
            for (size_t b = k; b-- > 0;)
                if (out[b].start >= 0) { before = out[b].end; break; }
            for (size_t a = k + 1; a < out.size(); ++a)
                if (out[a].start >= 0) { after = out[a].start; break; }
            if (after < 0)
                after = before + 1.5;
            out[k].start = before;
            out[k].end = std::max (before, after);
        }
        // lines don't overlap: a line that starts before the last one ended is nudged
        for (size_t k = 1; k < out.size(); ++k)
            if (out[k].start < out[k - 1].end)
                out[k - 1].end = std::max (out[k - 1].start, out[k].start);
        for (auto& p : out)
            fillWordTimes (p);
        return out;
    }

    void settleOnPhrases (std::vector<LinePlace>& lines, const std::vector<Phrase>& phrases)
    {
        if (lines.empty() || phrases.empty())
            return;
        constexpr double reach = 1.5, endReach = 1.0;

        // each phrase start goes to the heard line whose start is nearest to it
        std::vector<int> owner (phrases.size(), -1);
        for (size_t k = 0; k < phrases.size(); ++k)
        {
            double best = reach;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (lines[i].sure <= 0.0f)
                    continue;
                const double d = std::abs (lines[i].start - phrases[k].start);
                if (d < best)
                {
                    best = d;
                    owner[k] = (int) i;
                }
            }
        }
        for (size_t i = 0; i < lines.size(); ++i)
        {
            auto& l = lines[i];
            int pick = -1;
            for (size_t k = 0; k < phrases.size(); ++k)
                if (owner[k] == (int) i && (pick < 0 || std::abs (phrases[k].start - l.start) < std::abs (phrases[(size_t) pick].start - l.start)))
                    pick = (int) k;
            if (pick < 0)
                continue;
            const double target = phrases[(size_t) pick].start;
            // stay between the lines around it
            const double lo = i > 0 ? lines[i - 1].start + 0.05 : -1e9;
            const double hi = i + 1 < lines.size() ? lines[i + 1].start - 0.05 : 1e9;
            if (target <= lo || target >= hi)
                continue;
            const double shift = target - l.start;
            l.start += shift;
            l.end += shift;
            for (auto& w : l.words)
            {
                w.start += shift;
                w.end += shift;
            }
            // its end on the end of the phrase it finishes in, if that's close
            double bestEnd = endReach;
            for (auto& p : phrases)
            {
                const double d = std::abs (p.end - l.end);
                if (p.end > l.start && d < bestEnd)
                {
                    bestEnd = d;
                    l.end = p.end;
                }
            }
            if (! l.words.empty())
            {
                l.words.back().end = std::max (l.words.back().start, std::min (l.words.back().end, l.end));
                if (l.words.back().end < l.end && l.words.back().heard)
                    l.words.back().end = l.end;
            }
        }
        // no overlaps
        for (size_t i = 0; i + 1 < lines.size(); ++i)
            if (lines[i].end > lines[i + 1].start)
                lines[i].end = std::max (lines[i].start, lines[i + 1].start);
    }

    std::string lyricsFromWords (const std::vector<HeardWord>& words)
    {
        std::string out, line;
        int count = 0;
        double lastEnd = -1.0;
        auto flush = [&] {
            if (! line.empty())
            {
                line[0] = (char) std::toupper ((unsigned char) line[0]);
                out += line + "\n";
            }
            line.clear();
            count = 0;
        };
        for (auto& w : words)
        {
            std::string t = w.text;
            while (! t.empty() && t.front() == ' ')
                t.erase (t.begin());
            if (plainWord (t).empty())
                continue;
            const bool pause = lastEnd >= 0 && w.start - lastEnd > 0.6;
            if (pause || count >= 12)
                flush();
            if (! line.empty())
                line += " ";
            // the recognizer's commas and full stops aren't wanted in lyrics
            while (! t.empty() && (t.back() == '.' || t.back() == ','))
                t.pop_back();
            line += t;
            ++count;
            lastEnd = w.end;
        }
        flush();
        return out;
    }
}
