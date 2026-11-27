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

#include "cc/Features.h"
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
        const std::string& modelName() const noexcept; // "base.en-q5_1" for ggml-base.en-q5_1.bin

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

    // Everything about a take that's worked out by listening: reads the take (a WAV file), finds its
    // phrases, and hears its words (words heard where nobody sings are dropped: the recognizer sometimes
    // "hears" words in silence). Empty optional if it was stopped or failed (error says why).
    // features (optional): also works out what the take sounds like every 10 ms (for the scores), from the same read.
    std::optional<Hearing> hearTake (SpeechRecognizer&, const std::filesystem::path& wav,
                                     const SpeechRecognizer::Options&, std::string& error, TakeFeatures* features = nullptr);

    // The parts of a take that are listened to: the sung phrases (with a little room either side, phrases
    // under a second apart kept together), each placed 'at' seconds into the joined audio, with a pause of
    // partGap seconds after each one.
    struct SungPart
    {
        double from = 0.0, to = 0.0; // take seconds
        double at = 0.0;             // where it starts in the joined audio
    };
    inline constexpr double partGap = 0.5;
    std::vector<SungPart> sungParts (const std::vector<Phrase>& phrases, double takeLength);
    double takeTime (const std::vector<SungPart>&, double joinedSeconds);

    // Words that sit outside every phrase (by more than a little) are dropped.
    std::vector<HeardWord> keepWordsInPhrases (const std::vector<HeardWord>&, const std::vector<Phrase>&);
}
