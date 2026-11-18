// GHOST CITY — the haunted cyberpunk boulevard. One painter, two compositions (landscape for the plugin window, portrait for phones).
// Pinhole view down a wide avenue: X = metres left/right of the centre line, Y = metres up, Z = metres away.
window.CITY = (cfg) => {
  const {W, H, S, VP, f = 700, camH = 9, half = 22, name = 'city'} = cfg;
  const {TAU, clamp, lerp, R, rgba, mix, lighten, darken, shift, jit, nz, layer, poly, rr, glow, neon, neonText, lin, rad, ink, texture} = PT;
  const L = layer(W, H, S); let g = L.g;
  const pr = (X, Y, Z) => [VP[0] + f * X / Z, VP[1] + f * (camH - Y) / Z], sc = Z => f / Z;
  const NEON = {mag: '#ff2fa8', pink: '#ff6ad0', cyan: '#27e4ff', blue: '#4f7bff', pur: '#a855f7', grn: '#00ff88', yel: '#ffd23a', org: '#ff8a2a', red: '#ff3d55', wht: '#eaf2ff', lime: '#7cff4f'};
  const NC = Object.values(NEON);
  const HAZE = '#3a1466', HAZE2 = '#b0307c';
  const hz = (c, Z, k = 1) => mix(c, HAZE, clamp(Math.pow(Z / 520, .8) * k, 0, .86));   // atmosphere: far things sink into violet haze
  const meta = {W, H, signs: [], lamps: [], windows: [], hero: cfg.hero, vp: VP};
  const JP = ['ゴーストブロック', 'ブロックステップ', 'ドラム', 'ビート', 'リズム', 'ブレイク', '幽霊', '太鼓', '音楽', 'ホテル', 'ラーメン', '酒', '薬', 'カラオケ', '喫茶', 'レコード', '電気', '夜', '雨', '月', '営業中', '24時間', 'ネオン', 'ゴースト', 'お化け', '鼓動', 'ベース', 'ステップ', '麺', '湯', '本', '花', '夢'];
  const EN = ['GHOSTBLOCK', 'BLOCK STEP', 'OPEN', '24H', 'HOTEL', 'BAR', 'SOUND', 'BEATS', 'DRUMS', 'NEON', 'GHOST'];
  const JFONT = ['"Dela Gothic One"', '"Zen Maru Gothic"', '"RocknRoll One"', '"Reggae One"', '"Mochiy Pop One"', '"Potta One"', '"Yusei Magic"'];
  const EFONT = ['Orbitron', 'Audiowide', '"Tilt Neon"', 'Rajdhani'];
  const isJ = t => /[^\x00-\x7f]/.test(t);
  const GBODY = 'M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z';
  // [drum index, side, Z, top (m), height (m), width (m), out from the wall (m), text, colour, style]
  const DRUMS = [[0, -1, 86, 30, 10, 2.9, .6, 'キック', '#00ff88', 'tube'], [1, 1, 86, 30, 10, 2.9, .6, 'スネア', '#b56bff', 'tube'], [2, -1, 112, 24.5, 9, 2.5, 3.4, 'クラップ', '#ff7ac0', 'box'], [3, 1, 112, 24.5, 7, 2.5, 3.4, 'ハット', '#6aa9ff', 'box'],
    [4, -1, 142, 27, 8.4, 2.6, 2.2, 'オープン', '#22d3ee', 'tube'], [5, 1, 142, 27, 8.4, 2.6, 2.2, 'パーカス', '#ff9a45', 'tube'], [6, -1, 176, 21.5, 5.2, 2.6, 4.2, 'リム', '#ffc93a', 'led'], [7, 1, 176, 23.5, 9, 2.6, 4.2, 'クラッシュ', '#ff6b6b', 'box'],
    [8, -1, 214, 26, 5, 3, 5.4, 'サブ', '#e879f9', 'tube'], [9, 1, 214, 22, 4.6, 4, 5.8, 'FX', '#f1ecff', 'tube'], [10, -1, 262, 24, 7.5, 3, 7, 'ピアノ', '#fde68a', 'box'], [11, 1, 262, 26, 9, 3, 7, 'アシッド', '#7cff4f', 'led']];
  const drumRect = d => { const [i, side, Z, top, h, w, out] = d, p = sc(Z), X = side * half, x = side < 0 ? VP[0] + f * (X + out) / Z : VP[0] + f * (X - out - w) / Z; return [x, pr(0, top, Z)[1], w * p, h * p, side < 0 ? X + out + w / 2 : X - out - w / 2]; };
  meta.drums = [];

  // ---------------------------------------------------------------- signs
  // a sign facing us: (x, y) top-left on screen, w x h on screen px (design units). style: box | tube | led
  function sign(x, y, w, h, o) {
    const {c = NEON.mag, txt = '', style = 'box', rnd, Z = 60, lit = 1, c2, dim = 0} = o, k = clamp(1 - Z / 700, .25, 1), vert = h > w * 1.25, pad = Math.min(w, h) * .12;
    const hazeK = clamp(Z / 600, 0, .55);
    g.save();
    // bracket / shadow behind
    g.fillStyle = rgba('#05030c', .55); g.fillRect(x - w * .04, y - h * .01, w * 1.08, h * 1.02);
    if (style === 'box') {
      const top = lighten(c, .18), bot = darken(c, .28);
      g.fillStyle = lin(g, x, y, x, y + h, [[0, mix(top, HAZE, hazeK)], [.5, mix(c, HAZE, hazeK)], [1, mix(bot, HAZE, hazeK)]]); rr(g, x, y, w, h, Math.min(w, h) * .08); g.fill();
      g.save(); rr(g, x, y, w, h, Math.min(w, h) * .08); g.clip();
      g.globalCompositeOperation = 'lighter'; g.fillStyle = rad(g, x + w / 2, y + h * .45, 0, Math.max(w, h) * .6, [[0, rgba(lighten(c, .6), .5 * lit)], [1, rgba(c, 0)]]); g.fillRect(x, y, w, h);
      g.globalCompositeOperation = 'source-over'; texture(g, 'grain', x, y, w, h, {alpha: .16, scale: .5});
      // grime streaks from the top edge
      for (let i = 0; i < 5; i++) { const sx = x + rnd() * w; g.fillStyle = rgba('#12061e', rnd.r(.08, .22)); g.fillRect(sx, y, rnd.r(.4, 1.4), h * rnd.r(.15, .6)); }
      g.restore();
      g.lineWidth = Math.max(.6, Math.min(w, h) * .05); g.strokeStyle = rgba(darken(c, .62), .95); rr(g, x, y, w, h, Math.min(w, h) * .08); g.stroke();
      g.lineWidth = Math.max(.35, Math.min(w, h) * .018); g.strokeStyle = rgba(lighten(c, .7), .8); rr(g, x + pad * .45, y + pad * .45, w - pad * .9, h - pad * .9, Math.min(w, h) * .06); g.stroke();
    } else {
      g.fillStyle = lin(g, x, y, x, y + h, [[0, mix('#161126', HAZE, hazeK)], [1, mix('#0a0714', HAZE, hazeK)]]); rr(g, x, y, w, h, Math.min(w, h) * .07); g.fill();
      g.lineWidth = Math.max(.5, Math.min(w, h) * .03); g.strokeStyle = rgba('#3b3157', .9); rr(g, x, y, w, h, Math.min(w, h) * .07); g.stroke();
      if (style === 'tube') neon(g, gg => rr(gg, x + pad * .55, y + pad * .55, w - pad * 1.1, h - pad * 1.1, Math.min(w, h) * .1), c2 || c, Math.max(.5, Math.min(w, h) * .03), {S, halo: .8 * lit});
      else { g.save(); rr(g, x, y, w, h, 2); g.clip(); g.fillStyle = rgba(c, .07); for (let yy = y; yy < y + h; yy += Math.max(1.2, h / 46)) g.fillRect(x, yy, w, .5); g.restore(); }
    }
    // lettering
    if (txt) {
      const chars = [...txt], n = chars.length, jp = isJ(txt), font = o.font || (style === 'led' ? '"DotGothic16"' : jp ? rnd.p(JFONT) : rnd.p(EFONT)), wt = /Zen Maru/.test(font) ? 900 : /Rajdhani|Orbitron/.test(font) ? 700 : 400;
      let size, tx = x + w / 2, ty;
      if (vert) { size = Math.min((w - pad * 2) * .98, (h - pad * 2) / (n * 1.04)); ty = y + h / 2 - (n - 1) * size * 1.04 / 2; }
      else { g.font = `${wt} 100px ${font}`; const tw = g.measureText(txt).width / 100; size = Math.min((h - pad * 1.6) * .9, (w - pad * 2) / tw); ty = y + h / 2 + size * .04; }
      const fnt = `${wt} ${size}px ${font}`;
      if (style === 'box') { g.save(); g.font = fnt; g.textAlign = 'center'; g.textBaseline = 'middle'; const ink2 = o.ink || (PT.toHsl(c)[2] > .62 ? darken(c, .78) : '#fff7ff');
        const put = (t, a, b) => { g.lineWidth = size * .09; g.lineJoin = 'round'; g.strokeStyle = rgba(darken(c, .7), .55); if (ink2 === '#fff7ff') g.strokeText(t, a, b); g.fillStyle = ink2; g.fillText(t, a, b); };
        if (vert) chars.forEach((ch, i) => { g.save(); if (ch === 'ー') { g.translate(tx, ty + i * size * 1.04); g.rotate(Math.PI / 2); put(ch, 0, 0); } else put(ch, tx, ty + i * size * 1.04); g.restore(); }); else put(txt, tx, ty); g.restore(); }
      else neonText(g, txt, tx, ty, fnt, c, {S, size, vertical: vert, gap: 1.04, tube: style === 'tube' ? Math.max(.45, size * .075) : 0, halo: lit});
    }
    // halo on the air around it
    if (!o.noHalo) glow(g, x + w / 2, y + h / 2, Math.max(w, h) * 1.25, c, .16 * k * lit);
    if (dim) { g.fillStyle = rgba('#0a0518', dim); rr(g, x - 1, y - 1, w + 2, h + 2, Math.min(w, h) * .08); g.fill(); }
    g.restore();
    return {x, y, w, h, c};
  }
  const signText = (rnd, vert) => { const jp = rnd.c(.72); let t = jp ? rnd.p(JP) : rnd.p(EN); if (vert && !jp && t.length > 6) t = rnd.p(['OPEN', 'BAR', '24H', 'HOTEL', 'NEON']); if (!vert && jp && t.length > 5) t = rnd.p(['ドラム', 'ビート', 'ラーメン', 'ホテル', '営業中']); return t; };

  // ---------------------------------------------------------------- sky
  function sky() {
    g.fillStyle = lin(g, 0, 0, 0, VP[1] + 40, [[0, '#070418'], [.3, '#150b3a'], [.62, '#3a1368'], [.86, '#8a2a86'], [1, '#d6468f']]); g.fillRect(0, 0, W, H);
    const r = R(name + 'sky');
    // painted cloud bands: long flat dabs, lit from below by the city
    for (let i = 0; i < 150; i++) { const y = lerp(0, VP[1] - 6, Math.pow(r(), .8)), x = r() * W, t = y / VP[1], w = r.r(60, 260) * (1.1 - t * .5), h = r.r(3, 13) * (1.2 - t * .6);
      g.save(); g.globalAlpha = r.r(.05, .22); g.fillStyle = mix(mix('#221050', '#ff5fb0', t * t), '#0a0520', r.r(0, .5)); g.translate(x, y); g.rotate(r.g() * .04);
      g.beginPath(); g.ellipse(0, 0, w / 2, h / 2, 0, 0, TAU); g.fill(); g.restore(); }
    for (let i = 0; i < 46; i++) { const y = lerp(VP[1] - 120, VP[1] - 4, r()), x = r() * W; g.save(); g.globalAlpha = r.r(.06, .2); g.fillStyle = r.p(['#ff8fd0', '#ffb07a', '#c070ff']); g.beginPath(); g.ellipse(x, y, r.r(40, 160), r.r(1.5, 5), 0, 0, TAU); g.fill(); g.restore(); }
    // a few stars up high
    for (let i = 0; i < 120; i++) { const x = r() * W, y = Math.pow(r(), 2.2) * VP[1] * .7; g.fillStyle = rgba('#dfe6ff', r.r(.15, .8)); g.fillRect(x, y, r.r(.4, 1.1), r.r(.4, 1.1)); }
    glow(g, VP[0], VP[1] + 10, W * .45, '#ff4fa8', .38); glow(g, VP[0], VP[1], W * .2, '#ffb0d8', .3);
    texture(g, 'bristle', 0, 0, W, VP[1] + 40, {alpha: .18, scale: .7, mode: 'soft-light'});
  }
  function moon(mx, my, mr) {
    const r = R(name + 'moon');
    glow(g, mx, my, mr * 4.2, '#c9a0ff', .28); glow(g, mx, my, mr * 2, '#ffe9ff', .3);
    g.save(); g.beginPath(); g.arc(mx, my, mr, 0, TAU); g.clip();
    g.fillStyle = rad(g, mx - mr * .3, my - mr * .35, mr * .1, mr * 1.5, [[0, '#fffdf2'], [.55, '#f3e6ff'], [1, '#b99be6']]); g.fillRect(mx - mr, my - mr, mr * 2, mr * 2);
    for (let i = 0; i < 26; i++) { const a = r() * TAU, d = Math.sqrt(r()) * mr * .92, cr = r.r(.05, .2) * mr * (1 - d / mr * .5), cx = mx + Math.cos(a) * d, cy = my + Math.sin(a) * d;
      g.fillStyle = rgba('#9a7fd0', r.r(.12, .3)); g.beginPath(); g.ellipse(cx, cy, cr, cr * r.r(.7, 1), r() * 3, 0, TAU); g.fill();
      g.strokeStyle = rgba('#fff', .25); g.lineWidth = .5; g.beginPath(); g.arc(cx, cy, cr, .6, 2.6); g.stroke(); }
    // maria (dark seas), painted
    PT.dabs(g, mx - mr, my - mr, mr * 2, mr * 2, {n: 40, cols: ['#a48ad8', '#c7b2ee', '#8f76c8'], size: mr * .13, ang: .5, spread: 1.2, alpha: [.08, .22], rnd: r, long: 2});
    g.fillStyle = rad(g, mx + mr * .5, my + mr * .5, mr * .2, mr * 1.5, [[0, rgba('#3a1a70', .55)], [1, rgba('#3a1a70', 0)]]); g.fillRect(mx - mr, my - mr, mr * 2, mr * 2);
    texture(g, 'grain', mx - mr, my - mr, mr * 2, mr * 2, {alpha: .2, scale: .4});
    g.restore();
    // cloud wisps across it
    for (let i = 0; i < 5; i++) { g.save(); g.globalAlpha = r.r(.25, .5); g.fillStyle = '#5b2a8a'; g.beginPath(); g.ellipse(mx + r.g() * mr, my + r.r(.1, .9) * mr, mr * r.r(.9, 1.8), mr * r.r(.04, .1), 0, 0, TAU); g.fill(); g.restore(); }
  }

  // ---------------------------------------------------------------- far skyline (flat cut-outs sinking into haze)
  function skyline() {
    const r = R(name + 'skyline');
    const row = (n, baseY, hMin, hMax, wMin, wMax, col, winA, spread) => {
      for (let i = 0; i < n; i++) { const x = VP[0] + r.g() * spread + r.g() * spread * .4, w = r.r(wMin, wMax), h = r.r(hMin, hMax) * (1 - Math.min(.5, Math.abs(x - VP[0]) / spread * .35)), y = baseY - h;
        const c = jit(col, r, 8, .05, .03);
        g.fillStyle = lin(g, 0, y, 0, baseY, [[0, c], [1, mix(c, HAZE2, .55)]]);
        const kind = r.i(0, 3); g.beginPath();
        if (kind === 0) g.rect(x, y, w, h); else if (kind === 1) { g.moveTo(x, baseY); g.lineTo(x, y + h * .12); g.lineTo(x + w * .3, y); g.lineTo(x + w, y); g.lineTo(x + w, baseY); }
        else if (kind === 2) { g.rect(x, y + h * .18, w, h); g.rect(x + w * .25, y, w * .5, h); } else { g.moveTo(x, baseY); g.lineTo(x + w * .08, y + h * .1); g.lineTo(x + w * .5, y); g.lineTo(x + w * .92, y + h * .1); g.lineTo(x + w, baseY); }
        g.fill();
        // window specks
        const cw = Math.max(1.1, w / r.i(5, 9)), ch = cw * r.r(1.2, 2);
        for (let yy = y + h * .2 + 2; yy < baseY - 2; yy += ch) for (let xx = x + 1.2; xx < x + w - cw * .6; xx += cw) if (r.c(winA)) { g.fillStyle = rgba(r.p(['#ffd9a0', '#ff9ad8', '#9be8ff', '#ffe9c8', '#c9a0ff']), r.r(.35, .95)); g.fillRect(xx, yy, cw * .55, ch * .5); }
        if (r.c(.5)) { g.strokeStyle = rgba(c, 1); g.lineWidth = .7; g.beginPath(); g.moveTo(x + w * .5, y); g.lineTo(x + w * .5, y - r.r(6, 22)); g.stroke(); g.fillStyle = '#ff4060'; g.fillRect(x + w * .5 - .8, y - 1, 1.6, 1.6); meta.lamps.push([x + w * .5, y, 0]); }
        if (r.c(.16)) { const sy = y + h * r.r(.1, .4); g.fillStyle = rgba(r.p(NC), .85); g.fillRect(x + 1, sy, w - 2, Math.max(1, h * .03)); }
      } };
    row(46, VP[1] + 4, 40, 150, 10, 34, '#5a2a90', .2, 330);
    glow(g, VP[0], VP[1] - 20, 320, '#ff5fb5', .2);
    row(34, VP[1] + 6, 30, 185, 14, 46, '#3d1c74', .3, 300);
    row(22, VP[1] + 8, 24, 120, 18, 60, '#2a1458', .34, 300);
    // holo rings and a searchlight
    for (let i = 0; i < 3; i++) { const x = VP[0] + r.g() * 220, y = VP[1] - r.r(70, 150); g.save(); g.globalCompositeOperation = 'lighter'; g.strokeStyle = rgba(r.p([NEON.cyan, NEON.mag]), .5); g.lineWidth = 1; g.beginPath(); g.ellipse(x, y, r.r(14, 30), r.r(3, 6), 0, 0, TAU); g.stroke(); g.restore(); }
  }

  // ---------------------------------------------------------------- the two walls of the avenue
  const WALLC = ['#1b1536', '#231a44', '#161b38', '#2a1846', '#1a2140', '#301a3c', '#12223a', '#241238'];
  function facade(side, Z0, Z1, Hb, r, idx) {
    const X = side * half, base = r.p(WALLC), A = pr(X, 0, Z0), B = pr(X, 0, Z1), C = pr(X, Hb, Z1), D = pr(X, Hb, Z0), Zm = (Z0 + Z1) / 2;
    const c0 = hz(base, Z0), c1 = hz(base, Z1);
    // wall plane: darker up high, picking up the street's neon low down
    g.save(); poly(g, [A, B, C, D]); g.clip();
    g.fillStyle = lin(g, A[0], 0, B[0], 0, [[0, c0], [1, c1]]); g.fillRect(Math.min(A[0], B[0]) - 2, Math.min(C[1], D[1]) - 2, Math.abs(B[0] - A[0]) + 4, Math.max(A[1], B[1]) - Math.min(C[1], D[1]) + 4);
    const street = r.p([NEON.mag, NEON.cyan, NEON.pur, NEON.org]);
    g.globalCompositeOperation = 'lighter'; g.fillStyle = lin(g, 0, Math.max(A[1], B[1]), 0, Math.max(A[1], B[1]) - sc(Zm) * 16, [[0, rgba(street, .34)], [1, rgba(street, 0)]]); g.fillRect(-W, -H, W * 3, H * 3);
    g.globalCompositeOperation = 'source-over';
    g.fillStyle = lin(g, 0, Math.min(C[1], D[1]), 0, Math.min(C[1], D[1]) + sc(Zm) * 12, [[0, rgba('#05030f', .5)], [1, rgba('#05030f', 0)]]); g.fillRect(-W, -H, W * 3, H * 3);
    const fl = r.p([3.1, 3.4, 3.8]), bay = r.p([2.4, 2.9, 3.4, 4.2]), nF = Math.floor(Hb / fl), mat = r.p(['glass', 'brick', 'panel', 'panel']);
    const litP = r.r(.22, .5), wc = r.p([['#ffd08a', '#ffb060'], ['#9fe9ff', '#5ad0ff'], ['#ff8fd6', '#ffd08a'], ['#ffd08a', '#ffe9c0'], ['#b9a0ff', '#9fe9ff']]);
    const px = sc(Zm);
    if (px > .9) {
      // brick / panel courses
      if (mat !== 'glass' && px > 3) { g.strokeStyle = rgba('#000', .16); g.lineWidth = .5; const st = mat === 'brick' ? .45 : fl / 2; for (let y = 0; y < Hb; y += st) { const a = pr(X, y, Z0), b = pr(X, y, Z1); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); } }
      for (let k = 1; k <= nF; k++) for (let z = Z0 + bay * .25; z < Z1 - bay * .5; z += bay) {
        const y0 = (k - 1) * fl + fl * .34, y1 = k * fl - fl * .16, z0 = z, z1 = z + bay * (mat === 'glass' ? .92 : .62);
        if (k === 1) continue;   // ground floor: shop fronts, drawn below
        const q = [pr(X, y0, z0), pr(X, y0, z1), pr(X, y1, z1), pr(X, y1, z0)], lit = r.c(litP), c = lit ? r.p(wc) : null;
        poly(g, q);
        if (lit) { g.fillStyle = lin(g, 0, q[2][1], 0, q[0][1], [[0, hz(lighten(c, .25), Zm, .6)], [1, hz(darken(c, .25), Zm, .6)]]); g.fill();
          if (px > 4) { const t = r(); g.save(); poly(g, q); g.clip();
            if (t < .3) { g.strokeStyle = rgba('#3a2010', .45); g.lineWidth = Math.max(.4, px * .05); for (let s = .15; s < 1; s += .14) { const a = pr(X, lerp(y0, y1, s), z0), b = pr(X, lerp(y0, y1, s), z1); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); } }   // blinds
            else if (t < .42) ghostShade(q, px, r);   // somebody is home
            else if (t < .55) { g.fillStyle = rgba('#20102a', .6); const a = pr(X, y0, lerp(z0, z1, .55)), b2 = pr(X, lerp(y0, y1, .6), lerp(z0, z1, .55)); g.fillRect(a[0], b2[1], Math.abs(q[1][0] - q[0][0]) * .5, a[1] - b2[1]); }   // curtain
            g.restore(); }
          if (px > 2.2 && r.c(.25)) meta.windows.push([(q[0][0] + q[1][0]) / 2, (q[0][1] + q[2][1]) / 2, Math.abs(q[1][0] - q[0][0]), Math.abs(q[0][1] - q[2][1]), c]);
        } else { g.fillStyle = hz(mat === 'glass' ? '#1a2a52' : '#0c0a1c', Zm, .8); g.fill(); if (px > 3 && r.c(.5)) { g.fillStyle = rgba(r.p([NEON.mag, NEON.cyan, NEON.pur]), .1); g.fill(); } }
        if (px > 3.4) { g.strokeStyle = rgba('#07040f', .7); g.lineWidth = Math.max(.4, px * .06); poly(g, q); g.stroke(); const m0 = pr(X, y0, (z0 + z1) / 2), m1 = pr(X, y1, (z0 + z1) / 2); g.beginPath(); g.moveTo(m0[0], m0[1]); g.lineTo(m1[0], m1[1]); g.stroke();
          g.strokeStyle = rgba(lighten(base, .25), .5); g.lineWidth = Math.max(.4, px * .07); const s0 = pr(X, y0 - fl * .04, z0 - bay * .04), s1 = pr(X, y0 - fl * .04, z1 + bay * .04); g.beginPath(); g.moveTo(s0[0], s0[1]); g.lineTo(s1[0], s1[1]); g.stroke(); }   // sill
      }
      if (px > 4.2) {
        for (let k = 2; k <= nF; k++) if (r.c(.3)) { const za = lerp(Z0, Z1, r.r(0, .5)), zb = Math.min(Z1, za + bay * r.i(2, 5)), y = (k - 1) * fl + fl * .2;
          const a = pr(X - side * .5, y, za), b2 = pr(X - side * .5, y, zb), a2 = pr(X - side * .5, y + 1, za), b3 = pr(X - side * .5, y + 1, zb), w0 = pr(X, y, za), w1 = pr(X, y, zb);
          poly(g, [w0, w1, b2, a]); g.fillStyle = hz(darken(base, .35), Zm); g.fill(); g.strokeStyle = hz(lighten(base, .32), Zm); g.lineWidth = Math.max(.4, px * .06); g.beginPath(); g.moveTo(a2[0], a2[1]); g.lineTo(b3[0], b3[1]); g.stroke();
          for (let z = za; z <= zb + .01; z += .45) { const q0 = pr(X - side * .5, y, z), q1 = pr(X - side * .5, y + 1, z); g.beginPath(); g.moveTo(q0[0], q0[1]); g.lineTo(q1[0], q1[1]); g.stroke(); }
          if (r.c(.4)) { const zz = lerp(za, zb, r.r(.2, .8)), q = pr(X - side * .5, y + 1, zz); g.fillStyle = hz(r.p(['#e8e2ff', '#ff8fd0', '#8fe0ff', '#ffd98a']), Zm, .7); g.fillRect(q[0] - px * .25, q[1], px * .5, px * .9); } }   // washing on the rail
        for (let k = 0; k < r.i(1, 3); k++) { const z = lerp(Z0, Z1, r()), a = pr(X - side * .22, .3, z), b2 = pr(X - side * .22, Hb, z); g.strokeStyle = hz(lighten(base, .16), Zm); g.lineWidth = Math.max(.5, sc(z) * .16); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b2[0], b2[1]); g.stroke(); }
      }
      if (px > 2.4 && Hb > 18 && r.c(.3)) {   // a wall of light: a tall screen with a ghost on it
        const zc = lerp(Z0 + 2, Z1 - 6, r()), zw = Math.min(Z1 - zc - .5, r.r(4, 7)), y1 = Math.min(Hb - 1, r.r(13, 22)), y0 = y1 - r.r(6, 9), zl = side < 0 ? zc : zc + zw, zr = side < 0 ? zc + zw : zc;
        const O = pr(X - side * .12, y1, zl), U = pr(X - side * .12, y1, zr), V = pr(X - side * .12, y0, zl), c = r.p([NEON.cyan, NEON.mag, NEON.grn, NEON.yel, NEON.pur]);
        g.save(); g.transform((U[0] - O[0]) / 100, (U[1] - O[1]) / 100, (V[0] - O[0]) / 140, (V[1] - O[1]) / 140, O[0], O[1]);
        g.fillStyle = '#05030c'; g.fillRect(-3, -3, 106, 146); g.fillStyle = lin(g, 0, 0, 0, 140, [[0, hz(lighten(c, .2), Zm, .5)], [1, hz(darken(c, .5), Zm, .5)]]); g.fillRect(0, 0, 100, 140);
        g.fillStyle = rgba('#fff', .92); g.translate(50 - 32 * 1.5, 14 - 13 * 1.5); g.scale(1.5, 1.5); g.fill(new Path2D(GBODY)); g.fillStyle = darken(c, .7); g.fillRect(24.5, 28, 5, 7); g.fillRect(35, 28, 5, 7);
        g.restore(); g.save(); g.transform((U[0] - O[0]) / 100, (U[1] - O[1]) / 100, (V[0] - O[0]) / 140, (V[1] - O[1]) / 140, O[0], O[1]);
        g.fillStyle = rgba('#05030c', .32); for (let yy = 0; yy < 140; yy += 4) g.fillRect(0, yy, 100, 1.6);
        g.fillStyle = darken(c, .75); g.font = '900 20px "Zen Maru Gothic"'; g.textAlign = 'center'; g.fillText(r.p(['ゴースト', 'ビート', 'ドラム', 'BEATS', 'GHOST']), 50, 128); g.restore();
        const m0 = pr(X, (y0 + y1) / 2, zc + zw / 2); glow(g, m0[0], m0[1], px * 9, c, .3); }
      // floor ledges
      if (px > 1.6) for (let k = 1; k <= nF; k++) { const a = pr(X, k * fl, Z0), b = pr(X, k * fl, Z1); g.strokeStyle = rgba(lighten(base, .2), .22); g.lineWidth = Math.max(.4, px * .1); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
      // ground floor: lit shop fronts with awnings
      if (px > 1.2) for (let z = Z0 + .6; z < Z1 - 3; z += r.r(4.5, 7)) { const z1 = Math.min(Z1 - .6, z + r.r(3.4, 5.2)), q = [pr(X, .3, z), pr(X, .3, z1), pr(X, fl * .86, z1), pr(X, fl * .86, z)], c = r.p(['#ffd08a', '#9fe9ff', '#ff9ad8', '#ffe9c0', '#baffc9']);
        poly(g, q); g.fillStyle = lin(g, 0, q[2][1], 0, q[0][1], [[0, hz(c, Zm, .5)], [1, hz(mix(c, '#ff7a3a', .35), Zm, .5)]]); g.fill();
        if (px > 3) { g.save(); poly(g, q); g.clip(); g.fillStyle = rgba('#1a0c20', .55); for (let s = 0; s < 6; s++) { const zz = lerp(z, z1, r()), a = pr(X, .3, zz), hh = px * r.r(.8, 1.9); g.fillRect(a[0] - px * .3, a[1] - hh, px * r.r(.4, .9), hh); } g.restore(); }
        const aw = r.p([NEON.red, NEON.mag, NEON.yel, NEON.cyan, '#f4efe2']), t0 = pr(X, fl * .86, z), t1 = pr(X, fl * .86, z1), o0 = pr(X - side * 1.1, fl * .7, z), o1 = pr(X - side * 1.1, fl * .7, z1);
        poly(g, [t0, t1, o1, o0]); g.fillStyle = hz(darken(aw, .25), Zm, .7); g.fill(); if (px > 3) { g.strokeStyle = rgba('#fff', .25); g.lineWidth = .5; g.stroke(); }
        glow(g, (q[0][0] + q[1][0]) / 2, q[0][1], px * 5, c, .2); }
    }
    texture(g, 'bristle', -W, -H, W * 3, H * 3, {alpha: .22, scale: .8, mode: 'soft-light', rot: side * 12});
    g.restore();
    // roof line + corner in ink
    if (px > 2) { ink(g, [D, C], {w: Math.max(.6, px * .1), color: '#07040f', wob: .3, seed: idx + 1, taper: 0}); ink(g, [A, D], {w: Math.max(.6, px * .12), color: '#07040f', wob: .3, seed: idx + 7, taper: 0}); }
    return {X, Z0, Z1, Hb, px};
  }
  // a ghost standing in a lit window (dark shape + two bright eyes)
  function ghostShade(q, px, r) { const cx = (q[0][0] + q[1][0]) / 2 + r.g() * px * .2, by = q[0][1], w = Math.abs(q[1][0] - q[0][0]) * .5, h = (q[0][1] - q[2][1]) * .78;
    g.fillStyle = rgba('#160a26', .85); g.beginPath(); g.moveTo(cx - w / 2, by); g.lineTo(cx - w / 2, by - h * .55); g.bezierCurveTo(cx - w / 2, by - h * 1.15, cx + w / 2, by - h * 1.15, cx + w / 2, by - h * .55); g.lineTo(cx + w / 2, by); g.fill();
    g.fillStyle = '#fffbe0'; g.fillRect(cx - w * .22, by - h * .62, w * .13, h * .13); g.fillRect(cx + w * .1, by - h * .62, w * .13, h * .13); }
  function wall(side) {
    const r = R(name + 'wall' + side), B = []; let z = 430, i = 0;
    while (z > 9.5) { const d = z > 200 ? r.r(18, 40) : z > 80 ? r.r(12, 24) : r.r(9, 17), gap = r.c(.3) ? r.r(1.2, 3) : .25; const Z1 = z, Z0 = Math.max(9, z - d);
      const Hb = Z0 > 150 ? r.r(16, 44) : Z0 > 70 ? r.r(15, 34) : r.r(26, 70); B.push({Z0, Z1, Hb, i: i++}); z = Z0 - gap; }
    for (const b of B) {
      // the alley beyond this building glows
      const a0 = pr(side * half, 0, b.Z1 + 3), a1 = pr(side * half, b.Hb * .7, b.Z1 + 3); glow(g, a0[0], (a0[1] + a1[1]) / 2, sc(b.Z1) * 9, r.p([NEON.mag, NEON.org, NEON.cyan]), .22);
      const fc = facade(side, b.Z0, b.Z1, b.Hb, r, b.i * 3 + (side > 0 ? 500 : 0));
      roofStuff(side, b, r); bladeSigns(side, b, r);
    }
  }
  function roofStuff(side, b, r) {
    const Zm = (b.Z0 + b.Z1) / 2, px = sc(Zm); if (px < 1.4) return;
    const X = side * half;
    // water tank / AC boxes / antenna
    for (let k = 0; k < r.i(1, 3); k++) { const z = lerp(b.Z0, b.Z1, r()), [x, y] = pr(X + side * r.r(1, 4), b.Hb, z), w = px * r.r(1.2, 2.6), h = px * r.r(1, 2.4);
      g.fillStyle = hz('#120e24', z); g.fillRect(x - w / 2, y - h, w, h); g.fillStyle = rgba('#fff', .06); g.fillRect(x - w / 2, y - h, w, Math.max(.5, h * .12)); }
    if (r.c(.6)) { const z = lerp(b.Z0, b.Z1, r()), [x, y] = pr(X + side * 2, b.Hb, z), h = px * r.r(4, 9); g.strokeStyle = hz('#0b0818', z); g.lineWidth = Math.max(.5, px * .12); g.beginPath(); g.moveTo(x, y); g.lineTo(x, y - h); g.moveTo(x - px, y - h * .7); g.lineTo(x + px, y - h * .7); g.stroke();
      g.fillStyle = '#ff3d55'; g.beginPath(); g.arc(x, y - h, Math.max(.7, px * .18), 0, TAU); g.fill(); glow(g, x, y - h, px * 1.6, '#ff3d55', .5); meta.lamps.push([x, y - h, px]); }
    // a billboard on the roof edge, facing us
    if (b.Z0 > 60 && b.Z0 < 330 && r.c(.62)) { const z = b.Z0 + .3, w = r.r(7, 15), h = r.r(3, 6.5), [x0, y0] = pr(X + (side > 0 ? 0 : -w) + side * r.r(0, 2), b.Hb + h + r.r(.8, 2), z), p = sc(z);
      g.strokeStyle = hz('#0b0818', z); g.lineWidth = Math.max(.5, p * .14); for (const fx of [.15, .5, .85]) { g.beginPath(); g.moveTo(x0 + w * p * fx, y0 + h * p); g.lineTo(x0 + w * p * fx, y0 + h * p + p * 2.2); g.stroke(); }
      const s = sign(x0, y0, w * p, h * p, {c: r.p(NC), txt: signText(r, false), style: r.p(['box', 'tube', 'box', 'led']), rnd: r, Z: z}); meta.signs.push({...s, Z: z, k: 'roof'}); }
  }
  function bladeSigns(side, b, r) {
    const X = side * half, Zm = (b.Z0 + b.Z1) / 2, px = sc(Zm); if (px < .8) return;
    const mine = DRUMS.filter(d => d[1] === side && d[2] >= b.Z0 && d[2] < b.Z1), busy = DRUMS.filter(d => d[1] === side).map(drumRect);
    for (const d of mine) { const [x, y, w, h, Xc] = drumRect(d), p = sc(d[2]), wx = pr(X, 0, d[2])[0];
      g.strokeStyle = hz('#0a0716', d[2]); g.lineWidth = Math.max(.6, p * .18); g.beginPath(); g.moveTo(wx, y + p * .7); g.lineTo(x + (side < 0 ? 0 : w), y + p * .7); g.moveTo(wx, y + h - p * .7); g.lineTo(x + (side < 0 ? 0 : w), y + h - p * .7); g.stroke();
      const sg = sign(x, y, w, h, {c: d[8], txt: d[7], style: d[9], rnd: R(name + 'drum' + d[0]), Z: d[2] * .5, dim: .3, font: d[9] === 'led' ? '"DotGothic16"' : '"Dela Gothic One"'});
      meta.signs.push({...sg, Z: d[2], k: 'blade', X: Xc}); meta.drums.push({i: d[0], x, y, w, h, c: d[8]}); }
    const n = px > 6 ? r.i(2, 4) : r.i(1, 3);
    for (let k = 0; k < n; k++) { const z = lerp(b.Z0 + .4, b.Z1 - .4, r()), p = sc(z), w = r.r(1.5, 2.8), h = r.r(4.5, 10), out = r.r(.5, 3.2), yTop = Math.min(b.Hb - .5, r.r(7, 9 + Math.min(18, b.Hb * .6)) + h * .5);
      const x = side < 0 ? VP[0] + f * (X + out) / z : VP[0] + f * (X - out - w) / z, [, y] = pr(0, yTop, z);
      if (busy.some(q => x < q[0] + q[2] + 3 && x + w * p > q[0] - 3 && y < q[1] + q[3] + 3 && y + h * p > q[1] - 3)) continue;
      // arm back to the wall
      const wx = pr(X, 0, z)[0]; g.strokeStyle = hz('#0a0716', z); g.lineWidth = Math.max(.5, p * .16); g.beginPath(); g.moveTo(wx, y + p * .6); g.lineTo(x + (side < 0 ? 0 : w * p), y + p * .6); g.moveTo(wx, y + h * p - p * .6); g.lineTo(x + (side < 0 ? 0 : w * p), y + h * p - p * .6); g.stroke();
      const s = sign(x, y, w * p, h * p, {c: r.p(NC), txt: signText(r, true), style: r.p(['box', 'box', 'tube', 'tube', 'led']), rnd: r, Z: z}); meta.signs.push({...s, Z: z, k: 'blade', X: side < 0 ? X + out + w / 2 : X - out - w / 2}); }
    // air-con boxes and a pipe on nearer walls
    if (px > 5) { for (let k = 0; k < r.i(2, 5); k++) { const z = lerp(b.Z0, b.Z1, r()), yy = r.r(4, Math.min(30, b.Hb - 2)), [x, y] = pr(X - side * .35, yy, z), p = sc(z); g.fillStyle = hz('#2a2440', z); g.fillRect(x - p * .5, y - p * .7, p * 1, p * .7); g.strokeStyle = rgba('#000', .5); g.lineWidth = .5; g.strokeRect(x - p * .5, y - p * .7, p * 1, p * .7); g.beginPath(); g.arc(x, y - p * .35, p * .22, 0, TAU); g.stroke(); } }
  }

  // ---------------------------------------------------------------- the street
  function street() {
    const r = R(name + 'street'), Zn = Math.min(9.6, f * camH / (H - VP[1]) * .9), far = 2000;
    const quad = (X0, X1, Z0, Z1) => [pr(X0, 0, Z0), pr(X1, 0, Z0), pr(X1, 0, Z1), pr(X0, 0, Z1)];
    // asphalt, wet
    poly(g, quad(-half, half, Zn, far)); g.fillStyle = lin(g, 0, VP[1], 0, H, [[0, '#5a2a7a'], [.12, '#2a1650'], [.5, '#120b2a'], [1, '#080616']]); g.fill();
    g.save(); poly(g, quad(-half, half, Zn, far)); g.clip();
    texture(g, 'grain', 0, VP[1], W, H - VP[1], {alpha: .22, scale: 1.2});
    // sidewalks (paving) and kerbs
    for (const s of [-1, 1]) { poly(g, quad(s * 15, s * half, Zn, far)); g.fillStyle = lin(g, 0, VP[1], 0, H, [[0, '#6a3a86'], [.2, '#33205a'], [1, '#1a1434']]); g.fill();
      g.strokeStyle = rgba('#000', .25); g.lineWidth = .6; for (let z = 10; z < 160; z *= 1.09) { const a = pr(s * 15, 0, z), b = pr(s * half, 0, z); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
      for (const xx of [17.3, 19.6]) { const a = pr(s * xx, 0, Zn), b = pr(s * xx, 0, 400); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
      poly(g, quad(s * 15, s * 15.5, Zn, far)); g.fillStyle = rgba('#b9a6e8', .35); g.fill(); }
    // lane marks
    for (let z = 12; z < 500; z += 7) { poly(g, quad(-.18, .18, z, z + 3.6)); g.fillStyle = rgba('#ffe9a8', clamp(.75 - z / 500, .1, .7)); g.fill(); }
    for (const xx of [-7.5, 7.5]) for (let z = 12; z < 400; z += 5) { poly(g, quad(xx - .1, xx + .1, z, z + 2.2)); g.fillStyle = rgba('#e8e2ff', clamp(.45 - z / 600, .06, .45)); g.fill(); }
    // crossings
    for (const zc of [27, 96]) for (let xx = -13.6; xx < 13.6; xx += 1.7) { poly(g, quad(xx, xx + .95, zc, zc + 4.2)); g.fillStyle = rgba('#efe8ff', zc < 50 ? .5 : .32); g.fill(); }
    // light pooling on the wet road: every sign smears down toward us
    g.globalCompositeOperation = 'lighter';
    for (let i = 0; i < 26; i++) { const z = r.r(11, 130), x = r.r(-20, 20), [px0, py] = pr(x, 0, z); glow(g, px0, py, sc(z) * r.r(3, 8), r.p(NC), r.r(.05, .14)); }
    g.globalCompositeOperation = 'source-over';
    // puddles: mirror-bright blobs with ripples
    for (let i = 0; i < 16; i++) { const z = r.r(10.5, 80), x = r.r(-19, 19), [cx, cy] = pr(x, 0, z), p = sc(z), rx = p * r.r(.8, 2.6), ry = rx * .18 * (9 / camH);
      g.save(); g.beginPath(); g.ellipse(cx, cy, rx, ry, 0, 0, TAU); g.clip(); g.fillStyle = lin(g, cx - rx, 0, cx + rx, 0, [[0, rgba(r.p(NC), .45)], [.5, rgba('#fff', .2)], [1, rgba(r.p(NC), .45)]]); g.fillRect(cx - rx, cy - ry, rx * 2, ry * 2); g.restore();
      g.strokeStyle = rgba('#fff', .3); g.lineWidth = .5; g.beginPath(); g.ellipse(cx, cy, rx * .6, ry * .6, 0, 0, TAU); g.stroke(); meta.puddles = meta.puddles || []; meta.puddles.push([cx, cy, rx, ry]); }
    // manhole steam + far ground haze
    g.fillStyle = lin(g, 0, VP[1] - 4, 0, VP[1] + 60, [[0, rgba('#ff7ac8', .5)], [1, rgba('#ff7ac8', 0)]]); g.fillRect(0, VP[1] - 4, W, 64);
    g.restore();
  }
  function lampsAndProps() {
    const r = R(name + 'props');
    for (const s of [-1, 1]) for (let z = 16; z < 210; z += 31) { const [x, y] = pr(s * 15.9, 0, z), [, yt] = pr(0, 8.2, z), p = sc(z), c = z % 62 < 31 ? '#ffd9a0' : '#9fe9ff', dk = clamp(p / 14, .25, 1);
      g.strokeStyle = hz('#0c0918', z, .6); g.lineWidth = Math.max(.6, p * .16); g.lineCap = 'round'; g.beginPath(); g.moveTo(x, y); g.lineTo(x, yt); g.quadraticCurveTo(x, yt - p * .8, x - s * p * 1.6, yt - p * .8); g.stroke();
      const lx = x - s * p * 1.6, ly = yt - p * .7; g.fillStyle = lighten(c, .5); g.beginPath(); g.ellipse(lx, ly + p * .12, p * .5, p * .2, 0, 0, TAU); g.fill();
      glow(g, lx, ly, p * 4, c, .42 * dk); g.save(); g.globalCompositeOperation = 'lighter'; g.fillStyle = lin(g, 0, ly, 0, y, [[0, rgba(c, .16 * dk)], [1, rgba(c, 0)]]); g.beginPath(); g.moveTo(lx, ly); g.lineTo(lx - p * 3.4, y); g.lineTo(lx + p * 3.4, y); g.fill(); g.restore();
      glow(g, lx, y, p * 4, c, .14 * dk); meta.lamps.push([lx, ly, p]); }
    // vending machines against the walls
    for (const s of [-1, 1]) for (let k = 0; k < 7; k++) { const z = r.r(13, 150), [x, y] = pr(s * (half - .7), 0, z), p = sc(z), w = p * 1.15, h = p * 1.95, c = r.p(['#bff3ff', '#ffd1ee', '#fff1c9']);
      g.fillStyle = hz('#1a1630', z, .6); g.fillRect(x - w / 2 - p * .08, y - h - p * .08, w + p * .16, h + p * .08);
      g.fillStyle = lin(g, 0, y - h, 0, y, [[0, c], [.6, mix(c, NEON.cyan, .3)], [1, '#243a6a']]); g.fillRect(x - w / 2, y - h, w, h * .92);
      if (p > 5) { for (let a = 0; a < 3; a++) for (let b2 = 0; b2 < 5; b2++) { g.fillStyle = r.p([NEON.red, NEON.grn, NEON.yel, NEON.blue, NEON.mag]); g.fillRect(x - w / 2 + w * (.08 + b2 * .18), y - h + h * (.08 + a * .17), w * .12, h * .1); } g.fillStyle = '#10162c'; g.fillRect(x - w * .4, y - h * .28, w * .8, h * .16); }
      glow(g, x, y - h * .5, p * 4, c, .35); glow(g, x - s * p, y, p * 3, c, .25); }
  }
  // strings of paper lanterns and power lines across the avenue
  function strings() {
    const r = R(name + 'strings');
    for (const [z, yh, n, sag] of [[230, 21, 9, 3], [150, 19, 9, 3.4], [104, 18, 8, 3.6], [78, 20, 7, 4], [52, 23, 6, 5], [34, 26, 5, 6]]) {
      const a = pr(-half, yh, z), b = pr(half, yh + r.g() * 2, z), p = sc(z), pt = t => [lerp(a[0], b[0], t), lerp(a[1], b[1], t) + Math.sin(t * Math.PI) * sag * p];
      g.strokeStyle = rgba('#07040f', .9); g.lineWidth = Math.max(.5, p * .06); g.beginPath(); for (let t = 0; t <= 1.001; t += .04) { const q = pt(t); t ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1]); } g.stroke();
      for (let k = 1; k <= n; k++) { const [x, y] = pt(k / (n + 1)), lw = p * .62, lh = p * .82, red = k % 2, c = red ? '#ff3b4a' : '#ffb13a';
        g.fillStyle = '#07040f'; g.fillRect(x - lw * .25, y, lw * .5, lh * .2);
        g.fillStyle = rad(g, x, y + lh * .7, 0, lh * .7, [[0, lighten(c, .55)], [.6, c], [1, darken(c, .35)]]); g.beginPath(); g.ellipse(x, y + lh * .7, lw / 2, lh / 2, 0, 0, TAU); g.fill();
        if (p > 2.6) { g.strokeStyle = rgba('#5a0a12', .5); g.lineWidth = .4; for (const q of [-.25, 0, .25]) { g.beginPath(); g.ellipse(x, y + lh * .7, lw / 2 * Math.abs(q * 2 + .001) + .2, lh / 2, 0, 0, TAU); g.stroke(); }
          g.fillStyle = rgba('#3a0508', .85); g.font = `900 ${lh * .5}px "Zen Maru Gothic"`; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText('ゴーストブロックビート'[(k + Math.round(z)) % 11], x, y + lh * .72); }
        g.fillStyle = '#07040f'; g.fillRect(x - lw * .22, y + lh * 1.18, lw * .44, lh * .12);
        glow(g, x, y + lh * .7, p * 2.6, c, .4); meta.lamps.push([x, y + lh * .7, p]); }
    }
    for (let i = 0; i < 9; i++) { const z = r.r(20, 260), y0 = r.r(14, 30), a = pr(-half, y0, z), b = pr(half, y0 + r.g() * 4, z + r.g() * 20), sag = r.r(1, 4) * sc(z);
      g.strokeStyle = rgba('#06030d', .85); g.lineWidth = Math.max(.4, sc(z) * .04); g.beginPath(); g.moveTo(a[0], a[1]); g.quadraticCurveTo((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + sag * 2, b[0], b[1]); g.stroke(); }
  }

  // ---------------------------------------------------------------- the far end: GhostBlock tower, a torii across the avenue
  function endTower() {
    const r = R(name + 'tower'), x = VP[0], by = VP[1] + 6;
    const tw = (cx, w, h, c, crown) => { const y = by - h; g.fillStyle = lin(g, 0, y, 0, by, [[0, c], [1, mix(c, HAZE2, .6)]]);
      g.beginPath(); g.moveTo(cx - w / 2, by); g.lineTo(cx - w / 2, y + h * .22); g.lineTo(cx - w * .36, y + h * .2); g.lineTo(cx - w * .36, y + h * .08); g.lineTo(cx - w * .2, y + h * .06); g.lineTo(cx - w * .2, y); g.lineTo(cx + w * .2, y); g.lineTo(cx + w * .2, y + h * .06); g.lineTo(cx + w * .36, y + h * .08); g.lineTo(cx + w * .36, y + h * .2); g.lineTo(cx + w / 2, y + h * .22); g.lineTo(cx + w / 2, by); g.fill();
      for (let yy = y + h * .1; yy < by - 3; yy += 3.1) for (let xx = cx - w / 2 + 1.5; xx < cx + w / 2 - 2; xx += 2.6) { const inset = yy < y + h * .22 ? w * .16 : 0; if (xx < cx - w / 2 + inset || xx > cx + w / 2 - inset - 1.5) continue; if (r.c(.42)) { g.fillStyle = rgba(r.p(['#ffd9a0', '#ff9ad8', '#9be8ff', '#fff3d6']), r.r(.4, .95)); g.fillRect(xx, yy, 1.5, 1.7); } }
      if (crown) { g.strokeStyle = c; g.lineWidth = 1.2; g.beginPath(); g.moveTo(cx, y); g.lineTo(cx, y - h * .22); g.stroke(); g.fillStyle = '#ff3d55'; g.beginPath(); g.arc(cx, y - h * .22, 1.4, 0, TAU); g.fill(); glow(g, cx, y - h * .22, 9, '#ff3d55', .6); meta.lamps.push([cx, y - h * .22, 2]); } };
    tw(x - 118, 36, 150, '#3a1a70', true); tw(x + 122, 32, 132, '#34186a', true); tw(x - 168, 30, 104, '#2c1560', false); tw(x + 172, 28, 96, '#2c1560', true);
    // the stage the ghost drummer is projected from: a ring of emitters on a low hall, beams fanning up
    const hh = cfg.holoH, hw = hh * .5; g.fillStyle = lin(g, 0, by - 26, 0, by, [[0, '#2a1560'], [1, '#5a2a86']]); g.beginPath(); g.moveTo(x - hw * .9, by); g.quadraticCurveTo(x - hw * .8, by - 24, x, by - 27); g.quadraticCurveTo(x + hw * .8, by - 24, x + hw * .9, by); g.fill();
    for (let i = -6; i <= 6; i++) { g.fillStyle = rgba('#9be8ff', .8); g.fillRect(x + i * hw * .12 - .6, by - 12 - Math.cos(i / 6 * 1.4) * 10, 1.2, 1.6); }
    g.save(); g.globalCompositeOperation = 'lighter'; g.fillStyle = lin(g, 0, by - 20, 0, by - hh * 1.05, [[0, rgba('#5ff0ff', .26)], [.5, rgba('#5ff0ff', .08)], [1, rgba('#5ff0ff', 0)]]); g.beginPath(); g.moveTo(x - hw * .5, by - 22); g.lineTo(x - hw * 1.15, by - hh * 1.05); g.lineTo(x + hw * 1.15, by - hh * 1.05); g.lineTo(x + hw * .5, by - 22); g.fill(); g.restore();
    neon(g, gg => { gg.beginPath(); gg.ellipse(x, by - 22, hw * .62, hw * .1, 0, 0, TAU); }, NEON.cyan, .9, {S, halo: 1.3});
    meta.holo = [x, by - 22 - hh * .52, hh];
    // torii of light across the far end of the avenue
    const zt = 380, p = sc(zt), a = pr(-13, 0, zt), b = pr(13, 0, zt), top = pr(0, 15, zt)[1], c = '#ff4a5a';
    g.save(); g.lineCap = 'butt'; g.strokeStyle = '#7a1420'; g.lineWidth = p * 1.5; g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(a[0] + p, top); g.moveTo(b[0], b[1]); g.lineTo(b[0] - p, top); g.stroke();
    g.lineWidth = p * 1.6; g.beginPath(); g.moveTo(a[0] - p * 3, top); g.quadraticCurveTo(x, top + p * 1.2, b[0] + p * 3, top); g.stroke(); g.lineWidth = p * 1.1; g.beginPath(); g.moveTo(a[0] - p, top + p * 3.2); g.lineTo(b[0] + p, top + p * 3.2); g.stroke(); g.restore();
    neon(g, gg => { gg.beginPath(); gg.moveTo(a[0] - p * 3, top - p * .9); gg.quadraticCurveTo(x, top + p * .3, b[0] + p * 3, top - p * .9); }, c, 1, {S, halo: 1.2});
    glow(g, x, top + p * 6, p * 22, '#ff6a7a', .32);
  }

  // ---------------------------------------------------------------- wet-road reflections: every light mirrored about its own ground line, smeared and rippled
  function reflections() {
    const RL = layer(W, H, S), q = RL.g, r = R(name + 'refl'), yg = Z => VP[1] + f * camH / Z;
    q.globalCompositeOperation = 'lighter';
    for (const s of meta.signs) { const g0 = yg(s.Z), top = 2 * g0 - (s.y + s.h), len = s.h * 1.9 + 14;
      q.fillStyle = lin(q, 0, top, 0, top + len, [[0, rgba(s.c, .62)], [.45, rgba(s.c, .3)], [1, rgba(s.c, 0)]]); q.fillRect(s.x - s.w * .1, top, s.w * 1.2, len);
      q.fillStyle = lin(q, 0, top, 0, top + len * .6, [[0, rgba(lighten(s.c, .6), .35)], [1, rgba(s.c, 0)]]); q.fillRect(s.x + s.w * .3, top, s.w * .4, len * .6); }
    for (const [x, y, p, c] of meta.lamps) { if (!p) continue; const Z = f / p, g0 = yg(Z), ry = 2 * g0 - y; q.fillStyle = lin(q, 0, ry - p, 0, ry + p * 9, [[0, rgba(c || '#ffd9a0', .5)], [1, rgba(c || '#ffd9a0', 0)]]); q.fillRect(x - p * .7, ry - p, p * 1.4, p * 10); }
    // the sky and the far city, flipped about the horizon (true for far things)
    q.save(); q.globalAlpha = .5; q.setTransform(S, 0, 0, -S * 1.5, 0, (VP[1] + VP[1] * 1.5 + 4) * S); q.drawImage(L.c, 0, 0, W * S, VP[1] * S, 0, 0, W, VP[1]); q.restore();
    // smear downward (toward us), blur, then lay it on the road in rippled strips
    const sm = layer(W, H, S); sm.g.globalCompositeOperation = 'lighter'; for (let k = 0; k < 9; k++) { sm.g.globalAlpha = .2 * (1 - k / 11); sm.g.drawImage(RL.c, 0, k * 5, W, H); }
    const bl = PT.blurred(sm.c, 2.2 * S), zn = Math.min(9.4, f * camH / (H - VP[1]) * .88), road = [pr(-half, 0, zn), pr(half, 0, zn), pr(half, 0, 3000), pr(-half, 0, 3000)];
    g.save(); poly(g, road); g.clip(); g.globalCompositeOperation = 'lighter'; g.globalAlpha = .9;
    for (let y = Math.floor(VP[1]); y < H; y += 1.5) { const t = (y - VP[1]) / (H - VP[1]), amp = .4 + t * 5, ox = Math.sin(y * .61 + Math.sin(y * .13) * 3) * amp + (nz.n2(y * .21, 3.3) - .5) * amp;
      g.drawImage(bl, 0, y * S, W * S, 1.5 * S, ox, y, W, 1.5); }
    g.restore();
  }

  // ---------------------------------------------------------------- the haunting: GhostBlock ghosts all over town
  const ghostAt = (tag, cx, cy, size, o = {}) => { const im = cfg.ghosts[tag]; if (!im) return; g.save(); g.translate(cx, cy); g.rotate(o.rot || 0); if (o.flip) g.scale(-1, 1);
    if (o.glow !== 0) { g.shadowColor = rgba(o.gc || '#b7ffe0', .9); g.shadowBlur = size * .35 * S; } g.globalAlpha = o.a ?? 1; g.drawImage(im, -size / 2, -size / 2, size, size); g.shadowBlur = 0;
    if (o.haze) { g.globalCompositeOperation = 'source-atop'; g.fillStyle = rgba(HAZE, o.haze); g.fillRect(-size / 2, -size / 2, size, size); } g.restore(); };
  // a ghost bent out of neon tube
  const ghostNeon = (cx, cy, size, c, eyes = true) => { const k = size / 40; g.save(); g.translate(cx - 32 * k, cy - 33 * k); g.scale(k, k);
    const P2 = new Path2D(GBODY); g.lineJoin = 'round'; g.globalCompositeOperation = 'lighter'; g.shadowColor = c; g.strokeStyle = rgba(c, .4); g.lineWidth = 3.4; g.shadowBlur = 14 * k * S; g.stroke(P2); g.shadowBlur = 6 * k * S; g.stroke(P2);
    g.globalCompositeOperation = 'source-over'; g.shadowBlur = 3 * k * S; g.strokeStyle = c; g.lineWidth = 2.1; g.stroke(P2); g.shadowBlur = 0; g.strokeStyle = rgba(lighten(c, .8), .9); g.lineWidth = .8; g.stroke(P2);
    if (eyes) { g.fillStyle = lighten(c, .7); g.shadowColor = c; g.shadowBlur = 5 * k * S; g.beginPath(); g.ellipse(26.5, 31, 2.3, 3.2, 0, 0, TAU); g.ellipse(37.5, 31, 2.3, 3.2, 0, 0, TAU); g.fill(); } g.restore(); };
  function haunt() {
    const r = R(name + 'haunt'), tags = Object.keys(cfg.ghosts);
    // ghosts drifting above the avenue, each with a fading wake
    for (const [X, Y, Z, tag, rot] of cfg.fly) { const [x, y] = pr(X, Y, Z), sz = sc(Z) * 3.4;
      for (let k = 5; k > 0; k--) ghostAt(tag, x + k * sz * .34 * (X < 0 ? -1 : 1), y + k * sz * .09, sz * (1 - k * .1), {a: .07 * (6 - k) / 2, rot, flip: X > 0, glow: 0});
      ghostAt(tag, x, y, sz, {rot, flip: X > 0, haze: clamp(Z / 900, 0, .35)}); glow(g, x, y, sz * 1.5, '#aaffdd', .16); }
    // will-o'-wisps (hitodama): little cold flames along the street
    for (let i = 0; i < 30; i++) { const z = r.r(24, 240), X = r.r(-20, 20), Y = r.r(1.5, 13), [x, y] = pr(X, Y, z), p = sc(z) * r.r(.35, .7), c = r.p(['#7dffd8', '#8fd0ff', '#b9ffa0']);
      g.save(); g.globalCompositeOperation = 'lighter'; g.fillStyle = rad(g, x, y, 0, p * 1.3, [[0, '#fff'], [.35, c], [1, rgba(c, 0)]]); g.beginPath(); g.moveTo(x - p * .8, y + p * .3); g.quadraticCurveTo(x - p * 1.2, y - p * .8, x + p * .2, y - p * 2.4); g.quadraticCurveTo(x + p * 1.3, y - p * .4, x + p * .7, y + p * .5); g.quadraticCurveTo(x, y + p * 1.2, x - p * .8, y + p * .3); g.fill(); g.restore(); glow(g, x, y, p * 4, c, .28); meta.wisps = meta.wisps || []; meta.wisps.push([x, y, p]); }
    // green ectoplasm creeping along the kerbs
    for (let i = 0; i < 70; i++) { const z = r.r(12, 200), s = r.p([-1, 1]), [x, y] = pr(s * r.r(13, 21), r.r(0, 1.2), z), p = sc(z); g.save(); g.globalCompositeOperation = 'lighter'; g.globalAlpha = r.r(.05, .16); g.fillStyle = r.p(['#3dffb0', '#6affc8', '#39d0ff']); g.beginPath(); g.ellipse(x, y, p * r.r(2, 6), p * r.r(.4, 1.1), 0, 0, TAU); g.fill(); g.restore(); }
    // pedestrians: little ghosts under umbrellas
    for (let i = 0; i < 26; i++) { const z = r.r(14, 150), s = r.p([-1, 1]), X = r.c(.2) ? r.r(-12, 12) : s * r.r(16, 20.5), [x, y] = pr(X, 0, z), p = sc(z), sz = p * 1.9, tag = r.p(tags), uc = r.p([NEON.red, NEON.yel, NEON.cyan, '#f3efff', NEON.mag, NEON.grn]);
      g.fillStyle = rgba('#05030c', .45); g.beginPath(); g.ellipse(x, y, sz * .32, sz * .07, 0, 0, TAU); g.fill();
      ghostAt(tag, x, y - sz * .62, sz, {flip: r.c(.5), gc: '#ffffff', haze: clamp(z / 500, 0, .3)});
      g.strokeStyle = '#0a0716'; g.lineWidth = Math.max(.5, p * .07); g.beginPath(); g.moveTo(x + sz * .3, y - sz * .5); g.lineTo(x + sz * .3, y - sz * 1.42); g.stroke();
      g.fillStyle = uc; g.beginPath(); g.moveTo(x + sz * .3 - sz * .62, y - sz * 1.3); g.quadraticCurveTo(x + sz * .3, y - sz * 1.95, x + sz * .3 + sz * .62, y - sz * 1.3); g.quadraticCurveTo(x + sz * .3, y - sz * 1.42, x + sz * .3 - sz * .62, y - sz * 1.3); g.fill();
      g.strokeStyle = rgba('#07040f', .6); g.lineWidth = Math.max(.3, p * .04); g.stroke(); glow(g, x, y - sz * 1.4, sz, uc, .1); }
    // a blimp with a running sign
    if (cfg.blimp) { const [bx, by, bw] = cfg.blimp, bh = bw * .3; g.save();
      g.fillStyle = lin(g, 0, by - bh / 2, 0, by + bh / 2, [[0, '#5a3a9a'], [.5, '#2a1858'], [1, '#150b33']]); g.beginPath(); g.ellipse(bx, by, bw / 2, bh / 2, 0, 0, TAU); g.fill();
      g.fillStyle = '#1c1040'; g.beginPath(); g.moveTo(bx + bw * .4, by); g.lineTo(bx + bw * .56, by - bh * .55); g.lineTo(bx + bw * .5, by); g.lineTo(bx + bw * .56, by + bh * .55); g.fill(); g.fillRect(bx - bw * .1, by + bh * .42, bw * .2, bh * .2);
      g.fillStyle = '#07040f'; g.fillRect(bx - bw * .3, by - bh * .2, bw * .56, bh * .4); neonText(g, 'GHOSTBLOCK', bx - bw * .02, by + bh * .02, `400 ${bh * .3}px "DotGothic16"`, NEON.grn, {S, size: bh * .3});
      g.strokeStyle = rgba('#b9a0ff', .35); g.lineWidth = .5; for (const t of [-.3, 0, .3]) { g.beginPath(); g.ellipse(bx, by, bw / 2, bh / 2 * Math.abs(t * 2 + .2), 0, Math.PI, TAU); g.stroke(); }
      g.fillStyle = '#ff3d55'; g.beginPath(); g.arc(bx - bw / 2, by, 1.3, 0, TAU); g.fill(); glow(g, bx - bw / 2, by, 8, '#ff3d55', .6); glow(g, bx, by + bh, bw * .5, NEON.grn, .12); g.restore(); meta.blimp = [bx, by, bw]; }
  }

  // ---------------------------------------------------------------- compose
  sky(); moon(cfg.moon[0], cfg.moon[1], cfg.moon[2]); skyline();
  // the far end of the avenue: a plaza of light
  glow(g, VP[0], VP[1] + 4, 120, '#ffc0ea', .28); endTower();
  wall(-1); wall(1);
  street(); reflections(); lampsAndProps(); haunt(); strings();
  { const cells = meta.drums.map(d => { const m = Math.max(d.w, d.h) * .55; return {d, m, cw: d.w + m * 2, ch: d.h + m * 2}; }); let ax = 0; const ah = Math.max(...cells.map(c => c.ch));
    const A = layer(cells.reduce((a, c) => a + c.cw + 2, 0), ah, S); A.g.fillStyle = '#000'; A.g.fillRect(0, 0, A.W, A.H); const keep = g; g = A.g;
    for (const c of cells) { const d = c.d, D = DRUMS.find(q => q[0] === d.i), x = ax + c.m, y = c.m; g.save(); g.beginPath(); g.rect(ax, 0, c.cw, c.ch); g.clip();
      g.save(); g.translate(x + d.w / 2, y + d.h / 2); g.scale(c.cw / c.ch, 1); glow(g, 0, 0, c.ch / 2, d.c, .55); g.restore(); sign(x, y, d.w, d.h, {c: d.c, txt: D[7], style: D[9], rnd: R(name + 'drum' + d.i), Z: 20, lit: 1.6, noHalo: true, font: D[9] === 'led' ? '"DotGothic16"' : '"Dela Gothic One"'});
      g.restore(); d.a = [ax, 0, c.cw, c.ch]; d.t = [d.x - c.m, d.y - c.m, c.cw, c.ch]; ax += c.cw + 2; }
    g = keep; window.atlas = A; meta.drums = meta.drums.map(d => [d.i, ...d.a, ...d.t]); }
  window.meta = meta;
  return L;
};
window.main = async (a) => {
  await loadFonts('ゴーストブロックステップドラムビートリズムブレイク幽霊太鼓音楽ホテルラーメン酒薬カラオケ喫茶レコード電気夜雨月営業中24時間ネオンお化け鼓動ベース麺湯本花夢キックスネアクラップハットオープンパーカスリムクラッシュサブピアノアシッドABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789');
  const S = a.S || 2, cfg = a.portrait ? {W: 390, H: 844, S, VP: [195, 356], f: 520, hero: [16, 200, 358, 150], moon: [300, 120, 32], name: 'cityP', holoH: 132, blimp: [92, 132, 58], fly: [[-9, 30, 120, 'F6X', -.2], [11, 24, 170, 'AxF', .15], [-4, 44, 260, '8Hu', -.1]]} : {W: 1370, H: 835, S, VP: [812, 215], f: 700, hero: [611, 10, 403, 219], moon: [905, 62, 40], name: 'city', holoH: 168, blimp: [712, 52, 74], fly: [[-9, 30, 120, 'F6X', -.2], [11, 24, 170, 'AxF', .15], [-4, 44, 260, '8Hu', -.1], [15, 33, 95, 'J3E', .2], [-16, 21, 78, 'BDs', -.25]]};
  cfg.ghosts = {}; for (const t of GH.GH_TAGS) cfg.ghosts[t] = await ghostImg(t);
  const L = CITY(cfg);
  PT.bloom(L, {r: 10, a: .35, thresh: 1}); PT.vignette(L, .5);
  PT.texture(L.g, 'grain', 0, 0, L.W, L.H, {alpha: .07, scale: 1 / S});
  out[a.portrait ? 'city-p' : 'city'] = L.c; out[a.portrait ? 'add-cityPdrums' : 'add-cityLdrums'] = window.atlas.c;
};
