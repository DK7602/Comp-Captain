#include "UiResources.h"

#include "BinaryData.h"

#include <cstring>

UiResources::UiResources()
    : zipData (BinaryData::CompCaptainUi_zip, (size_t) BinaryData::CompCaptainUi_zipSize, false)
{
    zip = std::make_unique<juce::ZipFile> (zipData);
}

int UiResources::numFiles() const
{
    return zip != nullptr ? zip->getNumEntries() : 0;
}

juce::String UiResources::mimeTypeFor (const juce::String& path)
{
    const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "text/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> UiResources::get (const juce::String& url) const
{
    if (zip == nullptr)
        return std::nullopt;

    auto path = url.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    path = path.trimCharactersAtStart ("/");
    if (path.isEmpty())
        path = "index.html";
    if (path.contains (".."))
        return std::nullopt;

    const std::scoped_lock guard (lock);
    const auto* entry = zip->getEntry (path, true);
    if (entry == nullptr)
        return std::nullopt;

    std::unique_ptr<juce::InputStream> stream (zip->createStreamForEntry (*entry));
    if (stream == nullptr)
        return std::nullopt;

    juce::MemoryBlock block;
    stream->readIntoMemoryBlock (block);

    juce::WebBrowserComponent::Resource res;
    res.data.resize (block.getSize());
    std::memcpy (res.data.data(), block.getData(), block.getSize());
    res.mimeType = mimeTypeFor (path);
    return res;
}
