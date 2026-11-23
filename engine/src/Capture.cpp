#include "cc/Capture.h"

#include "cc/TakesFolder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <system_error>

namespace cc
{
    // ============================================================================ CaptureFeed

    void CaptureFeed::prepare (double sampleRate, int numChannels, double seconds)
    {
        const std::scoped_lock lock (consumer);
        ready.store (false, std::memory_order_release);

        rate = sampleRate > 0 ? sampleRate : 48000.0;
        ringChannels = std::clamp (numChannels, 1, maxChannels);
        ringFrames = std::max<std::int64_t> (8192, (std::int64_t) std::ceil (std::max (1.0, seconds) * rate));
        ring.assign ((size_t) ringFrames * (size_t) ringChannels, 0.0f);
        writePos.store (0);
        readPos.store (0);
        eventWrite.store (0);
        eventRead.store (0);

        capturing = false;
        waitForRecordOff = false;
        pendingGap = 0;
        haveExpected = false;
        capturingNow.store (false);

        gen.fetch_add (1, std::memory_order_acq_rel);
        ready.store (true, std::memory_order_release);
    }

    void CaptureFeed::setArmed (bool on) noexcept { armed.store (on, std::memory_order_relaxed); }
    void CaptureFeed::setLoopTakes (bool on) noexcept { loopAll.store (on, std::memory_order_relaxed); }

    int CaptureFeed::freeEventSlots() const noexcept
    {
        return eventSlots - (int) (eventWrite.load (std::memory_order_relaxed) - eventRead.load (std::memory_order_acquire));
    }

    bool CaptureFeed::push (const CaptureEvent& ev) noexcept
    {
        const auto w = eventWrite.load (std::memory_order_relaxed);
        if ((int) (w - eventRead.load (std::memory_order_acquire)) >= eventSlots)
            return false;
        events[w % eventSlots] = ev;
        eventWrite.store (w + 1, std::memory_order_release);
        return true;
    }

    bool CaptureFeed::popEvent (CaptureEvent& out) noexcept
    {
        const auto r = eventRead.load (std::memory_order_relaxed);
        if (r == eventWrite.load (std::memory_order_acquire))
            return false;
        out = events[r % eventSlots];
        eventRead.store (r + 1, std::memory_order_release);
        return true;
    }

    bool CaptureFeed::peekEvent (CaptureEvent& out) const noexcept
    {
        const auto r = eventRead.load (std::memory_order_relaxed);
        if (r == eventWrite.load (std::memory_order_acquire))
            return false;
        out = events[r % eventSlots];
        return true;
    }

