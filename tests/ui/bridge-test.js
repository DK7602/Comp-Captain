// The window's messages to and from the plug-in, checked in a real browser (Chromium via Playwright).
// A fake plug-in side sends what the C++ code sends and records what the window sends back.
//   cd tests/ui && npm install && npx playwright install chromium && node bridge-test.js
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1672, height: 941 } });
  const errors = [];
  p.on('pageerror', (e) => errors.push(e.message));
  p.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await p.addInitScript(() => {
    const listeners = {};
    window.__sent = [];
    window.__JUCE__ = { backend: {
      addEventListener: (id, fn) => { (listeners[id] = listeners[id] || []).push(fn); },
      emitEvent: (id, msg) => { window.__sent.push(msg); },
    } };
    window.__fromPlugin = (msg) => (listeners.cc || []).forEach((fn) => fn(msg));
  });
  await p.goto(require('url').pathToFileURL(require('path').join(__dirname, '..', '..', 'ui', 'index.html')).href);
  const send = (m) => p.evaluate((m) => window.__fromPlugin(m), m);
  const sent = () => p.evaluate(() => window.__sent.splice(0));
  const check = (ok, what) => { console.log((ok ? '  PASS  ' : '  FAIL  ') + what); if (!ok) process.exitCode = 1; };

  let out = await sent();
  check(out.some((m) => m.type === 'ready'), 'the window says it is ready');
  await send({ type: 'init', theme: 'dark', page: 'record', compView: 'line', version: '0.2.0', host: 'Test', sonicology: { voxology: 'missing', mixology: 'missing' }, ara: false, standalone: false, takesRoot: 'C:/Users/Me/Documents/Comp Captain' });
  await send({ type: 'transport', known: true, playing: false, recording: false, ppq: 32, bpm: 92, num: 4, den: 4 });
  await send({ type: 'capture', armed: false, recording: false, loopTakes: true, ara: false });
  await send({ type: 'tracks', tracks: [{ id: '7', name: 'Lead Vox', role: 'lead', takes: 2, ara: false, self: true, viewing: true }] });
  const take = (id, name, bar, extra) => Object.assign({ id, name, role: 'lead', roleChosen: false, fav: false, source: 'capture', bar, beat: 1, barText: String(bar), ppq: (bar - 1) * 4, lenQ: 40, startSec: 0, lenSec: 26, bpm: 92, meter: '4/4', rate: 48000, channels: 1, track: 'Lead Vox', file: name + '.wav', original: '', missing: false, hasPeaks: true }, extra || {});
  await send({ type: 'takes', takes: [take('t1', 'Take 1', 9), take('t2', 'Take 2', 9, { fav: true })], track: 'self', trackName: 'Lead Vox', trackRole: 'lead', folder: 'C:/Users/Me/Documents/Comp Captain/2026-11-23 Lead Vox (4f1c)', importing: 0 });
  out = await sent();
  const ask = out.find((m) => m.type === 'getPeaks');
  check(ask && ask.ids.join() === 't1,t2', 'asks the plug-in for both waveforms');
  const bytes = Buffer.from(Array.from({ length: 2600 }, (_, i) => Math.round(127 + 120 * Math.sin(i / 9)))).toString('base64');
  await send({ type: 'peaks', id: 't1', perSecond: 100, data: bytes });
  await send({ type: 'peaks', id: 't2', perSecond: 100, data: bytes });
  await p.waitForTimeout(100);
  const rows = await p.$$eval('#take-list .tl-row.real', (r) => r.map((x) => x.querySelector('.nm').textContent + '|' + x.querySelector('.at').textContent));
  check(rows.join() === 'Take 1|bar 9,Take 2|bar 9', 'the take list shows both takes at bar 9 (' + rows.join(', ') + ')');
  check((await p.textContent('#rec-bpm')) === '92' && (await p.textContent('#rec-sig')) === '4/4', 'BPM and time signature come from the DAW');
  check((await p.textContent('#takes-count')) === '(2)', 'the count says 2');

  // arm
  await p.click('#r-arm');
  out = await sent();
  check(out.some((m) => m.type === 'arm' && m.on === true), 'the red button arms CAPTURE');
  await send({ type: 'capture', armed: true, recording: true, loopTakes: true, ara: false, live: { serial: 1, name: 'Take 3', ppq: 32, bpm: 92, bar: 9, barText: '9', seconds: 2.5, from: 0, perSecond: 100, data: bytes } });
  await p.waitForTimeout(100);
  check(await p.$eval('#r-arm', (e) => e.classList.contains('recording')), 'the red button pulses while a take records');
  check((await p.textContent('#take-list .tl-row.recording .nm')) === 'Take 3', 'the take being recorded shows in the list');
  check((await p.textContent('#msg-title')).startsWith('Recording Take 3 from bar 9'), 'the Assistant says what is recording: ' + (await p.textContent('#msg-title')));
  // a take where nothing comes through: it says so while recording, and why it wasn't kept afterwards
  const quiet = Buffer.from(new Uint8Array(300)).toString('base64');
  await send({ type: 'capture', armed: true, recording: true, loopTakes: true, ara: false, live: { serial: 2, name: 'Take 3', ppq: 32, bpm: 92, bar: 9, barText: '9', seconds: 3.0, from: 0, perSecond: 100, data: quiet } });
  await p.waitForTimeout(100);
  check((await p.textContent('#take-list')).includes('Comp Captain hears nothing on this track'), 'recording silence: the take list says nothing is coming through');
  check((await p.textContent('#msg-title')).includes('hears nothing'), 'and so does the Assistant');
  await send({ type: 'capture', armed: true, recording: false, loopTakes: true, ara: false });
  await send({ type: 'news', items: [{ kind: 'discarded', text: 'Take 3 was silent, so it wasn\'t kept.' }] });
  await p.waitForTimeout(100);
  check((await p.textContent('#take-list')).includes('Take 3 was silent') && (await p.textContent('#msg-title')).includes('Take 3 was silent'), 'a pass that wasn\'t kept stays explained while CAPTURE is armed');
  await send({ type: 'capture', armed: true, recording: false, loopTakes: true, ara: false });
  await send({ type: 'news', items: [{ kind: 'saved', text: 'Take 3 saved: starts at bar 9, 0:26 long.' }] });
  await p.waitForTimeout(100);
  check((await p.textContent('#toast')).includes('Take 3 saved'), 'news pops up');
  check(!(await p.textContent('#take-list')).includes('was silent'), 'a saved take clears the warning');

  // take actions
  await p.click('#take-list .tl-row.real .fav-btn');
  out = await sent();
  check(out.some((m) => m.type === 'take' && m.op === 'fav' && m.id === 't1' && m.value === true), 'the heart sends a favourite');
  await p.click('#take-list .tl-row.real .role-chip');
  await p.click('.menu .menu-item:has-text("Double")');
  out = await sent();
  check(out.some((m) => m.type === 'take' && m.op === 'role' && m.id === 't1' && m.value === 'double'), 'the role chip changes the role');
  await p.click('#take-list .tl-row.real .more-btn');
  await p.click('.menu .menu-item:has-text("Rename")');
  await p.fill('#dlg-name', 'Keeper');
  await p.click('#dlg-ok');
  out = await sent();
  check(out.some((m) => m.type === 'take' && m.op === 'rename' && m.value === 'Keeper'), 'rename sends the new name');
  await p.click('#take-list .tl-row.real .more-btn');
  await p.click('.menu .menu-item:has-text("Remove")');
  await p.click('#dlg-ok');
  out = await sent();
  check(out.some((m) => m.type === 'take' && m.op === 'remove' && m.id === 't1'), 'remove asks first, then sends');

  // track picker
  await send({ type: 'tracks', tracks: [{ id: '7', name: 'Lead Vox', role: 'lead', takes: 2, self: true, viewing: true }, { id: '9', name: 'Vox Dbl', role: 'double', takes: 3 }] });
  await p.click('#track-select');
  await p.click('.menu .menu-item:has-text("Vox Dbl")');
  out = await sent();
  check(out.some((m) => m.type === 'viewTrack' && m.id === '9'), 'the track picker shows another track\'s takes');

  // drop a file: bytes go over in pieces, then the placement
  await p.evaluate(() => {
    const data = new Uint8Array(1300 * 1024); for (let i = 0; i < data.length; i++) data[i] = i % 251;
    const file = new File([data], 'Vox Dbl L.wav', { type: 'audio/wav' });
    const dt = new DataTransfer(); dt.items.add(file);
    const stage = document.getElementById('stage');
    stage.dispatchEvent(new DragEvent('dragenter', { dataTransfer: dt, bubbles: true }));
    stage.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true }));
  });
  check(await p.isVisible('#dlg-bar'), 'dropping a file asks where it starts');
  check((await p.$eval('.dlg-select', (e) => e.options[0].textContent)) === 'Guess: Double', 'it guesses the role from the name');
  await p.fill('#dlg-bar', '17');
  await p.click('#dlg-ok');
  await p.waitForTimeout(50);
  out = await sent();
  const begin = out.find((m) => m.type === 'dropBegin');
  check(begin && begin.name === 'Vox Dbl L.wav' && begin.size === 1300 * 1024, 'it starts sending the file');
  await send({ type: 'dropReady', ref: begin.ref, upload: 5 });
  let received = [];
  for (let k = 0; k < 5; k++) {
    await p.waitForTimeout(50);
    out = await sent();
    const chunk = out.find((m) => m.type === 'dropChunk');
    const end = out.find((m) => m.type === 'dropEnd');
    if (chunk) { received.push(Buffer.from(chunk.data, 'base64')); await send({ type: 'dropAck', upload: 5, received: 0, ok: true }); }
    if (end) { check(end.bar === 17 && end.bpm === 92 && end.upload === 5, 'then tells the plug-in to add it at bar 17, 92 BPM'); break; }
  }
  const all = Buffer.concat(received);
  check(all.length === 1300 * 1024 && all[1000] === 1000 % 251 && all[all.length - 1] === (all.length - 1) % 251, `the file arrives whole, in ${received.length} pieces`);

  // settings: ARA report
  await p.click('#btn-settings');
  await p.click('#set-copy');
  out = await sent();
  check(out.some((m) => m.type === 'copyReport'), 'Settings can copy the ARA report');
  await send({ type: 'report', text: 'What the DAW sent to Comp Captain (ARA)\nTakes: 3', copied: true });
  await p.waitForTimeout(50);
  check((await p.textContent('#set-report-text')).includes('Takes: 3'), 'the report shows in Settings');

  await p.click('#settings [data-close]');

  // ---------------------------------------------------------------- M2: lyrics
  await p.click('.nav-btn[data-go="lyrics"]');
  const lyricsText = '[Verse 1]\nI was lost in the dark\nbut You found me\n[Chorus]\nI\'m alive, I\'m alive\n';
  const line = (text, section, start, end, extra) => Object.assign({ text, section, start, end, startQ: start * 92 / 60, endQ: end * 92 / 60, sure: 0.9, takes: 2, from: 'Take 2 (Lead Vox)',
    words: text.split(' ').map((w, i, a) => [start + (end - start) * i / a.length, start + (end - start) * (i + 1) / a.length, 1]) }, extra || {});
  await send({ type: 'lyrics', text: lyricsText, linedUp: 2, sungTakes: 2, heardTakes: 2,
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, 21.0, 23.4), line('but You found me', 0, 24.0, 25.6), line('I\'m alive, I\'m alive', 1, 27.0, 29.0, { takes: 0, sure: 0, from: '' })] });
  await p.waitForTimeout(100);
  check((await p.inputValue('#lyr-text')) === lyricsText, 'the Lyrics box shows the song\'s lyrics');
  const lyrRows = await p.$$eval('#lyr-list .ll:not(.head)', (r) => r.map((x) => x.querySelector('.t').textContent + '|' + x.querySelector('.txt').textContent));
  check(lyrRows.join() === '9|I was lost in the dark,10|but You found me,11.2|I\'m alive, I\'m alive', 'each line shows the bar it\'s sung at (' + lyrRows.join(', ') + ')');
  check((await p.$$eval('#lyr-list .ll.head .sec', (r) => r.map((x) => x.textContent))).join() === 'VERSE 1,CHORUS', 'lines are grouped in their sections');
  check(await p.$eval('#lyr-list .ll:not(.head):nth-last-child(1)', (e) => e.classList.contains('unheard')), 'a line no take sang is dimmed');
  check((await p.textContent('#lyr-status')).includes('3 lines \u00B7 2 sections'), 'Lyrics Status counts lines and sections');
  check((await p.$$eval('#section-chips .chip', (r) => r.map((x) => x.textContent))).join() === 'Verse 1,Chorus', 'the section strip shows the song\'s sections');

  await p.fill('#lyr-text', lyricsText + 'singing out\n');
  await p.click('#lyr-list');
  await p.waitForTimeout(600);
  out = await sent();
  check(out.some((m) => m.type === 'lyricsSet' && m.text.endsWith('singing out\n')), 'typing in the box sends the lyrics to the plug-in');
  await p.click('#lyr-open');
  out = await sent();
  check(out.some((m) => m.type === 'lyricsOpen'), 'Open File asks the plug-in to open a lyrics file');
  await p.click('#lyr-extras');
  await p.click('.menu .menu-item:has-text("Song Rider Pro")');
  out = await sent();
  check(out.some((m) => m.type === 'lyricsOpen' && m.from === 'songrider'), 'From My Songs opens a Song Rider Pro song');
  await send({ type: 'lyricsLoaded', name: 'Brand New Start.docx', text: '[Chorus]\nHallelujah\n' });
  await p.waitForTimeout(50);
  check((await p.inputValue('#lyr-text')) === '[Chorus]\nHallelujah\n' && await p.isVisible('#lyr-undo'), 'an opened file fills the box, with Undo');
  await sent();
  await p.click('#lyr-undo');
  out = await sent();
  check(out.some((m) => m.type === 'lyricsSet' && m.text.endsWith('singing out\n')), 'Undo brings the old lyrics back');
  await p.click('#lyr-hear');
  out = await sent();
  check(out.some((m) => m.type === 'hearIt'), 'Hear It asks the plug-in for the words it heard');
  await send({ type: 'hearIt', text: 'I was lost in the dark\n', from: 'Take 2' });
  await p.waitForTimeout(50);
  check(await p.isVisible('#dialog-body .dlg-title'), 'with lyrics already there, Hear It asks before replacing them');
  await p.click('#dialog-body button:has-text("Replace")');
  out = await sent();
  check(out.some((m) => m.type === 'lyricsSet' && m.text === 'I was lost in the dark\n'), 'Replace puts the heard words in');
  await send({ type: 'listening', now: 'Take 3 (Lead Vox)', percent: 40, waiting: 1, paused: false, problem: '' });
  await p.waitForTimeout(50);
  check((await p.textContent('#lyr-status')).includes('Listening to Take 3 (Lead Vox)\u2026 40%'), 'Lyrics Status shows the take being listened to');

  // before any take is lined up, the teleprompter says why the words don't follow yet
  await p.click('.nav-btn[data-go="record"]');
  await send({ type: 'lyrics', text: lyricsText, linedUp: 0, sungTakes: 1, heardTakes: 0,
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, -1, -1, { takes: 0 }), line('but You found me', 0, -1, -1, { takes: 0 }), line('I\'m alive, I\'m alive', 1, -1, -1, { takes: 0 })] });
  await p.waitForTimeout(120);
  check((await p.textContent('#p-prev')).includes('Listening to Take 3 (Lead Vox)'), 'not lined up yet: the teleprompter says the take is being listened to');
  await send({ type: 'listening', now: '', percent: 0, waiting: 0, paused: false, problem: '' });
  await send({ type: 'lyrics', text: lyricsText, linedUp: 0, sungTakes: 1, heardTakes: 1,
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, -1, -1, { takes: 0 }), line('but You found me', 0, -1, -1, { takes: 0 }), line('I\'m alive, I\'m alive', 1, -1, -1, { takes: 0 })] });
  await p.waitForTimeout(120);
  check((await p.textContent('#p-prev')).includes('didn\u2019t seem to sing these lyrics'), 'and says so when the take didn\'t sing these lyrics');
  await send({ type: 'lyrics', text: lyricsText, linedUp: 0, sungTakes: 1, heardTakes: 1, unmatched: [{ take: 'Take 1 (Verse Vox)', words: 4, heard: 'Happy birthday to you' }],
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, -1, -1, { takes: 0 }), line('but You found me', 0, -1, -1, { takes: 0 }), line('I\'m alive, I\'m alive', 1, -1, -1, { takes: 0 })] });
  await p.click('.nav-btn[data-go="lyrics"]');
  await p.waitForTimeout(100);
  check((await p.textContent('#lyr-status')).includes('Take 1 (Verse Vox) heard: \u201CHappy birthday to you\u201D'), 'Lyrics Status shows what an unmatched take heard');
  await p.click('.nav-btn[data-go="record"]');
  await p.click('#btn-settings');
  await p.click('#set-lcopy');
  out = await sent();
  check(out.some((m) => m.type === 'copyListenReport'), 'Settings can copy the lyrics and listening report');
  await p.click('#settings [data-close]');

  // the teleprompter follows the DAW
  await send({ type: 'lyrics', text: lyricsText, linedUp: 2, sungTakes: 2, heardTakes: 2,
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, 21.0, 23.4), line('but You found me', 0, 24.0, 25.6), line('I\'m alive, I\'m alive', 1, 27.0, 29.0)] });
  await p.click('.nav-btn[data-go="record"]');
  await send({ type: 'transport', known: true, playing: true, recording: false, ppq: 24.0 * 92 / 60, seconds: 24.6, bpm: 92, num: 4, den: 4 });
  await p.waitForTimeout(120);
  check((await p.textContent('#p-now')) === 'but You found me', 'the teleprompter shows the line being sung');
  check((await p.textContent('#p-prev')) === 'I was lost in the dark' && (await p.textContent('#p-next')) === 'I\'m alive, I\'m alive', 'with the line before above it and the next below');
  check((await p.$$eval('#p-now .sung', (r) => r.length)) >= 1, 'the words already sung light up');
  check((await p.textContent('#rec-section')) === 'Verse 1', 'the section follows the song');
  await send({ type: 'transport', known: true, playing: true, recording: false, ppq: 19.4 * 92 / 60, seconds: 19.4, bpm: 92, num: 4, den: 4 });
  await p.waitForTimeout(120);
  check(await p.$eval('#p-prev', (e) => e.classList.contains('counting') && !!e.querySelector('.count')), 'the beats before the first line count down');
  await send({ type: 'transport', known: true, playing: false, recording: false, ppq: 27.5 * 92 / 60, seconds: 27.5, bpm: 92, num: 4, den: 4 });
  await p.waitForTimeout(120);
  check((await p.textContent('#p-now')) === 'I\'m alive, I\'m alive', 'stopped: it shows the line where the playhead is');

  // beat scroll: before any take is listened to, the words move on with the beat from where Play was pressed
  await send({ type: 'transport', known: true, playing: false, recording: false, ppq: 32, seconds: 20, bpm: 400, num: 4, den: 4 });
  await send({ type: 'listening', now: '', percent: 0, waiting: 0, paused: false, problem: '' });
  await send({ type: 'lyrics', text: lyricsText, linedUp: 0, sungTakes: 0, heardTakes: 0, unmatched: [],
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, -1, -1, { takes: 0 }), line('but You found me', 0, -1, -1, { takes: 0 }), line('I\'m alive, I\'m alive', 1, -1, -1, { takes: 0 })] });
  await p.click('.nav-btn[data-go="record"]');
  await p.click('#rec-sec-prev'); await p.click('#rec-sec-prev');
  await p.waitForTimeout(100);
  check((await p.textContent('#p-prev')).includes('Before your first take: a new line every 2 bars'), 'before the first take, the teleprompter says the words move with the beat');
  check((await p.textContent('#rec-scroll-text')) === 'Every 2 bars', 'the Lines button shows 2 bars a line');
  await send({ type: 'transport', known: true, playing: true, recording: true, ppq: 32, seconds: 20, bpm: 400, num: 4, den: 4 });
  await p.waitForTimeout(150);
  check((await p.textContent('#p-now')) === 'I was lost in the dark' && await p.$eval('#p-prev', (e) => !!e.querySelector('.count')), 'pressing Play shows the first line, with the beats counting in');
  await p.waitForTimeout(1900);   // 400 BPM: a bar of count-in, then 2 bars a line
  check((await p.textContent('#p-now')) === 'but You found me', 'two bars later the next line comes up');
  check((await p.textContent('#p-next')) === 'I\'m alive, I\'m alive', 'with the line after it underneath');
  await send({ type: 'transport', known: true, playing: true, recording: true, ppq: 32, seconds: 20, bpm: 400, num: 4, den: 4 });
  await p.waitForTimeout(150);
  check((await p.textContent('#p-now')) === 'I was lost in the dark', 'the loop coming round starts the words again');
  await send({ type: 'transport', known: true, playing: false, recording: false, ppq: 40, seconds: 21, bpm: 400, num: 4, den: 4 });
  await p.click('#rec-scroll');
  await p.click('.menu .menu-item:has-text("4 bars")');
  check((await p.textContent('#rec-scroll-text')) === 'Every 4 bars', 'the Lines button changes the bars per line');

  // ---------------------------------------------------------------- M3: scores
  await send({ type: 'tracks', tracks: [{ id: '7', name: 'Lead Vox', role: 'lead', takes: 3, ara: false, self: true, viewing: true }] });
  await send({ type: 'takes', takes: [take('t1', 'Take 1', 9), take('t2', 'Take 2', 9, { fav: true }), take('t3', 'Take 3', 9)], track: 'self', trackName: 'Lead Vox', trackRole: 'lead', folder: 'C:/x', importing: 0 });
  await send({ type: 'lyrics', text: lyricsText, linedUp: 3, sungTakes: 3, heardTakes: 3, unmatched: [],
    sections: [{ name: 'Verse 1', first: 0, count: 2 }, { name: 'Chorus', first: 2, count: 1 }],
    lines: [line('I was lost in the dark', 0, 21.0, 23.4), line('but You found me', 0, 24.0, 25.6), line('I\'m alive, I\'m alive', 1, 27.0, 29.0)] });
  const sc = (id, p, t, c, e, extra) => Object.assign({ id, p, t, c, e, good: [], bad: [], problems: [] }, extra || {});
  await send({ type: 'scores', ready: true, any: true, style: 'Singing', weights: [0.35, 0.25, 0.2, 0.2], picks: {},
    lines: [{ line: 0, takes: [sc('t1', 90, 90, 90, 90, { bad: ['Flat on "dark" (about 40 cents)'] }), sc('t2', 95, 95, 95, 95, { good: ['Every word comes through clearly'] }), sc('t3', 97, 97, 97, 97, { problems: ['Clipped on "lost" (too loud for the input)'] })] },
            { line: 1, takes: [sc('t1', 99, 99, 99, 99), sc('t2', 80, 80, 80, 80)] }] });
  await p.click('.nav-btn[data-go="comp"]');
  await p.click('#view-seg button[data-view="line"]');
  await p.click('#section-chips .chip:first-child');
  await p.waitForTimeout(100);
  const takeRows = await p.$$eval('#take-rows .take-row', (r) => r.map((x) => x.querySelector('.name').textContent + '|' + x.querySelector('.total').textContent + '|' + x.classList.contains('chosen')));
  check(takeRows.join() === 'Take 1|90%|false,Take 2|95%|true,Take 3|94%|false', 'the Comp page shows every take of the line with its total, the best one ticked (' + takeRows.join(', ') + ')');
  check((await p.textContent('#take-rows .take-row:nth-child(3) .warn-badge')) !== '', 'a take with a problem shows the warning');
  check((await p.textContent('#reasons')).includes('Every word comes through clearly') && (await p.textContent('#sc-best')).includes('Best: Take 2'), 'Line Scores shows the chosen take and why');
  await sent();
  await p.click('#take-rows .take-row:nth-child(1) .name');
  out = await sent();
  check(out.some((m) => m.type === 'pick' && m.line === 0 && m.id === 't1' && m.text === 'I was lost in the dark'), 'clicking a take picks it for the line');
  await p.waitForTimeout(50);
  check(await p.$eval('#take-rows .take-row:nth-child(1)', (e) => e.classList.contains('chosen') && e.classList.contains('mine')), 'your pick is ticked');
  await p.click('#undo');
  out = await sent();
  check(out.some((m) => m.type === 'picksSet' && Object.keys(m.picks).length === 0), 'Undo takes the pick back');
  await p.click('#comp-style');
  await p.click('.menu .menu-item:has-text("Rap")');
  await p.waitForTimeout(350);
  out = await sent();
  check(out.some((m) => m.type === 'style' && m.name === 'Rap' && m.weights[0] === 0), 'the style menu sends Rap (pitch doesn\'t count)');
  check((await p.textContent('#take-rows .take-row:nth-child(2) .total')) === '95%' && (await p.textContent('#comp-style-name')) === 'Rap', 'the totals are re-weighed at once');
  await p.click('.nav-btn[data-go="review"]');
  await p.waitForTimeout(100);
  const reviewRows = await p.$$eval('#review-rows .rv', (r) => r.map((x) => x.querySelector('.tg') ? x.querySelector('.tg').textContent : '-'));
  check(reviewRows.join() === 'T2,T1,-', 'Review lists every line with its chosen take (' + reviewRows.join(', ') + ')');
  await send({ type: 'scores', ready: true, any: true, style: 'Rap', weights: [0, 0.35, 0.35, 0.3], picks: { 0: { id: 't1', text: 'I was lost in the dark' }, 1: { id: 't1', text: 'but You found me' } },
    lines: [{ line: 0, takes: [sc('t1', 90, 90, 90, 90), sc('t2', 95, 95, 95, 95)] }, { line: 1, takes: [sc('t1', 99, 99, 99, 99), sc('t2', 80, 80, 80, 80)] }] });
  await p.waitForTimeout(100);
  check((await p.textContent('#msg-title')).includes('matches Comp Captain\u2019s on 1 of 2 lines'), 'Review counts how often your picks match Comp Captain\'s');
  await sent();
  await p.click('.nav-btn[data-go="comp"]');
  await p.click('#auto-comp');
  out = await sent();
  check(out.some((m) => m.type === 'picksSet' && Object.keys(m.picks).length === 0), 'Auto Comp lets Comp Captain pick every line');

  check(errors.length === 0, 'no script errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await b.close();
})();
