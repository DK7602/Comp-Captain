// Comp Captain engine: speech recognition (whisper.cpp, on this computer: nothing is sent anywhere).
// Hears the words in a take and when each one starts and ends. Used to line lyrics up with the audio
// (M2) and to write lyrics from a take (Hear It). Slow work: always call it from a background thread.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cc/Words.h"

namespace cc
{
    class SpeechRecognizer
    {
    public:
        static constexpr int sampleRate = 16000; // audio must be mono at this rate (see cc::resample)

        SpeechRecognizer();
        ~SpeechRecognizer();
        SpeechRecognizer (const SpeechRecognizer&) = delete;
        SpeechRecognizer& operator= (const SpeechRecognizer&) = delete;

        // Loads a whisper model file (ggml format). False (with a plain-words reason) if it can't.
        bool load (const std::filesystem::path& model, std::string& error);
        bool isLoaded() const noexcept;

        struct Options
        {
            int threads = 2;
            std::string prompt;                       // the lyrics, if known: helps it hear the right words
            const std::atomic<bool>* cancel = nullptr; // set to true to stop early
            std::function<void (int percent)> progress;
        };

        // The words in the audio, in order, with their times. Empty optional if it was stopped or failed.
        std::optional<std::vector<HeardWord>> listen (const float* audio, size_t numSamples, const Options&);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
