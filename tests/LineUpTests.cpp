// Lining lyrics up with the whole song: several takes, each covering all or part of it.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cc/LineUp.h"

using Catch::Approx;

namespace
{
    // words sung at a steady pace: one every 0.4 s from 'at' (take seconds)
    std::vector<cc::HeardWord> sing (const std::string& text, double at)
    {
        std::vector<cc::HeardWord> out;
        for (auto& w : cc::shownWords (text))
        {
            out.push_back ({ " " + w, at, at + 0.3, 0.9f });
            at += 0.4;
        }
        return out;
    }

    void add (std::vector<cc::HeardWord>& to, const std::vector<cc::HeardWord>& more)
    {
        to.insert (to.end(), more.begin(), more.end());
    }

    const char* chorus1 = "I'm alive, I'm alive";
    const char* chorus2 = "singing out with all I am";

    const char* song =
        "[Verse 1]\n"
        "I was lost in the dark\n"
        "but You found me and gave me life\n"
        "[Chorus]\n"
        "I'm alive, I'm alive\n"
        "singing out with all I am\n"
        "[Verse 2]\n"
        "Morning light is breaking through\n"
        "every chain is falling too\n"
        "[Chorus]\n";

    cc::TakeHeard take (std::uint64_t key, double startSeconds, double length, const std::vector<cc::HeardWord>& words, cc::Role role = cc::Role::lead)
    {
        cc::TakeHeard t;
        t.key = key;
        t.startSeconds = startSeconds;
        t.startPpq = startSeconds * 2.0; // 120 BPM
        t.bpm = 120.0;
        t.lengthSeconds = length;
        t.role = role;
        t.words = &words;
        return t;
    }
}

TEST_CASE ("A full take puts every line on the song")
{
    const auto lyrics = cc::Lyrics::parse (song);
    REQUIRE (lyrics.lines.size() == 8);
    std::vector<cc::HeardWord> w;
    add (w, sing ("I was lost in the dark", 1.0));
    add (w, sing ("but You found me and gave me life", 4.0));
    add (w, sing (chorus1, 8.0));
    add (w, sing (chorus2, 10.0));
    add (w, sing ("Morning light is breaking through", 14.0));
    add (w, sing ("every chain is falling too", 17.0));
    add (w, sing (chorus1, 20.0));
    add (w, sing (chorus2, 22.0));

    // the take starts at 10 s into the song (bar 6 at 120 BPM)
    const auto up = cc::lineUpSong (lyrics, { take (7, 10.0, 30.0, w) });
    CHECK (up.takesLinedUp == 1);
    REQUIRE (up.lines.size() == 8);
    CHECK (up.lines[0].start == Approx (11.0));
    CHECK (up.lines[0].startPpq == Approx (22.0));
    CHECK (up.lines[0].from == 7);
    CHECK (up.lines[0].takes == 1);
    CHECK (up.lines[2].start == Approx (18.0));
    CHECK (up.lines[6].start == Approx (30.0)); // the repeated chorus has its own place
    CHECK (up.lines[7].start == Approx (32.0));
    REQUIRE (up.lines[0].words.size() == 6);
    CHECK (up.lines[0].words[1].start == Approx (11.4));
}

TEST_CASE ("A punch-in on the last chorus lands on the last chorus")
{
    const auto lyrics = cc::Lyrics::parse (song);
    std::vector<cc::HeardWord> full;
    add (full, sing ("I was lost in the dark", 1.0));
    add (full, sing ("but You found me and gave me life", 4.0));
    add (full, sing (chorus1, 8.0));
    add (full, sing (chorus2, 10.0));
    add (full, sing ("Morning light is breaking through", 14.0));
    add (full, sing ("every chain is falling too", 17.0));
    add (full, sing (chorus1, 20.0));
    add (full, sing (chorus2, 22.0));

    // a second take, recorded from 19 s into the take above's time (29 s into the song): the last chorus,
    // sung a little clearer. On its own it could be either chorus.
    std::vector<cc::HeardWord> punch;
    add (punch, sing (chorus1, 1.0));
    add (punch, sing (chorus2, 3.0));

    const auto up = cc::lineUpSong (lyrics, { take (1, 10.0, 30.0, full), take (2, 29.0, 6.0, punch) });
    REQUIRE (up.takes.count (2) == 1);
    CHECK (up.takes.at (2).front().line == 6);
    CHECK (up.lines[6].takes == 2);
    CHECK (up.lines[2].takes == 1);
    CHECK (up.lines[6].start == Approx (30.0));
}

TEST_CASE ("Lead takes set the song's timing; ad-libs and guitars don't")
{
    const auto lyrics = cc::Lyrics::parse ("I was lost in the dark\nbut You found me and gave me life\n");
    std::vector<cc::HeardWord> lead, adlib, guitar;
    add (lead, sing ("I was lost in the dark", 1.0));
    add (lead, sing ("but You found me and gave me life", 4.0));
    add (adlib, sing ("I was lost in the dark", 1.5));
    add (guitar, sing ("I was lost in the dark", 0.5));

    const auto up = cc::lineUpSong (lyrics, { take (1, 0.0, 10.0, adlib, cc::Role::adlib), take (2, 0.0, 10.0, lead),
                                              take (3, 0.0, 10.0, guitar, cc::Role::guitar) });
    CHECK (up.lines[0].from == 2);
    CHECK (up.lines[0].takes == 1);
    CHECK (up.takes.count (1) == 1);     // the ad-lib is still lined up on its own
    CHECK (up.takes.count (3) == 0);     // guitars aren't listened to for words
    CHECK (up.takesLinedUp == 1);
}

TEST_CASE ("Lines nobody sang yet sit between the ones that were")
{
    const auto lyrics = cc::Lyrics::parse ("I was lost in the dark\nbut You found me and gave me life\nI'm alive, I'm alive\n");
    std::vector<cc::HeardWord> a, b;
    add (a, sing ("I was lost in the dark", 1.0));
    add (b, sing (chorus1, 1.0));
    // two takes: one sang the first line, one the last (each lined up alone)
    const auto up = cc::lineUpSong (lyrics, { take (1, 0.0, 4.0, a), take (2, 10.0, 4.0, b) });
    REQUIRE (up.lines.size() == 3);
    CHECK (up.lines[1].from == 0);
    CHECK (up.lines[1].start == Approx (3.3));   // after line 1 ends
    CHECK (up.lines[1].end == Approx (11.0));    // before line 3 starts
    CHECK (up.lines[1].words.size() == 8);

    // no takes: lines have no time (except from an .lrc file)
    const auto none = cc::lineUpSong (lyrics, {});
    CHECK (none.lines[0].start < 0);
    const auto lrc = cc::lineUpSong (cc::Lyrics::parse ("[00:05.00]one line\n[00:09.00]two line\n"), {});
    CHECK (lrc.lines[0].start == Approx (5.0));
    CHECK (lrc.lines[0].end == Approx (9.0));
    CHECK (lrc.lines[1].start == Approx (9.0));
    CHECK (lrc.lines[1].startPpq < 0);
}
