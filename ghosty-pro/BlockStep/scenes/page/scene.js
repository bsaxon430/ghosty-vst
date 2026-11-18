
/* ---------- LOOKS (owner 2026-11-13): "redo the UI completely ... closer to what we did for Poltergeist 2 and Phantom FX",
   then: "hand drawn ... insane glow for neon ... 3-D looking see-through", and "the old ghost money look should still
   be an option too". The SCENE button switches three looks:
   GHOST CITY (a haunted neon avenue at night, seen through glass panels in hand-bent neon tube, with glass keys),
   SPIRIT HILLS (a gouache-and-ink painted world: inked paper, painted wooden blocks) and ORIGINAL (the Ghost Money look,
   drawn by the page's own untouched code). Same layout, same controls: only the look. The paintings are pictures
   inside this page (no files to fetch); a canvas behind the page shows the scene and its moving parts (rain, signs that
   flare with each drum, the ghost drummer). GHOST ANIMATION: OFF (or reduced motion) holds the scene still. HACKER MODE
   is unchanged. Nothing here touches the notes or the sound. ---------- */
var TH = null;
/* the tracker's colours for each scene (the grid canvas reads these; HACKER MODE has its own) */
const scRgb = h => { const n = parseInt(h.slice(1), 16); return `${n >> 16},${(n >> 8) & 255},${n & 255}`; };
const cShade = new Map(), cShades = rgb => { let o = cShade.get(rgb); if (!o) { const v = rgb.split(',').map(Number), mx = (k, t) => v.map(c => Math.round(c + (t - c) * k)).join(','); o = {hi: mx(.64, 255), lo: mx(.52, 10), dk: mx(.74, 4), rim: mx(.55, 255)}; cShade.set(rgb, o); } return o; };
function cityCell(g, x, y, w, h, ce, lit, selected) {   // a note = a thick block of lit gel standing on the glass, throwing its colour round it
  const rgb = cellRgb(ce), C = cShades(rgb), f = (ce.v || 80) / 100, pno = ce.i === PIANO_I || ce.i === SYN_I, soft = (ce.v || 80) < 50;
  const E = h >= 16 ? 3 : h >= 11 ? 2 : 0, hh = h - E, r = Math.min(7, hh / 2);
  if (ce.m) { g.save(); g.globalAlpha = .4; }
  g.save(); g.globalCompositeOperation = 'lighter'; const ga = (soft ? .04 : .085) + .07 * f + .5 * lit;                       // its light on the glass
  g.fillStyle = `rgba(${rgb},${ga * .7})`; rr(g, x - 6, y - 5, w + 12, h + 11, r + 6); g.fill();
  g.fillStyle = `rgba(${rgb},${ga})`; rr(g, x - 2.5, y - 2, w + 5, h + 4.5, r + 2.5); g.fill();
  if (lit > 0) { g.shadowColor = `rgb(${rgb})`; g.shadowBlur = 32 * lit; g.fillStyle = `rgba(${rgb},${.55 * lit})`; rr(g, x, y, w, h, r); g.fill(); }
  g.restore();
  if (E) { g.fillStyle = `rgba(${soft ? '6,4,18' : C.dk},.96)`; rr(g, x, y + E, w, hh, r); g.fill(); }                           // the block's side: its thickness
  const body = g.createLinearGradient(0, y, 0, y + hh);
  if (soft) { body.addColorStop(0, `rgba(${C.lo},.8)`); body.addColorStop(1, 'rgba(8,6,22,.86)'); }                              // a quiet hit: dark glass with a dashed tube
  else { body.addColorStop(0, `rgba(${C.hi},${Math.min(1, .92 + .08 * lit)})`); body.addColorStop(.42, `rgba(${rgb},${.8 + .15 * f})`); body.addColorStop(1, `rgba(${C.lo},.95)`); }
  g.fillStyle = body; rr(g, x, y, w, hh, r); g.fill();
  if (hh > 8) { const gl = g.createLinearGradient(0, y + 1, 0, y + 1 + hh * .55); gl.addColorStop(0, `rgba(255,255,255,${soft ? .16 : .6})`); gl.addColorStop(1, 'rgba(255,255,255,0)'); g.fillStyle = gl; rr(g, x + 2.5, y + 1.5, w - 5, hh * .5, Math.max(1, r - 2.5)); g.fill(); }   // the shine on its upper half
  g.lineWidth = selected ? 2 : 1.25; g.strokeStyle = selected ? '#ffffff' : `rgba(${soft ? rgb : C.rim},${soft ? .85 : .95})`; if (soft && !selected) g.setLineDash([3, 2]); rr(g, x + .6, y + .6, w - 1.2, hh - 1.2, r); g.stroke(); g.setLineDash([]);
  if (selected) { g.save(); g.globalCompositeOperation = 'lighter'; g.strokeStyle = 'rgba(255,255,255,.3)'; g.lineWidth = 5; rr(g, x - 1, y - 1, w + 2, h + 2, r + 2); g.stroke(); g.restore(); }
  const one = Math.min(hh, ROW - 6), ty = y + one / 2, nm = pno ? noteName(ce.i === SYN_I ? synMidi(ce) : pianoMidi(ce)) : INST[ce.i].ab + sndNum(ce.i, ce.s), pa = pno ? '' : playsAs(ce);
  const put = (t, tx, t2) => { if (!soft) { g.fillStyle = 'rgba(255,255,255,.42)'; g.fillText(t, tx, t2 + 1); } g.fillStyle = soft ? '#f2f6ff' : '#0b0722'; g.fillText(t, tx, t2); };   // dark lettering pressed into the gel
  g.textAlign = 'center'; g.textBaseline = 'middle';
  if (one < 24) { g.font = `700 ${one < 14 ? 9 : 11}px ${DISP}`; put(nm, x + w / 2, ty + .5); }
  else { g.font = `700 ${pno ? 14 : 13}px ${DISP}`; put(nm, x + w / 2, ty - 5);
    g.font = `600 9.5px ${DISP}`; g.fillStyle = soft ? 'rgba(236,240,255,.8)' : 'rgba(11,7,34,.84)';
    g.fillText((pa ? pa + ' · ' : '') + ce.v + (!pno && ce.p ? ' ' + (ce.p > 0 ? '+' : '') + ce.p : '') + (ce.pn ? ' ' + panTxt(ce.pn) : '') + (ce.fx ? ' ' + FXS[ce.fx] : '') + (ce.ac ? ' AC' : '') + (ce.sl ? ' SL' : ''), x + w / 2, ty + 8); }
  if (ce.l > 0) { g.fillStyle = selected ? '#ffffff' : soft ? 'rgba(236,240,255,.65)' : 'rgba(11,7,34,.6)'; g.fillRect(x + w / 2 - 9, y + hh - 4, 18, 2); }
  if (ce.m) { g.strokeStyle = 'rgba(236,240,255,.9)'; g.lineWidth = 1.6; g.beginPath(); g.moveTo(x + 6, y + one - 4); g.lineTo(x + w - 6, y + 4); g.stroke(); g.restore(); }
}
/* GHOST CITY's grid is a sheet of glass standing in the street: you see the city through it. Under the notes: the glass
   darkens toward the bottom, every channel is a shallow groove, a long reflection drifts across. Over the notes: neon
   lines between the channels, a glow on every bar line, a beam of light where the song is playing, and the glass's
   edges flash with the kick. */
