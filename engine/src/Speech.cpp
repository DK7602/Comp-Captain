#include "cc/Speech.h"

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
        ~Impl()
        {
            if (ctx != nullptr)
                whisper_free (ctx);
        }
    };

    SpeechRecognizer::SpeechRecognizer() : impl (std::make_unique<Impl>()) {}
    SpeechRecognizer::~SpeechRecognizer() = default;

    bool SpeechRecognizer::isLoaded() const noexcept { return impl->ctx != nullptr; }

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
}
