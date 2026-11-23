// The in-app manual, in plain words. Keep it up to date whenever behaviour changes.
'use strict';

const Manual = `
<h2>What Comp Captain does</h2>
<p>You sing a part five or ten times. Comp Captain listens to every take, scores every line, and builds one great
performance from the best lines. You approve every pick. Then it lines up your doubles, ad-libs, backing vocals and
doubled guitars so they sit tight with the lead, and hands the finished parts back to your DAW.</p>
<p><b>This version is the look only (M0).</b> Everything you see uses a made-up song and made-up takes so you can try
the pages. The plug-in passes your audio through untouched. Recording, scoring and comping arrive in the next
versions; parts that don't work yet are marked <span class="soon">SOON</span>.</p>

<h2>The pages (left side)</h2>
<ul>
<li><b>Record</b>: the teleprompter. Your lyrics scroll in time with the beat, karaoke style. <span class="soon">SOON</span></li>
<li><b>Lyrics</b>: get the words in, then Comp Captain lines each line up with the audio. <span class="soon">SOON</span></li>
<li><b>Comp</b>: every take of every line with its scores. Pick the best, or let Auto Comp pick. <span class="soon">SOON</span></li>
<li><b>Tighten</b>: line up doubles, ad-libs, backing vocals and guitars with the lead. <span class="soon">SOON</span></li>
<li><b>Review</b>: scores for the whole song, and the Style that decides what matters most. <span class="soon">SOON</span></li>
<li><b>Export</b>: make the finished audio files, or drag them straight into your DAW. <span class="soon">SOON</span></li>
</ul>

<h2>Recording (how it will work)</h2>
<p>Put Comp Captain on your vocal track and press the big red button: it glows to show it's <b>armed</b>. A plug-in
can't press your DAW's Record button, so you start recording in your DAW as usual. Every pass you sing becomes a new
take, stamped with the bar it started on. With <b>Loop Takes</b> on, every time your DAW's loop comes round you get
a fresh take.</p>

<h2>Getting lyrics in</h2>
<ul>
<li><b>Paste</b> or type them. Put <b>[Verse 1]</b>, <b>[Chorus]</b> and so on above each part to make sections.</li>
<li><b>Open File</b>: a .txt, a Word .docx, or an .lrc karaoke file.</li>
<li><b>Hear It</b>: no lyrics written down? Comp Captain writes them from your takes, and you fix any wrong words.</li>
<li><b>From My Songs</b>: if you have Song Rider Pro, Song Savant or Key Compass, open a song from them.</li>
<li>No lyrics at all is fine too: it splits the takes into phrases at the breaths.</li>
</ul>

<h2>The scores</h2>
<ul>
<li><b>Pitch</b>: on the notes. Off for rap.</li>
<li><b>Timing</b>: on the beat, and with your other takes.</li>
<li><b>Clarity</b>: the words came through clearly and correctly.</li>
<li><b>Energy</b>: the delivery, compared with your other takes of the same line.</li>
<li><b>⚠</b>: a problem in the recording, like a pop, clipping or a loud breath. It says what it heard.</li>
</ul>
<p>The <b>Style</b> (Rap, Pop, Country, Worship / Gospel...) decides how much each score counts. The scores advise;
you decide. A take you mark with a heart wins ties.</p>

<h2>Doubles on other tracks</h2>
<p>Put a Comp Captain on each vocal track (lead, doubles, ad-libs, backing). They find each other inside the same
project, and the track picker at the top shows them all in one window. <span class="soon">SOON</span></p>

<h2>Works on its own, even better with Sonicology</h2>
<p>Export works the same for everyone: clearly named WAV files that land on the right bar in any DAW. If Voxology
or Mixology is installed, Send buttons appear so the roles (lead, double, ad-lib...) and lyrics go straight over.</p>

<h2>Light and dark</h2>
<p>Click the logo at the top to switch between the light and dark look.</p>

<h2>Keeping your session light</h2>
<p>Comp Captain never does heavy work while your song plays: listening and scoring run in the background on at most
half your computer's cores, once per take. On its track it passes the audio through untouched.</p>
`;
