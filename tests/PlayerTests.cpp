// Hearing a take or the comp from the plug-in: a free clip plays while the DAW is stopped and gives way when it
// starts; an in-time clip follows the song position while the DAW plays (never while recording); the track's own
// sound passes untouched otherwise; old clips are freed only once the audio thread is done with them.
#include <catch2/catch_test_macros.hpp>

#include "cc/Player.h"

#include <vector>

namespace
{
    std::unique_ptr<cc::Player::Clip> ramp (int frames, double songStart, bool inTime, std::uint32_t id = 1)
    {
        auto c = std::make_unique<cc::Player::Clip>();
        c->channels.assign (1, std::vector<float> ((size_t) frames));
        for (int i = 0; i < frames; ++i)
            c->channels[0][(size_t) i] = (float) (i + 1);
        c->sampleRate = 1000.0;
        c->songStart = songStart;
        c->inTime = inTime;
        c->id = id;
        return c;
    }

    struct Block
    {
        std::vector<float> left, right;
        float* ptrs[2];
        explicit Block (int n, float fill = -1.0f) : left ((size_t) n, fill), right ((size_t) n, fill) { ptrs[0] = left.data(); ptrs[1] = right.data(); }
    };
}

TEST_CASE ("A free clip plays while the DAW is stopped, then stops by itself", "[player]")
{
    cc::Player p;
    p.play (ramp (25, 3.0, false));
    Block b (10);
    REQUIRE (p.process (b.ptrs, 2, 10, 1000.0, false, false, 0.0));
    CHECK (b.left[0] == 1.0f);
    CHECK (b.left[9] == 10.0f);
    CHECK (b.right[9] == 10.0f); // a mono clip goes to both sides
    CHECK (p.state().sounding);
    CHECK (p.state().seconds == 3.010);
    REQUIRE (p.process (b.ptrs, 2, 10, 1000.0, false, false, 0.0));
    Block last (10);
    REQUIRE (p.process (last.ptrs, 2, 10, 1000.0, false, false, 0.0));
    CHECK (last.left[4] == 25.0f);
    CHECK (last.left[5] == 0.0f); // silence after the end
    CHECK (p.state().finished);
    Block after (10);
    CHECK_FALSE (p.process (after.ptrs, 2, 10, 1000.0, false, false, 0.0));
    CHECK (after.left[0] == -1.0f); // the track's own sound, untouched
}

TEST_CASE ("A free clip gives way when the DAW starts", "[player]")
{
    cc::Player p;
    auto c = ramp (100, 0.0, false);
    c->playFrom = 40;
    p.play (std::move (c));
    Block b (10);
    REQUIRE (p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0));
    CHECK (b.left[0] == 41.0f); // from where it was asked to start
    Block playing (10);
    CHECK_FALSE (p.process (playing.ptrs, 1, 10, 1000.0, true, false, 5.0));
    CHECK (playing.left[0] == -1.0f);
    CHECK (p.state().finished);
}

TEST_CASE ("An in-time clip follows the song while the DAW plays", "[player]")
{
    cc::Player p;
    p.play (ramp (100, 2.0, true)); // song 2.0 .. 2.1 s
    Block stopped (10);
    CHECK_FALSE (p.process (stopped.ptrs, 1, 10, 1000.0, false, false, 2.0)); // stopped: the track as it is
    Block before (10);
    CHECK_FALSE (p.process (before.ptrs, 1, 10, 1000.0, true, false, 1.5));   // before the clip: the track
    Block in (10);
    REQUIRE (p.process (in.ptrs, 1, 10, 1000.0, true, false, 2.05));
    CHECK (in.left[0] == 51.0f);
    Block edge (10);
    REQUIRE (p.process (edge.ptrs, 1, 10, 1000.0, true, false, 2.095));
    CHECK (edge.left[4] == 100.0f);
    CHECK (edge.left[5] == 0.0f);
    Block recording (10);
    CHECK_FALSE (p.process (recording.ptrs, 1, 10, 1000.0, true, true, 2.05)); // recording: never covers what's sung
    CHECK (recording.left[0] == -1.0f);
}

TEST_CASE ("Stopping, and a clip at the wrong sample rate, leave the track alone", "[player]")
{
    cc::Player p;
    p.play (ramp (100, 0.0, false));
    p.stop();
    Block b (10);
    CHECK_FALSE (p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0));
    p.play (ramp (100, 0.0, false));
    CHECK_FALSE (p.process (b.ptrs, 1, 10, 44100.0, false, false, 0.0));
    CHECK (b.left[0] == -1.0f);
}

TEST_CASE ("A new clip starts from its own beginning", "[player]")
{
    cc::Player p;
    p.play (ramp (100, 0.0, false, 1));
    Block b (10);
    p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0);
    p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0);
    p.play (ramp (100, 0.0, false, 2));
    CHECK (p.state().id == 2);
    CHECK_FALSE (p.state().sounding); // not played yet
    p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0);
    CHECK (b.left[0] == 1.0f);
    CHECK (p.state().sounding);
}

TEST_CASE ("Old clips are freed once the audio thread has moved on", "[player]")
{
    cc::Player p;
    p.play (ramp (100, 0.0, false, 1));
    p.play (ramp (100, 0.0, false, 2));
    p.play (ramp (100, 0.0, false, 3));
    // the audio thread isn't in a block: nothing can be using the old ones
    CHECK (p.waitingToBeFreed() == 0);
    Block b (10);
    p.process (b.ptrs, 1, 10, 1000.0, false, false, 0.0);
    p.play (ramp (100, 0.0, false, 4));
    p.tidy();
    CHECK (p.waitingToBeFreed() == 0);
    CHECK (p.state().id == 4);
}
