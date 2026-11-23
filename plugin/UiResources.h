#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <mutex>
#include <optional>

// Serves the packed interface (ui/ zipped into the plug-in) to the web view.
class UiResources
{
public:
    UiResources();

    // url is what the web view asks for, e.g. "/" or "/css/app.css?x=1".
    std::optional<juce::WebBrowserComponent::Resource> get (const juce::String& url) const;

    static juce::String mimeTypeFor (const juce::String& path);
    int numFiles() const;

private:
    juce::MemoryInputStream zipData;
    std::unique_ptr<juce::ZipFile> zip;
    mutable std::mutex lock; // the web view may ask from more than one thread
};
