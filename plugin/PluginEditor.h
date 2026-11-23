#pragma once

#include "PluginProcessor.h"
#include "UiResources.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

// The window: a web view showing the packed interface (ui/), resizable, keeping the art's 16:9 shape.
class CompCaptainEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit CompCaptainEditor (CompCaptainProcessor&);
    ~CompCaptainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterfaceReady() const noexcept { return uiReady; }

private:
    void timerCallback() override;
    void handleUiEvent (const juce::var& message);
    void sendInit();
    void updateState (const std::function<void (cc::UiState&)>& change);
    juce::WebBrowserComponent::Options makeOptions();

    CompCaptainProcessor& processor;
    UiResources resources;
    juce::WebBrowserComponent browser;

    bool uiReady = false;
    float lastSentPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompCaptainEditor)
};
