#include "cc/Lyrics.h"

#include "cc/TextFormat.h"
#include "cc/Words.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace cc
{
    namespace
    {
        constexpr std::array<std::string_view, 22> sectionWords {
            "intro", "verse", "prechorus", "pre", "chorus", "postchorus", "hook", "prehook", "bridge", "outro",
            "refrain", "tag", "interlude", "breakdown", "vamp", "ending", "coda", "instrumental", "rap", "drop",
            "build", "solo",
        };

        // "pre-chorus 2" -> "Pre-Chorus 2"
        std::string titleCase (std::string_view s)
        {
            std::string out;
            bool start = true;
            for (char c : s)
            {
                const auto u = (unsigned char) c;
                out += start ? (char) std::toupper (u) : (char) std::tolower (u);
                start = ! std::isalnum (u);
            }
            return out;
        }

        // "[01:23.45]" -> 83.45 seconds. Returns false if it isn't a time tag.
        bool lrcTime (std::string_view tag, double& seconds)
        {
            const auto colon = tag.find (':');
            if (colon == std::string_view::npos || colon == 0)
                return false;
            std::int64_t minutes = 0;
            double secs = 0;
            if (! text::parseInt (tag.substr (0, colon), minutes) || ! text::parseDouble (tag.substr (colon + 1), secs))
                return false;
            if (minutes < 0 || secs < 0 || secs >= 60.0)
                return false;
            seconds = (double) minutes * 60.0 + secs;
            return true;
        }

        // Notes in lyrics that aren't sung: "(x2)", "x3", "[Repeat]", "*repeat chorus*", "(instrumental)".
        bool isNote (std::string_view line)
        {
            auto w = plainWords (line);
            if (w.empty())
                return true; // only punctuation
            if (w.size() == 1 && w[0].size() >= 2 && w[0][0] == 'x' && std::isdigit ((unsigned char) w[0][1]))
                return true;
            if (w.size() <= 2 && (w[0] == "repeat" || w[0] == "instrumental"))
                return true;
            const auto t = text::trim (line);
            return t.size() >= 2 && t.front() == '*' && t.back() == '*';
        }
    }

    namespace
    {
        // "x2", "X3", "2x", "(x2)" at the end of a heading: how many times it's sung. Removes it from s.
        int takeTimes (std::string_view& s)
        {
            auto t = text::trim (s);
            const auto space = t.find_last_of (" \t");
            auto last = space == std::string_view::npos ? t : t.substr (space + 1);
            auto bare = last;
            if (bare.size() >= 2 && bare.front() == '(' && bare.back() == ')')
                bare = bare.substr (1, bare.size() - 2);
            std::string_view digits;
            if (bare.size() >= 2 && (bare.front() == 'x' || bare.front() == 'X'))
                digits = bare.substr (1);
            else if (bare.size() >= 2 && (bare.back() == 'x' || bare.back() == 'X'))
                digits = bare.substr (0, bare.size() - 1);
            if (digits.empty() || digits.size() > 2 || space == std::string_view::npos)
                return 1;
            for (char c : digits)
                if (! std::isdigit ((unsigned char) c))
                    return 1;
            s = text::trim (t.substr (0, space));
            return std::max (1, std::min (9, std::stoi (std::string (digits))));
        }
    }

    bool isChord (std::string_view w)
    {
        if (w.size() >= 2 && w.front() == '(' && w.back() == ')')
            w = w.substr (1, w.size() - 2);
        auto root = [] (std::string_view& s) {
            if (s.empty() || s[0] < 'A' || s[0] > 'G')
                return false;
            s.remove_prefix (1);
            if (! s.empty() && (s[0] == '#' || s[0] == 'b'))
                s.remove_prefix (1);
            return true;
        };
        if (! root (w))
            return false;
        // the chord's kind: m, maj7, sus4, add9, dim, aug, +, b5, #9, 6/9 ...
        static constexpr std::string_view parts[] = { "maj", "min", "dim", "aug", "sus", "add", "m", "M", "+", "b", "#", "-", "\xC2\xB0" };
        while (! w.empty() && w[0] != '/')
        {
            if (std::isdigit ((unsigned char) w[0]))
            {
                w.remove_prefix (1);
                continue;
            }
            bool matched = false;
            for (auto p : parts)
                if (w.substr (0, p.size()) == p)
                {
                    w.remove_prefix (p.size());
                    matched = true;
                    break;
                }
            if (! matched)
                return false;
        }
        if (w.empty())
            return true;
        w.remove_prefix (1); // a bass note: D/F#
        return root (w) && w.empty();
    }

    namespace
    {
        // A row of chords over the words ("G    D/F#   Em7  |  C"), as chord sheets have.
        bool isChordRow (std::string_view line)
        {
            int chords = 0;
            for (auto& w : shownWords (line))
            {
                if (isChord (w))
                    ++chords;
                else if (! (w == "|" || w == "||" || w == "/" || w == "-" || w == "%" || w == "N.C." || w == "NC" || w == "x2" || w == "x3" || w == "x4"))
                    return false;
            }
            return chords > 0;
        }

        // "[G]I was [D]lost" -> "I was lost" (brackets holding anything else stay)
        std::string withoutInlineChords (std::string_view line)
        {
            std::string out;
            size_t i = 0;
            while (i < line.size())
            {
                if (line[i] == '[')
                {
                    const auto close = line.find (']', i);
                    if (close != std::string_view::npos && isChord (line.substr (i + 1, close - i - 1)))
                    {
                        i = close + 1;
                        continue;
                    }
                }
                out += line[i++];
            }
            // "I was  lost" after taking a chord out between two words: one space
            std::string tidy;
            for (char c : out)
                if (! (c == ' ' && ! tidy.empty() && tidy.back() == ' '))
                    tidy += c;
            return std::string (text::trim (tidy));
        }
    }

    bool isSectionHeading (std::string_view raw, std::string& name)
    {
        int times = 1;
        return isSectionHeading (raw, name, times);
    }

    bool isSectionHeading (std::string_view raw, std::string& name, int& times)
    {
        times = 1;
        auto line = text::trim (raw);
        if (line.empty())
            return false;

        // [Verse 1], (Chorus), {Bridge}: anything in brackets on its own line is a heading,
        // as long as it isn't an .lrc time tag
        const char open = line.front(), close = line.back();
        if (line.size() >= 3 && ((open == '[' && close == ']') || (open == '(' && close == ')') || (open == '{' && close == '}')))
        {
            auto inner = text::trim (line.substr (1, line.size() - 2));
            double t = 0;
            if (open == '[' && lrcTime (inner, t))
                return false;
            if (inner.empty())
                return false;
            // "(x2)" and "(repeat)" are notes, not headings; "(oh, oh)" is sung
            if (isNote (line))
                return false;
            const auto w = plainWords (inner);
            if (open != '[' && (w.empty() || std::find (sectionWords.begin(), sectionWords.end(), w[0]) == sectionWords.end()))
                return false;
            if (! inner.empty() && inner.back() == ':')
                inner.remove_suffix (1);
            times = takeTimes (inner);
            name = titleCase (text::trim (inner));
            return ! name.empty();
        }

        // Verse 1: / CHORUS / Pre-Chorus 2: / Chorus x2 on its own line
        if (line.back() == ':')
            line.remove_suffix (1);
        times = takeTimes (line);
        if (! line.empty() && line.back() == ':')
            line.remove_suffix (1);
        auto w = plainWords (line);
        if (w.empty() || w.size() > 3)
            return false;
        std::string joined;
        for (auto& x : w)
            if (! std::isdigit ((unsigned char) x[0]))
                joined += x;
        if (std::find (sectionWords.begin(), sectionWords.end(), joined) == sectionWords.end())
            return false;
        // the rest must be a number ("Verse 2") or nothing
        int words = 0;
        for (auto& x : w)
            words += std::isdigit ((unsigned char) x[0]) ? 0 : 1;
        if (words > 2)
            return false;
        name = titleCase (text::trim (line));
        return true;
    }

    std::vector<std::string> shownWords (std::string_view line)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char c : line)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (! cur.empty())
                    out.push_back (std::move (cur));
                cur.clear();
            }
            else
                cur += c;
        }
        if (! cur.empty())
            out.push_back (std::move (cur));
        return out;
    }

    Lyrics Lyrics::parse (std::string_view text)
    {
        Lyrics out;
        std::vector<int> timesOf;   // per section: "x2" headings
        double offset = 0.0; // .lrc [offset:+250] is in milliseconds
        auto ensureSection = [&] {
            if (out.sections.empty())
            {
                out.sections.push_back ({ "Lyrics", 0, 0 });
                timesOf.push_back (1);
            }
        };

        while (! text.empty())
        {
            const auto end = text.find ('\n');
            auto line = text::trim (text.substr (0, end));
            text = end == std::string_view::npos ? std::string_view {} : text.substr (end + 1);
            if (line.empty())
                continue;

            // .lrc: one or more [mm:ss.xx] tags at the start, then the words
            std::vector<double> times;
            while (line.size() > 2 && line.front() == '[')
            {
                const auto close = line.find (']');
                if (close == std::string_view::npos)
                    break;
                const auto tag = line.substr (1, close - 1);
                double t = 0;
                if (lrcTime (tag, t))
                {
                    times.push_back (t);
                    line = text::trim (line.substr (close + 1));
                    continue;
                }
                // .lrc information tags: [ar:Artist] [ti:Title] [offset:+250]
                const auto colon = tag.find (':');
                const auto key = colon == std::string_view::npos ? std::string_view {} : tag.substr (0, colon);
                if (key == "ar" || key == "ti" || key == "al" || key == "by" || key == "au" || key == "length" || key == "re" || key == "ve" || key == "#")
                {
                    line = {};
                    break;
                }
                if (key == "offset")
                {
                    double ms = 0;
                    if (text::parseDouble (tag.substr (colon + 1), ms))
                        offset = ms / 1000.0;
                    line = {};
                    break;
                }
                break;
            }
            if (line.empty())
                continue;

            // chord sheets: rows of chords and [G] chords inside a line aren't sung
            if (times.empty() && isChordRow (line))
                continue;
            std::string stripped;
            if (line.find ('[') != std::string_view::npos)
            {
                stripped = withoutInlineChords (line);
                line = stripped;
                if (line.empty())
                    continue;
            }

            std::string name;
            int repeat = 1;
            // Song Rider Pro's section breaks: "__________VERSE 1"
            if (times.empty() && line.rfind ("_____", 0) == 0)
            {
                auto title = line.substr (line.find_first_not_of ('_') == std::string_view::npos ? line.size() : line.find_first_not_of ('_'));
                title = text::trim (title);
                if (title.empty())
                    continue;
                if (! isSectionHeading (title, name, repeat))
                {
                    repeat = 1;
                    name = titleCase (title);
                }
                out.sections.push_back ({ name, (int) out.lines.size(), 0 });
                timesOf.push_back (repeat);
                continue;
            }
            if (times.empty() && isSectionHeading (line, name, repeat))
            {
                out.sections.push_back ({ name, (int) out.lines.size(), 0 });
                timesOf.push_back (repeat);
                continue;
            }
            if (isNote (line))
                continue;

            ensureSection();
            if (times.empty())
                times.push_back (-1.0);
            for (double t : times)
            {
                LyricLine l;
                l.text = std::string (line);
                l.section = (int) out.sections.size() - 1;
                l.lrcTime = t >= 0 ? std::max (0.0, t - offset) : -1.0;
                out.lines.push_back (std::move (l));
                ++out.sections.back().lineCount;
            }
        }

        // Sections in the order they're sung. A heading with no lines under it that names an earlier section
        // sings that section again; other empty headings (a heading at the end, two in a row) are dropped.
        // "x2" sings a section twice.
        auto sameName = [] (const std::string& a, const std::string& b) {
            return plainWords (a) == plainWords (b);
        };
        Lyrics sung;
        for (size_t i = 0; i < out.sections.size(); ++i)
        {
            const auto& sec = out.sections[i];
            int from = -1;
            if (sec.lineCount > 0)
                from = (int) i;
            else
                for (size_t k = i; k-- > 0;)
                    if (out.sections[k].lineCount > 0 && sameName (out.sections[k].name, sec.name))
                    {
                        from = (int) k;
                        break;
                    }
            if (from < 0)
                continue;
            const auto& src = out.sections[(size_t) from];
            for (int rep = 0; rep < timesOf[i]; ++rep)
            {
                LyricSection s2 { sec.name, (int) sung.lines.size(), src.lineCount };
                for (int k = 0; k < src.lineCount; ++k)
                {
                    auto l = out.lines[(size_t) (src.firstLine + k)];
                    l.section = (int) sung.sections.size();
                    if (from != (int) i || rep > 0)
                        l.lrcTime = -1.0; // a repeat has its own time
                    sung.lines.push_back (std::move (l));
                }
                sung.sections.push_back (std::move (s2));
            }
        }
        out.sections = std::move (sung.sections);
        out.lines = std::move (sung.lines);

        // .lrc lines can come in any order (repeated choruses list several times on one line)
        bool timed = ! out.lines.empty();
        for (auto& l : out.lines)
            timed = timed && l.lrcTime >= 0;
        if (timed && out.sections.size() == 1)
        {
            std::stable_sort (out.lines.begin(), out.lines.end(), [] (const LyricLine& a, const LyricLine& b) { return a.lrcTime < b.lrcTime; });
        }
        return out;
    }

    std::string Lyrics::toText() const
    {
        std::string out;
        for (size_t s = 0; s < sections.size(); ++s)
        {
            if (s > 0)
                out += "\n";
            out += "[" + sections[s].name + "]\n";
            for (int i = 0; i < sections[s].lineCount; ++i)
                out += lines[(size_t) (sections[s].firstLine + i)].text + "\n";
        }
        return out;
    }
}
