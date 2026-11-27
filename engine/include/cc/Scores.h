// Comp Captain engine: a score for every line of every take, each with plain-words reasons.
//  - Pitch:   on the notes, against the melody your takes agree on (and the semitones, when there's only one take)
//  - Timing:  words landing with your other takes, and on the beat
//  - Clarity: the lyric heard clearly (speech recognition heard each word)
//  - Energy:  louder and brighter than your other takes of that line (delivery)
//  - Problems (the warning badge): clipping, pops, a loud breath before the line, background noise
// Takes are compared with the other takes of the same part (one track's takes), never with harmonies on other tracks.
#pragma once

#include "cc/Align.h"
#include "cc/Features.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    struct ScoreWeights
    {
        double pitch = 0.25, timing = 0.25, clarity = 0.25, energy = 0.25;
        bool operator== (const ScoreWeights&) const = default;
    };

    struct Style
    {
        std::string name;
        ScoreWeights weights;
    };

    // Rap, Trap, Singing, Pop, Country, Rock, Worship / Gospel, R&B, My Own (in that order)
    const std::vector<Style>& styles();
    std::optional<ScoreWeights> styleWeights (std::string_view name);

    struct TakeForScoring
    {
        std::uint64_t key = 0;
        double startSeconds = 0.0;                  // where the take starts in the song
        double startPpq = 0.0, bpm = 120.0;         // for the beat grid
        bool favourite = false;
        const TakeFeatures* features = nullptr;
        const std::vector<LinePlace>* places = nullptr; // where its lines are (take seconds)
        const std::vector<HeardWord>* words = nullptr;  // what was heard (take seconds)
    };

    struct LineScore
    {
        std::uint64_t take = 0;
        int line = 0;
        float pitch = -1.0f, timing = -1.0f, clarity = -1.0f, energy = -1.0f; // 0..100, -1 = not scored
        std::vector<std::string> good;      // what's good about it, in plain words
        std::vector<std::string> bad;       // what could be better
        std::vector<std::string> problems;  // the warning badge: something wrong in the recording
    };

    struct LineScores
    {
        int line = 0;
        std::vector<LineScore> takes;       // the takes that sang this line
    };

    // Every line of the lyrics, with a score for each take that sang it.
    std::vector<LineScores> scoreLines (const Lyrics&, const std::vector<TakeForScoring>&);

    // The total for a style (0..100): the weighted scores, less a little for each problem (3 each, 8 at most).
    double totalFor (const LineScore&, const ScoreWeights&);

    // The best take for the line (the highest total; a favourite wins a tie). 0 if no take sang it.
    std::uint64_t bestTake (const LineScores&, const ScoreWeights&, const std::vector<TakeForScoring>&);

    // "dark," -> "dark", "\"Hallelujah!\"" -> "Hallelujah" (for reasons that name a word)
    std::string displayWord (std::string_view);
}
