// Scores for every line of every take: made-up takes that are each wrong in one known way (a flat word, a late
// line, a word not heard, too quiet, clipped) must be caught, with the right reason, and the best take picked.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cc/Scores.h"

#include <algorithm>
#include <cmath>

using Catch::Approx;

namespace
{
    // One take of the line "I was lost tonight": four words, half a second each, from 1 s into the take.
    struct Fake
    {
        cc::TakeFeatures f;
        std::vector<cc::LinePlace> places;
        std::vector<cc::HeardWord> words;
    };

    struct How
    {
        double late = 0.0;          // seconds later than the others
        double flatOnLost = 0.0;    // cents flat on the third word
        double level = -15.0;       // dB
        bool clipOnWas = false;
        bool missTonight = false;
    };

    Fake make (const How& how)
    {
        Fake t;
        const size_t frames = 600;
        for (auto* v : { &t.f.pitch, &t.f.periodic })
            v->assign (frames, 0.0f);
        t.f.level.assign (frames, -75.0f);
        t.f.lowBand.assign (frames, -90.0f);
        t.f.highBand.assign (frames, -95.0f);
        t.f.peak.assign (frames, 0.0f);
        t.f.clipped.assign (frames, 0.0f);
        const double notes[] = { 220.0, 246.94, 261.63, 220.0 };
        const char* names[] = { "I", "was", "lost", "tonight" };
        cc::LinePlace p;
        p.line = 0;
        p.start = 1.0 + how.late;
        p.end = 3.0 + how.late - 0.05;
        p.sure = how.missTonight ? 0.75f : 1.0f;
        for (int w = 0; w < 4; ++w)
        {
            const double a = 1.0 + how.late + 0.5 * w, b = a + 0.45;
            const bool heard = ! (how.missTonight && w == 3);
            p.words.push_back ({ a, b, heard });
            if (heard)
                t.words.push_back ({ std::string (" ") + names[w], a, b, 0.9f });
            for (size_t i = t.f.frameAt (a); i < t.f.frameAt (b); ++i)
            {
                const double cents = w == 2 ? -how.flatOnLost : 0.0;
                t.f.pitch[i] = (float) (notes[w] * std::pow (2.0, cents / 1200.0));
                t.f.periodic[i] = 0.9f;
                t.f.level[i] = (float) how.level;
                t.f.lowBand[i] = (float) how.level - 15.0f;
                t.f.highBand[i] = (float) how.level - 20.0f;
                t.f.peak[i] = how.clipOnWas && w == 1 ? 1.0f : 0.3f;
                t.f.clipped[i] = how.clipOnWas && w == 1 ? 12.0f : 0.0f;
            }
            // the short gap between words
            for (size_t i = t.f.frameAt (b); i < t.f.frameAt (b + 0.05); ++i)
                t.f.level[i] = -60.0f;
        }
        t.places.push_back (p);
        return t;
    }

    cc::TakeForScoring forScoring (std::uint64_t key, const Fake& t, bool favourite = false)
    {
        cc::TakeForScoring s;
        s.key = key;
        s.startSeconds = 10.0; // every take starts at 10 s into the song (bar 6 at 120 BPM)
        s.startPpq = 20.0;
        s.bpm = 120.0;
        s.favourite = favourite;
        s.features = &t.f;
        s.places = &t.places;
        s.words = &t.words;
        return s;
    }

    const cc::LineScore& of (const cc::LineScores& line, std::uint64_t key)
    {
        return *std::find_if (line.takes.begin(), line.takes.end(), [&] (const cc::LineScore& s) { return s.take == key; });
    }

    bool says (const std::vector<std::string>& reasons, const std::string& part)
    {
        return std::any_of (reasons.begin(), reasons.end(), [&] (const std::string& r) { return r.find (part) != std::string::npos; });
    }
}

