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

    bool isSectionHeading (std::string_view raw, std::string& name)
    {
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
            name = titleCase (text::trim (inner));
            return true;
        }

        // Verse 1: / CHORUS / Pre-Chorus 2: / Chorus x2 on its own line
        if (line.back() == ':')
            line.remove_suffix (1);
        auto w = plainWords (line);
        auto isTimes = [] (const std::string& x) { return x.size() >= 2 && x[0] == 'x' && std::isdigit ((unsigned char) x[1]); };
        if (! w.empty() && isTimes (w.back()))
        {
            w.pop_back();
            const auto cut = line.find_last_of ("xX");
            line = text::trim (line.substr (0, cut));
        }
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

    Lyrics Lyrics::parse (std::string_view text)
    {
        Lyrics out;
        double offset = 0.0; // .lrc [offset:+250] is in milliseconds
        auto ensureSection = [&] {
            if (out.sections.empty())
                out.sections.push_back ({ "Lyrics", 0, 0 });
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

            std::string name;
            if (times.empty() && isSectionHeading (line, name))
            {
                out.sections.push_back ({ name, (int) out.lines.size(), 0 });
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

        // sections with no lines (a heading at the end, or two headings in a row) are dropped
        std::vector<LyricSection> kept;
        std::vector<int> newIndex (out.sections.size(), -1);
        for (size_t i = 0; i < out.sections.size(); ++i)
        {
            if (out.sections[i].lineCount > 0)
            {
                newIndex[i] = (int) kept.size();
                kept.push_back (out.sections[i]);
            }
        }
        for (auto& l : out.lines)
            l.section = newIndex[(size_t) l.section];
        out.sections = std::move (kept);

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
