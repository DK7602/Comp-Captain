// The scores (M3): every line of every take on the track being viewed, with the reasons, your own pick for each
// line, and the style (how much each score counts). Inside the plug-in they come from the C++ side, worked out in the
// background; the totals and the best take are worked out here, so moving a style slider is instant.
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
  const chosen = (li) => pickOf(li) || best(li);
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
    listeners.forEach((fn) => fn('picks'));
  }
  function setPicks(picks) {
    st.picks = picks;
    Bridge.send('picksSet', { picks });
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
    listeners.forEach((fn) => fn('style'));
  }

  return { st, STYLES, onChange, total, takesOf, best, pickOf, chosen, scoreOf, agreement, snapshot, setPick, setPicks, clearPicks, setStyle };
})();
