// Comp Captain interface: pages, light / dark, manual and settings. Real takes (M1) come from takes.js and
// the song's lyrics with their times (M2) from song.js; the parts that need scores (M3+) still show a
// sample song, clearly marked.
'use strict';

(() => {
  const $ = (s, el) => (el || document).querySelector(s);
  const $$ = (s, el) => Array.from((el || document).querySelectorAll(s));
  const h = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; return e; };
  const icon = (id) => `<svg viewBox="0 0 24 24"><use href="#${id}"/></svg>`;
  const fmt = (t) => { const m = Math.floor(t / 60), s = t - m * 60; return `${m}:${s.toFixed(3).padStart(6, '0')}`; };
  const fmtShort = (t) => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, '0')}`;

  const stage = $('#stage');
  const D = Demo;
  const T = Takes, TS = Takes.st;
  const SG = Song, SS = Song.st;
  const real = () => TS.real;
  if (real()) stage.dataset.real = '1';
  const PAGES = ['record', 'lyrics', 'comp', 'tighten', 'review', 'export'];

  // ------------------------------------------------------------------ state
  const S = {
    theme: Bridge.localGet('theme', 'dark'),
    page: Bridge.localGet('page', 'comp'),
    compView: Bridge.localGet('compView', 'line'),
    line: 2,            // the sample song's line (Comp page scores, until M3)
    sl: 0,              // the song's current lyric line (lyric bar, teleprompter, Lyrics page)
    following: false,   // the lit line follows the DAW's playhead
    lyrUndo: null,      // the lyrics before the last Paste / Open / Hear It
    choice: [],
    undo: [], redo: [],
    style: 'Worship / Gospel',
    weights: Object.assign({}, D.styles['Worship / Gospel']),
    playing: false, time: 10.5, loop: false,
    armed: false, recSection: 1, recLoop: true, recClick: true,
    recOpts: { countIn: true, loopTakes: true, autoNext: true, guide: false },
    tab: 'doubles', ba: 'after',
    layerOn: {}, layerDb: {},
    align: JSON.parse(JSON.stringify(D.alignDefaults)),
    exportOn: {},
    sonicology: { voxology: 'installed', mixology: 'installed' },
    version: '0.3.0', host: 'Browser preview',
    level: 0,
  };
  if (!PAGES.includes(S.page)) S.page = 'comp';

  // ------------------------------------------------------------------ scoring (demo)
  function total(sc, w) {
    const sum = w.p + w.t + w.c + w.e || 1;
    let v = (sc.p * w.p + sc.t * w.t + sc.c * w.c + sc.e * w.e) / sum;
    if (sc.warn) v -= 3;
    return v;
  }
  function bestTake(li) {
    let best = 0, bv = -1;
    D.scores[li].forEach((sc, ti) => {
      const v = total(sc, S.weights) + (D.takes[ti].fav ? 0.01 : 0);
      if (v > bv) { bv = v; best = ti; }
    });
    return best;
  }
  function autoComp() { return D.lines.map((_, li) => bestTake(li)); }
  S.choice = autoComp();

  function setChoice(next, message) {
    S.undo.push(S.choice.slice()); S.redo = [];
    S.choice = next;
    renderComp(); renderReview(); renderFooter();
    if (message) toast(message);
  }

  // ------------------------------------------------------------------ stage scaling
  function fitStage() {
    const s = Math.min(window.innerWidth / 1672, window.innerHeight / 941);
    stage.style.transform = `scale(${s})`;
    stage.style.left = Math.max(0, (window.innerWidth - 1672 * s) / 2) + 'px';
    stage.style.top = Math.max(0, (window.innerHeight - 941 * s) / 2) + 'px';
    Wave.setStageScale(s);
    redrawCanvases();
  }

  // ------------------------------------------------------------------ toast
  let toastTimer = 0;
  function toast(text, ms) {
    const t = $('#toast'); t.textContent = text; t.hidden = false;
    clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.hidden = true; }, ms || 2600);
  }
  const soon = (what, milestone) => toast(`${what} arrives in ${milestone}.`);

  // ------------------------------------------------------------------ small widgets
  function bindSlider(el, onChange) {
    const set = (v) => {
      v = Math.max(0, Math.min(1, v)); el.dataset.value = v;
      $('.fill', el).style.width = (v * 100) + '%'; $('.knob', el).style.left = (v * 100) + '%';
    };
    set(parseFloat(el.dataset.value || '0.5'));
    el.addEventListener('pointerdown', (e) => {
      const r = el.getBoundingClientRect();
      const move = (ev) => { set((ev.clientX - r.left) / r.width); onChange && onChange(parseFloat(el.dataset.value)); };
      el.setPointerCapture(e.pointerId); move(e);
      el.onpointermove = move;
      el.onpointerup = () => { el.onpointermove = null; el.onpointerup = null; };
    });
    return set;
  }
  function sliderHTML(v) { return `<div class="slider gold-slider" data-value="${v}"><div class="track"><div class="fill"></div></div><div class="knob"></div></div>`; }

  let openMenu = null;
  function menu(anchor, items, onPick) {
    closeMenu();
    const r = anchor.getBoundingClientRect(), sr = stage.getBoundingClientRect(), k = sr.width / 1672;
    const m = h('div', 'panel menu');
    Object.assign(m.style, { left: (r.left - sr.left) / k + 'px', top: (r.bottom - sr.top) / k + 4 + 'px', minWidth: Math.max(160, r.width / k) + 'px', zIndex: 40, padding: '6px' });
    items.forEach((raw) => {
      const it = typeof raw === 'string' ? { label: raw, value: raw } : raw;
      if (it.header) { m.appendChild(h('div', 'menu-head', escapeHtml(it.label))); return; }
      const b = h('button', 'menu-item' + (it.checked ? ' checked' : ''), (it.color ? `<i class="menu-dot" style="background:${it.color}"></i>` : '') + escapeHtml(it.label));
      b.onclick = (e) => { e.stopPropagation(); closeMenu(); onPick(typeof raw === 'string' ? raw : it); };
      m.appendChild(b);
    });
    stage.appendChild(m); openMenu = m;
    // keep it on the stage
    const over = m.offsetTop + m.offsetHeight - 930;
    if (over > 0) m.style.top = Math.max(8, m.offsetTop - over) + 'px';
    const right = m.offsetLeft + m.offsetWidth - 1664;
    if (right > 0) m.style.left = Math.max(8, m.offsetLeft - right) + 'px';
  }
  function closeMenu() { if (openMenu) { openMenu.remove(); openMenu = null; } }
  document.addEventListener('pointerdown', (e) => { if (openMenu && !openMenu.contains(e.target)) closeMenu(); });

  // the Record page's level meter follows your voice
  let glowLevel = 0;

  // ------------------------------------------------------------------ theme + pages
  function setTheme(t, tell) {
    S.theme = t === 'light' ? 'light' : 'dark';
    stage.dataset.theme = S.theme; Bridge.localSet('theme', S.theme);
    if (tell) Bridge.send('theme', { value: S.theme });
    redrawCanvases();
  }
  function setPage(p, tell) {
    if (!PAGES.includes(p)) return;
    S.page = p; stage.dataset.page = p; Bridge.localSet('page', p);
    $$('.nav-btn').forEach((b) => b.classList.toggle('active', b.dataset.go === p));
    if (tell) Bridge.send('page', { value: p });
    renderFooter(); redrawCanvases();
  }
  function setCompView(v, tell) {
    S.compView = v === 'song' ? 'song' : 'line';
    stage.dataset.compview = S.compView; Bridge.localSet('compView', S.compView);
    $$('#view-seg button').forEach((b) => b.classList.toggle('on', b.dataset.view === S.compView));
    if (tell) Bridge.send('compView', { value: S.compView });
    renderComp();
  }

  // ------------------------------------------------------------------ the song's time
  // Seconds into the song: the DAW's playhead inside the plug-in (moving on smoothly between its
  // messages), the sample song's clock in a browser. -1 when the DAW hasn't said.
  function songTime() {
    if (!Bridge.inHost) return S.time;
    const t = TS.transport;
    if (!t.known) return -1;
    return (+t.seconds || 0) + (t.playing ? (performance.now() - t.at) / 1000 : 0);
  }
  const songLength = () => { const sp = SG.span(); return sp ? sp[1] : (Bridge.inHost ? 0 : D.length); };
  function barText(q) {
    const t = TS.transport, qpb = (t.num > 0 && t.den > 0) ? t.num * 4 / t.den : 4, beat = 4 / (t.den || 4);
    const bar = Math.floor(q / qpb + 1e-6), b = Math.floor((q - bar * qpb) / beat + 1e-6);
    return b > 0 ? `${bar + 1}.${b + 1}` : String(bar + 1);
  }
  const lineWhen = (l) => (!SG.timed(l) ? '\u2013' : (Bridge.inHost && l.startQ >= 0 ? barText(l.startQ) : fmtShort(l.start)));

  // ------------------------------------------------------------------ shared lyric bar
  const secColors = ['#2f8fff', '#9c6bff', '#b76cff', '#7a5cff'];
  function renderLyricBar() {
    const ln = SS.lines[S.sl];
    const lt = $('#line-text'); lt.innerHTML = '';
    if (!ln) lt.appendChild(h('span', 'dim', SS.text.trim() ? '' : 'No lyrics yet: add them on the Lyrics page'));
    else ln.shown.forEach((w, i) => lt.appendChild(h('span', '', escapeHtml((i ? ' ' : '') + w))));
    markSung();
    fitLineText();
    const chips = $('#section-chips'); chips.innerHTML = '';
    const sp = SG.span(), secs = SS.sections;
    secs.forEach((sec, i) => {
      const c = h('div', 'chip' + (i % 2 ? '' : ' cyan') + (ln && ln.section === i ? ' on' : ''), escapeHtml(sec.name));
      let a = i / secs.length * 100, b = (i + 1) / secs.length * 100;
      if (sp && sec.start >= 0) { a = (sec.start - sp[0]) / (sp[1] - sp[0]) * 100; b = (sec.end - sp[0]) / (sp[1] - sp[0]) * 100; }
      c.style.left = `calc(${a}% + 3px)`; c.style.width = `calc(${Math.max(1.5, b - a)}% - 6px)`;
      c.title = sec.name + (sec.start >= 0 ? ` \u00B7 ${lineWhen(SS.lines[sec.first])}` : '');
      c.onclick = () => setLine(sec.first, true);
      chips.appendChild(c);
    });
    const ru = $('#ruler'); ru.innerHTML = '';
    if (sp) {
      const len = sp[1] - sp[0], step = len > 240 ? 30 : len > 90 ? 15 : len > 40 ? 8 : 4;
      for (let t = Math.ceil(sp[0] / step) * step; t <= sp[1]; t += step) { const e = h('span', '', fmtShort(t)); e.style.left = ((t - sp[0]) / len * 100) + '%'; ru.appendChild(e); }
    }
    drawOverview();
    movePlayheads();
  }
  // The words already sung light up gold.
  function markSung() {
    const ln = SS.lines[S.sl], t = songTime();
    const on = ln && SG.timed(ln) && t >= ln.start - 0.05;
    $$('#line-text span').forEach((e, i) => e.classList.toggle('sung', !!on && !!ln.words[i] && t >= ln.words[i][0]));
  }
  // The current lyric line is big (40px) and only shrinks as much as a long line needs to fit.
  function fitLineText() {
    const lt = $('#line-text'); lt.style.fontSize = '';
    const room = lt.clientWidth, need = lt.scrollWidth;
    if (room > 0 && need > room) lt.style.fontSize = Math.max(24, Math.floor(40 * room / need)) + 'px';
  }
  // The song in one strip: where each line is sung, coloured by section, the current line gold.
  function drawOverview() {
    const sp = SG.span(), cv = $('#overview');
    if (!sp) { Wave.draw(cv, { t0: 0, t1: 1, env: () => -1, color: '#ffc531', glow: 0, noise: false }); return; }
    const ls = SS.lines.filter(SG.timed);
    const lineAt = (t) => ls.find((l) => t >= l.start && t <= l.end);
    const env = Bridge.inHost
      ? (t) => { const l = lineAt(t); return l ? 0.35 + 0.65 * Math.abs(Math.sin((t - l.start) * 7.3)) * (l.takes ? 1 : 0.4) : 0.02; }
      : (t) => 0.25 + 0.75 * D.vocalEnvelope(t, 1, 0);
    const color = (t) => {
      const l = lineAt(t) || ls.find((x) => x.start > t) || ls[ls.length - 1];
      if (l === SS.lines[S.sl]) return '#ffc531';
      return secColors[(l ? l.section : 0) % 4];
    };
    Wave.draw(cv, { t0: sp[0], t1: sp[1], env, color, seed: 3, step: 2, glow: 5 });
  }
  function movePlayheads() {
    const t = songTime(), sp = SG.span();
    $('#time-now').textContent = fmt(Math.max(0, t));
    $('#time-len').textContent = fmt(songLength());
    const ov = $('#overview'), oh = $('#ov-playhead');
    oh.style.display = sp && t >= sp[0] && t <= sp[1] ? '' : 'none';
    if (sp) oh.style.left = (28 + ov.clientWidth * (t - sp[0]) / (sp[1] - sp[0])) + 'px';
    // best comp playhead (sample song)
    const span = compSpan();
    const bc = $('.bc-wave .p-head');
    if (bc) bc.style.left = Math.max(0, Math.min(100, (S.time - span[0]) / (span[1] - span[0]) * 100)) + '%';
    // record page: through the section
    const sec = SS.sections[S.recSection];
    const f = sec && sec.end > sec.start ? Math.max(0, Math.min(1, (t - sec.start) / (sec.end - sec.start))) : 0;
    $$('.page[data-page="record"] .prompter .p-head').forEach((e) => { e.style.left = (f * 100) + '%'; });
  }
  // The current lyric line on the song's bars (for the Song view), or null.
  function laneBand() {
    const l = SS.lines[S.sl];
    return l && SG.timed(l) && l.startQ >= 0 ? [l.startQ, l.endQ] : null;
  }
  function setLine(li, seek) {
    S.sl = Math.max(0, Math.min(SS.lines.length - 1, li));
    const sec = SG.sectionOf(S.sl);
    if (sec >= 0) S.recSection = sec;
    if (!Bridge.inHost) {
      S.line = Math.max(0, Math.min(D.lines.length - 1, li));
      if (seek && SS.lines[S.sl] && SG.timed(SS.lines[S.sl])) S.time = SS.lines[S.sl].start;
    }
    renderLyricBar(); renderComp(); renderRecord(); renderLyricsList();
  }

  // ------------------------------------------------------------------ COMP page
  const compSpan = () => [D.lines[0].start - 0.6, D.lines[D.lines.length - 1].end + 0.6];
  function scoreCell(v, cls) { return `<div class="score ${cls || ''}">${v}%<i style="--w:${Math.max(0, (v - 60) / 40 * 100)}%"></i></div>`; }
  function renderComp() {
    const li = S.line, ln = D.lines[li], chosen = S.choice[li];
    const sec = D.sections[ln.section];
    $('#li-count').textContent = `Line ${li + 1} of ${D.lines.length} | ${sec.name}`;
    $('#li-text').textContent = `\u201C${ln.text}\u201D`;
    $('#track-name').textContent = real() ? T.trackLabel() : ($('#track-name').dataset.custom || `Vocal \u2013 Lead (${sec.name})`);

    // Line view
    const rows = $('#take-rows'); rows.innerHTML = '';
    D.takes.forEach((tk, ti) => {
      const sc = D.scores[li][ti];
      const r = h('div', 'take-row' + (ti === chosen ? ' chosen' : ''));
      r.innerHTML = `<span class="check">\u2713</span><button class="sq" title="Hear this take">${icon('i-play')}</button>
        <span class="name">${tk.name}</span>${tk.fav ? '<span class="fav" title="Favourite">\u2665</span>' : ''}<canvas></canvas>
        <span class="warn-badge" title="${sc.warn || ''}">${sc.warn ? '\u26A0' : ''}</span>
        <div class="scores">${scoreCell(sc.p)}${scoreCell(sc.t)}${scoreCell(sc.c)}${scoreCell(sc.e, 'e')}<div class="total">${Math.round(total(sc, S.weights))}%</div></div>`;
      r.onclick = (e) => {
        if (e.target.closest('.sq')) { S.time = ln.start; play(true); return; }
        if (ti !== S.choice[li]) { const n = S.choice.slice(); n[li] = ti; setChoice(n); }
      };
      rows.appendChild(r);
      const cv = $('canvas', r);
      Wave.draw(cv, { t0: ln.start - 0.25, t1: ln.end + 0.25, env: (t) => D.vocalEnvelope(t, tk.seed, tk.shift), color: tk.color, seed: tk.seed, step: 1.5, glow: 6 });
    });

    // Real takes: the Song view shows every take on the song's bars (the Line view needs lyrics + scores)
    if (real()) {
      T.drawLanes($('#lanes'), $('#lane-lyrics'), laneBand());
      renderBestCompNote();
      renderScorePanel();
      movePlayheads();
      return;
    }
    $('#lane-lyrics').classList.remove('bar-ruler');

    // Song view: the lines of the current section
    const secLines = D.lines.filter((l) => l.section === ln.section);
    const t0 = secLines[0].start - 0.3, t1 = secLines[secLines.length - 1].end + 0.3;
    const ll = $('#lane-lyrics'); ll.innerHTML = '';
    secLines.forEach((l) => {
      const c = h('div', 'lane-lyric' + (l.id === li ? ' on' : ''), l.text);
      const a = (l.start - 0.1 - t0) / (t1 - t0) * 100, b = (l.end + 0.1 - t0) / (t1 - t0) * 100;
      c.style.left = a + '%'; c.style.width = (b - a) + '%'; c.title = l.text;
      c.onclick = () => setLine(l.id, true);
      ll.appendChild(c);
    });
    const lanes = $('#lanes'); lanes.innerHTML = '';
    D.takes.forEach((tk, ti) => {
      const lane = h('div', 'lane');
      lane.innerHTML = `<div class="lane-name">${icon('i-play')}<span>${tk.name}</span><span class="dot" style="color:${tk.color}"></span></div><canvas></canvas><button class="more" title="More">\u2022\u2022\u2022</button>`;
      lanes.appendChild(lane);
      const cv = $('canvas', lane);
      Wave.draw(cv, { t0, t1, env: (t) => D.vocalEnvelope(t, tk.seed, tk.shift), color: tk.color, seed: tk.seed, step: 1.6, glow: 5 });
      const cvW = lane.clientWidth - 240;
      secLines.forEach((l) => {
        const cell = h('div', 'cell' + (S.choice[l.id] === ti ? ' on' : ''));
        const a = (l.start - 0.1 - t0) / (t1 - t0), b = (l.end + 0.1 - t0) / (t1 - t0);
        cell.style.left = (200 + a * cvW) + 'px'; cell.style.width = ((b - a) * cvW) + 'px';
        cell.title = `${tk.name}: ${Math.round(total(D.scores[l.id][ti], S.weights))}%`;
        cell.onclick = () => { S.line = l.id; if (S.choice[l.id] !== ti) { const n = S.choice.slice(); n[l.id] = ti; setChoice(n); } renderLyricBar(); };
        lane.appendChild(cell);
      });
    });

    // Best comp
    const [c0, c1] = compSpan();
    const colorAt = () => '#ffc531';
    Wave.draw($('#bc-canvas'), { t0: c0, t1: c1, env: (t) => {
      const l = D.lines.find((x) => t >= x.start - 0.3 && t <= x.end + 0.3);
      const tk = D.takes[l ? S.choice[l.id] : 0];
      return D.vocalEnvelope(t, tk.seed, 0);
    }, color: colorAt, seed: 9, step: 1.6, glow: 7 });
    const tags = $('#bc-tags'); tags.innerHTML = '';
    D.lines.forEach((l, i) => {
      if (i > 0) { const sep = h('div', 'bc-sep'); sep.style.left = ((((D.lines[i - 1].end + l.start) / 2) - c0) / (c1 - c0) * 100) + '%'; tags.appendChild(sep); }
      const tk = D.takes[S.choice[i]];
      const tg = h('div', 'bc-tag', 'T' + (S.choice[i] + 1));
      tg.style.left = (((l.start + l.end) / 2 - c0) / (c1 - c0) * 100) + '%';
      tg.style.background = `linear-gradient(180deg, ${tk.color}, ${shade(tk.color)})`;
      tg.title = `Line ${i + 1}: ${tk.name}`;
      tg.onclick = () => setLine(i, true);
      if (i === li) tg.style.boxShadow = '0 0 0 2px #ffd04a, 0 0 10px #ffb000';
      tags.appendChild(tg);
    });
    const ph = h('div', 'p-head small'); tags.appendChild(ph);
    renderScorePanel();
    movePlayheads();
  }
  function renderBestCompNote() {
    Wave.draw($('#bc-canvas'), { t0: 0, t1: 1, env: () => -1, color: '#ffc531', glow: 0, noise: false });
    $('#bc-tags').innerHTML = '<div class="bc-note">Your best comp is built here from your real takes once scores (M3) are in. Your lyrics are already lined up.</div>';
  }
  function shade(hex) { const n = parseInt(hex.slice(1), 16); const f = (v) => Math.round(v * 0.45); return `rgb(${f(n >> 16)},${f((n >> 8) & 255)},${f(n & 255)})`; }

  function renderScorePanel() {
    const li = S.line, ti = S.choice[li], sc = D.scores[li][ti];
    $('#sc-best').textContent = `(Chosen: T${ti + 1}${ti === bestTake(li) ? ', best' : ''})`;
    const rows = [['i-fork', 'Pitch', sc.p], ['i-clock', 'Timing', sc.t], ['i-gem', 'Clarity', sc.c], ['i-bolt', 'Energy', sc.e], ['i-star', 'Total (Line)', Math.round(total(sc, S.weights)), 'tot']];
    const box = $('#score-sliders'); box.innerHTML = '';
    rows.forEach(([ic, name, v, cls]) => {
      const r = h('div', 'ss ' + (cls || ''), `${icon(ic)}<span>${name}</span>${sliderHTML(v / 100)}<span class="val">${v}%</span>`);
      box.appendChild(r); bindSlider($('.slider', r));
      $('.slider', r).style.pointerEvents = 'none';
    });
    const reasons = $('#reasons'); reasons.innerHTML = '';
    const others = D.scores[li].map((s) => s);
    const avg = (k) => others.reduce((a, s) => a + s[k], 0) / others.length;
    const say = [];
    if (sc.p >= avg('p')) say.push('On pitch: closest to the melody most takes agree on'); else say.push('Pitch is a little under your other takes');
    if (sc.t >= avg('t')) say.push('Words land right on the beat');
    if (sc.c >= avg('c')) say.push('Every word comes through clearly');
    if (sc.e >= avg('e')) say.push('Strong, confident delivery');
    if (D.takes[ti].fav) say.push('You marked this take as a favourite');
    say.slice(0, 4).forEach((t) => reasons.appendChild(h('li', '', t)));
    if (sc.warn) reasons.appendChild(h('li', 'w', sc.warn));
  }

  // ------------------------------------------------------------------ RECORD page
  function renderRecord() {
    const sec = SS.sections[S.recSection];
    $('#rec-section').textContent = sec ? sec.name : 'No lyrics';
    const armed = real() ? TS.capture.armed : S.armed;
    $('#r-arm').classList.toggle('armed', armed);
    $('#r-arm').classList.toggle('recording', real() && TS.capture.recording);
    $('#rec-loop').classList.toggle('on', S.recLoop);
    $('#rec-click').classList.toggle('on', S.recClick);
    renderPrompter();
    if (real()) { renderRecordReal(); renderSectionLists(); return; }
    // bars and beats across the section (sample song)
    const dsec = D.sections[Math.min(D.sections.length - 1, S.recSection + 1)] || D.sections[0];
    const bars = $('#rec-bars'); bars.innerHTML = '';
    const beats = Math.round((dsec.end - dsec.start) / (D.bar / 4));
    for (let b = 0; b <= beats; b++) {
      const x = b / beats * 100;
      const lab = h('span', '', `${Math.floor(b / 4) + 1}.${(b % 4) + 1}`); lab.style.left = x + '%';
      if (b > 0 && b < beats) bars.appendChild(lab);
      const ln = h('i'); ln.style.left = x + '%'; bars.appendChild(ln);
    }
    drawRecWave();
    // takes list
    const list = $('#take-list'); list.innerHTML = '';
    const totalTakes = 8;
    for (let i = 0; i < totalTakes; i++) {
      const row = h('div', 'tl-row');
      if (i < D.takes.length) {
        const ov = Math.round(D.lines.reduce((a, l) => a + total(D.scores[l.id][i], S.weights), 0) / D.lines.length);
        row.innerHTML = `<span class="play">${icon('i-play')}</span><span class="nm">Take ${i + 1}</span><span class="bar" style="--w:${(ov - 60) / 40 * 100}%"></span><span class="pc">${ov}%</span>`;
      } else if (i === D.takes.length && S.armed) {
        row.classList.add('recording');
        row.innerHTML = `<span class="play">${icon('i-play')}</span><span class="nm">Take ${i + 1}</span><span class="rec-lbl">${S.playing ? 'RECORDING' : 'ARMED'}</span>`;
      } else {
        row.innerHTML = `<span class="play">${icon('i-play')}</span><span class="nm">Take ${i + 1}</span><span class="empty">\u2014</span>`;
      }
      list.appendChild(row);
    }
    $('#takes-count').textContent = `(${D.takes.length})`;
    $('#rec-take').textContent = `Take ${D.takes.length + 1} of ${totalTakes}`;
    renderSectionLists();
  }
  function renderRecordReal() {
    const t = TS.transport;
    $('#rec-bpm').textContent = t.known ? String(Math.round(t.bpm * 100) / 100) : '\u2013';
    $('#rec-sig').textContent = t.known ? `${t.num}/${t.den}` : '\u2013/\u2013';
    $('#rec-key').textContent = '\u2013';
    T.drawRecord($('#rec-canvas'), $('#rec-bars'));
    T.renderList($('#take-list'));
    $('#takes-count').textContent = `(${TS.list.length})`;
    const live = TS.capture.recording && TS.capture.live;
    const i = TS.list.findIndex((x) => x.id === TS.sel);
    $('#rec-take').textContent = live ? `\u25CF ${live.name}` : (i >= 0 ? `${TS.list[i].name} (${i + 1}/${TS.list.length})` : 'No takes yet');
    $('#rec-take').title = i >= 0 ? `${TS.list[i].name}: take ${i + 1} of ${TS.list.length}` : '';
  }

  // The teleprompter: the line being sung big in the middle (words light up as they're sung), the one
  // before above, the next one below. While the song plays it follows the playhead; with the count-in on,
  // the beats before a line count down where the previous line was.
  function renderPrompter() {
    const ls = SS.lines, t = songTime();
    const cur = S.following ? Math.max(0, SG.lineAt(t)) : S.sl;
    const l = ls[cur];
    const words = (x, live) => (!x ? '' : x.shown.map((w, i) => (live && SG.timed(x) && x.words[i] && t >= x.words[i][0] && t >= x.start - 0.05 ? `<span class="sung">${escapeHtml(w)}</span>` : escapeHtml(w))).join(' '));
    const count = countIn(cur, t);
    $('#p-prev').innerHTML = count || words(ls[cur - 1], false);
    $('#p-prev').classList.toggle('counting', !!count);
    $('#p-now').innerHTML = l ? words(l, S.following || !Bridge.inHost) : `<span class="dim">${SS.text.trim() ? '' : 'Your lyrics show here'}</span>`;
    $('#p-next').innerHTML = l ? words(ls[cur + 1], false) : `<span class="dim">${SS.text.trim() ? '' : 'Add them on the Lyrics page'}</span>`;
    fitPrompter();
  }
  // Beats left before line li starts, as lit dots (only in the gap before it, within two bars).
  function countIn(li, t) {
    if (!S.recOpts.countIn || !S.following) return '';
    const l = SS.lines[li];
    if (!l || !SG.timed(l) || t >= l.start) return '';
    const tr = TS.transport, bpm = Bridge.inHost ? (+tr.bpm || 120) : D.bpm, den = Bridge.inHost ? (tr.den || 4) : 4, num = Bridge.inHost ? (tr.num || 4) : 4;
    const beat = 60 / bpm * 4 / den, left = (l.start - t) / beat;
    const prev = SS.lines[li - 1];
    if (left > num * 2 || (prev && SG.timed(prev) && t < prev.end + beat * 0.5)) return '';
    const n = Math.ceil(left - 1e-6);
    if (n > num) return '<span class="count-wait">Next line in 2 bars</span>';
    let dots = '';
    for (let i = num; i >= 1; i--) dots += `<i class="${i >= n ? 'lit' : ''}"></i>`;
    return `<span class="count">${dots}<b>${n}</b></span>`;
  }
  // Long lines shrink so they always fit across the teleprompter.
  function fitPrompter() {
    [['#p-prev', 56], ['#p-now', 80], ['#p-next', 56]].forEach(([id, size]) => {
      const el = $(id); el.style.fontSize = size + 'px';
      const room = el.clientWidth - 40, need = el.scrollWidth;
      if (need > room && room > 0) el.style.fontSize = Math.max(28, Math.floor(size * room / need)) + 'px';
    });
  }
  function drawRecWave() {
    const sec = D.sections[Math.min(D.sections.length - 1, S.recSection + 1)] || D.sections[0];
    const upTo = S.armed && S.playing ? S.time : sec.end;
    Wave.draw($('#rec-canvas'), { t0: sec.start, t1: sec.end, env: (t) => (t <= upTo ? D.vocalEnvelope(t, 222, 0) : 0.01), color: '#ffc531', seed: 5, step: 1.6, glow: 7 });
  }
  // The song's sections (both side lists): click one to go to its first line.
  function renderSectionLists() {
    for (const id of ['#sec-list', '#sec-list-2']) {
      const box = $(id); box.innerHTML = '';
      if (!SS.sections.length) { box.appendChild(h('div', 'sec-empty dim', 'Put <b>[Verse 1]</b>, <b>[Chorus]</b>\u2026 above the lines on the Lyrics page to make sections.')); continue; }
      const cur = SG.sectionOf(S.sl);
      SS.sections.forEach((sec, i) => {
        const on = id === '#sec-list' ? i === S.recSection : i === cur;
        const when = sec.start >= 0 ? `<small>${lineWhen(SS.lines[sec.first])}</small>` : '';
        const r = h('div', 'sec-row' + (on ? ' on' : ''), `<span class="check">${on ? '\u2713' : ''}</span>${escapeHtml(sec.name)}${when}`);
        r.onclick = () => { S.recSection = i; setLine(sec.first, true); };
        box.appendChild(r);
      });
    }
  }

  // ------------------------------------------------------------------ LYRICS page
  // Each line with where it's sung (bar.beat), a bar showing where in the song, and how sure that is.
  function renderLyricsList() {
    const list = $('#lyr-list'); list.innerHTML = '';
    if (!SS.lines.length) {
      list.appendChild(h('div', 'll-empty', SS.text.trim() ? 'No sung lines found yet.' : 'Your lines show up here, each with the bar it\u2019s sung at, once a take has been listened to.<br><br>Paste or type your lyrics on the left, open a file, or let <b>Hear It</b> write them from a take.'));
      renderLyricsStatus();
      return;
    }
    const sp = SG.span();
    let lastSec = -1;
    SS.lines.forEach((l, i) => {
      if (l.section !== lastSec) {
        lastSec = l.section;
        const sec = SS.sections[l.section];
        list.appendChild(h('div', 'll head', `<span class="sec">${escapeHtml(sec ? sec.name : '').toUpperCase()}</span>`));
      }
      const heard = l.takes > 0;
      const r = h('div', 'll' + (i === S.sl ? ' on' : '') + (heard ? '' : ' unheard') + (heard && l.sure < 0.5 ? ' unsure' : ''));
      let where = `<span class="dim small">${SS.heardTakes ? 'not heard yet' : ''}</span>`;
      if (sp && SG.timed(l)) {
        const a = (l.start - sp[0]) / (sp[1] - sp[0]) * 100, b = (l.end - sp[0]) / (sp[1] - sp[0]) * 100;
        where = `<span class="span"><i style="left:${a}%;width:${Math.max(2, b - a)}%"></i></span>`;
      }
      r.innerHTML = `<span class="t">${lineWhen(l)}</span><span class="txt">${escapeHtml(l.text)}</span>${where}`;
      r.title = SG.whereFrom(l);
      r.onclick = () => setLine(i, true);
      list.appendChild(r);
    });
    const on = list.querySelector('.ll.on');
    if (on && on.dataset.shown !== '1') { on.scrollIntoView({ block: 'nearest' }); on.dataset.shown = '1'; }
    renderLyricsStatus();
  }
  // Lyrics Status (right side): lines and sections, listening, lining up, lines to check.
  function renderLyricsStatus() {
    const box = $('#lyr-status'); if (!box) return;
    const L = SS.listening, rows = [];
    const add = (kind, text) => rows.push(`<div><i class="${kind}"></i>${text}</div>`);
    if (SS.lines.length) add('ok', `${SS.lines.length} line${SS.lines.length === 1 ? '' : 's'} \u00B7 ${SS.sections.length} section${SS.sections.length === 1 ? '' : 's'}`);
    else add('warn', 'No lyrics yet');
    if (L.problem) add('warn', escapeHtml(L.problem));
    else if (L.now) add('busy', `Listening to ${escapeHtml(L.now)}\u2026 ${L.percent}%`);
    else if (L.paused) add('busy', 'Waiting while a take records');
    if (Bridge.inHost && !L.problem) {
      if (!SS.sungTakes) add('warn', 'No vocal takes yet');
      else add(SS.heardTakes === SS.sungTakes ? 'ok' : 'busy', `${SS.heardTakes} of ${SS.sungTakes} vocal take${SS.sungTakes === 1 ? '' : 's'} listened to`);
    }
    if (SS.lines.length && SS.heardTakes) {
      if (SS.linedUp) add('ok', `Lined up on ${SS.linedUp} take${SS.linedUp === 1 ? '' : 's'}`);
      else add('warn', 'These lyrics weren\u2019t heard in any take yet');
      const missing = SS.lines.map((l, i) => (l.takes ? -1 : i)).filter((i) => i >= 0);
      if (SS.linedUp && missing.length) add('warn', missing.length === 1 ? `Line ${missing[0] + 1} wasn\u2019t heard: check it` : `${missing.length} lines weren\u2019t heard (dimmed)`);
      else if (SS.linedUp) add('ok', 'Every line was found');
    }
    box.innerHTML = rows.join('') + (Bridge.inHost && SS.sungTakes ? '<button class="pill small-pill" id="lyr-again">Listen to the takes again</button>' : '');
    const again = $('#lyr-again');
    if (again) again.onclick = () => { SG.listenAgain(); toast('Listening to this track\u2019s takes again.'); };
  }
  // The lyrics box shows the song's lyrics (unless you're typing in it).
  function showLyricsText(force) {
    const ta = $('#lyr-text');
    if (force || document.activeElement !== ta) { if (ta.value !== SS.text) ta.value = SS.text; }
  }
  // Paste, Open and Hear It replace the lyrics; Undo brings back what was there.
  function replaceLyrics(text, message) {
    const before = $('#lyr-text').value;
    S.lyrUndo = before.trim() && before !== text ? before : null;
    $('#lyr-undo').hidden = !S.lyrUndo;
    $('#lyr-text').value = text;
    SG.setText(text, true);
    if (message) toast(message + (S.lyrUndo ? ' (Undo brings back the old ones.)' : ''), 4000);
  }
  function lyricsDialog(title, body, buttons) {
    const ov = $('#dialog'), box = $('#dialog-body');
    box.innerHTML = `<div class="dlg-title">${title}</div><p>${body}</p><div class="dlg-buttons">${buttons.map((b, i) => `<button class="pill${b.main ? ' gold-btn' : ''}" data-b="${i}">${b.label}</button>`).join('')}</div>`;
    ov.hidden = false;
    $$('button', box).forEach((e) => { e.onclick = () => { ov.hidden = true; const b = buttons[+e.dataset.b]; if (b.fn) b.fn(); }; });
  }
  const escapeHtml = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  // ------------------------------------------------------------------ TIGHTEN page
  function renderTighten() {
    $$('#layer-tabs button').forEach((b) => b.classList.toggle('on', b.dataset.tab === S.tab));
    $$('#ba-seg button').forEach((b) => b.classList.toggle('on', b.dataset.ba === S.ba));
    // alignment sliders for this tab
    const box = $('#align-box'); box.innerHTML = '';
    S.align[S.tab].forEach((row, i) => {
      const [ic, name, v] = row;
      const r = h('div', 'align-row', `${icon(ic)}<span class="lbl">${name}</span>${sliderHTML(v)}<span class="row-val">${Math.round(v * 100)}%</span>`);
      box.appendChild(r);
      bindSlider($('.slider', r), (nv) => { row[2] = nv; $('.row-val', r).textContent = Math.round(nv * 100) + '%'; if (i === 0) drawTightLanes(); });
    });
    const notes = { doubles: 'Doubles are locked tight by default.', adlibs: 'Ad-libs are left alone unless they repeat the lead\'s words.', bv: 'Backing vocals a little looser than doubles.', guitars: 'Guitars line up with the main part (or the grid).' };
    box.appendChild(h('p', 'align-note', notes[S.tab]));
    // layer control (all layers)
    const lc = $('#layer-ctl'); lc.innerHTML = '';
    const all = [{ name: 'Lead (Comp)', db: 0, on: true, key: 'lead' }].concat(...Object.values(D.layers).map((arr) => arr.map((l) => Object.assign({ key: l.name }, l))));
    all.forEach((l) => {
      if (S.layerOn[l.key] === undefined) S.layerOn[l.key] = l.on;
      if (S.layerDb[l.key] === undefined) S.layerDb[l.key] = l.db;
      const db = S.layerDb[l.key];
      const r = h('div', 'lc', `<span class="check ${S.layerOn[l.key] ? 'on' : ''}">\u2713</span><span>${l.name}</span>${sliderHTML((db + 24) / 30)}<span class="row-val">${db.toFixed(1)} dB</span>`);
      $('.check', r).onclick = () => { S.layerOn[l.key] = !S.layerOn[l.key]; renderTighten(); };
      lc.appendChild(r);
      bindSlider($('.slider', r), (v) => { const d = Math.round((v * 30 - 24) * 2) / 2; S.layerDb[l.key] = d; $('.row-val', r).textContent = d.toFixed(1) + ' dB'; });
    });
    drawTightLanes();
  }
  function drawTightLanes() {
    const wrap = $('#tight-lanes'); wrap.innerHTML = '';
    const ln = D.lines[S.line];
    const secLines = D.lines.filter((l) => l.section === ln.section);
    const t0 = secLines[0].start - 0.3, t1 = secLines[secLines.length - 1].end + 0.3;
    const lead = { name: 'Lead (Comp)', role: 'Lead', seed: 222 };
    const items = [lead].concat(...Object.values(D.layers));
    items.forEach((l, i) => {
      const lane = h('div', 'tlane');
      const tabOf = Object.keys(D.layers).find((k) => D.layers[k].includes(l));
      const tight = tabOf ? S.align[tabOf][0][2] : 1;
      const miss = l === lead ? 0 : (S.ba === 'before' ? 0.045 : 0.045 * (1 - tight));
      const off = (k) => (Wave.hash(k * 7 + l.seed) - 0.5) * 2 * miss;
      lane.innerHTML = `<div class="nm">${l.name}<small>${l.role}${l === lead ? '' : ' \u00B7 ' + (S.layerOn[l.name] === false ? 'muted' : (S.ba === 'before' ? 'as recorded' : `within ${Math.max(2, Math.round(miss * 1000))} ms`))}</small></div><canvas></canvas>`;
      wrap.appendChild(lane);
      const env = (t) => {
        if (l.sparse) { const lnI = D.lines.findIndex((x) => t >= x.start && t <= x.end); if (lnI % 2 === 0) return 0.01; }
        if (l.guitar) { const beat = (t / (D.bar / 8)) % 1; return 0.25 + 0.6 * Math.exp(-beat * 5); }
        const w = D.lines.flatMap((x) => x.words).findIndex((x) => t >= x.start - 0.06 && t <= x.end + 0.06);
        return D.vocalEnvelope(t - (w >= 0 ? off(w) : 0), l.seed, 0) * (l === lead ? 1 : 0.85);
      };
      const color = l === lead ? '#ffc531' : (l.guitar ? '#4df0e0' : (l.role === 'Ad-lib' ? '#ff5fc4' : (l.role === 'Backing' ? '#6b7bff' : '#b05cff')));
      const dim = S.layerOn[l.name] === false;
      Wave.draw($('canvas', lane), { t0, t1, env, color, seed: l.seed, step: 1.6, glow: 5, alpha: dim ? 0.3 : 1 });
      if (l !== lead && !l.guitar) Wave.outline($('canvas', lane), { t0, t1, env: (t) => D.vocalEnvelope(t, 222, 0), color: 'rgba(255,200,60,.55)', alpha: 0.8 });
    });
  }

  // ------------------------------------------------------------------ REVIEW page
  function renderReview() {
    const rows = $('#review-rows'); if (!rows) return; rows.innerHTML = '';
    D.lines.forEach((l, i) => {
      const ti = S.choice[i], sc = D.scores[i][ti], tk = D.takes[ti];
      const r = h('div', 'rv', `<span class="n">${i + 1}</span><span class="sec">${D.sections[l.section].name}</span><span class="txt">${l.text}</span><span class="w" title="${sc.warn || ''}">${sc.warn ? '\u26A0' : ''}</span>
        <div class="cells"><span class="tg" style="background:linear-gradient(180deg,${tk.color},${shade(tk.color)})">T${ti + 1}</span><span>${sc.p}%</span><span>${sc.t}%</span><span>${sc.c}%</span><span>${sc.e}%</span><div class="total">${Math.round(total(sc, S.weights))}%</div></div>`);
      r.onclick = () => { setLine(i, true); setPage('comp', true); };
      rows.appendChild(r);
    });
    const grid = $('#style-grid'); grid.innerHTML = '';
    Object.keys(D.styles).forEach((name) => {
      const b = h('button', name === S.style ? 'on' : '', name);
      b.onclick = () => setStyle(name);
      grid.appendChild(b);
    });
    const ws = $('#weight-sliders'); ws.innerHTML = '';
    [['i-fork', 'Pitch', 'p'], ['i-clock', 'Timing', 't'], ['i-gem', 'Clarity', 'c'], ['i-bolt', 'Energy', 'e']].forEach(([ic, name, k]) => {
      const v = S.weights[k];
      const r = h('div', 'ss', `${icon(ic)}<span>${name}</span>${sliderHTML(v / 0.5)}<span class="val">${k === 'p' && v === 0 ? 'Off' : Math.round(v * 100) + '%'}</span>`);
      ws.appendChild(r);
      bindSlider($('.slider', r), (nv) => {
        S.weights[k] = Math.round(nv * 0.5 * 100) / 100; S.style = 'My Own';
        $('.val', r).textContent = Math.round(S.weights[k] * 100) + '%';
        $$('#style-grid button').forEach((b) => b.classList.toggle('on', b.textContent === 'My Own'));
        $('#comp-style-name').textContent = S.style;
      });
    });
    $('#comp-style-name').textContent = S.style;
  }
  function setStyle(name) {
    S.style = name; S.weights = Object.assign({}, D.styles[name]);
    $('#comp-style-name').textContent = name;
    renderReview(); renderComp(); renderFooter();
    toast(`Style: ${name}. Scores re-weighted.`);
  }

  // ------------------------------------------------------------------ EXPORT page
  function renderExport() {
    const rows = $('#export-rows'); rows.innerHTML = '';
    const secName = D.sections[D.lines[S.line].section].name;
    const files = [
      ['Lead Comp - ' + secName + '.wav', 'LEAD', '#ffc531'],
      ['Double L (tight).wav', 'DOUBLE', '#b05cff'], ['Double R (tight).wav', 'DOUBLE', '#b05cff'],
      ['Ad-Libs (tight).wav', 'AD-LIB', '#ff5fc4'], ['Backing Vocals (tight).wav', 'BACKING', '#6b7bff'],
      ['Guitar Layer 1 (tight).wav', 'GUITAR', '#4df0e0'], ['Guitar Layer 2 (tight).wav', 'GUITAR', '#4df0e0'],
    ];
    files.forEach(([name, role, col], i) => {
      if (S.exportOn[name] === undefined) S.exportOn[name] = i !== 6;
      const r = h('div', 'ex', `<span class="check ${S.exportOn[name] ? 'on' : ''}">\u2713</span><span>${name}</span><span class="role" style="color:${col}">${role}</span><span class="len">from bar 1.1</span><span class="len">${fmtShort(D.length)}</span>`);
      $('.check', r).onclick = () => { S.exportOn[name] = !S.exportOn[name]; renderExport(); renderFooter(); };
      rows.appendChild(r);
    });
    const anySuite = S.sonicology.voxology !== 'missing' || S.sonicology.mixology !== 'missing';
    $('#sonicology-box').style.display = anySuite ? '' : 'none';
    $$('#sonicology-box .send-btn').forEach((b) => { b.style.display = S.sonicology[b.dataset.send] === 'missing' ? 'none' : ''; });
    const linked = S.sonicology.voxology === 'linked' || S.sonicology.mixology === 'linked';
    $('#sonicology-box .dot').classList.toggle('on', linked);
    $('#sonicology-box .box-title .dim').textContent = linked ? 'connected' : 'installed';
  }

  // ------------------------------------------------------------------ FOOTER (changes with the page)
  function sendButtons() {
    const col = $('#send-col'); col.innerHTML = '';
    const add = (ic, label, fn) => { const b = h('button', 'send-btn', `${icon(ic)}${label}${icon('i-ext').replace('<svg', '<svg class="ext"')}`); b.onclick = fn; col.appendChild(b); };
    const suite = [['voxology', 'Send to Voxology', 'i-wave'], ['mixology', 'Send to Mixology', 'i-sliders']].filter(([k]) => S.sonicology[k] !== 'missing');
    suite.forEach(([k, label, ic]) => add(ic, label, () => sendTo(k)));
    if (suite.length < 2) add('i-drag', 'Drag to DAW', () => setPage('export', true));
    if (suite.length < 1) add('i-export', 'Render All', () => soon('Rendering', 'M4'));
  }
  function sendTo(k) {
    const name = k === 'voxology' ? 'Voxology' : 'Mixology';
    if (S.sonicology[k] === 'installed') toast(`Put ${name} on your ${k === 'voxology' ? 'lead vocal track' : 'mix bus'}, then click Send again.`);
    else soon(`Sending to ${name}`, 'M6');
  }
  function lyricsMessage() {
    const n = SS.lines.length, L = SS.listening;
    const ticks = ['Nothing sent online', 'Saved with your project', 'Every Comp Captain shares them'];
    if (!n) return ['Add your lyrics: paste, type, open a file, or Hear It', ticks];
    if (L.problem) return [L.problem, ticks];
    if (L.now) return [`Listening to ${L.now}\u2026 ${L.percent}%`, ticks];
    if (Bridge.inHost && !SS.heardTakes) return [`${n} lines ready: they line up once a take is listened to`, ticks];
    const found = SS.lines.filter((l) => l.takes > 0).length;
    return [`${found} of ${n} lines lined up with your takes`, [`${SS.sections.length} section${SS.sections.length === 1 ? '' : 's'}`, `Lined up on ${SS.linedUp} take${SS.linedUp === 1 ? '' : 's'}`, 'Saved with your project']];
  }
  function renderFooter() {
    const p = S.page;
    const ov = Math.round(D.lines.reduce((a, l, i) => a + total(D.scores[i][S.choice[i]], S.weights), 0) / D.lines.length);
    const flagged = D.lines.filter((l, i) => D.scores[i][S.choice[i]].warn).length;
    const msgs = {
      record: real() ? T.status() : [S.armed ? (S.playing ? `Recording Take ${D.takes.length + 1}` : `Armed: press Record in your DAW`) : 'Lyrics locked to the beat', ['Count-in on screen', 'Every pass becomes a take', 'Bar-stamped']],
      lyrics: lyricsMessage(),
      comp: [`Comp complete! Best of ${D.takes.length} takes, line by line.`, ['Strongest pitch & timing', 'Consistent tone', 'High energy', 'Joins in the breaths']],
      tighten: ['Layers are tight and in time with the lead.', ['Doubles within 12 ms', 'Ad-libs left natural', 'BV lined up', 'Guitars on the grid']],
      review: [`Average score ${ov}%${flagged ? ` \u00B7 ${flagged} line${flagged > 1 ? 's' : ''} to check` : ''}`, [`Style: ${S.style}`, 'Hearts win ties', 'Click a line to comp it']],
      export: [`Ready: ${Object.values(S.exportOn).filter(Boolean).length || 6} files, all from bar 1.1`, ['Named by role', 'Works in any DAW', 'Nothing sent online']],
    }[p];
    $('#msg-title').textContent = msgs[0];
    const ticks = $('#msg-ticks'); ticks.innerHTML = ''; msgs[1].forEach((t) => ticks.appendChild(h('span', '', t)));

    const ob = $('#opt-box'); ob.className = 'opt-box' + (p === 'record' ? ' record' : ''); ob.innerHTML = '';
    if (p === 'record') {
      if (real()) S.recOpts.loopTakes = TS.capture.loopTakes;
      const opts = [['countIn', 'Count-in on screen', '(the beats before each line)'], ['loopTakes', 'Loop Takes', '(each time your DAW\'s loop comes round)'],
        real() ? ['folder', 'Takes Folder', '(open where your takes are saved)'] : ['autoNext', 'Auto Next Take', '(after Stop)'], ['guide', 'Guide Vocal', '(hear your best take while you sing, M4)']];
      opts.forEach(([k, a, b]) => {
        const mark = k === 'folder' ? `<span class="check folder">${icon('i-folder')}</span>` : `<span class="check ${S.recOpts[k] ? 'on' : ''}">\u2713</span>`;
        const o = h('div', 'opt', `${mark}<div>${a}<small>${b}</small></div>`);
        o.onclick = () => {
          if (k === 'folder') { T.openFolder(); return; }
          if (k === 'loopTakes' && real()) { T.setLoopTakes(!TS.capture.loopTakes); return; }
          S.recOpts[k] = !S.recOpts[k]; renderFooter();
        };
        ob.appendChild(o);
      });
    } else {
      const opts = {
        lyrics: ['Tips', 'i-bulb', ['Put [Verse 1] above a part to make a section', 'Type [Chorus] again to sing it again', 'No lyrics? Hear It writes them for you']],
        comp: ['Comp Builder', 'i-target', ['Build from the highest-scoring takes', 'Keep the strongest feel & performance', 'Blend joins in the breaths (no clicks)']],
        tighten: ['Suggestions (Optional)', 'i-bulb', ['Tighten doubles (\u221212 ms)', 'Match backing vocal level (+1.5 dB)', 'Line up Guitar Layer 2 with the grid']],
        review: ['What the scores mean', 'i-bulb', ['Pitch: on the notes \u00B7 Timing: on the beat', 'Clarity: words clear \u00B7 Energy: delivery', '\u26A0 a pop, clip or loud breath was heard']],
        export: ['Each file carries its role', 'i-bulb', ['Lead, Double, Ad-lib, Backing, Guitar', 'Voxology reads the role automatically', 'Plain WAV: opens in any DAW']],
      }[p];
      const checks = p === 'comp' || p === 'tighten';
      ob.appendChild(h('div', 'opt-title', `${icon(opts[1])}${opts[0]}`));
      opts[2].forEach((t) => ob.appendChild(h('div', 'opt', checks ? `<span class="check on">\u2713</span>${t}` : `<span class="gold">\u2022</span>${t}`)));
    }
    sendButtons();
    const go = { lyrics: 'Line Up<br>Lyrics', comp: 'Build<br>Performance', tighten: 'Apply &amp;<br>Render', review: 'Re-score<br>All Takes', export: 'Render<br>All' }[p];
    if (go) $('#go-label').innerHTML = go;
  }
  function goAction() {
    if (S.page === 'comp') { setChoice(autoComp(), 'Best performance built from the highest-scoring lines.'); return; }
    if (S.page === 'lyrics' && Bridge.inHost) {
      const L = SS.listening;
      if (!SS.lines.length) return toast('Add your lyrics first: paste them, open a file, or use Hear It.', 4000);
      if (L.problem) return toast(L.problem, 6000);
      if (!SS.sungTakes) return toast('Record a take (or add one with +): your lyrics line up as soon as it has been listened to.', 5000);
      if (L.now || L.waiting) return toast(`Still listening: ${L.now || 'the next take'}${L.now ? ` (${L.percent}%)` : ''}. Lines move into place as each take is done.`, 4000);
      const found = SS.lines.filter((l) => l.takes > 0).length;
      return toast(`Lined up: ${found} of ${SS.lines.length} lines found in your takes. It happens by itself whenever lyrics or takes change.`, 5000);
    }
    const m = { lyrics: ['Lining up lyrics', 'M2'], tighten: ['Tightening and rendering', 'M5'], review: ['Scoring real takes', 'M3'], export: ['Rendering', 'M4'] }[S.page];
    if (m) soon(m[0], m[1]);
  }

  // ------------------------------------------------------------------ playback (demo: moves the playheads only)
  let raf = 0, lastT = 0, lastRec = 0;
  function play(force) {
    if (Bridge.inHost) { toast('Use your DAW\u2019s Play: Comp Captain follows it. (Hearing takes from here arrives in M4.)', 4000); return; }
    S.playing = force === true ? true : !S.playing;
    $('#t-play').innerHTML = icon(S.playing ? 'i-pause' : 'i-play');
    if (S.playing) { lastT = performance.now(); raf = requestAnimationFrame(tick); } else cancelAnimationFrame(raf);
    renderRecord(); renderFooter();
  }
  function stop() { S.playing = false; cancelAnimationFrame(raf); $('#t-play').innerHTML = icon('i-play'); renderRecord(); renderFooter(); }
  function tick(now) {
    const dt = Math.min(0.1, (now - lastT) / 1000); lastT = now;
    S.time += dt;
    const ln = D.lines[S.line];
    if (S.loop && S.time > ln.end + 0.3) S.time = ln.start - 0.2;
    if (S.page === 'record') {
      const sec = SS.sections[S.recSection];
      if (sec && sec.end > 0 && S.time > sec.end + 1) S.time = S.recLoop ? Math.max(0, sec.start - 1) : sec.end;
    }
    if (S.time >= D.length) { S.time = 0; stop(); }
    // follow the line being sung
    S.following = S.playing;
    const cur = SG.lineAt(S.time);
    if (cur >= 0 && cur !== S.sl && !S.loop) { S.sl = cur; S.line = Math.min(D.lines.length - 1, cur); S.recSection = Math.max(0, SG.sectionOf(cur)); renderLyricBar(); renderComp(); if (S.page === 'lyrics') renderLyricsList(); }
    else markSung();
    movePlayheads();
    if (S.page === 'record' && now - lastRec > 60) { lastRec = now; renderRecordLive(); }
    if (real() && S.page === 'record') T.drawRecord($('#rec-canvas'), $('#rec-bars'));
    if (!Bridge.inHost) { glowLevel = S.playing ? Math.max(0, D.vocalEnvelope(S.time, 222, 0)) : 0; setMeter(glowLevel); }
    if (S.playing) raf = requestAnimationFrame(tick);
  }
  function renderRecordLive() {
    renderPrompter();
    if (S.armed && !real()) drawRecWave();
  }

  // ------------------------------------------------------------------ following the DAW (inside the plug-in)
  // While the DAW plays, the lit line, its sung words and the teleprompter follow the playhead.
  let followRaf = 0, lastFollowDraw = 0;
  function follow(now) {
    followRaf = 0;
    const playing = TS.transport.known && TS.transport.playing;
    S.following = playing && SS.lines.some(SG.timed);
    if (S.following) {
      const cur = SG.lineAt(songTime());
      if (cur >= 0 && cur !== S.sl) {
        S.sl = cur; S.recSection = Math.max(0, SG.sectionOf(cur));
        renderLyricBar();
        if (S.page === 'lyrics') renderLyricsList();
        if (S.page === 'record') { $('#rec-section').textContent = SS.sections[S.recSection] ? SS.sections[S.recSection].name : ''; renderSectionLists(); }
      } else markSung();
      if (now - lastFollowDraw > 50) { lastFollowDraw = now; if (S.page === 'record') renderPrompter(); movePlayheads(); }
    }
    if (playing) followRaf = requestAnimationFrame(follow);
    else if (S.page === 'record') renderPrompter();
  }
  function startFollowing() {
    if (!Bridge.inHost) return;
    // stopped: the lit line is wherever the playhead was put
    if (!TS.transport.playing && SS.lines.some(SG.timed)) {
      const cur = SG.lineAt(songTime());
      if (cur >= 0 && cur !== S.sl && TS.transport.ppq !== lastStoppedAt) { lastStoppedAt = TS.transport.ppq; setLine(cur, false); }
    }
    if (!followRaf) followRaf = requestAnimationFrame(follow);
  }
  let lastStoppedAt = null;
  function setMeter(v) {
    const m = $('#rec-meter'); if (!m.children.length) for (let i = 0; i < 14; i++) m.appendChild(h('i', i >= 12 ? 'r' : i >= 9 ? 'y' : ''));
    const lit = Math.round(Math.min(1, v) * 14);
    Array.from(m.children).forEach((e, i) => { e.classList.toggle('lit', i < lit); e.classList.toggle('y', i >= 9 && i < 12); e.classList.toggle('r', i >= 12); });
  }

  // ------------------------------------------------------------------ overlays
  function openSheet(id) { $(id).hidden = false; }
  function renderSettings() {
    const sb = $('#settings-body');
    const st = (k) => ({ linked: 'Connected in this project', installed: 'Installed', missing: 'Not installed' }[S.sonicology[k]]);
    sb.innerHTML = `
      <div class="set-row"><div>Look<small>Or click the logo</small></div><div class="seg" id="set-theme"><button data-v="dark">Dark</button><button data-v="light">Light</button></div></div>
      <div class="set-row"><div>Window size<small>Drag the corner in your DAW, or pick one</small></div><div class="seg" id="set-size"><button data-v="0.6">S</button><button data-v="0.8">M</button><button data-v="1">L</button></div></div>
      <div class="set-row"><div>Takes folder<small class="path">${escapeHtml(TS.folder || (TS.takesRoot ? TS.takesRoot + ' (a folder per track is made at the first take)' : 'Documents / Comp Captain'))}</small></div><button class="pill" id="set-folder">Open</button></div>
      <div class="set-row"><div>Loop Takes<small>Each time your DAW's loop comes round, a new take (off: only the first pass)</small></div><div class="seg" id="set-loop"><button data-v="1">On</button><button data-v="0">Off</button></div></div>
      <div class="set-row"><div>ARA (Cubase Extension)<small>What your DAW shared with Comp Captain, to send back if something looks wrong</small></div><div class="btn-row"><button class="pill" id="set-report">Show</button><button class="pill" id="set-copy">Copy</button></div></div>
      <pre class="report" id="set-report-text" hidden></pre>
      <div class="set-row"><div>Background listening<small>Uses at most half your computer's cores, never the audio thread</small></div><span class="gold">On</span></div>
      <div class="set-row"><div>Voxology<small>${st('voxology')}</small></div><span class="dot ${S.sonicology.voxology === 'linked' ? 'on' : ''}"></span></div>
      <div class="set-row"><div>Mixology<small>${st('mixology')}</small></div><span class="dot ${S.sonicology.mixology === 'linked' ? 'on' : ''}"></span></div>
      <p class="dim small" style="margin-top:14px">Comp Captain works fully on its own. It works even better with the Sonicology suite (Voxology, Mixology).</p>
      <p class="dim small">Comp Captain ${S.version} \u00B7 ${S.host}</p>`;
    $$('#set-theme button').forEach((b) => { b.classList.toggle('on', b.dataset.v === S.theme); b.onclick = () => { setTheme(b.dataset.v, true); renderSettings(); }; });
    $$('#set-size button').forEach((b) => { b.onclick = () => Bridge.inHost ? Bridge.send('size', { scale: parseFloat(b.dataset.v) }) : toast('In the plug-in this resizes the window.'); });
    $('#set-folder').onclick = () => T.openFolder();
    $$('#set-loop button').forEach((b) => { b.classList.toggle('on', (b.dataset.v === '1') === TS.capture.loopTakes); b.onclick = () => { T.setLoopTakes(b.dataset.v === '1'); setTimeout(renderSettings, 150); }; });
    $('#set-report').onclick = () => T.askReport(false);
    $('#set-copy').onclick = () => T.askReport(true);
    if (TS.report) { $('#set-report-text').hidden = false; $('#set-report-text').textContent = TS.report; }
  }

  // ------------------------------------------------------------------ redraw all canvases (size or theme change)
  let redrawQueued = false;
  function redrawCanvases() {
    if (redrawQueued) return; redrawQueued = true;
    requestAnimationFrame(() => {
      redrawQueued = false;
      renderLyricBar(); renderComp(); renderRecord(); renderTighten();
    });
  }

  // ------------------------------------------------------------------ wiring
  function wire() {
    $('#logo').onclick = () => setTheme(S.theme === 'dark' ? 'light' : 'dark', true);
    $$('.nav-btn').forEach((b) => { b.onclick = () => setPage(b.dataset.go, true); });
    $('#line-prev').onclick = () => setLine(S.line - 1, true);
    $('#line-next').onclick = () => setLine(S.line + 1, true);
    $('#ov-left').onclick = () => setLine(S.line - 1, true);
    $('#ov-right').onclick = () => setLine(S.line + 1, true);
    $('#overview').onclick = (e) => {
      const sp = SG.span(); if (!sp) return;
      const r = e.currentTarget.getBoundingClientRect(), t = sp[0] + (e.clientX - r.left) / r.width * (sp[1] - sp[0]);
      if (!Bridge.inHost) S.time = t;
      const c = SG.lineAt(t); if (c >= 0) setLine(c, false);
    };
    $('#t-play').onclick = () => play(); $('#t-stop').onclick = stop; $('#bc-play').onclick = () => play();
    $('#t-start').onclick = () => { if (!Bridge.inHost) S.time = 0; setLine(0, false); };
    $('#t-loop').onclick = (e) => { S.loop = !S.loop; e.currentTarget.classList.toggle('on', S.loop); toast(S.loop ? 'Looping the current line' : 'Loop off'); };
    $('#t-ab').onclick = (e) => { e.currentTarget.classList.toggle('on'); toast(e.currentTarget.classList.contains('on') ? 'Hearing: before' : 'Hearing: after'); };
    $('#t-solo').onclick = (e) => e.currentTarget.classList.toggle('on');
    bindSlider($('.topbar .vol'));
    bindSlider($('#picky'), (v) => toast(v < 0.5 ? 'Fewer cuts: sounds more like one performance' : 'Best every line: picks the top take for each line'));
    $('#track-select').onclick = (e) => {
      if (real()) return T.trackMenu(e.currentTarget);
      menu(e.currentTarget, ['Vocal \u2013 Lead', 'Vocal \u2013 Double L', 'Vocal \u2013 Double R', 'Vocal \u2013 Ad-Libs', 'BV \u2013 Stack', 'Guitar \u2013 Rhythm'], (it) => { $('#track-name').textContent = it; $('#track-name').dataset.custom = it; toast('In the plug-in, every track with Comp Captain on it shows here.'); });
    };
    $('#comp-style').onclick = (e) => menu(e.currentTarget, Object.keys(D.styles), setStyle);
    $$('#view-seg button').forEach((b) => { b.onclick = () => setCompView(b.dataset.view, true); });
    $('#auto-comp').onclick = () => setChoice(autoComp(), 'Auto Comp picked the best take for every line.');
    $('#swap-take').onclick = () => {
      const li = S.line, order = D.scores[li].map((s, i) => [total(s, S.weights), i]).sort((a, b) => b[0] - a[0]).map((x) => x[1]);
      const next = order[(order.indexOf(S.choice[li]) + 1) % order.length];
      const n = S.choice.slice(); n[li] = next; setChoice(n, `Line ${li + 1}: now using ${D.takes[next].name}`);
    };
    $('#undo').onclick = () => { if (!S.undo.length) return toast('Nothing to undo'); S.redo.push(S.choice); S.choice = S.undo.pop(); renderComp(); renderReview(); renderFooter(); };
    $('#redo').onclick = () => { if (!S.redo.length) return toast('Nothing to redo'); S.undo.push(S.choice); S.choice = S.redo.pop(); renderComp(); renderReview(); renderFooter(); };
    $('#go-btn').onclick = goAction;
    // record page
    $('#r-arm').onclick = () => {
      if (real()) {
        if (TS.ara || TS.capture.ara) return toast('This Comp Captain is a Cubase Extension: its takes come from the lanes. Use the one on the track\'s inserts to capture.', 5000);
        if (TS.standalone) return toast('The standalone app has no DAW to record with. Put Comp Captain on a track in your DAW to capture takes, or add files with +.', 5000);
        const on = !TS.capture.armed; T.setArmed(on);
        toast(on ? 'Armed. Press Record in your DAW: every pass becomes a take. (The track must be monitoring.)' : 'Disarmed', 4000);
        return;
      }
      S.armed = !S.armed; toast(S.armed ? 'Armed. Press Record in your DAW to start the take.' : 'Disarmed'); renderRecord(); renderFooter();
    };
    $('#r-play').onclick = () => play(); $('#r-stop').onclick = stop;
    const goSection = (i) => {
      if (!SS.sections.length) return toast('Add your lyrics with [Verse 1], [Chorus]\u2026 headings on the Lyrics page to step through sections.', 4000);
      S.recSection = Math.max(0, Math.min(SS.sections.length - 1, i));
      setLine(SS.sections[S.recSection].first, true);
    };
    $('#r-start').onclick = () => goSection(S.recSection);
    $('#rec-sec-prev').onclick = () => goSection(S.recSection - 1);
    $('#rec-sec-next').onclick = () => goSection(S.recSection + 1);
    $('#rec-loop').onclick = () => { S.recLoop = !S.recLoop; renderRecord(); };
    $('#rec-click').onclick = () => { S.recClick = !S.recClick; renderRecord(); };
    const stepTake = (d) => {
      if (!real()) return toast('Takes are counted for you as you record.');
      if (!TS.list.length) return;
      const i = TS.list.findIndex((x) => x.id === TS.sel);
      T.select(TS.list[Math.max(0, Math.min(TS.list.length - 1, (i < 0 ? TS.list.length : i) + d))].id);
    };
    $('#take-prev').onclick = () => stepTake(-1);
    $('#take-next').onclick = () => stepTake(1);
    $('#add-files').onclick = () => T.addFiles();
    $('#open-folder').onclick = () => T.openFolder();
    $('#sv-add').onclick = () => T.addFiles();
    T.wireDrop(stage, $('#drop-hint'));
    // lyrics page
    $('#lyr-text').value = SS.text;
    $('#lyr-text').addEventListener('input', () => { S.lyrUndo = null; $('#lyr-undo').hidden = true; SG.setText($('#lyr-text').value); });
    $('#lyr-text').addEventListener('blur', () => SG.setText($('#lyr-text').value, true));
    $('#lyr-undo').onclick = () => { if (S.lyrUndo == null) return; const t = S.lyrUndo; S.lyrUndo = null; $('#lyr-undo').hidden = true; $('#lyr-text').value = t; SG.setText(t, true); toast('Your lyrics are back.'); };
    $('#lyr-paste').onclick = async () => {
      try { const t = await navigator.clipboard.readText(); if (t && t.trim()) { replaceLyrics(t, 'Lyrics pasted.'); return; } } catch (e) { /* clipboard blocked */ }
      $('#lyr-text').focus(); toast('Click in the lyrics box and press Ctrl+V (Cmd+V on Mac).');
    };
    const picker = h('input'); picker.type = 'file'; picker.accept = '.txt,.lrc,.docx'; picker.style.display = 'none'; document.body.appendChild(picker);
    $('#lyr-open').onclick = () => (Bridge.inHost ? SG.open() : picker.click());
    picker.onchange = () => {
      const f = picker.files[0]; if (!f) return;
      if (/\.docx$/i.test(f.name)) { toast('In the plug-in, Word files open too.'); picker.value = ''; return; }
      f.text().then((t) => { replaceLyrics(t, `Opened ${f.name}.`); picker.value = ''; });
    };
    $('#lyr-hear').onclick = () => {
      if (!Bridge.inHost) return toast('In the plug-in, Hear It writes the lyrics from your lead take.');
      const sel = T.selected();
      SG.hearIt(sel && sel.heard === 'done' ? sel.id : null);
    };
    $('#lyr-extras').onclick = (e) => menu(e.currentTarget, [
      { label: 'Song Rider Pro song\u2026', value: 'songrider' },
      { label: 'Use its Export button first: it saves the song as a page (usually in Downloads)', header: true },
      { label: 'Song Savant and Key Compass songs hold chords, not words', header: true },
    ], (it) => {
      if (it.value !== 'songrider') return;
      if (!Bridge.inHost) return toast('In the plug-in, this opens a song page exported from Song Rider Pro.');
      SG.open('songrider');
    });
    T.setLyricsDrop((f) => {
      f.arrayBuffer().then((buf) => {
        const bytes = new Uint8Array(buf);
        let bin = '';
        for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
        if (Bridge.inHost) SG.sendFile(f.name, btoa(bin));
        else f.text().then((t) => replaceLyrics(t, `Opened ${f.name}.`));
      });
    });
    // tighten
    $$('#layer-tabs button').forEach((b) => { b.onclick = () => { S.tab = b.dataset.tab; renderTighten(); }; });
    $$('#ba-seg button').forEach((b) => { b.onclick = () => { S.ba = b.dataset.ba; renderTighten(); }; });
    // export
    $('#drag-tile').addEventListener('dragstart', (e) => { e.preventDefault(); soon('Drag to DAW', 'M4'); });
    $('#drag-tile').onclick = () => soon('Drag to DAW', 'M4');
    $$('#sonicology-box .send-btn').forEach((b) => { b.onclick = () => sendTo(b.dataset.send); });
    // overlays
    $('#btn-help').onclick = () => openSheet('#manual');
    $('#btn-settings').onclick = () => { renderSettings(); openSheet('#settings'); };
    $$('[data-close]').forEach((b) => { b.onclick = () => { b.closest('.overlay').hidden = true; }; });
    $$('.overlay').forEach((o) => { o.addEventListener('pointerdown', (e) => { if (e.target === o) o.hidden = true; }); });
    $('#manual-body').innerHTML = Manual;
    // keys: up/down = step through takes of this line, left/right = lines, space = play
    document.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'TEXTAREA') return;
      if (e.key === 'Escape') { $$('.overlay').forEach((o) => { o.hidden = true; }); closeMenu(); }
      if (S.page !== 'comp') return;
      if (e.key === 'ArrowLeft') { setLine(S.line - 1, true); e.preventDefault(); }
      if (e.key === 'ArrowRight') { setLine(S.line + 1, true); e.preventDefault(); }
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
        const n = S.choice.slice(), d = e.key === 'ArrowUp' ? -1 : 1;
        n[S.line] = (n[S.line] + d + D.takes.length) % D.takes.length; setChoice(n); e.preventDefault();
      }
      if (e.key === ' ') { play(); e.preventDefault(); }
    });
    window.addEventListener('resize', fitStage);
  }

  // ------------------------------------------------------------------ messages from the plug-in
  Bridge.on('init', (m) => {
    if (m.version) S.version = m.version;
    stage.dataset.ara = m.ara ? '1' : '';
    if (m.host) S.host = m.host;
    if (m.sonicology) S.sonicology = Object.assign(S.sonicology, m.sonicology);
    if (m.theme) setTheme(m.theme, false);
    if (m.page) setPage(m.page, false);
    if (m.compView) setCompView(m.compView, false);
    renderExport(); renderFooter();
  });
  Bridge.on('level', (m) => {
    const v = Math.max(0, Math.min(1, +m.peak || 0));
    const db = v > 0 ? 20 * Math.log10(v) : -100;
    const n = Math.max(0, Math.min(1, (db + 48) / 48));
    if (Math.abs(n - glowLevel) < 0.01 && n < 0.01) return;
    glowLevel = n; setMeter(n);
  });

  // ------------------------------------------------------------------ lyrics from the plug-in (song.js)
  Bridge.on('lyricsLoaded', (m) => {
    if (m.error) { toast(m.error, 6000); return; }
    replaceLyrics(m.text || '', `Opened ${m.name || 'the file'}.`);
  });
  Bridge.on('hearIt', (m) => {
    if (m.error) { toast(m.error, 6000); return; }
    const text = m.text || '';
    if (!$('#lyr-text').value.trim()) { replaceLyrics(text, `Written from ${m.from}. Check the words, then add [Verse 1], [Chorus]\u2026 headings.`); return; }
    lyricsDialog(`Lyrics heard in ${escapeHtml(m.from || 'your take')}`, 'Put them in place of your lyrics, or add them below?', [
      { label: 'Cancel' },
      { label: 'Add below', fn: () => replaceLyrics($('#lyr-text').value.replace(/\s*$/, '\n\n') + text, `Added what was heard in ${m.from}.`) },
      { label: 'Replace', main: true, fn: () => replaceLyrics(text, `Written from ${m.from}.`) },
    ]);
  });
  let lyricsQueued = false;
  SG.onChange((what) => {
    if (what === 'listening') { if (S.page === 'lyrics') { renderLyricsStatus(); renderFooter(); } return; }
    if (lyricsQueued) return; lyricsQueued = true;
    requestAnimationFrame(() => {
      lyricsQueued = false;
      if (S.sl >= SS.lines.length) S.sl = Math.max(0, SS.lines.length - 1);
      if (S.recSection >= SS.sections.length) S.recSection = Math.max(0, SS.sections.length - 1);
      showLyricsText(false);
      renderLyricBar(); renderLyricsList(); renderRecord(); renderFooter();
      startFollowing();
    });
  });

  // ------------------------------------------------------------------ real takes changed (takes.js)
  let realQueued = false;
  function realRedraw() {
    if (realQueued) return; realQueued = true;
    requestAnimationFrame(() => {
      realQueued = false;
      if (S.page === 'record') renderRecord();
      if (S.page === 'comp') renderComp();
      if (S.page === 'record') renderFooter();
      $('#track-name').textContent = T.trackLabel();
    });
  }
  T.setToast(toast);
  T.setMenu(menu);
  T.onChange((what) => {
    if (!real()) return;
    if (what === 'transport') {
      startFollowing();
      // only the numbers and playheads move
      if (S.page === 'record') {
        const t = TS.transport;
        $('#rec-bpm').textContent = t.known ? String(Math.round(t.bpm * 100) / 100) : '\u2013';
        $('#rec-sig').textContent = t.known ? `${t.num}/${t.den}` : '\u2013/\u2013';
        T.drawRecord($('#rec-canvas'), $('#rec-bars'));
      } else if (S.page === 'comp' && S.compView === 'song') T.drawLanes($('#lanes'), $('#lane-lyrics'), laneBand());
      return;
    }
    if (what === 'report') { if (!$('#settings').hidden) renderSettings(); return; }
    if (what === 'takes' && S.page === 'lyrics') renderLyricsStatus();
    realRedraw();
  });

  // ------------------------------------------------------------------ start
  wire();
  T.mockStart();
  setTheme(S.theme, false);
  setPage(S.page, false);
  setCompView(S.compView, false);
  S.sl = Math.min(S.line, Math.max(0, SS.lines.length - 1)); S.recSection = Math.max(0, SG.sectionOf(S.sl));
  renderLyricsList(); renderReview(); renderExport(); renderFooter(); setMeter(0);
  fitStage();
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(redrawCanvases);
  Bridge.send('ready', {});
})();
