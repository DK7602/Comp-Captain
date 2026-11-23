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

TEST_CASE ("A take is read, split into phrases and heard")
{
    const std::string model = CC_WHISPER_MODEL_PATH;
    if (model.empty())
        SKIP ("no model in this build");
    cc::SpeechRecognizer sr;
    std::string error;
    REQUIRE (sr.load (cc::pathFromUtf8 (model), error));
    CHECK (sr.modelName() == "base.en-q5_1");

    cc::SpeechRecognizer::Options o;
    const auto h = cc::hearTake (sr, cc::pathFromUtf8 (std::string (CC_WHISPER_SAMPLES) + "/jfk.wav"), o, error);
    REQUIRE (h.has_value());
    CHECK (h->model == "base.en-q5_1");
    CHECK (h->phrases.size() >= 3);                // the speech has clear pauses
    CHECK (h->words.size() >= 15);
    CHECK_FALSE (cc::hearTake (sr, "/no/such/take.wav", o, error).has_value());
    CHECK (error.find ("couldn't be read") != std::string::npos);
}

TEST_CASE ("Words heard where nobody sings are dropped")
{
    const std::vector<cc::HeardWord> words { { " Thank", 0.5, 0.8, 0.5f }, { " you.", 0.8, 1.0, 0.5f },
                                             { " I", 2.0, 2.2, 0.9f }, { " was", 2.2, 2.5, 0.9f }, { " lost", 4.1, 4.4, 0.9f } };
    const auto kept = cc::keepWordsInPhrases (words, { { 2.0, 3.0 }, { 4.5, 6.0 } });
    REQUIRE (kept.size() == 3);                       // "lost" ends just before the second phrase: kept
    CHECK (kept[0].text == " I");
    CHECK (cc::keepWordsInPhrases (words, {}).size() == words.size());
}

TEST_CASE ("Only the sung parts are listened to, and words go back to their place in the take")
{
    // phrases at 8-10.5, 11-14 (a breath: kept together) and 30-32 (after a long gap: a part of its own)
    const auto parts = cc::sungParts ({ { 8.0, 10.5 }, { 11.0, 14.0 }, { 30.0, 32.0 } }, 40.0);
    REQUIRE (parts.size() == 2);
    CHECK (parts[0].from == 7.75);
    CHECK (parts[0].to == 14.25);
    CHECK (parts[0].at == 0.0);
    CHECK (parts[1].from == 29.75);
    CHECK (parts[1].at == 6.5 + cc::partGap);
    CHECK (cc::takeTime (parts, 0.25) == 8.0);
    CHECK (cc::takeTime (parts, 6.8) == 14.25);              // in the pause: the end of the part before
    CHECK (cc::takeTime (parts, 7.25) == 30.0);
}

TEST_CASE ("A missing model gives a plain reason")
{
    cc::SpeechRecognizer sr;
    std::string error;
    CHECK_FALSE (sr.load ("/no/such/model.bin", error));
    CHECK (error.find ("missing") != std::string::npos);
    CHECK_FALSE (sr.isLoaded());
}
