#include "cc/WavFile.h"

#include <algorithm>
#include <cstring>

namespace cc
{
    std::filesystem::path pathFromUtf8 (std::string_view s)
    {
        return std::filesystem::path (std::u8string (reinterpret_cast<const char8_t*> (s.data()), s.size()));
    }

    std::string utf8FromPath (const std::filesystem::path& p)
    {
        const auto u = p.u8string();
        return std::string (reinterpret_cast<const char*> (u.data()), u.size());
    }

    namespace
    {
        void put16 (unsigned char* p, std::uint32_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
        void put32 (unsigned char* p, std::uint32_t v) { put16 (p, v & 0xffff); put16 (p + 2, v >> 16); }
        std::uint32_t get16 (const unsigned char* p) { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8); }
        std::uint32_t get32 (const unsigned char* p) { return get16 (p) | (get16 (p + 2) << 16); }

        // RIFF (12) + fmt (8 + 18) + fact (8 + 4) + data header (8)
        constexpr int headerSize = 12 + 26 + 12 + 8;
        constexpr std::int64_t maxDataBytes = 0xFFFFFFFFll - headerSize; // plain WAV can't go past 4 GB

        void makeHeader (unsigned char* h, double rate, int channels, std::int64_t frames)
        {
            const auto dataBytes = (std::uint32_t) std::min<std::int64_t> (frames * channels * 4, maxDataBytes);
            std::memcpy (h, "RIFF", 4);
            put32 (h + 4, (std::uint32_t) (headerSize - 8) + dataBytes);
            std::memcpy (h + 8, "WAVE", 4);
            std::memcpy (h + 12, "fmt ", 4);
            put32 (h + 16, 18);
            put16 (h + 20, 3); // IEEE float
            put16 (h + 22, (std::uint32_t) channels);
            put32 (h + 24, (std::uint32_t) (rate + 0.5));
            put32 (h + 28, (std::uint32_t) (rate + 0.5) * (std::uint32_t) channels * 4);
            put16 (h + 32, (std::uint32_t) channels * 4);
            put16 (h + 34, 32);
            put16 (h + 36, 0); // no extra format bytes
            std::memcpy (h + 38, "fact", 4);
            put32 (h + 42, 4);
            put32 (h + 46, (std::uint32_t) std::min<std::int64_t> (frames, 0xFFFFFFFFll));
            std::memcpy (h + 50, "data", 4);
            put32 (h + 54, dataBytes);
        }
    }

    WavWriter::~WavWriter()
    {
        close();
    }

    bool WavWriter::open (const std::filesystem::path& path, double sampleRate, int numChannels)
    {
        close();
        if (sampleRate <= 0 || numChannels < 1 || numChannels > 8)
            return false;
        out.open (path, std::ios::binary | std::ios::trunc);
        if (! out.is_open())
            return false;
        rate = sampleRate;
        channels = numChannels;
        frames = 0;
        failed = false;
        unsigned char h[headerSize];
        makeHeader (h, rate, channels, 0);
        out.write (reinterpret_cast<const char*> (h), headerSize);
        return out.good();
    }

    bool WavWriter::writeInterleaved (const float* data, int numFrames)
    {
        if (! out.is_open() || failed || numFrames <= 0)
            return ! failed;
        const auto bytes = (std::int64_t) numFrames * channels * 4;
        if ((frames * channels * 4) + bytes > maxDataBytes)
        {
            failed = true; // a take over 4 GB: keep what we have
            return false;
        }
        // Takes are little-endian float on every computer we build for.
        out.write (reinterpret_cast<const char*> (data), (std::streamsize) bytes);
        if (! out.good())
        {
            failed = true;
            return false;
        }
        frames += numFrames;
        return true;
    }

    bool WavWriter::write (const float* const* data, int numFrames)
    {
        if (numFrames <= 0)
            return ! failed;
        scratch.resize ((size_t) numFrames * (size_t) channels);
        for (int f = 0; f < numFrames; ++f)
            for (int c = 0; c < channels; ++c)
                scratch[(size_t) f * (size_t) channels + (size_t) c] = data[c][f];
        return writeInterleaved (scratch.data(), numFrames);
    }

    bool WavWriter::writeSilence (std::int64_t numFrames)
    {
        constexpr int chunk = 4096;
        scratch.assign ((size_t) chunk * (size_t) channels, 0.0f);
        while (numFrames > 0)
        {
            const int n = (int) std::min<std::int64_t> (numFrames, chunk);
            if (! writeInterleaved (scratch.data(), n))
                return false;
            numFrames -= n;
        }
        return true;
    }

    void WavWriter::updateHeader()
    {
        if (! out.is_open())
            return;
        unsigned char h[headerSize];
        makeHeader (h, rate, channels, frames);
        const auto here = out.tellp();
        out.seekp (0);
        out.write (reinterpret_cast<const char*> (h), headerSize);
        out.seekp (here);
        out.flush();
    }

    bool WavWriter::close()
    {
        if (! out.is_open())
            return false;
        updateHeader();
        const bool ok = out.good() && ! failed;
        out.close();
        return ok;
    }