let hudC = null, hudKey = '';
function cityHud(W, H) {   // the etching in the glass, painted once per size: a ghost emblem in rings, corner brackets, a line of katakana
  const key = W + 'x' + H + 'x' + DPR + 'x' + GUT; if (hudC && hudKey === key) return hudC; hudKey = key;
  const c = hudC = document.createElement('canvas'); c.width = Math.max(1, Math.round(W * DPR)); c.height = Math.max(1, Math.round(H * DPR)); const g = c.getContext('2d'); g.scale(DPR, DPR);
  const gx = GUT, gw = W - GUT, gy = HEAD, gh = H - HEAD, cx = gx + gw * .5, cy = gy + gh * .52, R0 = Math.min(gw * .3, gh * .44), CY = '39,228,255', MG = '255,47,168';
  g.lineCap = 'round';
  for (const [k, a, col, lw] of [[1, .13, CY, 1.4], [.8, .1, MG, 1], [.62, .12, CY, 2.2], [.34, .09, MG, 1]]) { g.strokeStyle = `rgba(${col},${a})`; g.lineWidth = lw; g.beginPath(); g.arc(cx, cy, R0 * k, 0, TAU); g.stroke(); }
  g.strokeStyle = `rgba(${CY},.16)`; g.lineWidth = 1.2; for (let i = 0; i < 72; i++) { const a = i / 72 * TAU, r1 = R0 * 1.02, r2 = R0 * (i % 6 ? 1.05 : 1.1); g.beginPath(); g.moveTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); g.lineTo(cx + Math.cos(a) * r2, cy + Math.sin(a) * r2); g.stroke(); }
  g.strokeStyle = `rgba(${MG},.2)`; g.lineWidth = 3; for (const [a0, a1] of [[-.5, .3], [1.3, 2.2], [3.3, 3.9], [4.6, 5.2]]) { g.beginPath(); g.arc(cx, cy, R0 * .9, a0, a1); g.stroke(); }
  { const k = R0 * .9 / 44, P2 = new Path2D('M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z');   // the ghost, as one tube
    g.save(); g.translate(cx - 32 * k, cy - 33 * k); g.scale(k, k); g.lineJoin = 'round'; g.strokeStyle = `rgba(${CY},.07)`; g.lineWidth = 5 / k; g.stroke(P2); g.strokeStyle = `rgba(190,245,255,.2)`; g.lineWidth = 1.6 / k; g.stroke(P2);
    g.fillStyle = `rgba(${MG},.2)`; g.beginPath(); g.ellipse(25, 31, 2.6, 3.6, 0, 0, TAU); g.ellipse(39, 31, 2.6, 3.6, 0, 0, TAU); g.fill(); g.restore(); }
  g.strokeStyle = `rgba(${CY},.3)`; g.lineWidth = 1.6;                                                                     // brackets in the corners of the glass
  for (const [bx, by, sx, sy] of [[gx + 12, gy + 12, 1, 1], [W - 12, gy + 12, -1, 1], [gx + 12, H - 12, 1, -1], [W - 12, H - 12, -1, -1]]) { g.beginPath(); g.moveTo(bx, by + sy * 26); g.lineTo(bx, by); g.lineTo(bx + sx * 26, by); g.stroke();
    for (let i = 1; i <= 5; i++) { g.beginPath(); g.moveTo(bx + sx * (30 + i * 6), by); g.lineTo(bx + sx * (30 + i * 6), by + sy * (i % 5 ? 3 : 6)); g.stroke(); } }
  g.save(); g.font = `700 ${Math.round(Math.min(46, gh * .1))}px "Hiragino Sans","Yu Gothic","Noto Sans CJK JP",sans-serif`; g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillStyle = `rgba(${MG},.1)`;      // ブロックステップ = BLOCK STEP, down the right edge
  const kana = 'ブロックステップ', fs = Math.min(46, gh * .1); for (let i = 0; i < kana.length; i++) g.fillText(kana[i], W - 34, gy + gh * .1 + (i + .5) * fs * 1.08); g.restore();
  g.font = `600 9px ${DISP}`; g.textAlign = 'left'; g.textBaseline = 'alphabetic'; g.fillStyle = `rgba(${CY},.34)`; g.fillText('GHOSTBLOCK // BLOCK STEP // GLASS 01', gx + 46, H - 9);
  return c; }
