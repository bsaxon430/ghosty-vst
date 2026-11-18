// SPIRIT HILLS — the moving pictures (cut-outs with soft edges, drawn over the painting by the page):
// the big ghost drummer at his taiko (body, two arms, the drum), twelve little spirits (one per drum, in that drum's
// colour), and the windmill's sails. Flat cel colour, two tones of shade, hand-inked outlines.
window.main = async () => {
  const {rgba, lin, rad, mk, ink, inkPoly, poly, TAU, R, lighten, darken, mix} = PT, INK = '#2b1d14', K = 6, MG = 4;
  const BODY = 'M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z';
  const sample = (d, n) => { const sv = document.createElementNS('http://www.w3.org/2000/svg', 'svg'), p = document.createElementNS('http://www.w3.org/2000/svg', 'path'); p.setAttribute('d', d); sv.appendChild(p); document.body.appendChild(sv); const L = p.getTotalLength(), o = []; for (let i = 0; i <= n; i++) { const q = p.getPointAtLength(L * i / n); o.push([q.x, q.y]); } sv.remove(); return o; };
  const bodyPts = sample(BODY, 140);
  const inkPath = (g, pts, w, seed, o = {}) => ink(g, pts, Object.assign({w, color: INK, wob: .16, seed, taper: .04, step: .7, press: .3}, o));
  // one ghost in cel colour: base, a shade tone down the right side and under the brow, a rim of light on the left
  function ghost(g, base, shade, o = {}) {
    const P = new Path2D(BODY); g.save(); g.fillStyle = base; g.fill(P); g.clip(P);
    g.fillStyle = shade; g.beginPath(); g.moveTo(40, 12); g.bezierCurveTo(47, 24, 43, 40, 44, 56); g.lineTo(52, 56); g.lineTo(52, 12); g.fill();
    g.beginPath(); g.ellipse(32, 55, 20, 6.5, 0, 0, TAU); g.fill();
    g.fillStyle = rgba('#ffffff', .75); g.beginPath(); g.ellipse(22, 24, 3.2, 6.5, .5, 0, TAU); g.fill();
    if (o.hem) { g.fillStyle = o.hem; g.beginPath(); g.moveTo(14, 47); for (let x = 14; x <= 50; x += 3) g.quadraticCurveTo(x + 1.5, 44 + ((x / 3) % 2 ? 2.4 : -.6), x + 3, 47); g.lineTo(50, 58); g.lineTo(14, 58); g.fill();
      g.fillStyle = darken(o.hem, .22); g.beginPath(); g.moveTo(40, 47); g.lineTo(50, 46); g.lineTo(50, 58); g.lineTo(42, 58); g.fill(); }
    g.restore(); inkPath(g, bodyPts, o.lw || 1.25, o.seed || 3);
  }
  const face = (g, o = {}) => { const eye = (x) => { g.fillStyle = '#2a2350'; g.beginPath(); g.ellipse(x, 31.2, 2.5, 3.5, 0, 0, TAU); g.fill(); g.fillStyle = '#fff'; g.beginPath(); g.arc(x - .8, 29.7, 1, 0, TAU); g.fill(); g.beginPath(); g.arc(x + .9, 32.6, .45, 0, TAU); g.fill(); };
    if (o.happy) { g.strokeStyle = INK; g.lineWidth = 1.1; g.lineCap = 'round'; for (const x of [26.5, 37.5]) { g.beginPath(); g.moveTo(x - 2.4, 31.6); g.quadraticCurveTo(x, 28.6, x + 2.4, 31.6); g.stroke(); } } else { eye(26.5); eye(37.5); }
    g.fillStyle = rgba('#ff8fa8', .7); g.beginPath(); g.ellipse(21.6, 36.2, 2.7, 1.6, 0, 0, TAU); g.ellipse(42.4, 36.2, 2.7, 1.6, 0, 0, TAU); g.fill();
    g.strokeStyle = INK; g.lineWidth = .9; g.lineCap = 'round'; g.beginPath(); g.moveTo(29.4, 37.2); g.quadraticCurveTo(32, 39.8, 34.6, 37.2); g.stroke(); };
  const parts = {
    body: {box: [10, 6, 50, 52], draw(g) {
      ghost(g, '#fffdf7', '#cfd6ee', {hem: '#74c05a', seed: 5});
      // hachimaki: a vermilion headband, knotted on the right, two tails in the wind
      g.fillStyle = '#d6452b'; g.beginPath(); g.moveTo(16.4, 23.6); g.quadraticCurveTo(32, 15.6, 47.6, 23.6); g.lineTo(47.9, 27.2); g.quadraticCurveTo(32, 19.4, 16.1, 27.2); g.fill();
      g.fillStyle = '#b5341f'; g.beginPath(); g.moveTo(40, 19.2); g.quadraticCurveTo(45, 21.4, 47.6, 23.6); g.lineTo(47.9, 27.2); g.quadraticCurveTo(44.5, 24.2, 40.4, 22.6); g.fill();
      g.fillStyle = '#d6452b'; g.beginPath(); g.moveTo(47, 24); g.quadraticCurveTo(54, 20, 58, 14); g.quadraticCurveTo(56, 22, 49.5, 27); g.fill(); g.beginPath(); g.moveTo(47.4, 25.4); g.quadraticCurveTo(55, 26, 59, 22.5); g.quadraticCurveTo(56, 29.5, 48.6, 28.4); g.fill();
      g.beginPath(); g.arc(47.6, 25.4, 2, 0, TAU); g.fill();
      inkPath(g, [[16.4, 23.6], [24, 20], [32, 18.8], [40, 20], [47.6, 23.6]], .9, 11); inkPath(g, [[16.1, 27.2], [24, 23.6], [32, 22.5], [40, 23.6], [47.9, 27.2]], .9, 12);
      inkPath(g, [[47, 24], [53, 20.5], [58, 14], [55.6, 21.8], [49.5, 27]], .8, 13); inkPath(g, [[48.6, 28.4], [55, 28.2], [59, 22.5]], .8, 14);
      face(g); } },
    arm0: {box: [0, 26, 30, 26], draw(g) { arm(g, 0); } },
    arm1: {box: [34, 26, 30, 26], draw(g) { arm(g, 1); } },
    drum: {box: [10, 40, 44, 26], draw(g) {
      // an X stand, then the barrel lying toward us: wooden body, cream head, a ring of studs
      g.strokeStyle = '#6b4526'; g.lineWidth = 2.4; g.lineCap = 'round'; g.beginPath(); g.moveTo(19, 63); g.lineTo(41, 50); g.moveTo(45, 63); g.lineTo(23, 50); g.stroke();
      g.strokeStyle = INK; g.lineWidth = .6; g.beginPath(); g.moveTo(17.8, 63.6); g.lineTo(40, 50.4); g.moveTo(46.2, 63.6); g.lineTo(24, 50.4); g.stroke();
      g.fillStyle = '#a9643a'; g.beginPath(); g.ellipse(32, 54.5, 17.5, 6.6, 0, 0, TAU); g.fill(); g.fillRect(14.5, 48.5, 35, 6);
      g.fillStyle = '#8a4d2a'; g.beginPath(); g.moveTo(40, 48.5); g.lineTo(49.5, 48.5); g.lineTo(49.5, 54.5); g.quadraticCurveTo(47, 59.4, 40, 60.6); g.fill();
      g.fillStyle = '#c98652'; g.fillRect(16, 49, 3.4, 6.4);
      g.fillStyle = '#3a2416'; for (let i = 0; i < 11; i++) { const a = Math.PI * (i + .5) / 11, x = 32 - Math.cos(a) * 16.4, y = 50.6 + Math.sin(a) * 5.4; g.beginPath(); g.arc(x, y, .75, 0, TAU); g.fill(); }
      g.fillStyle = '#fbf3df'; g.beginPath(); g.ellipse(32, 48.5, 17.5, 5.4, 0, 0, TAU); g.fill(); g.fillStyle = rgba('#d9c7a2', .8); g.beginPath(); g.ellipse(35, 49.6, 12, 3.2, 0, 0, TAU); g.fill();
      g.fillStyle = '#fbf3df'; g.beginPath(); g.ellipse(31, 48.4, 11, 2.9, 0, 0, TAU); g.fill();
      // the crest on the head: a tomoe (three commas)
      g.fillStyle = '#d6452b'; for (let i = 0; i < 3; i++) { const a = i * TAU / 3 - .5; g.beginPath(); g.ellipse(32 + Math.cos(a) * 3.1, 48.4 + Math.sin(a) * 1.05, 2, .72, a * .3, 0, TAU); g.fill(); }
      const el = (cy, ry, a0, a1) => { const o = []; for (let i = 0; i <= 40; i++) { const a = a0 + (a1 - a0) * i / 40; o.push([32 + Math.cos(a) * 17.5, cy + Math.sin(a) * ry]); } return o; };
      inkPath(g, el(48.5, 5.4, 0, TAU), 1.05, 21); inkPath(g, el(54.5, 6.6, 0, Math.PI), 1.05, 22); inkPath(g, [[14.5, 48.5], [14.5, 54.5]], 1, 23); inkPath(g, [[49.5, 48.5], [49.5, 54.5]], 1, 24); } },
  };
  function arm(g, side) { const Lf = side === 0, sx = Lf ? 16 : 48, hx = Lf ? 7.5 : 56.5, tx = Lf ? 26 : 38;
    // the stick first (it passes behind the hand)
    g.strokeStyle = '#d9b06a'; g.lineWidth = 2.3; g.lineCap = 'round'; g.beginPath(); g.moveTo(hx, 33.5); g.lineTo(tx, 46.5); g.stroke(); g.strokeStyle = INK; g.lineWidth = .55; const dx = (tx - hx), dy = 13, n = Math.hypot(dx, dy), ox = -dy / n * 1.2, oy = dx / n * 1.2; g.beginPath(); g.moveTo(hx + ox, 33.5 + oy); g.lineTo(tx + ox, 46.5 + oy); g.moveTo(hx - ox, 33.5 - oy); g.lineTo(tx - ox, 46.5 - oy); g.stroke();
    // a stubby arm and a round hand
    const pts = Lf ? [[17, 32], [10, 31.4], [6.4, 33.2], [6.8, 37], [11, 38.6], [17, 38]] : [[47, 32], [54, 31.4], [57.6, 33.2], [57.2, 37], [53, 38.6], [47, 38]];
    g.fillStyle = '#fffdf7'; poly(g, pts); g.fill(); g.fillStyle = '#cfd6ee'; g.beginPath(); g.moveTo(pts[3][0], pts[3][1]); g.lineTo(pts[4][0], pts[4][1]); g.lineTo(pts[5][0], pts[5][1]); g.lineTo(pts[5][0], 36); g.fill();
    inkPath(g, pts, 1.05, 31 + side * 7); }
  // ---- sheet 1: the drummer's parts
  let ax = 0; const names = Object.keys(parts), W = names.reduce((a, n) => a + (parts[n].box[2] + MG * 2) * K + 2, 0), H = Math.max(...names.map(n => (parts[n].box[3] + MG * 2) * K));
  const A = mk(W, H), meta = {K, MG, parts: {}};
  for (const n of names) { const p = parts[n], [bx, by, bw, bh] = p.box, cw = (bw + MG * 2) * K, ch = (bh + MG * 2) * K; A.g.save(); A.g.translate(ax, 0); A.g.beginPath(); A.g.rect(0, 0, cw, ch); A.g.clip(); A.g.scale(K, K); A.g.translate(MG - bx, MG - by); p.draw(A.g); A.g.restore();
    meta.parts[n] = [ax, 0, cw, ch, bx - MG, by - MG, bw + MG * 2, bh + MG * 2]; ax += cw + 2; }
  out['spr-drummer'] = A.c;
  // ---- sheet 2: twelve little spirits, one per drum, in that drum's gouache colour (the last has its eyes shut: FX)
  const GOU = ['#4fb06c', '#8a6fd6', '#e486ab', '#5a9ade', '#43b6c4', '#e8963f', '#e3bb3a', '#de5c52', '#c070cc', '#b9b1a0', '#e6cc74', '#84c548'], KS = 2.4, cell = 52 * KS;
  const Sp = mk(cell * 12, 54 * KS); for (let i = 0; i < 12; i++) { const g = Sp.g; g.save(); g.translate(i * cell, 0); g.scale(KS, KS); g.translate(-6, -8);
    ghost(g, lighten(GOU[i], .28), mix(GOU[i], '#3a2a50', .18), {lw: 1.7, seed: 40 + i}); g.fillStyle = rgba('#fff', .5); g.beginPath(); g.ellipse(32, 44, 9, 5, 0, 0, TAU); g.fill(); face(g, {happy: i % 3 === 1});
    // a leaf on its head
    g.fillStyle = '#5faa46'; g.beginPath(); g.moveTo(32, 15.5); g.quadraticCurveTo(27, 8, 34, 7); g.quadraticCurveTo(38, 11, 32, 15.5); g.fill(); g.strokeStyle = INK; g.lineWidth = 1; g.stroke(); g.restore(); }
  out['spr-spirits'] = Sp.c; meta.spirit = [cell, 54 * KS, 12];
  // ---- sheet 3: the windmill's sails (four lattice blades on a hub), centred
  const Z = 260, M = mk(Z, Z), g = M.g; g.translate(Z / 2, Z / 2);
  for (let b = 0; b < 4; b++) { g.save(); g.rotate(b * Math.PI / 2 + .3);
    g.fillStyle = '#7a5234'; g.fillRect(-3, -Z * .47, 6, Z * .47); g.fillStyle = rgba('#fff7e4', .92); g.fillRect(3, -Z * .46, Z * .1, Z * .33);
    g.strokeStyle = '#7a5234'; g.lineWidth = 2.2; g.strokeRect(3, -Z * .46, Z * .1, Z * .33); for (let i = 1; i < 6; i++) { g.beginPath(); g.moveTo(3, -Z * .46 + Z * .33 * i / 6); g.lineTo(3 + Z * .1, -Z * .46 + Z * .33 * i / 6); g.stroke(); } g.beginPath(); g.moveTo(3 + Z * .05, -Z * .46); g.lineTo(3 + Z * .05, -Z * .13); g.stroke();
    g.strokeStyle = INK; g.lineWidth = 2.4; g.strokeRect(-3, -Z * .47, 6, Z * .47); g.strokeRect(3, -Z * .46, Z * .1, Z * .33); g.restore(); }
  g.fillStyle = '#c9472f'; g.beginPath(); g.arc(0, 0, 10, 0, TAU); g.fill(); g.strokeStyle = INK; g.lineWidth = 2.4; g.stroke();
  out['spr-sails'] = M.c; window.meta = meta;
};
