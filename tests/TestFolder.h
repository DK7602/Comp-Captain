// A fresh, empty folder for one test, removed again afterwards.
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace cctest
{
    class TestFolder
    {
    public:
        TestFolder()
        {
            static std::atomic<int> counter { 0 };
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path()
                   / ("cc_tests_" + std::to_string (stamp) + "_" + std::to_string (counter++));
            std::filesystem::create_directories (path);
        }
        ~TestFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all (path, ec);
        }
        TestFolder (const TestFolder&) = delete;
        TestFolder& operator= (const TestFolder&) = delete;

        std::filesystem::path path;
    };
}
