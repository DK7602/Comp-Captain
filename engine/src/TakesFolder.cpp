#include "cc/TakesFolder.h"

#include "cc/WavFile.h"

#include <array>
#include <cctype>
#include <system_error>

namespace cc
{
    std::string safeFileName (std::string_view name, std::string_view fallback, size_t maxLength)
    {
        std::string out;
        for (char c : name)
        {
            const auto u = (unsigned char) c;
            if (u < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                out += u < 32 ? ' ' : '-';
            else
                out += c;
        }

        // no runs of spaces, nothing at the ends that Windows would strip
        std::string tidy;
        for (char c : out)
            if (! (c == ' ' && (tidy.empty() || tidy.back() == ' ')))
                tidy += c;
        while (! tidy.empty() && (tidy.back() == ' ' || tidy.back() == '.'))
            tidy.pop_back();
        while (! tidy.empty() && (tidy.front() == ' ' || tidy.front() == '.'))
            tidy.erase (tidy.begin());

        if (tidy.size() > maxLength)
        {
            size_t cut = maxLength;
            while (cut > 0 && ((unsigned char) tidy[cut] & 0xC0) == 0x80)
                --cut; // don't split a UTF-8 character
            tidy.resize (cut);
            while (! tidy.empty() && (tidy.back() == ' ' || tidy.back() == '.'))
                tidy.pop_back();
        }

        // CON, PRN, AUX, NUL, COM1-9, LPT1-9 can't be file names on Windows (even with an extension)
        std::string upper;
        for (char c : tidy.substr (0, tidy.find ('.')))
            upper += (char) std::toupper ((unsigned char) c);
        static constexpr std::array<std::string_view, 4> reserved { "CON", "PRN", "AUX", "NUL" };
        bool isReserved = false;
        for (auto r : reserved)
            isReserved = isReserved || upper == r;
        if (upper.size() == 4 && (upper.rfind ("COM", 0) == 0 || upper.rfind ("LPT", 0) == 0) && upper[3] >= '1' && upper[3] <= '9')
            isReserved = true;
        if (isReserved)
            tidy = "_" + tidy;

        return tidy.empty() ? std::string (fallback) : tidy;
    }

    std::string trackFolderName (Date d, std::string_view trackName, std::string_view shortId)
    {
        auto two = [] (int v) { return (v < 10 ? "0" : "") + std::to_string (v); };
        const std::string date = std::to_string (d.year) + "-" + two (d.month) + "-" + two (d.day);
        const std::string track = safeFileName (trackName, "Comp Captain", 48);
        return date + " " + track + (shortId.empty() ? "" : " (" + std::string (shortId) + ")");
    }

    std::filesystem::path uniqueFile (const std::filesystem::path& folder, std::string_view stem, std::string_view extension)
    {
        const std::string base = safeFileName (stem);
        std::error_code ec;
        for (int n = 1; n < 10000; ++n)
        {
            const std::string name = base + (n == 1 ? "" : " (" + std::to_string (n) + ")") + std::string (extension);
            auto candidate = folder / pathFromUtf8 (name);
            if (! std::filesystem::exists (candidate, ec))
                return candidate;
        }
        return folder / pathFromUtf8 (base + " (more)" + std::string (extension));
    }

    std::string shortHexId (unsigned long long value)
    {
        static constexpr char hex[] = "0123456789abcdef";
        // mix the bits so nearby numbers don't give nearby ids
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdull;
        value ^= value >> 33;
        std::string out;
        for (int i = 0; i < 4; ++i)
            out += hex[(value >> (i * 4)) & 15];
        return out;
    }
}
