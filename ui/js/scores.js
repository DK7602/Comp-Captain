// The scores (M3): every line of every take on the track being viewed, with the reasons, your own pick for each
// line, and the style (how much each score counts). Inside the plug-in they come from the C++ side, worked out in the
// background; the totals and the best take are worked out here, so moving a style slider is instant.
// The comp (M4): the take for every line (your pick, else the best, keeping one take going towards Fewer cuts) is
// worked out here too and handed to the plug-in, which plays it and builds the file.
'use strict';

const Scores = (() => {
  const STYLES = [
    ['Rap', [0, 0.35, 0.35, 0.30]], ['Trap', [0.10, 0.35, 0.25, 0.30]], ['Singing', [0.35, 0.25, 0.20, 0.20]],
    ['Pop', [0.30, 0.25, 0.25, 0.20]], ['Country', [0.30, 0.25, 0.25, 0.20]], ['Rock', [0.25, 0.25, 0.20, 0.30]],
    ['Worship / Gospel', [0.35, 0.20, 0.25, 0.20]], ['R&B', [0.35, 0.20, 0.20, 0.25]], ['My Own', [0.25, 0.25, 0.25, 0.25]],
  ];
  const st = {
    got: false, any: false, ready: false,
    lines: {},             // line index -> [{ id, p, t, c, e, good, bad, problems }]
    picks: {},             // line index -> { id, text }
    style: 'Singing',
    weights: { p: 0.35, t: 0.25, c: 0.20, e: 0.20 },
    picky: 0.7,            // 0 = Fewer cuts, 1 = Best every line
  };
  const listeners = [];
  const onChange = (fn) => listeners.push(fn);

  Bridge.on('scores', (m) => {
    st.got = true;
    st.any = !!m.any; st.ready = !!m.ready;
    st.lines = {};
    (m.lines || []).forEach((l) => { st.lines[l.line] = l.takes || []; });
    st.picks = m.picks || {};
    st.style = m.style || 'Singing';
    if (Array.isArray(m.weights) && m.weights.length === 4) st.weights = { p: +m.weights[0], t: +m.weights[1], c: +m.weights[2], e: +m.weights[3] };
    if (m.picky != null && isFinite(+m.picky)) st.picky = Math.max(0, Math.min(1, +m.picky));
    changed();
    listeners.forEach((fn) => fn('scores'));
  });

  // The total for a take of a line: the weighted scores (left out where there's no score), less 3 for each problem
  // (8 at most). The same sum as the plug-in's.
  function total(sc, w) {
    w = w || st.weights;
    let sum = 0, weight = 0;
    [['p', sc.p], ['t', sc.t], ['c', sc.c], ['e', sc.e]].forEach(([k, v]) => {
      if (v == null || v < 0 || !(w[k] > 0)) return;
      sum += v * w[k]; weight += w[k];
    });
    if (weight <= 0) return 0;
    return Math.max(0, sum / weight - Math.min(8, 3 * ((sc.problems || []).length)));
  }
  const takesOf = (li) => st.lines[li] || [];
  const isFav = (id) => { const t = Takes.st.list.find((x) => x.id === id); return !!(t && t.fav); };
  // Comp Captain's pick: the highest total; within half a point, a favourite (heart) wins.
  function best(li) {
    let bestSc = null, bestTotal = -1;
    takesOf(li).forEach((sc) => {
      const v = total(sc);
      const better = !bestSc || v > bestTotal + 0.5 || (Math.abs(v - bestTotal) <= 0.5 && isFav(sc.id) && !isFav(bestSc.id))
        || (Math.abs(v - bestTotal) <= 0.5 && isFav(sc.id) === isFav(bestSc.id) && v > bestTotal);
      if (better) { bestSc = sc; bestTotal = v; }
    });
    return bestSc ? bestSc.id : null;
  }
  // Your own pick, if you made one for this line (and the line's words haven't changed since).
  function pickOf(li) {
    const p = st.picks[li], line = Song.st.lines[li];
    if (!p || !line || p.text !== line.text || !takesOf(li).some((sc) => sc.id === p.id)) return null;
    return p.id;
  }

  // The comp: the take for every line. Your picks stay. Elsewhere the best take, except that towards Fewer cuts a
  // change of take between two lines sung close together costs points, so a take that's nearly as good carries on
  // (one performance, fewer joins). At Best every line it's simply the best take of each line.
  let compCache = null;
  const closeTogether = (a, b) => {
    const la = Song.st.lines[a], lb = Song.st.lines[b];
    if (!la || !lb || b !== a + 1) return false;
    if (la.end >= 0 && lb.start >= 0) return lb.start - la.end < 2.0;
    return true;
  };
  function compOf() {
    if (compCache) return compCache;
    const out = {}, n = Song.st.lines.length, cost = Math.max(0, 1 - st.picky) * 10;
    if (cost <= 0) {
      for (let li = 0; li < n; li++) { const id = pickOf(li) || best(li); if (id) out[li] = id; }
      return (compCache = out);
    }
    const steps = [];
    for (let li = 0; li < n; li++) {
      const pick = pickOf(li);
      const cands = takesOf(li).filter((sc) => !pick || sc.id === pick);
      if (!cands.length) { steps.push(null); continue; }
      const last = steps.length ? steps[steps.length - 1] : null;
      const linked = !!last && closeTogether(last.li, li);
      const score = {}, from = {};
      cands.forEach((sc) => {
        const own = total(sc) + (isFav(sc.id) ? 0.5 : 0); // a favourite wins within half a point
        if (!linked) { score[sc.id] = own; from[sc.id] = null; return; }
        let bestV = -Infinity, bestP = null;
        Object.keys(last.score).forEach((p) => {
          const v = last.score[p] - (p === sc.id ? 0 : cost);
          if (v > bestV + 1e-9 || (Math.abs(v - bestV) <= 1e-9 && p === sc.id)) { bestV = v; bestP = p; }
        });
        score[sc.id] = own + bestV; from[sc.id] = bestP;
      });
      steps.push({ li, score, from, linked });
    }
    let next = null;
    for (let k = steps.length - 1; k >= 0; k--) {
      const s = steps[k];
      if (!s) { next = null; continue; }
      let id = next && s.score[next] != null ? next : null;
      if (!id) id = Object.keys(s.score).reduce((a, b) => (s.score[b] > s.score[a] + 1e-9 ? b : a));
      out[s.li] = id;
      next = s.linked ? s.from[id] : null;
    }
    return (compCache = out);
  }
  const chosen = (li) => pickOf(li) || compOf()[li] || best(li);
  // how many joins the comp has (a change of take between lines next to each other)
  function joins() {
    const c = compOf(); let count = 0, prev = null;
    for (let li = 0; li < Song.st.lines.length; li++) { const id = c[li]; if (id && prev && id !== prev) count++; if (id) prev = id; }
    return count;
  }

  // Hands the comp to the plug-in whenever it changes (it plays it and builds the file from it).
  let syncTimer = 0, lastSent = '';
  function changed() {
    compCache = null;
    if (!Bridge.inHost) return;
    clearTimeout(syncTimer);
    syncTimer = setTimeout(() => {
      if (!st.got) return;
      const c = compOf();
      const lines = Object.keys(c).map((k) => ({ line: +k, id: c[k] }));
      const msg = { lines, picky: Math.round(st.picky * 1000) / 1000 };
      const text = JSON.stringify(msg);
      if (text === lastSent) return;
      lastSent = text;
      Bridge.send('comp', msg);
    }, 150);
  }
  Song.onChange((what) => { if (what !== 'listening') changed(); });
  Takes.onChange((what) => { if (what === 'takes') changed(); });
  function setPicky(v) {
    st.picky = Math.max(0, Math.min(1, v));
    changed();
    listeners.forEach((fn) => fn('picky'));
  }
  const scoreOf = (li, id) => takesOf(li).find((sc) => sc.id === id) || null;
  // How often your picks match Comp Captain's: { picked, same }
  function agreement() {
    let picked = 0, same = 0;
    Object.keys(st.picks).forEach((k) => { const li = +k, id = pickOf(li); if (!id) return; picked++; if (id === best(li)) same++; });
    return { picked, same };
  }
  // The valid picks, for undo
  function snapshot() {
    const out = {};
    Object.keys(st.picks).forEach((k) => { if (pickOf(+k)) out[k] = Object.assign({}, st.picks[k]); });
    return out;
  }

  // Asking the plug-in
  function setPick(li, id) {
    const line = Song.st.lines[li];
    if (!line) return;
    if (id) st.picks[li] = { id, text: line.text }; else delete st.picks[li];
    Bridge.send('pick', { line: li, id: id || '', text: line.text });
    changed();
    listeners.forEach((fn) => fn('picks'));
  }
  function setPicks(picks) {
    st.picks = picks;
    Bridge.send('picksSet', { picks });
    changed();
    listeners.forEach((fn) => fn('picks'));
  }
  function clearPicks() { setPicks({}); }
  let styleTimer = 0;
  function setStyle(name, weights) {
    st.style = name;
    if (weights) st.weights = Object.assign({}, weights);
    else { const s = STYLES.find((x) => x[0] === name); if (s) st.weights = { p: s[1][0], t: s[1][1], c: s[1][2], e: s[1][3] }; }
    clearTimeout(styleTimer);
    styleTimer = setTimeout(() => Bridge.send('style', { name: st.style, weights: [st.weights.p, st.weights.t, st.weights.c, st.weights.e] }), 250);
    changed();
    listeners.forEach((fn) => fn('style'));
  }

  return { st, STYLES, onChange, total, takesOf, best, pickOf, chosen, compOf, joins, scoreOf, agreement, snapshot, setPick, setPicks, clearPicks, setStyle, setPicky };
})();
