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

TEST_CASE ("A chorus named again sings it again")
{
    const auto l = cc::Lyrics::parse (
        "[Chorus]\n"
        "I'm alive, I'm alive\n"
        "singing out with all I am\n"
        "[Verse 2]\n"
        "Morning light is breaking through\n"
        "[Chorus]\n"
        "\n"
        "[Tag x2]\n"
        "You gave me a brand new start\n"
        "[Bridge]\n");                                 // never written out: dropped
    REQUIRE (l.sections.size() == 5);
    CHECK (l.sections[2].name == "Chorus");
    CHECK (l.sections[2].lineCount == 2);
    CHECK (l.sections[3].name == "Tag");
    CHECK (l.sections[4].name == "Tag");                 // x2
    REQUIRE (l.lines.size() == 7);
    CHECK (l.lines[3].text == "I'm alive, I'm alive");
    CHECK (l.lines[3].section == 2);
    CHECK (l.lines[6].text == "You gave me a brand new start");
    CHECK (l.lines[6].section == 4);

    std::string name;
    int times = 0;
    CHECK (cc::isSectionHeading ("[Chorus x3]", name, times));
    CHECK (name == "Chorus");
    CHECK (times == 3);
    CHECK (cc::isSectionHeading ("Chorus (2x):", name, times));
    CHECK (name == "Chorus");
    CHECK (times == 2);
    CHECK (cc::isSectionHeading ("Verse 2", name, times));
    CHECK (times == 1);
}

TEST_CASE ("Chord sheets: chord rows and chords in brackets aren't sung")
{
    CHECK (cc::isChord ("G"));
    CHECK (cc::isChord ("D/F#"));
    CHECK (cc::isChord ("Em7"));
    CHECK (cc::isChord ("Csus4"));
    CHECK (cc::isChord ("Bbmaj7"));
    CHECK (cc::isChord ("F#m7b5"));
    CHECK (cc::isChord ("(A)"));
    CHECK_FALSE (cc::isChord ("Be"));
    CHECK_FALSE (cc::isChord ("Add"));
    CHECK_FALSE (cc::isChord ("Dim"));
    CHECK_FALSE (cc::isChord ("I"));
    CHECK_FALSE (cc::isChord ("Verse"));

    const auto l = cc::Lyrics::parse (
        "__________VERSE 1\n"
        "G          D/F#        Em7\n"
        "I was lost in the dark\n"
        "[C]but You [G]found me\n"
        "|  C  |  G  |  x2\n"
        "__________\n"
        "__________CHORUS\n"
        "[G] [D]\n"
        "Be still and know\n");
    REQUIRE (l.sections.size() == 2);
    CHECK (l.sections[0].name == "Verse 1");
    CHECK (l.sections[1].name == "Chorus");
    REQUIRE (l.lines.size() == 3);
    CHECK (l.lines[0].text == "I was lost in the dark");
    CHECK (l.lines[1].text == "but You found me");
    CHECK (l.lines[2].text == "Be still and know");
}

TEST_CASE ("A line's words as shown")
{
    CHECK (cc::shownWords ("  Pre-chorus  time\tnow ") == std::vector<std::string> { "Pre-chorus", "time", "now" });
    CHECK (cc::shownWords ("").empty());
}

TEST_CASE ("Words are compared without capitals or punctuation")
{
    CHECK (cc::plainWord (" Can't,") == "cant");
    CHECK (cc::plainWord ("HALLELUJAH!") == "hallelujah");
    CHECK (cc::plainWords ("I'm alive - I'm alive!") == std::vector<std::string> { "im", "alive", "im", "alive" });
    CHECK (cc::wordLikeness ("love", "love") == 1.0);
    CHECK (cc::wordLikeness ("love", "loves") == Approx (0.8));
    CHECK (cc::wordLikeness ("dark", "start") < 0.5);
    // curly apostrophes and quotes from a word processor, dashes between words
    CHECK (cc::plainWord ("don\xE2\x80\x99t") == "dont");
    CHECK (cc::plainWord ("\xE2\x80\x9CLove\xE2\x80\x9D") == "love");
    CHECK (cc::plainWords ("free\xE2\x80\x94" "free") == std::vector<std::string> { "free", "free" });
    CHECK (cc::plainWord ("caf\xC3\xA9") == "caf\xC3\xA9");
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
    // each shown word has its time
    REQUIRE (placed[0].words.size() == 6);
    CHECK (placed[0].words[2].start == Approx (1.6));
    CHECK (placed[0].words[2].heard);
}

