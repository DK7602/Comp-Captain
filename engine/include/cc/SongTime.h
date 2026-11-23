// Comp Captain engine: where things sit in the song. Positions are kept in quarter notes ("ppq", what the
// DAW reports) so a take stays on its bar even if the window shows the song at a different zoom.
#pragma once

#include <string>
#include <string_view>

namespace cc
{
    struct Meter
    {
        int numerator = 4;
        int denominator = 4;

        bool isValid() const noexcept;
        double quartersPerBeat() const noexcept; // 4/4: 1, 6/8: 0.5
        double quartersPerBar() const noexcept;  // 4/4: 4, 6/8: 3, 3/4: 3

        std::string toText() const;              // "4/4"
        static Meter fromText (std::string_view);                  // 4/4 if it isn't a time signature
        static Meter fromText (std::string_view, Meter fallback);

        bool operator== (const Meter&) const = default;
    };

    // A place in the song as the DAW described it.
    struct SongPosition
    {
        double ppq = 0.0;          // quarter notes from the song start
        double seconds = 0.0;      // seconds from the song start
        double bpm = 120.0;        // tempo there
        Meter meter;               // time signature there
        double barStartPpq = 0.0;  // where the bar it's in starts (the DAW tells us; it's exact)
        bool hasBarStart = false;
        int barNumber = 0;         // the bar's number when the DAW knows it exactly (ARA reads the
                                   // song's bar map, so time-signature changes count); 0 = work it out

        bool operator== (const SongPosition&) const = default;
    };

    // Bar 1, beat 1 is the very start of the song.
    struct BarBeat
    {
        int bar = 1;
        double beat = 1.0; // 1.0 = on the bar line, 2.5 = halfway through beat 2

        std::string toText() const; // "5" on the bar line, "5.3" from beat 3 on
    };

    // Bar and beat for a position. Uses the DAW's own bar start when it gave one (right even when the
    // song started with a pickup), otherwise counts bars from the song start in the current meter.
    BarBeat barBeatAt (const SongPosition&);
    BarBeat barBeatAtPpq (double ppq, Meter);

    // Quarter notes from the song start for bar / beat (both start at 1) in a steady meter.
    double ppqForBarBeat (int bar, double beat, Meter);

    // A whole song at one tempo (used when a file is dropped in: "starts at bar 9, 92 BPM").
    SongPosition positionAtBar (int bar, double beat, double bpm, Meter);

    double clampBpm (double bpm); // 20 .. 400, 120 if it's nonsense
}
