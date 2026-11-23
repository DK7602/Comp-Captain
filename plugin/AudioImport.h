// Adding a take from a file: WAV, AIFF, FLAC or MP3 is decoded into a 32-bit float WAV in the takes
// folder (so the take keeps working even if the original file moves), with its waveform picture.
// Runs on a background thread. Kept apart from the plug-in so it can be tested on its own.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "cc/Take.h"

#include <atomic>
#include <optional>
#include <vector>

struct ImportedTake
{
    bool ok = false;
    cc::Take take;
    std::vector<std::uint8_t> peaks;
    juce::String message; // plain words for the window
};

struct ImportPlan
{
    juce::File source;
    juce::String name;                 // "" = the file's name
    juce::File folder;                 // the track's takes folder
    int bar = 1;
    double bpm = 120.0;
    cc::Meter meter;
    std::optional<cc::Role> role;      // none = guess from the name, else fallbackRole
    cc::Role fallbackRole = cc::Role::lead;
    juce::String trackName;
};

ImportedTake importAudioFile (const ImportPlan&, const std::atomic<bool>* cancel = nullptr);

// The file types Comp Captain can add, for file choosers and messages.
juce::String importableWildcard();
