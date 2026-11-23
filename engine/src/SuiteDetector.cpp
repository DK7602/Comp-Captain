#include "cc/SuiteDetector.h"

#include <cstdlib>
#include <system_error>

namespace cc
{
    std::string_view toName (SuiteStatus s)
    {
        switch (s)
        {
            case SuiteStatus::installed: return "installed";
            case SuiteStatus::linked: return "linked";
            case SuiteStatus::missing: break;
        }
        return "missing";
    }

    std::vector<std::filesystem::path> pluginFolders (Platform platform, const std::function<std::optional<std::string> (const char*)>& getEnv)
    {
        std::vector<std::filesystem::path> out;
        auto env = [&] (const char* name) -> std::optional<std::filesystem::path> {
            if (auto v = getEnv (name); v && ! v->empty())
                return std::filesystem::path (*v);
            return std::nullopt;
        };

        switch (platform)
        {
            case Platform::windows:
                if (auto p = env ("CommonProgramFiles")) out.push_back (*p / "VST3");
                if (auto p = env ("CommonProgramW6432")) out.push_back (*p / "VST3");
                if (auto p = env ("LOCALAPPDATA")) out.push_back (*p / "Programs" / "Common" / "VST3");
                break;
            case Platform::macOS:
                out.emplace_back ("/Library/Audio/Plug-Ins/VST3");
                out.emplace_back ("/Library/Audio/Plug-Ins/Components");
                if (auto p = env ("HOME"))
                {
                    out.push_back (*p / "Library/Audio/Plug-Ins/VST3");
                    out.push_back (*p / "Library/Audio/Plug-Ins/Components");
                }
                break;
            case Platform::linuxOS:
                if (auto p = env ("HOME")) out.push_back (*p / ".vst3");
                out.emplace_back ("/usr/lib/vst3");
                out.emplace_back ("/usr/local/lib/vst3");
                break;
        }
        return out;
    }

    SuiteInfo detectSuite (const std::vector<std::filesystem::path>& folders)
    {
        auto found = [&] (const std::string& name) {
            for (const auto& folder : folders)
            {
                for (const char* ext : { ".vst3", ".component" })
                {
                    std::error_code ec;
                    if (std::filesystem::exists (folder / (name + ext), ec))
                        return true;
                }
            }
            return false;
        };

        SuiteInfo info;
        info.voxology = found ("Voxology") ? SuiteStatus::installed : SuiteStatus::missing;
        info.mixology = found ("Mixology") ? SuiteStatus::installed : SuiteStatus::missing;
        return info;
    }

    SuiteInfo detectSuiteOnThisComputer()
    {
#if defined(_WIN32)
        constexpr auto platform = Platform::windows;
#elif defined(__APPLE__)
        constexpr auto platform = Platform::macOS;
#else
        constexpr auto platform = Platform::linuxOS;
#endif
        auto getEnv = [] (const char* name) -> std::optional<std::string> {
#if defined(_MSC_VER)
            char* buffer = nullptr;
            size_t size = 0;
            if (_dupenv_s (&buffer, &size, name) != 0 || buffer == nullptr)
                return std::nullopt;
            std::string value (buffer);
            std::free (buffer);
            return value;
#else
            if (const char* v = std::getenv (name))
                return std::string (v);
            return std::nullopt;
#endif
        };
        return detectSuite (pluginFolders (platform, getEnv));
    }
}
