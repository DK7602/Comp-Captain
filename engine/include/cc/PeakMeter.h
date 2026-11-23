// Comp Captain engine: a lock-free peak meter. The audio thread only reads the samples (never changes
// them) and stores the loudest one; the window reads and resets it about 30 times a second.
#pragma once

#include <atomic>

namespace cc
{
    class PeakMeter
    {
    public:
        // Audio thread. Never allocates, never locks, never touches the audio.
        void process (const float* const* channels, int numChannels, int numSamples) noexcept;

        // Window. Returns the loudest absolute sample since the last call, then starts again.
        float readAndReset() noexcept;

        // Window. Looks without resetting.
        float peek() const noexcept { return peak.load (std::memory_order_relaxed); }

    private:
        std::atomic<float> peak { 0.0f };
    };
}
