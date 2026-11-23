// Comp Captain engine: where takes are saved, and safe file names for them.
// Each track gets its own folder inside Documents/Comp Captain, named so you can find it by eye:
//   "2026-11-23 Lead Vox (4f1c)"
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cc
{
    // A name that works as a file or folder name on Windows, Mac and Linux (no \ / : * ? " < > |,
    // no trailing dots or spaces, not a reserved Windows name), at most maxLength bytes. Empty -> fallback.
    std::string safeFileName (std::string_view name, std::string_view fallback = "Take", size_t maxLength = 80);

    struct Date { int year = 2026, month = 1, day = 1; };

    // "2026-11-23 Lead Vox (4f1c)"; trackName may be empty ("2026-11-23 Comp Captain (4f1c)").
    std::string trackFolderName (Date, std::string_view trackName, std::string_view shortId);

    // folder/"stem.ext", or folder/"stem (2).ext" ... if that's taken.
    std::filesystem::path uniqueFile (const std::filesystem::path& folder, std::string_view stem, std::string_view extension);

    // Four lowercase hex characters from any number (for the folder name).
    std::string shortHexId (unsigned long long value);
}
