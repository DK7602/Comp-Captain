// Comp Captain engine: hearing a take or the comp from inside the plug-in, without ever making the audio thread
// wait. The window side hands over a finished sound (a Clip, worked out in the background); the audio thread plays it:
//  - free: from a starting point while the DAW is stopped. It stops by itself at the end, or when the DAW starts.
//  - in time: while the DAW plays, what the clip has at the song position replaces the track's sound (outside the
//    clip, and while recording, the track plays as normal).
// A clip is never freed on the audio thread: the message thread frees an old one once the audio thread is done with it.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace cc
{
    class Player
    {
    public:
        struct Clip
        {
            std::vector<std::vector<float>> channels;
            double sampleRate = 48000.0;
            double songStart = 0.0;     // song seconds of the first sample
            bool inTime = false;        // follows the DAW's playhead (true) or plays on its own while stopped (false)
            std::int64_t playFrom = 0;  // free: the first frame to play
            std::uint32_t id = 0;       // the window's name for it
            std::uint64_t serial = 0;   // set by play(): tells the audio thread it's a new clip

            std::int64_t frames() const noexcept { return channels.empty() ? 0 : (std::int64_t) channels[0].size(); }
        };

        Player() = default;
        ~Player();
        Player (const Player&) = delete;
        Player& operator= (const Player&) = delete;

        // ---------------------------------------------------------------- message thread
        void play (std::unique_ptr<Clip>); // replaces whatever is playing
        void stop();
        void tidy();                       // frees clips the audio thread is done with (call it regularly)

        struct State
        {
            std::uint32_t id = 0;   // the clip handed over (0: none)
            bool inTime = false;
            bool sounding = false;  // the audio thread played it in the last block
            bool finished = false;  // a free clip reached its end, or the DAW started
            double seconds = 0.0;   // song seconds being played
        };
        State state() const;
        size_t waitingToBeFreed() const;

        // ---------------------------------------------------------------- audio thread
        // Writes the clip over `out` and returns true, or returns false (leave the track's own sound).
        bool process (float* const* out, int numChannels, int numFrames, double sampleRate,
                      bool hostPlaying, bool hostRecording, double hostSeconds) noexcept;

    private:
        // shared
        std::atomic<Clip*> current { nullptr };
        std::atomic<std::uint64_t> generation { 0 }, seenGeneration { 0 };
        std::atomic<bool> inProcess { false };
        std::atomic<std::uint64_t> playingSerial { 0 };
        std::atomic<bool> sounding { false }, finished { false };
        std::atomic<double> position { 0.0 };

        // message thread
        mutable std::mutex lock;
        struct Owned
        {
            std::unique_ptr<Clip> clip;
            std::uint64_t retiredAt = 0; // the generation that replaced it
        };
        Owned live;
        std::vector<Owned> retired;
        std::uint64_t nextSerial = 1;
        void retireLive();  // under lock
        void tidyLocked();  // under lock

        // audio thread
        std::uint64_t lastSerial = 0;
        std::int64_t at = 0;
        bool done = false;
    };
}
