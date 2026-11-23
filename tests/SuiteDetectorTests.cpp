#include "cc/SuiteDetector.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>

using namespace cc;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path path;
        TempDir()
        {
            path = fs::temp_directory_path() / ("cc-suite-" + std::to_string (std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories (path);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all (path, ec);
        }
    };
}

TEST_CASE ("Nothing installed: both Send buttons stay hidden")
{
    TempDir dir;
    const auto info = detectSuite ({ dir.path, dir.path / "does-not-exist" });
    CHECK (info.voxology == SuiteStatus::missing);
    CHECK (info.mixology == SuiteStatus::missing);
}

TEST_CASE ("Voxology installed as a VST3 bundle folder")
{
    TempDir dir;
    fs::create_directories (dir.path / "Voxology.vst3" / "Contents");
    const auto info = detectSuite ({ dir.path });
    CHECK (info.voxology == SuiteStatus::installed);
    CHECK (info.mixology == SuiteStatus::missing);
}

TEST_CASE ("Mixology found in a second folder, and as a Mac .component")
{
    TempDir a, b;
    fs::create_directories (b.path / "Mixology.component");
    const auto info = detectSuite ({ a.path, b.path });
    CHECK (info.mixology == SuiteStatus::installed);
}

TEST_CASE ("Plug-in folders for each computer")
{
    std::map<std::string, std::string> env { { "CommonProgramFiles", "C:\\Program Files\\Common Files" },
                                             { "LOCALAPPDATA", "C:\\Users\\me\\AppData\\Local" },
                                             { "HOME", "/home/me" } };
    auto getEnv = [&] (const char* n) -> std::optional<std::string> {
        if (auto it = env.find (n); it != env.end())
            return it->second;
        return std::nullopt;
    };

    const auto win = pluginFolders (Platform::windows, getEnv);
    REQUIRE (win.size() == 2);
    CHECK (win[0] == fs::path ("C:\\Program Files\\Common Files") / "VST3");

    const auto mac = pluginFolders (Platform::macOS, getEnv);
    CHECK (mac.size() == 4);
    CHECK (mac[0] == fs::path ("/Library/Audio/Plug-Ins/VST3"));

    const auto lin = pluginFolders (Platform::linuxOS, getEnv);
    CHECK (lin.front() == fs::path ("/home/me") / ".vst3");

    CHECK (pluginFolders (Platform::windows, [] (const char*) { return std::optional<std::string> {}; }).empty());
}

TEST_CASE ("Status names match the web interface")
{
    CHECK (toName (SuiteStatus::missing) == "missing");
    CHECK (toName (SuiteStatus::installed) == "installed");
    CHECK (toName (SuiteStatus::linked) == "linked");
}
