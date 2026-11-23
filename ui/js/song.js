// The song's lyrics (M2): lines and sections, and where each line sits in the song (worked out by the
// plug-in from every take it has listened to). Inside the plug-in everything comes from the C++ side; in a
// plain browser the sample song (demo.js) stands in, so every page can still be seen working.
'use strict';

const Song = (() => {
  const st = {
    real: Takes.st.real,
    got: false,            // the plug-in has sent the lyrics at least once
    text: '',
    lines: [],             // { text, section, start, end, startQ, endQ, sure, takes, from, words: [[s, e, heard]], shown: [...] }
    sections: [],          // { name, first, count, start, end }
    linedUp: 0, sungTakes: 0, heardTakes: 0,
    listening: { now: '', percent: 0, waiting: 0, paused: false, problem: '' },
  };

  const listeners = [];
  const changed = (what) => listeners.forEach((fn) => fn(what));
  const onChange = (fn) => listeners.push(fn);

  // A line's words as shown (split at spaces): the plug-in sends one time per shown word.
  const shownWords = (text) => String(text || '').split(/[ \t\r\n]+/).filter(Boolean);
  const timed = (l) => l && l.start >= 0 && l.end >= l.start;

  // Sections take their times from their first and last lines.
  function finish() {
    st.lines.forEach((l) => { l.shown = shownWords(l.text); });
    st.sections.forEach((s) => {
      const ls = st.lines.slice(s.first, s.first + s.count).filter(timed);
      s.start = ls.length ? ls[0].start : -1;
      s.end = ls.length ? ls[ls.length - 1].end : -1;
    });
  }

  Bridge.on('lyrics', (m) => {
    st.got = true;
    st.text = m.text || '';
    st.lines = Array.isArray(m.lines) ? m.lines : [];
    st.sections = Array.isArray(m.sections) ? m.sections : [];
    st.linedUp = m.linedUp || 0; st.sungTakes = m.sungTakes || 0; st.heardTakes = m.heardTakes || 0;
    finish();
    changed('lyrics');
  });
  Bridge.on('listening', (m) => {
    st.listening = { now: m.now || '', percent: m.percent || 0, waiting: m.waiting || 0, paused: !!m.paused, problem: m.problem || '' };
    changed('listening');
  });

  // ------------------------------------------------------------------ typing in the Lyrics box
  let sendTimer = 0, lastSent = null;
  function setText(text, now) {
    lastSent = text;
    if (!Bridge.inHost) { previewParse(text); return; }
    clearTimeout(sendTimer);
    const go = () => Bridge.send('lyricsSet', { text });
    if (now) go(); else sendTimer = setTimeout(go, 450);
  }
  const sentLast = () => lastSent;

  // Browser preview only: a simple reading of the text (the plug-in's own is much fuller).
  function previewParse(text) {
    const lines = [], sections = [];
    String(text).split(/\r?\n/).forEach((raw) => {
      const line = raw.replace(/^\[\d+:\d+(\.\d+)?\]/, '').trim();
      if (!line) return;
      const m = line.match(/^\[(.+)\]$/) || line.match(/^(verse|chorus|bridge|intro|outro|hook|pre-?chorus)(\s*\d*):?$/i);
      if (m) { sections.push({ name: m[1].replace(/\b\w/g, (c) => c.toUpperCase()) + (m[2] || ''), first: lines.length, count: 0 }); return; }
      if (!sections.length) sections.push({ name: 'Lyrics', first: 0, count: 0 });
      sections[sections.length - 1].count++;
      lines.push({ text: line, section: sections.length - 1, start: -1, end: -1, startQ: -1, endQ: -1, sure: 0, takes: 0, words: [] });
    });
    st.text = text;
    st.sections = sections.filter((s) => s.count > 0);
    let k = 0; const remap = sections.map((s) => (s.count > 0 ? k++ : -1));
    lines.forEach((l) => { l.section = remap[l.section]; });
    st.lines = lines;
    finish();
    changed('lyrics');
  }

  // The sample song for the browser preview.
  function sample() {
    const D = Demo;
    st.text = '';
    let last = -1;
    D.lines.forEach((l) => { if (l.section !== last) { st.text += (st.text ? '\n' : '') + `[${D.sections[l.section].name}]\n`; last = l.section; } st.text += l.text + '\n'; });
    const secIndex = {}; st.sections = [];
    D.lines.forEach((l, i) => {
      if (secIndex[l.section] === undefined) { secIndex[l.section] = st.sections.length; st.sections.push({ name: D.sections[l.section].name, first: i, count: 0 }); }
      st.sections[secIndex[l.section]].count++;
    });
    const perQ = D.bpm / 60;
    st.lines = D.lines.map((l) => ({ text: l.text, section: secIndex[l.section], start: l.start, end: l.end, startQ: l.start * perQ, endQ: l.end * perQ,
      sure: 0.95, takes: D.takes.length, from: 'Take 2 (Lead Vox)', words: l.words.map((w) => [w.start, w.end, 1]) }));
    st.linedUp = D.takes.length; st.sungTakes = D.takes.length; st.heardTakes = D.takes.length;
    finish();
  }
  if (!Bridge.inHost) sample();

  // ------------------------------------------------------------------ where things are
  // The line being sung at time t (seconds), or the next one coming; -1 if the song has no times.
  function lineAt(t) {
    const ls = st.lines;
    if (!ls.length || !ls.some(timed)) return -1;
    for (let i = 0; i < ls.length; i++) {
      if (!timed(ls[i])) continue;
      const next = ls.slice(i + 1).find(timed);
      // a line stays lit until halfway into the gap before the next one
      const until = next ? Math.max(ls[i].end, (ls[i].end + next.start) / 2) : Infinity;
      if (t < until) return i;
    }
    return ls.length - 1;
  }
  function sectionOf(li) { const l = st.lines[li]; return l ? l.section : -1; }
  // [start, end] seconds around the whole song's lines (with a little room), or null without times.
  function span() {
    const ls = st.lines.filter(timed);
    if (!ls.length) return null;
    const a = Math.max(0, ls[0].start - 2), b = ls[ls.length - 1].end + 2;
    return [a, Math.max(b, a + 4)];
  }
  // A take that sang this line, as words for the Lyrics page.
  function whereFrom(l) {
    if (!l) return '';
    if (l.takes > 0) return `Heard in ${l.takes} take${l.takes === 1 ? '' : 's'}${l.from ? ` \u00B7 timing from ${l.from}` : ''}`;
    if (timed(l)) return 'Not heard in any take yet: placed between the lines around it';
    return st.heardTakes ? 'Not heard in any take yet' : 'Lines get their times once a take is listened to';
  }

  // Asking the plug-in for things
  const open = (from) => Bridge.send('lyricsOpen', from ? { from } : {});
  const sendFile = (name, b64) => Bridge.send('lyricsFile', { name, data: b64 });
  const hearIt = (id) => Bridge.send('hearIt', id ? { id } : {});
  const listenAgain = () => Bridge.send('listenAgain', {});

  return { st, onChange, setText, sentLast, shownWords, timed, lineAt, sectionOf, span, whereFrom, open, sendFile, hearIt, listenAgain };
})();
