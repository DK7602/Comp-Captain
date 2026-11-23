#include "cc/Align.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace cc
{
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

    std::vector<LinePlace> placeLines (const Lyrics& lyrics, const std::vector<HeardWord>& heard)
    {
        // the lyric words, each knowing its line
        struct LyricWord { std::string w; int line; };
        std::vector<LyricWord> L;
        for (size_t li = 0; li < lyrics.lines.size(); ++li)
            for (auto& w : plainWords (lyrics.lines[li].text))
                L.push_back ({ w, (int) li });
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
            int words = 0, found = 0;
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
                if (! any)
                {
                    p.start = hw.start;
                    p.end = hw.end;
                    any = true;
                }
                p.start = std::min (p.start, hw.start);
                p.end = std::max (p.end, hw.end);
                sure += likeOf[k];
                ++found;
            }
            if (! any)
            {
                p.start = p.end = -1.0; // filled in from its neighbours below
            }
            p.sure = words > 0 ? (float) (sure / words) : 0.0f;
            out.push_back (p);
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
        return out;
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
