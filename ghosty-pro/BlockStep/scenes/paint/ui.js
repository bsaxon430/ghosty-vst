// The controls of GHOST CITY as painted pieces (owner 2026-11-13: "hand drawn ... insane glow for neon ... 3-D looking
// see-through"). Every panel frame and every key is a small painting, drawn by an unsteady hand, that the page lays
// over its boxes as a 9-slice picture (corners keep their shape, sides stretch or repeat), so no control moves.
//   node paint/render.js paint/ui.js      -> out/ui-*.png (2x) + out/ui.json + out/ui-sheet.png (a preview)
window.main = async () => {
  const {mk, mix, R} = PT, TAU = Math.PI * 2;
  window.out = window.out || {}; const meta = window.meta = {ui: {}};
  const rgbaOf = (c, a) => { const n = parseInt(c.slice(1), 16); return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`; };

  // ---- an outline drawn by hand. Each side carries one slow wave across its middle (it starts and ends level, so
  // the middle can stretch or repeat without a seam) and a quicker wobble that lives only near the corners.
  function hand(x, y, w, h, r, o = {}) {
    const {amp = 1.4, cz = 40, W = 0, H = 0, seed = 'h', step = 2} = o, rnd = R(seed), p = [rnd() * TAU, rnd() * TAU, rnd() * TAU, rnd() * TAU], sg = [rnd() < .5 ? -1 : 1, rnd() < .5 ? -1 : 1, rnd() < .5 ? -1 : 1, rnd() < .5 ? -1 : 1];
    const pts = [], arcs = [[], [], [], []]; let d = 0;
    const quick = () => Math.sin(d * .105 + p[0]) * .55 + Math.sin(d * .23 + p[1]) * .3 + Math.sin(d * .041 + p[2]) * .4;
    const side = (x0, y0, x1, y1, nx, ny, k) => { const L = Math.hypot(x1 - x0, y1 - y0), n = Math.max(2, Math.round(L / step)), horiz = ny !== 0, T = horiz ? W : H;
      for (let i = 0; i <= n; i++) { const t = i / n, px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t, q = horiz ? px : py, band = T - 2 * cz;
        const slow = band > 4 ? Math.sin(TAU * (q - cz) / band) * sg[k] * .55 : 0, edge = horiz ? x + r : y + r;
        const win = q < cz ? Math.min(1, (cz - q) / Math.max(1, cz - edge)) : q > T - cz ? Math.min(1, (q - (T - cz)) / Math.max(1, cz - edge)) : 0;
        const off = amp * (slow + quick() * win); pts.push([px + nx * off, py + ny * off]); d += L / n; } };
    const arc = (cx, cy, a0, k) => { const n = Math.max(4, Math.round(r * 1.57 / step));
      for (let i = 1; i < n; i++) { const a = a0 + (i / n) * Math.PI / 2, off = amp * quick(); const pt = [cx + Math.cos(a) * (r + off), cy + Math.sin(a) * (r + off)]; pts.push(pt); arcs[k].push(pt); d += r * 1.57 / n; } };
    // start in the middle of the top side (the wobble is still there, so the loop closes cleanly)
    side(x + w / 2, y, x + w - r, y, 0, -1, 0); arc(x + w - r, y + r, -Math.PI / 2, 0); side(x + w, y + r, x + w, y + h - r, 1, 0, 1); arc(x + w - r, y + h - r, 0, 1);
    side(x + w - r, y + h, x + r, y + h, 0, 1, 2); arc(x + r, y + h - r, Math.PI / 2, 2); side(x, y + h - r, x, y + r, -1, 0, 3); arc(x + r, y + r, Math.PI, 3); side(x + r, y, x + w / 2, y, 0, -1, 0);
    pts.arcs = arcs; return pts; }
  const path = (g, pts, close = true) => { g.beginPath(); pts.forEach(([px, py], i) => i ? g.lineTo(px, py) : g.moveTo(px, py)); if (close) g.closePath(); };
  const stroke = (g, pts, col, lw, blur = 0, a = 1, comp = 'source-over', close = true) => { g.save(); g.globalAlpha = a; g.globalCompositeOperation = comp; if (blur) g.filter = `blur(${blur}px)`; g.strokeStyle = col; g.lineWidth = lw; g.lineJoin = g.lineCap = 'round'; path(g, pts, close); g.stroke(); g.restore(); };
  const fill = (g, pts, style, blur = 0, a = 1, comp = 'source-over') => { g.save(); g.globalAlpha = a; g.globalCompositeOperation = comp; if (blur) g.filter = `blur(${blur}px)`; g.fillStyle = style; path(g, pts); g.fill(); g.restore(); };
  // a glass tube with gas in it: a wide soft halo, a tight halo, the coloured tube, a hot white core
  const tube = (g, pts, col, lw, close = true, k = 1) => { stroke(g, pts, col, lw * 3.4, lw * 5, .5 * k, 'lighter', close); stroke(g, pts, col, lw * 1.9, lw * 1.7, .85 * k, 'lighter', close); stroke(g, pts, col, lw, 0, 1, 'source-over', close); stroke(g, pts, mix(col, '#ffffff', .8), lw * .42, 0, .95, 'source-over', close); };
  const dot = (g, x, y, r, col) => { g.save(); g.globalCompositeOperation = 'lighter'; g.filter = `blur(${r * 1.6}px)`; g.fillStyle = col; g.beginPath(); g.arc(x, y, r * 1.8, 0, TAU); g.fill(); g.restore(); g.fillStyle = col; g.beginPath(); g.arc(x, y, r, 0, TAU); g.fill(); g.fillStyle = 'rgba(255,255,255,.85)'; g.beginPath(); g.arc(x - r * .25, y - r * .25, r * .4, 0, TAU); g.fill(); };

  // ---- a panel: a sheet of smoked glass you can see the street through, in a hand-bent neon tube frame
  function panel(name, tubeC, accC, o = {}) {
    const W = o.W || 352, H = o.H || W, M = o.M ?? 28, CZ = o.CZ || 104, r = o.r || 30, k = o.k || 1, {c, g} = mk(W, H), x = M, y = M, w = W - 2 * M, h = H - 2 * M;
    const body = hand(x, y, w, h, r, {amp: 1.2 * k, cz: CZ, W, H, seed: name + 'b'});
    const gr = g.createLinearGradient(0, y, 0, y + h), e0 = (CZ - M) / h; gr.addColorStop(0, o.top || 'rgba(54,40,128,.34)'); gr.addColorStop(e0, o.mid || 'rgba(18,13,50,.40)'); gr.addColorStop(1 - e0, o.mid || 'rgba(18,13,50,.40)'); gr.addColorStop(1, o.bot || 'rgba(6,4,20,.56)'); fill(g, body, gr);   // (the colour only changes inside the corner zones, so the sides repeat without a seam)
    g.save(); path(g, body); g.clip();
    g.save(); g.beginPath(); g.rect(0, 0, CZ, CZ); g.clip(); const sh = g.createLinearGradient(x, y, x + 64 * k, y + 64 * k); sh.addColorStop(0, 'rgba(210,244,255,.30)'); sh.addColorStop(.55, 'rgba(210,244,255,.07)'); sh.addColorStop(1, 'rgba(210,244,255,0)');   // the light catching the top-left corner of the glass
    g.fillStyle = sh; g.beginPath(); g.moveTo(x, y); g.lineTo(x + 92 * k, y); g.lineTo(x, y + 92 * k); g.fill(); g.restore();
    g.fillStyle = 'rgba(200,240,255,.22)'; g.fillRect(0, y + 8 * k, W, 2); g.fillStyle = rgbaOf(accC, .10); g.fillRect(0, y + h - 26 * k, W, 26 * k); g.fillStyle = 'rgba(0,0,0,.34)'; g.fillRect(0, y + h - 10 * k, W, 4);   // the pane's bevel: light along the top, the neon's colour pooled along the bottom
    g.restore();
    stroke(g, body, '#04030d', 9.5 * (k < 1 ? .8 : 1), 0, .92);
    const t = hand(x, y, w, h, r, {amp: 1.7 * k, cz: CZ, W, H, seed: name + 't'}); tube(g, t, tubeC, 3.3 * (k < 1 ? .85 : 1));
    const inner = hand(x + 12 * k, y + 12 * k, w - 24 * k, h - 24 * k, Math.max(4, r - 8 * k), {amp: 1.5 * k, cz: CZ, W, H, seed: name + 'i'}); stroke(g, inner, accC, 3.6, 4, .55, 'lighter'); stroke(g, inner, accC, 1.5, 0, .92);
    t.arcs.forEach((a, q) => { if (q % 2 === 0) tube(g, a, mix(tubeC, '#ffffff', .25), 5 * (k < 1 ? .8 : 1), false, .8); else tube(g, a, accC, 4.2 * (k < 1 ? .8 : 1), false, .8); });   // heavier bends at the corners, alternating colours
    if (!o.plain) { dot(g, W - CZ + 16, y + h - 18, 3, '#2bff9a'); dot(g, W - CZ + 30, y + h - 18, 3, '#ffc93c'); dot(g, W - CZ + 44, y + h - 18, 3, accC);                    // three little status lamps, bottom right
      g.strokeStyle = rgbaOf(tubeC, .8); g.lineWidth = 1.6; for (let i = 0; i < 6; i++) { g.beginPath(); g.moveTo(x + 40 + i * 7, y + 15); g.lineTo(x + 40 + i * 7, y + (i % 5 ? 20 : 24)); g.stroke(); } }   // a row of ruler ticks, top left
    out['ui-' + name] = c; meta.ui[name] = {w: W, h: H, cz: CZ, m: M};
  }
  // ---- a key: a thick little block of glass. Dark with a tube round it when off, a lit gel when on
  const rrect = (g, x, y, w, h, r) => { r = Math.min(r, w / 2, h / 2); g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); };
  function key(name, o) {
    const W = 148, H = 100, M = 12, CZ = 36, r = 21, E = 7, {c, g} = mk(W, H), x = M, y = M, w = W - 2 * M, h = H - 2 * M - E;
    const all = hand(x, y, w, h + E, r, {amp: 1, cz: CZ, W, H, seed: name + 'a'}), face = hand(x, y, w, h, r, {amp: 1, cz: CZ, W, H, seed: name + 'a'});
    if (o.lit) { fill(g, all, o.lit, 9, .8, 'lighter'); fill(g, all, o.lit, 4, .45, 'lighter'); }           // light spilling round a lit key
    fill(g, all, o.side); if (o.clear) fill(g, face, '#000', 0, 1, 'destination-out');   // (a clear key: the page's own colour shows through its face)
    let gr; if (o.across) { gr = g.createLinearGradient(x, 0, x + w, 0); o.across.forEach(([t, col]) => gr.addColorStop(t, col)); } else { gr = g.createLinearGradient(0, y, 0, y + h); o.face.forEach(([t, col]) => gr.addColorStop(t, col)); }
    fill(g, face, gr);
    g.save(); path(g, face); g.clip();
    if (o.across) { const dk = g.createLinearGradient(0, y, 0, y + h); dk.addColorStop(0, 'rgba(255,255,255,.22)'); dk.addColorStop(.5, 'rgba(0,0,0,0)'); dk.addColorStop(1, 'rgba(6,2,20,.5)'); g.fillStyle = dk; g.fillRect(0, 0, W, H); }
    const gl = g.createLinearGradient(0, y + 3, 0, y + h * .56); gl.addColorStop(0, `rgba(255,255,255,${o.gloss})`); gl.addColorStop(1, 'rgba(255,255,255,0)'); g.fillStyle = gl; rrect(g, x + 5, y + 4, w - 10, h * .52, r - 6); g.fill();   // the shine on its upper half
    g.fillStyle = 'rgba(0,0,0,.26)'; g.fillRect(0, y + h - 6, W, 6);
    g.restore();
    stroke(g, all, '#04030d', 3.6, 0, .95);
    stroke(g, face, o.rim, 5, 3.2, o.lit ? .8 : .5, 'lighter'); stroke(g, face, o.rim, 2.1, 0, 1); if (o.lit) stroke(g, face, '#ffffff', .9, 0, .7);
    out['ui-' + name] = c; meta.ui[name] = {w: W, h: H, cz: CZ, m: M};
  }
  // ---- GHOST CITY's set
  const COL = {cy: '#27e4ff', mg: '#ff2fa8', gn: '#22ff9c', am: '#ffc93c', vi: '#a35cff', rd: '#ff4466', or: '#ff9a3c', tl: '#2ee6c8', bl: '#4f8dff', pk: '#ff7ac8'};
  const DARK = [[0, 'rgba(60,50,130,.84)'], [.5, 'rgba(30,24,78,.88)'], [1, 'rgba(13,10,36,.93)']], SIDE = 'rgba(3,2,10,.92)';
  panel('c-panel', COL.cy, COL.mg); panel('c-panel2', COL.mg, COL.cy, {top: 'rgba(120,40,130,.30)'}); panel('c-frame', COL.mg, COL.cy, {top: 'rgba(20,14,56,.10)', mid: 'rgba(10,7,30,.10)', bot: 'rgba(6,4,20,.16)'});
  panel('c-bar', COL.cy, COL.mg, {W: 300, H: 124, M: 20, CZ: 52, r: 20, k: .6, plain: true});   // a low frame for the tool rail and the layer switches
  key('c-off', {face: DARK, side: SIDE, rim: '#93a8ff', gloss: .26});
  key('c-hov', {face: [[0, 'rgba(84,72,170,.9)'], [.5, 'rgba(44,36,104,.92)'], [1, 'rgba(18,14,48,.95)']], side: SIDE, rim: '#d6e2ff', gloss: .34});
  for (const [k, col] of Object.entries(COL)) {
    key('c-rim-' + k, {face: [[0, mix('#3c3282', col, .22)], [.5, mix('#1e184e', col, .12)], [1, '#0d0a24']].map(([t, c2]) => [t, rgbaOf(c2, .88)]), side: SIDE, rim: col, gloss: .26});
    key('c-on-' + k, {face: [[0, mix(col, '#ffffff', .62)], [.42, col], [1, mix(col, '#0a0618', .5)]], side: mix(col, '#05030c', .66), rim: mix(col, '#ffffff', .55), gloss: .72, lit: col});
  }
  key('c-gloss', {face: [[0, 'rgba(255,255,255,.34)'], [.42, 'rgba(255,255,255,0)'], [1, 'rgba(6,2,20,.42)']], side: 'rgba(4,2,12,.5)', rim: '#ffffff', gloss: .55, clear: true, lit: null});   // laid over a key the page colours itself (each drum's own colour)
  key('c-gen', {across: [[0, '#ff2fa8'], [.5, '#8b46ff'], [1, '#27e4ff']], side: '#2a0f5c', rim: '#ffd0f0', gloss: .6, lit: '#c44cff'});

  // ---- SPIRIT HILLS' set: gouache on paper, every edge inked by hand, a hard cel shadow under everything
  const INK = '#2b1d14';
  function paper(name, o = {}) {
    const W = o.W || 352, H = o.H || W, M = o.M ?? 24, CZ = o.CZ || 96, r = o.r || 26, k = o.k || 1, {c, g} = mk(W, H), x = M, y = M, w = W - 2 * M, h = H - 2 * M;
    { const shp = hand(x + 6 * k, y + 9 * k, w, h, r, {amp: 1.3 * k, cz: CZ, W, H, seed: name + 'b'}); if (o.open) stroke(g, shp, 'rgba(43,29,20,.3)', 4.5); else fill(g, shp, 'rgba(43,29,20,.32)'); }   // the cel shadow
    const body = hand(x, y, w, h, r, {amp: 1.7 * k, cz: CZ, W, H, seed: name + 'b'});
    if (!o.open) { const gr = g.createLinearGradient(0, y, 0, y + h); const e0 = (CZ - M) / h; (o.fill || [[0, 'rgba(255,250,238,.9)'], [e0, 'rgba(251,243,222,.91)'], [1 - e0, 'rgba(251,243,222,.91)'], [1, 'rgba(245,234,206,.92)']]).forEach(([t, col]) => gr.addColorStop(t, col)); fill(g, body, gr);
      g.save(); path(g, body); g.clip();
      if (o.wood) { g.strokeStyle = 'rgba(70,40,16,.22)'; g.lineWidth = 1.6; for (let i = 0; i < 9; i++) { const yy = y + 8 + i * (h - 16) / 8; g.beginPath(); g.moveTo(0, yy); g.lineTo(W, yy + (i % 2 ? 1.5 : -1.5)); g.stroke(); }   // the grain runs along the plank
        g.fillStyle = 'rgba(255,236,200,.4)'; g.fillRect(0, y + 4, W, 3); g.fillStyle = 'rgba(60,35,15,.3)'; g.fillRect(0, y + h - 9, W, 9);
        for (const [nx, ny] of [[x + 16, y + 16], [W - x - 16, y + 16], [x + 16, y + h - 16], [W - x - 16, y + h - 16]]) { g.fillStyle = '#3a2413'; g.beginPath(); g.arc(nx, ny, 3.4, 0, TAU); g.fill(); g.fillStyle = 'rgba(255,230,190,.6)'; g.beginPath(); g.arc(nx - 1, ny - 1, 1.1, 0, TAU); g.fill(); } }   // four nails
      else { g.fillStyle = 'rgba(200,160,95,.16)'; g.fillRect(0, y + h - 26 * k, W, 26 * k); g.fillStyle = 'rgba(255,255,255,.55)'; g.fillRect(0, y + 5 * k, W, 3); }                 // warmth pooled at the foot of the sheet, light along its top
      g.restore(); stroke(g, hand(x + 7 * k, y + 7 * k, w - 14 * k, h - 14 * k, Math.max(4, r - 6 * k), {amp: 1.2 * k, cz: CZ, W, H, seed: name + 'k'}), o.wood ? 'rgba(255,236,200,.28)' : 'rgba(255,255,255,.75)', 2.2); }
    else stroke(g, hand(x + 8, y + 8, w - 16, h - 16, r - 6, {amp: 1.4, cz: CZ, W, H, seed: name + 'k'}), 'rgba(43,29,20,.45)', 1.6);                        // an open frame: a second, lighter ink line inside
    stroke(g, body, INK, 4 * (k < 1 ? .85 : 1)); stroke(g, hand(x - 1, y + 1, w + 1, h, r, {amp: 2.2 * k, cz: CZ, W, H, seed: name + 'z'}), INK, 1.3, 0, .45);          // inked twice, the second pass a little off: a hand-drawn line
    if (o.seal) { g.save(); g.translate(W - CZ + 34, y + h - 36); g.rotate(-.1); g.fillStyle = '#d6452b'; rrect(g, -15, -15, 30, 30, 5); g.fill(); g.strokeStyle = 'rgba(120,30,14,.6)'; g.lineWidth = 1.4; rrect(g, -15, -15, 30, 30, 5); g.stroke();
      g.fillStyle = '#fff3e0'; g.font = '900 20px "Zen Maru Gothic"'; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText('鼓', 0, 1); g.restore();                         // the red seal: 鼓, "drum"
      g.save(); g.translate(x + 26, y + 30); g.rotate(-.5); for (const [lx, ly, a] of [[0, 0, -.7], [10, -3, .5], [20, 0, -.6]]) { g.save(); g.translate(lx, ly); g.rotate(a); g.fillStyle = '#6fb45a'; g.beginPath(); g.ellipse(0, -7, 4.2, 9, 0, 0, TAU); g.fill(); g.strokeStyle = INK; g.lineWidth = 1.3; g.stroke(); g.restore(); }
      g.strokeStyle = INK; g.lineWidth = 1.6; g.beginPath(); g.moveTo(-8, 4); g.quadraticCurveTo(10, 8, 30, 2); g.stroke(); g.restore(); }                                                 // a little sprig in the top-left corner
    out['ui-' + name] = c; meta.ui[name] = {w: W, h: H, cz: CZ, m: M};
  }
  function block(name, top, bot, o = {}) {   // a painted wooden block: the darker band underneath is its thickness; a pressed block sits lower
    const W = 148, H = 100, M = 12, CZ = 36, r = 17, E = o.down ? 3 : 8, dy = o.down ? 5 : 0, {c, g} = mk(W, H), x = M, y = M + dy, w = W - 2 * M, h = H - 2 * M - E - dy;
    const all = hand(x, y, w, h + E, r, {amp: 1.3, cz: CZ, W, H, seed: name + 'a'}), face = hand(x, y, w, h, r, {amp: 1.3, cz: CZ, W, H, seed: name + 'a'});
    if (o.clear) { fill(g, all, 'rgba(43,29,20,.5)'); fill(g, face, '#000', 0, 1, 'destination-out'); }
    else { fill(g, all, mix(bot, INK, .5)); const gr = g.createLinearGradient(0, y, 0, y + h); gr.addColorStop(0, top); gr.addColorStop(1, bot); fill(g, face, gr); }
    g.save(); path(g, face); g.clip(); g.fillStyle = `rgba(255,255,255,${o.clear ? .4 : .62})`; rrect(g, x + 9, y + 5, w - 18, 4.5, 2.2); g.fill();                                 // one stroke of white along the top
    g.strokeStyle = 'rgba(60,35,15,.07)'; g.lineWidth = 1; for (let i = 0; i < 5; i++) { g.beginPath(); g.moveTo(0, y + 16 + i * 9); g.lineTo(W, y + 17 + i * 9); g.stroke(); }                                   // the brush's bristles
    g.fillStyle = 'rgba(60,35,15,.10)'; g.fillRect(0, y + h - 7, W, 7); g.restore();
    stroke(g, all, INK, 3.4); stroke(g, face, INK, 2);
    out['ui-' + name] = c; meta.ui[name] = {w: W, h: H, cz: CZ, m: M};
  }
  paper('a-panel', {seal: true}); paper('a-frame', {open: true}); paper('a-wood', {W: 300, H: 124, M: 20, CZ: 52, r: 18, k: .7, wood: true, fill: [[0, '#c99c68'], [.55, '#ab7a4a'], [1, '#8f6038']]});
  const BL = {off: ['#fffaf0', '#f1e4c4'], btn: ['#dbe9fc', '#b6cef1'], grn: ['#b8e89e', '#7cc763'], red: ['#ffe4da', '#f3b8a6'], gen: ['#f1765a', '#d2432a'], lin: ['#d3e6fc', '#a4c6f0'], wild: ['#fcd7e5', '#eea3c1'], chop: ['#fce0c0', '#f0b274'],
    var: ['#e5dafb', '#c0aaec'], fill: ['#fcedb8', '#efcc68'], roll: ['#cbf1e9', '#8fd5c6'], brk: ['#dddafc', '#b6aff0']};
  for (const [k2, [t, b]] of Object.entries(BL)) block('a-' + k2, t, b);
  block('a-on', '#ffe894', '#f5c543', {down: true}); block('a-gloss', '', '', {clear: true});

  // ---- a preview: the pieces laid out the way the page uses them
  const nine = (g, im, cz, m, x, y, w, h, rep) => { const W = im.width, H = im.height, k = .5, c = cz * k, o = m * k; x -= o; y -= o; w += 2 * o; h += 2 * o;
    const xs = [0, cz, W - cz, W], ys = [0, cz, H - cz, H], dx = [x, x + c, x + w - c, x + w], dy = [y, y + c, y + h - c, y + h];
    for (let j = 0; j < 3; j++) for (let i = 0; i < 3; i++) { const sw = xs[i + 1] - xs[i], sh2 = ys[j + 1] - ys[j], dw = dx[i + 1] - dx[i], dh = dy[j + 1] - dy[j]; if (dw <= 0 || dh <= 0) continue;
      if (rep && (i === 1) !== (j === 1)) { const n = Math.max(1, Math.round((i === 1 ? dw : dh) / ((i === 1 ? sw : sh2) * k))); for (let q = 0; q < n; q++) i === 1 ? g.drawImage(im, xs[i], ys[j], sw, sh2, dx[i] + q * dw / n, dy[j], dw / n, dh) : g.drawImage(im, xs[i], ys[j], sw, sh2, dx[i], dy[j] + q * dh / n, dw, dh / n); }
      else g.drawImage(im, xs[i], ys[j], sw, sh2, dx[i], dy[j], dw, dh); } };
  const S = mk(1400, 520), sg = S.g; { const bg = sg.createLinearGradient(0, 0, 1400, 520); bg.addColorStop(0, '#2a1458'); bg.addColorStop(.5, '#5a1f7a'); bg.addColorStop(1, '#0e2a5c'); sg.fillStyle = bg; sg.fillRect(0, 0, 1400, 520);
    for (let i = 0; i < 90; i++) { sg.fillStyle = ['#ff2fa8', '#27e4ff', '#ffc93c', '#22ff9c'][i % 4]; sg.globalAlpha = .5; sg.fillRect((i * 97) % 1400, (i * 53) % 520, 26, 60); } sg.globalAlpha = 1; }
  const P = n => [out['ui-' + n], meta.ui[n].cz, meta.ui[n].m];
  nine(sg, ...P('c-panel'), 20, 20, 585, 219, true); nine(sg, ...P('c-panel2'), 640, 20, 330, 263, true); nine(sg, ...P('c-frame'), 1000, 20, 380, 263, true); nine(sg, ...P('c-bar'), 640, 300, 560, 46, true);
  sg.font = '700 13px sans-serif'; sg.textAlign = 'center'; sg.textBaseline = 'middle';
  const names = ['c-off', 'c-hov', 'c-gen', ...Object.keys(COL).map(k => 'c-rim-' + k), ...Object.keys(COL).map(k => 'c-on-' + k)];
  names.forEach((n, i) => { const bx = 30 + (i % 8) * 170, by = 310 + Math.floor(i / 8) * 62, bw = i % 3 === 1 ? 40 : 60 + (i * 37) % 90; nine(sg, ...P(n), bx, by, bw, 36, false); sg.fillStyle = n.includes('-on-') ? '#07101c' : '#eef3ff'; sg.fillText(n.slice(2).toUpperCase(), bx + bw / 2, by + 17); });
  ['c-off', 'c-on-cy', 'c-rim-mg', 'c-off', 'c-on-gn'].forEach((n, i) => { nine(sg, ...P(n), 40 + i * 104, 44 + (i % 2) * 0, 96, 36, false); sg.fillStyle = n.includes('-on-') ? '#07101c' : '#eef3ff'; sg.fillText(['PLAY', 'ON', 'NEW', 'UNDO', 'GO'][i], 88 + i * 104, 61); });
  out['ui-sheet'] = S.c;
  const S2 = mk(1400, 420), s2 = S2.g; { const bg = s2.createLinearGradient(0, 0, 0, 420); bg.addColorStop(0, '#8fd0ee'); bg.addColorStop(.5, '#9ad98a'); bg.addColorStop(1, '#5fae4e'); s2.fillStyle = bg; s2.fillRect(0, 0, 1400, 420); }
  nine(s2, ...P('a-panel'), 20, 20, 585, 219, true); nine(s2, ...P('a-frame'), 640, 20, 330, 219, true); nine(s2, ...P('a-wood'), 20, 262, 950, 46, false);
  s2.font = '700 13px sans-serif'; s2.textAlign = 'center'; s2.textBaseline = 'middle';
  ['a-off', 'a-btn', 'a-grn', 'a-red', 'a-gen', 'a-lin', 'a-wild', 'a-chop', 'a-var', 'a-fill', 'a-roll', 'a-brk', 'a-on', 'a-gloss'].forEach((n, i) => { const bx = 40 + (i % 7) * 132, by = 44 + Math.floor(i / 7) * 56 + (i > 6 ? 276 : 0) - (i > 6 ? 56 : 0), bw = 70 + (i * 29) % 50; nine(s2, ...P(n), bx, i > 6 ? 330 : 44, bw, 36, false); s2.fillStyle = n === 'a-gen' ? '#fff7e6' : '#2b1d14'; s2.fillText(n.slice(2).toUpperCase(), bx + bw / 2, (i > 6 ? 330 : 44) + 17); });
  out['ui-sheet2'] = S2.c;
};
