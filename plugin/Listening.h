#pragma once

#include <juce_core/juce_core.h>

#include "cc/Features.h"
#include "cc/Words.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

// Background listening: one thread for the whole DAW session works through every track's takes, one take
// at a time, reading each take's file, finding its phrases and hearing its words (speech recognition on
// this computer; nothing is sent anywhere), and measuring what it sounds like every 10 ms for the scores.
// Both are kept next to the take ("Take 3.words", "Take 3.feat") so a take is only listened to once. It never touches the audio thread, uses at most half the computer's cores
// (four at most), and waits while any Comp Captain is recording a take.
class Listening final : private juce::Thread
{
public:
    Listening();
    ~Listening() override;

    struct Result
    {
        std::uint32_t takeId = 0;
        std::filesystem::path file;  // the take's file (a take reloaded with the project may have a new one)
        bool ok = false;
        cc::Hearing hearing;
        std::shared_ptr<const cc::TakeFeatures> features; // what it sounds like every 10 ms (for the scores)
        juce::String message; // why it didn't work, in plain words
    };

    struct Job
    {
        const void* owner = nullptr;                 // the Comp Captain asking (to cancel its jobs)
        std::uint32_t takeId = 0;
        std::filesystem::path file;                  // the take's WAV
        juce::String name;                           // "Take 3 (Lead Vox)", for the progress line
        bool again = false;                          // listen again even if it was heard before
        std::function<void (Result&&)> done;         // called on the listening thread
    };

    void add (Job);
    void cancelFor (const void* owner);              // forget its waiting jobs and stop its current one

    struct Status
    {
        juce::String now;                            // the take being listened to ("" = none)
        int percent = 0;
        int waiting = 0;
        bool pausedForRecording = false;
        juce::String problem;                        // set if speech can't run here (no model, old processor)
    };
    Status status() const;

    // Where the speech model is looked for (the plug-in's bundle first). Empty file if it isn't anywhere.
    static juce::File findModel();
    static constexpr const char* modelFileName = "ggml-base.en-q5_1.bin";

    // Each Comp Captain says when it starts and stops capturing a take (listening waits meanwhile).
    void recording (bool started) { recordingCount.fetch_add (started ? 1 : -1); }

private:
    void run() override;
    bool prepare();                                  // loads the model the first time it's needed

    struct Impl;
    std::unique_ptr<Impl> impl;

    mutable std::mutex lock;
    std::deque<Job> jobs;
    Status state;
    const void* currentOwner = nullptr;
    std::atomic<bool> stopCurrent { false };
    std::atomic<int> recordingCount { 0 };

    JUCE_DECLARE_NON_COPYABLE (Listening)
};
