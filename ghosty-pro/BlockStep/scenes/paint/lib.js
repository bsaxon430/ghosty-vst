// Painter toolkit (runs in the offline harness page; Chromium canvas 2D). All drawing is in design units.
const PT = (() => {
  const TAU = Math.PI * 2;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v)), lerp = (a, b, t) => a + (b - a) * t;
  // ---------- seeded random
  const rng = seed => { let a = seed >>> 0 || 1;
    const f = () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
    f.r = (lo, hi) => lo + f() * (hi - lo); f.i = (lo, hi) => Math.floor(f.r(lo, hi + 1)); f.p = arr => arr[Math.floor(f() * arr.length)];
    f.c = p => f() < p; f.g = () => (f() + f() + f() + f()) / 2 - 1;   // ~gaussian, -1..1
    f.sh = arr => { const o = arr.slice(); for (let i = o.length - 1; i > 0; i--) { const j = Math.floor(f() * (i + 1)); [o[i], o[j]] = [o[j], o[i]]; } return o; };
    return f; };
  const hash = s => { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; };
  const R = name => rng(hash(String(name)));
  // ---------- colour ('#rrggbb' in, css strings out)
  const hex = c => { if (Array.isArray(c)) return c; c = c.replace('#', ''); if (c.length === 3) c = c.split('').map(x => x + x).join(''); return [parseInt(c.slice(0, 2), 16), parseInt(c.slice(2, 4), 16), parseInt(c.slice(4, 6), 16)]; };
  const toHex = a => '#' + a.map(v => clamp(Math.round(v), 0, 255).toString(16).padStart(2, '0')).join('');
  const rgba = (c, a = 1) => { const [r, g, b] = hex(c); return `rgba(${Math.round(r)},${Math.round(g)},${Math.round(b)},${a})`; };
  const mix = (a, b, t) => { const A = hex(a), B = hex(b); return toHex([lerp(A[0], B[0], t), lerp(A[1], B[1], t), lerp(A[2], B[2], t)]); };
  const lighten = (c, t) => mix(c, '#ffffff', t), darken = (c, t) => mix(c, '#000000', t);
  const toHsl = c => { let [r, g, b] = hex(c).map(v => v / 255); const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2; let h = 0, s = 0;
    if (mx !== mn) { const d = mx - mn; s = l > .5 ? d / (2 - mx - mn) : d / (mx + mn); h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4; h /= 6; } return [h * 360, s, l]; };
  const hsl = (h, s, l) => { h = ((h % 360) + 360) % 360 / 360; const q = l < .5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    const f = t => { t = (t + 1) % 1; return t < 1 / 6 ? p + (q - p) * 6 * t : t < .5 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p; };
    return toHex([f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255]); };
  const shift = (c, dh = 0, ds = 0, dl = 0) => { const [h, s, l] = toHsl(c); return hsl(h + dh, clamp(s + ds, 0, 1), clamp(l + dl, 0, 1)); };
  const jit = (c, rnd, dh = 6, ds = .06, dl = .06) => shift(c, rnd.g() * dh, rnd.g() * ds, rnd.g() * dl);
  // ---------- noise (value noise, smooth)
  const nz = (() => { const perm = new Uint8Array(512), r = rng(1337); const p = Array.from({length: 256}, (_, i) => i); for (let i = 255; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [p[i], p[j]] = [p[j], p[i]]; } for (let i = 0; i < 512; i++) perm[i] = p[i & 255];
    const val = (x, y) => perm[(perm[x & 255] + y) & 255] / 255, sm = t => t * t * (3 - 2 * t);
    const n2 = (x, y) => { const xi = Math.floor(x), yi = Math.floor(y), xf = sm(x - xi), yf = sm(y - yi); return lerp(lerp(val(xi, yi), val(xi + 1, yi), xf), lerp(val(xi, yi + 1), val(xi + 1, yi + 1), xf), yf); };
    const fbm = (x, y, oct = 4, gain = .5) => { let a = 1, f = 1, s = 0, n = 0; for (let i = 0; i < oct; i++) { s += a * n2(x * f, y * f); n += a; a *= gain; f *= 2; } return s / n; };
    return {n2, fbm}; })();
  // ---------- canvases
  const mk = (w, h) => { const c = document.createElement('canvas'); c.width = Math.max(1, Math.round(w)); c.height = Math.max(1, Math.round(h)); const g = c.getContext('2d'); return {c, g}; };
  // a layer in design units at scale S
  const layer = (W, H, S) => { const o = mk(W * S, H * S); o.g.scale(S, S); o.W = W; o.H = H; o.S = S; return o; };
  // ---------- paths
  const poly = (g, pts, close = true) => { g.beginPath(); pts.forEach((p, i) => i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1])); if (close) g.closePath(); };
  const rr = (g, x, y, w, h, r) => { r = Math.min(r, w / 2, h / 2); g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); };
  // smooth closed blob through jittered radius points
  const blobPts = (cx, cy, rx, ry, n, rough, rnd, rot = 0) => { const o = []; for (let i = 0; i < n; i++) { const a = rot + i / n * TAU, k = 1 + rnd.g() * rough; o.push([cx + Math.cos(a) * rx * k, cy + Math.sin(a) * ry * k]); } return o; };
  const smooth = (g, pts, close = true) => { const n = pts.length; g.beginPath(); if (close) { const m = (i, j) => [(pts[i][0] + pts[j][0]) / 2, (pts[i][1] + pts[j][1]) / 2]; let s = m(n - 1, 0); g.moveTo(s[0], s[1]);
      for (let i = 0; i < n; i++) { const e = m(i, (i + 1) % n); g.quadraticCurveTo(pts[i][0], pts[i][1], e[0], e[1]); } g.closePath(); }
    else { g.moveTo(pts[0][0], pts[0][1]); for (let i = 1; i < n - 1; i++) { const ex = (pts[i][0] + pts[i + 1][0]) / 2, ey = (pts[i][1] + pts[i + 1][1]) / 2; g.quadraticCurveTo(pts[i][0], pts[i][1], ex, ey); } g.lineTo(pts[n - 1][0], pts[n - 1][1]); } };
  const blob = (g, cx, cy, rx, ry, rnd, n = 9, rough = .12) => smooth(g, blobPts(cx, cy, rx, ry, n, rough, rnd, rnd.r(0, TAU)));
  // resample a polyline to points every `step`
  const resample = (pts, step) => { const o = [pts[0]]; let carry = 0; for (let i = 1; i < pts.length; i++) { const [x0, y0] = pts[i - 1], [x1, y1] = pts[i], d = Math.hypot(x1 - x0, y1 - y0); let t = step - carry; while (t <= d) { o.push([x0 + (x1 - x0) * t / d, y0 + (y1 - y0) * t / d]); t += step; } carry = d - (t - step); } o.push(pts[pts.length - 1]); return o; };
  // ---------- hand-inked line: a wobbly ribbon with pressure (width) changes and tapered ends
  const ink = (g, pts, o = {}) => { const {w = 1.6, color = '#1a1420', wob = .6, taper = .35, seed = 1, alpha = 1, step = 3, press = .35} = o;
    const P2 = resample(pts, step), n = P2.length; if (n < 2) return; const L = [], Rr = [];
    for (let i = 0; i < n; i++) { const a = P2[Math.max(0, i - 1)], b = P2[Math.min(n - 1, i + 1)]; let dx = b[0] - a[0], dy = b[1] - a[1]; const d = Math.hypot(dx, dy) || 1; dx /= d; dy /= d;
      const t = i / (n - 1), tp = taper > 0 ? Math.min(1, Math.min(t, 1 - t) / taper * 1.2 + .25) : 1;
      const pr = 1 + (nz.n2(i * .09 + seed * 7.1, seed * 3.3) - .5) * 2 * press, hw = w / 2 * tp * pr;
      const ox = (nz.n2(i * .13 + seed * 1.7, 9.2 + seed) - .5) * 2 * wob, oy = (nz.n2(4.4 + seed * 2.3, i * .13 + seed) - .5) * 2 * wob;
      const x = P2[i][0] + ox, y = P2[i][1] + oy; L.push([x - dy * hw, y + dx * hw]); Rr.push([x + dy * hw, y - dx * hw]); }
    g.save(); g.globalAlpha *= alpha; g.fillStyle = color; g.beginPath(); L.forEach((p, i) => i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1])); for (let i = n - 1; i >= 0; i--) g.lineTo(Rr[i][0], Rr[i][1]); g.closePath(); g.fill(); g.restore(); };
  const inkPoly = (g, pts, o = {}) => { for (let i = 0; i < pts.length; i++) { const a = pts[i], b = pts[(i + 1) % pts.length], ov = o.over ?? 1.5, d = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1, ux = (b[0] - a[0]) / d, uy = (b[1] - a[1]) / d;
      ink(g, [[a[0] - ux * ov, a[1] - uy * ov], [b[0] + ux * ov, b[1] + uy * ov]], Object.assign({}, o, {seed: (o.seed || 1) + i * 13.7})); } };
  const inkRect = (g, x, y, w, h, o = {}) => inkPoly(g, [[x, y], [x + w, y], [x + w, y + h], [x, y + h]], o);
  // ---------- gouache: fill a clipped shape, then lay semi-opaque brush dabs along a direction
  const dabs = (g, x, y, w, h, o) => { const {n = 60, cols, size = 14, ang = 0, spread = .5, alpha = [.12, .4], rnd, long = 3, round = true} = o;
    for (let i = 0; i < n; i++) { const cx = x + rnd() * w, cy = y + rnd() * h, s = size * rnd.r(.5, 1.5), a = ang + rnd.g() * spread;
      g.save(); g.translate(cx, cy); g.rotate(a); g.globalAlpha = rnd.r(alpha[0], alpha[1]); g.fillStyle = rnd.p(cols);
      const L = s * long * rnd.r(.6, 1.3), H2 = s * rnd.r(.35, .7);
      g.beginPath(); if (round) { g.moveTo(-L / 2, 0); g.quadraticCurveTo(-L / 4, -H2, L / 4, -H2 * rnd.r(.6, 1)); g.quadraticCurveTo(L / 2, -H2 * .2, L / 2, 0); g.quadraticCurveTo(L / 3, H2 * rnd.r(.6, 1), -L / 4, H2); g.quadraticCurveTo(-L / 2, H2 * .4, -L / 2, 0); } else g.rect(-L / 2, -H2, L, H2 * 2);
      g.fill(); g.restore(); } };
  const paint = (g, pathFn, base, o = {}) => { g.save(); pathFn(g); g.clip(); g.fillStyle = base; pathFn(g); g.fill(); if (o.dabs) dabs(g, o.box[0], o.box[1], o.box[2], o.box[3], o.dabs); if (o.after) o.after(g); g.restore(); };
  // ---------- light
  const glow = (g, x, y, r, c, a = 1, mode = 'lighter') => { g.save(); g.globalCompositeOperation = mode; const gr = g.createRadialGradient(x, y, 0, x, y, r); gr.addColorStop(0, rgba(c, a)); gr.addColorStop(.35, rgba(c, a * .45)); gr.addColorStop(1, rgba(c, 0)); g.fillStyle = gr; g.fillRect(x - r, y - r, r * 2, r * 2); g.restore(); };
  const glowRect = (g, x, y, w, h, c, a, spread) => { g.save(); g.globalCompositeOperation = 'lighter'; g.shadowColor = rgba(c, 1); g.shadowBlur = spread; g.fillStyle = rgba(c, a); for (let i = 0; i < 2; i++) g.fillRect(x, y, w, h); g.restore(); };
  // neon tube along a path function: wide soft halo, coloured tube, hot white core
  const neon = (g, pathFn, c, w = 2, o = {}) => { const {halo = 1, core = .85, S = 1} = o; g.save(); g.lineCap = 'round'; g.lineJoin = 'round';
    g.globalCompositeOperation = 'lighter'; g.shadowColor = rgba(c, 1); g.strokeStyle = rgba(c, .35 * halo);
    g.shadowBlur = w * 9 * S; g.lineWidth = w * 1.6; pathFn(g); g.stroke(); g.shadowBlur = w * 4 * S; pathFn(g); g.stroke();
    g.globalCompositeOperation = 'source-over'; g.shadowBlur = w * 2 * S; g.strokeStyle = c; g.lineWidth = w; pathFn(g); g.stroke();
    g.shadowBlur = 0; g.strokeStyle = rgba(lighten(c, .75), core); g.lineWidth = Math.max(.5, w * .42); pathFn(g); g.stroke(); g.restore(); };
  // neon lettering: text drawn as tubes (stroke) or as a glowing fill
  const neonText = (g, txt, x, y, font, c, o = {}) => { const {tube = 0, S = 1, halo = 1, align = 'center', base = 'middle', fill = true, vertical = false, gap = 1.05, size = 20} = o;
    g.save(); g.font = font; g.textAlign = align; g.textBaseline = base; g.lineJoin = 'round'; g.lineCap = 'round';
    const each = fn => { if (!vertical) fn(txt, x, y); else [...txt].forEach((ch, i) => { g.save(); if (ch === 'ー' || ch === '-') { g.translate(x, y + i * size * gap); g.rotate(Math.PI / 2); fn(ch, 0, 0); } else fn(ch, x, y + i * size * gap); g.restore(); }); };
    g.globalCompositeOperation = 'lighter'; g.shadowColor = rgba(c, 1); g.fillStyle = rgba(c, .3 * halo); g.strokeStyle = rgba(c, .3 * halo);
    for (const b of [size * .9, size * .4]) { g.shadowBlur = b * S; each((t, a, b2) => tube ? (g.lineWidth = tube * 1.5, g.strokeText(t, a, b2)) : g.fillText(t, a, b2)); }
    g.globalCompositeOperation = 'source-over'; g.shadowBlur = size * .18 * S;
    if (tube) { g.strokeStyle = c; g.lineWidth = tube; each((t, a, b2) => g.strokeText(t, a, b2)); g.shadowBlur = 0; g.strokeStyle = rgba(lighten(c, .8), .9); g.lineWidth = Math.max(.4, tube * .4); each((t, a, b2) => g.strokeText(t, a, b2)); }
    else if (fill) { g.fillStyle = lighten(c, .35); each((t, a, b2) => g.fillText(t, a, b2)); }
    g.restore(); };
  // ---------- textures (built once per scale)
  const tex = {};
  const mkTex = (name, w, h, fn) => { const o = mk(w, h); fn(o.g, w, h); tex[name] = o.c; return o.c; };
  const initTex = () => {
    mkTex('grain', 512, 512, (g, w, h) => { const id = g.createImageData(w, h), d = id.data, r = rng(77); for (let i = 0; i < d.length; i += 4) { const v = 128 + (r() - .5) * 90 + (r() - .5) * 50; d[i] = d[i + 1] = d[i + 2] = v; d[i + 3] = 255; } g.putImageData(id, 0, 0); });
    mkTex('paper', 512, 512, (g, w, h) => { const id = g.createImageData(w, h), d = id.data, r = rng(91); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const i = (y * w + x) * 4, f = nz.fbm(x / 38, y / 38, 4) * .55 + nz.fbm(x / 7, y / 4.5, 2) * .3 + r() * .15; const v = 96 + f * 120; d[i] = d[i + 1] = d[i + 2] = v; d[i + 3] = 255; } g.putImageData(id, 0, 0); });
    mkTex('bristle', 512, 512, (g, w, h) => { g.fillStyle = '#808080'; g.fillRect(0, 0, w, h); const r = rng(5); for (let i = 0; i < 2600; i++) { const y = r() * h, x = r() * w, L = r.r(30, 190), a = r.r(.03, .12); g.strokeStyle = r.c(.5) ? `rgba(255,255,255,${a})` : `rgba(0,0,0,${a})`; g.lineWidth = r.r(.5, 2.2); g.beginPath(); g.moveTo(x, y); g.lineTo(x + L, y + r.g() * 3); g.stroke(); if (x + L > w) { g.beginPath(); g.moveTo(x - w, y); g.lineTo(x + L - w, y); g.stroke(); } } });
  };
  const pat = (g, name, scale = 1, rot = 0) => { const p = g.createPattern(tex[name], 'repeat'); p.setTransform(new DOMMatrix().scale(scale).rotate(rot)); return p; };
  // overlay a texture on whatever is inside the current clip / whole canvas (design-space rect)
  const texture = (g, name, x, y, w, h, o = {}) => { const {mode = 'overlay', alpha = .25, scale = 1, rot = 0} = o; g.save(); g.globalCompositeOperation = mode; g.globalAlpha = alpha; g.fillStyle = pat(g, name, scale, rot); g.fillRect(x, y, w, h); g.restore(); };
  // ---------- whole-canvas post effects (device pixels)
  const blurred = (c, px) => { const o = mk(c.width, c.height); o.g.filter = `blur(${px}px)`; o.g.drawImage(c, 0, 0); return o.c; };
  const bloom = (L, o = {}) => { const {r = 14, a = .5, thresh = 0} = o, src = L.c, t = mk(src.width, src.height); t.g.drawImage(src, 0, 0);
    if (thresh) { t.g.globalCompositeOperation = 'multiply'; t.g.drawImage(src, 0, 0); if (thresh > 1) t.g.drawImage(src, 0, 0); }
    const b = blurred(t.c, r * L.S); L.g.save(); L.g.setTransform(1, 0, 0, 1, 0, 0); L.g.globalCompositeOperation = 'screen'; L.g.globalAlpha = a; L.g.drawImage(b, 0, 0); L.g.restore(); };
  const vignette = (L, a = .5, c = '#05020c') => { const g = L.g, W = L.W, H = L.H; g.save(); const gr = g.createRadialGradient(W / 2, H / 2, Math.min(W, H) * .35, W / 2, H / 2, Math.hypot(W, H) * .62); gr.addColorStop(0, rgba(c, 0)); gr.addColorStop(1, rgba(c, a)); g.fillStyle = gr; g.fillRect(0, 0, W, H); g.restore(); };

  // ---------- gouache: turn a smoothly shaded grey picture into a few flat paint tones with wandering, hand-cut edges
  // L: a layer whose pixels' brightness says "how lit"; pal: colours dark -> light. Alpha is kept.
  const post = (L, pal, o = {}) => { const {amp = .07, sc = .03, soft = .25, seed = 0, lo = 0, hi = 1} = o, c = L.c, id = L.g.getImageData(0, 0, c.width, c.height), d = id.data, n = pal.length, P = pal.map(hex), w = c.width, h = c.height;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const i = (y * w + x) * 4; if (!d[i + 3]) continue;
      let v = ((d[i] * .299 + d[i + 1] * .587 + d[i + 2] * .114) / 255 - lo) / (hi - lo) + (nz.fbm(x * sc + seed, y * sc + seed * 1.7, 2) - .5) * 2 * amp; v = clamp(v, 0, .9999) * n;
      const k = Math.floor(v), fr = v - k; let r = P[k][0], g2 = P[k][1], b = P[k][2];
      if (k < n - 1 && fr > 1 - soft) { const t = (fr - (1 - soft)) / soft, t2 = t * t * (3 - 2 * t) * .5; r += (P[k + 1][0] - r) * t2; g2 += (P[k + 1][1] - g2) * t2; b += (P[k + 1][2] - b) * t2; }
      else if (k > 0 && fr < soft) { const t = 1 - fr / soft, t2 = t * t * (3 - 2 * t) * .5; r += (P[k - 1][0] - r) * t2; g2 += (P[k - 1][1] - g2) * t2; b += (P[k - 1][2] - b) * t2; }
      d[i] = r; d[i + 1] = g2; d[i + 2] = b; }
    L.g.putImageData(id, 0, 0); return L; };
  // a scratch layer covering a design-space box (so only that box's pixels are worked on)
  const box = (x, y, w, h, S) => { const o = mk(Math.ceil(w * S), Math.ceil(h * S)); o.g.scale(S, S); o.g.translate(-x, -y); o.x = x; o.y = y; o.w = w; o.h = h; o.S = S; o.to = g => g.drawImage(o.c, x, y, o.c.width / S, o.c.height / S); return o; };
  const lin = (g, x0, y0, x1, y1, stops) => { const gr = g.createLinearGradient(x0, y0, x1, y1); stops.forEach(([t, c]) => gr.addColorStop(t, c)); return gr; };
  const rad = (g, x, y, r0, r1, stops) => { const gr = g.createRadialGradient(x, y, r0, x, y, r1); stops.forEach(([t, c]) => gr.addColorStop(t, c)); return gr; };
  return {TAU, clamp, lerp, rng, hash, R, hex, toHex, rgba, mix, lighten, darken, toHsl, hsl, shift, jit, nz, mk, layer, poly, rr, blobPts, smooth, blob, resample, ink, inkPoly, inkRect, dabs, paint, glow, glowRect, neon, neonText, tex, initTex, pat, texture, blurred, bloom, vignette, lin, rad, post, box};
})();