    bool WavReader::open (const std::filesystem::path& path)
    {
        in.close();
        in.clear();
        wav = {};
        position = 0;
        in.open (path, std::ios::binary);
        if (! in.is_open())
            return false;

        in.seekg (0, std::ios::end);
        const std::int64_t fileSize = (std::int64_t) in.tellg();
        in.seekg (0);

        unsigned char riff[12];
        if (! in.read (reinterpret_cast<char*> (riff), 12) || std::memcmp (riff, "RIFF", 4) != 0 || std::memcmp (riff + 8, "WAVE", 4) != 0)
            return false;

        bool haveFormat = false;
        std::int64_t at = 12;
        while (at + 8 <= fileSize)
        {
            unsigned char ch[8];
            in.seekg (at);
            if (! in.read (reinterpret_cast<char*> (ch), 8))
                return false;
            std::int64_t size = get32 (ch + 4);
            const std::int64_t body = at + 8;

            if (std::memcmp (ch, "fmt ", 4) == 0)
            {
                unsigned char f[40] {};
                const auto n = (std::streamsize) std::min<std::int64_t> (size, 40);
                if (n < 16 || ! in.read (reinterpret_cast<char*> (f), n))
                    return false;
                std::uint32_t tag = get16 (f);
                if (tag == 0xFFFE && n >= 26)
                    tag = get16 (f + 24); // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format
                wav.channels = (int) get16 (f + 2);
                wav.sampleRate = (double) get32 (f + 4);
                wav.bitsPerSample = (int) get16 (f + 14);
                wav.isFloat = tag == 3;
                const bool pcm = tag == 1 && (wav.bitsPerSample == 16 || wav.bitsPerSample == 24 || wav.bitsPerSample == 32);
                const bool flt = tag == 3 && (wav.bitsPerSample == 32 || wav.bitsPerSample == 64);
                if ((! pcm && ! flt) || wav.channels < 1 || wav.channels > 64 || wav.sampleRate <= 0)
                    return false;
                haveFormat = true;
            }
            else if (std::memcmp (ch, "data", 4) == 0)
            {
                if (! haveFormat)
                    return false;
                // A take cut short by a crash may say 0 (or too much): use what's really in the file.
                if (size == 0 || body + size > fileSize)
                    size = fileSize - body;
                const int frameBytes = wav.channels * wav.bitsPerSample / 8;
                wav.lengthFrames = size / frameBytes;
                dataStart = body;
                in.seekg (dataStart);
                return true;
            }
            at = body + size + (size & 1); // chunks are padded to an even size
        }
        return false;
    }

    void WavReader::seek (std::int64_t frame) noexcept
    {
        position = std::clamp<std::int64_t> (frame, 0, wav.lengthFrames);
    }

    int WavReader::read (float* const* dest, int numChannels, int numFrames)
    {
        if (! in.is_open() || wav.channels == 0 || numFrames <= 0)
            return 0;
        const auto remaining = wav.lengthFrames - position;
        const int n = (int) std::min<std::int64_t> (remaining, numFrames);
        if (n <= 0)
            return 0;

        const int bytesPer = wav.bitsPerSample / 8, frameBytes = bytesPer * wav.channels;
        raw.resize ((size_t) n * (size_t) frameBytes);
        in.seekg (dataStart + position * frameBytes);
        if (! in.read (reinterpret_cast<char*> (raw.data()), (std::streamsize) raw.size()))
            return 0;

        for (int f = 0; f < n; ++f)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                const int srcChannel = std::min (c, wav.channels - 1);
                const unsigned char* p = raw.data() + (size_t) f * (size_t) frameBytes + (size_t) srcChannel * (size_t) bytesPer;
                float v = 0.0f;
                if (wav.isFloat && bytesPer == 4)
                {
                    std::uint32_t bits = get32 (p);
                    std::memcpy (&v, &bits, 4);
                }
                else if (wav.isFloat && bytesPer == 8)
                {
                    std::uint64_t bits = (std::uint64_t) get32 (p) | ((std::uint64_t) get32 (p + 4) << 32);
                    double dv = 0;
                    std::memcpy (&dv, &bits, 8);
                    v = (float) dv;
                }
                else if (bytesPer == 2)
                {
                    v = (float) (std::int16_t) get16 (p) / 32768.0f;
                }
                else if (bytesPer == 3)
                {
                    std::int32_t s = (std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24);
                    v = (float) (s >> 8) / 8388608.0f;
                }
                else
                {
                    v = (float) ((double) (std::int32_t) get32 (p) / 2147483648.0);
                }
                dest[c][f] = v;
            }
        }
        position += n;
        return n;
    }

    std::optional<WavData> readWavFile (const std::filesystem::path& path)
    {
        WavReader reader;
        if (! reader.open (path))
            return std::nullopt;
        WavData data;
        data.info = reader.info();
        data.channels.assign ((size_t) data.info.channels, std::vector<float> ((size_t) data.info.lengthFrames));
        std::vector<float*> ptrs;
        constexpr int chunk = 65536;
        for (std::int64_t done = 0; done < data.info.lengthFrames;)
        {
            ptrs.clear();
            for (auto& ch : data.channels)
                ptrs.push_back (ch.data() + done);
            const int n = reader.read (ptrs.data(), data.info.channels, (int) std::min<std::int64_t> (chunk, data.info.lengthFrames - done));
            if (n <= 0)
                break;
            done += n;
        }
        return data;
    }
}
