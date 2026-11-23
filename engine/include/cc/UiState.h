// Comp Captain engine: what the window remembers between sessions (saved with the DAW project).
// Framework-free so it can be tested without a DAW.
#pragma once

#include <string>
#include <string_view>

namespace cc
{
    enum class Theme { dark, light };

    // The six pages, in workflow order (left side of the window).
    enum class Page { record, lyrics, comp, tighten, review, exportPage };

    enum class CompView { line, song };

    struct UiState
    {
        Theme theme = Theme::dark;
        Page page = Page::comp;
        CompView compView = CompView::line;

        // Window size as a fraction of the art's size (1672 x 941). 0.8 = 1338 x 753.
        double windowScale = 0.8;

        static constexpr int artWidth = 1672;
        static constexpr int artHeight = 941;
        static constexpr double minScale = 0.5;
        static constexpr double maxScale = 1.5;

        int windowWidth() const;
        int windowHeight() const;

        // Saved as short "key=value" lines, so older and newer versions can read each other:
        // unknown keys are skipped, missing keys keep their defaults.
        std::string toText() const;
        static UiState fromText (std::string_view text);

        bool operator== (const UiState&) const = default;
    };

    // Names shared with the web interface ("record", "lyrics", "comp", "tighten", "review", "export").
    std::string_view toName (Theme);
    std::string_view toName (Page);
    std::string_view toName (CompView);
    bool fromName (std::string_view, Theme&);
    bool fromName (std::string_view, Page&);
    bool fromName (std::string_view, CompView&);

    double clampScale (double scale);
}