function cityBack(g, W, H, st, sl, now) {
  const gr = g.createLinearGradient(0, HEAD, 0, H); gr.addColorStop(0, 'rgba(70,34,140,.10)'); gr.addColorStop(.5, 'rgba(8,5,26,.10)'); gr.addColorStop(1, 'rgba(3,2,12,.44)'); g.fillStyle = gr; g.fillRect(0, HEAD, W, H - HEAD);
  const c0 = Math.max(0, Math.floor(sl / colW)), c1 = Math.min(CH - 1, Math.ceil((sl + W - GUT) / colW));
  for (let c = c0; c <= c1; c++) { const x = GUT + c * colW - sl, lg = g.createLinearGradient(x, 0, x + colW, 0);
    lg.addColorStop(0, 'rgba(0,0,0,.34)'); lg.addColorStop(.07, 'rgba(120,210,255,.08)'); lg.addColorStop(.5, 'rgba(120,210,255,0)'); lg.addColorStop(.93, 'rgba(255,90,200,.07)'); lg.addColorStop(1, 'rgba(0,0,0,.38)');
    g.fillStyle = lg; g.fillRect(x, HEAD, colW, H - HEAD); }
  const still = RM.matches || window.__ghostsOff, k = still ? .3 : (now / 16000) % 1, x0 = -W * .5 + k * W * 2, rg = g.createLinearGradient(x0, HEAD, x0 + W * .32, HEAD + W * .2);
  rg.addColorStop(0, 'rgba(210,240,255,0)'); rg.addColorStop(.4, 'rgba(210,240,255,.045)'); rg.addColorStop(.5, 'rgba(230,248,255,.10)'); rg.addColorStop(.6, 'rgba(210,240,255,.045)'); rg.addColorStop(1, 'rgba(210,240,255,0)');
  g.fillStyle = rg; g.fillRect(0, HEAD, W, H - HEAD);
  try { g.drawImage(cityHud(W, H), 0, 0, W, H); } catch { /* fine */ }
  if (!still) { const cx = GUT + (W - GUT) * .5, cy = HEAD + (H - HEAD) * .52, R0 = Math.min((W - GUT) * .3, (H - HEAD) * .44), a = now / 5200;   // one ring of the emblem turns; a scan line drops down the glass
    g.save(); g.lineCap = 'round'; g.strokeStyle = 'rgba(39,228,255,.22)'; g.lineWidth = 2.4; g.setLineDash([R0 * .5, R0 * .34]); g.beginPath(); g.arc(cx, cy, R0 * .72, a, a + TAU); g.stroke();
    g.strokeStyle = 'rgba(255,47,168,.2)'; g.lineWidth = 1.4; g.setLineDash([R0 * .16, R0 * .22]); g.beginPath(); g.arc(cx, cy, R0 * .5, -a * 1.7, -a * 1.7 + TAU); g.stroke(); g.restore();
    const sy = HEAD + ((now / 7000) % 1) * (H - HEAD), sg = g.createLinearGradient(0, sy - 26, 0, sy + 2); sg.addColorStop(0, 'rgba(39,228,255,0)'); sg.addColorStop(1, 'rgba(120,235,255,.10)'); g.fillStyle = sg; g.fillRect(GUT, sy - 26, W - GUT, 28); }
}
function cityOver(g, W, H, st, sl, now) {
  const p = P(), n = p.rows.length, r0 = Math.max(0, Math.floor(st / ROW) - 1), r1 = Math.min(n - 1, Math.ceil((st + H) / ROW) + 1);
  g.save(); g.beginPath(); g.rect(0, HEAD, W, H - HEAD); g.clip(); g.globalCompositeOperation = 'lighter';
  const c0 = Math.max(0, Math.floor(sl / colW)), c1 = Math.min(CH - 1, Math.ceil((sl + W - GUT) / colW));
  for (let c = c0; c <= c1 + 1; c++) { const x = GUT + c * colW - sl; if (x < GUT - 1) continue; g.fillStyle = 'rgba(39,228,255,.05)'; g.fillRect(x - 3, HEAD, 7, H - HEAD); g.fillStyle = 'rgba(120,235,255,.26)'; g.fillRect(x, HEAD, 1, H - HEAD); }   // a thin tube between channels
  const tm = tmap(p);
  for (let r = r0; r <= r1; r++) if (tm.t[r] % 16 < 1e-6) { const y = HEAD + r * ROW - st, bg = g.createLinearGradient(0, y - 7, 0, y + 9); bg.addColorStop(0, 'rgba(255,47,168,0)'); bg.addColorStop(.5, 'rgba(255,47,168,.22)'); bg.addColorStop(1, 'rgba(255,47,168,0)'); g.fillStyle = bg; g.fillRect(GUT, y - 7, W - GUT, 16); }   // every bar line glows
  const ph = playing && playStep >= 0 ? playStep : -1;
  if (ph >= 0) { const y = HEAD + ph * ROW - st, bm = g.createLinearGradient(0, y - 12, 0, y + ROW + 12);                                                              // the beam where the song is
    bm.addColorStop(0, 'rgba(39,228,255,0)'); bm.addColorStop(.3, 'rgba(39,228,255,.13)'); bm.addColorStop(.5, 'rgba(170,246,255,.26)'); bm.addColorStop(.7, 'rgba(39,228,255,.13)'); bm.addColorStop(1, 'rgba(39,228,255,0)');
    g.fillStyle = bm; g.fillRect(0, y - 12, W, ROW + 24); }
  if (!RM.matches) for (const [key, t0] of hitT) { const age = now - t0; if (age < 0 || age > 460) continue; const r = Math.floor(key / 32), c = key % 32, ce = p.rows[r] && p.rows[r][c]; if (!ce) continue;
    const k = age / 460, x = GUT + c * colW - sl, y = HEAD + r * ROW - st, rgb = cellRgb(ce), cxm = x + colW / 2, cym = y + ROW / 2, e = 4 + 26 * k;
    g.strokeStyle = `rgba(${rgb},${.75 * (1 - k)})`; g.lineWidth = 2.6 * (1 - k) + .6; rr(g, x + 2 - e, y + 3 - e * .6, colW - 4 + 2 * e, ROW - 6 + e * 1.2, 8 + e * .5); g.stroke();                 // the ring
    g.fillStyle = `rgba(255,255,255,${.9 * (1 - k)})`; for (let i = 0; i < 9; i++) { const a = (key * 2.399 + i * .698) % TAU, d = (colW * .34 + 46 * k) * (.7 + .3 * ((i * 7 + key) % 5) / 4), sz = 2.6 * (1 - k) + .6;   // the sparks
      g.fillRect(cxm + Math.cos(a) * d - sz / 2, cym + Math.sin(a) * d * .62 - sz / 2, sz, sz); } }
  const kp = RM.matches ? 0 : Math.min(1, kickPulse || 0), ea = .1 + .5 * kp, lg = g.createLinearGradient(GUT, 0, GUT + 34, 0), rg2 = g.createLinearGradient(W - 34, 0, W, 0), bg2 = g.createLinearGradient(0, H - 40, 0, H);
  lg.addColorStop(0, `rgba(255,47,168,${ea})`); lg.addColorStop(1, 'rgba(255,47,168,0)'); rg2.addColorStop(0, 'rgba(255,47,168,0)'); rg2.addColorStop(1, `rgba(255,47,168,${ea})`); bg2.addColorStop(0, 'rgba(39,228,255,0)'); bg2.addColorStop(1, `rgba(39,228,255,${.08 + .3 * kp})`);
  g.fillStyle = lg; g.fillRect(GUT, HEAD, 34, H - HEAD); g.fillStyle = rg2; g.fillRect(W - 34, HEAD, 34, H - HEAD); g.fillStyle = bg2; g.fillRect(0, H - 40, W, 40);
  g.restore();
}
/* SPIRIT HILLS: a note = a painted wooden block with an ink outline. Every drum keeps its own colour, mixed as gouache. */
const GOU = ['#4fb06c', '#8a6fd6', '#e486ab', '#5a9ade', '#43b6c4', '#e8963f', '#e3bb3a', '#de5c52', '#c070cc', '#b9b1a0', '#e6cc74', '#84c548'];
const gouOf = rgb => { const [r0, g0, b0] = rgb.split(',').map(Number), mx = Math.max(r0, g0, b0), mn = Math.min(r0, g0, b0), k = .58, m = (mx + mn) / 2 * .2 + 150 * .8;   // a neon colour pulled toward a mid-value, chalky paint
  return '#' + [r0, g0, b0].map(v => Math.round(clamp(m + (v - (mx + mn) / 2) * k, 30, 240)).toString(16).padStart(2, '0')).join(''); };
