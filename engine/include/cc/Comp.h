// Comp Captain engine: the comp. The chosen take for every lyric line, joined into one performance.
//  - Lines in a row from the same take play as one piece (no join inside it).
//  - A join between two takes goes in the quietest spot between the two lines (the gap or the breath), judged
//    from both takes' loudness, with a short crossfade (equal power): no clicks, no jumps. Where there's no gap
//    (one line runs into the next) the crossfade is a little longer.
//  - Level match: each piece gets one gain (no EQ, no squashing) so a louder take doesn't jump out, at most
//    +-6 dB and never into clipping.
//  - Hearing one take of one line uses the same mixing (planLine).
#pragma once

#include "cc/Align.h"
#include "cc/Features.h"
#include "cc/SongTime.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cc
{
    // A take that can be used in the comp.
    struct CompSource
    {
        std::uint64_t key = 0;
        double startSeconds = 0.0;                      // where the take starts in the song
        double lengthSeconds = 0.0;
        const std::vector<LinePlace>* places = nullptr; // where its lines are (take seconds)
        const TakeFeatures* features = nullptr;         // its loudness every 10 ms (quiet spots, level match); may be null
    };

    // The take chosen for a line (lines in song order).
    struct CompChoice
    {
        int line = 0;
        std::uint64_t take = 0;
    };

    // One take playing for one or more lines in a row.
    struct CompPiece
    {
        std::uint64_t take = 0;
        int firstLine = 0, lastLine = 0;
        double from = 0.0, to = 0.0;        // song seconds: where it takes over and where it hands on (the joins)
        double fadeIn = 0.0, fadeOut = 0.0; // crossfade lengths (seconds), centred on from / to
        double gainDb = 0.0;                // level match (gain only)
        float joinLevelDb = -120.0f;        // the louder of the two takes where it takes over (quiet = a clean join)
    };

    struct CompPlan
    {
        std::vector<CompPiece> pieces;
        double start = 0.0, end = 0.0;      // song seconds, fades included
        bool empty() const noexcept { return pieces.empty(); }
    };

    struct CompOptions
    {
        double before = 0.35;     // seconds kept before the first line (the breath in)
        double after = 0.8;       // at most this long after the last line (the last word's tail)
        double quietFade = 0.015; // a join in a gap
        double loudFade = 0.040;  // a join where someone's singing
        double maxGainDb = 6.0;
        bool levelMatch = true;
    };

    CompPlan planComp (const std::vector<CompChoice>&, const std::vector<CompSource>&, const CompOptions& = {});

    // One take on its own around one line (for hearing it): from `around` seconds before the line to after it.
    // Empty if the take didn't sing the line.
    CompPlan planLine (const CompSource&, int line, double around = 0.4);

    // A whole take on its own (for hearing it in time with the song).
    CompPlan planTake (const CompSource&);

    struct CompAudio
    {
        double sampleRate = 48000.0;
        double start = 0.0;                             // song seconds of the first sample
        std::vector<std::vector<float>> channels;
        std::int64_t frames() const noexcept { return channels.empty() ? 0 : (std::int64_t) channels[0].size(); }
    };

    // Where a take's audio is.
    struct CompFile
    {
        std::uint64_t key = 0;
        std::filesystem::path wav;
        double startSeconds = 0.0;                      // where the take starts in the song
    };

    // Mixes the plan's pieces from the takes' WAV files into one sound from songFrom to songTo (seconds; silence
    // where nothing plays), at sampleRate (a take at another rate is resampled), with `channels` channels (a mono
    // take goes to every channel, a stereo take is mixed down for mono). Empty with an error in plain words if a
    // file can't be read, or if cancel was set.
    std::optional<CompAudio> renderComp (const CompPlan&, const std::vector<CompFile>&, double songFrom, double songTo,
                                         double sampleRate, int channels, const std::atomic<bool>* cancel, std::string& error);

    // The bar line at or before `seconds` (steady tempo and meter from `ref`, a known place in the song), so a
    // rendered comp can start on a bar and land right where it's dropped.
    struct BarStart
    {
        int bar = 1;
        double seconds = 0.0;
        double ppq = 0.0;
    };
    BarStart barStartBefore (const SongPosition& ref, double seconds);
}
