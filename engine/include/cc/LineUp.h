// Comp Captain engine: where each lyric line sits in the song, from every take that was listened to.
// Each take is lined up on its own (see placeLines); a take that only covers part of the song (a punch-in
// on the second chorus) is then lined up again against the lines around where it sits in the song, so a
// chorus sung three times lands on the right one. The song's time for a line comes from the take that
// sang it most clearly (lead takes first).
#pragma once

#include "cc/Align.h"
#include "cc/Take.h"

#include <cstdint>
#include <map>
#include <vector>

namespace cc
{
    struct TakeHeard
    {
        std::uint64_t key = 0;        // which take (any number that's unique in the song)
        double startSeconds = 0.0;    // where it starts in the song
        double startPpq = 0.0;        // the same in quarter notes
        double bpm = 120.0;           // the tempo there
        double lengthSeconds = 0.0;
        Role role = Role::lead;
        bool favourite = false;
        const std::vector<HeardWord>* words = nullptr;
        const std::vector<Phrase>* phrases = nullptr; // its sung phrases (lines settle on them)
    };

    struct SongLine
    {
        int line = 0;
        double start = -1.0;          // seconds into the song (-1: not known yet)
        double end = -1.0;
        double startPpq = 0.0, endPpq = 0.0;
        float sure = 0.0f;            // 0..1, from the take whose timing is used (0: worked out from its neighbours)
        int takes = 0;                // how many takes sang it
        std::uint64_t from = 0;       // the take whose timing is used (0: none)
        std::vector<WordTime> words;  // song seconds, one per shown word
    };

    struct SongLineUp
    {
        std::vector<SongLine> lines;                          // one per lyric line
        std::map<std::uint64_t, std::vector<LinePlace>> takes; // per take, in take seconds
        int takesLinedUp = 0;                                 // takes that sang some of these lyrics
    };

    // Vocal roles take part (not guitars).
    bool isSung (Role);

    SongLineUp lineUpSong (const Lyrics&, const std::vector<TakeHeard>&);
}
