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
   * opts: t0, t1 (seconds shown), env(t) -> 0..1, color (string or fn(t)), seed, gain, step (px), glow (px), alpha
   */
  function draw(canvas, opts) {
    const p = prep(canvas); if (!p) return;
    const { ctx, w, h } = p;
    const step = opts.step || 1.6;
    const mid = h / 2;
    const gain = opts.gain == null ? 0.92 : opts.gain;
    ctx.globalAlpha = opts.alpha == null ? 1 : opts.alpha;
    ctx.lineWidth = opts.lineWidth || 1.2;
    ctx.lineCap = 'round';
    if (opts.glow !== 0) { ctx.shadowBlur = opts.glow || 6; }
    let lastColor = null;
    ctx.beginPath();
    for (let x = 0; x <= w; x += step) {
      const t = opts.t0 + (x / w) * (opts.t1 - opts.t0);
      const color = typeof opts.color === 'function' ? opts.color(t) : opts.color;
      if (color !== lastColor) {
        if (lastColor !== null) ctx.stroke();
        ctx.beginPath(); ctx.strokeStyle = color; ctx.shadowColor = color; lastColor = color;
      }
      const n = 0.45 + 0.55 * hash(Math.floor(x / step) + (opts.seed || 0) * 977);
      const a = Math.min(1, opts.env(t) * n) * mid * gain;
      ctx.moveTo(x, mid - a); ctx.lineTo(x, mid + a);
    }
    ctx.stroke();
    // thin bright centre line like the art
    ctx.shadowBlur = 0; ctx.globalAlpha = (opts.alpha == null ? 1 : opts.alpha) * 0.55;
    ctx.strokeStyle = typeof opts.color === 'function' ? opts.color(opts.t0) : opts.color;
    ctx.lineWidth = 0.8; ctx.beginPath(); ctx.moveTo(0, mid); ctx.lineTo(w, mid); ctx.stroke();
    ctx.globalAlpha = 1;
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
