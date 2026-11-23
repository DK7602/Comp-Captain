// Comp Captain engine: where each lyric line is sung in a take.
// The words speech recognition heard are matched to the lyric words in order, the way a spell-checker
// lines up two spellings: words may be missed or misheard, ad-libs may be added, and a take may only
// cover part of the song (one verse of a song's lyrics).
#pragma once

#include "cc/Lyrics.h"
#include "cc/Words.h"

#include <vector>

namespace cc
{
    struct WordTime
    {
        double start = 0.0;
        double end = 0.0;
        bool heard = false;     // false: worked out from the words around it

        bool operator== (const WordTime&) const = default;
    };

    struct LinePlace
    {
        int line = 0;           // index into Lyrics::lines
        double start = 0.0;     // seconds into the take
        double end = 0.0;
        float sure = 0.0f;      // 0..1: how many of the line's words were heard
        std::vector<WordTime> words; // one per shown word of the line (see shownWords)
    };

    // Lines sung in this take, in order. Lines the take doesn't cover are left out.
    std::vector<LinePlace> placeLines (const Lyrics&, const std::vector<HeardWord>&);

    // The same, looking only at lines firstLine..lastLine (a take known to sit in that part of the song).
    std::vector<LinePlace> placeLines (const Lyrics&, const std::vector<HeardWord>&, int firstLine, int lastLine);

    // How alike two plain words are, 0..1 ("love" / "loves" ~ 0.8).
    double wordLikeness (const std::string& a, const std::string& b);

    // Hear It: lyrics written from the words heard, a new line at each pause (or every dozen words).
    std::string lyricsFromWords (const std::vector<HeardWord>&);
}
