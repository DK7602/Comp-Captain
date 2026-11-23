#include "cc/UiState.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <utility>

namespace cc
{
    namespace
    {
        constexpr std::array<std::pair<Theme, std::string_view>, 2> themeNames { { { Theme::dark, "dark" }, { Theme::light, "light" } } };
        constexpr std::array<std::pair<Page, std::string_view>, 6> pageNames { {
            { Page::record, "record" }, { Page::lyrics, "lyrics" }, { Page::comp, "comp" },
            { Page::tighten, "tighten" }, { Page::review, "review" }, { Page::exportPage, "export" } } };
        constexpr std::array<std::pair<CompView, std::string_view>, 2> viewNames { { { CompView::line, "line" }, { CompView::song, "song" } } };

        template <typename E, size_t N>
        std::string_view nameOf (const std::array<std::pair<E, std::string_view>, N>& table, E value)
        {
            for (auto& [e, name] : table)
                if (e == value)
                    return name;
            return table[0].second;
        }

        template <typename E, size_t N>
        bool valueOf (const std::array<std::pair<E, std::string_view>, N>& table, std::string_view name, E& out)
        {
            for (auto& [e, n] : table)
            {
                if (n == name)
                {
                    out = e;
                    return true;
                }
            }
            return false;
        }

        std::string_view trim (std::string_view s)
        {
            while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
                s.remove_prefix (1);
            while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
                s.remove_suffix (1);
            return s;
        }

        bool parseDouble (std::string_view s, double& out)
        {
            // from_chars for double is C++17 but not everywhere yet; parse a plain decimal by hand.
            if (s.empty() || s.size() > 32)
                return false;
            double value = 0.0, scale = 1.0;
            bool seenDot = false, seenDigit = false;
            for (char c : s)
            {
                if (c >= '0' && c <= '9')
                {
                    seenDigit = true;
                    if (seenDot)
                    {
                        scale /= 10.0;
                        value += (c - '0') * scale;
                    }
                    else
                    {
                        value = value * 10.0 + (c - '0');
                    }
                }
                else if (c == '.' && ! seenDot)
                {
                    seenDot = true;
                }
                else
                {
                    return false;
                }
            }
            if (! seenDigit)
                return false;
            out = value;
            return true;
        }
    }

    std::string_view toName (Theme t) { return nameOf (themeNames, t); }
    std::string_view toName (Page p) { return nameOf (pageNames, p); }
    std::string_view toName (CompView v) { return nameOf (viewNames, v); }
    bool fromName (std::string_view s, Theme& t) { return valueOf (themeNames, s, t); }
    bool fromName (std::string_view s, Page& p) { return valueOf (pageNames, s, p); }
    bool fromName (std::string_view s, CompView& v) { return valueOf (viewNames, s, v); }

    double clampScale (double scale)
    {
        if (! std::isfinite (scale))
            return 0.8;
        return std::clamp (scale, UiState::minScale, UiState::maxScale);
    }

    int UiState::windowWidth() const { return (int) std::lround (artWidth * clampScale (windowScale)); }
    int UiState::windowHeight() const { return (int) std::lround (artHeight * clampScale (windowScale)); }

    std::string UiState::toText() const
    {
        // Two decimals is plenty for a window size and keeps the text stable across platforms.
        const auto hundredths = (long) std::lround (clampScale (windowScale) * 100.0);
        std::string scale = std::to_string (hundredths / 100) + "." + (hundredths % 100 < 10 ? "0" : "") + std::to_string (hundredths % 100);

        std::string out = "compcaptain-ui=1\n";
        out += "theme=" + std::string (toName (theme)) + "\n";
        out += "page=" + std::string (toName (page)) + "\n";
        out += "compView=" + std::string (toName (compView)) + "\n";
        out += "windowScale=" + scale + "\n";
        return out;
    }

    UiState UiState::fromText (std::string_view text)
    {
        UiState s;
        while (! text.empty())
        {
            const auto end = text.find ('\n');
            const auto line = trim (text.substr (0, end));
            text = end == std::string_view::npos ? std::string_view {} : text.substr (end + 1);

            const auto eq = line.find ('=');
            if (eq == std::string_view::npos)
                continue;
            const auto key = trim (line.substr (0, eq));
            const auto value = trim (line.substr (eq + 1));

            if (key == "theme")
                fromName (value, s.theme);
            else if (key == "page")
                fromName (value, s.page);
            else if (key == "compView")
                fromName (value, s.compView);
            else if (key == "windowScale")
            {
                double d = 0;
                if (parseDouble (value, d))
                    s.windowScale = clampScale (d);
            }
        }
        return s;
    }
}
