// The in-app manual, in plain words. Keep it up to date whenever behaviour changes.
// (Special characters are written as HTML codes, e.g. &#x2013; for a dash: see the UI files test.)
'use strict';

const Manual = `
<h2>What Comp Captain does</h2>
<p>You sing a part five or ten times. Comp Captain listens to every take, scores every line, and builds one great
performance from the best lines. You approve every pick. Then it lines up your doubles, ad-libs, backing vocals and
doubled guitars so they sit tight with the lead, and hands the finished parts back to your DAW.</p>
<p><b>This version (M3) brings your takes and lyrics in, and scores every line of every take.</b> Record takes with CAPTURE, add audio files, or let
Cubase hand over the takes on a track's lanes. Every take lands on the bar it belongs to. Give Comp Captain your
lyrics and it listens to each take and works out where every line is sung, so the teleprompter follows your song.
Building the finished comp, tightening and exporting come in the next versions: until then those parts show a
sample song, marked with a gold <span class="soon">SAMPLE</span> tag. On its track, Comp Captain passes your audio through untouched.</p>

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
monitoring) or shorter than half a second isn't kept: the reason stays in the take list (click it to hide it) until
the next take is saved. If nothing at all is coming through while you record, the take list says so straight away,
so you can turn the monitor on and go again.</p>
<p><b>No takes showing up?</b> Comp Captain hears only what goes <i>through its track</i>. Check three things: it's on
the same track you're recording; that track's <b>monitor</b> (speaker) button is on; and in Cubase's
<b>Studio Setup &gt; your audio interface</b>, <b>Direct Monitoring</b> is off (with it on, your voice goes straight to
your headphones and never passes through the track's plug-ins).</p>
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
<p>Cubase shows an Extension in the lower part of the project window. If that space is short, Comp Captain stays
readable and you scroll it up and down with the mouse wheel; Cubase's arrow button at the top right of that area
opens it in its own window, which you can make bigger.</p>
<p>This part is a test: we're finding out how much Cubase Artist shares. Open <b>&#x2699; Settings</b> and click
<b>Copy</b> next to ARA: it copies a plain report of what Cubase sent. Paste it into the chat so it can be checked.</p>

<h2>The pages (left side)</h2>
<ul>
<li><b>Record</b>: CAPTURE, your takes, and the teleprompter with your lyrics.</li>
<li><b>Lyrics</b>: get the words in; Comp Captain lines each line up with your takes.</li>
<li><b>Comp</b>: every take of every line with its scores, your picks, and the Song view of all your takes.
Building the finished comp: <span class="soon">M4</span></li>
<li><b>Tighten</b>: line up doubles, ad-libs, backing vocals and guitars with the lead. <span class="soon">M5</span></li>
<li><b>Review</b>: scores for the whole song, the lines to check, and the Style that decides what matters most.</li>
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
<p><b>Before your first take</b> (nothing listened to yet, so Comp Captain doesn't know where the lines go), the words
move with the beat instead: when you press Play, a new line comes up every 2 bars, so the next line is always in
front of you. The <b>Lines</b> button on the Record page (next to 4/4) sets how many bars each line gets (1, 2 or 4)
and when the first line comes in (on the bar you press Play, or 1 or 2 bars after it, with the beats counting
down). The arrows next to the section name pick where the words start (Verse 1, Chorus...). If you loop a part, the
words start again each time round.</p>
<p><b>After a take has been listened to</b>, the words follow your singing instead: each line comes up when you
sang it, and the words light up as you go. The space above the big line says what it's doing (for example
"Listening to Take 1... 40%"). The <b>Loop</b> button there is Loop Takes (a new take each time the loop comes
round).</p>
<p>Words not following? Open <b>&#x2699; Settings</b> and click <b>Copy</b> next to <b>Lyrics &amp; listening</b>: it
copies a plain report of what Comp Captain heard in your takes and where each line landed. Paste it into the chat.</p>

<h2>The scores</h2>
<p>Once your takes have been listened to (and your lyrics are in), every line of every take on the track gets four
scores from 0 to 100, worked out on your computer in the background:</p>
<ul>
<li><b>Pitch</b>: on the notes, against the melody your other takes agree on (and the semitones, with only one take).
It names a word that's sharp or flat, and by how much.</li>
<li><b>Timing</b>: lined up with your other takes of the line, and on the beat. It says if a line is a little behind
or rushed, and by how many milliseconds.</li>
<li><b>Clarity</b>: every word heard clearly. It names a word that didn't come through.</li>
<li><b>Energy</b>: the delivery (how loud and bright) against your other takes of the same line.</li>
<li><b>&#x26A0;</b>: a problem in the recording: clipping (too loud for the input), a pop (a burst of air on the mic),
a loud breath right before the line, or more background noise than your other takes. Each one lowers the total a
little, and hovering over the &#x26A0; says what it heard.</li>
</ul>
<p>The scores compare a take with your <b>other takes on the same track</b>, so the more takes you record of a part,
the better they get (five is a good start). A track's takes are never compared with a harmony on another track.</p>

<h2>Picking takes (Comp page)</h2>
<ul>
<li>The <b>Line</b> view shows every take of the current line with its scores and total. The ticked one is the take
in your comp: Comp Captain's best, or your own pick.</li>
<li><b>Click a take</b> to pick it for that line (a small dot by its name shows it's yours). Click your pick again
to let Comp Captain choose. <b>&#x2191; &#x2193;</b> (or Swap Take) step through the takes, best first;
<b>&#x2190; &#x2192;</b> move between lines. Undo and Redo work on your picks.</li>
<li><b>Line Scores</b> (right) shows the ticked take's scores and why: what's good, what could be better, and any
&#x26A0; problem.</li>
<li><b>AUTO COMP</b> clears your picks: Comp Captain's best take on every line.</li>
<li>The <b>Best Comp</b> lane shows which take each line uses, along the song. Hearing it and making it into one
finished performance comes next (M4).</li>
</ul>

<h2>Review and Style</h2>
<p>The <b>Review</b> page lists every line with the take it uses and its scores. Lines with a &#x26A0; or a total
under 75% are highlighted: click one to go and fix it. The <b>Style</b> (right) decides how much each score counts:
Rap and Trap lean on Timing, Clarity and Energy (Rap doesn't count Pitch at all); singing styles lean on Pitch. Move
a slider to make your own. When you've made picks, the Assistant says how often yours match Comp Captain's: tell us
that number when you test it. The scores advise; you decide. A take you mark with a heart wins ties.</p>

<h2>Works on its own, even better with Sonicology</h2>
<p>Export works the same for everyone: clearly named WAV files that land on the right bar in any DAW. If Voxology
or Mixology is installed, Send buttons appear so the roles (lead, double, ad-lib...) and lyrics go straight over.</p>

<h2>Light and dark</h2>
<p>Click the logo at the top to switch between the light and dark look.</p>

<h2>Keeping your session light</h2>
<p>Comp Captain never does heavy work on the audio thread. While you record, the audio thread only copies the sound
into memory; a background thread writes it to disk. On its track it passes the audio through untouched.</p>
`;
