# Comp Captain: the plan

Written 2026-11-22, before any code. The owner's art came in the same day and we agreed the decisions in
"Decisions after the art" below; M0 has started. This file is the agreement on what Comp Captain does and the
order we build it in.

## What it is, in one breath

You record a verse five or ten times. Comp Captain listens to every take, scores every line, builds the best
performance out of the best lines (you approve every pick), then lines up your doubles, stacks, ad-libs, backing
vocals and doubled guitars so they sit tight with the lead. The finished parts go back into the DAW, ready for
Voxology and Mixology.

Think of a photographer who takes twenty group photos and makes one where everyone's eyes are open.

## Who it's for

- The owner: Cubase Artist 14 on Windows, all genres (rap, rock, country, pop, worship...), clean lyrics,
  installs builds from the GitHub Actions artifacts.
- Sessions of about 80 tracks on a normal computer: Comp Captain must never make playback stutter.
- Keep it DAW-neutral where that costs nothing (VST3 everywhere, AU on Mac). Cubase-only steps live in a DAW guide,
  not in the engine.

## The workflow (the steps the header shows)

### 1. TAKES: bring the takes in
Three ways, all landing in the same take list:
- **CAPTURE**: Comp Captain sits on the vocal track. Arm it, record in Cubase as usual: every pass is kept as a take,
  stamped with the bar it started on (the same trick as Koolaid Man's CAPTURE), so it lines up with the song.
- **From Cubase (ARA)**: select the takes on the track's lanes, *Audio > Extensions > Comp Captain* (the same way
  Honey Tune goes on a clip). Comp Captain sees every take and where it sits in the song. **To prove in M1**: that
  Cubase Artist hands us several lane events in one ARA document, and what plays back.
- **Drop files**: WAV / AIFF / FLAC / MP3 from anywhere (a session from another DAW, an old project), with a bar
  and tempo to place them.

Each take gets a role: **Lead**, **Double**, **Harmony**, **Ad-lib**, **Backing**, **Guitar**, or **Other**.
Comp Captain guesses from the track name and how the take lines up with the others; you can always change it.

### 2. LYRICS: know what's being said
Every user can get lyrics in, whatever other software they own:
- **Paste or type** (from Notes, Google Docs, a text message...). `[Verse 1]`, `[Chorus]` style headings become
  sections automatically.
- **Open or drag in a file**: .txt, .docx (Word) or .lrc (karaoke lyric files, which carry their own timings).
- **Hear it**: no lyrics written down? Comp Captain writes them from your takes with the same speech recognition
  it already uses for scoring, and you fix any wrong words.
- **Extras** for owners of the other plug-ins: open a song from Song Rider Pro, Song Savant or Key Compass.
- Comp Captain lines each lyric line up with the audio by itself (speech recognition run on your computer, nothing
  sent anywhere). You can drag a line's start or end if it's off.
- No lyrics? It still works: it splits the takes into phrases at the gaps and breaths, and names them "Phrase 1, 2..."
- **Teleprompter** (while CAPTURE is armed): the lyrics scroll in time with the beat, karaoke style, with the line
  you're on lit, plus a count-in to your punch.

