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
  await send({ type: 'capture', armed: true, recording: false, loopTakes: true, ara: false });
  await send({ type: 'news', items: [{ kind: 'saved', text: 'Take 3 saved: starts at bar 9, 0:26 long.' }] });
  await p.waitForTimeout(100);
  check((await p.textContent('#toast')).includes('Take 3 saved'), 'news pops up');

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

  check(errors.length === 0, 'no script errors' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await b.close();
})();
