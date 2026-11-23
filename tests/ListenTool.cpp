// cc_listen <model.bin> <take.wav> [lyrics.txt]: what the plug-in hears in a take (its phrases and every
// word, with times) and, given the lyrics, where each line lands. A tool for trying it on real takes;
// not part of the plug-in.
#include "cc/LineUp.h"
#include "cc/Speech.h"
#include "cc/TextFile.h"
#include "cc/WavFile.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: cc_listen <model.bin> <take.wav> [lyrics.txt]" << std::endl;
        return 2;
    }
    cc::SpeechRecognizer sr;
    std::string error;
    if (! sr.load (cc::pathFromUtf8 (argv[1]), error))
    {
        std::cout << error << std::endl;
        return 1;
    }
    cc::SpeechRecognizer::Options o;
    o.threads = (int) std::max (1u, std::thread::hardware_concurrency() / 2);
    const auto t0 = std::chrono::steady_clock::now();
    const auto heard = cc::hearTake (sr, cc::pathFromUtf8 (argv[2]), o, error);
    const auto secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (! heard)
    {
        std::cout << error << std::endl;
        return 1;
    }
    for (auto& w : heard->words)
        std::printf ("%7.2f %7.2f  %.2f %s\n", w.start, w.end, w.probability, w.text.c_str());
    std::printf ("(%zu words, %zu phrases, %.1f s to listen)\n", heard->words.size(), heard->phrases.size(), secs);

    if (argc > 3)
    {
        std::ifstream in (cc::pathFromUtf8 (argv[3]), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto lyrics = cc::Lyrics::parse (cc::textFromBytes (ss.str()));
        cc::TakeHeard t;
        t.key = 1;
        t.lengthSeconds = heard->phrases.empty() ? 0.0 : heard->phrases.back().end;
        t.words = &heard->words;
        const auto up = cc::lineUpSong (lyrics, { t });
        std::printf ("\n%zu lines, %zu sections:\n", lyrics.lines.size(), lyrics.sections.size());
        for (auto& l : up.lines)
        {
            const auto& line = lyrics.lines[(size_t) l.line];
            std::printf ("%7.2f %7.2f  %3d%%  %-12s %s\n", l.start, l.end, (int) (l.sure * 100.0f + 0.5f),
                         lyrics.sections[(size_t) line.section].name.c_str(), line.text.c_str());
        }
    }
    return 0;
}
