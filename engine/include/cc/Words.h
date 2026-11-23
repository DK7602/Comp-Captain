// Comp Captain engine: words heard in a take, and keeping them next to the take so a take is only
// listened to once.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    struct HeardWord
    {
        std::string text;      // as heard, e.g. " Hallelujah," (leading space and punctuation kept)
        double start = 0.0;    // seconds from the start of the take
        double end = 0.0;
        float probability = 0.0f;

        bool operator== (const HeardWord&) const = default;
    };

    // "Can't," -> "cant", "Hallelujah!" -> "hallelujah", "1st" -> "1st": letters and digits, lower case.
    std::string plainWord (std::string_view);

    // The words of a line of text, plain (see plainWord), empty ones dropped.
    std::vector<std::string> plainWords (std::string_view line);

    // Saved next to the take ("Take 3.wav" -> "Take 3.words") as plain text, one word per line.
    std::string wordsToText (const std::vector<HeardWord>&);
    std::vector<HeardWord> wordsFromText (std::string_view);
    std::filesystem::path wordsFileFor (const std::filesystem::path& takeFile);
    bool saveWords (const std::filesystem::path& takeFile, const std::vector<HeardWord>&);
    std::optional<std::vector<HeardWord>> loadWords (const std::filesystem::path& takeFile);
}
