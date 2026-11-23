// Comp Captain engine: small helpers for the saved text ("key=value" lines).
// Numbers are written and read by hand, never through the C library, so a DAW that switches the
// computer's language (for example to a decimal comma) can't change how a project is saved or read.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cc::text
{
    // Leading / trailing spaces, tabs and \r removed.
    std::string_view trim (std::string_view);

    // "1.25", "-3", "2e-3" ... false if it isn't a plain number.
    bool parseDouble (std::string_view, double& out);
    bool parseInt (std::string_view, std::int64_t& out);

    // Fixed decimals, always with a dot: formatFixed (2.5, 3) == "2.500".
    std::string formatFixed (double value, int decimals);

    // Values can hold any text: backslash, new line and carriage return are escaped.
    std::string escape (std::string_view);
    std::string unescape (std::string_view);

    // Calls fn (key, value) for every "key=value" line (trimmed; lines without '=' are skipped).
    void forEachKeyValue (std::string_view text, const std::function<void (std::string_view key, std::string_view value)>& fn);
}
