// Speech recognition on a known recording (whisper.cpp's own sample: a famous speech, public domain).
// Runs when the build was given the model (CC_WHISPER_MODEL); CI downloads it.
#include <catch2/catch_test_macros.hpp>

#include "cc/Resample.h"
#include "cc/Speech.h"
#include "cc/WavFile.h"

#include <algorithm>
#include <cctype>

namespace
{
    std::string plain (std::string s)
    {
        std::string out;
        for (char c : s)
            if (std::isalpha ((unsigned char) c))
                out += (char) std::tolower ((unsigned char) c);
        return out;
    }
}

TEST_CASE ("Speech recognition hears the words and when they're said")
{
    const std::string model = CC_WHISPER_MODEL_PATH;
    if (model.empty())
        SKIP ("no model in this build");

    cc::SpeechRecognizer sr;
    std::string error;
    REQUIRE (sr.load (cc::pathFromUtf8 (model), error));

    const auto wav = cc::readWavFile (cc::pathFromUtf8 (std::string (CC_WHISPER_SAMPLES) + "/jfk.wav"));
    REQUIRE (wav.has_value());
    const auto audio = cc::resample (cc::toMono (wav->channels), wav->info.sampleRate, cc::SpeechRecognizer::sampleRate);

    cc::SpeechRecognizer::Options o;
    o.threads = 2;
    int lastProgress = -1;
    o.progress = [&] (int p) { lastProgress = p; };
    const auto words = sr.listen (audio.data(), audio.size(), o);
    REQUIRE (words.has_value());

    std::vector<std::string> heard;
    for (auto& w : *words)
        heard.push_back (plain (w.text));
    auto at = [&] (const std::string& word) { return std::find (heard.begin(), heard.end(), word); };
    REQUIRE (at ("ask") != heard.end());
    REQUIRE (at ("country") != heard.end());
    CHECK (at ("ask") < at ("country"));

    // times go forward and sit inside the recording
    double last = 0.0;
    for (auto& w : *words)
    {
        CHECK (w.start >= last - 0.05);
        CHECK (w.end >= w.start);
        CHECK (w.end <= (double) audio.size() / 16000.0 + 0.5);
        last = w.start;
    }
    // "And so, my fellow Americans" is said within the first two seconds
    const auto first = std::find_if (words->begin(), words->end(), [] (auto& w) { return plain (w.text) == "fellow"; });
    REQUIRE (first != words->end());
    CHECK (first->start < 3.0);

    // stopping early works
    std::atomic<bool> stop { true };
    o.cancel = &stop;
    CHECK_FALSE (sr.listen (audio.data(), audio.size(), o).has_value());
}

TEST_CASE ("A missing model gives a plain reason")
{
    cc::SpeechRecognizer sr;
    std::string error;
    CHECK_FALSE (sr.load ("/no/such/model.bin", error));
    CHECK (error.find ("missing") != std::string::npos);
    CHECK_FALSE (sr.isLoaded());
}
