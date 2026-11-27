// cc_score <model.bin> <lyrics.txt> <take1.wav> <take2.wav> ... [--style Rap]
// Listens to each take (all starting at the same place in the song), lines the lyrics up, and prints every line's
// scores for every take, with the reasons and the best take. A tool for trying the scores on real takes; not part of
// the plug-in.
#include "cc/LineUp.h"
#include "cc/Scores.h"
#include "cc/Speech.h"
#include "cc/TextFile.h"
#include "cc/WavFile.h"

#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::cout << "Usage: cc_score <model.bin> <lyrics.txt> <take1.wav> [take2.wav ...] [--style Rap]" << std::endl;
        return 2;
    }
    std::string style = "Singing";
    std::vector<std::string> files;
    for (int i = 3; i < argc; ++i)
    {
        if (std::string (argv[i]) == "--style" && i + 1 < argc)
            style = argv[++i];
        else
            files.push_back (argv[i]);
    }
    const auto weights = cc::styleWeights (style).value_or (cc::ScoreWeights {});

    cc::SpeechRecognizer sr;
    std::string error;
    if (! sr.load (cc::pathFromUtf8 (argv[1]), error))
    {
        std::cout << error << std::endl;
        return 1;
    }
    std::ifstream in (cc::pathFromUtf8 (argv[2]), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto lyrics = cc::Lyrics::parse (cc::textFromBytes (ss.str()));

    std::deque<cc::Hearing> heard;
    std::deque<cc::TakeFeatures> features;
    std::vector<cc::TakeHeard> lineUpInput;
    cc::SpeechRecognizer::Options o;
    o.threads = (int) std::max (1u, std::thread::hardware_concurrency() / 2);
    for (size_t i = 0; i < files.size(); ++i)
    {
        features.emplace_back();
        auto h = cc::hearTake (sr, cc::pathFromUtf8 (files[i]), o, error, &features.back());
        if (! h)
        {
            std::cout << files[i] << ": " << error << std::endl;
            return 1;
        }
        heard.push_back (std::move (*h));
        cc::TakeHeard t;
        t.key = i + 1;
        t.lengthSeconds = (double) features.back().frames() * cc::TakeFeatures::hop;
        t.words = &heard.back().words;
        t.phrases = &heard.back().phrases;
        lineUpInput.push_back (t);
        std::printf ("T%zu  %s: %zu words, %zu phrases\n", i + 1, files[i].c_str(), heard.back().words.size(), heard.back().phrases.size());
    }
    const auto up = cc::lineUpSong (lyrics, lineUpInput);

    std::vector<cc::TakeForScoring> takes;
    for (size_t i = 0; i < files.size(); ++i)
    {
        cc::TakeForScoring t;
        t.key = i + 1;
        t.features = &features[i];
        t.words = &heard[i].words;
        const auto it = up.takes.find (t.key);
        t.places = it != up.takes.end() ? &it->second : nullptr;
        takes.push_back (t);
    }
    const auto scores = cc::scoreLines (lyrics, takes);
    std::printf ("\nStyle: %s\n", style.c_str());
    for (auto& line : scores)
    {
        std::printf ("\n%d. %s\n", line.line + 1, lyrics.lines[(size_t) line.line].text.c_str());
        const auto best = cc::bestTake (line, weights, takes);
        for (auto& s : line.takes)
        {
            auto show = [] (float v) { return v < 0 ? std::string ("  -") : std::to_string ((int) std::lround (v)); };
            std::printf ("  %sT%llu  pitch %3s  timing %3s  clarity %3s  energy %3s  = %3.0f\n", s.take == best ? "*" : " ",
                         (unsigned long long) s.take, show (s.pitch).c_str(), show (s.timing).c_str(), show (s.clarity).c_str(),
                         show (s.energy).c_str(), cc::totalFor (s, weights));
            for (auto& r : s.good)
                std::printf ("        + %s\n", r.c_str());
            for (auto& r : s.bad)
                std::printf ("        - %s\n", r.c_str());
            for (auto& r : s.problems)
                std::printf ("        ! %s\n", r.c_str());
        }
    }
    return 0;
}
