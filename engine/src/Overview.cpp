#include "cc/Overview.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace cc
{
    std::uint8_t peakToByte (float linearPeak) noexcept
    {
        if (! (linearPeak > 0.0f))
            return 0;
        const float v = std::sqrt (std::min (1.0f, linearPeak));
        return (std::uint8_t) std::lround (v * 255.0f);
    }

    OverviewBuilder::OverviewBuilder (double sampleRate, double slicesPerSecond)
    {
        reset (sampleRate, slicesPerSecond);
    }

    void OverviewBuilder::reset (double sampleRate, double slicesPerSecond)
    {
        slices.clear();
        perSecond = slicesPerSecond > 0 ? slicesPerSecond : defaultSlicesPerSecond;
        framesPerSlice = std::max (1.0, (sampleRate > 0 ? sampleRate : 48000.0) / perSecond);
        sliceEnd = framesPerSlice;
        frame = 0;
        current = 0.0f;
        hasCurrent = false;
        maxPeak = 0.0f;
    }

    void OverviewBuilder::push (float peak)
    {
        slices.push_back (peakToByte (peak));
    }

    void OverviewBuilder::add (const float* const* channels, int numChannels, int numFrames)
    {
        for (int f = 0; f < numFrames; ++f)
        {
            float a = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                a = std::max (a, std::fabs (channels[c][f]));
            if (! std::isfinite (a))
                a = 1.0f;
            current = std::max (current, a);
            maxPeak = std::max (maxPeak, a);
            hasCurrent = true;
            if ((double) ++frame >= sliceEnd)
            {
                push (current);
                current = 0.0f;
                hasCurrent = false;
                sliceEnd += framesPerSlice;
            }
        }
    }

    void OverviewBuilder::addInterleaved (const float* data, int numChannels, int numFrames)
    {
        for (int f = 0; f < numFrames; ++f)
        {
            const float* frameStart = data + (size_t) f * (size_t) numChannels;
            float a = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                a = std::max (a, std::fabs (frameStart[c]));
            if (! std::isfinite (a))
                a = 1.0f;
            current = std::max (current, a);
            maxPeak = std::max (maxPeak, a);
            hasCurrent = true;
            if ((double) ++frame >= sliceEnd)
            {
                push (current);
                current = 0.0f;
                hasCurrent = false;
                sliceEnd += framesPerSlice;
            }
        }
    }

    void OverviewBuilder::addSilence (std::int64_t numFrames)
    {
        while (numFrames > 0)
        {
            const auto toEdge = (std::int64_t) std::ceil (sliceEnd - (double) frame);
            const auto n = std::max<std::int64_t> (1, std::min (numFrames, toEdge));
            frame += n;
            numFrames -= n;
            hasCurrent = true;
            if ((double) frame >= sliceEnd)
            {
                push (current);
                current = 0.0f;
                hasCurrent = false;
                sliceEnd += framesPerSlice;
            }
        }
    }

    void OverviewBuilder::finish()
    {
        if (hasCurrent)
            push (current);
        current = 0.0f;
        hasCurrent = false;
    }

    namespace
    {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    }

    std::string toBase64 (const std::uint8_t* data, size_t size)
    {
        std::string out;
        out.reserve ((size + 2) / 3 * 4);
        for (size_t i = 0; i < size; i += 3)
        {
            const std::uint32_t b0 = data[i];
            const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
            const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
            const std::uint32_t v = (b0 << 16) | (b1 << 8) | b2;
            out += alphabet[(v >> 18) & 63];
            out += alphabet[(v >> 12) & 63];
            out += i + 1 < size ? alphabet[(v >> 6) & 63] : '=';
            out += i + 2 < size ? alphabet[v & 63] : '=';
        }
        return out;
    }

    std::vector<std::uint8_t> fromBase64 (std::string_view s)
    {
        std::vector<std::uint8_t> out;
        std::uint32_t acc = 0;
        int bits = 0;
        for (char c : s)
        {
            const auto pos = alphabet.find (c);
            if (pos == std::string_view::npos)
                continue; // '=' padding, line breaks
            acc = (acc << 6) | (std::uint32_t) pos;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back ((std::uint8_t) ((acc >> bits) & 0xff));
            }
        }
        return out;
    }
}
