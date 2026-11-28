// Real takes (M1): the take list, CAPTURE, waveforms on the song's bars, the track picker and adding
// files (the Add Files button or drag and drop). Inside the plug-in everything comes from the C++ side;
// in a plain browser, add ?real to the address to try it with made-up takes.
'use strict';

const Takes = (() => {
  const ROLES = [
    ['lead', 'Lead', '#ffc531'], ['double', 'Double', '#b05cff'], ['harmony', 'Harmony', '#2bea7a'],
    ['adlib', 'Ad-lib', '#ff5fc4'], ['backing', 'Backing', '#6b7bff'], ['guitar', 'Guitar', '#4df0e0'], ['other', 'Other', '#9aa7c7'],
  ];
  const roleLabel = (r) => (ROLES.find((x) => x[0] === r) || ROLES[0])[1];
  const roleColor = (r) => (ROLES.find((x) => x[0] === r) || ROLES[0])[2];
  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const fmtLen = (sec) => { const s = Math.round(sec || 0); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`; };

  const mock = !Bridge.inHost && /(^|[?&#])real\b/.test(location.search + location.hash);
  const st = {
    real: Bridge.inHost || mock,
    mock,
    list: [], track: 'self', trackName: '', trackRole: 'lead', folder: '', importing: 0,
    tracks: [],
    peaks: {}, asked: {},
    transport: { known: false, playing: false, recording: false, ppq: 0, bpm: 120, num: 4, den: 4, at: 0 },
    capture: { armed: false, recording: false, loopTakes: true, ara: false, live: null },
    live: { serial: 0, data: new Uint8Array(4096), len: 0 },
    sel: null,
    notKept: '',          // why the last recorded pass wasn't kept (stays until a take is saved)
    ara: false, standalone: false, takesRoot: '',
    report: '',
  };

  // ------------------------------------------------------------------ change notices for app.js
  const listeners = [];
  const changed = (what) => listeners.forEach((fn) => fn(what));
  const onChange = (fn) => listeners.push(fn);
  let toastFn = () => {};
  const setToast = (fn) => { toastFn = fn; };
  let lastNews = null;

  // ------------------------------------------------------------------ waiting for a reply from the plug-in
  const waiters = [];
  function waitFor(type, test, ms) {
    return new Promise((resolve, reject) => {
      const w = { type, test, resolve };
      waiters.push(w);
      setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); reject(new Error('no reply')); } }, ms || 20000);
    });
  }
  function settle(type, m) {
    for (let i = 0; i < waiters.length; i++) {
      const w = waiters[i];
      if (w.type === type && w.test(m)) { waiters.splice(i, 1); w.resolve(m); return; }
    }
  }

  // ------------------------------------------------------------------ base64
  function decode(b64) {
    const s = atob(b64 || ''); const a = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i);
    return a;
  }
  function encode(bytes) {
    let s = '';
    for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
  }

  // ------------------------------------------------------------------ messages from the plug-in
  Bridge.on('takes', (m) => {
    st.list = Array.isArray(m.takes) ? m.takes : [];
    st.track = m.track; st.trackName = m.trackName || ''; st.trackRole = m.trackRole || 'lead';
    st.folder = m.folder || ''; st.importing = m.importing || 0;
    if (!st.list.find((t) => t.id === st.sel)) st.sel = st.list.length ? st.list[st.list.length - 1].id : null;
    const want = st.list.filter((t) => t.hasPeaks && !st.peaks[t.id] && !st.asked[t.id]).map((t) => t.id);
    want.forEach((id) => { st.asked[id] = true; });
    if (want.length) Bridge.send('getPeaks', { ids: want });
    changed('takes');
  });
  Bridge.on('peaks', (m) => { st.peaks[m.id] = { data: decode(m.data), perSecond: m.perSecond || 100 }; changed('peaks'); });
  Bridge.on('tracks', (m) => { st.tracks = Array.isArray(m.tracks) ? m.tracks : []; changed('tracks'); });
  Bridge.on('transport', (m) => {
    Object.assign(st.transport, m, { at: performance.now() });
    changed('transport');
  });
  Bridge.on('capture', (m) => {
    const wasRecording = st.capture.recording;
    st.capture.armed = !!m.armed; st.capture.recording = !!m.recording;
    st.capture.loopTakes = m.loopTakes !== false; st.capture.ara = !!m.ara;
    st.capture.live = m.live || null;
    if (m.live) {
      if (m.live.serial !== st.live.serial) { st.live.serial = m.live.serial; st.live.len = 0; }
      if (m.live.data) {
        const add = decode(m.live.data), at = m.live.from || 0;
        if (at + add.length > st.live.data.length) { const bigger = new Uint8Array(Math.max(st.live.data.length * 2, at + add.length + 4096)); bigger.set(st.live.data.subarray(0, st.live.len)); st.live.data = bigger; }
        st.live.data.set(add, at); st.live.len = Math.max(st.live.len, at + add.length);
      }
    }
    changed(wasRecording !== st.capture.recording ? 'recording' : 'capture');
  });
  Bridge.on('news', (m) => {
    (m.items || []).forEach((n) => {
      lastNews = n;
      if (n.kind === 'discarded' || n.kind === 'failed') st.notKept = n.text;
      else if (n.kind === 'saved' || n.kind === 'imported') st.notKept = '';
      toastFn(n.text, n.kind === 'failed' || n.kind === 'discarded' ? 7000 : 3500);
    });
    changed('news');
  });
  Bridge.on('report', (m) => { st.report = m.text || ''; if (m.copied) toastFn('Copied. Paste it into a chat or an email.'); changed('report'); });
  ['dropReady', 'dropAck', 'chosen'].forEach((type) => Bridge.on(type, (m) => settle(type, m)));
  Bridge.on('chosen', (m) => placementDialog({ token: m.token, files: m.files || [] }));
  Bridge.on('init', (m) => { st.ara = !!m.ara; st.standalone = !!m.standalone; st.takesRoot = m.takesRoot || ''; changed('init'); });

  // ------------------------------------------------------------------ actions
  function act(op, id, value) {
    if (st.mock) return mockAct(op, id, value);
    Bridge.send('take', { op, id, value });
  }
  const setArmed = (on) => { if (st.mock) { st.capture.armed = on; changed('capture'); return; } Bridge.send('arm', { on }); };
  const setLoopTakes = (on) => { if (st.mock) { st.capture.loopTakes = on; changed('capture'); return; } Bridge.send('loopTakes', { on }); };
  const viewTrack = (id) => { if (st.mock) { st.tracks.forEach((t) => { t.viewing = t.id === id; }); changed('tracks'); return; } Bridge.send('viewTrack', { id }); };
  const setTrackRole = (role) => { if (st.mock) { st.trackRole = role; const me = st.tracks.find((t) => t.viewing); if (me) me.role = role; changed('tracks'); return; } Bridge.send('trackRole', { value: role }); };
  const openFolder = () => { if (st.mock) return toastFn('In the plug-in this opens the takes folder.'); Bridge.send('openFolder', {}); };
  const askReport = (copy) => { if (st.mock) { st.report = 'What the DAW sent to Comp Captain (ARA)\n(in the plug-in this shows what Cubase shared)'; changed('report'); return; } Bridge.send(copy ? 'copyReport' : 'getReport', {}); };
  function addFiles() {
    if (st.mock) return placementDialog({ token: 1, files: [{ name: 'Vox Dbl R', guess: 'double' }, { name: 'Harmony High', guess: 'harmony' }] });
    Bridge.send('addFiles', {});
  }
  const select = (id) => { st.sel = id; changed('select'); };
  const selected = () => st.list.find((t) => t.id === st.sel) || null;
  const viewingSelf = () => st.track === 'self';

  // ------------------------------------------------------------------ the song's bars
  const quartersPerBar = () => { const t = st.transport; return (t.num > 0 && t.den > 0) ? t.num * 4 / t.den : 4; };
  function playheadPpq() {
    const t = st.transport;
    if (!t.playing) return t.ppq;
    return t.ppq + (performance.now() - t.at) / 1000 * t.bpm / 60;
  }
  function liveTake() {
    const l = st.capture.live;
    if (!st.capture.recording || !l) return null;
    return { id: 'live', name: l.name, role: st.trackRole, ppq: l.ppq, lenQ: (l.seconds || 0) * (l.bpm || 120) / 60, bpm: l.bpm, barText: l.barText, live: true };
  }
  // [first quarter, last quarter] on bar lines, around the given takes (at least 4 bars).
  function span(takes) {
    const qpb = quartersPerBar();
    if (!takes.length) { const b = Math.floor(Math.max(0, playheadPpq()) / qpb); return [b * qpb, (b + 4) * qpb]; }
    let a = Infinity, b = -Infinity;
    takes.forEach((t) => { a = Math.min(a, t.ppq); b = Math.max(b, t.ppq + Math.max(t.lenQ, 0.01)); });
    let q0 = Math.floor(a / qpb + 1e-6) * qpb, q1 = Math.ceil(b / qpb - 1e-6) * qpb;
    if (q1 - q0 < 4 * qpb) q1 = q0 + 4 * qpb;
    return [q0, q1];
  }
  // How loud a take is between quarter q and q + dq (0..1), or -1 where it isn't playing.
  function envFor(take) {
    const live = take.live;
    const p = live ? { data: st.live.data, len: st.live.len, perSecond: 100 } : st.peaks[take.id];
    const perQ = 60 / (take.bpm || 120) * (p ? p.perSecond : 100); // peaks per quarter note
    return (q, dq) => {
      if (q + dq < take.ppq || q > take.ppq + take.lenQ) return -1;
      if (!p) return 0.12; // still drawing it
      const n = live ? p.len : p.data.length;
      let i0 = Math.floor((q - take.ppq) * perQ), i1 = Math.ceil((q + dq - take.ppq) * perQ);
      i0 = Math.max(0, i0); i1 = Math.min(n, Math.max(i1, i0 + 1));
      let m = 0;
      for (let i = i0; i < i1; i++) if (p.data[i] > m) m = p.data[i];
      return i0 >= n ? -1 : m / 255;
    };
  }
  // Bar numbers over a ruler element: "9  10  11 ..." (or "9 9.2 9.3 ..." when zoomed in).
  function drawBars(el, q0, q1, opts) {
    el.innerHTML = '';
    const qpb = quartersPerBar(), bars = (q1 - q0) / qpb, beats = Math.round(qpb / (4 / st.transport.den || 1)) || 4;
    const withBeats = bars <= 6;
    const stepBars = bars <= 12 ? 1 : bars <= 24 ? 2 : bars <= 48 ? 4 : 8;
    const lines = opts && opts.lines;
    for (let b = Math.round(q0 / qpb); b * qpb <= q1 + 1e-6; b++) {
      const x = (b * qpb - q0) / (q1 - q0) * 100;
      if (lines) { const ln = document.createElement('i'); ln.style.left = x + '%'; el.appendChild(ln); }
      if ((b - Math.round(q0 / qpb)) % stepBars === 0 && x < 99.5) {
        const s = document.createElement('span'); s.textContent = String(b + 1); s.style.left = x + '%'; s.className = 'bar-no' + (x < 1 ? ' first' : ''); el.appendChild(s);
      }
      if (withBeats) {
        for (let k = 1; k < beats; k++) {
          const xq = b * qpb + k * qpb / beats; if (xq >= q1) break;
          const s = document.createElement('span'); s.textContent = `${b + 1}.${k + 1}`; s.className = 'beat-no';
          s.style.left = ((xq - q0) / (q1 - q0) * 100) + '%'; el.appendChild(s);
        }
      }
    }
  }

  // ------------------------------------------------------------------ menus and small dialogs
  let menuImpl = null;
  const setMenu = (fn) => { menuImpl = fn; };
  function dialog(html, wire) {
    const ov = document.getElementById('dialog'), body = document.getElementById('dialog-body');
    body.innerHTML = html; ov.hidden = false;
    const close = () => { ov.hidden = true; body.innerHTML = ''; };
    body.querySelectorAll('[data-close]').forEach((b) => { b.onclick = close; });
    wire(body, close);
  }
  function confirmRemove(t) {
    dialog(`<div class="dlg-title">Remove ${esc(t.name)}?</div>
      <p>It leaves the take list. Its file isn't deleted: it moves to the <b>Removed takes</b> folder, in case you want it back.</p>
      <div class="dlg-buttons"><button class="pill" data-close>Keep it</button><button class="pill gold-btn" id="dlg-ok">Remove</button></div>`,
    (body, close) => { body.querySelector('#dlg-ok').onclick = () => { act('remove', t.id); close(); }; });
  }
  function rename(t) {
    dialog(`<div class="dlg-title">Rename ${esc(t.name)}</div>
      <input class="dlg-input" id="dlg-name" maxlength="80" value="${esc(t.name)}">
      <div class="dlg-buttons"><button class="pill" data-close>Cancel</button><button class="pill gold-btn" id="dlg-ok">Rename</button></div>`,
    (body, close) => {
      const input = body.querySelector('#dlg-name');
      const ok = () => { const v = input.value.trim(); if (v) act('rename', t.id, v); close(); };
      body.querySelector('#dlg-ok').onclick = ok;
      input.onkeydown = (e) => { if (e.key === 'Enter') ok(); if (e.key === 'Escape') close(); };
      setTimeout(() => { input.focus(); input.select(); }, 30);
    });
  }
  function roleMenu(anchor, t) {
    if (!menuImpl) return;
    menuImpl(anchor, ROLES.map((r) => ({ label: r[1], value: r[0], checked: r[0] === t.role, color: r[2] })), (it) => act('role', t.id, it.value));
  }
  function takeMenu(anchor, t) {
    if (!menuImpl) return;
    const ara = t.source === 'ara';
    const items = [];
    if (!ara) items.push({ label: 'Rename\u2026', value: 'rename' });
    items.push({ label: t.fav ? 'Remove the heart' : 'Mark as favourite \u2665', value: 'fav' });
    items.push({ label: 'Role\u2026', value: 'role' });
    if (!ara) items.push({ label: 'Show the takes folder', value: 'folder' });
    if (!ara) items.push({ label: 'Remove\u2026', value: 'remove' });
    menuImpl(anchor, items, (it) => {
      if (it.value === 'rename') rename(t);
      else if (it.value === 'fav') act('fav', t.id, !t.fav);
      else if (it.value === 'role') roleMenu(anchor, t);
      else if (it.value === 'folder') openFolder();
      else if (it.value === 'remove') confirmRemove(t);
    });
  }

  // ------------------------------------------------------------------ the take list (Record page, right side)
  function renderList(box) {
    box.innerHTML = '';
    const live = liveTake();
    const takes = st.list.slice();
    if (!takes.length && !live) {
      box.innerHTML = `<div class="tl-empty">${st.capture.ara || st.ara
        ? 'No takes from the lanes yet.<br>In Cubase: select the takes, then <b>Audio &gt; Extensions &gt; Comp Captain</b>.'
        : 'No takes yet.<br>Arm <b class="red-dot">\u25CF</b> and press Record in your DAW,<br>or drop audio files here.'}</div>`;
    }
    takes.forEach((t) => {
      const row = document.createElement('div');
      row.className = 'tl-row real' + (t.id === st.sel ? ' sel' : '') + (t.missing ? ' missing' : '');
      row.title = `${t.name}: bar ${t.barText}, ${fmtLen(t.lenSec)} long${heardText(t)}${t.source === 'file' && t.original ? `\nAdded from ${t.original}` : ''}${t.source === 'ara' ? `\nOn the lanes of "${t.laneTrack || t.track}"${t.here ? ' (played by this Comp Captain)' : ''}` : ''}${t.missing ? '\nThe file is missing: was the takes folder moved?' : ''}`;
      row.innerHTML = `<span class="play" title="Hear this take (arrives in M4)"><svg viewBox="0 0 24 24"><use href="#i-play"/></svg></span>
        <span class="nm">${esc(t.name)}${t.heard ? `<i class="heard-dot ${t.heard}"></i>` : ''}</span>
        <span class="role-chip" style="--rc:${roleColor(t.role)}" title="Role: ${roleLabel(t.role)} (click to change)">${roleLabel(t.role)}</span>
        <span class="at">${t.missing ? '\u26A0 missing' : 'bar ' + esc(t.barText)}</span>
        <span class="fav-btn${t.fav ? ' on' : ''}" title="${t.fav ? 'Favourite (wins ties)' : 'Mark as favourite'}">\u2665</span>
        <span class="more-btn" title="More">\u2022\u2022\u2022</span>`;
      row.onclick = (e) => {
        if (e.target.closest('.role-chip')) return roleMenu(e.target.closest('.role-chip'), t);
        if (e.target.closest('.fav-btn')) return act('fav', t.id, !t.fav);
        if (e.target.closest('.more-btn')) return takeMenu(e.target.closest('.more-btn'), t);
        if (e.target.closest('.play')) return toastFn('Hearing a take on its own arrives in M4 (Comp). For now, play it in your DAW.');
        select(t.id);
      };
      row.ondblclick = (e) => { if (t.source !== 'ara' && e.target.closest('.nm')) rename(t); };
      row.oncontextmenu = (e) => { e.preventDefault(); takeMenu(row.querySelector('.more-btn'), t); };
      box.appendChild(row);
    });
    if (live && hearingNothing()) {
      const row = document.createElement('div');
      row.className = 'tl-row warn';
      row.innerHTML = `<span class="nm">\u26A0 ${esc(NOTHING)}</span>`;
      box.appendChild(row);
    } else if (!live && st.notKept) {
      const row = document.createElement('div');
      row.className = 'tl-row warn';
      row.title = 'Click to hide';
      row.innerHTML = `<span class="nm">\u26A0 ${esc(st.notKept)}</span>`;
      row.onclick = () => { st.notKept = ''; changed('news'); };
      box.appendChild(row);
    }
    if (live) {
      const row = document.createElement('div');
      row.className = 'tl-row recording';
      row.innerHTML = `<span class="play"><svg viewBox="0 0 24 24"><use href="#i-play"/></svg></span><span class="nm">${esc(live.name)}</span><span class="rec-lbl">RECORDING \u00B7 bar ${esc(live.barText)}</span>`;
      box.appendChild(row);
    } else if (st.capture.armed && !(st.capture.ara || st.ara)) {
      const row = document.createElement('div');
      row.className = 'tl-row recording armed';
      row.innerHTML = `<span class="play"><svg viewBox="0 0 24 24"><use href="#i-play"/></svg></span><span class="nm">Next take</span><span class="rec-lbl">ARMED: press Record</span>`;
      box.appendChild(row);
    }
    if (st.importing > 0) {
      const row = document.createElement('div'); row.className = 'tl-row busy';
      row.innerHTML = `<span class="nm">Adding ${st.importing} file${st.importing > 1 ? 's' : ''}\u2026</span>`;
      box.appendChild(row);
    }
    // bring a newly picked take into view (only when the pick changes, so you can scroll freely)
    const sel = box.querySelector('.sel');
    if (sel && st.sel !== scrolledTo) { scrolledTo = st.sel; sel.scrollIntoView({ block: 'nearest' }); }
  }
  let scrolledTo = null;
  function heardText(t) {
    if (t.heard === 'done') return `\nListened to: ${t.words || 0} words, ${(t.phrases || []).length} phrases`;
    if (t.heard === 'waiting') return '\nWaiting to be listened to (for lining up the lyrics)';
    if (t.heard === 'failed') return '\nCouldn\u2019t be listened to';
    return '';
  }

  // ------------------------------------------------------------------ Record page: the selected (or recording) take on its bars
  function drawRecord(canvas, barsEl) {
    const live = liveTake();
    const t = live || selected();
    const head = canvas.parentElement.querySelector('.p-head');
    if (!t) {
      const [q0, q1] = span([]);
      drawBars(barsEl, q0, q1, { lines: true });
      Wave.draw(canvas, { t0: q0, t1: q1, env: () => -1, color: '#ffc531', glow: 0, noise: false });
      if (head) head.style.display = 'none';
      return;
    }
    let [q0, q1] = span([t]);
    if (live) { const qpb = quartersPerBar(); q1 = Math.max(q1, Math.ceil((t.ppq + t.lenQ) / qpb + 1) * qpb); }
    drawBars(barsEl, q0, q1, { lines: true });
    Wave.draw(canvas, { t0: q0, t1: q1, env: envFor(t), color: live ? '#ff4d4d' : roleColor(t.role), seed: 5, step: 1.6, glow: 7, noise: false });
    const ph = playheadPpq();
    if (head) {
      const show = st.transport.known && ph >= q0 && ph <= q1;
      head.style.display = show ? '' : 'none';
      if (show) head.style.left = ((ph - q0) / (q1 - q0) * 100) + '%';
    }
  }

  // ------------------------------------------------------------------ Comp page, Song view: every take on the song's bars
  // band: [first quarter, last quarter] of the current lyric line, shaded across the lanes (or null).
  function drawLanes(lanesEl, rulerEl, band) {
    lanesEl.innerHTML = '';
    const live = liveTake();
    const takes = st.list.slice().concat(live ? [live] : []);
    const [q0, q1] = span(takes);
    rulerEl.innerHTML = '';
    rulerEl.classList.add('bar-ruler');
    drawBars(rulerEl, q0, q1, { lines: false });
    if (!takes.length) {
      lanesEl.innerHTML = `<div class="lanes-empty">Your takes show up here, each on its own lane, lined up on the song's bars.<br>Record some on the Record page, or drop audio files anywhere on this window.</div>`;
      return;
    }
    const ph = playheadPpq();
    takes.forEach((t) => {
      const lane = document.createElement('div');
      lane.className = 'lane real' + (t.id === st.sel ? ' sel' : '');
      lane.innerHTML = `<div class="lane-name"><span class="dot" style="color:${t.live ? '#ff4d4d' : roleColor(t.role)}"></span><span class="ln">${esc(t.name)}</span>${t.fav ? '<span class="fav">\u2665</span>' : ''}<small>${t.live ? 'recording' : roleLabel(t.role)}</small></div><canvas></canvas>${t.live ? '' : '<button class="more" title="More">\u2022\u2022\u2022</button>'}`;
      lanesEl.appendChild(lane);
      Wave.draw(lane.querySelector('canvas'), { t0: q0, t1: q1, env: envFor(t), color: t.live ? '#ff4d4d' : roleColor(t.role), seed: 3, step: 1.6, glow: 5, noise: false });
      const at = (q) => `calc(200px + (100% - 240px) * ${(q - q0) / (q1 - q0)})`;
      const wide = (a, b) => `calc((100% - 240px) * ${Math.max(0, b - a) / (q1 - q0)})`;
      if (band && band[1] > q0 && band[0] < q1) {
        const a = Math.max(q0, band[0]), b = Math.min(q1, band[1]);
        const e = document.createElement('div'); e.className = 'line-band'; e.style.left = at(a); e.style.width = wide(a, b); lane.appendChild(e);
      }
      // the sung phrases (found by listening), under the waveform
      if (!t.live && Array.isArray(t.phrases)) {
        const perSec = (t.bpm || 120) / 60;
        t.phrases.forEach(([s0, s1]) => {
          const a = Math.max(q0, t.ppq + s0 * perSec), b = Math.min(q1, t.ppq + s1 * perSec);
          if (b <= a) return;
          const e = document.createElement('div'); e.className = 'phrase'; e.style.left = at(a); e.style.width = wide(a, b); lane.appendChild(e);
        });
      }
      if (st.transport.known && ph >= q0 && ph <= q1) {
        const p = document.createElement('div'); p.className = 'lane-head';
        p.style.left = at(ph); lane.appendChild(p);
      }
      if (!t.live) {
        lane.onclick = (e) => { if (e.target.closest('.more')) return takeMenu(e.target.closest('.more'), t); select(t.id); };
        lane.oncontextmenu = (e) => { e.preventDefault(); takeMenu(lane.querySelector('.more'), t); };
      }
    });
  }

  // One take's waveform over a stretch of the song's bars (quarters q0..q1): the Comp page's take rows.
  function drawTakeSpan(canvas, id, q0, q1) {
    const t = st.list.find((x) => x.id === id);
    if (!t) return Wave.draw(canvas, { t0: q0, t1: q1, env: () => -1, color: '#ffc531', glow: 0, noise: false });
    if (!st.peaks[t.id] && !st.asked[t.id] && t.hasPeaks) { st.asked[t.id] = true; Bridge.send('getPeaks', { ids: [t.id] }); }
    Wave.draw(canvas, { t0: q0, t1: q1, env: envFor(t), color: roleColor(t.role), seed: 3, step: 1.5, glow: 6, noise: false });
  }
  // A take's short tag: "T3" for "Take 3", else its first letters.
  function shortName(id) {
    const t = st.list.find((x) => x.id === id);
    if (!t) return '?';
    const m = /(\d+)\s*$/.exec(t.name);
    return m ? 'T' + m[1] : t.name.slice(0, 3);
  }
  const takeColors = ['#19d3ff', '#b05cff', '#2bea7a', '#6b7bff', '#ff5fc4', '#ffb340', '#4df0e0', '#ff7a5c'];
  function envOf(id) { const t = st.list.find((x) => x.id === id); return t ? envFor(t) : null; }
  function colorOf(id) { const i = st.list.findIndex((x) => x.id === id); return takeColors[(i < 0 ? 0 : i) % takeColors.length]; }

  // ------------------------------------------------------------------ the track picker (top left)
  function trackMenu(anchor) {
    if (!menuImpl) return;
    const items = [];
    const list = st.tracks.length ? st.tracks : [{ id: 'self', name: st.trackName, role: st.trackRole, takes: st.list.length, self: true, viewing: true }];
    items.push({ label: 'Comp Captain on these tracks:', header: true });
    list.forEach((t) => items.push({ label: `${t.name || 'This track'}${t.ara ? ' (lanes)' : ''} \u00B7 ${t.takes} take${t.takes === 1 ? '' : 's'}${t.self ? ' (this one)' : ''}`, value: 'track:' + t.id, checked: !!t.viewing }));
    items.push({ label: `Role of ${viewingSelf() ? 'this track' : 'that track'}:`, header: true });
    ROLES.forEach((r) => items.push({ label: r[1], value: 'role:' + r[0], checked: r[0] === st.trackRole, color: r[2] }));
    menuImpl(anchor, items, (it) => {
      if (it.value.startsWith('track:')) viewTrack(it.value.slice(6));
      else if (it.value.startsWith('role:')) setTrackRole(it.value.slice(5));
    });
  }
  function trackLabel() {
    const viewing = st.tracks.find((t) => t.viewing);
    const name = (viewing ? viewing.name : st.trackName) || (st.standalone ? 'Standalone' : 'This track');
    return `${name} \u2013 ${roleLabel(st.trackRole)}${viewing && !viewing.self ? ' (another track)' : ''}`;
  }

  // ------------------------------------------------------------------ adding files: where they go in the song
  function placementDialog(src) {
    const t = st.transport;
    const files = src.files.map((f) => ({ name: f.name || f.file || 'File', guess: f.guess || '' }));
    const roleSelect = (i, guess) => `<select class="dlg-select" data-i="${i}"><option value="">${guess ? 'Guess: ' + roleLabel(guess) : 'Same as the track (' + roleLabel(st.trackRole) + ')'}</option>${ROLES.map((r) => `<option value="${r[0]}">${r[1]}</option>`).join('')}</select>`;
    dialog(`<div class="dlg-title">Add ${files.length} take${files.length > 1 ? 's' : ''}</div>
      <p>Where do ${files.length > 1 ? 'these files' : 'this file'} start in your song? Files exported from another session usually start at bar 1.</p>
      <div class="dlg-grid">
        <label>Starts at bar</label><input class="dlg-input small" id="dlg-bar" type="number" min="1" max="9999" value="1">
        <label>Song tempo</label><span><input class="dlg-input small" id="dlg-bpm" type="number" min="20" max="400" step="0.01" value="${(+t.bpm || 120).toFixed(2).replace(/\.00$/, '')}"> BPM ${t.known ? '<small>(from your DAW)</small>' : ''}</span>
        <label>Time signature</label><span><input class="dlg-input tiny" id="dlg-num" type="number" min="1" max="32" value="${t.num || 4}"> / <input class="dlg-input tiny" id="dlg-den" type="number" min="1" max="32" value="${t.den || 4}"></span>
      </div>
      <div class="dlg-files">${files.map((f, i) => `<div class="dlg-file"><span>${esc(f.name)}</span>${roleSelect(i, f.guess)}</div>`).join('')}</div>
      <div class="dlg-buttons"><button class="pill" data-close>Cancel</button><button class="pill gold-btn" id="dlg-ok">Add takes</button></div>`,
    (body, close) => {
      body.querySelector('#dlg-ok').onclick = () => {
        const placement = {
          bar: Math.max(1, parseInt(body.querySelector('#dlg-bar').value, 10) || 1),
          bpm: parseFloat(body.querySelector('#dlg-bpm').value) || 120,
          num: parseInt(body.querySelector('#dlg-num').value, 10) || 4,
          den: parseInt(body.querySelector('#dlg-den').value, 10) || 4,
        };
        const roles = Array.from(body.querySelectorAll('.dlg-select')).map((s) => s.value);
        close();
        if (src.dropped) upload(src.dropped, placement, roles);
        else if (st.mock) mockImport(files, placement, roles);
        else Bridge.send('importChosen', Object.assign({ token: src.token, roles }, placement));
      };
    });
  }

  // Dropped files: the web view can't tell the plug-in where a dropped file lives, so its bytes are sent
  // over in pieces and saved by the plug-in, then added like any other file.
  const CHUNK = 512 * 1024;
  let refs = 0, uploading = false;
  async function upload(files, placement, roles) {
    if (st.mock) return mockImport(files.map((f) => ({ name: f.name.replace(/\.[^.]+$/, '') })), placement, roles);
    uploading = true;
    for (let i = 0; i < files.length; i++) {
      const f = files[i], ref = ++refs;
      try {
        Bridge.send('dropBegin', { name: f.name, size: f.size, ref });
        const ready = await waitFor('dropReady', (m) => m.ref === ref);
        if (!ready.upload) throw new Error('no room');
        for (let at = 0; at < f.size; at += CHUNK) {
          const bytes = new Uint8Array(await f.slice(at, at + CHUNK).arrayBuffer());
          Bridge.send('dropChunk', { upload: ready.upload, data: encode(bytes) });
          await waitFor('dropAck', (m) => m.upload === ready.upload);
          if (f.size > 4 * CHUNK) toastFn(`Bringing in ${f.name}: ${Math.min(100, Math.round((at + CHUNK) / f.size * 100))}%`, 1500);
        }
        Bridge.send('dropEnd', Object.assign({ upload: ready.upload, role: roles[i] || '' }, placement));
      } catch (e) {
        toastFn(`Couldn't bring in ${f.name}. Try the Add Files button instead.`, 6000);
      }
    }
    uploading = false;
  }
  const AUDIO = /\.(wav|wave|aif|aiff|aifc|flac|mp3)$/i;
  const LYRICS = /\.(txt|text|lrc|docx|doc|html|htm)$/i;
  let lyricsDrop = null;
  // The comp being dragged out to the DAW: if it's dropped back on this window, it isn't added as a take.
  let draggedOut = { name: '', at: 0 };
  const noteDragOut = (name) => { draggedOut = { name, at: Date.now() }; };
  const setLyricsDrop = (fn) => { lyricsDrop = fn; };
  function wireDrop(stage, hint) {
    let depth = 0;
    const hasFiles = (e) => e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files');
    stage.addEventListener('dragenter', (e) => { if (!hasFiles(e)) return; e.preventDefault(); depth++; hint.hidden = false; });
    stage.addEventListener('dragover', (e) => { if (!hasFiles(e)) return; e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; });
    stage.addEventListener('dragleave', (e) => { if (!hasFiles(e)) return; depth = Math.max(0, depth - 1); if (!depth) hint.hidden = true; });
    stage.addEventListener('drop', (e) => {
      if (!hasFiles(e)) return;
      e.preventDefault(); depth = 0; hint.hidden = true;
      const files = Array.from(e.dataTransfer.files || []);
      if (draggedOut.name && Date.now() - draggedOut.at < 120000 && files.some((f) => f.name === draggedOut.name)) {
        toastFn('That\u2019s your comp: drop it on a track in your DAW.', 4000); return;
      }
      const audio = files.filter((f) => AUDIO.test(f.name));
      const lyricFile = files.find((f) => LYRICS.test(f.name));
      if (!audio.length && lyricFile && lyricsDrop) { lyricsDrop(lyricFile); return; }
      if (!audio.length) { toastFn('Comp Captain takes WAV, AIFF, FLAC and MP3 files (and lyrics as .txt, .lrc, .docx, or a Song Rider Pro page).', 4000); return; }
      if (uploading) { toastFn('Still bringing in the last files. Try again in a moment.'); return; }
      if (audio.length < files.length) toastFn(`${files.length - audio.length} file${files.length - audio.length > 1 ? 's were' : ' was'} skipped (not WAV, AIFF, FLAC or MP3).`, 4000);
      placementDialog({ dropped: audio, files: audio.map((f) => ({ name: f.name.replace(/\.[^.]+$/, ''), guess: guessRole(f.name) })) });
    });
    // never let a stray drop open the file in the web view
    window.addEventListener('dragover', (e) => e.preventDefault());
    window.addEventListener('drop', (e) => e.preventDefault());
  }
  // The same idea as the plug-in's guess, for the dialog (the plug-in makes the final guess).
  function guessRole(name) {
    const w = name.replace(/\.[^.]+$/, '').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/([a-zA-Z])(\d)|(\d)([a-zA-Z])/g, '$1$3 $2$4').toLowerCase().split(/[^a-z0-9]+/).filter(Boolean);
    const has = (...xs) => w.some((x) => xs.includes(x));
    const pair = (a, b) => w.some((x, i) => x === a && b.includes(w[i + 1]));
    if (has('gtr', 'gtrs', 'guitar', 'guitars', 'guit', 'acoustic')) return 'guitar';
    if (has('adlib', 'adlibs', 'adl', 'hype', 'hypes') || pair('ad', ['lib', 'libs'])) return 'adlib';
    if (w.some((x) => x.startsWith('harm')) || has('hrm', 'third', 'fifth') || pair('3', ['rd']) || pair('5', ['th'])) return 'harmony';
    if (has('dbl', 'dbls', 'double', 'doubles', 'doubled', 'dub')) return 'double';
    if (has('bv', 'bvs', 'bgv', 'bgvs', 'bgvox', 'backing', 'backings', 'backs', 'backup', 'backups', 'background', 'choir', 'stack', 'stacks', 'gang', 'oohs', 'aahs')) return 'backing';
    if (has('keys', 'piano', 'bass', 'drums', 'drum', 'kick', 'snare', 'hats', 'synth', 'pad', 'pads', 'strings', 'organ', 'beat', 'instrumental', 'inst', 'perc', '808')) return 'other';
    if (has('lead', 'ld', 'main', 'vox', 'vocal', 'vocals', 'voc', 'vo', 'voice', 'rap', 'verse', 'hook', 'lv', 'singer', 'comp')) return 'lead';
    return '';
  }

  // While a take records: true once two seconds have gone by and nothing louder than -60 dB came through
  // (the track isn't monitoring, or Comp Captain isn't on the track being recorded).
  function hearingNothing() {
    const c = st.capture, l = c.live;
    if (!c.recording || !l || (l.seconds || 0) < 2 || st.live.len < 150) return false;
    let loudest = 0;
    for (let i = 0; i < st.live.len; i++) if (st.live.data[i] > loudest) loudest = st.live.data[i];
    return loudest < 8; // the waveform is stored as sqrt(level) x 255: 8 is about -60 dB
  }
  const NOTHING = 'Comp Captain hears nothing on this track. Turn on the track\u2019s monitor (speaker) button, and check Comp Captain is on the track you\u2019re recording.';

  // ------------------------------------------------------------------ what the footer says on the Record page
  function status() {
    const c = st.capture;
    if (c.ara || st.ara) return ['Takes from your DAW\'s lanes (ARA)', ['Placed on their real bars', 'Played back unchanged', 'Report in \u2699 Settings']];
    if (c.recording && c.live && hearingNothing()) return [NOTHING, ['Monitor (speaker) button on', 'Comp Captain on this track', 'Then record again']];
    if (c.recording && c.live) return [`Recording ${c.live.name} from bar ${c.live.barText} \u00B7 ${fmtLen(c.live.seconds)}`, ['Every pass becomes a take', 'Bar-stamped', c.loopTakes ? 'Loop Takes on' : 'Loop Takes off']];
    if (c.armed && st.notKept) return [st.notKept, ['Monitor (speaker) button on', 'Comp Captain on the track you record', 'Then record again']];
    if (c.armed) return ['Armed: press Record in your DAW', ['Track must be monitoring', 'Every pass becomes a take', 'Bar-stamped']];
    if (lastNews && lastNews.text) return [lastNews.text, [`${st.list.length} take${st.list.length === 1 ? '' : 's'} on this track`, 'Saved with your project', 'Drop files to add more']];
    return [st.list.length ? `${st.list.length} take${st.list.length === 1 ? '' : 's'} on this track` : 'Ready to capture your takes', ['Arm \u25CF then record in your DAW', 'Every pass becomes a take', 'Bar-stamped']];
  }

  // ------------------------------------------------------------------ made-up takes for trying it in a browser (?real)
  function mockPeaks(seed, seconds, sparse) {
    const n = Math.round(seconds * 100), a = new Uint8Array(n), r = Demo.rng(seed);
    for (let i = 0; i < n; i++) {
      const t = i / 100, phrase = (t % 4.3) < 3.4 ? 1 : 0.05;
      const syl = Math.pow(Math.abs(Math.sin(t * 6.1 + seed)), 0.7);
      a[i] = Math.round(255 * Math.min(1, Math.sqrt(phrase * syl * (0.55 + 0.45 * r())) * (sparse && (t % 8) > 3 ? 0.08 : 1)));
    }
    return a;
  }
  function mockStart() {
    if (!st.mock) return;
    const bpm = 92, qpb = 4, mk = (id, name, role, bar, sec, extra) => Object.assign({
      id, name, role, roleChosen: false, fav: false, source: 'capture', bar, beat: 1, barText: String(bar), ppq: (bar - 1) * qpb,
      lenQ: sec * bpm / 60, startSec: (bar - 1) * qpb * 60 / bpm, lenSec: sec, bpm, meter: '4/4', rate: 48000, channels: 1, track: 'Lead Vox', file: name + '.wav', missing: false, hasPeaks: true,
    }, extra || {});
    st.list = [
      mk('t1', 'Take 1', 'lead', 9, 31.2), mk('t2', 'Take 2', 'lead', 9, 31.6, { fav: true }), mk('t3', 'Take 3', 'lead', 9, 30.9),
      mk('t4', 'Take 4', 'lead', 9, 31.4), mk('t5', 'Take 5', 'lead', 17, 16.2),
      mk('t6', 'Vox Dbl L', 'double', 9, 31.0, { source: 'file', original: 'D:\\Old Session\\Vox Dbl L.wav' }),
      mk('t7', 'Adlibs', 'adlib', 13, 20.5, { source: 'file' }),
    ];
    st.list.forEach((t, i) => { st.peaks[t.id] = { data: mockPeaks(10 + i * 7, t.lenSec, t.role === 'adlib'), perSecond: 100 }; });
    st.sel = 't2'; st.trackName = 'Lead Vox'; st.trackRole = 'lead';
    st.tracks = [{ id: '1', name: 'Lead Vox', role: 'lead', takes: 7, self: true, viewing: true }, { id: '2', name: 'Vox Dbl', role: 'double', takes: 3 }, { id: '3', name: 'BV Stack', role: 'backing', takes: 0 }];
    Object.assign(st.transport, { known: true, playing: false, ppq: 40, bpm, num: 4, den: 4, at: performance.now() });
    changed('takes');
  }
  function mockAct(op, id, value) {
    const t = st.list.find((x) => x.id === id); if (!t) return;
    if (op === 'rename') t.name = value;
    if (op === 'role') { t.role = value; t.roleChosen = true; }
    if (op === 'fav') t.fav = !!value;
    if (op === 'remove') { st.list = st.list.filter((x) => x !== t); toastFn(`${t.name} removed. Its file is in the "Removed takes" folder.`); }
    changed('takes');
  }
  function mockImport(files, placement, roles) {
    files.forEach((f, i) => {
      const id = 't' + (100 + st.list.length), sec = 24;
      st.list.push({ id, name: f.name, role: roles[i] || f.guess || st.trackRole, fav: false, source: 'file', bar: placement.bar, beat: 1, barText: String(placement.bar),
        ppq: (placement.bar - 1) * placement.num * 4 / placement.den, lenQ: sec * placement.bpm / 60, lenSec: sec, bpm: placement.bpm, hasPeaks: true });
      st.peaks[id] = { data: mockPeaks(77 + i, sec), perSecond: 100 };
    });
    toastFn(`Added ${files.length} file${files.length > 1 ? 's' : ''} at bar ${placement.bar}.`);
    changed('takes');
  }

  return {
    st, ROLES, roleLabel, roleColor, onChange, setToast, setMenu, noteDragOut,
    setArmed, setLoopTakes, viewTrack, setTrackRole, openFolder, askReport, addFiles, select, selected,
    renderList, drawRecord, drawLanes, trackMenu, trackLabel, wireDrop, setLyricsDrop, status, playheadPpq, mockStart, fmtLen, hearingNothing,
    drawTakeSpan, shortName, colorOf, envOf,
  };
})();
