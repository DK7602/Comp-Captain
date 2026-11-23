#include "cc/Words.h"

#include "cc/TextFormat.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace cc
{
    namespace
    {
        // Typographic punctuation in UTF-8 that word processors put in lyrics: curly quotes and
        // apostrophes, dashes, the ellipsis (U+2010..U+203F), no-break space, inverted ! and ?.
        // Returns its length in bytes, 0 if s doesn't start with one.
        size_t fancyPunctuation (std::string_view s)
        {
            const auto b = [&] (size_t i) { return i < s.size() ? (unsigned char) s[i] : 0u; };
            if (b (0) == 0xE2 && b (1) == 0x80 && b (2) >= 0x90 && b (2) <= 0xBF)
                return 3;
            if (b (0) == 0xC2 && (b (1) == 0xA0 || b (1) == 0xA1 || b (1) == 0xBF || b (1) == 0xAB || b (1) == 0xBB))
                return 2;
            return 0;
        }

        // dashes and spaces that split words (en dash, em dash, no-break space)
        bool splitsWords (std::string_view s)
        {
            const auto b = [&] (size_t i) { return i < s.size() ? (unsigned char) s[i] : 0u; };
            return (b (0) == 0xE2 && b (1) == 0x80 && (b (2) == 0x93 || b (2) == 0x94)) || (b (0) == 0xC2 && b (1) == 0xA0);
        }
    }

    std::string plainWord (std::string_view w)
    {
        std::string out;
        for (size_t i = 0; i < w.size();)
        {
            if (const auto skip = fancyPunctuation (w.substr (i)); skip > 0)
            {
                i += skip;
                continue;
            }
            const auto u = (unsigned char) w[i];
            if (std::isalnum (u))
                out += (char) std::tolower (u);
            else if (u >= 0x80)
                out += w[i]; // letters like "\xC3\xA9" stay as they are
            ++i;
        }
        return out;
    }

    std::vector<std::string> plainWords (std::string_view line)
    {
        std::vector<std::string> out;
        std::string cur;
        auto flush = [&] {
            auto p = plainWord (cur);
            if (! p.empty())
                out.push_back (std::move (p));
            cur.clear();
        };
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            // hyphens and slashes split words ("Pre-chorus" -> pre, chorus); apostrophes don't ("can't")
            if (c == ' ' || c == '\t' || c == '-' || c == '/' || c == '\r' || c == '\n')
                flush();
            else if (splitsWords (line.substr (i)))
            {
                flush();
                i += (unsigned char) c == 0xC2 ? 1 : 2;
            }
            else
                cur += c;
        }
        flush();
        return out;
    }

    namespace
    {
        constexpr std::string_view hearingHeader = "compcaptain-words=2";

        // splits "a\tb\tc" into at most n parts (the last part keeps any further tabs)
        int splitTabs (std::string_view line, std::string_view* parts, int n)
        {
            int count = 0;
            while (count < n - 1)
            {
                const auto tab = line.find ('\t');
                if (tab == std::string_view::npos)
                    break;
                parts[count++] = line.substr (0, tab);
                line = line.substr (tab + 1);
            }
            parts[count++] = line;
            return count;
        }
    }

    std::string hearingToText (const Hearing& h)
    {
        std::string out = std::string (hearingHeader) + "\n";
        out += "model=" + text::escape (h.model) + "\n";
        out += "version=" + std::to_string (h.version) + "\n";
        for (auto& p : h.phrases)
            out += "phrase\t" + text::formatFixed (p.start, 3) + "\t" + text::formatFixed (p.end, 3) + "\n";
        for (auto& w : h.words)
        {
            out += "word\t" + text::formatFixed (w.start, 3) + "\t" + text::formatFixed (w.end, 3) + "\t"
                   + text::formatFixed (w.probability, 3) + "\t" + text::escape (w.text) + "\n";
        }
        return out;
    }

    std::optional<Hearing> hearingFromText (std::string_view s)
    {
        if (s.rfind (hearingHeader, 0) != 0)
            return std::nullopt;
        Hearing h;
        while (! s.empty())
        {
            const auto end = s.find ('\n');
            auto line = s.substr (0, end);
            s = end == std::string_view::npos ? std::string_view {} : s.substr (end + 1);
            if (line.rfind ("model=", 0) == 0)
            {
                h.model = text::unescape (line.substr (6));
                continue;
            }
            if (line.rfind ("version=", 0) == 0)
            {
                std::int64_t v = 0;
                if (text::parseInt (line.substr (8), v))
                    h.version = (int) v;
                continue;
            }
            std::string_view parts[5];
            const int n = splitTabs (line, parts, 5);
            if (n == 3 && parts[0] == "phrase")
            {
                Phrase p;
                if (text::parseDouble (parts[1], p.start) && text::parseDouble (parts[2], p.end))
                    h.phrases.push_back (p);
            }
            else if (n == 5 && parts[0] == "word")
            {
                HeardWord w;
                double prob = 0;
                if (! text::parseDouble (parts[1], w.start) || ! text::parseDouble (parts[2], w.end) || ! text::parseDouble (parts[3], prob))
                    continue;
                w.probability = (float) prob;
                w.text = text::unescape (parts[4]);
                h.words.push_back (std::move (w));
            }
        }
        return h;
    }

    std::filesystem::path hearingFileFor (const std::filesystem::path& takeFile)
    {
        auto p = takeFile;
        p.replace_extension (".words");
        return p;
    }

    bool saveHearing (const std::filesystem::path& takeFile, const Hearing& h)
    {
        // written to a side file first, so a crash never leaves half a file that looks finished
        const auto dest = hearingFileFor (takeFile);
        auto temp = dest;
        temp += ".part";
        {
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            const auto t = hearingToText (h);
            out.write (t.data(), (std::streamsize) t.size());
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

    std::optional<Hearing> loadHearing (const std::filesystem::path& takeFile)
    {
        std::ifstream in (hearingFileFor (takeFile), std::ios::binary);
        if (! in.is_open())
            return std::nullopt;
        std::stringstream ss;
        ss << in.rdbuf();
        return hearingFromText (ss.str());
    }
}
