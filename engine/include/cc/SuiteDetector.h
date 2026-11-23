// Comp Captain engine: is Voxology / Mixology (the Sonicology suite) installed?
// The Send buttons only show for what's installed; everyone else gets Drag to DAW and Render All.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cc
{
    enum class SuiteStatus
    {
        missing,   // not installed: no Send button
        installed, // installed, not in this project: clicking Send explains what to do
        linked     // open in this project: Send works directly (M6)
    };

    std::string_view toName (SuiteStatus);

    struct SuiteInfo
    {
        SuiteStatus voxology = SuiteStatus::missing;
        SuiteStatus mixology = SuiteStatus::missing;
    };

    enum class Platform { windows, macOS, linuxOS };

    // Where DAWs look for VST3 / AU plug-ins on each platform. getEnv returns an environment variable.
    std::vector<std::filesystem::path> pluginFolders (Platform, const std::function<std::optional<std::string> (const char*)>& getEnv);

    // Looks for "Voxology.vst3" / "Mixology.vst3" (and .component on Mac) in the given folders.
    SuiteInfo detectSuite (const std::vector<std::filesystem::path>& folders);

    // Both of the above for the computer we're running on.
    SuiteInfo detectSuiteOnThisComputer();
}
