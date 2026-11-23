// Lyrics in: what people paste (headings in every style, karaoke .lrc files, notes like "(x2)"), the words
// of a line, Hear It, and lining lyrics up with the words heard in a take.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestFolder.h"
#include "cc/Align.h"
#include "cc/Lyrics.h"
#include "cc/Words.h"

using Catch::Approx;

TEST_CASE ("Section headings in every style people use")
{
    std::string name;
    CHECK (cc::isSectionHeading ("[Verse 1]", name));
    CHECK (name == "Verse 1");
    CHECK (cc::isSectionHeading ("Chorus:", name));
    CHECK (name == "Chorus");
    CHECK (cc::isSectionHeading ("(Bridge)", name));
    CHECK (cc::isSectionHeading ("PRE-CHORUS", name));
    CHECK (name == "Pre-Chorus");
    CHECK (cc::isSectionHeading ("Verse 2:", name));
    CHECK (name == "Verse 2");
    CHECK (cc::isSectionHeading ("Chorus x2", name));
    CHECK (name == "Chorus");
    CHECK (cc::isSectionHeading ("[Hook - Lead]", name));
    CHECK_FALSE (cc::isSectionHeading ("(oh, oh, oh)", name));      // sung
    CHECK_FALSE (cc::isSectionHeading ("I was lost in the dark", name));
    CHECK_FALSE (cc::isSectionHeading ("[01:23.45]", name));       // a karaoke time
    CHECK_FALSE (cc::isSectionHeading ("(x2)", name));             // a note
    CHECK_FALSE (cc::isSectionHeading ("Chorus of angels singing", name));
}

TEST_CASE ("Pasted lyrics become sections and lines")
{
    const auto l = cc::Lyrics::parse (
        "[Verse 1]\r\n"
        "I was lost in the dark\n"
        "  but You found me  \n"
        "\n"
        "Chorus:\n"
        "I'm alive, I'm alive\n"
        "(x2)\n"
        "*repeat*\n"
        "[Bridge]\n"
        "[Outro]\n"
        "You gave me a brand new start\n");
    REQUIRE (l.sections.size() == 3);                  // the empty Bridge is dropped
    CHECK (l.sections[0].name == "Verse 1");
    CHECK (l.sections[1].name == "Chorus");
    CHECK (l.sections[2].name == "Outro");
    REQUIRE (l.lines.size() == 4);
    CHECK (l.lines[1].text == "but You found me");    // trimmed
    CHECK (l.lines[2].section == 1);
    CHECK (l.lines[3].section == 2);
    CHECK (l.sections[2].firstLine == 3);

    // lines before any heading still count
    const auto plain = cc::Lyrics::parse ("one line\nanother line\n");
    REQUIRE (plain.sections.size() == 1);
    CHECK (plain.lines.size() == 2);

    // and back to text for the editor
    CHECK (cc::Lyrics::parse (l.toText()).toText() == l.toText());
    CHECK (l.toText().rfind ("[Verse 1]\nI was lost in the dark\n", 0) == 0);
}

TEST_CASE ("Karaoke .lrc files keep their times")
{
    const auto l = cc::Lyrics::parse (
        "[ti:Brand New Start]\n"
        "[ar:Comp Captain]\n"
        "[offset:+500]\n"
        "[00:12.50]I was lost in the dark\n"
        "[00:30.00][01:10.00]I'm alive, I'm alive\n"
        "[00:15.25]but You found me\n");
    REQUIRE (l.lines.size() == 4);
    CHECK (l.lines[0].text == "I was lost in the dark");
    CHECK (l.lines[0].lrcTime == Approx (12.0));      // 12.5 s less the 0.5 s offset
    CHECK (l.lines[1].text == "but You found me");    // put back in time order
    CHECK (l.lines[3].lrcTime == Approx (69.5));
}

TEST_CASE ("Words are compared without capitals or punctuation")
{
    CHECK (cc::plainWord (" Can't,") == "cant");
    CHECK (cc::plainWord ("HALLELUJAH!") == "hallelujah");
    CHECK (cc::plainWords ("I'm alive - I'm alive!") == std::vector<std::string> { "im", "alive", "im", "alive" });
    CHECK (cc::wordLikeness ("love", "love") == 1.0);
    CHECK (cc::wordLikeness ("love", "loves") == Approx (0.8));
    CHECK (cc::wordLikeness ("dark", "start") < 0.5);
}

namespace
{
    std::vector<cc::HeardWord> heard (std::initializer_list<std::pair<const char*, double>> words)
    {
        std::vector<cc::HeardWord> out;
        for (auto& [t, s] : words)
            out.push_back ({ std::string (" ") + t, s, s + 0.3, 0.9f });
        return out;
    }

