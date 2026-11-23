// Waveform drawing on <canvas>, with the glow from the owner's art. Canvases are sized to the real
// pixels on screen (window scale x screen scaling) so they stay sharp at any size.
'use strict';

const Wave = (() => {
  let stageScale = 1;
  function setStageScale(s) { stageScale = s; }

  function prep(canvas) {
    const cw = canvas.clientWidth, ch = canvas.clientHeight;
    if (!cw || !ch) return null;
    const k = stageScale * (window.devicePixelRatio || 1);
    const w = Math.round(cw * k), h = Math.round(ch * k);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(w / cw, 0, 0, h / ch, 0, 0);
    ctx.clearRect(0, 0, cw, ch);
    return { ctx, w: cw, h: ch };
  }

  // Cheap repeatable "noise" so each take has its own texture.
  function hash(n) { const x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); }

  /**
   * opts: t0, t1 (the time shown: seconds, or quarter notes for real takes), env(t, dt) -> 0..1 for the column
   * from t to t + dt (-1 = nothing there: no line), color (string or fn(t)), seed, gain, step (px), glow (px),
   * alpha, noise (false = draw the levels exactly, for real takes)
   */
  // Drawn on the screen's real pixels: sharp 1-pixel lines on top, with a separate soft glow
  // underneath, so the waves stay crisp at any window size.
  function draw(canvas, opts) {
    const p = prep(canvas); if (!p) return;
    const { ctx, w, h } = p;
    const k = canvas.width / w;                       // real pixels per layout pixel
    const W = canvas.width, H = canvas.height, mid = Math.round(H / 2) + 0.5;
    const step = Math.max(2, Math.round((opts.step || 1.6) * k * 0.75)); // in real pixels
    const gain = opts.gain == null ? 0.92 : opts.gain;
    const alpha = opts.alpha == null ? 1 : opts.alpha;
    const colorAt = (t) => (typeof opts.color === 'function' ? opts.color(t) : opts.color);

    // work out the columns once
    const cols = [];
    const dt = (opts.t1 - opts.t0) * step / W;
    for (let X = 0; X <= W; X += step) {
      const t = opts.t0 + (X / W) * (opts.t1 - opts.t0);
      const e = opts.env(t, dt);
      if (e < 0) { cols.push({ x: X + 0.5, skip: true }); continue; }
      const n = opts.noise === false ? 1 : 0.45 + 0.55 * hash(Math.floor(X / step) + (opts.seed || 0) * 977);
      cols.push({ x: X + 0.5, a: Math.max(0.5, Math.min(1, e * n) * (H / 2) * gain), c: colorAt(t) });
    }
    const stroke = (width, a, blur) => {
      ctx.lineWidth = width; ctx.globalAlpha = alpha * a; ctx.shadowBlur = blur;
      let last = null;
      ctx.beginPath();
      for (const col of cols) {
        if (col.skip) continue;
        if (col.c !== last) {
          if (last !== null) ctx.stroke();
          ctx.beginPath(); ctx.strokeStyle = col.c; ctx.shadowColor = col.c; last = col.c;
        }
        ctx.moveTo(col.x, mid - col.a); ctx.lineTo(col.x, mid + col.a);
      }
      ctx.stroke();
    };

    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.lineCap = 'butt';
    if (opts.glow !== 0) stroke(Math.max(2, step), 0.35, (opts.glow || 6) * k); // soft halo
    stroke(Math.max(1, Math.round(k * 0.9)), 1, 0);                            // sharp lines
    // thin bright centre line like the art (only where there's sound, for real takes)
    ctx.shadowBlur = 0; ctx.globalAlpha = alpha * 0.6; ctx.lineWidth = 1;
    ctx.beginPath();
    let open = null;
    for (const col of cols) {
      if (col.skip) { if (open !== null) { ctx.lineTo(col.x, mid); open = null; } continue; }
      if (open === null) { ctx.moveTo(col.x, mid); ctx.strokeStyle = col.c; open = col.x; }
    }
    if (open !== null) ctx.lineTo(W, mid);
    ctx.stroke();
    ctx.restore();
    return p;
  }

  // Faint outline of another wave on top (used to show the lead behind a layer).
  function outline(canvas, opts) {
    const cw = canvas.clientWidth, ch = canvas.clientHeight; if (!cw) return;
    const ctx = canvas.getContext('2d');
    const mid = ch / 2, gain = opts.gain || 0.92;
    ctx.save(); ctx.strokeStyle = opts.color; ctx.globalAlpha = opts.alpha || 0.7; ctx.lineWidth = 1.1; ctx.shadowBlur = 0;
    for (const sign of [-1, 1]) {
      ctx.beginPath();
      for (let x = 0; x <= cw; x += 2) {
        const t = opts.t0 + (x / cw) * (opts.t1 - opts.t0);
        const y = mid + sign * Math.min(1, opts.env(t)) * mid * gain * 0.9;
        x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    ctx.restore();
  }

  return { draw, outline, prep, setStageScale, hash };
})();
