#pragma once

#include "PluginProcessor.h"
#include "UiResources.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <map>

// The window: a web view showing the packed interface (ui/), resizable, keeping the art's 16:9 shape.
// It passes takes, waveforms and CAPTURE news to the interface, and the interface's clicks back.
class CompCaptainEditor final : public juce::AudioProcessorEditor,
#if JucePlugin_Enable_ARA
                                public juce::AudioProcessorEditorARAExtension,
#endif
                                private juce::Timer
{
public:
    explicit CompCaptainEditor (CompCaptainProcessor&);
    ~CompCaptainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

#if JucePlugin_Enable_ARA
    juce::AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }
#endif

    bool isInterfaceReady() const noexcept { return uiReady; }

private:
    // As a DAW insert the window keeps the art's 16:9 shape. Opened as an ARA Extension (Cubase shows it in the
    // project window's lower zone, wide and short) it takes whatever size it's given, and the page fits itself in.
    void applySizeRules();
    bool embedded = false;
    void timerCallback() override;
    void handleUiEvent (const juce::var& message);
    void sendInit();
    void send (const juce::String& type, juce::DynamicObject* data);
    void updateState (const std::function<void (cc::UiState&)>& change);
    juce::WebBrowserComponent::Options makeOptions();

    // what the interface is shown
    void sendTakes();
    void sendPeaks (const juce::var& ids);
    void sendTracks (bool force);
    void sendTransport (bool force);
    void sendCapture (bool force);
    void sendNews();
    void takeAction (const juce::var& message);

    // lyrics (shared by the whole song) and listening
    void sendLyrics (bool force);
    void sendListening (bool force);
    void openLyricsFile (bool songRider);
    void lyricsFromFile (const juce::String& name, const juce::MemoryBlock& bytes);
    void hearIt (const juce::var& message);
    juce::String listeningReport();

    // adding files
    void chooseFiles();
    void importChosen (const juce::var& message);
    void uploadBegin (const juce::var& message);
    void uploadChunk (const juce::var& message);
    void uploadEnd (const juce::var& message);
    void uploadCancel (const juce::var& message);

    // the copy of Comp Captain whose takes are shown (0 = this one)
    juce::uint64 viewing = 0;
    template <typename Fn> bool withViewed (Fn&& fn);

    CompCaptainProcessor& processor;
    UiResources resources;
    juce::WebBrowserComponent browser;

    bool uiReady = false;
    float lastSentPeak = 0.0f;
    int tick = 0;

    juce::uint32 sentTakesVersion = 0, sentAraVersion = 0, sentHearingVersion = 0;
    juce::uint64 sentSongVersion = 0;
    juce::String sentListening;
    juce::String sentTracks, sentTransport, sentCapture;
    juce::uint32 liveSerial = 0;
    size_t liveSent = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    std::map<int, juce::Array<juce::File>> chosen;
    int nextToken = 1;

    struct Upload
    {
        juce::File file;
        std::unique_ptr<juce::FileOutputStream> out;
        juce::String name;
        juce::int64 size = 0, received = 0;
    };
    std::map<int, Upload> uploads;
    int nextUpload = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompCaptainEditor)
};
