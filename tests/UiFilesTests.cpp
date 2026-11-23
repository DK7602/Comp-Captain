// Checks on the web interface files in ui/: everything they point at is there, and nothing is
// loaded from the internet (the interface is packed inside the plug-in).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    const fs::path uiDir = fs::path (CC_SOURCE_DIR) / "ui";

    std::string readFile (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::set<std::string> matches (const std::string& text, const std::regex& re)
    {
        std::set<std::string> out;
        for (auto it = std::sregex_iterator (text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
            out.insert ((*it)[1].str());
        return out;
    }
}

TEST_CASE ("index.html and everything it links to are in ui/")
{
    REQUIRE (fs::exists (uiDir / "index.html"));
    const auto html = readFile (uiDir / "index.html");
    const auto refs = matches (html, std::regex (R"((?:src|href)=\"([^\"#][^\"]*)\")"));
    REQUIRE_FALSE (refs.empty());
    for (const auto& ref : refs)
    {
        INFO (ref);
        CHECK (fs::exists (uiDir / ref));
    }
}

TEST_CASE ("Every url() in the stylesheet points at a packed file")
{
    const auto cssPath = uiDir / "css" / "app.css";
    REQUIRE (fs::exists (cssPath));
    const auto css = readFile (cssPath);
    for (const auto& ref : matches (css, std::regex (R"re(url\("([^"]+)"\))re")))
    {
        INFO (ref);
        CHECK (fs::exists (uiDir / "css" / ref));
    }
}

TEST_CASE ("The interface never loads anything from the internet")
{
    for (const auto& entry : fs::recursive_directory_iterator (uiDir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".html" && ext != ".css" && ext != ".js")
            continue;
        const auto text = readFile (entry.path());
        INFO (entry.path().string());
        CHECK (text.find ("http://") == std::string::npos);
        CHECK (text.find ("https://") == std::string::npos);
    }
}

TEST_CASE ("The six pages are all in the interface, and Mix Assist is gone")
{
    const auto html = readFile (uiDir / "index.html");
    for (auto page : { "record", "lyrics", "comp", "tighten", "review", "export" })
    {
        INFO (page);
        CHECK (html.find (std::string ("data-go=\"") + page + "\"") != std::string::npos);
        CHECK (html.find (std::string ("class=\"page\" data-page=\"") + page + "\"") != std::string::npos);
    }
    CHECK (html.find ("Mix Assist") == std::string::npos);
}
