// Made-up song and takes for M0, so every page can be seen working before real audio arrives (M1+).
'use strict';

const Demo = (() => {
  // Small seeded random generator so the made-up waveforms look the same every time.
  function rng(seed) {
    let s = seed >>> 0;
    return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  }

  const bpm = 92;
  const bar = 4 * 60 / bpm;               // seconds per bar
  const length = 63.772;

  const sectionDefs = [
    ['Intro', 0, 2], ['Verse 1', 2, 6], ['Pre', 6, 8], ['Chorus', 8, 11], ['Verse 2', 11, 14],
    ['Chorus', 14, 17], ['Bridge', 17, 19], ['Chorus', 19, 22], ['Outro', 22, length / bar],
  ];
  const sections = sectionDefs.map(([name, b0, b1], i) => ({ id: i, name, start: b0 * bar, end: Math.min(b1 * bar, length) }));

  const lineDefs = [
    [1, 'I was lost in the dark', 5.40, 7.60],
    [1, 'but You found me and gave me life', 7.90, 10.20],
    [1, 'You pulled me from the dark and gave me a brand new start', 10.50, 13.10],
    [1, "now I'm free in Your love", 13.30, 15.40],
    [2, "Every step I take, You're holding on", 15.90, 18.20],
    [2, 'lifting me up when the night is long', 18.50, 20.70],
    [3, "I'm alive, I'm alive", 21.10, 23.60],
    [3, 'singing out with all I am', 23.90, 26.40],
    [4, 'Morning light is breaking through', 29.00, 31.60],
    [4, 'every chain is falling too', 32.00, 34.60],
    [6, 'Hallelujah, I am Yours', 44.60, 47.40],
    [7, 'You gave me a brand new start', 50.00, 52.80],
  ];
  const lines = lineDefs.map(([sec, text, start, end], i) => {
    const words = text.split(' ');
    const step = (end - start) / words.length;
    return { id: i, section: sec, text, start, end, words: words.map((w, k) => ({ w, start: start + k * step, end: start + (k + 0.85) * step })) };
  });

  // Scores per take per line: [pitch, timing, clarity, energy]. Line 3 matches the owner's mockup.
  const takeCount = 5;
  const takeColors = ['#19d3ff', '#b05cff', '#2bea7a', '#6b7bff', '#ff5fc4', '#ffb340', '#4df0e0', '#ff7a5c'];
  const fixed = { 2: [[92, 95, 88, 86], [96, 97, 94, 95], [98, 96, 95, 94], [89, 91, 90, 88], [94, 93, 92, 92]] };
  const warnings = { '0,1': 'Small pop on "dark"', '3,4': 'Loud breath before the line', '6,0': 'Clipped on "alive"' };
  const r = rng(7);
  const scores = lines.map((ln, li) => {
    if (fixed[li]) return fixed[li].map((s) => ({ p: s[0], t: s[1], c: s[2], e: s[3] }));
    return Array.from({ length: takeCount }, () => ({
      p: Math.round(82 + r() * 16), t: Math.round(82 + r() * 16), c: Math.round(80 + r() * 18), e: Math.round(80 + r() * 18),
    }));
  });
  for (const key of Object.keys(warnings)) { const [li, ti] = key.split(',').map(Number); scores[li][ti].warn = warnings[key]; }

  const takes = Array.from({ length: takeCount }, (_, i) => ({ id: i, name: 'Take ' + (i + 1), color: takeColors[i], fav: i === 1, seed: 100 + i * 17, shift: (i - 2) * 0.012 }));

  const styles = {
    'Rap':              { p: 0,   t: .35, c: .35, e: .30 },
    'Trap':             { p: .10, t: .35, c: .25, e: .30 },
    'Singing':          { p: .35, t: .25, c: .20, e: .20 },
    'Pop':              { p: .30, t: .25, c: .25, e: .20 },
    'Country':          { p: .30, t: .25, c: .25, e: .20 },
    'Rock':             { p: .25, t: .25, c: .20, e: .30 },
    'Worship / Gospel': { p: .35, t: .20, c: .25, e: .20 },
    'R&B':              { p: .35, t: .20, c: .20, e: .25 },
    'My Own':           { p: .25, t: .25, c: .25, e: .25 },
  };

  const layers = {
    doubles: [{ name: 'Double L', role: 'Double', db: -6, on: true, seed: 401 }, { name: 'Double R', role: 'Double', db: -6, on: true, seed: 402 }],
    adlibs:  [{ name: 'Ad-Libs', role: 'Ad-lib', db: -8, on: true, seed: 403, sparse: true }],
    bv:      [{ name: 'Backing Vocals', role: 'Backing', db: -10, on: true, seed: 404 }],
    guitars: [{ name: 'Guitar Layer 1', role: 'Guitar', db: -6, on: true, seed: 405, guitar: true }, { name: 'Guitar Layer 2', role: 'Guitar', db: -12, on: false, seed: 406, guitar: true }],
  };
  const alignDefaults = {
    doubles: [['i-target', 'Tightness', .74, 'Natural', 'Locked'], ['i-pulse', 'Pitch Follow', 0, 'Off', 'Full'], ['i-level', 'Level Match', .85, 'Off', 'Full']],
    adlibs:  [['i-target', 'Tightness', .35, 'Natural', 'Locked'], ['i-pulse', 'Pitch Follow', 0, 'Off', 'Full'], ['i-level', 'Level Match', .60, 'Off', 'Full']],
    bv:      [['i-target', 'Tightness', .60, 'Natural', 'Locked'], ['i-pulse', 'Pitch Follow', 0, 'Off', 'Full'], ['i-level', 'Level Match', .80, 'Off', 'Full']],
    guitars: [['i-target', 'Tightness', .70, 'Natural', 'Locked'], ['i-link', 'Phase Align', 1, 'Off', 'Full'], ['i-level', 'Level Match', .85, 'Off', 'Full']],
  };

  // Loudness envelope of a vocal at time t (seconds): syllable bumps inside lines, quiet in the gaps.
  function vocalEnvelope(t, seed, shift) {
    for (const ln of lines) {
      if (t < ln.start + shift - 0.05 || t > ln.end + shift + 0.1) continue;
      for (const wd of ln.words) {
        const a = wd.start + shift, b = wd.end + shift;
        if (t >= a && t <= b) {
          const x = (t - a) / (b - a);
          const shape = Math.sin(Math.PI * x) ** 0.6;
          const wob = 0.75 + 0.25 * Math.sin(seed * 0.37 + t * 41) * Math.sin(seed + t * 13.7);
          return Math.max(0.05, shape * wob);
        }
      }
      return 0.04;
    }
    return 0.015;
  }

  return { rng, bpm, bar, length, sections, lines, takes, scores, styles, layers, alignDefaults, vocalEnvelope, key: 'E minor', sig: '4/4' };
})();
