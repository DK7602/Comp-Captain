#include "PluginEditor.h"

#include "cc/SuiteDetector.h"

namespace
{
    const juce::Identifier eventId { "cc" };
    const juce::Colour backgroundNavy { 0xff040b1c };

    juce::String hostDescription (juce::AudioProcessor::WrapperType wrapper)
    {
        const juce::PluginHostType host;
        juce::String format = juce::AudioProcessor::getWrapperTypeDescription (wrapper);
        if (wrapper == juce::AudioProcessor::wrapperType_Standalone)
            return "Standalone app";
        return juce::String (host.getHostDescription()) + " (" + format + ")";
    }
}

CompCaptainEditor::CompCaptainEditor (CompCaptainProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      browser (makeOptions())
{
    addAndMakeVisible (browser);

    const auto state = processor.getUiState();
    setResizable (true, true);
    setResizeLimits (cc::UiState::artWidth / 2, (int) std::lround (cc::UiState::artHeight * cc::UiState::minScale),
                     (int) std::lround (cc::UiState::artWidth * cc::UiState::maxScale), (int) std::lround (cc::UiState::artHeight * cc::UiState::maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) cc::UiState::artWidth / (double) cc::UiState::artHeight);
    setSize (state.windowWidth(), state.windowHeight());

    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

CompCaptainEditor::~CompCaptainEditor()
{
    stopTimer();
}

juce::WebBrowserComponent::Options CompCaptainEditor::makeOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    return Options {}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (Options::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CompCaptainWebView2"))
                                     .withBackgroundColour (backgroundNavy)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withEventListener (eventId, [this] (const juce::var& message) { handleUiEvent (message); })
        .withResourceProvider ([this] (const juce::String& url) { return resources.get (url); });
}

void CompCaptainEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundNavy); // shows for a moment while the interface loads
}

void CompCaptainEditor::resized()
{
    browser.setBounds (getLocalBounds());
    const auto scale = cc::clampScale ((double) getWidth() / (double) cc::UiState::artWidth);
    updateState ([scale] (cc::UiState& s) { s.windowScale = scale; });
}

void CompCaptainEditor::updateState (const std::function<void (cc::UiState&)>& change)
{
    auto s = processor.getUiState();
    change (s);
    processor.setUiState (s);
}

void CompCaptainEditor::handleUiEvent (const juce::var& message)
{
    const auto type = message.getProperty ("type", {}).toString();
    const auto value = message.getProperty ("value", {}).toString().toStdString();

    if (type == "ready")
    {
        uiReady = true;
        sendInit();
    }
    else if (type == "theme")
    {
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.theme); });
    }
    else if (type == "page")
    {
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.page); });
    }
    else if (type == "compView")
    {
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.compView); });
    }
    else if (type == "size")
    {
        const auto scale = cc::clampScale ((double) message.getProperty ("scale", 0.8));
        setSize ((int) std::lround (cc::UiState::artWidth * scale), (int) std::lround (cc::UiState::artHeight * scale));
    }
    // "arm" (CAPTURE) arrives with M1.
}

void CompCaptainEditor::sendInit()
{
    const auto state = processor.getUiState();
    const auto suite = cc::detectSuiteOnThisComputer();

    auto* sonicology = new juce::DynamicObject();
    sonicology->setProperty ("voxology", juce::String (std::string (cc::toName (suite.voxology))));
    sonicology->setProperty ("mixology", juce::String (std::string (cc::toName (suite.mixology))));

    auto* init = new juce::DynamicObject();
    init->setProperty ("type", "init");
    init->setProperty ("theme", juce::String (std::string (cc::toName (state.theme))));
    init->setProperty ("page", juce::String (std::string (cc::toName (state.page))));
    init->setProperty ("compView", juce::String (std::string (cc::toName (state.compView))));
    init->setProperty ("version", JucePlugin_VersionString);
    init->setProperty ("host", hostDescription (processor.wrapperType));
    init->setProperty ("sonicology", juce::var (sonicology));

    browser.emitEventIfBrowserIsVisible (eventId, juce::var (init));
}

void CompCaptainEditor::timerCallback()
{
    const float peak = processor.getMeter().readAndReset();
    if (! uiReady)
        return;

    // Only talk to the interface when there's sound (or to settle back to silence),
    // so an idle window costs nothing in a big session.
    constexpr float quiet = 0.0005f;
    if (peak < quiet && lastSentPeak < quiet)
        return;

    auto* msg = new juce::DynamicObject();
    msg->setProperty ("type", "level");
    msg->setProperty ("peak", peak);
    browser.emitEventIfBrowserIsVisible (eventId, juce::var (msg));
    lastSentPeak = peak;
}
