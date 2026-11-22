# Comp Captain: notes for Claude

Comp Captain is a planned JUCE 8 plug-in and standalone app (VST3, Standalone, AU on Mac, ARA 2). It scores every
line of every vocal take, builds the best comp from them, and tightens doubles, stacks, ad-libs, backing vocals and
doubled guitars to the lead. **The plan is `docs/PLAN.md`: read it first.**

## Status: planning only

- **Do not write code or start building.** The owner is making the graphics and UI. Building starts (M0) when they
  share the art and say go.
- When they share art: look at it carefully, list anything unclear or inconsistent, and settle the interface
  technology with them (web view like Voxology / Mixology / Masterology, or drawn in JUCE like Groove Grabber /
  Song Savant) before M0.

## About the owner

- Uses **Cubase Artist 14 on Windows**. Installs builds from the GitHub Actions artifacts. Sessions run to about
  80 tracks on a standard computer, so nothing heavy on the audio thread, ever.
- Makes all genres (rap, rock, country, pop, worship...) and doesn't use profanity: keep names, examples, presets
  and UI text clean.
- Explain things in plain, beginner-friendly words. Their other plug-ins (same GitHub account, DK7602) are the
  reference for habits: Voxology, Mixology, Masterology (the Sonicology suite), Groove Grabber, Song Savant,
  Koolaid Man, Sample Snagger, VoxaVersa, Sanctuary Choir, Key Compass, Song Rider Suite.

## Habits to keep (from the other plug-ins)

- Framework-free C++20 engine with Catch2 tests; the JUCE wrapper stays thin.
- CI on every push (Windows, macOS, Linux), tests must pass, plus a host test that loads the VST3 like a DAW.
- Only licences that allow selling later (MIT / Apache / BSD / BSL; no GPL). Every third-party piece goes in
  THIRD_PARTY_NOTICES.txt.
- An in-app manual in plain words, updated whenever behaviour changes. Version in `CMakeLists.txt`.
- Light and dark looks, switched by clicking the logo.
- Commit with the session's attribution trailer and push to `main`.
