#include "cc/UiState.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cc;

TEST_CASE ("UiState defaults match the art and open on the Comp page")
{
    UiState s;
    CHECK (s.theme == Theme::dark);
    CHECK (s.page == Page::comp);
    CHECK (s.compView == CompView::line);
    CHECK (s.windowWidth() == 1338);
    CHECK (s.windowHeight() == 753);
}

TEST_CASE ("UiState survives a save and load")
{
    UiState s;
    s.theme = Theme::light;
    s.page = Page::exportPage;
    s.compView = CompView::song;
    s.windowScale = 1.25;
    const auto loaded = UiState::fromText (s.toText());
    CHECK (loaded == s);
}

TEST_CASE ("UiState reads every page name the web interface uses")
{
    for (auto name : { "record", "lyrics", "comp", "tighten", "review", "export" })
    {
        Page p = Page::comp;
        REQUIRE (fromName (name, p));
        CHECK (toName (p) == name);
    }
    Page p = Page::comp;
    CHECK_FALSE (fromName ("mix-assist", p)); // dropped from the art on purpose
    CHECK (p == Page::comp);
}

TEST_CASE ("UiState ignores rubbish and unknown keys, keeping defaults")
{
    const auto s = UiState::fromText ("garbage\nfuture=thing\ntheme=purple\npage=review\nwindowScale=abc\n");
    CHECK (s.theme == Theme::dark);
    CHECK (s.page == Page::review);
    CHECK (s.windowScale == 0.8);

    CHECK (UiState::fromText ("") == UiState {});
    CHECK (UiState::fromText ("====\n\n\n=") == UiState {});
}

TEST_CASE ("UiState keeps the window between half and one-and-a-half times the art")
{
    CHECK (UiState::fromText ("windowScale=9\n").windowScale == UiState::maxScale);
    CHECK (UiState::fromText ("windowScale=0.1\n").windowScale == UiState::minScale);
    CHECK (UiState::fromText ("windowScale=-1\n").windowScale == 0.8); // not a plain number
    CHECK (clampScale (std::numeric_limits<double>::quiet_NaN()) == 0.8);

    UiState s;
    s.windowScale = 0.5;
    CHECK (s.windowWidth() == 836);
    CHECK (s.windowHeight() == 471);
}

TEST_CASE ("UiState text is stable (so saved projects don't change on reload)")
{
    UiState s;
    s.windowScale = 0.8;
    CHECK (s.toText() == "compcaptain-ui=1\ntheme=dark\npage=comp\ncompView=line\nwindowScale=0.80\n");
    CHECK (UiState::fromText (s.toText()).toText() == s.toText());
}
