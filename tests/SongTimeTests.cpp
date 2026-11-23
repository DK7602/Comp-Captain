// Where takes sit in the song: bars and beats from what the DAW reports, and the saved-text helpers.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cc/SongTime.h"
#include "cc/TextFormat.h"

using Catch::Approx;

TEST_CASE ("Bars and beats in 4/4")
{
    const cc::Meter m;
    CHECK (m.quartersPerBar() == 4.0);
    CHECK (cc::barBeatAtPpq (0.0, m).bar == 1);
    CHECK (cc::barBeatAtPpq (0.0, m).beat == Approx (1.0));
    CHECK (cc::barBeatAtPpq (16.0, m).bar == 5);   // bar 5 starts after 4 bars of 4 beats
    CHECK (cc::barBeatAtPpq (18.0, m).beat == Approx (3.0));
    CHECK (cc::barBeatAtPpq (17.5, m).beat == Approx (2.5));
    CHECK (cc::barBeatAtPpq (15.9999999999, m).bar == 5); // rounding noise from the DAW stays on the bar
}

TEST_CASE ("Bars and beats in other time signatures")
{
    const cc::Meter sixEight { 6, 8 }, threeFour { 3, 4 };
    CHECK (sixEight.quartersPerBar() == Approx (3.0));
    CHECK (sixEight.quartersPerBeat() == Approx (0.5));
    CHECK (cc::barBeatAtPpq (6.0, sixEight).bar == 3);
    CHECK (cc::barBeatAtPpq (7.0, sixEight).beat == Approx (3.0));
    CHECK (cc::barBeatAtPpq (9.0, threeFour).bar == 4);
    CHECK_FALSE (cc::Meter { 4, 3 }.isValid());
    CHECK (cc::Meter { 4, 3 }.quartersPerBar() == 4.0); // nonsense falls back to 4/4
}

TEST_CASE ("The DAW's own bar start is used when it gives one")
{
    cc::SongPosition p;
    p.ppq = 33.0;
    p.meter = { 4, 4 };
    p.hasBarStart = true;
    p.barStartPpq = 32.0;
    const auto bb = cc::barBeatAt (p);
    CHECK (bb.bar == 9);
    CHECK (bb.beat == Approx (2.0));
    CHECK (bb.toText() == "9.2");

    p.barStartPpq = 100.0; // doesn't fit the position: count it ourselves
    CHECK (cc::barBeatAt (p).bar == 9);

    // ARA knows the real bar number (a 3/4 bar earlier in the song moved everything after it)
    p.barStartPpq = 32.0;
    p.barNumber = 10;
    CHECK (cc::barBeatAt (p).bar == 10);
    CHECK (cc::barBeatAt (p).beat == Approx (2.0));
}

TEST_CASE ("Placing a dropped file at a bar")
{
    const auto p = cc::positionAtBar (9, 1.0, 92.0, {});
    CHECK (p.ppq == Approx (32.0));
    CHECK (p.seconds == Approx (32.0 * 60.0 / 92.0));
    CHECK (cc::barBeatAt (p).bar == 9);
    CHECK (cc::barBeatAt (p).toText() == "9");
    CHECK (cc::positionAtBar (0, 1.0, 0.0, {}).ppq == 0.0); // bar 0 and tempo 0 are made sensible
    CHECK (cc::positionAtBar (1, 1.0, 0.0, {}).bpm == 120.0);
}

TEST_CASE ("Time signatures read and write as text")
{
    CHECK (cc::Meter { 6, 8 }.toText() == "6/8");
    CHECK (cc::Meter::fromText ("7/8") == cc::Meter { 7, 8 });
    CHECK (cc::Meter::fromText ("nonsense") == cc::Meter {});
    CHECK (cc::Meter::fromText ("4/0") == cc::Meter {});
}

TEST_CASE ("Numbers are saved the same way in every language setting")
{
    using namespace cc::text;
    CHECK (formatFixed (2.5, 3) == "2.500");
    CHECK (formatFixed (-0.25, 2) == "-0.25");
    CHECK (formatFixed (0.0004, 3) == "0.000");
    CHECK (formatFixed (1234567.123456, 6) == "1234567.123456");

    double d = 0;
    CHECK (parseDouble ("92.5", d));
    CHECK (d == Approx (92.5));
    CHECK (parseDouble ("-1e-3", d));
    CHECK (d == Approx (-0.001));
    CHECK_FALSE (parseDouble ("92,5", d));
    CHECK_FALSE (parseDouble ("", d));
    CHECK_FALSE (parseDouble ("1.2.3", d));

    std::int64_t i = 0;
    CHECK (parseInt ("-42", i));
    CHECK (i == -42);
    CHECK_FALSE (parseInt ("4.2", i));
}

TEST_CASE ("Any text survives being saved")
{
    using namespace cc::text;
    const std::string tricky = "Verse 1\\Take \"2\"\nline two\r=end";
    CHECK (unescape (escape (tricky)) == tricky);
    CHECK (escape (tricky).find ('\n') == std::string::npos);
}
