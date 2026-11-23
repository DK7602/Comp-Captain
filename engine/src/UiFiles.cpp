#include "cc/UiFiles.h"

#include <cctype>

namespace cc
{
    std::string mimeTypeFor (std::string_view path)
    {
        const auto dot = path.find_last_of ('.');
        std::string ext;
        if (dot != std::string_view::npos)
            for (char c : path.substr (dot + 1))
                ext += (char) std::tolower ((unsigned char) c);

        if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
        if (ext == "css") return "text/css; charset=utf-8";
        if (ext == "js") return "text/javascript; charset=utf-8";
        if (ext == "json") return "application/json; charset=utf-8";
        if (ext == "txt") return "text/plain; charset=utf-8";
        if (ext == "svg") return "image/svg+xml; charset=utf-8";
        if (ext == "png") return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "woff2") return "font/woff2";
        if (ext == "woff") return "font/woff";
        return "application/octet-stream";
    }
}
