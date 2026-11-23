# Comp Captain

**Your best takes, captained into one.** Comp Captain listens to every take you record, scores every line, builds
the best performance out of the best lines, and lines up your doubles, stacks, ad-libs and doubled guitars so they
sit tight with the lead.

Status: **M1, takes in (0.2.0).** CAPTURE records every pass through the track as a take stamped with its bar; audio
files (WAV, AIFF, FLAC, MP3) can be dropped in; as a Cubase Extension (ARA) it lists and plays the takes on a track's
lanes. Takes have roles and hearts, are saved in Documents/Comp Captain and remembered with the project. Lyrics,
scores and comping come next (those pages show a sample song). On a track it passes audio through untouched. The
plan is in [docs/PLAN.md](docs/PLAN.md).

## Getting a build

Every push to `main` builds Windows, macOS and Linux on GitHub Actions. Open the latest run under **Actions**,
download **Comp Captain - Windows** (or your system), and follow `INSTALL.txt` inside.

## What's where

| Folder | What it is |
| --- | --- |
| `ui/` | The interface (HTML / CSS / JS), packed inside the plug-in. Open `ui/index.html` in a browser to preview it. |
| `engine/` | Framework-free C++20: every decision, tested without a DAW. |
| `tests/` | Catch2 tests for the engine and the interface files. |
| `plugin/` | The thin JUCE 8 wrapper (VST3, Standalone, AU on Mac). |
| `host-test/` | Loads the built VST3 like a DAW (scan, play, CAPTURE a take, save, restore, open the window), tests ARA with a pretend DAW, and tests adding audio files. |
| `tests/ui/` | Checks the window's messages to and from the plug-in in a real browser (Playwright). |
| `art/` | The owner's original art and mockups. |

## Building yourself

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

On Windows, JUCE needs the WebView2 SDK from NuGet first (see `.github/workflows/build.yml`). The ARA SDK is
downloaded by CMake.

Handy for testing: `COMP_CAPTAIN_TAKES_FOLDER=<folder>` saves takes there instead of Documents/Comp Captain, and
`CC_TEST_AUDIO=<folder>` makes `CompCaptainImportTest` also add the recordings in that folder (they never go in the repo).
Open `ui/index.html?real` in a browser to try the take list with made-up takes.
