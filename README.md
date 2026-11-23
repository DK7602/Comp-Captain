# Comp Captain

**Your best takes, captained into one.** Comp Captain listens to every take you record, scores every line, builds
the best performance out of the best lines, and lines up your doubles, stacks, ad-libs and doubled guitars so they
sit tight with the lead.

Status: **M0, the look.** The full interface from the owner's art, with a made-up song and takes, light and dark
looks, and the manual's outline. On a track it passes audio through untouched. The plan is in
[docs/PLAN.md](docs/PLAN.md).

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
| `host-test/` | Loads the built VST3 like a DAW: scan, play, save, restore, open the window. |
| `art/` | The owner's original art and mockups. |

## Building yourself

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

On Windows, JUCE needs the WebView2 SDK from NuGet first (see `.github/workflows/build.yml`).
