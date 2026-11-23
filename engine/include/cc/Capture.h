// Comp Captain engine: CAPTURE. While it's armed and the DAW is recording, every pass that comes through
// the track becomes a take, stamped with where it started in the song.
//
// Two halves, so the audio thread never waits for the disk:
//   CaptureFeed   - on the audio thread: copies the sound into a ring buffer (no locks, no memory
//                   allocation, never blocks) and notes when a take starts and stops.
//   CaptureWriter - on a background thread: empties the ring into a WAV file and builds its waveform.
#pragma once

#include "cc/Overview.h"
#include "cc/SongTime.h"
#include "cc/Take.h"
#include "cc/WavFile.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cc
{
    // What the DAW said about the transport for one audio block.
    struct Transport
    {
        bool playing = false;
        bool recording = false;

        bool hasSamples = false;
        std::int64_t timeInSamples = 0;
        bool hasSeconds = false;
        double seconds = 0.0;
        bool hasPpq = false;
        double ppq = 0.0;

        double bpm = 120.0;
        Meter meter;
        bool hasBarStart = false;
        double barStartPpq = 0.0;
    };

    struct CaptureEvent
    {
        enum class Kind : std::uint8_t { start, stop, gap };

        Kind kind = Kind::start;
        std::int64_t frame = 0;      // where in the stream of captured frames it happens
        std::int64_t gapFrames = 0;  // gap: frames of silence that couldn't be stored (the disk fell behind)
        SongPosition position;       // start: where the take starts in the song
        bool positionKnown = false;  // start: the DAW said where we were
        double sampleRate = 48000.0;
        int channels = 1;
    };

    class CaptureFeed
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int eventSlots = 64;

        // Not while the audio thread is running process() on a prepared feed (call it from
        // prepareToPlay, or before the first arm). The buffer holds `seconds` of sound in case the
        // disk is slow for a moment.
        void prepare (double sampleRate, int numChannels, double seconds = 8.0);
        bool isPrepared() const noexcept { return ready.load (std::memory_order_acquire); }

        // Any thread.
        void setArmed (bool) noexcept;
        bool isArmed() const noexcept { return armed.load (std::memory_order_relaxed); }
        // On: every time the DAW's loop comes round, a new take. Off: only the first pass is kept.
        void setLoopTakes (bool) noexcept;
        bool loopTakes() const noexcept { return loopAll.load (std::memory_order_relaxed); }
        bool isCapturing() const noexcept { return capturingNow.load (std::memory_order_relaxed); }
        std::uint32_t generation() const noexcept { return gen.load (std::memory_order_acquire); }

        // Audio thread. Never blocks, never allocates. The audio itself is not touched.
        void process (const float* const* channels, int numChannels, int numFrames, const Transport&) noexcept;

        // Writer thread.
        bool popEvent (CaptureEvent&) noexcept;
        bool peekEvent (CaptureEvent&) const noexcept;
        std::int64_t framesReadable() const noexcept;
        std::int64_t framesRead() const noexcept { return readPos.load (std::memory_order_relaxed); }
        int read (float* interleaved, int maxFrames) noexcept;
        int numChannels() const noexcept { return ringChannels; }
        double sampleRate() const noexcept { return rate; }

        // Held by the writer while it reads, and by prepare() while it rebuilds the buffer.
        std::mutex& consumerLock() noexcept { return consumer; }

    private:
        bool push (const CaptureEvent&) noexcept;
        int freeEventSlots() const noexcept;
        void startTake (const Transport&) noexcept;
        void endTake() noexcept;
        void store (const float* const* channels, int numChannels, int numFrames) noexcept;

        std::mutex consumer;
        std::atomic<bool> ready { false }, armed { false }, loopAll { true }, capturingNow { false };
        std::atomic<std::uint32_t> gen { 0 };

        std::vector<float> ring;            // interleaved frames
        std::int64_t ringFrames = 0;
        int ringChannels = 1;
        double rate = 48000.0;
        std::atomic<std::int64_t> writePos { 0 }, readPos { 0 };

        std::array<CaptureEvent, eventSlots> events {};
        std::atomic<std::uint32_t> eventWrite { 0 }, eventRead { 0 };

        // audio thread only
        bool capturing = false, waitForRecordOff = false;
        std::int64_t pendingGap = 0;
        bool haveExpected = false, expectSamples = false, expectPpq = false;
        std::int64_t expectedSamples = 0;
        double expectedPpq = 0.0;
    };

    struct CaptureNews
    {
        enum class Kind { started, saved, discarded, failed };
        Kind kind = Kind::saved;
        Take take;                       // started / saved: the take (its id is set by whoever adds it to a list)
        std::vector<std::uint8_t> peaks; // saved: its waveform
        std::string message;             // plain words for the window
    };

    class CaptureWriter
    {
    public:
        static constexpr double shortestTakeSeconds = 0.5;
        static constexpr float silentBelow = 0.001f; // -60 dB: nothing came through

        explicit CaptureWriter (CaptureFeed&);
        ~CaptureWriter();

        // Message thread: where takes go. nextNumber is shared with the take list ("Take N").
        void setDestination (const std::filesystem::path& folder, const std::string& trackName, Role role);
        void setNumbering (std::atomic<int>* nextNumber) noexcept { numbering = nextNumber; }
        // Writer thread: does whatever is waiting. Returns true if there was something to do.
        bool pump();

        // Message thread.
        std::vector<CaptureNews> takeNews();

        struct Live
        {
            bool recording = false;
            std::string name;
            SongPosition start;
            std::int64_t frames = 0;
            double sampleRate = 48000.0;
            size_t peaksFrom = 0;            // index of the first peak in `peaks`
            std::vector<std::uint8_t> peaks; // the waveform so far, from peaksFrom on
            std::uint32_t serial = 0;        // changes with every new take
        };
        // The take being recorded right now, with its waveform from peak `from` on.
        Live live (size_t from) const;

    private:
        void begin (const CaptureEvent&);
        void finish();
        void post (CaptureNews);
        int claimNumber();
        void releaseNumber (int);

        CaptureFeed& feed;
        std::uint32_t seenGeneration = 0;
        std::optional<CaptureEvent> pending;

        // where to write (message thread sets, writer reads)
        mutable std::mutex destLock;
        std::filesystem::path folder;
        std::string track;
        Role role = Role::lead;
        std::atomic<int>* numbering = nullptr;
        int ownNumber = 1;

        // the take being written (writer thread)
        bool writing = false, dropping = false;
        WavWriter wav;
        OverviewBuilder overview;
        Take take;
        int takeNumber = 0;
        std::filesystem::path takePath;
        std::vector<float> buffer;
        std::int64_t framesSinceHeader = 0;

        mutable std::mutex liveLock;
        Live liveState;

        std::mutex newsLock;
        std::vector<CaptureNews> news;
    };
}
