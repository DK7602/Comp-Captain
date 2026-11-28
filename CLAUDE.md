# Comp Captain: notes for Claude

Comp Captain is a planned JUCE 8 plug-in and standalone app (VST3, Standalone, AU on Mac, ARA 2). It scores every
line of every vocal take, builds the best comp from them, and tightens doubles, stacks, ad-libs, backing vocals and
doubled guitars to the lead. **The plan is `docs/PLAN.md`: read it first.**

## Status (2026-11-28): M3 (scores) 0.4.2, owner-tested in Cubase: 10 takes of a 4-line rap verse, "your pick matches on 2 of 3 lines", no false flags (asked which line differed and why). Next: M4 (Comp). Takes not shared with us: scores were tuned on the owner's Don vocal with altered copies

- **M0 (the look)** done, 0.1.1, tested in Cubase by the owner (loads clean; sharper waveforms, bigger lyric text,
  navy Assistant badge backing).
- **M1 (takes in)** built, version 0.2.0: CAPTURE, file drop, take list with roles and hearts, takes on the song's
  bars, track picker across copies, ARA Extension with a copyable report. Details in `docs/PLAN.md` (M1 status).
  The owner tested it in Cubase: CAPTURE works (needs the track's monitor button on). **ARA works in Cubase Artist
  14** (report 2026-11-27: one lane take shared, readable, placed on its bar via the tempo map); ARA takes aren't
  listened to for lyrics yet (offered to the owner).
- **M2 (lines and lyrics)** done, 0.3.0-0.3.5, owner-tested ("the lyrics followed along, it worked"): lyrics (paste / .txt / .lrc / .docx, shared by every copy, saved
  with the project), background listening with whisper.cpp (model inside each bundle), lines lined up across takes,
  teleprompter following the DAW with count-in, Hear It, From My Songs (Song Rider Pro's exported page; Song
  Savant / Key Compass hold no lyrics). Details in `docs/PLAN.md`
  (M2 status). `cc_listen <model> <take.wav> [lyrics.txt]` tries listening + lining up on a real take.
- **M3 (scores)** built, 0.4.0: per take features (Take N.feat: pitch, loudness, 20-60 Hz pop band, brightness,
  flat-top clipping), scores per line per take (Pitch vs the nearest half of the other takes, Timing by lining the
  takes' sound up in song time, Clarity from what was heard, Energy vs the other takes; problems: clipping, pops,
  loud breath, noise), plain reasons, Styles, the owner's picks per line (saved), Review with "your pick matches on
  N of M lines". Details in `docs/PLAN.md` (M3 status). `cc_score` tries it on real takes.
- The owner cares most about **big lyrics on the Record page** (the teleprompter).
- Decisions are in `docs/PLAN.md`, "Decisions after the art". Interface: **web view** (`ui/`), packed into the
  plug-in. Keep the title, logo, glows and backgrounds exactly as the owner's art (`art/`); layout is ours.
- **Header and Assistant badge art** (both looks, since 0.2.1): the owner's `art/header.webp` (logo + sound waves +
  title + tagline in one picture) -> `ui/img/header.png` via `art/make_header.py`, and `art/assistant.webp` ->
  `ui/img/assistant.png` via `art/make_assistant.py`.
- Pictures the owner sends while a turn is running only arrive as chat images, not files: ask them to resend
  when the turn has ended (they land in the session's `images/` folder).
- Takes are saved in `Documents/Comp Captain/<date> <track> (<id>)/` (owner's choice). `COMP_CAPTAIN_TAKES_FOLDER`
  overrides it (tests).
- **Interface files must stay plain ASCII** (write `\u2013` in JS, `&#x2013;` in HTML, `\2013` in CSS): Linux's
  web view reads them as Latin-1. A test enforces it.
- **Test vocals:** the owner's dry vocals are in the public Voxology repo, `test-audio/` (MP3, single takes: e.g.
  Don_Birthday_2024_Vox_only, Gallas / Schaf / Don & Lysette acapellas; see its README). Clone it into the
  scratchpad and point `CC_TEST_AUDIO` at them. For M3 we still need 5+ takes of the same part from the owner.
  Never commit vocals to this repo.
- Work milestone by milestone; each ends with a test in Cubase by the owner.

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
- CI on every push (Windows, macOS, Linux), tests must pass, plus a host test that loads the VST3 like a DAW, an
  ARA test with a pretend DAW, and (Linux) the interface message test in a real browser.
- Only licences that allow selling later (MIT / Apache / BSD / BSL; no GPL). Every third-party piece goes in
  THIRD_PARTY_NOTICES.txt.
- An in-app manual in plain words, updated whenever behaviour changes. Version in `CMakeLists.txt`.
- Light and dark looks, switched by clicking the logo.
- Commit with the session's attribution trailer and push to `main`.
