// Comp Captain engine: how the packed interface files are served to the window.
#pragma once

#include <string>
#include <string_view>

namespace cc
{
    // The content type for a file in ui/ ("css/app.css" -> "text/css; charset=utf-8"). Text files always
    // say they're UTF-8: without it some web views (Mac, Linux) read "–", "·" and "✓" as junk.
    std::string mimeTypeFor (std::string_view path);
}
