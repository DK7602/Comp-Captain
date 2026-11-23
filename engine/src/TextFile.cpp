#include "cc/TextFile.h"

#include <cstdint>

namespace cc
{
    namespace
    {
        void appendUtf8 (std::string& out, std::uint32_t c)
        {
            if (c < 0x80)
                out += (char) c;
            else if (c < 0x800)
            {
                out += (char) (0xC0 | (c >> 6));
                out += (char) (0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += (char) (0xE0 | (c >> 12));
                out += (char) (0x80 | ((c >> 6) & 0x3F));
                out += (char) (0x80 | (c & 0x3F));
            }
            else
            {
                out += (char) (0xF0 | (c >> 18));
                out += (char) (0x80 | ((c >> 12) & 0x3F));
                out += (char) (0x80 | ((c >> 6) & 0x3F));
                out += (char) (0x80 | (c & 0x3F));
            }
        }

        // Windows-1252's letters at 0x80..0x9F (the rest match Unicode)
        constexpr std::uint16_t cp1252[32] = {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
            0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
        };

        std::string fromUtf16 (std::string_view b, bool bigEndian)
        {
            std::string out;
            for (size_t i = 0; i + 1 < b.size(); i += 2)
            {
                const auto lo = (unsigned char) b[i], hi = (unsigned char) b[i + 1];
                std::uint32_t c = bigEndian ? (std::uint32_t) ((lo << 8) | hi) : (std::uint32_t) ((hi << 8) | lo);
                if (c >= 0xD800 && c < 0xDC00 && i + 3 < b.size())
                {
                    const auto lo2 = (unsigned char) b[i + 2], hi2 = (unsigned char) b[i + 3];
                    const std::uint32_t d = bigEndian ? (std::uint32_t) ((lo2 << 8) | hi2) : (std::uint32_t) ((hi2 << 8) | lo2);
                    if (d >= 0xDC00 && d < 0xE000)
                    {
                        c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                        i += 2;
                    }
                }
                appendUtf8 (out, c);
            }
            return out;
        }

        std::string newLines (const std::string& s)
        {
            // \r\n and lone \r -> \n
            std::string out;
            out.reserve (s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '\r')
                {
                    out += '\n';
                    if (i + 1 < s.size() && s[i + 1] == '\n')
                        ++i;
                }
                else
                    out += s[i];
            }
            return out;
        }

        // &amp; &lt; &#8217; &#x2019; ...
        void appendXmlText (std::string& out, std::string_view s)
        {
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] != '&')
                {
                    out += s[i];
                    continue;
                }
                const auto semi = s.find (';', i);
                if (semi == std::string_view::npos || semi - i > 10)
                {
                    out += s[i];
                    continue;
                }
                const auto name = s.substr (i + 1, semi - i - 1);
                if (name == "amp") out += '&';
                else if (name == "lt") out += '<';
                else if (name == "gt") out += '>';
                else if (name == "quot") out += '"';
                else if (name == "apos") out += '\'';
                else if (name.size() > 1 && name[0] == '#')
                {
                    std::uint32_t c = 0;
                    const bool hex = name[1] == 'x' || name[1] == 'X';
                    for (size_t k = hex ? 2 : 1; k < name.size(); ++k)
                    {
                        const char d = name[k];
                        if (d >= '0' && d <= '9') c = c * (hex ? 16 : 10) + (std::uint32_t) (d - '0');
                        else if (hex && d >= 'a' && d <= 'f') c = c * 16 + (std::uint32_t) (d - 'a' + 10);
                        else if (hex && d >= 'A' && d <= 'F') c = c * 16 + (std::uint32_t) (d - 'A' + 10);
                    }
                    if (c > 0 && c < 0x110000)
                        appendUtf8 (out, c);
                }
                else
                {
                    out += std::string (s.substr (i, semi - i + 1));
                }
                i = semi;
            }
        }
    }

    bool isUtf8 (std::string_view s)
    {
        for (size_t i = 0; i < s.size();)
        {
            const auto c = (unsigned char) s[i];
            size_t more = 0;
            if (c < 0x80) more = 0;
            else if ((c & 0xE0) == 0xC0 && c >= 0xC2) more = 1;
            else if ((c & 0xF0) == 0xE0) more = 2;
            else if ((c & 0xF8) == 0xF0 && c <= 0xF4) more = 3;
            else return false;
            if (more > 0 && i + more >= s.size())
                return false; // cut off at the end
            for (size_t k = 1; k <= more; ++k)
                if ((((unsigned char) s[i + k]) & 0xC0) != 0x80)
                    return false;
            i += more + 1;
        }
        return true;
    }

    std::string textFromBytes (std::string_view b)
    {
        const auto at = [&] (size_t i) { return i < b.size() ? (unsigned char) b[i] : 0u; };
        if (at (0) == 0xEF && at (1) == 0xBB && at (2) == 0xBF)
            return newLines (std::string (b.substr (3)));
        if (at (0) == 0xFF && at (1) == 0xFE)
            return newLines (fromUtf16 (b.substr (2), false));
        if (at (0) == 0xFE && at (1) == 0xFF)
            return newLines (fromUtf16 (b.substr (2), true));
        // UTF-16 without a marker: every other byte is zero in English text
        if (b.size() >= 4 && at (1) == 0 && at (3) == 0 && at (0) != 0)
            return newLines (fromUtf16 (b, false));
        if (b.size() >= 4 && at (0) == 0 && at (2) == 0 && at (1) != 0)
            return newLines (fromUtf16 (b, true));
        if (isUtf8 (b))
            return newLines (std::string (b));
        std::string out;
        for (char ch : b)
        {
            const auto c = (unsigned char) ch;
            if (c < 0x80)
                out += ch;
            else if (c < 0xA0)
                appendUtf8 (out, cp1252[c - 0x80]);
            else
                appendUtf8 (out, c);
        }
        return newLines (out);
    }

    std::string textFromWordXml (std::string_view xml)
    {
        std::string out;
        bool inText = false; // inside <w:t>
        int inRun = 0;       // inside <w:r> (tabs and breaks in paragraph settings aren't text)
        size_t i = 0;
        while (i < xml.size())
        {
            const auto lt = xml.find ('<', i);
            if (inText)
                appendXmlText (out, xml.substr (i, (lt == std::string_view::npos ? xml.size() : lt) - i));
            if (lt == std::string_view::npos)
                break;
            const auto gt = xml.find ('>', lt);
            if (gt == std::string_view::npos)
                break;
            auto tag = xml.substr (lt + 1, gt - lt - 1);
            i = gt + 1;
            const bool closing = ! tag.empty() && tag.front() == '/';
            const bool selfClosing = ! tag.empty() && tag.back() == '/';
            if (closing)
                tag.remove_prefix (1);
            if (selfClosing)
                tag.remove_suffix (1);
            const auto nameEnd = tag.find_first_of (" \t\r\n");
            const auto name = tag.substr (0, nameEnd);

            if (name == "w:t")
                inText = ! closing && ! selfClosing;
            else if (name == "w:r")
                inRun = closing ? (inRun > 0 ? inRun - 1 : 0) : (selfClosing ? inRun : inRun + 1);
            else if (name == "w:p" && closing)
                out += '\n';
            else if (name == "w:p" && selfClosing)
                out += '\n'; // an empty paragraph
            else if ((name == "w:br" || name == "w:cr") && inRun > 0)
                out += '\n';
            else if (name == "w:tab" && inRun > 0)
                out += '\t';
        }
        return out;
    }
}
