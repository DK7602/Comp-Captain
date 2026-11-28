#include "cc/Player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cc
{
    Player::~Player()
    {
        current.store (nullptr);
    }

    void Player::retireLive()
    {
        if (live.clip == nullptr)
            return;
        live.retiredAt = generation.load() + 1; // the generation that stops anyone reaching it
        retired.push_back (std::move (live));
        live = {};
    }

    void Player::play (std::unique_ptr<Clip> clip)
    {
        const std::scoped_lock l (lock);
        retireLive();
        if (clip != nullptr)
            clip->serial = nextSerial++;
        live.clip = std::move (clip);
        current.store (live.clip.get());
        generation.fetch_add (1);
        tidyLocked();
    }

    void Player::stop()
    {
        play (nullptr);
    }

    void Player::tidy()
    {
        const std::scoped_lock l (lock);
        tidyLocked();
    }

    void Player::tidyLocked()
    {
        // A retired clip is free to go once the audio thread has started a block after it was replaced (it reads the
        // clip afresh every block), or while the audio thread isn't in a block at all (the next one finds the new clip).
        const auto seen = seenGeneration.load();
        const bool idle = ! inProcess.load();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [&] (const Owned& o) { return idle || seen >= o.retiredAt; }),
                       retired.end());
    }

    Player::State Player::state() const
    {
        const std::scoped_lock l (lock);
        State s;
        if (live.clip == nullptr)
            return s;
        s.id = live.clip->id;
        s.inTime = live.clip->inTime;
        const bool mine = playingSerial.load() == live.clip->serial;
        s.sounding = mine && sounding.load();
        s.finished = mine && finished.load();
        s.seconds = mine ? position.load() : live.clip->songStart + (double) live.clip->playFrom / live.clip->sampleRate;
        return s;
    }

    size_t Player::waitingToBeFreed() const
    {
        const std::scoped_lock l (lock);
        return retired.size();
    }

    bool Player::process (float* const* out, int numChannels, int numFrames, double sampleRate,
                          bool hostPlaying, bool hostRecording, double hostSeconds) noexcept
    {
        inProcess.store (true);
        const auto gen = generation.load();
        const Clip* c = current.load();
        bool wrote = false;

        if (c != nullptr && numFrames > 0 && numChannels > 0 && ! c->channels.empty() && std::abs (c->sampleRate - sampleRate) < 1.0)
        {
            if (c->serial != lastSerial)
            {
                lastSerial = c->serial;
                at = std::clamp<std::int64_t> (c->playFrom, 0, c->frames());
                done = false;
                playingSerial.store (c->serial);
                finished.store (false);
            }
            const auto length = c->frames();
            const int clipChannels = (int) c->channels.size();
            auto copy = [&] (std::int64_t from) {
                // clip frames from..from+numFrames over the output (silence beyond the clip)
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto& src = c->channels[(size_t) std::min (ch, clipChannels - 1)];
                    float* dst = out[ch];
                    for (int i = 0; i < numFrames; ++i)
                    {
                        const auto k = from + i;
                        dst[i] = (k >= 0 && k < length) ? src[(size_t) k] : 0.0f;
                    }
                }
            };

            if (! c->inTime)
            {
                if (hostPlaying)
                    done = true; // the DAW started: back to the track
                if (! done)
                {
                    copy (at);
                    at += numFrames;
                    wrote = true;
                    if (at >= length)
                        done = true;
                }
                position.store (c->songStart + (double) at / c->sampleRate);
                finished.store (done);
            }
            else if (hostPlaying && ! hostRecording)
            {
                const auto from = (std::int64_t) std::llround ((hostSeconds - c->songStart) * c->sampleRate);
                if (from + numFrames > 0 && from < length)
                {
                    copy (from);
                    wrote = true;
                }
                position.store (hostSeconds);
            }
        }
        sounding.store (wrote);
        seenGeneration.store (gen);
        inProcess.store (false);
        return wrote;
    }
}
