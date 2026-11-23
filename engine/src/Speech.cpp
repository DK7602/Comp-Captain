#include "cc/Speech.h"

#include "cc/Resample.h"
#include "cc/WavFile.h"

#include <whisper.h>

#include <algorithm>
#include <cmath>

namespace cc
{
    namespace
    {
        void quiet (enum ggml_log_level, const char*, void*) {} // whisper's own messages stay out of the DAW's log
    }

    struct SpeechRecognizer::Impl
    {
        whisper_context* ctx = nullptr;
        std::string name;
        ~Impl()
        {
            if (ctx != nullptr)
                whisper_free (ctx);
        }
    };

    SpeechRecognizer::SpeechRecognizer() : impl (std::make_unique<Impl>()) {}
    SpeechRecognizer::~SpeechRecognizer() = default;

    bool SpeechRecognizer::isLoaded() const noexcept { return impl->ctx != nullptr; }
    const std::string& SpeechRecognizer::modelName() const noexcept { return impl->name; }

    bool SpeechRecognizer::load (const std::filesystem::path& model, std::string& error)
    {
        whisper_log_set (quiet, nullptr);
        std::error_code ec;
        if (! std::filesystem::is_regular_file (model, ec))
        {
            error = "The speech model is missing: " + utf8FromPath (model);
            return false;
        }
        auto params = whisper_context_default_params();
        params.use_gpu = false;
        // whisper opens the file itself; on Windows a narrow path can't hold every name, so it gets the
        // short UTF-8 form, which whisper.cpp's file reader accepts on all three systems.
        auto* ctx = whisper_init_from_file_with_params (utf8FromPath (model).c_str(), params);
        if (ctx == nullptr)
        {
            error = "The speech model couldn't be opened (it may be damaged): " + utf8FromPath (model);
            return false;
        }
        if (impl->ctx != nullptr)
            whisper_free (impl->ctx);
        impl->ctx = ctx;
        impl->name = utf8FromPath (model.stem());
        if (impl->name.rfind ("ggml-", 0) == 0)
            impl->name = impl->name.substr (5);
        return true;
    }

    std::optional<std::vector<HeardWord>> SpeechRecognizer::listen (const float* audio, size_t numSamples, const Options& options)
    {
        if (impl->ctx == nullptr || audio == nullptr || numSamples == 0)
            return std::nullopt;

        auto p = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
        p.n_threads = std::max (1, options.threads);
        p.language = "en";
        p.translate = false;
        p.no_context = true;
        p.print_progress = false;
        p.print_realtime = false;
        p.print_timestamps = false;
        p.print_special = false;
        p.suppress_nst = true;
        // one word per segment, with its own start and end
        p.token_timestamps = true;
        p.max_len = 1;
        p.split_on_word = true;
        if (! options.prompt.empty())
            p.initial_prompt = options.prompt.c_str();

        struct Callbacks
        {
            const Options* options;
        } cb { &options };
        p.abort_callback = [] (void* user) -> bool {
            auto* c = static_cast<Callbacks*> (user);
            return c->options->cancel != nullptr && c->options->cancel->load();
        };
        p.abort_callback_user_data = &cb;
        if (options.progress)
        {
            p.progress_callback = [] (whisper_context*, whisper_state*, int percent, void* user) {
                static_cast<Callbacks*> (user)->options->progress (percent);
            };
            p.progress_callback_user_data = &cb;
        }

        if (whisper_full (impl->ctx, p, audio, (int) std::min<size_t> (numSamples, 0x7fffffff)) != 0)
            return std::nullopt;
        if (options.cancel != nullptr && options.cancel->load())
            return std::nullopt;

        std::vector<HeardWord> words;
        const int segments = whisper_full_n_segments (impl->ctx);
        for (int s = 0; s < segments; ++s)
        {
            const char* text = whisper_full_get_segment_text (impl->ctx, s);
            if (text == nullptr)
                continue;
            HeardWord w;
            w.text = text;
            bool hasLetters = false;
            for (char c : w.text)
                hasLetters = hasLetters || std::isalnum ((unsigned char) c) != 0;
            if (! hasLetters)
                continue; // plain punctuation segments
            w.start = (double) whisper_full_get_segment_t0 (impl->ctx, s) / 100.0;
            w.end = (double) whisper_full_get_segment_t1 (impl->ctx, s) / 100.0;
            const int tokens = whisper_full_n_tokens (impl->ctx, s);
            float sum = 0.0f;
            int counted = 0;
            for (int t = 0; t < tokens; ++t)
            {
                const auto data = whisper_full_get_token_data (impl->ctx, s, t);
                if (data.id >= whisper_token_eot (impl->ctx))
                    continue; // timestamps and other special tokens
                sum += data.p;
                ++counted;
            }
            w.probability = counted > 0 ? sum / (float) counted : 0.0f;
            words.push_back (std::move (w));
        }
        return words;
    }