TEST_CASE ("Each line of each take gets its four scores and the reasons")
{
    const auto lyrics = cc::Lyrics::parse ("I was lost tonight\n");
    const auto good1 = make ({}), good2 = make ({}), good3 = make ({});
    const auto flat = make ({ .flatOnLost = 50.0 });
    const auto late = make ({ .late = 0.15 });
    const auto rough = make ({ .level = -21.0, .clipOnWas = true, .missTonight = true });
    const std::vector<cc::TakeForScoring> takes { forScoring (1, good1), forScoring (2, good2, true), forScoring (3, good3),
                                                  forScoring (4, flat), forScoring (5, late), forScoring (6, rough) };

    const auto scores = cc::scoreLines (lyrics, takes);
    REQUIRE (scores.size() == 1);
    REQUIRE (scores[0].takes.size() == 6);
    const auto& g = of (scores[0], 1);

    // a good take: high everywhere, and the reasons say so
    CHECK (g.pitch >= 95.0f);
    CHECK (g.timing >= 95.0f);
    CHECK (g.clarity >= 95.0f);
    CHECK (g.energy == Approx (85.0f).margin (2.0f));       // the same delivery as the others
    CHECK (g.problems.empty());
    CHECK (says (g.good, "Every word comes through clearly"));
    CHECK (says (g.good, "Words land right with your other takes"));

    // flat on "lost": a lower pitch score, and it says which word
    const auto& f = of (scores[0], 4);
    CHECK (f.pitch < g.pitch - 5.0f);
    CHECK (says (f.bad, "Flat on \"lost\" (about 50 cents)"));
    CHECK (f.timing >= 95.0f);

    // late: a lower timing score, and it says how late
    const auto& l = of (scores[0], 5);
    CHECK (l.timing < g.timing - 15.0f);
    CHECK (says (l.bad, "A little behind your other takes (about 150 ms)"));
    CHECK (l.pitch >= 95.0f);

    // a word not heard, quieter, clipped
    const auto& r = of (scores[0], 6);
    CHECK (r.clarity < g.clarity - 10.0f);
    CHECK (says (r.bad, "\"tonight\" wasn't heard clearly"));
    CHECK (r.energy < g.energy - 8.0f);
    CHECK (says (r.bad, "Softer than your other takes of this line"));
    REQUIRE (r.problems.size() == 1);
    CHECK (r.problems[0] == "Clipped on \"was\" (too loud for the input)");

    // the best take: one of the three good ones, and the favourite wins the tie
    const cc::ScoreWeights singing = *cc::styleWeights ("Singing");
    CHECK (cc::bestTake (scores[0], singing, takes) == 2);
    CHECK (cc::totalFor (r, singing) < cc::totalFor (g, singing) - 5.0);

    // Rap doesn't count pitch: the flat take is as good as the others there
    const cc::ScoreWeights rap = *cc::styleWeights ("Rap");
    CHECK (cc::totalFor (f, rap) == Approx (cc::totalFor (g, rap)).margin (0.5));
    CHECK (cc::totalFor (f, singing) < cc::totalFor (g, singing) - 1.0);
}

TEST_CASE ("One take on its own still gets pitch, timing and clarity")
{
    const auto lyrics = cc::Lyrics::parse ("I was lost tonight\n");
    const auto only = make ({});
    const auto scores = cc::scoreLines (lyrics, { forScoring (1, only) });
    REQUIRE (scores[0].takes.size() == 1);
    const auto& s = scores[0].takes[0];
    CHECK (s.pitch >= 95.0f);         // right on the semitones
    CHECK (s.timing >= 95.0f);        // right on the beat (bar 6 at 120 BPM)
    CHECK (s.clarity >= 95.0f);
    CHECK (s.energy < 0.0f);          // nothing to compare the delivery with
    CHECK (cc::totalFor (s, *cc::styleWeights ("Pop")) >= 95.0);
}

TEST_CASE ("Lines nobody sang have no scores; styles and words for reasons")
{
    const auto lyrics = cc::Lyrics::parse ("I was lost tonight\nand nobody sang this one\n");
    const auto t = make ({});
    const auto scores = cc::scoreLines (lyrics, { forScoring (1, t) });
    REQUIRE (scores.size() == 2);
    CHECK (scores[1].takes.empty());
    CHECK (cc::bestTake (scores[1], {}, {}) == 0);

    REQUIRE (cc::styles().size() == 9);
    CHECK (cc::styles().front().name == "Rap");
    CHECK (cc::styleWeights ("Worship / Gospel")->pitch == Approx (0.35));
    CHECK_FALSE (cc::styleWeights ("Polka").has_value());

    CHECK (cc::displayWord ("dark,") == "dark");
    CHECK (cc::displayWord ("\"Hallelujah!\"") == "Hallelujah");
    CHECK (cc::displayWord ("can't") == "can't");

    // each problem costs 3 points, 8 at most
    cc::LineScore s;
    s.pitch = s.timing = s.clarity = s.energy = 90.0f;
    CHECK (cc::totalFor (s, {}) == Approx (90.0));
    s.problems = { "a", "b" };
    CHECK (cc::totalFor (s, {}) == Approx (84.0));
    s.problems = { "a", "b", "c", "d" };
    CHECK (cc::totalFor (s, {}) == Approx (82.0));
}
