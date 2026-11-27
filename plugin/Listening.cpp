#include "Listening.h"

#include "cc/WavFile.h"

#if CC_HAS_SPEECH
 #include "cc/Speech.h"
#endif

#include <algorithm>
#include <thread>

struct Listening::Impl
{
#if CC_HAS_SPEECH
    cc::SpeechRecognizer recognizer;
#endif
    bool tried = false;
    juce::String problem;
    std::string modelName;
};

Listening::Listening() : juce::Thread ("Comp Captain listening"), impl (std::make_unique<Impl>())
{
    startThread (juce::Thread::Priority::low);
}

Listening::~Listening()
{
    signalThreadShouldExit();
    stopCurrent.store (true);
    notify();
    stopThread (10000);
}

void Listening::add (Job job)
{
    {
        const std::scoped_lock l (lock);
        // the same take asked for twice: once is enough
        for (auto& j : jobs)
            if (j.owner == job.owner && j.takeId == job.takeId)
            {
                j.again = j.again || job.again;
                return;
            }
        jobs.push_back (std::move (job));
        state.waiting = (int) jobs.size();
    }
    notify();
}

void Listening::cancelFor (const void* owner)
{
    const std::scoped_lock l (lock);
    jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [owner] (const Job& j) { return j.owner == owner; }), jobs.end());
    state.waiting = (int) jobs.size();
    if (currentOwner == owner)
        stopCurrent.store (true);
}

Listening::Status Listening::status() const
{
    const std::scoped_lock l (lock);
    return state;
}

juce::File Listening::findModel()
{
    // tests and power users can point at a model file
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("COMP_CAPTAIN_SPEECH_MODEL", {}); env.isNotEmpty())
        if (juce::File f (env); f.existsAsFile())
            return f;

    // Inside the plug-in: <bundle>/Contents/Resources (VST3 on every system, AU, the Mac app). The
    // standalone app on Windows / Linux keeps it in a Resources folder next to it.
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> places { self.getParentDirectory().getSiblingFile ("Resources"),
                                     self.getParentDirectory().getChildFile ("Resources"),
                                     self.getParentDirectory() };

    // The standalone app also borrows the one inside the installed VST3.
    const juce::String vst3 = "Comp Captain.vst3/Contents/Resources";
#if JUCE_WINDOWS
    places.add (juce::File (juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files")).getChildFile ("VST3").getChildFile (vst3));
#elif JUCE_MAC
    places.add (juce::File ("/Library/Audio/Plug-Ins/VST3").getChildFile (vst3));
    places.add (juce::File ("~/Library/Audio/Plug-Ins/VST3").getChildFile (vst3));
#else
    places.add (juce::File ("~/.vst3").getChildFile (vst3));
    places.add (juce::File ("/usr/lib/vst3").getChildFile (vst3));
    places.add (juce::File ("/usr/local/lib/vst3").getChildFile (vst3));
#endif
    for (auto& p : places)
        if (auto f = p.getChildFile (modelFileName); f.existsAsFile())
            return f;
    return {};
}

bool Listening::prepare()
{
    if (impl->tried)
        return impl->problem.isEmpty();
    impl->tried = true;
#if ! CC_HAS_SPEECH
    impl->problem = "Speech recognition isn't part of this build.";
#else
 #if JUCE_INTEL
    // The speech code is built for processors from about 2013 on (AVX2). Older ones would crash.
    if (! juce::SystemStats::hasAVX2() || ! juce::SystemStats::hasFMA3())
        impl->problem = "This computer's processor is too old for lining up lyrics (it needs AVX2). Lyrics and sections still work.";
 #endif
    if (impl->problem.isEmpty())
    {
        const auto model = findModel();
        std::string error;
        if (model == juce::File())
            impl->problem = "The speech model isn't installed with the plug-in. Copy the whole \"Comp Captain.vst3\" folder again.";
        else if (! impl->recognizer.load (cc::pathFromUtf8 (model.getFullPathName().toStdString()), error))
            impl->problem = juce::String::fromUTF8 (error.c_str());
        else
            impl->modelName = impl->recognizer.modelName();
    }
#endif
    const std::scoped_lock l (lock);
    state.problem = impl->problem;
    return impl->problem.isEmpty();
}

void Listening::run()
{
    while (! threadShouldExit())
    {
        Job job;
        {
            std::unique_lock l (lock);
            if (jobs.empty() || recordingCount.load() > 0)
            {
                state.pausedForRecording = ! jobs.empty();
                l.unlock();
                wait (500);
                continue;
            }
            state.pausedForRecording = false;
            job = std::move (jobs.front());
            jobs.pop_front();
            state.waiting = (int) jobs.size();
            state.now = job.name;
            state.percent = 0;
            currentOwner = job.owner;
            stopCurrent.store (false);
        }

        Result r;
        r.takeId = job.takeId;
        r.file = job.file;
        // Heard before (and by this model): read it back, no model needed.
        const auto saved = job.again ? std::optional<cc::Hearing> {} : cc::loadHearing (job.file);
        const auto expected = juce::String (Listening::modelFileName).fromFirstOccurrenceOf ("ggml-", false, false).upToLastOccurrenceOf (".bin", false, false);
        if (saved.has_value() && juce::String (saved->model) == expected && saved->version == cc::hearingVersion)
        {
            r.ok = true;
            r.hearing = std::move (*saved);
            // measured before too? if not (listened to by an older version), measure it now: no model needed
            auto measured = job.again ? std::optional<cc::TakeFeatures> {} : cc::loadFeatures (job.file);
            if (! measured.has_value())
            {
                std::string error;
                measured = cc::measureTake (job.file, &stopCurrent, error);
                if (measured.has_value())
                    cc::saveFeatures (job.file, *measured);
            }
            if (measured.has_value())
                r.features = std::make_shared<const cc::TakeFeatures> (std::move (*measured));
        }
        else if (! prepare())
        {
            r.message = impl->problem;
        }
        else
        {
#if CC_HAS_SPEECH
            cc::SpeechRecognizer::Options o;
            o.threads = juce::jlimit (1, 4, (int) std::thread::hardware_concurrency() / 2);
            o.cancel = &stopCurrent;
            o.progress = [this] (int percent) {
                const std::scoped_lock l (lock);
                state.percent = percent;
            };
            std::string error;
            cc::TakeFeatures measured;
            if (auto h = cc::hearTake (impl->recognizer, job.file, o, error, &measured))
            {
                r.ok = true;
                r.hearing = std::move (*h);
                cc::saveHearing (job.file, r.hearing);
                if (! measured.empty())
                {
                    cc::saveFeatures (job.file, measured);
                    r.features = std::make_shared<const cc::TakeFeatures> (std::move (measured));
                }
            }
            else
            {
                r.message = juce::String::fromUTF8 (error.c_str());
            }
#endif
        }

        const bool stopped = stopCurrent.load();
        {
            const std::scoped_lock l (lock);
            state.now.clear();
            state.percent = 0;
            currentOwner = nullptr;
        }
        if (! stopped && job.done)
            job.done (std::move (r));
    }
}
