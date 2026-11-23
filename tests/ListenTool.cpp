// cc_listen <model.bin> <take.wav> [lyrics.txt]: prints every word speech recognition hears, with its time.
// A tool for trying the recognizer on real takes; not part of the plug-in.
#include "cc/Resample.h"
#include "cc/Speech.h"
#include "cc/WavFile.h"

#include <chrono>
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
    const auto wav = cc::readWavFile (cc::pathFromUtf8 (argv[2]));
    if (! wav)
    {
        std::cout << "Can't read " << argv[2] << std::endl;
        return 1;
    }
    const auto audio = cc::resample (cc::toMono (wav->channels), wav->info.sampleRate, cc::SpeechRecognizer::sampleRate);
    cc::SpeechRecognizer::Options o;
    o.threads = (int) std::max (1u, std::thread::hardware_concurrency() / 2);
    if (argc > 3)
    {
        std::ifstream in (cc::pathFromUtf8 (argv[3]));
        std::stringstream ss;
        ss << in.rdbuf();
        o.prompt = ss.str();
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto words = sr.listen (audio.data(), audio.size(), o);
    const auto secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (! words)
    {
        std::cout << "Nothing heard." << std::endl;
        return 1;
    }
    for (auto& w : *words)
        std::cout << w.start << "\t" << w.end << "\t" << w.probability << "\t" << w.text << std::endl;
    std::cout << "(" << words->size() << " words from " << audio.size() / 16000.0 << " s of audio in " << secs << " s)" << std::endl;
    return 0;
}
