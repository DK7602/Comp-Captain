// Comp Captain engine: the song's lyrics, as lines grouped in sections.
// Reads what people actually paste: "[Verse 1]" / "Chorus:" / "(Bridge)" headings, karaoke .lrc time tags,
// blank lines, and notes like "(x2)" or "*repeat*" that aren't sung words. A heading on its own that names
// an earlier section ("[Chorus]" again, with no lines under it) sings that section again; "[Chorus x2]"
// sings it twice. Chord sheets work too: rows of chords ("G   D/F#   Em7") and chords in brackets inside a
// line ("[G]I was [D]lost") are left out, and Song Rider Pro's "__________VERSE 1" break lines are headings.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cc
{
    struct LyricLine
    {
        std::string text;      // as written ("Now I'm free in Your love")
        int section = 0;       // index into Lyrics::sections
        double lrcTime = -1.0; // from an .lrc file: seconds into the song (-1 = none)
    };

    struct LyricSection
    {
        std::string name;      // "Verse 1", "Chorus"; a song can have several sections with the same name
        int firstLine = 0;
        int lineCount = 0;
    };

    struct Lyrics
    {
        std::vector<LyricSection> sections;
        std::vector<LyricLine> lines;

        bool empty() const noexcept { return lines.empty(); }

        // Reads pasted text or an .lrc file.
        static Lyrics parse (std::string_view text);

        // Back to text with "[Section]" headings (what the Lyrics page's editor shows).
        std::string toText() const;
    };

    // True for a line that's a section heading ("[Chorus]", "Verse 2:", "(Bridge)", "PRE-CHORUS").
    // name gets the heading's name, tidied ("Pre-Chorus").
    bool isSectionHeading (std::string_view line, std::string& name);
    bool isSectionHeading (std::string_view line, std::string& name, int& times); // "Chorus x2": times = 2

    // "G", "D/F#", "Em7", "Csus4", "Bbmaj7", "(A)": a chord name.
    bool isChord (std::string_view word);

    // The words of a line as they're shown (split at spaces), for lighting them up as they're sung.
    std::vector<std::string> shownWords (std::string_view line);
}
