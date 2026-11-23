// The in-app manual, in plain words. Keep it up to date whenever behaviour changes.
// (Special characters are written as HTML codes, e.g. &#x2013; for a dash: see the UI files test.)
'use strict';

const Manual = `
<h2>What Comp Captain does</h2>
<p>You sing a part five or ten times. Comp Captain listens to every take, scores every line, and builds one great
performance from the best lines. You approve every pick. Then it lines up your doubles, ad-libs, backing vocals and
doubled guitars so they sit tight with the lead, and hands the finished parts back to your DAW.</p>
<p><b>This version (M2) brings your takes and your lyrics in.</b> Record takes with CAPTURE, add audio files, or let
Cubase hand over the takes on a track's lanes. Every take lands on the bar it belongs to. Give Comp Captain your
lyrics and it listens to each take and works out where every line is sung, so the teleprompter follows your song.
Scores and comping come in the next versions: until then those parts show a sample song, marked with a gold
<span class="soon">SAMPLE</span> tag. On its track, Comp Captain passes your audio through untouched.</p>

<h2>Recording takes (CAPTURE)</h2>
<ol>
<li>Put Comp Captain on your vocal track as an insert effect.</li>
<li>Open it, go to the <b>Record</b> page and click the big red button. It glows: CAPTURE is <b>armed</b>.</li>
<li>Make sure the track is <b>monitoring</b> (in Cubase: the speaker button on the track is lit). Comp Captain hears
your voice only through the track.</li>
<li>Press Record in your DAW and sing. A plug-in can't press your DAW's Record button, so you do that as usual.</li>
</ol>
<p>Every pass becomes a take (Take 1, Take 2...), stamped with the bar it started on, and appears in the
<b>Takes</b> list. While you sing, the take grows in red on the bar ruler. A take that's silent (the track wasn't
monitoring) or shorter than half a second isn't kept, and the Assistant tells you why.</p>
<p><b>Loop Takes</b> (bottom of the Record page): on, every time your DAW's loop (cycle) comes round you get a fresh
take. Off, only the first pass is kept until you press Record again.</p>
<p>Click the red button again to disarm when you're done, so it doesn't capture when you record other tracks.
CAPTURE is never armed when a project opens.</p>

<h2>Where your takes are saved</h2>
<p>In <b>Documents &gt; Comp Captain</b>, one folder per track, named by date and track, for example
<i>2026-11-23 Lead Vox (4f1c)</i>. The takes are plain WAV files that open in any DAW. Your project remembers which
takes belong to it. The folder button next to TAKES (or <b>Takes Folder</b> at the bottom) opens it.</p>

<h2>The take list</h2>
<ul>
<li>Click a take to see it on the bar ruler. The arrows under the waveform step through them.</li>
<li><b>Role</b> (the coloured tag): Lead, Double, Harmony, Ad-lib, Backing, Guitar or Other. Comp Captain guesses
from the track or file name ("Vox Dbl L" is a double, "BGV" is backing). Click the tag to change it.</li>
<li><b>&#x2665;</b> marks a favourite: it wins ties when comping.</li>
<li><b>&#x2022;&#x2022;&#x2022;</b> (or right-click) to rename or remove a take. Removing doesn't delete the file:
it moves to a <b>Removed takes</b> folder inside the takes folder.</li>
<li>Hearing a take on its own arrives with comping (M4). For now, play it in your DAW.</li>
</ul>

<h2>Adding takes from files</h2>
<p>Drop WAV, AIFF, FLAC or MP3 files anywhere on the window (from Explorer or Finder), or click <b>+</b> next to TAKES.
Comp Captain asks which bar they start at and the song's tempo (it fills in your DAW's tempo). Files exported from
another session usually start at bar 1. Each file is copied into the takes folder, so it keeps working even if the
original moves.</p>

<h2>Seeing every take on the song's bars</h2>
<p>On the <b>Comp</b> page, pick <b>Song</b> view: every take is a lane, lined up on the song's bars, with your DAW's
play position moving across them.</p>

<h2>More than one track</h2>
<p>Put a Comp Captain on each vocal track (lead, doubles, ad-libs, backing). The track picker at the top left lists
every track that has one, with its number of takes: pick one to see its takes from this window. The same menu sets
the track's role; takes recorded on it take that role.</p>

<h2>Takes from Cubase's lanes (ARA)</h2>
<p>If you recorded takes in Cubase (cycle recording makes one lane per take), select those events on the track, then
choose <b>Audio &gt; Extensions &gt; Comp Captain</b>. Comp Captain lists every take Cubase hands over, on its real
bar (from your project's tempo and time signatures), and plays them back exactly as they are.</p>
<p>This part is a test: we're finding out how much Cubase Artist shares. Open <b>&#x2699; Settings</b> and click
<b>Copy</b> next to ARA: it copies a plain report of what Cubase sent. Paste it into the chat so it can be checked.</p>

<h2>The pages (left side)</h2>
<ul>
<li><b>Record</b>: CAPTURE, your takes, and the teleprompter with your lyrics.</li>
<li><b>Lyrics</b>: get the words in; Comp Captain lines each line up with your takes.</li>
<li><b>Comp</b>: every take of every line with its scores; the Song view already shows your real takes.
<span class="soon">M3&#x2013;M4</span></li>
<li><b>Tighten</b>: line up doubles, ad-libs, backing vocals and guitars with the lead. <span class="soon">M5</span></li>
<li><b>Review</b>: scores for the whole song, and the Style that decides what matters most. <span class="soon">M3</span></li>
<li><b>Export</b>: make the finished audio files, or drag them straight into your DAW. <span class="soon">M4</span></li>
</ul>

<h2>Getting lyrics in</h2>
<ul>
<li><b>Type or paste</b> them in the box on the <b>Lyrics</b> page. Put <b>[Verse 1]</b>, <b>[Chorus]</b> and so on
above each part to make sections (Chorus:, (Bridge) and VERSE 2 work too).</li>
<li>A chorus you sing again: write <b>[Chorus]</b> again with nothing under it, and Comp Captain sings it again.
<b>[Chorus x2]</b> sings it twice. Notes like (x2) or *repeat* on their own line are skipped.</li>
<li><b>Open File</b>, or drop the file on the window: a .txt, a Word .docx, or an .lrc karaoke file. Older Word
files (.doc) and PDFs can't be read: open them and copy the words into the box.</li>
<li><b>Hear It</b>: no lyrics written down? Comp Captain writes them from your lead take (or the take you picked on the
Record page). Fix any wrong words, then add the section headings.</li>
<li>Paste, Open and Hear It replace what's in the box. <b>Undo</b> (top right of the box) brings the old lyrics back.</li>
<li><b>From My Songs</b>: a song from <b>Song Rider Pro</b>. In Song Rider Pro, press its <b>Export</b> button first:
it saves the song as a page (<i>Song name &#x2013; Full Preview (Print).html</i>, usually in your Downloads folder).
Then pick that page here (or drop it on the window). The chords are left out and its sections come along. Song Savant
and Key Compass songs hold chords and parts but no words, so they can't fill in lyrics.</li>
<li>Chord sheets pasted from anywhere work too: rows of chords and chords in brackets like <b>[G]</b> are left out.</li>
</ul>
<p>The lyrics belong to the song: every Comp Captain in your project shows the same lyrics, and they're saved with the
project.</p>

<h2>How the lyrics get lined up</h2>
<p>Comp Captain <b>listens</b> to each vocal take in the background (speech recognition on your computer: nothing is
sent anywhere) and works out where every line is sung. It does this once per take and keeps what it heard next to the
take (a small <i>.words</i> file), so opening the project again is instant. It uses at most half your computer's cores,
never the audio thread, and waits while you record.</p>
<ul>
<li>The <b>Lined up with the audio</b> list shows each line with the bar it starts on. A dimmed line wasn't heard in
any take yet: it's placed between the lines around it. Hover over a line to see which takes sang it.</li>
<li>Lead takes set the timing. A take that only covers part of the song (a punch-in on the last chorus) lands on the
right part, by where it sits in your song.</li>
<li><b>Lyrics Status</b> shows which take is being listened to. A small dot next to each take in the take list shows
it too: green when it's been listened to.</li>
<li>Changed the words a lot? Lining up happens again by itself. <b>Listen to the takes again</b> starts over.</li>
<li>The listening needs a processor from about 2013 or newer. On an older one, lyrics and sections still work, just
without the lining up.</li>
</ul>
<p>No lyrics at all is fine too: each take is still split into its sung <b>phrases</b> (shown as thin blue marks under
each take in the Comp page's Song view).</p>

<h2>The teleprompter (Record page)</h2>
<p>While your DAW plays, the line being sung sits big in the middle, the words light up gold as they're sung, and the
line before and the next line sit above and below. The section box and the lyric strip on the other pages follow
along. With <b>Count-in on screen</b> on (bottom of the Record page), the beats before a line count down as glowing
dots. When your DAW is stopped, it shows the line where the playhead is; the arrows step through the sections.</p>
<p>The teleprompter follows once a take has been listened to (that's how it knows where the lines are). For a brand
new song, record a first take, and from the second take on the words follow you. Until then, the space above the big
line says what it's waiting for (for example "Listening to Take 1... 40%").</p>
<p>Words not following? Open <b>&#x2699; Settings</b> and click <b>Copy</b> next to <b>Lyrics &amp; listening</b>: it
copies a plain report of what Comp Captain heard in your takes and where each line landed. Paste it into the chat.</p>

<h2>The scores (M3)</h2>
<ul>
<li><b>Pitch</b>: on the notes. Off for rap.</li>
<li><b>Timing</b>: on the beat, and with your other takes.</li>
<li><b>Clarity</b>: the words came through clearly and correctly.</li>
<li><b>Energy</b>: the delivery, compared with your other takes of the same line.</li>
<li><b>&#x26A0;</b>: a problem in the recording, like a pop, clipping or a loud breath. It says what it heard.</li>
</ul>
<p>The <b>Style</b> (Rap, Pop, Country, Worship / Gospel...) decides how much each score counts. The scores advise;
you decide. A take you mark with a heart wins ties.</p>

<h2>Works on its own, even better with Sonicology</h2>
<p>Export works the same for everyone: clearly named WAV files that land on the right bar in any DAW. If Voxology
or Mixology is installed, Send buttons appear so the roles (lead, double, ad-lib...) and lyrics go straight over.</p>

<h2>Light and dark</h2>
<p>Click the logo at the top to switch between the light and dark look.</p>

<h2>Keeping your session light</h2>
<p>Comp Captain never does heavy work on the audio thread. While you record, the audio thread only copies the sound
into memory; a background thread writes it to disk. On its track it passes the audio through untouched.</p>
`;
