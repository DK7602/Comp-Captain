#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "AraSupport.h"
#include "Listening.h"
#include "cc/Capture.h"
#include "cc/LineUp.h"
#include "cc/Scores.h"
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
// M2 adds the song's lyrics (shared by every copy in the project, saved with it), listening to each
// vocal take in the background (its phrases and words), and lining the lyrics up with the takes.
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

    // ---------------------------------------------------------------- the song (every copy shares it)
    // The lyrics belong to the song: every Comp Captain in the project shows and saves the same ones.
    static juce::String songLyrics();
    static void setSongLyrics (const juce::String& text); // typed, pasted or opened: every copy gets them

    struct Song
    {
        juce::uint64 version = 0;
        juce::String text;
        cc::Lyrics lyrics;
        cc::SongLineUp lineUp;
        std::map<std::uint64_t, juce::String> takeNames; // "Take 3 (Lead Vox)"
        int sungTakes = 0;                               // vocal takes in the song
        int heardTakes = 0;                              // ...that have been listened to
        struct Unmatched
        {
            juce::String take;                           // "Take 1 (Verse Vox)"
            int words = 0;
            juce::String heard;                          // its first words, as heard
        };
        std::vector<Unmatched> unmatched;                // takes listened to that didn't sing these lyrics
    };
    // The lyrics and where each line sits (message thread). Worked out again only when something changed.
    static std::shared_ptr<const Song> song();
    static Listening::Status listeningStatus();

    // What was heard in this track's takes ("" = not asked yet, "waiting", "done", "failed").
    juce::String takeHearingState (std::uint32_t id) const;
    std::optional<cc::Hearing> takeHearing (std::uint32_t id) const;
    juce::uint32 hearingVersion() const noexcept { return heardChanged.load(); }
    void listenAgain(); // forget what was heard in this track's takes and listen to them again

    // ---------------------------------------------------------------- scores (M3)
    // Every line of this track's takes, scored against each other in the background whenever the lyrics, the takes
    // or what was heard change. ready: the scores are for the song as it is now (not still being worked out).
    struct Scores
    {
        juce::uint64 songVersion = 0;
        std::vector<cc::LineScores> lines;
        std::map<std::uint64_t, std::uint32_t> takeOf; // score key -> take id on this track
    };
    std::shared_ptr<const Scores> scores() const;
    bool scoresReady() const;
    juce::uint32 scoresVersion() const noexcept { return scoresChanged.load(); }

    // The style (how much each score counts) and your own pick for each line, saved with the project.
    struct Pick
    {
        std::uint32_t take = 0;
        std::string lineText;  // the line it was picked for (a pick is dropped if the lyrics change under it)
    };
    std::string styleName() const;
    cc::ScoreWeights styleWeights() const;
    void setStyle (const std::string& name, const cc::ScoreWeights&);
    std::map<int, Pick> picks() const;
    void setPick (int line, std::uint32_t take, const std::string& lineText); // take 0 = no pick (Comp Captain's)
    void clearPicks();

private:
    void timerCallback() override;
    int useTimeSlice() override;
    void collectNews();
    void updateCaptureDestination();
    void scheduleOverviews();
    void changed (bool markProjectDirty = true);
    void addNews (const juce::String& kind, const juce::String& text);
    void scheduleListening();
    static void adoptSongLyrics (const std::string& text, juce::int64 stamp);

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

    // listening
    struct TakeHearing
    {
        bool asked = false, done = false, failed = false, again = false;
        cc::Hearing hearing;
        std::shared_ptr<const cc::TakeFeatures> features;
    };
    void scheduleScoring();
    std::shared_ptr<const Scores> scored;                // under stateLock
    juce::uint64 scoringFor = 0;                         // the song version being scored (0 = none)
    std::atomic<juce::uint32> scoresChanged { 1 };
    std::string style = "Singing";
    cc::ScoreWeights weights = *cc::styleWeights ("Singing");
    std::map<int, Pick> picked;
    juce::SharedResourcePointer<Listening> listening;
    std::map<std::uint32_t, TakeHearing> hearings;   // under stateLock
    std::atomic<juce::uint32> heardChanged { 1 };
    bool wasCapturing = false;
    std::atomic<juce::uint32> blocksPlayed { 0 };     // counted by the audio thread
    juce::uint32 blocksSeen = 0;
    int quietTicks = 0;                               // timer ticks with no audio blocks

    // transport, written by the audio thread
    std::atomic<bool> tKnown { false }, tPlaying { false }, tRecording { false };
    std::atomic<double> tPpq { 0.0 }, tBpm { 120.0 }, tSeconds { 0.0 };
    std::atomic<int> tNum { 4 }, tDen { 4 };

    const juce::uint64 uniqueId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompCaptainProcessor)
};
