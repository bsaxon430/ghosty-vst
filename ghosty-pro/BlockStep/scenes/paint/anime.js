// SPIRIT HILLS — the painted world: gouache sky and hills, inked cel line work on the near things.
// One painter, two compositions (landscape for the plugin window, portrait for phones). Light comes from the upper left.
window.ANIME = (cfg) => {
  const {W, H, S, HX, HZ, k = 1, name = 'anime'} = cfg;   // HX: the hero hill's x; HZ: the horizon's y; k: size of things
  const {TAU, clamp, lerp, R, rgba, mix, lighten, darken, shift, jit, nz, layer, poly, smooth, blob, blobPts, dabs, ink, inkPoly, glow, lin, rad, texture} = PT;
  const L = layer(W, H, S), g = L.g, INK = '#2b1d14', meta = {W, H, hero: cfg.hero, spots: [], birds: [], vp: [HX, HZ]};
  const ridge = (x0, x1, y, amp, f, seed, step = 6) => { const o = []; for (let x = x0; x <= x1 + step; x += step) o.push([x, y - amp * (nz.fbm(x * f + seed * 13.1, seed * 7.7, 3) - .5) * 2]); return o; };
  const fillBelow = (pts, col, bottom = H + 4) => { g.beginPath(); g.moveTo(pts[0][0], bottom); pts.forEach(p => g.lineTo(p[0], p[1])); g.lineTo(pts[pts.length - 1][0], bottom); g.closePath(); g.fillStyle = col; g.fill(); };
  const clipBelow = (pts, bottom = H + 4) => { g.beginPath(); g.moveTo(pts[0][0], bottom); pts.forEach(p => g.lineTo(p[0], p[1])); g.lineTo(pts[pts.length - 1][0], bottom); g.closePath(); g.clip(); };
  const yAt = (pts, x) => { for (let i = 1; i < pts.length; i++) if (pts[i][0] >= x) { const a = pts[i - 1], b = pts[i]; return lerp(a[1], b[1], (x - a[0]) / Math.max(1e-6, b[0] - a[0])); } return pts[pts.length - 1][1]; };

  // ---------------------------------------------------------------- sky
  function sky() {
    const r = R(name + 'sky');
    g.fillStyle = lin(g, 0, 0, 0, HZ + 10, [[0, '#1f63c4'], [.3, '#3f8ee0'], [.62, '#86c4f0'], [.86, '#c8e8f6'], [1, '#f1f6e6']]); g.fillRect(0, 0, W, H);
    // broad gouache strokes, a little uneven like poster paint
    for (let i = 0; i < 520; i++) { const y = Math.pow(r(), 1.2) * (HZ + 6), t = y / HZ, c = jit(mix(mix('#1f63c4', '#86c4f0', clamp(t * 1.5, 0, 1)), '#f1f6e6', clamp((t - .62) * 2.4, 0, 1)), r, 3, .04, .035);
      g.save(); g.globalAlpha = r.r(.12, .34); g.fillStyle = c; g.translate(r() * W, y); g.rotate(r.g() * .05); g.beginPath(); g.ellipse(0, 0, r.r(40, 190) * k, r.r(4, 15) * k, 0, 0, TAU); g.fill(); g.restore(); }
    // sun glare, upper left
    glow(g, cfg.sun[0], cfg.sun[1], 420 * k, '#fff6d8', .5, 'screen'); glow(g, cfg.sun[0], cfg.sun[1], 150 * k, '#ffffff', .8, 'screen');
  }
  // a cumulus: grown billow on billow from a flat base, shaded smooth in grey, then cut into five flat gouache tones
  const CLOUDP = ['#8fa6d4', '#b2c6e8', '#d7e3f5', '#f1f5fb', '#fffdf6'];
  function cloud(cx, baseY, w, h, seed, o = {}) {
    const r = R(name + 'cloud' + seed), far = o.far || 0, ph = r() * 9;
    const env = x => { const t = (x - (cx - w / 2)) / w; return t <= 0 || t >= 1 ? 0 : Math.pow(Math.sin(t * Math.PI), .5) * (.62 + .5 * nz.fbm(t * 2.6 + ph, 1.3, 2)); };
    const P = [], nb = Math.round(7 + w / 46);
    for (let i = 0; i < nb; i++) { const x = cx - w / 2 + w * (i + .5) / nb + r.g() * w * .02, e = env(x), rd = Math.max(h * .09, h * .23 * e + r() * h * .05); P.push([x, baseY - rd * .5, rd]); }
    for (let i = 0; i < nb * 16; i++) { const p = r.p(P), a = -Math.PI / 2 + r.g() * 1.3, nr = p[2] * r.r(.6, .92), nx = p[0] + Math.cos(a) * p[2] * .82, ny = p[1] + Math.sin(a) * p[2] * .82; if (nr < h * .04) continue; if (ny - nr * .7 < baseY - h * env(nx)) continue; P.push([nx, ny, nr]); }
    P.sort((a, b) => a[1] - b[1]);
    const pad = h * .2, B = PT.box(cx - w / 2 - pad, baseY - h - pad, w + pad * 2, h + pad * 1.6, S), q = B.g;
    for (const [x, y, rd] of P) { q.fillStyle = rad(q, x - rd * .4, y - rd * .45, rd * .04, rd * 1.3, [[0, '#ffffff'], [.45, '#e9e9e9'], [.8, '#a8a8a8'], [1, '#787878']]); q.beginPath(); q.arc(x, y, rd, 0, TAU); q.fill(); }
    q.globalCompositeOperation = 'source-atop';
    q.fillStyle = lin(q, 0, baseY - h * .55, 0, baseY, [[0, 'rgba(50,50,50,0)'], [1, 'rgba(50,50,50,.55)']]); q.fillRect(B.x, B.y, B.w, B.h);
    q.fillStyle = lin(q, cx - w / 2, baseY - h, cx + w / 2, baseY, [[0, 'rgba(255,255,255,.3)'], [.45, 'rgba(255,255,255,0)'], [1, 'rgba(30,30,30,.25)']]); q.fillRect(B.x, B.y, B.w, B.h);
    q.globalCompositeOperation = 'destination-out'; q.beginPath(); q.moveTo(B.x, baseY + h); for (let x = B.x; x <= B.x + B.w + 6; x += 5) q.lineTo(x, baseY - h * .035 + (nz.n2(x * .03, ph) - .5) * h * .05); q.lineTo(B.x + B.w, baseY + h); q.fill();
    q.globalCompositeOperation = 'source-over';
    PT.post(B, CLOUDP.map(c => mix(c, '#d3e8f6', far * .75)), {amp: .045, sc: .028 / S * 2, soft: .2, seed: ph, lo: .28, hi: 1});
    B.to(g);
    // the base melts into the haze over the sea
    g.fillStyle = lin(g, 0, baseY - h * .12, 0, baseY + h * .04, [[0, rgba('#cfe3f2', 0)], [1, rgba('#cfe3f2', .55 + far * .3)]]); g.fillRect(cx - w * .56, baseY - h * .12, w * 1.12, h * .16);
  }
  function clouds() {
    const r = R(name + 'clouds');
    // far, small, low over the horizon
    for (let i = 0; i < 9; i++) cloud(r() * W, HZ - r.r(0, 14) * k, r.r(110, 260) * k, r.r(22, 46) * k, 'f' + i, {far: 1});
    // the great thunderhead that stands behind the hill
    for (const c of cfg.clouds) cloud(c[0], c[1], c[2], c[3], 'big' + c[0], {far: c[4] || 0});
    // cirrus streaks
    for (let i = 0; i < 26; i++) { const x = r() * W, y = r.r(8, HZ * .5); g.save(); g.globalAlpha = r.r(.12, .3); g.strokeStyle = '#ffffff'; g.lineWidth = r.r(.8, 2.4) * k; g.lineCap = 'round'; g.beginPath(); g.moveTo(x, y); g.quadraticCurveTo(x + r.r(30, 90) * k, y - r.r(2, 9), x + r.r(80, 190) * k, y + r.g() * 5); g.stroke(); g.restore(); }
    // birds: two inked strokes each
    for (let i = 0; i < 11; i++) { const x = HX + r.g() * 240 * k, y = HZ - r.r(60, 170) * k, s = r.r(2.2, 5) * k; ink(g, [[x - s, y - s * .5], [x, y], [x + s, y - s * .45]], {w: Math.max(.7, s * .22), color: '#2c3a55', wob: .2, seed: i + 3, taper: .5}); meta.birds.push([x, y, s]); }
  }

  // ---------------------------------------------------------------- distance: sea, capes, far hills
  function distance() {
    const r = R(name + 'dist');
    // sea
    g.fillStyle = lin(g, 0, HZ - 2, 0, HZ + 70 * k, [[0, '#cfeaf0'], [.12, '#8fd0e0'], [.6, '#3f9fc4'], [1, '#2f86b0']]); g.fillRect(0, HZ - 2, W, 80 * k);
    for (let i = 0; i < 240; i++) { const y = HZ + Math.pow(r(), 1.6) * 62 * k, x = r() * W, w = r.r(6, 46) * (y - HZ + 6) / 40; g.fillStyle = rgba('#ffffff', r.r(.2, .75)); g.fillRect(x, y, w, Math.max(.5, r.r(.4, 1.1) * k)); }
    glow(g, cfg.sun[0] + 120 * k, HZ + 6, 300 * k, '#fffbe6', .5, 'screen');
    // far mountains, pale
    const m1 = ridge(-10, W + 10, HZ - 2, 26 * k, .006, 3); fillBelow(m1.map(p => [p[0], Math.min(p[1], HZ + 1)]), '#b9d9e6', HZ + 3);
    const m2 = ridge(-10, W + 10, HZ + 3, 15 * k, .011, 9); fillBelow(m2.map(p => [p[0], Math.min(p[1], HZ + 6)]), '#9fc9d0', HZ + 8);
    // sails
    for (let i = 0; i < 5; i++) { const x = r() * W, y = HZ + r.r(8, 30) * k, s = r.r(2.5, 5) * k; g.fillStyle = '#fffdf6'; g.beginPath(); g.moveTo(x, y); g.lineTo(x, y - s * 2.2); g.lineTo(x + s * 1.3, y); g.fill(); g.fillStyle = '#7a4a2a'; g.fillRect(x - s * .9, y, s * 2.4, s * .35); }
  }

  // ---------------------------------------------------------------- land: a hill = paint + grass strokes on the crest (+ ink when near)
  const GREENS = [['#84b6b0', '#93c2b8', '#a4cec0', '#b8dbcb', '#cfe8d6'], ['#679f80', '#79b08b', '#8dc195', '#a5d29e', '#c3e2a8'], ['#488f52', '#5aa45a', '#71b760', '#90ca68', '#bcde78'], ['#38803f', '#4a964a', '#62ac50', '#84c356', '#b9de6c'], ['#2f733a', '#419042', '#58a448', '#7cbe4e', '#b4db66']];
  function hill(pts, depth, o = {}) {
    const r = R(name + 'hill' + (o.seed || depth)), pal = GREENS[depth], x0 = Math.max(-4, pts[0][0]), x1 = Math.min(W + 4, pts[pts.length - 1][0]), top = Math.min(...pts.map(p => p[1])), bot = Math.min(H + 4, o.bottom || H + 4), span = o.span || 240 * k;
    const B = PT.box(x0, top - 2, x1 - x0, bot - top + 4, S), q = B.g, gr = v => { const c = Math.round(clamp(v, 0, 1) * 255); return `rgb(${c},${c},${c})`; };
    for (let x = x0; x < x1; x += 1.5) { const yt = yAt(pts, x), sl = (yAt(pts, x + 12) - yAt(pts, x - 12)) / 24, v = clamp(.6 - sl * 1.25, .2, .95), G = q.createLinearGradient(0, yt, 0, yt + span);
      G.addColorStop(0, gr(v + .16)); G.addColorStop(.1, gr(v + .04)); G.addColorStop(.45, gr(v - .1)); G.addColorStop(1, gr(v - .26)); q.fillStyle = G; q.fillRect(x, yt, 1.9, bot - yt + 2); }
    // drifting sun patches and cloud shadows
    q.globalCompositeOperation = 'source-atop'; for (let i = 0; i < (o.dabs || 60); i++) { const x = lerp(x0, x1, r()), y = yAt(pts, x) + Math.pow(r(), 1.4) * span * 1.2; q.globalAlpha = r.r(.05, .16); q.fillStyle = r.c(.5) ? '#fff' : '#000'; q.beginPath(); q.ellipse(x, y, r.r(30, 150) * k, r.r(8, 30) * k, r.g() * .2, 0, TAU); q.fill(); }
    q.globalAlpha = 1; q.globalCompositeOperation = 'source-over';
    PT.post(B, pal, {amp: o.amp ?? .09, sc: (o.sc ?? .012) * 2 / S, soft: .16, seed: depth * 3.7 + 1}); B.to(g);
    g.save(); clipBelow(pts, bot); texture(g, 'bristle', x0, top, x1 - x0, bot - top, {alpha: .16, scale: .8, mode: 'soft-light'}); g.restore();
    const dens = o.grass ?? (depth >= 2 ? 1 : 0); if (dens) for (let x = x0; x < x1; x += 1.7 / dens) { const y = yAt(pts, x) + 1, hgt = r.r(2, 7) * k * (.5 + depth * .22), lean = r.g() * 2 + 1.6;
      g.strokeStyle = jit(r.p(pal), r, 5, .05, .04); g.lineWidth = r.r(.6, 1.3) * k; g.lineCap = 'round'; g.beginPath(); g.moveTo(x, y); g.quadraticCurveTo(x + lean * .3, y - hgt * .6, x + lean, y - hgt); g.stroke(); }
  }
  // a broadleaf tree: a trunk with boughs; the crown is shaded in grey then cut into five greens; near trees are inked
  function tree(x, y, s, seed, o = {}) {
    const r = R(name + 'tree' + seed), {far = 0, inked = false, tone = 0} = o, th = s * r.r(.48, .7), tw = s * .085;
    const pal = ['#245c36', '#2f7440', '#44914a', '#6db04e', '#b9d85e'].map(c => mix(shift(c, tone * 12, 0, 0), '#a9d2c4', far));
    const bark = mix('#5a3d2a', '#8fb0a8', far), barkL = mix('#8d6844', '#a9c4b8', far);
    const cx = x + r.g() * s * .04, cy = y - th - s * .3, P = [], nb = far ? 7 : 11;
    for (let i = 0; i < nb; i++) { const a = r() * TAU, d = Math.sqrt(r()) * s * .36; P.push([cx + Math.cos(a) * d * 1.25, cy + Math.sin(a) * d * .72, s * r.r(.15, .26)]); }
    for (let i = 0; i < nb * 3; i++) { const p = r.p(P), a = r() * TAU, nr = p[2] * r.r(.5, .8); P.push([p[0] + Math.cos(a) * p[2] * .8, p[1] + Math.sin(a) * p[2] * .62, nr]); }
    P.sort((a, b) => a[1] - b[1]);
    const sil = gg => { gg.beginPath(); for (const [bx, by, br] of P) { gg.moveTo(bx + br, by); gg.arc(bx, by, br, 0, TAU); } };
    // trunk and two boughs
    const trunk = [[x - tw, y], [x - tw * .55, y - th * .5], [x - tw * .8, y - th * 1.1], [x + tw * .5, y - th * 1.1], [x + tw * .5, y - th * .5], [x + tw * 1.25, y]];
    g.fillStyle = bark; poly(g, trunk); g.fill(); g.fillStyle = barkL; poly(g, [[x - tw * .85, y], [x - tw * .45, y - th * .5], [x - tw * .7, y - th * 1.1], [x - tw * .2, y - th * 1.1], [x - tw * .05, y - th * .5], [x - tw * .3, y]]); g.fill();
    if (!far) for (const sd of [-1, 1]) { g.strokeStyle = bark; g.lineWidth = tw * .7; g.lineCap = 'round'; g.beginPath(); g.moveTo(x, y - th * .78); g.quadraticCurveTo(x + sd * s * .1, y - th * 1.05, x + sd * s * .24, y - th - s * .2); g.stroke(); }
    if (inked) { g.save(); g.lineWidth = Math.max(1.3, s * .02) * 2; g.strokeStyle = INK; g.lineJoin = 'round'; sil(g); g.stroke(); g.restore(); }
    const m = s * .1, bx0 = Math.min(...P.map(p => p[0] - p[2])) - m, by0 = Math.min(...P.map(p => p[1] - p[2])) - m, bx1 = Math.max(...P.map(p => p[0] + p[2])) + m, by1 = Math.max(...P.map(p => p[1] + p[2])) + m;
    const B = PT.box(bx0, by0, bx1 - bx0, by1 - by0, S), q = B.g;
    for (const [px, py, pr2] of P) { q.fillStyle = rad(q, px - pr2 * .4, py - pr2 * .5, pr2 * .05, pr2 * 1.25, [[0, '#f4f4f4'], [.4, '#b4b4b4'], [.78, '#6e6e6e'], [1, '#3c3c3c']]); q.beginPath(); q.arc(px, py, pr2, 0, TAU); q.fill(); }
    q.globalCompositeOperation = 'source-atop'; q.fillStyle = lin(q, bx0, by0, bx1, by1, [[0, 'rgba(255,255,255,.3)'], [.5, 'rgba(0,0,0,0)'], [1, 'rgba(0,0,0,.42)']]); q.fillRect(bx0, by0, bx1 - bx0, by1 - by0);
    if (!far) for (let i = 0; i < 160 + s * 2; i++) { const p = r.p(P), a = r() * TAU, d = Math.sqrt(r()) * p[2]; q.globalAlpha = r.r(.12, .3); q.fillStyle = r.c(.5) ? '#fff' : '#000'; q.beginPath(); q.ellipse(p[0] + Math.cos(a) * d, p[1] + Math.sin(a) * d, s * r.r(.02, .05), s * r.r(.01, .025), r.g() - .5, 0, TAU); q.fill(); }
    q.globalAlpha = 1; q.globalCompositeOperation = 'source-over';
    PT.post(B, pal, {amp: far ? .04 : .1, sc: (far ? .05 : .045) * 2 / S, soft: .14, seed: s * .37}); B.to(g);
    if (inked) { const lw = Math.max(1, s * .018); ink(g, [trunk[0], trunk[1], trunk[2]], {w: lw, color: INK, wob: .5, seed: s + 1, taper: .08}); ink(g, [trunk[5], trunk[4], trunk[3]], {w: lw, color: INK, wob: .5, seed: s + 2, taper: .08});
      for (let i = 0; i < 5; i++) { const p = r.p(P); ink(g, [[p[0] - p[2] * .5, p[1] + p[2] * .55], [p[0], p[1] + p[2] * .75], [p[0] + p[2] * .55, p[1] + p[2] * .5]], {w: lw * .8, color: INK, wob: .4, seed: i + s, taper: .45, alpha: .7}); } }
  }
  // a cottage: plaster walls, a red tiled roof, inked
  function house(x, y, s, seed) {
    const r = R(name + 'house' + seed), w = s * r.r(1.1, 1.6), h = s * r.r(.7, .95), rh = s * r.r(.55, .8), roof = r.p(['#d95b3c', '#c9472f', '#e07a3e', '#b6452f', '#5f86a8']), wall = r.p(['#f6edd9', '#f1e3c6', '#fbf4e6']);
    g.fillStyle = rgba('#2f5a36', .35); g.beginPath(); g.ellipse(x + w * .15, y, w * .7, s * .12, 0, 0, TAU); g.fill();
    g.fillStyle = wall; g.fillRect(x - w / 2, y - h, w, h); g.fillStyle = rgba('#b79f7c', .55); g.fillRect(x + w * .12, y - h, w * .38, h);   // shaded side
    g.fillStyle = roof; g.beginPath(); g.moveTo(x - w * .62, y - h); g.lineTo(x - w * .28, y - h - rh); g.lineTo(x + w * .34, y - h - rh); g.lineTo(x + w * .62, y - h); g.closePath(); g.fill();
    g.fillStyle = darken(roof, .22); g.beginPath(); g.moveTo(x + w * .12, y - h); g.lineTo(x + w * .2, y - h - rh); g.lineTo(x + w * .34, y - h - rh); g.lineTo(x + w * .62, y - h); g.fill();
    g.strokeStyle = rgba(darken(roof, .35), .6); g.lineWidth = .6; for (let i = 1; i < 4; i++) { const yy = y - h - rh * i / 4; g.beginPath(); g.moveTo(x - w * .62 + (w * .34) * i / 4, yy); g.lineTo(x + w * .62 - (w * .28) * i / 4, yy); g.stroke(); }
    g.fillStyle = '#6b4a32'; g.fillRect(x - w * .1, y - h * .55, w * .16, h * .55); g.fillStyle = '#7fb6d6'; g.fillRect(x - w * .38, y - h * .7, w * .17, h * .3); g.fillRect(x + w * .2, y - h * .7, w * .17, h * .3);
    g.fillStyle = '#8a5a3a'; g.fillRect(x + w * .16, y - h - rh * 1.25, w * .09, rh * .55);
    const lw = Math.max(.8, s * .035); inkPoly(g, [[x - w / 2, y], [x - w / 2, y - h], [x + w / 2, y - h], [x + w / 2, y]], {w: lw, color: INK, wob: .3, seed: seed * 3}); inkPoly(g, [[x - w * .62, y - h], [x - w * .28, y - h - rh], [x + w * .34, y - h - rh], [x + w * .62, y - h]], {w: lw, color: INK, wob: .3, seed: seed * 5});
  }
  function windmill(x, y, s) {   // the blades are a separate picture (they turn); here: the tower, cap and porch
    g.fillStyle = '#f4ead4'; g.beginPath(); g.moveTo(x - s * .34, y); g.lineTo(x - s * .2, y - s * 1.25); g.lineTo(x + s * .2, y - s * 1.25); g.lineTo(x + s * .34, y); g.fill();
    g.fillStyle = rgba('#b9a27e', .6); g.beginPath(); g.moveTo(x + s * .04, y); g.lineTo(x + s * .06, y - s * 1.25); g.lineTo(x + s * .2, y - s * 1.25); g.lineTo(x + s * .34, y); g.fill();
    g.fillStyle = '#c9472f'; g.beginPath(); g.moveTo(x - s * .27, y - s * 1.22); g.quadraticCurveTo(x, y - s * 1.78, x + s * .27, y - s * 1.22); g.fill(); g.fillStyle = darken('#c9472f', .22); g.beginPath(); g.moveTo(x + s * .05, y - s * 1.22); g.quadraticCurveTo(x + s * .12, y - s * 1.6, x + s * .27, y - s * 1.22); g.fill();
    g.fillStyle = '#6b4a32'; g.fillRect(x - s * .08, y - s * .3, s * .16, s * .3); g.fillStyle = '#7fb6d6'; g.fillRect(x - s * .06, y - s * .82, s * .12, s * .16);
    g.fillStyle = '#8a5a3a'; g.fillRect(x - s * .44, y - s * .52, s * .88, s * .05); for (let i = -4; i <= 4; i++) g.fillRect(x + i * s * .1 - s * .012, y - s * .62, s * .024, s * .12);
    const lw = Math.max(.9, s * .03); inkPoly(g, [[x - s * .34, y], [x - s * .2, y - s * 1.25], [x + s * .2, y - s * 1.25], [x + s * .34, y]], {w: lw, color: INK, wob: .3, seed: 41});
    ink(g, [[x - s * .27, y - s * 1.22], [x - s * .14, y - s * 1.47], [x, y - s * 1.52], [x + s * .14, y - s * 1.47], [x + s * .27, y - s * 1.22]], {w: lw, color: INK, wob: .2, seed: 43});
    meta.mill = [x, y - s * 1.3, s];
  }
  function lighthouse(x, y, s) {
    g.fillStyle = '#fbf6ea'; g.beginPath(); g.moveTo(x - s * .2, y); g.lineTo(x - s * .12, y - s * 1.5); g.lineTo(x + s * .12, y - s * 1.5); g.lineTo(x + s * .2, y); g.fill();
    g.fillStyle = '#d6452b'; for (const [a, b] of [[.28, .5], [.82, 1.04]]) { g.beginPath(); g.moveTo(x - s * (.2 - .08 * a / 1.5), y - s * a); g.lineTo(x - s * (.2 - .08 * b / 1.5), y - s * b); g.lineTo(x + s * (.2 - .08 * b / 1.5), y - s * b); g.lineTo(x + s * (.2 - .08 * a / 1.5), y - s * a); g.fill(); }
    g.fillStyle = rgba('#8f8a9c', .4); g.beginPath(); g.moveTo(x + s * .03, y); g.lineTo(x + s * .03, y - s * 1.5); g.lineTo(x + s * .12, y - s * 1.5); g.lineTo(x + s * .2, y); g.fill();
    g.fillStyle = '#3a3340'; g.fillRect(x - s * .17, y - s * 1.56, s * .34, s * .07); g.fillStyle = '#fff3b0'; g.fillRect(x - s * .1, y - s * 1.8, s * .2, s * .24); g.fillStyle = '#d6452b'; g.beginPath(); g.moveTo(x - s * .15, y - s * 1.8); g.lineTo(x, y - s * 2.02); g.lineTo(x + s * .15, y - s * 1.8); g.fill();
    glow(g, x, y - s * 1.68, s * .9, '#fff6c0', .7, 'screen');
    const lw = Math.max(.9, s * .04); inkPoly(g, [[x - s * .2, y], [x - s * .12, y - s * 1.5], [x + s * .12, y - s * 1.5], [x + s * .2, y]], {w: lw, color: INK, wob: .25, seed: 61}); inkPoly(g, [[x - s * .15, y - s * 1.8], [x, y - s * 2.02], [x + s * .15, y - s * 1.8]], {w: lw, color: INK, wob: .2, seed: 63});
    meta.light = [x, y - s * 1.68, s];
  }
  // grass and flowers over a region (drawn in y order so near blades overlap far ones)
  function meadow(x0, x1, yTop, yBot, o = {}) {
    const r = R(name + 'meadow' + (o.seed || 0)), n = o.n || 9000, items = [];
    for (let i = 0; i < n; i++) { const t = Math.pow(r(), .85); items.push([lerp(x0, x1, r()), lerp(yTop, yBot, t), t, r()]); }
    items.sort((a, b) => a[1] - b[1]);
    const FL = ['#ffffff', '#ffe36a', '#ff9ac2', '#ffb347', '#c9a0ff', '#ff7a6a', '#8fd0ff'];
    for (const [x, y, t, q] of items) { const sz = (5 + t * 22) * k * (o.size || 1), lean = (nz.n2(x * .01, y * .02) - .35) * sz * .9 + r.g() * sz * .15, sun = nz.fbm(x * .004 + 5, y * .006, 2);
      const c = q < .5 ? mix('#3c8a3a', '#6dbb4c', sun) : q < .82 ? mix('#58a643', '#b4dc5c', sun) : mix('#2f7436', '#4f9a44', sun);
      g.strokeStyle = jit(c, r, 5, .05, .04); g.lineWidth = Math.max(.6, (.8 + t * 1.6) * k); g.lineCap = 'round'; g.beginPath(); g.moveTo(x, y); g.quadraticCurveTo(x + lean * .25, y - sz * .6, x + lean, y - sz); g.stroke();
      if (q > .955 && t > .05) { const fc = r.p(FL), fs = (1.3 + t * 3.4) * k, fx = x + lean, fy = y - sz;
        g.fillStyle = fc; for (let p2 = 0; p2 < 5; p2++) { const a = p2 / 5 * TAU + q * 9; g.beginPath(); g.ellipse(fx + Math.cos(a) * fs * .75, fy + Math.sin(a) * fs * .75, fs * .62, fs * .4, a, 0, TAU); g.fill(); }
        g.fillStyle = fc === '#ffe36a' ? '#e08a2a' : '#ffd84a'; g.beginPath(); g.arc(fx, fy, fs * .42, 0, TAU); g.fill();
        if (t > .45) { g.strokeStyle = rgba(INK, .7); g.lineWidth = Math.max(.5, fs * .12); g.beginPath(); g.arc(fx, fy, fs * 1.25, 0, TAU); g.stroke(); } } }
  }
  // the stone steps that climb the hill ("Block Step"): blocks in perspective, sun on top, inked
  function steps(path, w0, w1) {
    const r = R(name + 'steps'), n = path.length;
    for (let i = n - 1; i >= 0; i--) { const [x, y] = path[i], t = i / (n - 1), w = lerp(w0, w1, t), h = w * .2, d = w * .11, sk = (i % 2 ? 1 : -1) * w * .04;
      g.fillStyle = rgba('#2f6b36', .3); g.beginPath(); g.ellipse(x + w * .06, y + h * .9, w * .62, h * .5, 0, 0, TAU); g.fill();
      const top = [[x - w / 2 + sk, y - d], [x + w / 2 + sk, y - d], [x + w / 2, y], [x - w / 2, y]], face = [[x - w / 2, y], [x + w / 2, y], [x + w / 2, y + h], [x - w / 2, y + h]];
      poly(g, face); g.fillStyle = jit('#a8997f', r, 4, .03, .04); g.fill(); poly(g, top); g.fillStyle = jit('#e6dcc6', r, 4, .03, .03); g.fill();
      g.save(); poly(g, top); g.clip(); for (let q = 0; q < 5; q++) { g.fillStyle = rgba(r.p(['#fff8e6', '#cfc3aa', '#b8d08a']), r.r(.2, .5)); g.beginPath(); g.ellipse(x + r.g() * w * .4, y - d * r(), w * r.r(.05, .16), d * r.r(.1, .3), 0, 0, TAU); g.fill(); } g.restore();
      g.fillStyle = rgba('#6f9a4a', .75); for (let q = 0; q < 4; q++) g.fillRect(x - w / 2 + r() * w, y + h * r.r(.5, .9), w * r.r(.03, .09), h * .25);   // moss in the joints
      if (w > 9) { const lw = Math.max(.8, w * .018); inkPoly(g, top, {w: lw, color: INK, wob: .35, seed: i * 7}); inkPoly(g, [[x + w / 2, y], [x + w / 2, y + h], [x - w / 2, y + h], [x - w / 2, y]], {w: lw, color: INK, wob: .35, seed: i * 7 + 3}); }
    }
  }

  // ---------------------------------------------------------------- compose
  const r0 = R(name + 'compose');
  sky(); clouds(); distance();
  // far hills with the windmill (left of the hero) and the lighthouse cape (right)
  const h0 = ridge(-10, W + 10, HZ + 10 * k, 20 * k, .004, 21); hill(h0, 0, {seed: 'a', dabs: 30, bottom: HZ + 80 * k});
  for (let i = 0; i < 40; i++) { const x = r0() * W; tree(x, yAt(h0, x) + 3, r0.r(7, 13) * k, 'far' + i, {far: .7}); }
  const mx = HX - 172 * k, cape = ridge(HX + 90 * k, W + 10, HZ + 4 * k, 12 * k, .01, 31).map((p, i, a) => [p[0], p[1] - Math.sin(clamp((p[0] - a[0][0]) / (190 * k), 0, 1) * Math.PI) * 20 * k]);
  fillBelow(cape, '#8cc6a6', HZ + 40 * k); hill(cape, 1, {seed: 'cape', dabs: 20, bottom: HZ + 60 * k}); lighthouse(HX + 168 * k, yAt(cape, HX + 168 * k) + 2, 26 * k);
  for (let i = 0; i < 9; i++) { const x = HX + r0.r(205, 330) * k; house(x, yAt(cape, x) + 3 + r0() * 4, r0.r(6, 9) * k, 'cape' + i); }
  const h1 = ridge(-10, W + 10, HZ + 26 * k, 26 * k, .0052, 51); h1.forEach(p => { p[1] -= Math.exp(-Math.pow((p[0] - mx) / (95 * k), 2)) * 26 * k; });
  hill(h1, 1, {seed: 'b', dabs: 50, bottom: HZ + 120 * k}); windmill(mx, yAt(h1, mx) + 2, 30 * k);
  for (let i = 0; i < 28; i++) { const x = r0() * W; if (Math.abs(x - mx) < 34 * k) continue; tree(x, yAt(h1, x) + 4 + r0() * 14 * k, r0.r(11, 20) * k, 'm' + i, {far: .4, tone: r0.g()}); }
  // the village on the right-hand slopes
  const h2 = ridge(-10, W + 10, HZ + 62 * k, 24 * k, .0045, 77); h2.forEach(p => { p[1] -= Math.exp(-Math.pow((p[0] - HX) / (210 * k), 2)) * 62 * k; });
  hill(h2, 2, {seed: 'c', dabs: 80, bottom: HZ + 230 * k}); meta.crest = [];
  for (let i = 0; i < 16; i++) { const x = HX + (r0.c(.5) ? r0.r(250, 560) : -r0.r(300, 620)) * k; if (x < 0 || x > W) continue; house(x, yAt(h2, x) + 6 + r0() * 22 * k, r0.r(10, 16) * k, 'v' + i); }
  for (let i = 0; i < 26; i++) { const x = r0() * W; if (Math.abs(x - HX) < 150 * k) continue; tree(x, yAt(h2, x) + 6 + r0() * 30 * k, r0.r(18, 34) * k, 'n' + i, {far: .12, tone: r0.g()}); }
  // the hero hill: the drummer sits on its crown; little spirits line the crest; the steps climb to it
  const h3 = ridge(-10, W + 10, HZ + 120 * k, 16 * k, .006, 99); h3.forEach(p => { p[1] -= Math.exp(-Math.pow((p[0] - HX) / (150 * k), 2)) * 96 * k; });
  hill(h3, 3, {seed: 'd', dabs: 130, grass: 1.4, bottom: H + 4, span: 420 * k});
  const crown = [HX, yAt(h3, HX)]; meta.drum = [crown[0], crown[1], cfg.ghostH];
  for (let i = 0; i < 12; i++) { const side = i % 2 ? 1 : -1, j = Math.floor(i / 2), d = (72 + j * 25) * k, x = HX + side * d; meta.spots.push([i, x, Math.min(yAt(h3, x) - 1, crown[1] + (16 + j * 2.2) * k), (12.5 - j * .7) * k]); }
  // steps: from the bottom, winding up to the crown
  const sp = []; for (let i = 0; i <= 15; i++) { const t = i / 15, y = lerp(H - 30 * k, crown[1] + 16 * k, Math.pow(t, .78)); sp.push([lerp(cfg.stepX, HX - 4 * k, Math.pow(t, 1.5)) + Math.sin(t * 5.2) * 46 * k * (1 - t), y]); }
  meadow(-10, W + 10, crown[1] + 40 * k, H + 20, {n: cfg.grassN || 15000, seed: 1});
  steps(sp, 150 * k, 24 * k);
  // near trees framing the picture, inked
  for (const [x, y, s, sd] of cfg.trees) tree(x, y, s, sd, {inked: true, tone: -1});
  meadow(-10, W + 10, H - 150 * k, H + 30, {n: Math.round((cfg.grassN || 15000) * .35), seed: 2, size: 1.5});
  // light: rays from the upper left, warm bloom, paper
  g.save(); g.globalCompositeOperation = 'screen'; for (let i = 0; i < 9; i++) { const a = .5 + i * .085 + r0.g() * .02, x0 = cfg.sun[0], y0 = cfg.sun[1], len = Math.hypot(W, H) * 1.2, wd = r0.r(.03, .075);
    g.fillStyle = lin(g, x0, y0, x0 + Math.cos(a) * len, y0 + Math.sin(a) * len, [[0, rgba('#fff8d8', .1)], [.5, rgba('#fff8d8', .035)], [1, rgba('#fff8d8', 0)]]); g.beginPath(); g.moveTo(x0, y0); g.lineTo(x0 + Math.cos(a - wd) * len, y0 + Math.sin(a - wd) * len); g.lineTo(x0 + Math.cos(a + wd) * len, y0 + Math.sin(a + wd) * len); g.fill(); } g.restore();
  window.meta = meta;
  return L;
};
window.main = async (a) => {
  const S = a.S || 2, cfg = a.portrait
    ? {W: 390, H: 844, S, HX: 195, HZ: 312, k: .62, hero: [16, 212, 358, 130], sun: [20, 60], ghostH: 96, stepX: 150, name: 'animeP', grassN: 7000, clouds: [[70, 313, 170, 90, .3], [285, 314, 250, 190, 0]], trees: [[-8, 560, 150, 't1'], [398, 600, 130, 't2']]}
    : {W: 1370, H: 835, S, HX: 812, HZ: 176, k: 1, hero: [611, 10, 403, 219], sun: [90, 20], ghostH: 142, stepX: 520, name: 'anime', clouds: [[250, 178, 460, 130, .35], [1250, 176, 330, 120, .3], [640, 177, 230, 120, .15], [960, 178, 360, 190, 0]], trees: [[70, 560, 330, 't1'], [1290, 600, 300, 't2'], [330, 420, 150, 't3'], [1120, 430, 150, 't4']]};
  const L = ANIME(cfg);
  PT.texture(L.g, 'paper', 0, 0, L.W, L.H, {alpha: .2, scale: 1.2 / S, mode: 'multiply'});
  PT.texture(L.g, 'grain', 0, 0, L.W, L.H, {alpha: .06, scale: 1 / S});
  out[a.portrait ? 'anime-p' : 'anime'] = L.c;
};
