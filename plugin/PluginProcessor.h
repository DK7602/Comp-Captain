#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "cc/PeakMeter.h"
#include "cc/UiState.h"

#include <mutex>

// M0: passes audio through untouched (bit for bit) and remembers the window's look and page.
class CompCaptainProcessor final : public juce::AudioProcessor
{
public:
    CompCaptainProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Window side (message thread).
    cc::UiState getUiState() const;
    void setUiState (const cc::UiState&);
    cc::PeakMeter& getMeter() noexcept { return meter; }

private:
    cc::PeakMeter meter;

    mutable std::mutex stateLock; // never touched by the audio thread
    cc::UiState uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompCaptainProcessor)
};
