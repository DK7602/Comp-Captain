#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "AraSupport.h"
#include "cc/Capture.h"
#include "cc/PeakMeter.h"
#include "cc/Take.h"
#include "cc/UiState.h"

#include <deque>
#include <functional>
#include <map>
#include <mutex>

// The plug-in. On its track it passes audio through untouched (bit for bit). M1 adds:
//  - CAPTURE: while armed and the DAW records, every pass becomes a take (written by a background
//    thread into Documents/Comp Captain/<track folder>), stamped with its bar.
//  - Takes from files (WAV, AIFF, FLAC, MP3), placed at a bar.
//  - The take list (names, roles, hearts), saved with the project.
//  - ARA: as a Cubase Extension it lists and plays the takes on the track's lanes.
class CompCaptainProcessor final : public juce::AudioProcessor,
#if JucePlugin_Enable_ARA
                                   public juce::AudioProcessorARAExtension,
#endif
                                   private juce::Timer,
                                   private juce::TimeSliceClient
{
public:
    CompCaptainProcessor();
    ~CompCaptainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void updateTrackProperties (const TrackProperties&) override;

#if JucePlugin_Enable_ARA
    juce::AudioProcessorARAExtension* getARAClientExtensions() override { return this; }
    CompCaptainAraDocument* araDocument() const;
    std::vector<const void*> araRegionsPlayedHere() const;
    juce::String araRoles() const;
#endif
    bool isAra() const;

    // ---------------------------------------------------------------- window side (message thread)
    cc::UiState getUiState() const;
    void setUiState (const cc::UiState&);
    cc::PeakMeter& getMeter() noexcept { return meter; }

    juce::uint64 instanceId() const noexcept { return uniqueId; }

    // CAPTURE
    void setArmed (bool);
    bool isArmed() const;
    void setLoopTakes (bool);
    bool loopTakes() const;
    bool isCapturing() const noexcept { return feed.isCapturing(); }
    cc::CaptureWriter::Live liveTake (size_t peaksFrom) const { return writer.live (peaksFrom); }

    // The track this copy sits on
    juce::String trackName() const;
    cc::Role trackRole() const;
    void setTrackRole (cc::Role);

    // Takes
    cc::TakeList getTakes() const;
    juce::uint32 takesVersion() const noexcept { return takesChanged.load(); }
    bool renameTake (std::uint32_t id, const juce::String& name);
    bool setTakeRole (std::uint32_t id, cc::Role);
    bool setTakeFavourite (std::uint32_t id, bool);
    bool removeTake (std::uint32_t id);
    bool takeFileMissing (std::uint32_t id) const;
    std::optional<std::vector<std::uint8_t>> takeOverview (std::uint32_t id) const;
    juce::File takesFolder();          // made up (not created) the first time it's needed
    juce::File takeFile (const cc::Take&) const;

    struct Placement
    {
        int bar = 1;
        double bpm = 120.0;
        cc::Meter meter;
    };
    struct ImportRequest
    {
        juce::File file;
        juce::String name;                // shown in the list ("" = the file's name)
        std::optional<cc::Role> role;     // none = guess from the name, else the track's role
        bool deleteWhenDone = false;      // a copy the window uploaded
    };
    void importFiles (std::vector<ImportRequest>, Placement);
    int importsWaiting() const noexcept { return importsPending.load(); }

    // What the DAW's transport is doing (for the bar ruler and BPM read-out).
    struct TransportView
    {
        bool known = false, playing = false, recording = false;
        double ppq = 0.0, bpm = 120.0, seconds = 0.0;
        cc::Meter meter;
    };
    TransportView transport() const;

    // Short messages for the window ("Take 3 saved: starts at bar 9").
    struct News
    {
        juce::String kind; // "saved", "imported", "discarded", "failed", "info"
        juce::String text;
    };
    std::vector<News> takeNews();

    // Every Comp Captain in this project, for the track picker.
    struct TrackInfo
    {
        juce::uint64 id = 0;
        juce::String name;
        cc::Role role = cc::Role::lead;
        int takes = 0;
        bool ara = false;
    };
    static std::vector<TrackInfo> allTracks();
    // Runs fn on the copy with this id (message thread), if it still exists.
    static bool withTrack (juce::uint64 id, const std::function<void (CompCaptainProcessor&)>& fn);

    // Tests and power users can move the takes folder (default: Documents/Comp Captain).
    static juce::File takesRoot();

private:
    void timerCallback() override;
    int useTimeSlice() override;
    void collectNews();
    void updateCaptureDestination();
    void scheduleOverviews();
    void changed (bool markProjectDirty = true);
    void addNews (const juce::String& kind, const juce::String& text);

    cc::PeakMeter meter;
    juce::AudioParameterBool* captureParam = nullptr;

    cc::CaptureFeed feed;
    cc::CaptureWriter writer { feed };
    std::atomic<int> nextTakeNumber { 1 };
    struct WriterThread;
    juce::SharedResourcePointer<WriterThread> writerThread;
    bool writerRegistered = false;
    juce::String destinationKey;

    struct Mailbox;                        // where background jobs leave their results
    std::shared_ptr<Mailbox> mailbox;
    struct JobPool;
    juce::SharedResourcePointer<JobPool> jobs;
    std::atomic<int> importsPending { 0 };

    mutable std::mutex stateLock;          // never touched by the audio thread
    cc::UiState uiState;
    cc::TakeList takes;
    std::map<std::uint32_t, std::vector<std::uint8_t>> overviews;
    std::map<std::uint32_t, bool> missing;
    std::map<std::uint32_t, bool> overviewAsked;
    juce::String track;
    cc::Role roleOfTrack = cc::Role::lead;
    bool roleOfTrackChosen = false;
    juce::int64 seed = 0;
    std::deque<News> news;

    std::atomic<juce::uint32> takesChanged { 1 };

    // transport, written by the audio thread
    std::atomic<bool> tKnown { false }, tPlaying { false }, tRecording { false };
    std::atomic<double> tPpq { 0.0 }, tBpm { 120.0 }, tSeconds { 0.0 };
    std::atomic<int> tNum { 4 }, tDen { 4 };

    const juce::uint64 uniqueId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompCaptainProcessor)
};
