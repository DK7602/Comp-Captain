// Comp Captain interface: pages, made-up takes (M0), light / dark, manual and settings.
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
  const PAGES = ['record', 'lyrics', 'comp', 'tighten', 'review', 'export'];

  // ------------------------------------------------------------------ state
  const S = {
    theme: Bridge.localGet('theme', 'dark'),
    page: Bridge.localGet('page', 'comp'),
    compView: Bridge.localGet('compView', 'line'),
    line: 2,
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
    version: '0.1.0', host: 'Browser preview',
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
  function toast(text) {
    const t = $('#toast'); t.textContent = text; t.hidden = false;
    clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.hidden = true; }, 2600);
  }
  const soon = (what, milestone) => toast(`${what} arrives in ${milestone}. This version is the look only.`);

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
    Object.assign(m.style, { left: (r.left - sr.left) / k + 'px', top: (r.bottom - sr.top) / k + 4 + 'px', minWidth: r.width / k + 'px', zIndex: 40, padding: '6px' });
    items.forEach((it) => {
      const b = h('button', 'menu-item', it);
      Object.assign(b.style, { display: 'block', width: '100%', textAlign: 'left', border: '0', background: 'none', padding: '7px 10px', fontSize: '17px', borderRadius: '5px' });
      b.onmouseenter = () => { b.style.background = 'rgba(255,200,60,.18)'; }; b.onmouseleave = () => { b.style.background = 'none'; };
      b.onclick = (e) => { e.stopPropagation(); closeMenu(); onPick(it); };
      m.appendChild(b);
    });
    stage.appendChild(m); openMenu = m;
  }
  function closeMenu() { if (openMenu) { openMenu.remove(); openMenu = null; } }
  document.addEventListener('pointerdown', (e) => { if (openMenu && !openMenu.contains(e.target)) closeMenu(); });

  // ------------------------------------------------------------------ header glow (pulses with your voice)
  const glow = $('#waveglow');
  let glowLevel = 0;
  function drawGlow() {
    const p = Wave.prep(glow); if (!p) return;
    const { ctx, w, h: hh } = p, mid = hh / 2 - 2;
    const lvl = 0.62 + 0.38 * glowLevel;
    const group = (x0, x1, color, fadeIn) => {
      ctx.strokeStyle = color; ctx.shadowColor = color; ctx.shadowBlur = 8; ctx.lineWidth = 2.2;
      ctx.beginPath();
      for (let x = x0; x <= x1; x += 5) {
        const f = (x - x0) / (x1 - x0);
        const env = fadeIn ? Math.sin(Math.PI * Math.pow(f, 0.8)) * f : Math.sin(Math.PI * f) * (1 - f * 0.6);
        const n = 0.35 + 0.65 * Wave.hash(Math.floor(x / 5) * 3.1);
        const a = Math.max(2, env * n * 46 * lvl);
        ctx.moveTo(x, mid - a); ctx.lineTo(x, mid + a);
      }
      ctx.stroke();
    };
    group(10, 205, 'rgba(70,170,255,.85)', true);     // left of the logo
    group(360, 440, 'rgba(255,190,60,.75)', false);   // between logo and title
    ctx.shadowBlur = 0;
    const g = ctx.createLinearGradient(0, 0, 220, 0); g.addColorStop(0, 'rgba(70,170,255,0)'); g.addColorStop(1, 'rgba(120,200,255,.8)');
    ctx.strokeStyle = g; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, mid); ctx.lineTo(205, mid); ctx.stroke();
  }

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

  // ------------------------------------------------------------------ shared lyric bar
  const tToX = (t, w) => (t / D.length) * w;
  function renderLyricBar() {
    const ln = D.lines[S.line];
    const lt = $('#line-text'); lt.innerHTML = '';
    ln.words.forEach((wd, i) => {
      const s = h('span', S.time >= wd.end || (S.time >= wd.start && S.time > ln.start) && S.time < ln.end && S.time >= wd.start ? 'sung' : '', (i ? ' ' : '') + wd.w);
      lt.appendChild(s);
    });
    const chips = $('#section-chips'); chips.innerHTML = '';
    const cw = 1672 - 0; // positions in %, so width doesn't matter
    D.sections.forEach((sec, i) => {
      const c = h('div', 'chip' + (i % 2 ? '' : ' cyan') + (sec.id === ln.section ? ' on' : ''), sec.name);
      const a = sec.start / D.length * 100, b = sec.end / D.length * 100;
      c.style.left = `calc(${a}% + 3px)`; c.style.width = `calc(${b - a}% - 6px)`;
      c.onclick = () => { const first = D.lines.find((l) => l.section === sec.id); if (first) setLine(first.id, true); };
      chips.appendChild(c);
    });
    const ru = $('#ruler'); ru.innerHTML = '';
    for (let t = 0; t <= D.length; t += 8) { const s = h('span', '', fmtShort(t)); s.style.left = (t / D.length * 100) + '%'; ru.appendChild(s); }
    const s = h('span', '', fmtShort(D.length)); s.style.left = '100%'; ru.appendChild(s);
    drawOverview();
    movePlayheads();
  }
  function drawOverview() {
    const secColor = (t) => {
      const sec = D.sections.find((x) => t >= x.start && t < x.end) || D.sections[0];
      if (sec.id === D.lines[S.line].section) return '#ffc531';
      return ['#2f8fff', '#9c6bff', '#b76cff', '#7a5cff'][sec.id % 4];
    };
    Wave.draw($('#overview'), { t0: 0, t1: D.length, env: (t) => 0.25 + 0.75 * D.vocalEnvelope(t, 1, 0), color: secColor, seed: 3, step: 2, glow: 5 });
  }
  function movePlayheads() {
    $('#time-now').textContent = fmt(S.time);
    $('#time-len').textContent = fmt(D.length);
    const ov = $('#overview');
    $('#ov-playhead').style.left = (28 + ov.clientWidth * S.time / D.length) + 'px';
    // best comp playhead
    const span = compSpan();
    const bc = $('.bc-wave .p-head');
    if (bc) bc.style.left = Math.max(0, Math.min(100, (S.time - span[0]) / (span[1] - span[0]) * 100)) + '%';
    // record page
    const sec = D.sections[S.recSection];
    const f = Math.max(0, Math.min(1, (S.time - sec.start) / (sec.end - sec.start)));
    $$('.page[data-page="record"] .p-head').forEach((e) => { e.style.left = (f * 100) + '%'; });
  }
  function setLine(li, seek) {
    S.line = Math.max(0, Math.min(D.lines.length - 1, li));
    if (seek) S.time = D.lines[S.line].start;
    renderLyricBar(); renderComp(); renderRecord();
  }

  // ------------------------------------------------------------------ COMP page
  const compSpan = () => [D.lines[0].start - 0.6, D.lines[D.lines.length - 1].end + 0.6];
  function scoreCell(v, cls) { return `<div class="score ${cls || ''}">${v}%<i style="--w:${Math.max(0, (v - 60) / 40 * 100)}%"></i></div>`; }
  function renderComp() {
    const li = S.line, ln = D.lines[li], chosen = S.choice[li];
    const sec = D.sections[ln.section];
    $('#li-count').textContent = `Line ${li + 1} of ${D.lines.length} | ${sec.name}`;
    $('#li-text').textContent = `“${ln.text}”`;
    $('#track-name').textContent = $('#track-name').dataset.custom || `Vocal – Lead (${sec.name})`;

    // Line view
    const rows = $('#take-rows'); rows.innerHTML = '';
    D.takes.forEach((tk, ti) => {
      const sc = D.scores[li][ti];
      const r = h('div', 'take-row' + (ti === chosen ? ' chosen' : ''));
      r.innerHTML = `<span class="check">✓</span><button class="sq" title="Hear this take">${icon('i-play')}</button>
        <span class="name">${tk.name}</span>${tk.fav ? '<span class="fav" title="Favourite">♥</span>' : ''}<canvas></canvas>
        <span class="warn-badge" title="${sc.warn || ''}">${sc.warn ? '⚠' : ''}</span>
        <div class="scores">${scoreCell(sc.p)}${scoreCell(sc.t)}${scoreCell(sc.c)}${scoreCell(sc.e, 'e')}<div class="total">${Math.round(total(sc, S.weights))}%</div></div>`;
      r.onclick = (e) => {
        if (e.target.closest('.sq')) { S.time = ln.start; play(true); return; }
        if (ti !== S.choice[li]) { const n = S.choice.slice(); n[li] = ti; setChoice(n); }
      };
      rows.appendChild(r);
      const cv = $('canvas', r);
      Wave.draw(cv, { t0: ln.start - 0.25, t1: ln.end + 0.25, env: (t) => D.vocalEnvelope(t, tk.seed, tk.shift), color: tk.color, seed: tk.seed, step: 1.5, glow: 6 });
    });

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
      lane.innerHTML = `<div class="lane-name">${icon('i-play')}<span>${tk.name}</span><span class="dot" style="color:${tk.color}"></span></div><canvas></canvas><button class="more" title="More">•••</button>`;
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
    const sec = D.sections[S.recSection];
    $('#rec-section').textContent = sec.name;
    $('#r-arm').classList.toggle('armed', S.armed);
    $('#rec-loop').classList.toggle('on', S.recLoop);
    $('#rec-click').classList.toggle('on', S.recClick);
    // teleprompter: the line being sung (or next one) in the middle
    const secLines = D.lines.filter((l) => l.section === sec.id);
    let cur = D.lines.findIndex((l) => S.time < l.end);
    if (cur < 0) cur = D.lines.length - 1;
    if (!secLines.length) cur = S.line;
    const lineHTML = (l, live) => !l ? '' : l.words.map((w) => (live && S.time >= w.start ? `<span class="sung">${w.w}</span>` : w.w)).join(' ');
    $('#p-prev').innerHTML = lineHTML(D.lines[cur - 1], false);
    $('#p-now').innerHTML = lineHTML(D.lines[cur], true);
    $('#p-next').innerHTML = lineHTML(D.lines[cur + 1], false);
    fitPrompter();
    // bars and beats across the section
    const bars = $('#rec-bars'); bars.innerHTML = '';
    const beats = Math.round((sec.end - sec.start) / (D.bar / 4));
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
        row.innerHTML = `<span class="play">${icon('i-play')}</span><span class="nm">Take ${i + 1}</span><span class="empty">—</span>`;
      }
      list.appendChild(row);
    }
    $('#takes-count').textContent = `(${D.takes.length})`;
    $('#rec-take').textContent = `Take ${D.takes.length + 1} of ${totalTakes}`;
    renderSectionLists();
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
    const sec = D.sections[S.recSection];
    const upTo = S.armed && S.playing ? S.time : sec.end;
    Wave.draw($('#rec-canvas'), { t0: sec.start, t1: sec.end, env: (t) => (t <= upTo ? D.vocalEnvelope(t, 222, 0) : 0.01), color: '#ffc531', seed: 5, step: 1.6, glow: 7 });
  }
  function renderSectionLists() {
    for (const id of ['#sec-list', '#sec-list-2']) {
      const box = $(id); box.innerHTML = '';
      const seen = new Set();
      D.sections.forEach((sec) => {
        if (sec.name === 'Intro' || sec.name === 'Outro' || seen.has(sec.name)) return; seen.add(sec.name);
        const on = id === '#sec-list' ? D.sections[S.recSection].name === sec.name : D.sections[D.lines[S.line].section].name === sec.name;
        const r = h('div', 'sec-row' + (on ? ' on' : ''), `<span class="check">${on ? '✓' : ''}</span>${sec.name}`);
        r.onclick = () => {
          if (id === '#sec-list') { S.recSection = sec.id; S.time = sec.start; renderRecord(); movePlayheads(); }
          else { const first = D.lines.find((l) => l.section === sec.id); if (first) setLine(first.id, true); }
        };
        box.appendChild(r);
      });
    }
  }

  // ------------------------------------------------------------------ LYRICS page
  const demoLyricsText = () => {
    let out = '', last = -1;
    D.lines.forEach((l) => { if (l.section !== last) { out += (out ? '\n' : '') + `[${D.sections[l.section].name}]\n`; last = l.section; } out += l.text + '\n'; });
    return out;
  };
  function parseLyrics(text) {
    const res = []; let sec = 'Verse 1';
    text.split(/\r?\n/).forEach((raw) => {
      const line = raw.replace(/^\[\d+:\d+(\.\d+)?\]/, '').trim(); // .lrc time tags
      if (!line) return;
      const m = line.match(/^\[(.+)\]$/); if (m) { sec = m[1]; res.push({ head: sec }); return; }
      res.push({ text: line, section: sec });
    });
    return res;
  }
  function renderLyricsList() {
    const items = parseLyrics($('#lyr-text').value);
    const list = $('#lyr-list'); list.innerHTML = '';
    let n = 0;
    items.forEach((it) => {
      if (it.head) { list.appendChild(h('div', 'll head', `<span class="sec">${it.head.toUpperCase()}</span>`)); return; }
      const demo = D.lines[n];
      const r = h('div', 'll');
      if (demo) {
        const a = demo.start / D.length * 100, b = demo.end / D.length * 100;
        r.innerHTML = `<span class="t">${fmtShort(demo.start)}</span><span class="txt">${escapeHtml(it.text)}</span><span class="span"><i style="left:${a}%;width:${Math.max(2, b - a)}%"></i></span>`;
      } else {
        r.innerHTML = `<span class="t">–</span><span class="txt">${escapeHtml(it.text)}</span><span class="dim small">not lined up yet</span>`;
      }
      list.appendChild(r); n++;
    });
  }
  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

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
      const r = h('div', 'lc', `<span class="check ${S.layerOn[l.key] ? 'on' : ''}">✓</span><span>${l.name}</span>${sliderHTML((db + 24) / 30)}<span class="row-val">${db.toFixed(1)} dB</span>`);
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
      lane.innerHTML = `<div class="nm">${l.name}<small>${l.role}${l === lead ? '' : ' · ' + (S.layerOn[l.name] === false ? 'muted' : (S.ba === 'before' ? 'as recorded' : `within ${Math.max(2, Math.round(miss * 1000))} ms`))}</small></div><canvas></canvas>`;
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
      const r = h('div', 'rv', `<span class="n">${i + 1}</span><span class="sec">${D.sections[l.section].name}</span><span class="txt">${l.text}</span><span class="w" title="${sc.warn || ''}">${sc.warn ? '⚠' : ''}</span>
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
      const r = h('div', 'ex', `<span class="check ${S.exportOn[name] ? 'on' : ''}">✓</span><span>${name}</span><span class="role" style="color:${col}">${role}</span><span class="len">from bar 1.1</span><span class="len">${fmtShort(D.length)}</span>`);
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
  function renderFooter() {
    const p = S.page;
    const ov = Math.round(D.lines.reduce((a, l, i) => a + total(D.scores[i][S.choice[i]], S.weights), 0) / D.lines.length);
    const flagged = D.lines.filter((l, i) => D.scores[i][S.choice[i]].warn).length;
    const msgs = {
      record: [S.armed ? (S.playing ? `Recording Take ${D.takes.length + 1}` : `Armed: press Record in your DAW`) : 'Lyrics locked to the beat', ['Count-in on screen', 'Every pass becomes a take', 'Bar-stamped']],
      lyrics: [`${D.lines.length} lines lined up with your takes`, ['4 sections found', `Lined up on ${D.takes.length} takes`, 'Line 7: check the end']],
      comp: [`Comp complete! Best of ${D.takes.length} takes, line by line.`, ['Strongest pitch & timing', 'Consistent tone', 'High energy', 'Joins in the breaths']],
      tighten: ['Layers are tight and in time with the lead.', ['Doubles within 12 ms', 'Ad-libs left natural', 'BV lined up', 'Guitars on the grid']],
      review: [`Average score ${ov}%${flagged ? ` · ${flagged} line${flagged > 1 ? 's' : ''} to check` : ''}`, [`Style: ${S.style}`, 'Hearts win ties', 'Click a line to comp it']],
      export: [`Ready: ${Object.values(S.exportOn).filter(Boolean).length || 6} files, all from bar 1.1`, ['Named by role', 'Works in any DAW', 'Nothing sent online']],
    }[p];
    $('#msg-title').textContent = msgs[0];
    const ticks = $('#msg-ticks'); ticks.innerHTML = ''; msgs[1].forEach((t) => ticks.appendChild(h('span', '', t)));

    const ob = $('#opt-box'); ob.className = 'opt-box' + (p === 'record' ? ' record' : ''); ob.innerHTML = '';
    if (p === 'record') {
      [['countIn', 'Count-in on screen', '(2 bars)'], ['loopTakes', 'Loop Takes', '(follows your DAW\'s loop)'], ['autoNext', 'Auto Next Take', '(after Stop)'], ['guide', 'Guide Vocal', '(hear your best take while you sing)']].forEach(([k, a, b]) => {
        const o = h('div', 'opt', `<span class="check ${S.recOpts[k] ? 'on' : ''}">✓</span><div>${a}<small>${b}</small></div>`);
        o.onclick = () => { S.recOpts[k] = !S.recOpts[k]; renderFooter(); };
        ob.appendChild(o);
      });
    } else {
      const opts = {
        lyrics: ['Tips', 'i-bulb', ['Put [Verse 1] above a part to make a section', 'Drag a line\'s ends to fix where it sits', 'No lyrics? Hear It writes them for you']],
        comp: ['Comp Builder', 'i-target', ['Build from the highest-scoring takes', 'Keep the strongest feel & performance', 'Blend joins in the breaths (no clicks)']],
        tighten: ['Suggestions (Optional)', 'i-bulb', ['Tighten doubles (−12 ms)', 'Match backing vocal level (+1.5 dB)', 'Line up Guitar Layer 2 with the grid']],
        review: ['What the scores mean', 'i-bulb', ['Pitch: on the notes · Timing: on the beat', 'Clarity: words clear · Energy: delivery', '⚠ a pop, clip or loud breath was heard']],
        export: ['Each file carries its role', 'i-bulb', ['Lead, Double, Ad-lib, Backing, Guitar', 'Voxology reads the role automatically', 'Plain WAV: opens in any DAW']],
      }[p];
      const checks = p === 'comp' || p === 'tighten';
      ob.appendChild(h('div', 'opt-title', `${icon(opts[1])}${opts[0]}`));
      opts[2].forEach((t) => ob.appendChild(h('div', 'opt', checks ? `<span class="check on">✓</span>${t}` : `<span class="gold">•</span>${t}`)));
    }
    sendButtons();
    const go = { lyrics: 'Line Up<br>Lyrics', comp: 'Build<br>Performance', tighten: 'Apply &amp;<br>Render', review: 'Re-score<br>All Takes', export: 'Render<br>All' }[p];
    if (go) $('#go-label').innerHTML = go;
  }
  function goAction() {
    if (S.page === 'comp') { setChoice(autoComp(), 'Best performance built from the highest-scoring lines.'); return; }
    const m = { lyrics: ['Lining up lyrics', 'M2'], tighten: ['Tightening and rendering', 'M5'], review: ['Scoring real takes', 'M3'], export: ['Rendering', 'M4'] }[S.page];
    if (m) soon(m[0], m[1]);
  }

  // ------------------------------------------------------------------ playback (demo: moves the playheads only)
  let raf = 0, lastT = 0, lastRec = 0;
  function play(force) {
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
      const sec = D.sections[S.recSection];
      if (S.time > sec.end) S.time = S.recLoop ? sec.start : sec.end;
    }
    if (S.time >= D.length) { S.time = 0; stop(); }
    // follow the line being sung
    const cur = D.lines.findIndex((l) => S.time >= l.start - 0.2 && S.time <= l.end + 0.2);
    if (cur >= 0 && cur !== S.line && !S.loop) { S.line = cur; renderLyricBar(); renderComp(); }
    else renderLyricBarText();
    movePlayheads();
    if (S.page === 'record' && now - lastRec > 60) { lastRec = now; renderRecordLive(); }
    if (!Bridge.inHost) { glowLevel = S.playing ? Math.max(0, D.vocalEnvelope(S.time, 222, 0)) : 0; setMeter(glowLevel); drawGlow(); }
    if (S.playing) raf = requestAnimationFrame(tick);
  }
  function renderLyricBarText() {
    const ln = D.lines[S.line];
    $$('#line-text span').forEach((s, i) => s.classList.toggle('sung', S.time >= ln.words[i].start && S.time >= ln.start));
  }
  function renderRecordLive() {
    let cur = D.lines.findIndex((l) => S.time < l.end); if (cur < 0) cur = D.lines.length - 1;
    const l = D.lines[cur];
    $('#p-prev').innerHTML = D.lines[cur - 1] ? D.lines[cur - 1].text : '';
    $('#p-now').innerHTML = l.words.map((w) => (S.time >= w.start ? `<span class="sung">${w.w}</span>` : w.w)).join(' ');
    $('#p-next').innerHTML = D.lines[cur + 1] ? D.lines[cur + 1].text : '';
    fitPrompter();
    if (S.armed) drawRecWave();
  }
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
      <div class="set-row"><div>Background listening<small>Uses at most half your computer's cores, never the audio thread</small></div><span class="gold">On</span></div>
      <div class="set-row"><div>Voxology<small>${st('voxology')}</small></div><span class="dot ${S.sonicology.voxology === 'linked' ? 'on' : ''}"></span></div>
      <div class="set-row"><div>Mixology<small>${st('mixology')}</small></div><span class="dot ${S.sonicology.mixology === 'linked' ? 'on' : ''}"></span></div>
      <p class="dim small" style="margin-top:14px">Comp Captain works fully on its own. It works even better with the Sonicology suite (Voxology, Mixology).</p>
      <p class="dim small">Comp Captain ${S.version} · ${S.host}</p>`;
    $$('#set-theme button').forEach((b) => { b.classList.toggle('on', b.dataset.v === S.theme); b.onclick = () => { setTheme(b.dataset.v, true); renderSettings(); }; });
    $$('#set-size button').forEach((b) => { b.onclick = () => Bridge.inHost ? Bridge.send('size', { scale: parseFloat(b.dataset.v) }) : toast('In the plug-in this resizes the window.'); });
  }

  // ------------------------------------------------------------------ redraw all canvases (size or theme change)
  let redrawQueued = false;
  function redrawCanvases() {
    if (redrawQueued) return; redrawQueued = true;
    requestAnimationFrame(() => {
      redrawQueued = false;
      drawGlow(); renderLyricBar(); renderComp(); renderRecord(); renderTighten();
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
    $('#overview').onclick = (e) => { const r = e.currentTarget.getBoundingClientRect(); S.time = (e.clientX - r.left) / r.width * D.length; const c = D.lines.findIndex((l) => S.time <= l.end + 0.2); setLine(c < 0 ? D.lines.length - 1 : c, false); };
    $('#t-play').onclick = () => play(); $('#t-stop').onclick = stop; $('#bc-play').onclick = () => play();
    $('#t-start').onclick = () => { S.time = 0; setLine(0, false); };
    $('#t-loop').onclick = (e) => { S.loop = !S.loop; e.currentTarget.classList.toggle('on', S.loop); toast(S.loop ? 'Looping the current line' : 'Loop off'); };
    $('#t-ab').onclick = (e) => { e.currentTarget.classList.toggle('on'); toast(e.currentTarget.classList.contains('on') ? 'Hearing: before' : 'Hearing: after'); };
    $('#t-solo').onclick = (e) => e.currentTarget.classList.toggle('on');
    bindSlider($('.topbar .vol'));
    bindSlider($('#picky'), (v) => toast(v < 0.5 ? 'Fewer cuts: sounds more like one performance' : 'Best every line: picks the top take for each line'));
    $('#track-select').onclick = (e) => menu(e.currentTarget, ['Vocal – Lead', 'Vocal – Double L', 'Vocal – Double R', 'Vocal – Ad-Libs', 'BV – Stack', 'Guitar – Rhythm'], (it) => { $('#track-name').textContent = it; $('#track-name').dataset.custom = it; toast('Tracks link up in M1. This is a preview.'); });
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
    $('#r-arm').onclick = () => { S.armed = !S.armed; Bridge.send('arm', { on: S.armed }); toast(S.armed ? 'Armed. Press Record in your DAW to start the take.' : 'Disarmed'); renderRecord(); renderFooter(); };
    $('#r-play').onclick = () => play(); $('#r-stop').onclick = stop;
    $('#r-start').onclick = () => { S.time = D.sections[S.recSection].start; renderRecord(); movePlayheads(); };
    $('#rec-sec-prev').onclick = () => { S.recSection = Math.max(0, S.recSection - 1); S.time = D.sections[S.recSection].start; renderRecord(); movePlayheads(); };
    $('#rec-sec-next').onclick = () => { S.recSection = Math.min(D.sections.length - 1, S.recSection + 1); S.time = D.sections[S.recSection].start; renderRecord(); movePlayheads(); };
    $('#rec-loop').onclick = () => { S.recLoop = !S.recLoop; renderRecord(); };
    $('#rec-click').onclick = () => { S.recClick = !S.recClick; renderRecord(); };
    $('#take-prev').onclick = $('#take-next').onclick = () => toast('Takes are counted for you as you record.');
    // lyrics page
    $('#lyr-text').value = demoLyricsText();
    $('#lyr-text').addEventListener('input', renderLyricsList);
    $('#lyr-paste').onclick = async () => {
      try { const t = await navigator.clipboard.readText(); if (t) { $('#lyr-text').value = t; renderLyricsList(); toast('Lyrics pasted'); return; } } catch (e) { /* clipboard blocked */ }
      $('#lyr-text').focus(); toast('Click in the lyrics box and press Ctrl+V (Cmd+V on Mac).');
    };
    const picker = h('input'); picker.type = 'file'; picker.accept = '.txt,.lrc,.docx'; picker.style.display = 'none'; document.body.appendChild(picker);
    $('#lyr-open').onclick = () => picker.click();
    picker.onchange = () => {
      const f = picker.files[0]; if (!f) return;
      if (/\.docx$/i.test(f.name)) { soon('Opening Word files', 'M2'); picker.value = ''; return; }
      f.text().then((t) => { $('#lyr-text').value = t; renderLyricsList(); toast(`Opened ${f.name}`); picker.value = ''; });
    };
    $('#lyr-hear').onclick = () => soon('Hear It (lyrics written from your takes)', 'M2');
    $('#lyr-extras').onclick = (e) => menu(e.currentTarget, ['Song Rider Pro song…', 'Song Savant song…', 'Key Compass song…'], () => soon('Opening songs from your other apps', 'M2'));
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
    glowLevel = n; setMeter(n); drawGlow();
  });

  // ------------------------------------------------------------------ start
  wire();
  setTheme(S.theme, false);
  setPage(S.page, false);
  setCompView(S.compView, false);
  renderLyricsList(); renderReview(); renderExport(); renderFooter(); setMeter(0);
  fitStage();
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(redrawCanvases);
  Bridge.send('ready', {});
})();
