// Comp Captain engine: a take (one pass of a part) and the list of takes on a track.
// Saved with the DAW project as plain "key=value" text; the audio itself lives in the takes folder.
#pragma once

#include "cc/SongTime.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    // What a take is for. Drives how it's scored and tightened later.
    enum class Role { lead, doubled, harmony, adlib, backing, guitar, other };

    inline constexpr Role allRoles[] = { Role::lead, Role::doubled, Role::harmony, Role::adlib,
                                         Role::backing, Role::guitar, Role::other };

    std::string_view toName (Role);  // "lead", "double", "harmony", "adlib", "backing", "guitar", "other"
    std::string_view toLabel (Role); // "Lead", "Double", "Harmony", "Ad-lib", "Backing", "Guitar", "Other"
    bool fromName (std::string_view, Role&);

    // Guesses a role from a track or file name ("Vox Dbl L" -> double, "BGV stack" -> backing).
    // Nothing recognisable: no guess.
    std::optional<Role> guessRole (std::string_view name);

    enum class TakeSource
    {
        capture, // recorded through CAPTURE
        file,    // dropped in or added from a file
        ara      // a take on the DAW's lanes, seen through ARA (not saved by us: the DAW keeps it)
    };

    std::string_view toName (TakeSource);
    bool fromName (std::string_view, TakeSource&);

    struct Take
    {
        std::uint32_t id = 0;        // unique on its track, never reused
        std::string name;            // "Take 3", or the file's name
        Role role = Role::lead;
        bool roleChosen = false;     // the owner picked the role (so we never guess over it)
        bool favourite = false;      // the heart: wins ties
        TakeSource source = TakeSource::capture;

        std::string file;            // the audio, inside the takes folder ("Take 3.wav")
        std::string originalFile;    // for added files: where it came from
        double sampleRate = 48000.0;
        int channels = 1;
        std::int64_t lengthSamples = 0;

        SongPosition start;          // where it starts in the song
        std::string track;           // the DAW track it was recorded on

        double lengthSeconds() const noexcept;
        double lengthQuarters() const noexcept; // at the tempo it started at
        BarBeat startBarBeat() const { return barBeatAt (start); }

        bool operator== (const Take&) const = default;
    };

    class TakeList
    {
    public:
        // Adds a take, giving it the next id. Returns the id.
        std::uint32_t add (Take);
        bool remove (std::uint32_t id);
        Take* find (std::uint32_t id);
        const Take* find (std::uint32_t id) const;

        const std::vector<Take>& all() const noexcept { return takes; }
        std::vector<Take>& edit() noexcept { return takes; } // change takes in place (not their ids)
        bool empty() const noexcept { return takes.empty(); }
        size_t size() const noexcept { return takes.size(); }

        // Takes in song order (by start, then by when they were made).
        std::vector<const Take*> inSongOrder() const;

        // The folder (UTF-8 path) this track's takes are saved in. Empty until the first take.
        std::string folder;

        // "Take N" numbering keeps counting up, so a removed take's name and file are never reused.
        int nextTakeNumber = 1;

        std::string toText() const;
        static TakeList fromText (std::string_view);

        bool operator== (const TakeList&) const = default;

    private:
        std::vector<Take> takes;
        std::uint32_t nextId = 1;
    };
}