    std::int64_t CaptureFeed::framesReadable() const noexcept
    {
        return writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_relaxed);
    }

    int CaptureFeed::read (float* interleaved, int maxFrames) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto available = writePos.load (std::memory_order_acquire) - r;
        const int n = (int) std::min<std::int64_t> (available, maxFrames);
        for (int f = 0; f < n; ++f)
        {
            const auto at = (size_t) ((r + f) % ringFrames) * (size_t) ringChannels;
            for (int c = 0; c < ringChannels; ++c)
                interleaved[(size_t) f * (size_t) ringChannels + (size_t) c] = ring[at + (size_t) c];
        }
        readPos.store (r + n, std::memory_order_release);
        return n;
    }

    void CaptureFeed::startTake (const Transport& t) noexcept
    {
        if (freeEventSlots() < 3) // room for this start, a gap and the stop
            return;

        CaptureEvent ev;
        ev.kind = CaptureEvent::Kind::start;
        ev.frame = writePos.load (std::memory_order_relaxed);
        ev.sampleRate = rate;
        ev.channels = ringChannels;

        SongPosition& p = ev.position;
        p.bpm = clampBpm (t.bpm);
        p.meter = t.meter.isValid() ? t.meter : Meter {};
        const double secondsFromSamples = (double) t.timeInSamples / rate;
        p.seconds = t.hasSeconds ? t.seconds : (t.hasSamples ? secondsFromSamples : 0.0);
        p.ppq = t.hasPpq ? t.ppq : p.seconds * p.bpm / 60.0;
        if (! t.hasSeconds && ! t.hasSamples)
            p.seconds = p.ppq * 60.0 / p.bpm;
        p.hasBarStart = t.hasBarStart;
        p.barStartPpq = t.barStartPpq;
        ev.positionKnown = t.hasPpq || t.hasSeconds || t.hasSamples;

        push (ev);
        capturing = true;
        haveExpected = false;
        pendingGap = 0;
    }

    void CaptureFeed::endTake() noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        if (pendingGap > 0 && freeEventSlots() >= 2)
        {
            CaptureEvent gap;
            gap.kind = CaptureEvent::Kind::gap;
            gap.frame = w;
            gap.gapFrames = pendingGap;
            push (gap);
        }
        pendingGap = 0;

        CaptureEvent stop;
        stop.kind = CaptureEvent::Kind::stop;
        stop.frame = w;
        push (stop);
        capturing = false;
    }

    void CaptureFeed::store (const float* const* channels, int numChannels, int numFrames) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto freeFrames = ringFrames - (w - readPos.load (std::memory_order_acquire));

        if (pendingGap > 0)
        {
            // We fell behind earlier. Once there's room again, tell the writer how much silence to put
            // in, so everything after the gap still lines up with the song.
            if (freeFrames < numFrames || freeEventSlots() < 2)
            {
                pendingGap += numFrames;
                return;
            }
            CaptureEvent gap;
            gap.kind = CaptureEvent::Kind::gap;
            gap.frame = w;
            gap.gapFrames = pendingGap;
            push (gap);
            pendingGap = 0;
        }

        if (freeFrames < numFrames)
        {
            pendingGap += numFrames;
            return;
        }

        for (int f = 0; f < numFrames; ++f)
        {
            const auto at = (size_t) ((w + f) % ringFrames) * (size_t) ringChannels;
            for (int c = 0; c < ringChannels; ++c)
                ring[at + (size_t) c] = numChannels > 0 ? channels[std::min (c, numChannels - 1)][f] : 0.0f;
        }
        writePos.store (w + numFrames, std::memory_order_release);
    }

    void CaptureFeed::process (const float* const* channels, int numChannels, int numFrames, const Transport& t) noexcept
    {
        if (numFrames <= 0 || ! ready.load (std::memory_order_acquire))
            return;

        const bool rec = armed.load (std::memory_order_relaxed) && t.recording && t.playing;

        // Did the song position jump (the DAW's loop came round, or you clicked somewhere else)?
        bool jumped = false;
        if (capturing && haveExpected)
        {
            if (expectSamples && t.hasSamples)
            {
                jumped = std::llabs (t.timeInSamples - expectedSamples) > 32;
            }
            else if (expectPpq && t.hasPpq)
            {
                const double beatsPerBlock = (double) numFrames / rate * clampBpm (t.bpm) / 60.0;
                jumped = std::fabs (t.ppq - expectedPpq) > 0.05 + 0.02 * beatsPerBlock;
            }
        }

        if (capturing && (! rec || jumped))
        {
            endTake();
            if (jumped && rec && ! loopAll.load (std::memory_order_relaxed))
                waitForRecordOff = true; // Loop Takes off: keep only the first pass
        }
        if (! rec)
            waitForRecordOff = false;
        if (rec && ! capturing && ! waitForRecordOff)
            startTake (t);

        if (capturing)
        {
            store (channels, numChannels, numFrames);
            haveExpected = true;
            expectSamples = t.hasSamples;
            expectedSamples = t.timeInSamples + numFrames;
            expectPpq = t.hasPpq;
            expectedPpq = t.ppq + (double) numFrames / rate * clampBpm (t.bpm) / 60.0;
        }
        capturingNow.store (capturing, std::memory_order_relaxed);
    }

    // ============================================================================ CaptureWriter

    CaptureWriter::CaptureWriter (CaptureFeed& f) : feed (f) {}

    CaptureWriter::~CaptureWriter()
    {
        // A take still open (the plug-in closed mid-take): the file keeps what was recorded.
        wav.close();
    }

    void CaptureWriter::setDestination (const std::filesystem::path& f, const std::string& trackName, Role r)
    {
        const std::scoped_lock lock (destLock);
        folder = f;
        track = trackName;
        role = r;
    }

    int CaptureWriter::claimNumber()
    {
        if (numbering != nullptr)
            return numbering->fetch_add (1);
        return ownNumber++;
    }

    void CaptureWriter::releaseNumber (int n)
    {
        // A take that wasn't kept gives its number back, so you don't see "Take 6, Take 8".
        if (numbering != nullptr)
        {
            int expected = n + 1;
            numbering->compare_exchange_strong (expected, n);
        }
        else if (ownNumber == n + 1)
        {
            ownNumber = n;
        }
    }

    void CaptureWriter::post (CaptureNews n)
    {
        const std::scoped_lock lock (newsLock);
        news.push_back (std::move (n));
    }

    std::vector<CaptureNews> CaptureWriter::takeNews()
    {
        const std::scoped_lock lock (newsLock);
        std::vector<CaptureNews> out;
        out.swap (news);
        return out;
    }

    CaptureWriter::Live CaptureWriter::live (size_t from) const
    {
        const std::scoped_lock lock (liveLock);
        Live out;
        out.recording = liveState.recording;
        out.name = liveState.name;
        out.start = liveState.start;
        out.frames = liveState.frames;
        out.sampleRate = liveState.sampleRate;
        out.serial = liveState.serial;
        out.peaksFrom = std::min (from, liveState.peaks.size());
        out.peaks.assign (liveState.peaks.begin() + (std::ptrdiff_t) out.peaksFrom, liveState.peaks.end());
        return out;
    }

    void CaptureWriter::begin (const CaptureEvent& ev)
    {
        std::filesystem::path dest;
        {
            const std::scoped_lock lock (destLock);
            dest = folder;
            take = {};
            take.track = track;
            take.role = role;
        }
        take.source = TakeSource::capture;
        take.sampleRate = ev.sampleRate;
        take.channels = ev.channels;
        take.start = ev.position;
        takeNumber = claimNumber();
        take.name = "Take " + std::to_string (takeNumber);

        std::error_code ec;
        if (! dest.empty())
            std::filesystem::create_directories (dest, ec);
        if (dest.empty() || ec)
        {
            dropping = true;
            releaseNumber (takeNumber);
            post ({ CaptureNews::Kind::failed, take, {}, "Couldn't make the takes folder, so " + take.name + " wasn't saved: " + utf8FromPath (dest) });
            return;
        }

        takePath = uniqueFile (dest, take.name, ".wav");
        if (! wav.open (takePath, ev.sampleRate, ev.channels))
        {
            dropping = true;
            releaseNumber (takeNumber);
            post ({ CaptureNews::Kind::failed, take, {}, "Couldn't write " + take.name + " in " + utf8FromPath (dest) + ". Is the disk full?" });
            return;
        }

        take.file = utf8FromPath (takePath.filename());
        writing = true;
        overview.reset (ev.sampleRate);
        framesSinceHeader = 0;
        {
            const std::scoped_lock lock (liveLock);
            liveState.recording = true;
            liveState.name = take.name;
            liveState.start = take.start;
            liveState.frames = 0;
            liveState.sampleRate = ev.sampleRate;
            liveState.peaks.clear();
            ++liveState.serial;
        }
        post ({ CaptureNews::Kind::started, take, {}, "Recording " + take.name });
    }

    void CaptureWriter::finish()
    {
        {
            const std::scoped_lock lock (liveLock);
            liveState.recording = false;
        }
        if (dropping)
        {
            dropping = false;
            return;
        }
        if (! writing)
            return;
        writing = false;

        overview.finish();
        take.lengthSamples = wav.framesWritten();
        const bool ok = wav.close();
        const double seconds = take.lengthSeconds();
        std::error_code ec;

        if (! ok)
        {
            // Keep what made it to the disk: better a take that stops early than no take.
            if (take.lengthSamples > 0)
                post ({ CaptureNews::Kind::saved, take, overview.peaks(), take.name + " was cut short: the disk may be full." });
            else
            {
                std::filesystem::remove (takePath, ec);
                releaseNumber (takeNumber);
                post ({ CaptureNews::Kind::failed, take, {}, take.name + " couldn't be saved. Is the disk full?" });
            }
            return;
        }
        if (seconds < shortestTakeSeconds)
        {
            std::filesystem::remove (takePath, ec);
            releaseNumber (takeNumber);
            post ({ CaptureNews::Kind::discarded, take, {}, take.name + " was shorter than half a second, so it wasn't kept." });
            return;
        }
        if (overview.loudest() < silentBelow)
        {
            std::filesystem::remove (takePath, ec);
            releaseNumber (takeNumber);
            post ({ CaptureNews::Kind::discarded, take, {},
                    take.name + " was silent, so it wasn't kept. Comp Captain only hears you while the track is monitoring "
                                "(in Cubase: the speaker button on the track)." });
            return;
        }

        const auto bb = take.startBarBeat();
        const int secs = (int) std::lround (seconds);
        post ({ CaptureNews::Kind::saved, take, overview.peaks(),
                take.name + " saved: starts at bar " + bb.toText() + ", " + std::to_string (secs / 60) + ":"
                    + (secs % 60 < 10 ? "0" : "") + std::to_string (secs % 60) + " long." });
    }

    bool CaptureWriter::pump()
    {
        const std::scoped_lock lock (feed.consumerLock());
        if (! feed.isPrepared())
            return false;

        bool did = false;
        if (feed.generation() != seenGeneration)
        {
            // The buffer was rebuilt (new sample rate): close what we had.
            if (writing || dropping)
                finish();
            pending.reset();
            seenGeneration = feed.generation();
            did = true;
        }

        constexpr int chunk = 4096;
        buffer.resize ((size_t) chunk * (size_t) feed.numChannels());

        for (int guard = 0; guard < 100000; ++guard)
        {
            if (! pending)
            {
                CaptureEvent ev;
                if (feed.popEvent (ev))
                    pending = ev;
            }

            std::int64_t limit = feed.framesReadable();
            if (pending)
                limit = std::min (limit, pending->frame - feed.framesRead());

            if (limit > 0)
            {
                const int n = feed.read (buffer.data(), (int) std::min<std::int64_t> (limit, chunk));
                if (writing && n > 0)
                {
                    wav.writeInterleaved (buffer.data(), n);
                    overview.addInterleaved (buffer.data(), feed.numChannels(), n);
                    framesSinceHeader += n;
                }
                did = true;
                continue;
            }

            if (pending && feed.framesRead() >= pending->frame)
            {
                const auto ev = *pending;
                pending.reset();
                if (ev.kind == CaptureEvent::Kind::start)
                {
                    if (writing || dropping)
                        finish();
                    begin (ev);
                }
                else if (ev.kind == CaptureEvent::Kind::stop)
                {
                    finish();
                }
                else if (ev.kind == CaptureEvent::Kind::gap && writing)
                {
                    wav.writeSilence (ev.gapFrames);
                    overview.addSilence (ev.gapFrames);
                    framesSinceHeader += ev.gapFrames;
                }
                did = true;
                continue;
            }
            break;
        }

        if (writing)
        {
            if (framesSinceHeader >= (std::int64_t) take.sampleRate)
            {
                wav.updateHeader();
                framesSinceHeader = 0;
            }
            const std::scoped_lock liveGuard (liveLock);
            liveState.frames = wav.framesWritten();
            const auto& p = overview.peaks();
            if (p.size() > liveState.peaks.size())
                liveState.peaks.insert (liveState.peaks.end(), p.begin() + (std::ptrdiff_t) liveState.peaks.size(), p.end());
        }
        return did;
    }
}
