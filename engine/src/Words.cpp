#include "cc/Words.h"

#include "cc/TextFormat.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace cc
{
    std::string plainWord (std::string_view w)
    {
        std::string out;
        for (char c : w)
        {
            const auto u = (unsigned char) c;
            if (std::isalnum (u))
                out += (char) std::tolower (u);
            else if (u >= 0x80)
                out += c; // letters like "é" stay as they are
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
        for (char c : line)
        {
            // hyphens and slashes split words ("Pre-chorus" -> pre, chorus); apostrophes don't ("can't")
            if (c == ' ' || c == '\t' || c == '-' || c == '/' || c == '\r' || c == '\n')
                flush();
            else
                cur += c;
        }
        flush();
        return out;
    }

    std::string wordsToText (const std::vector<HeardWord>& words)
    {
        std::string out = "compcaptain-words=1\n";
        for (auto& w : words)
        {
            out += text::formatFixed (w.start, 3) + "\t" + text::formatFixed (w.end, 3) + "\t"
                   + text::formatFixed (w.probability, 3) + "\t" + text::escape (w.text) + "\n";
        }
        return out;
    }

    std::vector<HeardWord> wordsFromText (std::string_view s)
    {
        std::vector<HeardWord> out;
        while (! s.empty())
        {
            const auto end = s.find ('\n');
            auto line = s.substr (0, end);
            s = end == std::string_view::npos ? std::string_view {} : s.substr (end + 1);

            std::string_view parts[4];
            int n = 0;
            while (n < 3)
            {
                const auto tab = line.find ('\t');
                if (tab == std::string_view::npos)
                    break;
                parts[n++] = line.substr (0, tab);
                line = line.substr (tab + 1);
            }
            if (n < 3)
                continue;
            parts[3] = line;
            HeardWord w;
            double p = 0;
            if (! text::parseDouble (parts[0], w.start) || ! text::parseDouble (parts[1], w.end) || ! text::parseDouble (parts[2], p))
                continue;
            w.probability = (float) p;
            w.text = text::unescape (parts[3]);
            out.push_back (std::move (w));
        }
        return out;
    }

    std::filesystem::path wordsFileFor (const std::filesystem::path& takeFile)
    {
        auto p = takeFile;
        p.replace_extension (".words");
        return p;
    }

    bool saveWords (const std::filesystem::path& takeFile, const std::vector<HeardWord>& words)
    {
        std::ofstream out (wordsFileFor (takeFile), std::ios::binary | std::ios::trunc);
        const auto text = wordsToText (words);
        out.write (text.data(), (std::streamsize) text.size());
        return out.good();
    }

    std::optional<std::vector<HeardWord>> loadWords (const std::filesystem::path& takeFile)
    {
        std::ifstream in (wordsFileFor (takeFile), std::ios::binary);
        if (! in.is_open())
            return std::nullopt;
        std::stringstream ss;
        ss << in.rdbuf();
        const auto text = ss.str();
        if (text.rfind ("compcaptain-words=1", 0) != 0)
            return std::nullopt;
        return wordsFromText (text);
    }
}