TEST_CASE ("Words that weren't heard share the time around them")
{
    const auto lyrics = cc::Lyrics::parse ("Morning light is breaking through\n");
    const auto placed = cc::placeLines (lyrics, heard ({ { "Morning", 1.0 }, { "light", 1.4 }, { "through", 2.6 } }));
    REQUIRE (placed.size() == 1);
    const auto& w = placed[0].words;
    REQUIRE (w.size() == 5);
    CHECK_FALSE (w[2].heard);
    CHECK (w[2].start == Approx (1.7));                 // "light" ends at 1.7, "through" starts at 2.6
    CHECK (w[3].start == Approx (2.15));
    CHECK (w[4].heard);

    // "Pre-chorus" is one shown word made of two plain ones
    const auto l2 = cc::Lyrics::parse ("the pre-chorus starts\n");
    const auto p2 = cc::placeLines (l2, heard ({ { "the", 1.0 }, { "pre", 1.3 }, { "chorus", 1.6 }, { "starts", 1.9 } }));
    REQUIRE (p2.size() == 1);
    REQUIRE (p2[0].words.size() == 3);
    CHECK (p2[0].words[1].start == Approx (1.3));
    CHECK (p2[0].words[1].end == Approx (1.9));
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

TEST_CASE ("What was heard is kept next to the take")
{
    cctest::TestFolder dir;
    const auto take = dir.path / "Take 1.wav";
    cc::Hearing h;
    h.words = heard ({ { "Hallelujah,", 1.25 }, { "tab\there", 2.5 } });
    h.phrases = { { 1.2, 2.0 }, { 2.4, 3.1 } };
    h.model = "base.en-q5_1";
    REQUIRE (cc::saveHearing (take, h));
    CHECK (cc::hearingFileFor (take).filename() == "Take 1.words");
    const auto back = cc::loadHearing (take);
    REQUIRE (back.has_value());
    REQUIRE (back->words.size() == 2);
    CHECK (back->words[0].text == " Hallelujah,");
    CHECK (back->words[0].start == Approx (1.25));
    CHECK (back->words[1].text == " tab\there");
    REQUIRE (back->phrases.size() == 2);
    CHECK (back->phrases[1].end == Approx (3.1));
    CHECK (back->model == "base.en-q5_1");
    CHECK_FALSE (cc::loadHearing (dir.path / "Take 2.wav").has_value());
    CHECK_FALSE (cc::hearingFromText ("something else").has_value());
}

TEST_CASE ("Lines settle on the breath before them")
{
    const auto lyrics = cc::Lyrics::parse ("So happy birthday baby\nI'm so glad we're friends\nAnd so on\n");
    // the recognizer put each line about a second early; the singing starts at 14.27 and 17.65
    auto placed = cc::placeLines (lyrics, heard ({ { "So", 13.0 }, { "happy", 13.4 }, { "birthday", 13.9 }, { "baby", 14.5 },
                                                   { "I'm", 16.7 }, { "so", 17.0 }, { "glad", 17.3 }, { "we're", 17.6 }, { "friends", 18.0 },
                                                   { "And", 30.0 }, { "so", 30.3 }, { "on", 30.6 } }));
    REQUIRE (placed.size() == 3);
    cc::settleOnPhrases (placed, { { 14.27, 16.5 }, { 17.65, 20.79 }, { 33.0, 35.0 } });
    CHECK (placed[0].start == Approx (14.27));
    CHECK (placed[0].words[0].start == Approx (14.27));     // its words move with it
    CHECK (placed[0].words[1].start == Approx (14.67));
    CHECK (placed[0].end == Approx (16.5));                 // and it ends where the phrase ends
    CHECK (placed[1].start == Approx (17.65));
    CHECK (placed[2].start == Approx (30.0));               // no phrase start near it: it stays
}