const gouCache = new Map(), gou = ce => { if (ce.i !== PIANO_I && ce.i !== SYN_I) return GOU[ce.i] || '#b9b1a0'; const k = cellRgb(ce); if (!gouCache.has(k)) gouCache.set(k, gouOf(k)); return gouCache.get(k); };
const shadeHex = (h, k) => { const n = parseInt(h.slice(1), 16), f = v => Math.round(clamp(k > 0 ? v + (255 - v) * k : v * (1 + k), 0, 255)); return `rgb(${f(n >> 16)},${f((n >> 8) & 255)},${f(n & 255)})`; };
function animeCell(g, x, y, w, h, ce, lit, selected) {
  const c = gou(ce), r = Math.min(4, h / 2), pno = ce.i === PIANO_I || ce.i === SYN_I, soft = (ce.v || 80) < 50, INK = '#2b1d14';
  if (ce.m) { g.save(); g.globalAlpha = .45; }
  if (lit > 0) { g.save(); g.globalAlpha *= lit * .8; g.strokeStyle = shadeHex(c, .25); g.lineWidth = 2; rr(g, x - 3 - lit * 3, y - 3 - lit * 2, w + 6 + lit * 6, h + 6 + lit * 4, r + 4); g.stroke(); g.restore(); }
  g.fillStyle = 'rgba(60,40,20,.28)'; rr(g, x + 1, y + 2, w, h, r); g.fill();   // its shadow on the paper
  g.save(); if (soft) g.globalAlpha *= .62;
  g.fillStyle = shadeHex(c, lit * .45); rr(g, x, y, w, h, r); g.fill();
  g.save(); rr(g, x, y, w, h, r); g.clip();
  if (h > 8) { g.fillStyle = shadeHex(c, .32 + lit * .3); g.fillRect(x, y, w, Math.min(3.2, h * .22)); g.fillStyle = shadeHex(c, -.2); g.fillRect(x, y + h - Math.min(3.4, h * .24), w, 4); g.fillStyle = 'rgba(255,255,255,.14)'; g.fillRect(x, y, 2.2, h); }
  g.restore(); g.restore();
  g.lineWidth = selected ? 2.4 : 1.5; g.strokeStyle = selected ? '#d6452b' : INK; g.lineJoin = 'round'; if (soft && !selected) g.setLineDash([4, 2.5]); rr(g, x + .5, y + .5, w - 1, h - 1, r); g.stroke(); g.setLineDash([]);
  const one = Math.min(h, ROW - 6), ty = y + one / 2, nm = pno ? noteName(ce.i === SYN_I ? synMidi(ce) : pianoMidi(ce)) : INST[ce.i].ab + sndNum(ce.i, ce.s), pa = pno ? '' : playsAs(ce);
  g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillStyle = INK;
  if (one < 24) { g.font = `700 ${one < 14 ? 9 : 11}px ${DISP}`; g.fillText(nm, x + w / 2, ty + .5); }
  else { g.font = `700 ${pno ? 14 : 13}px ${DISP}`; g.fillText(nm, x + w / 2, ty - 5);
    g.font = `600 9.5px ${DISP}`; g.fillStyle = 'rgba(43,29,20,.78)';
    g.fillText((pa ? pa + ' · ' : '') + ce.v + (!pno && ce.p ? ' ' + (ce.p > 0 ? '+' : '') + ce.p : '') + (ce.pn ? ' ' + panTxt(ce.pn) : '') + (ce.fx ? ' ' + FXS[ce.fx] : '') + (ce.ac ? ' AC' : '') + (ce.sl ? ' SL' : ''), x + w / 2, ty + 8); }
  if (ce.l > 0) { g.fillStyle = selected ? '#d6452b' : 'rgba(43,29,20,.6)'; g.fillRect(x + w / 2 - 9, y + h - 4, 18, 2); }
  if (ce.m) { g.strokeStyle = INK; g.lineWidth = 1.6; g.beginPath(); g.moveTo(x + 6, y + one - 4); g.lineTo(x + w - 6, y + 4); g.stroke(); g.restore(); }
}
var THEMES = {
  city: {base: '#07051a', glow: true, glitch: true, rain: true, rainA: '39,228,255', rainB: '255,47,168', rainHead: '225,245,255', cell: cityCell, back: cityBack, over: cityOver,
    bg: 'rgba(6,4,20,.30)', loopFill: 'rgba(255,47,168,.11)', loopOff: 'rgba(255,255,255,.03)', loopLine: '#ff2fa8', loopLineOff: 'rgba(255,255,255,.25)', loopA: '#ff2fa8', loopB: '#27e4ff', cue: '#ffd23a',
    playFill: 'rgba(39,228,255,.10)', playLine: 'rgba(39,228,255,.9)', playGlow: '#27e4ff', playScan: 'rgba(39,228,255,.2)',
    bar: 'rgba(255,120,210,.85)', beat: 'rgba(150,225,255,.22)', sub: 'rgba(255,255,255,.07)', colOff: 'rgba(0,0,0,.5)', colLock: 'rgba(255,210,58,.08)', colAlt: 'rgba(255,255,255,.02)',
    gBar: 'rgba(255,140,220,.95)', gBeat: 'rgba(150,225,255,.42)', gStep: 'rgba(200,220,255,.16)', gCol: 'rgba(150,225,255,.2)', dot: 'rgba(190,225,255,.26)',
    pick: '#27e4ff', flickA: '39,228,255', flickB: '255,47,168', fxGlyph: '#27e4ff', sel: '#27e4ff', selFill: 'rgba(39,228,255,.12)',
    gut: 'rgba(8,6,22,.52)', gutShade: '5,4,14', numPlay: '#27e4ff', numBar: '#ff8fd6', numBeat: 'rgba(240,242,255,.85)', numStep: 'rgba(160,172,205,.65)',
    head: 'rgba(9,6,24,.8)', headLine: '255,47,168', headSel: 'rgba(255,47,168,.3)', headLock: '#ffd23a', headOff: 'rgba(156,163,175,.5)', headTxt: 'rgba(240,244,255,.9)', headStep: 'rgba(170,182,214,.9)',
    glA: 'rgba(255,47,168,.12)', glB: 'rgba(39,228,255,.10)'},
  anime: {base: '#bfe3f2', glow: false, glitch: false, rain: false, cell: animeCell, inst: GOU, rgb: ce => scRgb(gou(ce)),
    bg: 'rgba(250,243,226,.72)', loopFill: 'rgba(214,69,43,.09)', loopOff: 'rgba(60,40,20,.04)', loopLine: '#d6452b', loopLineOff: 'rgba(60,40,20,.35)', loopA: '#d6452b', loopB: '#e3a13a', cue: '#c98a14',
    playFill: 'rgba(214,69,43,.10)', playLine: 'rgba(196,58,36,.9)', playGlow: '#d6452b', playScan: 'rgba(214,69,43,.22)',
    bar: 'rgba(43,29,20,.62)', beat: 'rgba(43,29,20,.2)', sub: 'rgba(43,29,20,.1)', colOff: 'rgba(60,40,20,.2)', colLock: 'rgba(201,138,20,.12)', colAlt: 'rgba(120,90,40,.045)',
    gBar: 'rgba(43,29,20,.85)', gBeat: 'rgba(43,29,20,.42)', gStep: 'rgba(43,29,20,.2)', gCol: 'rgba(43,29,20,.3)', dot: 'rgba(43,29,20,.2)',
    pick: '#2f6fc0', flickA: '214,69,43', flickB: '47,111,192', fxGlyph: '#d6452b', sel: '#2f6fc0', selFill: 'rgba(47,111,192,.14)',
    gut: 'rgba(236,224,196,.86)', gutShade: '120,90,50', numPlay: '#c43a24', numBar: '#2b1d14', numBeat: 'rgba(43,29,20,.86)', numStep: 'rgba(90,70,50,.7)',
    head: 'rgba(110,72,40,.96)', headLine: '250,236,200', headSel: 'rgba(250,236,200,.3)', headLock: '#ffd66a', headOff: 'rgba(250,236,200,.45)', headTxt: 'rgba(253,245,226,.96)', headStep: 'rgba(240,224,190,.9)',
    glA: 'rgba(214,69,43,.1)', glB: 'rgba(47,111,192,.08)'},
};
var SCN = (() => {
  const cv = $('scn'); if (!cv) return null;
  const g = cv.getContext('2d', {alpha: false});
  const KEYS = ['city', 'anime', 'orig'], LABEL = {city: 'SCENE: CITY', anime: 'SCENE: ANIME', orig: 'SCENE: ORIGINAL'}, TOAST = {city: 'GHOST CITY', anime: 'SPIRIT HILLS', orig: 'ORIGINAL: the Ghost Money look'};
  const bgSync = window.__gmbgSync;   // the original background's own on / off (it rests while a painted scene is showing)
  const META = /*SMETA*/{}/*END*/;
  let cur = 'city';   // build 117: GHOST CITY every time the window opens (owner 2026-11-17); SCENE still switches while it's open
  let vw = 0, vh = 0, DPRS = 1, need = true, last = 0, t0 = performance.now(), prev = null, fadeT = 0, slow = 0, lite = false;
  const pulse = new Float32Array(16), hitAt = new Float64Array(24); let kick = 0, snare = 0;   // hitAt[20] / [21]: the drummer's right / left stick
  // ---- pictures (kept as text in the page; decoded the first time a scene needs them)
  const pics = {};
  function pic(id) { let o = pics[id]; if (o) return o; o = pics[id] = {img: new Image(), ok: false};
    const e = document.getElementById('art-' + id), d = e ? e.textContent.trim() : '';
    o.img.onload = () => { o.ok = true; need = true; }; o.img.onerror = () => { o.bad = true; };
    if (d) o.img.src = 'data:' + (e.dataset.mime || 'image/jpeg') + ';base64,' + d; else o.bad = true; return o; }
  // which painting fits this window: the wide one (the plugin window, a desktop browser) or the tall one (a phone)
  const kindOf = () => (vw / Math.max(1, vh) < .9 && META[cur + 'P']) ? 'P' : 'L';
  // where the scene's open "window" is on the page: the gap between the transport and the BREAK ENGINE panels (wide),
  // or the name row (phone). The painting is placed so its own focal part sits there.
  function slot() {
    const T = document.querySelector('#topBand section.tight'), E = $('engBox'), hd = document.querySelector('header.top'), sy = window.scrollY || 0;
    if (vw >= 900 && T && E) { const a = T.getBoundingClientRect(), b = E.getBoundingClientRect(); if (b.left - a.right > 120 && Math.abs(a.top - b.top) < 60) return [a.right + 12, a.top + sy, b.left - a.right - 24, a.height]; }
    const TR = $('topRight'); if (hd && TR) { const r = hd.getBoundingClientRect(), q = TR.getBoundingClientRect(); if (r.width > 0 && r.top - q.top > 50) return [0, q.top + sy, vw, r.top - q.top]; if (r.width > 0) return [0, r.top + sy - 24, vw, r.height + 72]; }
    return [vw * .25, vh * .05, vw * .5, vh * .28];
  }
  const fit = {};   // key -> {s, ox, oy}
  function place(key) { const m = META[key]; if (!m) return null; const [sx, sy, sw, sh] = slot(), s = Math.max(vw / m.W, vh / m.H);
    let ox = sx + sw / 2 - (m.hero[0] + m.hero[2] / 2) * s, oy = sy + sh / 2 - (m.hero[1] + m.hero[3] / 2) * s;
    ox = clamp(ox, vw - m.W * s, 0); oy = clamp(oy, vh - m.H * s, 0);
    if (Math.abs(ox) < 1.5 && Math.abs(oy) < 1.5 && Math.abs(s - 1) < .004) { ox = 0; oy = 0; }
    return fit[key] = {s, ox, oy, m, key}; }
  function resize() { vw = innerWidth; vh = innerHeight; DPRS = Math.min(window.devicePixelRatio || 1, 2);
    cv.width = Math.max(1, Math.round(vw * DPRS)); cv.height = Math.max(1, Math.round(vh * DPRS)); need = true; rainInit2(); }
  // ---- the moving parts
  let drops = [], petals = [];
  function rainInit2() { drops = []; const n = Math.round(clamp(vw * vh / 5200, 60, 260)); for (let i = 0; i < n; i++) drops.push({x: Math.random() * (vw + 200) - 100, y: Math.random() * vh, v: 620 + Math.random() * 520, l: 10 + Math.random() * 20, a: .05 + Math.random() * .16}); petals = []; }
  const FX = {
    city(F, t, dt, still) {
      const {s, ox, oy, m} = F, X = x => ox + x * s, Y = y => oy + y * s;
      g.save(); g.globalCompositeOperation = 'lighter';
      // the sky breathes with the kick; warning lights blink; windows flicker now and then
      if (kick > .02) { const r = m.W * .34 * s, x = X(m.vp[0]), y = Y(m.vp[1]); const gr = g.createRadialGradient(x, y, 0, x, y, r); gr.addColorStop(0, `rgba(255,120,200,${.22 * kick})`); gr.addColorStop(1, 'rgba(255,120,200,0)'); g.fillStyle = gr; g.fillRect(x - r, y - r, r * 2, r * 2); }
      if (!still && m.lamps) for (let i = 0; i < m.lamps.length; i++) { const L = m.lamps[i]; if (L[2] > 2.5) continue; const b = .5 + .5 * Math.sin(t * (1.3 + (i % 5) * .23) + i * 1.7); if (b < .55) continue;
        const x = X(L[0]), y = Y(L[1]), r = (5 + L[2] * 3) * s * b; const gr = g.createRadialGradient(x, y, 0, x, y, r); gr.addColorStop(0, `rgba(255,70,90,${.7 * b})`); gr.addColorStop(1, 'rgba(255,70,90,0)'); g.fillStyle = gr; g.fillRect(x - r, y - r, r * 2, r * 2); }
      if (m.wisps) for (let i = 0; i < m.wisps.length; i++) { const w = m.wisps[i], b = still ? .25 : .25 + .3 * Math.sin(t * 2.1 + i * 2.3) + pulse[3] * .5, x = X(w[0]) + (still ? 0 : Math.sin(t * .7 + i) * 2 * s), y = Y(w[1]) + (still ? 0 : Math.cos(t * .9 + i * 1.3) * 2 * s), r = w[2] * 5 * s;
        if (b <= 0) continue; const gr = g.createRadialGradient(x, y, 0, x, y, r); gr.addColorStop(0, `rgba(150,255,220,${Math.min(.9, b)})`); gr.addColorStop(1, 'rgba(150,255,220,0)'); g.fillStyle = gr; g.fillRect(x - r, y - r, r * 2, r * 2); }
      // each drum's own sign flares when that drum plays
      const da = pic(F.key + 'drums'); if (da.ok && m.drums) for (const d of m.drums) { const a = pulse[d[0]]; if (a < .03) continue; g.globalAlpha = Math.min(1, a * 1.15); g.drawImage(da.img, d[1] * 2, d[2] * 2, d[3] * 2, d[4] * 2, X(d[5]), Y(d[6]), d[7] * s, d[8] * s); }
      g.globalAlpha = 1;
      // the ghost drummer, a hologram standing over the far end of the avenue: sticks down on the kick and the snare
      const hp = META.holo, ha = pic('holo'); if (hp && ha.ok && m.holo) { const [hx, hy, hh] = m.holo, u = hh / 54 * s, bob = still ? 0 : Math.sin(t * 1.7) * 1.1, fl = still ? .92 : .82 + .1 * Math.sin(t * 23) * Math.sin(t * 3.1) + kick * .18, x0 = X(hx) - 32 * u, y0 = Y(hy) - 37 * u + bob * u;
        const part = (n, dx = 0, dy = 0) => { const q = hp.parts[n]; g.drawImage(ha.img, q[0], q[1], q[2], q[3], x0 + (q[4] + dx) * u, y0 + (q[5] + dy) * u, q[6] * u, q[7] * u); };
        const swing = (n, side, px, py) => { const k = (performance.now() - (side ? hitAt[20] : hitAt[21])) / 190; let ang = k < 1 ? (k < .3 ? 30 * (k / .3) : 30 * (1 - (k - .3) / .7)) : (still ? -4 : Math.sin(t * 2.4 + side * Math.PI) * 4 - 6);
          g.save(); g.translate(x0 + px * u, y0 + py * u); g.rotate((side ? -ang : ang) * Math.PI / 180); g.translate(-(x0 + px * u), -(y0 + py * u)); part(n); g.restore(); };
        { const cx = X(hx), cy = Y(hy), r = hh * .62 * s, gr = g.createRadialGradient(cx, cy, r * .2, cx, cy, r); gr.addColorStop(0, 'rgba(10,4,34,.5)'); gr.addColorStop(1, 'rgba(10,4,34,0)'); g.globalCompositeOperation = 'source-over'; g.fillStyle = gr; g.fillRect(cx - r, cy - r, r * 2, r * 2); g.globalCompositeOperation = 'lighter'; }
        g.globalAlpha = clamp(fl * .78, 0, 1); part('drum', 0, -bob); part('body'); swing('arm0', 0, 16, 35); swing('arm1', 1, 48, 35);
        const fa = Math.max(kick, snare); if (fa > .04) { g.globalAlpha = Math.min(.7, fa * .7); part('flash', 0, -bob); }
        g.globalAlpha = 1; }
      g.restore();
      if (still) return;
      // rain
      g.save(); g.lineCap = 'round'; g.lineWidth = 1; const sl = .2;
      for (const d of drops) { d.y += d.v * dt; d.x += d.v * dt * sl; if (d.y > vh + 30) { d.y = -30; d.x = Math.random() * (vw + 200) - 200; }
        g.strokeStyle = `rgba(196,220,255,${d.a})`; g.beginPath(); g.moveTo(d.x, d.y); g.lineTo(d.x - d.l * sl, d.y - d.l); g.stroke(); }
      g.restore();
    },
    anime(F, t, dt, still) {
      const {s, ox, oy, m} = F, X = x => ox + x * s, Y = y => oy + y * s, sp = META.animeSpr; if (!sp) return;
      g.save();
      // the windmill turns with the tempo
      const sa = pic('sails'); if (sa.ok && m.mill) { const d = m.mill[2] * 2.2 * s; g.save(); g.translate(X(m.mill[0]), Y(m.mill[1])); g.rotate(still ? .3 : t * (playing ? effBpm() / 60 * .4 : .22)); g.drawImage(sa.img, -d / 2, -d / 2, d, d); g.restore(); }
      // twelve little spirits on the hill, one per drum: each hops when its drum plays
      const si = pic('spirits'); if (si.ok && m.spots) { const [cw2, ch2] = sp.spirit; for (const q of m.spots) { const i = q[0], p = still ? 0 : pulse[i], sz = q[3] * 2.3 * s, hop = p * sz * .55, sq = 1 + p * .16, sway = still ? 0 : Math.sin(t * 2.1 + i * 1.3) * .05;
          g.save(); g.translate(X(q[1]), Y(q[2]) - hop); g.rotate(sway); g.drawImage(si.img, i * cw2, 0, cw2, ch2, -sz / 2 / sq, -sz * ch2 / cw2 * sq * .93, sz / sq, sz * ch2 / cw2 * sq); g.restore();
          if (p > .25) { g.strokeStyle = `rgba(43,29,20,${p})`; g.lineWidth = 1.4 * s; g.lineCap = 'round'; for (const a of [-2.3, -1.57, -.84]) { const r0 = sz * .62, r1 = sz * (.74 + p * .2), cx = X(q[1]), cy = Y(q[2]) - hop - sz * .55; g.beginPath(); g.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0); g.lineTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); g.stroke(); } } } }
      // the big ghost at his taiko on the crown of the hill: sticks down on the kick and the snare, rings off the drum
      const dr = pic('drummer'); if (dr.ok && m.drum) { const [dx, dy, dh] = m.drum, u = dh / 52 * s, bob = still ? 0 : Math.sin(t * 1.8) * .9, x0 = X(dx) - 32 * u, y0 = Y(dy) - 61 * u, now2 = performance.now();
        const part = (n, ddx = 0, ddy = 0, sy = 1) => { const q = sp.parts[n]; g.drawImage(dr.img, q[0], q[1], q[2], q[3], x0 + (q[4] + ddx) * u, y0 + (q[5] + ddy) * u + q[7] * u * (1 - sy), q[6] * u, q[7] * u * sy); };
        const swing = (n, side, px, py) => { const k2 = (now2 - (side ? hitAt[20] : hitAt[21])) / 200; const ang = k2 < 1 ? (k2 < .3 ? 32 * (k2 / .3) : 32 * (1 - (k2 - .3) / .7)) : (still ? -6 : Math.sin(t * 2.2 + side * Math.PI) * 5 - 8);
          g.save(); g.translate(x0 + px * u, y0 + (py + bob) * u); g.rotate((side ? -ang : ang) * Math.PI / 180); g.translate(-(x0 + px * u), -(y0 + (py + bob) * u)); part(n, 0, bob); g.restore(); };
        if (!still) for (const tt of [hitAt[20], hitAt[21]]) { const k2 = (now2 - tt) / 520; if (k2 > 0 && k2 < 1) { g.strokeStyle = `rgba(43,29,20,${.6 * (1 - k2)})`; g.lineWidth = (2.2 - k2 * 1.4) * s; g.beginPath(); g.ellipse(x0 + 32 * u, y0 + 49 * u, (18 + k2 * 44) * u, (5.5 + k2 * 13) * u, 0, 0, Math.PI * 2); g.stroke(); } }
        part('body', 0, bob - 7); part('drum', 0, 0, 1 - Math.max(kick, snare) * .06); swing('arm0', 0, 16, 28); swing('arm1', 1, 48, 28); }
      // petals on the wind (a gust on every kick)
      if (!still) { if (!petals.length) for (let i = 0; i < Math.round(clamp(vw * vh / 26000, 14, 46)); i++) petals.push({x: Math.random() * vw, y: Math.random() * vh, v: 16 + Math.random() * 30, r: Math.random() * 6.3, w: 1 + Math.random() * 2.5, z: 2.4 + Math.random() * 3.4, c: ['#ffd3e2', '#ffffff', '#ffe6a6', '#ffc0d6'][i % 4]});
        const gust = 1 + kick * 3; for (const p of petals) { p.x += p.v * 1.5 * gust * dt; p.y += (p.v * .55 + Math.sin(t * p.w + p.r) * 14) * dt; p.r += p.w * dt * gust; if (p.x > vw + 12) { p.x = -12; p.y = Math.random() * vh; } if (p.y > vh + 12) { p.y = -12; p.x = Math.random() * vw; }
          g.save(); g.translate(p.x, p.y); g.rotate(p.r); g.globalAlpha = .9; g.fillStyle = p.c; g.beginPath(); g.ellipse(0, 0, p.z, p.z * (.35 + .3 * Math.abs(Math.sin(p.r * 1.7))), 0, 0, Math.PI * 2); g.fill(); g.restore(); } }
      g.restore();
    },
  };
  // ---- drawing
  function drawPlate(F, a) { const o = pic(F.key); if (!o.ok) return false; g.globalAlpha = a; g.drawImage(o.img, F.ox * DPRS, F.oy * DPRS, F.m.W * F.s * DPRS, F.m.H * F.s * DPRS); g.globalAlpha = 1; return true; }
  function draw(now, dt, still) {
    const key = cur + kindOf(), F = place(key) || place(cur + 'L');
    g.setTransform(1, 0, 0, 1, 0, 0);
    if (!F || !pic(F.key).ok) { g.fillStyle = (THEMES[cur] || {}).base || '#05040a'; g.fillRect(0, 0, cv.width, cv.height); return; }
    drawPlate(F, 1);
    if (prev) { const k = 1 - (now - fadeT) / 700; if (k <= 0) prev = null; else { const P = place(prev + kindOf()) || place(prev + 'L'); if (P && pic(P.key).ok) drawPlate(P, k); } }
    g.setTransform(DPRS, 0, 0, DPRS, 0, 0);
    if (!prev && FX[cur]) FX[cur](F, (now - t0) / 1000, dt, still);
    g.setTransform(1, 0, 0, 1, 0, 0);
  }
  const still = () => RM.matches || !!window.__ghostsOff;
  function tick(now) {
    requestAnimationFrame(tick);
    if (HACK || document.hidden || cur === 'orig') return;
    const st = still(), iv = now - last;
    for (let i = 0; i < 16; i++) pulse[i] *= .9; kick *= .88; snare *= .88;
    if (st && !need && !prev) return;
    if (!st && iv < (lite ? 48 : 31)) return;
    const tb = performance.now(); draw(now, Math.min(.1, iv / 1000), st); need = false; last = now;
    if (!st) { const c = performance.now() - tb; slow = c > 24 ? slow + 1 : Math.max(0, slow - 1); if (slow > 40) lite = true; }
  }
  // ---- switching
  const TIP = 'SCENE: switch the look. GHOST CITY (a haunted neon city at night), SPIRIT HILLS (a painted anime world) or ORIGINAL (the Ghost Money look). Only the look changes';
  // HACKER MODE keeps its black page, and the SCENE button is greyed out there (the paintings are hidden, so it would do nothing)
  function hk() { document.documentElement.style.background = HACK ? '#050505' : ((TH && TH.base) || '');
    const b = $('scnBtn'); if (b) { b.disabled = !!HACK; b.title = HACK ? 'SCENE: the painted looks are hidden in HACKER MODE. Press EXIT HACKER to get them back' : TIP; } }
  function apply(quiet) {
    document.body.classList.add('scn'); KEYS.forEach(k => document.body.classList.toggle('sc-' + k, k === cur)); window.__scnOn = cur !== 'orig';
    TH = THEMES[cur] || null; try { rainInit(cw, chh); } catch { /* before the first layout */ }
    const b = $('scnBtn'); if (b) { const sp = document.createElement('span'); sp.className = 'scp'; sp.textContent = 'SCENE: '; b.replaceChildren(sp, LABEL[cur].slice(7)); }   // (the 'SCENE: ' part hides where the name row is short of room)
    const mt = document.querySelector('meta[name="theme-color"]'); if (mt && TH && TH.base) mt.setAttribute('content', TH.base);
    hk(); document.documentElement.style.colorScheme = cur === 'anime' ? 'light' : 'dark';
    try { if (bgSync) bgSync(); } catch { /* fine */ }
    need = true; if (!quiet) toast(TOAST[cur]);
  }
  function set(k, quiet) { if (!KEYS.includes(k) || k === cur) { apply(quiet); return; } prev = (cur === 'orig' || k === 'orig') ? null : cur; fadeT = performance.now(); cur = k;
    try { localStorage.setItem('gb_scene', cur); } catch { /* fine */ } apply(quiet); }
  // ---- the SCENE button (next to GLASS UI / HACKER MODE)
  { const h = document.querySelector('header.top'); if (h) { const b = document.createElement('button'); b.type = 'button'; b.className = 'btn small ghost'; b.id = 'scnBtn';
      b.title = TIP; b.onclick = () => set(KEYS[(KEYS.indexOf(cur) + 1) % KEYS.length]); h.append(b); } }
  addEventListener('resize', resize); addEventListener('orientationchange', resize);
  addEventListener('scroll', () => { try { winLayer.fit(); } catch { /* fine */ } }, {passive: true});   // (a phone: the page scrolls now; the pop-up windows follow the tracker)
  document.addEventListener('visibilitychange', () => { need = true; });
  window.__gmbgSync = () => { need = true; try { if (bgSync) bgSync(); } catch { /* fine */ } };   // GHOST ANIMATION: ON / OFF
  { const sh = setHack; setHack = function (on, q) { sh(on, q); hk(); need = true; }; }
  resize(); apply(true); if (cur !== 'orig') pic(cur + kindOf()); requestAnimationFrame(tick);
  window.__scn = {get cur() { return cur; }, get lite() { return lite; }, set, hit: (i, v) => SCN.hit(i, v)};   // (a hook for the page tests; the app does not use it)
  return {set, get cur() { return cur; }, get fit() { return fit[cur + kindOf()]; },
    hit(i, v) { if (i >= 0 && i < 16) { pulse[i] = 1; hitAt[i] = performance.now(); } const n = INST[i] && INST[i].name, w = performance.now(); if (n === 'KICK' || n === 'SUB') { kick = 1; hitAt[20] = w; } else if (n === 'SNARE' || n === 'CLAP') { snare = 1; hitAt[21] = w; } else if (n === 'CRASH') { kick = snare = 1; hitAt[20] = hitAt[21] = w; } },
    redraw() { need = true; }};
})();
