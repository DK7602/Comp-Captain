// Comp Captain engine: lyrics files as text. A lyrics file may come from any computer: UTF-8 (with or
// without the marker Windows puts at the start), UTF-16 (Notepad's "Unicode"), or the older Windows
// letters (Windows-1252, e.g. curly quotes typed in an old Word). Word files (.docx) are read from the
// document's own text (the zip is opened by the plug-in; this reads its word/document.xml).
#pragma once

#include <string>
#include <string_view>

namespace cc
{
    // Any of the encodings above -> UTF-8 with "\n" line ends.
    std::string textFromBytes (std::string_view bytes);

    // word/document.xml from a .docx -> its text, a line per paragraph (and per line break).
    std::string textFromWordXml (std::string_view documentXml);

    // A song page exported by Song Rider Pro (its Export button: "<Song> - Full Preview (Print).html", old
    // and new layouts) -> lyrics with "[Section]" headings. Chords (printed in red) and the title / BPM line
    // are left out. Other web pages come out as their plain text, a line per block.
    std::string textFromSongRiderPage (std::string_view html);

    // True if the bytes are valid UTF-8.
    bool isUtf8 (std::string_view);
}