### 3. SCORES: every line of every take gets a score
Four scores per line, each 0 to 100, each with a plain-words reason (like Voxology's Learn panel), plus a warning
badge for recording problems:

| Score | What it listens for |
| --- | --- |
| **Pitch** | On the notes: against the song's key and against what most of your takes sing there (the "agreed melody"). Off for rap by default. |
| **Timing** | On the beat: word starts against the grid and against the other takes. Rushed or dragging lines get called out. |
| **Clarity** | Clear and right: did the speech recognition hear the lyric? Catches flubbed, swallowed and missing words. |
| **Energy** | Delivery: loudness, brightness and push compared with your other takes of that line. |
| **⚠ badge** | Problems in the recording: clipping, pops, mic bumps, a loud breath, room noise, something in the background. It lowers the total and says what it heard. |

A **Style** sets how much each score counts (Rap, Trap, Singing, Pop, Country, Rock, Worship / Gospel, R&B, or your
own). Rap leans on Timing, Clarity and Energy; singing styles lean on Pitch. The total is shown as a percentage.

The scores advise; you decide. A take you mark as a favourite (heart) wins ties.

### 4. COMP: build the best version
- **AUTO COMP** picks the best take for every line. A **Fewer cuts / Best every line** slider decides how picky it
  is: fewer cuts sounds more natural (one singer, one moment), best every line wins on paper.
- The comp view: a lane per take, the lyric lines across the top, the chosen piece of each line lit. Click a line on
  any lane to hear it in context and to choose it. Arrow keys step through the takes of a line (fast A/B).
- Joins are made in the gaps and breaths, on a zero crossing, with short crossfades. Takes are level-matched
  (gain only) so a louder take doesn't jump out. **Word swap** (later): swap one word inside a line.
- Everything is undoable, and the comp is saved with the DAW project.

### 5. TIGHTEN: line the layers up with the lead
- For every Double, Harmony, Backing and echoing Ad-lib take: find where each word starts and ends against the lead
  and nudge it there (time-stretching only the bits that need it). Word starts, word ends and S / T / K sounds get
  the most attention, because those are what make a stack sound messy.
- **TIGHTNESS** knob: Natural (only the big misses) to Locked (every word). Per role, with sensible defaults
  (doubles tight, harmonies a little looser, ad-libs left alone unless they repeat the lead's words).
- **Pitch follow** (off by default): bends a double's notes gently toward the lead's.
- **Guitars**: doubled rhythm parts lined up with the main part, or with the grid.
- A before / after switch on every layer.

### 6. SEND: back to the DAW
- **DRAG TO DAW**: the comp and every tightened layer as audio files, bar-aligned, named
  "Lead Comp - Verse 1.wav", "Double 1 (tight).wav"...
- **Render all** to a folder next to the project.
- (From ARA) **play in place**: the tightened layers play from their own clips with no bounce. To prove in M1/M4.
- The DAW guide shows the steps for the DAW it's in (Cubase first).

## Working with the rest of your tools

- **Song Rider Pro / Song Savant / Key Compass**: lyrics, sections, tempo and key come from their song files.
- **Voxology**: each rendered file carries its role, so Voxology's Auto-Edit knows a double from a lead (a double
  gets less reverb, an ad-lib gets its own space). Comp Captain does no processing that's Voxology's job (no EQ,
  tone or pitch smoothing).
- **Mixology**: roles for Analyze Mix (lead vox, ad-libs, backing).
- **Comp Captain works fully on its own.** Export (Drag to DAW, Render All) is the same for everyone. The Send
  buttons depend on what's there:
  - Voxology / Mixology open in the same project: **Send to Voxology / Mixology** with a green "connected" dot;
    roles and lyrics go straight over.
  - Installed but not in this project: the button shows; clicking it says, in plain words, to put Voxology on the
    lead track and click again.
  - Not installed: the buttons are hidden and that spot shows **Drag to DAW** and **Render All**. No greyed-out
    buttons, no ads; one line in the manual and the ⚙ menu says it works even better with the Sonicology suite.
- **Sonicology Rack**: later, if wanted, Comp Captain shows there too. It is not part of the suite, it works alone.

## Staying light in an 80-track session

- One Comp Captain does the whole song. Analysis runs in the background on at most half your cores, never on the
  audio thread, and only once per take (results kept in a cache).
- On its track it passes audio through untouched (bit for bit) while you're not auditioning.
- Speech recognition uses a small model on the CPU; if a computer is slow, scores fill in line by line and you can
  start comping before it's done.

## How it's built (same habits as the other plug-ins)

- JUCE 8, C++20, CMake. A framework-free engine (`engine/`) with Catch2 tests, so every decision can be checked
  without a DAW. VST3 + Standalone (+ AU on Mac), ARA 2 for the Cubase lanes.
- GitHub Actions builds Windows, macOS and Linux on every push, runs the tests, and loads the VST3 the way a DAW
  does (scan, play, save, restore, open the window).
- Outside code, all with licences that allow selling later (no GPL):
  - CREPE "tiny" pitch model (MIT), already used by Voxology's Honey Tune.
  - whisper.cpp (MIT) with a small English model (MIT weights) for the lyrics and the Words score.
  - Signalsmith Stretch (MIT), already used by Groove Grabber, for the tightening.
  - ARA SDK (Apache 2.0), JUCE (AGPLv3 / commercial), Catch2 (BSL-1.0).
- Interface: **web view** (HTML / CSS / JS, like Voxology, Mixology and Masterology), chosen 2026-11-22. JUCE 8's
  WebBrowserComponent uses WebView2 on Windows and WKWebView on Mac, so it works in any DAW that loads VST3 / AU.
  The UI files are packed inside the plug-in; nothing is loaded from the internet.
- A light and a dark look, switched by clicking the logo, like the others.
- An in-app manual in plain words, kept up to date with every version.

## Decisions after the art (2026-11-22)

The owner's art (four mockups, dark and light backgrounds, logo, title, Assistant badge) lives in `art/`. The owner
said: keep the title, logo, detail, glows and background exactly; button placement and layout are ours to choose.

**Layout.** Header: logo (click = light / dark), title, "Your Comping Captain" script, ⚙ and ? only. Left: six
pages in workflow order. Top: the track picker and audition transport, then the current lyric line and the
sections strip (always shown). Right: a panel that changes with the page. Bottom: the Assistant bar (always shown)
and one big gold button for the page's main job.

| Page | Centre | Right panel | Gold button |
| --- | --- | --- | --- |
| **Record** | Teleprompter (karaoke lyrics, bar ruler, live waveform), take picker, arm / meter | Takes, Sections | (none; Cubase records) |
| **Lyrics** | Lyric editor with sections, load / paste / Hear it, line-up view | Sources, Sections | Line Up Lyrics |
| **Comp** | Line view (every take of one line, with scores) or Song view (every take across the song), Best Comp lane | Scores for the current line | Build Performance |
| **Tighten** | Layer lanes against the lead, before / after | Alignment & Layers (Doubles / Ad-Libs / BV / Guitars), Layer Control | Apply & Render |
| **Review** | Score table for the whole song, flagged lines | Style and weights | Re-score |
| **Export** | Files to render, Drag to DAW | Send to Voxology / Mixology (if installed) | Render All |

**Changes from the mockups** (owner: "do what's best"):
1. No "Mix Assist" page and no "pitch smoothing" suggestion: EQ, tone and tuning are Voxology's job. Layer volume
   lives in Tighten.
2. Four score columns (Pitch, Timing, Clarity, Energy); recording problems are a ⚠ badge (see Scores).
3. The red button **arms** CAPTURE. A plug-in can't press Cubase's Record button, so recording starts when you hit
   Record in Cubase, and each pass becomes a take. "Loop Takes" follows Cubase's cycle. "Pre-roll" is renamed
   "Count-in on screen" (the real pre-roll is set in Cubase).
4. No minimize / close buttons (the DAW's window has them). Only ⚙ and ?.
5. One transport per page (the Record page keeps the big bottom row only).
6. Best Comp appears once, and each piece sits under the lyric line it belongs to.
7. Doubles and ad-libs on other tracks: put Comp Captain on each vocal track; the copies in one project find each
   other, and any window shows them all (the track picker at the top).
8. The window is resizable and keeps its 16:9 shape (art size 1672 × 941).

**Art handling.** Backgrounds are the owner's plates (1672 × 941); the thin bright lines are redrawn on top so they
stay sharp on high-resolution screens. The sound-wave glow behind the logo and the compass rose are drawn in code
(crisp at any size, both looks). The logo's stray glow bits are erased. In the light look the tagline and the word
"Assistant" get a soft navy shadow so they read on white; panels become frosted white glass with gold edges.
**New header art (2026-11-23, 0.2.1).** The owner found the light look's graphics messy (art drawn for a dark
background) and sent new art: `art/header.webp` (logo, sound waves, "COMP CAPTAIN" and the tagline in one picture,
built into `ui/img/header.png` by `art/make_header.py`). It's the header in **both looks** (owner's choice); the
logo button is a see-through click area over its microphone. The old logo, title and drawn header waves are gone
(the originals stay in `art/`). The owner's new Assistant badge (`art/assistant.webp`, built into
`ui/img/assistant.png` by `art/make_assistant.py`) is used in both looks too.