    std::vector<SungPart> sungParts (const std::vector<Phrase>& phrases, double takeLength)
    {
        // phrases less than a second apart stay together (a breath inside a line), with a little room
        // either side so first and last sounds aren't clipped
        constexpr double joinBelow = 1.0, room = 0.25;
        std::vector<SungPart> out;
        double at = 0.0;
        for (auto& p : phrases)
        {
            const double from = std::max (0.0, p.start - room), to = std::min (takeLength, p.end + room);
            if (! out.empty() && from - out.back().to < joinBelow)
            {
                out.back().to = std::max (out.back().to, to);
                continue;
            }
            if (! out.empty())
                at = out.back().at + (out.back().to - out.back().from) + partGap;
            out.push_back ({ from, to, at });
        }
        return out;
    }

    double takeTime (const std::vector<SungPart>& parts, double t)
    {
        // a time in the joined audio -> the same moment in the take (inside the pause after a part: its end)
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto& p = parts[i];
            const double len = p.to - p.from;
            const bool last = i + 1 == parts.size();
            if (t < p.at + len + partGap || last)
                return p.from + std::clamp (t - p.at, 0.0, len);
        }
        return t;
    }

    std::vector<HeardWord> keepWordsInPhrases (const std::vector<HeardWord>& words, const std::vector<Phrase>& phrases)
    {
        if (phrases.empty())
            return words;
        constexpr double slack = 0.35; // word times are only good to a few hundredths; breaths are quiet
        std::vector<HeardWord> out;
        for (auto& w : words)
        {
            const bool inside = std::any_of (phrases.begin(), phrases.end(), [&] (const Phrase& p) {
                return w.end > p.start - slack && w.start < p.end + slack;
            });
            if (inside)
                out.push_back (w);
        }
        return out;
    }

    std::optional<Hearing> hearTake (SpeechRecognizer& sr, const std::filesystem::path& wav,
                                     const SpeechRecognizer::Options& options, std::string& error)
    {
        if (! sr.isLoaded())
        {
            error = "The speech model isn't loaded.";
            return std::nullopt;
        }
        WavReader reader;
        if (! reader.open (wav))
        {
            error = "The take's file couldn't be read: " + utf8FromPath (wav);
            return std::nullopt;
        }
        // the take in mono, a block at a time (a long stereo take never sits in memory twice)
        const auto& info = reader.info();
        const int channels = std::max (1, std::min (2, info.channels));
        std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (65536));
        std::vector<float*> ptrs;
        for (auto& c : block)
            ptrs.push_back (c.data());
        std::vector<float> mono;
        if (info.lengthFrames > 0)
            mono.reserve ((size_t) info.lengthFrames);
        int got = 0;
        while ((got = reader.read (ptrs.data(), channels, 65536)) > 0)
        {
            if (options.cancel != nullptr && options.cancel->load())
                return std::nullopt;
            for (int i = 0; i < got; ++i)
            {
                float v = 0.0f;
                for (int c = 0; c < channels; ++c)
                    v += block[(size_t) c][(size_t) i];
                mono.push_back (v / (float) channels);
            }
        }
        Hearing h;
        h.model = sr.modelName();
        if (mono.empty())
            return h;
        h.phrases = findPhrases (mono.data(), mono.size(), info.sampleRate);
        if (h.phrases.empty())
            return h; // nothing sung

        auto audio = resample (mono, info.sampleRate, SpeechRecognizer::sampleRate);
        std::vector<float>().swap (mono);

        // Only the sung parts are listened to, joined end to end with a short pause between them: long
        // silences (the bars before you come in, a break) throw the recognizer's word times off badly.
        const auto parts = sungParts (h.phrases, (double) audio.size() / SpeechRecognizer::sampleRate);
        std::vector<float> joined;
        for (auto& p : parts)
        {
            const auto a = (size_t) std::lround (p.from * SpeechRecognizer::sampleRate);
            const auto b = std::min (audio.size(), (size_t) std::lround (p.to * SpeechRecognizer::sampleRate));
            if (b > a)
                joined.insert (joined.end(), audio.begin() + (std::ptrdiff_t) a, audio.begin() + (std::ptrdiff_t) b);
            joined.insert (joined.end(), (size_t) std::lround (partGap * SpeechRecognizer::sampleRate), 0.0f);
        }
        std::vector<float>().swap (audio);

        auto words = sr.listen (joined.data(), joined.size(), options);
        if (! words.has_value())
        {
            error = (options.cancel != nullptr && options.cancel->load()) ? "Stopped." : "Listening didn't work on this take.";
            return std::nullopt;
        }
        for (auto& w : *words)
        {
            w.start = takeTime (parts, w.start);
            w.end = std::max (w.start, takeTime (parts, w.end));
        }
        h.words = keepWordsInPhrases (*words, h.phrases);
        return h;
    }
}
