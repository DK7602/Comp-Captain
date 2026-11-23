// Comp Captain engine: the sung phrases in a take, found from its gaps and breaths (no lyrics needed).
// Used when there are no lyrics ("Phrase 1, 2...") and to settle where lines start and end.
#pragma once

#include <cstddef>
#include <vector>

namespace cc
{
    struct Phrase
    {
        double start = 0.0; // seconds into the take
        double end = 0.0;
    };

    struct PhraseOptions
    {
        double quietBelowDb = -30.0;  // relative to the take's loud parts
        double shortestGap = 0.30;    // seconds of quiet that split two phrases (a breath)
        double shortestPhrase = 0.25; // seconds; anything shorter is a click or a breath, not singing
    };

    // mono: the take's sound (any sample rate).
    std::vector<Phrase> findPhrases (const float* mono, size_t numSamples, double sampleRate, const PhraseOptions& = {});
}
