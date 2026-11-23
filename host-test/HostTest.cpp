// Host test: loads "Comp Captain.vst3" like a DAW and checks the M0 promises.
//   CompCaptainHostTest <path to Comp Captain.vst3> [--no-window]
// Exit code 0 = everything passed.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

    // Plays noise through the plug-in in blocks of changing size and checks it comes out bit for bit.
    bool passesAudioUntouched (juce::AudioPluginInstance& plugin, int channels)
    {
        juce::AudioPluginInstance::BusesLayout layout;
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        if (! plugin.setBusesLayout (layout))
            return false;

        plugin.setRateAndBufferSizeDetails (48000.0, 512);
        plugin.prepareToPlay (48000.0, 512);

        juce::Random random (1234);
        juce::MidiBuffer midi;
        bool identical = true;
        for (int block = 0; block < 300 && identical; ++block)
        {
            const int n = 1 + random.nextInt (512);
            juce::AudioBuffer<float> buffer (channels, n), original (channels, n);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);
            original.makeCopyOf (buffer);
            plugin.processBlock (buffer, midi);
            for (int c = 0; c < channels && identical; ++c)
                identical = std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * (size_t) n) == 0;
        }
        plugin.releaseResources();
        return identical;
    }

    // A DAW keeps the plug-in's saved data inside its own wrapper (XML with the component state in
    // base64). These unwrap and rewrap it, so we can read and change our own text inside.
    std::string componentState (juce::AudioPluginInstance& plugin)
    {
        juce::MemoryBlock host;
        plugin.getStateInformation (host);
        juce::MemoryBlock component;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize()))
            if (auto* c = xml->getChildByName ("IComponent"))
                component.fromBase64Encoding (c->getAllSubText());
        return { static_cast<const char*> (component.getData()), component.getSize() };
    }

    void setComponentState (juce::AudioPluginInstance& plugin, const std::string& component)
    {
        juce::MemoryBlock host;
        plugin.getStateInformation (host);
        auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize());
        if (xml == nullptr)
            return;
        if (auto* c = xml->getChildByName ("IComponent"))
        {
            c->deleteAllTextElements();
            c->addTextElement (juce::MemoryBlock (component.data(), component.size()).toBase64Encoding());
        }
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        plugin.setStateInformation (out.getData(), (int) out.getSize());
    }

    bool replace (std::string& s, const std::string& from, const std::string& to)
    {
        const auto at = s.find (from);
        if (at == std::string::npos)
            return false;
        s.replace (at, from.size(), to);
        return true;
    }

    bool has (const std::string& s, const char* what) { return s.find (what) != std::string::npos; }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: CompCaptainHostTest <path to Comp Captain.vst3> [--no-window]" << std::endl;
        return 2;
    }
    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const bool openWindow = ! (argc > 2 && juce::String (argv[2]) == "--no-window");

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "Comp Captain host test: " << pluginFile.getFullPathName() << std::endl;

    // 1. Scan
    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (found, pluginFile.getFullPathName());
    check (found.size() == 1, "scan finds exactly one plug-in");
    if (found.isEmpty())
        return 1;
    const auto desc = *found.getFirst();
    check (desc.name == "Comp Captain", "it is called Comp Captain (got \"" + desc.name + "\")");
    check (desc.manufacturerName == "DK7602", "made by DK7602");
    check (! desc.isInstrument, "it is an effect, not an instrument");

    // 2. Load
    juce::String error;
    auto plugin = formats.createPluginInstance (desc, 48000.0, 512, error);
    check (plugin != nullptr, "loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
    if (plugin == nullptr)
        return 1;
    check (plugin->getLatencySamples() == 0, "adds no delay");

    // 3. Play: untouched, stereo and mono
    check (passesAudioUntouched (*plugin, 2), "stereo audio passes through bit for bit");
    check (passesAudioUntouched (*plugin, 1), "mono audio passes through bit for bit");

    // 4. Save and restore
    auto saved = componentState (*plugin);
    check (saved.rfind ("CompCaptainState\n", 0) == 0 && has (saved, "page=comp") && has (saved, "theme=dark"),
           "saves its state with the project (opens on the Comp page, dark look)");

    auto changed = saved;
    const bool edited = replace (changed, "theme=dark", "theme=light") && replace (changed, "page=comp", "page=review")
                        && replace (changed, "compView=line", "compView=song") && replace (changed, "windowScale=0.80", "windowScale=1.00");
    check (edited, "found the look, page and window size in the saved state");

    auto second = formats.createPluginInstance (desc, 48000.0, 512, error);
    check (second != nullptr, "a second copy loads");
    if (second != nullptr)
    {
        setComponentState (*second, changed);
        const auto restored = componentState (*second);
        check (has (restored, "theme=light") && has (restored, "page=review") && has (restored, "compView=song") && has (restored, "windowScale=1.00"),
               "restores a saved project (light look, Review page, Song view, full size)");

        setComponentState (*second, "not our state at all");
        check (has (componentState (*second), "theme=light"), "ignores state that isn't ours");
    }

    // 5. Open the window (the web interface), at the saved size, and let it load
    if (openWindow)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (second != nullptr ? second->createEditorAndMakeActive() : nullptr);
        check (editor != nullptr, "opens its window");
        if (editor != nullptr)
        {
            check (std::abs (editor->getWidth() - 1672) <= 2 && std::abs (editor->getHeight() - 941) <= 2,
                   "window opens at the saved size (got " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) + ")");
            check (editor->isResizable(), "window can be resized");
            editor->setTopLeftPosition (40, 40);
            editor->addToDesktop (0);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5000);
            check (editor->isShowing(), "window stays open while the interface loads");
            editor.reset(); // the editor tells the plug-in it is going away
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        }
    }
    else
    {
        std::cout << "  SKIP  window (--no-window)" << std::endl;
    }

    second.reset();
    plugin.reset();

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