    const char* song =
        "[Verse 1]\n"
        "I was lost in the dark\n"
        "but You found me and gave me life\n"
        "[Chorus]\n"
        "I'm alive, I'm alive\n"
        "singing out with all I am\n"
        "[Verse 2]\n"
        "Morning light is breaking through\n"
        "every chain is falling too\n";
}

TEST_CASE ("Lines are placed where their words were heard")
{
    const auto lyrics = cc::Lyrics::parse (song);
    const auto words = heard ({ { "I", 1.0 }, { "was", 1.3 }, { "lost", 1.6 }, { "in", 1.9 }, { "the", 2.1 }, { "dark.", 2.4 },
                                { "But", 3.5 }, { "you", 3.8 }, { "found", 4.1 }, { "me", 4.4 }, { "and", 4.7 }, { "gave", 5.0 }, { "me", 5.3 }, { "life", 5.6 } });
    const auto placed = cc::placeLines (lyrics, words);
    REQUIRE (placed.size() == 2);
    CHECK (placed[0].line == 0);
    CHECK (placed[0].start == Approx (1.0));
    CHECK (placed[0].end == Approx (2.7));
    CHECK (placed[0].sure == Approx (1.0));
    CHECK (placed[1].line == 1);
    CHECK (placed[1].start == Approx (3.5));
    CHECK (placed[1].end == Approx (5.9));
}

TEST_CASE ("A take of one section finds that section, through misheard words and ad-libs")
{
    const auto lyrics = cc::Lyrics::parse (song);
    // only Verse 2 was sung; "breaking" was heard as "braking", "chain" was missed, and "yeah" was added
    const auto words = heard ({ { "yeah", 0.2 }, { "Morning", 1.0 }, { "light", 1.4 }, { "is", 1.7 }, { "braking", 1.9 }, { "through", 2.3 },
                                { "every", 3.6 }, { "is", 4.2 }, { "falling", 4.5 }, { "too", 4.9 } });
    const auto placed = cc::placeLines (lyrics, words);
    REQUIRE (placed.size() == 2);
    CHECK (placed[0].line == 4);
    CHECK (placed[0].start == Approx (1.0));          // the ad-lib before it isn't part of the line
    CHECK (placed[1].line == 5);
    CHECK (placed[1].start == Approx (3.6));
    CHECK (placed[1].sure < 1.0f);                    // one word wasn't heard
    CHECK (placed[1].sure > 0.6f);
}

TEST_CASE ("A line that wasn't heard at all sits between its neighbours")
{
    const auto lyrics = cc::Lyrics::parse ("[Chorus]\nI'm alive, I'm alive\nsinging out with all I am\nevery chain is falling too\n");
    const auto words = heard ({ { "I'm", 1.0 }, { "alive", 1.3 }, { "I'm", 1.6 }, { "alive", 1.9 },
                                { "every", 5.0 }, { "chain", 5.3 }, { "is", 5.6 }, { "falling", 5.9 }, { "too", 6.2 } });
    const auto placed = cc::placeLines (lyrics, words);
    REQUIRE (placed.size() == 3);
    CHECK (placed[1].sure == 0.0f);
    CHECK (placed[1].start == Approx (2.2));
    CHECK (placed[1].end == Approx (5.0));
}

TEST_CASE ("A take that sings other words isn't forced onto these lyrics")
{
    const auto lyrics = cc::Lyrics::parse (song);
    CHECK (cc::placeLines (lyrics, heard ({ { "completely", 1.0 }, { "different", 1.4 }, { "song", 1.8 } })).empty());
    CHECK (cc::placeLines (lyrics, {}).empty());
    CHECK (cc::placeLines ({}, heard ({ { "I", 1.0 } })).empty());
}

TEST_CASE ("Hear It writes lines at the pauses")
{
    const auto words = heard ({ { "I", 1.0 }, { "was", 1.3 }, { "lost.", 1.6 }, { "but", 3.0 }, { "you", 3.3 }, { "found,", 3.6 }, { "me", 3.9 } });
    CHECK (cc::lyricsFromWords (words) == "I was lost\nBut you found me\n");
}

TEST_CASE ("Heard words are kept next to the take")
{
    cctest::TestFolder dir;
    const auto take = dir.path / "Take 1.wav";
    const auto words = heard ({ { "Hallelujah,", 1.25 }, { "tab\there", 2.5 } });
    REQUIRE (cc::saveWords (take, words));
    CHECK (cc::wordsFileFor (take).filename() == "Take 1.words");
    const auto back = cc::loadWords (take);
    REQUIRE (back.has_value());
    REQUIRE (back->size() == 2);
    CHECK ((*back)[0].text == " Hallelujah,");
    CHECK ((*back)[0].start == Approx (1.25));
    CHECK ((*back)[1].text == " tab\there");
    CHECK_FALSE (cc::loadWords (dir.path / "Take 2.wav").has_value());
}
