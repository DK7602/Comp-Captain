// Comp Captain engine: what was heard in a take (its words and phrases), kept next to the take so a
// take is only listened to once.
#pragma once

#include "cc/Phrases.h"

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

    struct Hearing
    {
        std::vector<HeardWord> words;
        std::vector<Phrase> phrases;
        std::string model;        // the speech model that heard it; a different model listens again

        bool operator== (const Hearing&) const = default;
    };

    // Saved next to the take ("Take 3.wav" -> "Take 3.words") as plain text, one word or phrase per line.
    std::string hearingToText (const Hearing&);
    std::optional<Hearing> hearingFromText (std::string_view);
    std::filesystem::path hearingFileFor (const std::filesystem::path& takeFile);
    bool saveHearing (const std::filesystem::path& takeFile, const Hearing&);
    std::optional<Hearing> loadHearing (const std::filesystem::path& takeFile);
}