## Milestones (each one ends with a test in Cubase)

### M0 - The look (waits for the owner's graphics)
Repo, CMake, CI, the full UI shell from the owner's art with made-up takes, light / dark, the manual's outline.
The plug-in loads in Cubase and passes audio through untouched. Pass: it looks like the art and loads cleanly.
**Status (2026-11-23):** done, version 0.1.1. CI green on Windows, macOS and Linux. The owner tested it in Cubase:
loads clean; their fixes (sharper waveforms, bigger lyrics, Assistant badge backing) are in 0.1.1.

### M1 - Takes in
CAPTURE, file drop and the ARA proof. Take list, roles, waveforms placed on the song's bars.
Pass: record 5 takes in Cubase, they all show at the right bar; ARA sees the lane takes (or we learn its limits).
**Status (2026-11-23):** done, 0.2.1. The owner tested it in Cubase: arming CAPTURE and recording works. (The ARA
report from Cubase Artist is still to come; it doesn't block M2.) What's in it:
- **CAPTURE** (engine `Capture`): armed by the red button (a non-automatable "CAPTURE armed" parameter, never armed
  when a project opens). While armed and the DAW records, the audio thread copies the track's sound into a lock-free
  ring; a shared background thread writes 32-bit float WAVs (header kept current, so a crash keeps the take) to
  `Documents/Comp Captain/<date> <track> (<id>)/Take N.wav`. A jump in the song position (the DAW's cycle) starts a
  new take ("Loop Takes"; off = keep only the first pass). Silent passes (track not monitoring) and passes under
  half a second aren't kept, with a plain message. If the disk falls behind, the gap is filled with silence so the
  take stays in time.
- **Where a take sits**: quarter notes from the DAW (`ppq`), the DAW's bar start, tempo and time signature.
  Bars are counted in the current time signature (exact with ARA, which reads the song's real bar map).
  Not compensated yet: the audio interface's round-trip delay (VST3 doesn't tell plug-ins); every captured take is
  late by the same few milliseconds, so takes still line up with each other. Revisit for Timing scores (M3).
- **Files**: WAV / AIFF / FLAC / MP3 by drag and drop (the web view only gets the bytes, which are sent over in
  pieces) or the + button (native file chooser); placed at a bar and tempo; copied into the takes folder as WAV.
- **Take list**: names, roles (guessed from the track or file name, always changeable), hearts, rename, remove
  (the file moves to "Removed takes"), all saved with the project. The track picker lists every Comp Captain in the
  project (same process); the track's role is set there.
- **ARA**: Comp Captain is an ARA 2 Extension. It lists every playback region the DAW shares (on its bar, from the
  song's tempo and bar map), plays the regions assigned to it unchanged (sample-exact in the tests), reads the audio
  for waveforms, keeps roles and hearts in the DAW's ARA archive, and writes a plain report (Settings > ARA > Copy)
  of what the DAW sent: that's the proof to collect from Cubase Artist.
- **Interface**: the Record page and the Comp page's Song view show the real takes; pages that need lyrics or scores
  show the sample song with a gold SAMPLE tag. Interface files are plain ASCII (special characters as escapes)
  because Linux's web view read them as Latin-1 (and the Mac's might).
- **Tests**: engine (Catch2), CAPTURE through a real VST3 load with a pretend transport (host test), adding files
  (WAV / AIFF / FLAC made on the fly; the owner's MP3 vocals locally), ARA with a pretend DAW (renders exactly, follows
  a moved take, saves, closes cleanly), and the window's messages in a real browser (Playwright).
- Not in M1 (by plan): hearing a take on its own (M4), the teleprompter following real lyrics (M2), guessing roles
  from how a take lines up with the others (needs M2/M3 analysis).

### M2 - Lines and lyrics
Lyrics in (paste / song files), auto line-up with the audio, phrases without lyrics, the teleprompter.
Pass: on the owner's real takes, lines land within a word of where they belong.
**Started 2026-11-23 (owner: "let's move forward").** Build order, each step pushed when it works:
1. Lyrics in: paste / type with `[Verse 1]` headings, open .txt / .lrc / .docx, saved with the project.
2. Phrases: each take split at its gaps and breaths (works with no lyrics).
3. Line-up: whisper.cpp (MIT) on the computer, English `base.en` model (~57 MB, MIT weights) shipped inside the
   plug-in bundle (no download at run time, nothing sent anywhere); word times aligned to the lyric words; Hear It.
4. Teleprompter: real lyrics following the DAW's position, current line lit, count-in.
5. From My Songs: Song Rider Pro / Song Savant / Key Compass files (formats read from their repos).

**M2 status (0.3.0, 2026-11-23): all five steps built, waiting for the owner's Cubase test.** What's in it:
- **Lyrics** (engine `Lyrics`, `TextFile`): headings in every style (`[Verse 1]`, `Chorus:`, `(Bridge)`, `VERSE 2`),
  a heading named again with no lines sings that section again, `[Chorus x2]` twice, notes like `(x2)` / `*repeat*`
  skipped, .lrc times kept. Files: .txt / .lrc in UTF-8, UTF-16 or Windows-1252, Word .docx (the plug-in unzips it,
  the engine reads `word/document.xml`). Curly quotes and dashes don't get in the way of matching words.
- **The song is shared**: the lyrics live once per project (a registry of every copy in the DAW), each copy saves them
  with a time stamp (`[cc-lyrics]` in its state) and the newest wins on load; editing in one copy updates all and
  marks the project changed. When the last copy closes the lyrics are cleared (a new project starts empty). Two
  projects open at once in one DAW would share them (rare; noted).
- **Listening** (`plugin/Listening`, engine `Speech`): one low-priority thread for the whole session; each sung take
  (not guitars, not ARA takes yet) is read, split into phrases, resampled to 16 kHz and heard by whisper.cpp
  (base.en q5_1, greedy, one word per segment with token times), max half the cores (4 at most), waiting while any
  copy is capturing. Words outside every phrase (by 0.35 s) are dropped (hallucinations in silence). Saved next to
  the take as `Take N.words` (words, phrases, model name) and read back next time; a different model listens again.
  The model sits in each bundle's `Contents/Resources` (CI copies it there; the standalone also finds it in an
  installed VST3). x86 builds need AVX2 + FMA (checked at run time: older processors get a plain message).
  Speed: ~11 s for a 105 s vocal on 2 cores.
- **Lining up** (engine `Align`, `LineUp`): local alignment of lyric words vs heard words per take (fuzzy word
  likeness), word times per shown word; then a take covering part of the song is aligned again against the lines
  that longer takes placed around its position (a punch-in on the 3rd chorus lands on the 3rd chorus). Song timing per
  line comes from the take that sang it most clearly (lead first); lines no take sang sit between their neighbours.
  Tried on the owner's Don Birthday vocal with a changed lyric sheet: all 30 lines placed.
- **Interface**: Lyrics page (editor with Undo, lined-up list with bar.beat, Lyrics Status with listening progress,
  Hear It with Replace / Add below), lyric strip on every page (current line, sections on the song's time, overview),
  teleprompter following the DAW (words light up, count-in dots in the two bars before a line), Song view shades the
  current line and shows each take's phrases, take list shows a listened-to dot.
- **From My Songs** (engine `textFromSongRiderPage`): only Song Rider Pro keeps lyrics. Its songs live in the
  browser (localStorage / its cloud), and the one file it writes is the Export button's printable page,
  `<Song> - Full Preview (Print).html` (usually Downloads). Both layouts are read: newer (sections as tables, heading
  underlined, lyrics `color:#111`, chords `color:#c00000`, chord-over-lyric as a two-row table cut at each chord)
  and older (bold heading, each section one `<pre>` with chord rows mixed in). Song Savant (`.songsavant`) and Key
  Compass (`.kcsong`) songs are XML with chords, BPM, key and section names but no words, so they can't fill lyrics
  (their BPM / key / sections could feed the Record page later). Chord sheets in general: chord rows and `[G]`
  inline chords are left out; Song Rider Pro's `__________VERSE 1` break lines are headings.
- 0.3.1 (owner: "recorded a take, the lyrics aren't following"): the teleprompter now says why it can't follow yet
  (listening %, waiting for the recording to stop, take didn't match the lyrics, speech problem); Settings has a
  "Lyrics & listening" report to copy (model path, AVX2, listening state, every take's heard words and lines found);
  listening no longer waits forever if the DAW stops sending audio blocks right after a recording.
- 0.3.2: the owner's first take was listened to but no line matched. Found: whisper's word times fall apart when a
  take has long silences (8 s before the singing bunched every word into the last second). Now only the sung parts
  (phrases, joined if under 1 s apart, 0.25 s room) are listened to, end to end with 0.5 s pauses, and each word is
  mapped back to its place in the take (engine `sungParts` / `takeTime`). Lines then settle on the start of the
  phrase they begin in (within 1.5 s; whisper was often ~1 s early) and end on a phrase end (`settleOnPhrases`). The
  phrase finder ignores digital silence when judging the quiet level, so silence before the singing doesn't hide the
  breaths. Same take with and without 8 s of silence in front now gives the same lines, 8 s apart.
- 0.3.3: owner confirms the take sang exactly those lyrics, still no match (cause not found yet; asked for the
  take's `.words` file). A take with sound but no phrases found is now listened to whole (it used to be skipped as
  "nothing sung"); Lyrics Status shows what a take that didn't match heard ("Take 1 heard: ..."), so the owner can
  see it. Takes heard by older versions are listened to again (hearing version 3).
- 0.3.4 (owner: "have the lyrics autoscroll on the beat for the first take, then smart scroll after it's listened
  to; I keep forgetting my 3rd line"): **beat scroll**. While no line has a time, playing moves the words on with the
  beat: line k of the chosen section starts at (bar where Play was pressed) + lead + k x bars-per-line. The Lines
  button (Record page, the metronome) sets bars per line (1 / 2 / 4, default 2) and the lead (0 / 1 / 2 bars,
  default 1, with the count-in dots); kept in the web view's local storage. A jump in the playhead (loop, click)
  starts again. Once any line has a time, the teleprompter follows the singing as before. The Record page's Loop
  button is now Loop Takes.
- 0.3.5 (owner on 0.3.4: "armed CAPTURE, recorded three or four takes, none saved; I see them in Cubase"). The
  capture code is unchanged since M1, where the owner's single take worked. Passes are thrown away when silent
  (< -60 dB) or shorter than half a second, with a 7-second message; while armed, the Assistant kept saying "Armed:
  press Record", so the reason was easy to miss. Now: while a take records and nothing has come through for 2 s, the
  take list and the Assistant say so (monitor button, Comp Captain on the recorded track); the reason a pass wasn't
  kept stays in the take list until a take is saved. Manual: Cubase's Direct Monitoring bypasses the track's inserts.
- Owner, 2026-11-27: the track's monitor button was off; turned on, takes save. (Likely also why the first take's
  words never matched: whatever came through the track wasn't the live singing. Not confirmed.)
- **Owner's Cubase test, 2026-11-27 (0.3.5): passed.** Takes save with the monitor on; after a take is listened to,
  the teleprompter follows the singing ("the lyrics followed along this time, it worked").
- Not yet: ARA takes aren't listened to yet (their audio comes through the ARA reader: later);
  dragging a line's ends by hand (M3/M4, with comping).

### M3 - Scores
The five scores, the Styles, the reasons. Pass: on the owner's sessions, Comp Captain's top pick matches the
owner's own pick on most lines (we count it), and every "bad line" it flags really has the problem it names.

### M4 - Comp
Auto Comp, the comp view, auditioning, swapping, joins and crossfades, level match, undo, saving with the project,
drag to DAW. Pass: a comped verse with no clicks or jumps at the joins.

### M5 - Tighten
Doubles, harmonies, backing, ad-libs and guitars lined up, the Tightness knob, before / after, rendering.
Pass: a 4-layer stack sounds like one tight performance with no warble or chopped words.

### M6 - The suite link and polish
Role tags for Voxology and Mixology, Word swap, the video tutorial, the DAW guides.

## What we need from the owner

- ~~The graphics and UI~~ (in, 2026-11-22). ~~Web view or drawn in JUCE~~ (web view).
- Optional: a cleaner, larger "Your Comping Captain" script as a transparent PNG (for now it's cut from a mockup).
- **Test sessions**: a few real vocal sessions, ideally 5+ takes of the same verse, a double or two and some ad-libs,
  across genres (a rap verse, a sung hook, a country or rock part, doubled guitars if possible). They're for testing
  only and never go in the repo. Already reachable: the owner's single-take vocals in the Voxology repository
  (`test-audio/`, e.g. Don_Birthday_2024_Vox_only.mp3, the Gallas / Schaf / Don & Lysette acapellas), fine for M1/M2;
  M3 still needs several takes of the same part.
- **From the M1 Cubase test**: the ARA report (Settings > ARA > Copy) with takes on lanes selected.
- Confirm the Styles list and the default score weights for each.
