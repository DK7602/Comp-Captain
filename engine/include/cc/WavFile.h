// Comp Captain engine: reading and writing WAV files, with no outside code.
// Takes are written as 32-bit float WAV. The header is kept up to date while recording, so even if
// the computer crashes mid-take the file opens in any DAW with everything up to the crash.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    // Paths are handed around as UTF-8 text; these make sure names like "Sofía's Verse" work on Windows too.
    std::filesystem::path pathFromUtf8 (std::string_view);
    std::string utf8FromPath (const std::filesystem::path&);

    class WavWriter
    {
    public:
        WavWriter() = default;
        ~WavWriter();
        WavWriter (const WavWriter&) = delete;
        WavWriter& operator= (const WavWriter&) = delete;

        bool open (const std::filesystem::path&, double sampleRate, int numChannels);
        bool isOpen() const noexcept { return out.is_open(); }

        // Planar (one pointer per channel) or interleaved (L R L R ...).
        bool write (const float* const* channels, int numFrames);
        bool writeInterleaved (const float* frames, int numFrames);
        bool writeSilence (std::int64_t numFrames);

        // Writes the current length into the header (cheap; done regularly while recording).
        void updateHeader();
        bool close();

        std::int64_t framesWritten() const noexcept { return frames; }
        int numChannels() const noexcept { return channels; }
        double sampleRate() const noexcept { return rate; }

    private:
        std::ofstream out;
        std::vector<float> scratch;
        std::int64_t frames = 0;
        int channels = 0;
        double rate = 0;
        bool failed = false;
    };

    struct WavInfo
    {
        double sampleRate = 0;
        int channels = 0;
        int bitsPerSample = 0;
        bool isFloat = false;
        std::int64_t lengthFrames = 0;
    };

    // Streams a WAV file: 16 / 24 / 32-bit PCM and 32 / 64-bit float, any number of channels.
    class WavReader
    {
    public:
        bool open (const std::filesystem::path&);
        const WavInfo& info() const noexcept { return wav; }

        // Reads up to numFrames into dest[channel][frame] (planar floats). Returns frames read (0 at the end).
        int read (float* const* dest, int numChannels, int numFrames);

        // Where the next read starts (frames from the start, clamped to the file).
        void seek (std::int64_t frame) noexcept;

    private:
        std::ifstream in;
        WavInfo wav;
        std::int64_t dataStart = 0, position = 0;
        std::vector<unsigned char> raw;
    };

    struct WavData
    {
        WavInfo info;
        std::vector<std::vector<float>> channels;
    };

    // The whole file in memory (tests and small files).
    std::optional<WavData> readWavFile (const std::filesystem::path&);
}
