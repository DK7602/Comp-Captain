#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Saved projects start with this line so we can recognise (and later upgrade) our own state.
    constexpr const char* stateHeader = "CompCaptainState\n";
}

CompCaptainProcessor::CompCaptainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CompCaptainProcessor::prepareToPlay (double, int)
{
    meter.readAndReset();
}

bool CompCaptainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, same in as out (it sits on a vocal or guitar track).
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void CompCaptainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The audio is never changed: input and output share the same buffer, so leaving it alone
    // is a bit-for-bit pass-through. We only glance at it for the meter and the logo glow.
    const int ins = getTotalNumInputChannels();
    meter.process (buffer.getArrayOfReadPointers(), juce::jmin (ins, buffer.getNumChannels()), buffer.getNumSamples());

    for (int ch = ins; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* CompCaptainProcessor::createEditor()
{
    return new CompCaptainEditor (*this);
}

void CompCaptainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = std::string (stateHeader) + getUiState().toText();
    dest.replaceAll (text.data(), text.size());
}

void CompCaptainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const std::string text (static_cast<const char*> (data), (size_t) sizeInBytes);
    if (text.rfind (stateHeader, 0) != 0)
        return; // not ours: keep the defaults
    setUiState (cc::UiState::fromText (std::string_view (text).substr (std::char_traits<char>::length (stateHeader))));
}

cc::UiState CompCaptainProcessor::getUiState() const
{
    const std::scoped_lock lock (stateLock);
    return uiState;
}

void CompCaptainProcessor::setUiState (const cc::UiState& s)
{
    const std::scoped_lock lock (stateLock);
    uiState = s;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompCaptainProcessor();
}
