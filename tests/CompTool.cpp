// cc_comp <model.bin> <lyrics.txt> <out.wav> <take1.wav> <take2.wav> ... [--style Rap] [--alternate]
// Listens to each take (all starting at the same place in the song), lines the lyrics up, scores every line, picks
// the best take for each line (--alternate: take 1, 2, 3... line by line, a join at every line, the hardest case),
// joins them into one performance and writes it. Prints every piece and join, and checks each join for a click:
// the biggest step between two samples around the join, against the takes' own biggest steps there.
// A tool for trying the comp on real takes; not part of the plug-in.
#include "cc/Comp.h"
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

namespace
{
    double biggestStep (const std::vector<float>& x, double rate, double around, double halfWidth)
    {
        const auto from = (long long) ((around - halfWidth) * rate), to = (long long) ((around + halfWidth) * rate);
        double most = 0.0;
        for (long long i = std::max (1LL, from); i < to && i < (long long) x.size(); ++i)
            most = std::max (most, (double) std::abs (x[(size_t) i] - x[(size_t) i - 1]));
        return most;
    }
}

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::cout << "Usage: cc_comp <model.bin> <lyrics.txt> <out.wav> <take1.wav> [take2.wav ...] [--style Rap] [--alternate]" << std::endl;
        return 2;
    }
    std::string style = "Singing";
    bool alternate = false;
    std::vector<std::string> files;
    for (int i = 4; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--style" && i + 1 < argc)
            style = argv[++i];
        else if (a == "--alternate")
            alternate = true;
        else
            files.push_back (a);
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
    }
    const auto up = cc::lineUpSong (lyrics, lineUpInput);

    std::vector<cc::TakeForScoring> takes;
    std::vector<cc::CompSource> sources;
    std::vector<cc::CompFile> wavs;
    int channels = 1;
    double rate = 48000.0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        cc::TakeForScoring t;
        t.key = i + 1;
        t.features = &features[i];
        t.words = &heard[i].words;
        const auto it = up.takes.find (t.key);
        t.places = it != up.takes.end() ? &it->second : nullptr;
        takes.push_back (t);

        cc::WavReader r;
        if (r.open (cc::pathFromUtf8 (files[i])))
        {
            channels = std::max (channels, r.info().channels);
            if (i == 0)
                rate = r.info().sampleRate;
        }
        cc::CompSource s;
        s.key = t.key;
        s.lengthSeconds = (double) features[i].frames() * cc::TakeFeatures::hop;
        s.places = t.places;
        s.features = &features[i];
        sources.push_back (s);
        wavs.push_back ({ t.key, cc::pathFromUtf8 (files[i]), 0.0 });
    }
    const auto scores = cc::scoreLines (lyrics, takes);
    std::vector<cc::CompChoice> choices;
    for (auto& line : scores)
    {
        if (line.takes.empty())
            continue;
        std::uint64_t take = cc::bestTake (line, weights, takes);
        if (alternate)
            take = line.takes[(size_t) line.line % line.takes.size()].take;
        choices.push_back ({ line.line, take });
    }
    const auto plan = cc::planComp (choices, sources);
    if (plan.empty())
    {
        std::cout << "Nothing to comp." << std::endl;
        return 1;
    }
    std::printf ("Comp %.2f s to %.2f s, %zu pieces (%s%s)\n", plan.start, plan.end, plan.pieces.size(), style.c_str(), alternate ? ", alternating" : "");
    for (auto& p : plan.pieces)
        std::printf ("  T%llu  lines %d-%d  %.3f .. %.3f s  fade in %2.0f ms  gain %+.1f dB  join at %.0f dB\n", (unsigned long long) p.take,
                     p.firstLine + 1, p.lastLine + 1, p.from, p.to, p.fadeIn * 1000.0, p.gainDb, (double) p.joinLevelDb);

    const auto audio = cc::renderComp (plan, wavs, 0.0, plan.end + 0.5, rate, channels, nullptr, error);
    if (! audio)
    {
        std::cout << error << std::endl;
        return 1;
    }
    // clicks: the comp's biggest sample step around each join, against the two takes' own there
    int clicks = 0;
    for (size_t i = 1; i < plan.pieces.size(); ++i)
    {
        const auto& p = plan.pieces[i];
        const double comp = biggestStep (audio->channels[0], rate, p.from, 0.03);
        double own = 1e-6;
        for (auto key : { plan.pieces[i - 1].take, p.take })
            if (auto w = cc::readWavFile (wavs[(size_t) key - 1].wav))
            {
                const double g = std::pow (10.0, (key == p.take ? p.gainDb : plan.pieces[i - 1].gainDb) / 20.0);
                own = std::max (own, g * biggestStep (w->channels[0], rate, p.from, 0.03));
            }
        const bool click = comp > own * 1.5 + 0.002;
        clicks += click ? 1 : 0;
        std::printf ("  join %zu at %.3f s: biggest step %.4f (takes' own %.4f)%s\n", i, p.from, comp, own, click ? "  << CLICK?" : "");
    }
    cc::WavWriter w;
    if (! w.open (cc::pathFromUtf8 (argv[3]), rate, channels))
        return 1;
    std::vector<const float*> ptrs;
    for (auto& c : audio->channels)
        ptrs.push_back (c.data());
    w.write (ptrs.data(), (int) audio->frames());
    w.close();
    std::printf ("Wrote %s (%.1f s). %d possible clicks.\n", argv[3], (double) audio->frames() / rate, clicks);
    return clicks == 0 ? 0 : 3;
}
