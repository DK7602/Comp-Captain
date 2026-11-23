// ARA: Comp Captain as a Cubase "Extension" (Audio > Extensions > Comp Captain). The DAW hands over the
// takes on a track's lanes; we list them as takes (on their real bars, from the song's tempo and bar map),
// play them back exactly as they are, and keep a plain report of what the DAW sent (the M1 proof).
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "cc/Overview.h"
#include "cc/Take.h"

#include <map>
#include <optional>
#include <vector>

#if JucePlugin_Enable_ARA

// One take on the DAW's lanes (an ARA playback region).
struct AraTake
{
    juce::String key;              // "a<number>", stable while the plug-in is open
    cc::Take take;                 // name, role, heart, where it sits, its length
    const void* region = nullptr;  // identity only (to see which copy of Comp Captain plays it)
    juce::String trackName;        // the DAW track (region sequence)
    int trackOrder = 0;
    juce::String fileName;         // the audio file (audio source)
    juce::String modificationId;
    double sourceRate = 0.0;
    int sourceChannels = 0;
    bool samplesReady = false;     // the DAW lets us read the audio
    bool tempoFromHost = false;    // positions came from the song's tempo map
};

class CompCaptainAraDocument final : public juce::ARADocumentControllerSpecialisation,
                                     private juce::Timer
{
public:
    CompCaptainAraDocument (const ARA::PlugIn::PlugInEntry* entry, const ARA::ARADocumentControllerHostInstance* instance);
    ~CompCaptainAraDocument() override;

    // Message thread.
    std::vector<AraTake> takes() const { return snapshot; }
    juce::uint32 version() const noexcept { return snapshotVersion; }
    juce::String report (const std::vector<const void*>& regionsThisCopyPlays, const juce::String& rolesThisCopy) const;
    bool setRole (const juce::String& key, cc::Role);
    bool setFavourite (const juce::String& key, bool);
    const std::vector<std::uint8_t>* peaks (const juce::String& key) const;

    // Audio thread (the playback renderer).
    juce::ScopedTryReadLock tryProcessingLock() { return juce::ScopedTryReadLock { processLock }; }
    std::atomic<bool> rateMismatch { false };

protected:
    void willBeginEditing (juce::ARADocument*) override;
    void didEndEditing (juce::ARADocument*) override;
    void didNotifyModelUpdates (juce::ARADocument*) override;
    void doUpdateMusicalContextContent (juce::ARAMusicalContext*, juce::ARAContentUpdateScopes) override;
    void didEnableAudioSourceSamplesAccess (juce::ARAAudioSource*, bool) override;
    void willDestroyAudioSource (juce::ARAAudioSource*) override;

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    bool doRestoreObjectsFromStream (juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override;
    bool doStoreObjectsToStream (juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override;

private:
    void timerCallback() override;
    void rebuild();
    void tellHostToSave();
    void readSomeAudio();

    struct Prefs
    {
        cc::Role role = cc::Role::lead;
        bool roleChosen = false;
        bool favourite = false;
    };
    std::map<std::string, Prefs> prefs; // by audio modification (survives saving, via the DAW's ARA archive)

    // Waveform pictures, read a little at a time on the message thread (only while the DAW allows it).
    struct SourceOverview
    {
        std::unique_ptr<juce::ARAAudioSourceReader> reader;
        cc::OverviewBuilder builder;
        std::vector<std::uint8_t> peaks;
        juce::int64 position = 0;
        bool done = false;
    };
    std::map<juce::ARAAudioSource*, std::unique_ptr<SourceOverview>> overviews;
    std::vector<std::unique_ptr<SourceOverview>> retired; // deleted on the next timer tick, not inside a callback
    std::map<juce::String, juce::ARAAudioSource*> sourceOfKey;

    std::vector<AraTake> snapshot;
    std::map<const void*, juce::String> regionKeys;
    int nextKey = 1;
    juce::uint32 snapshotVersion = 1;
    bool dirty = true;
    int tempoEntries = 0, barSignatures = 0;
    juce::String documentName;

    juce::ReadWriteLock processLock;
};

#endif
