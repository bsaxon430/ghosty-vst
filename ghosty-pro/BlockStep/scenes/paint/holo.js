// The ghost drummer as a hologram (GHOST CITY). Same drawing as Block Step's logo ghost, turned into light:
// parts on black, added over the painting by the page (body, two arms that swing on the hits, the drum, a flash for its head).
window.main = async () => {
  const {rgba, lin, rad, mk, blurred, TAU} = PT, K = 6, MG = 5;   // px per unit, glow margin (units)
  const BODY = 'M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z';
  const PIX = [[21, 29.5], [25, 29.5], [23, 31.5], [36, 29.5], [40, 29.5], [38, 31.5]];
  const parts = {
    body: {box: [12, 10, 40, 46], draw(g) {
      const P = new Path2D(BODY); g.fillStyle = rad(g, 32, 34, 4, 24, [[0, rgba('#35d8ff', .42)], [.7, rgba('#6ff3ff', .62)], [1, rgba('#c9ffff', .95)]]); g.fill(P);
      g.strokeStyle = '#eaffff'; g.lineWidth = .9; g.lineJoin = 'round'; g.stroke(P);
      g.fillStyle = '#000'; g.fill(new Path2D('M16.6 24 Q32 15 47.4 24 L47.9 28.5 Q32 20 16.1 28.5 Z')); g.fillRect(17, 27, 30, 2.2); g.fillRect(19, 28, 11, 6); g.fillRect(34, 28, 11, 6);
      g.strokeStyle = rgba('#9ff6ff', .9); g.lineWidth = .45; g.strokeRect(19, 28, 11, 6); g.strokeRect(34, 28, 11, 6); g.stroke(new Path2D('M16.6 24 Q32 15 47.4 24 L47.9 28.5 Q32 20 16.1 28.5 Z'));
      g.fillStyle = '#ff7ae0'; PIX.forEach(p => g.fillRect(p[0], p[1], 2, 2));
      g.fillStyle = '#7dffb8'; g.fillRect(44, 20, 2, 7); g.fillRect(43, 18, 4, 2);
      // a little smile under the shades
      g.strokeStyle = rgba('#eaffff', .9); g.lineWidth = .7; g.lineCap = 'round'; g.beginPath(); g.moveTo(28.5, 38); g.quadraticCurveTo(32, 40.6, 35.5, 38); g.stroke(); } },
    arm0: {box: [1, 28, 28, 23], draw(g) { arm(g, 0); } },
    arm1: {box: [35, 28, 28, 23], draw(g) { arm(g, 1); } },
    drum: {box: [12, 44, 40, 19], draw(g) {
      g.fillStyle = rgba('#2a8cff', .35); g.beginPath(); g.ellipse(32, 57, 16, 3.4, 0, 0, TAU); g.fill(); g.fillStyle = rgba('#3aa0ff', .4); g.fillRect(16, 49, 32, 8);
      g.strokeStyle = '#8dffc0'; g.lineWidth = .8; g.lineJoin = 'round'; g.beginPath(); [[16, 50.5], [20, 55.5], [24, 50.5], [28, 55.5], [32, 50.5], [36, 55.5], [40, 50.5], [44, 55.5], [48, 50.5]].forEach((p, i) => i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1])); g.stroke();
      g.fillStyle = '#ff7ae0'; g.fillRect(16, 49, 32, 1.2); g.fillRect(16, 55.8, 32, 1.2);
      g.strokeStyle = '#c9f6ff'; g.lineWidth = .6; g.beginPath(); g.ellipse(32, 57, 16, 3.4, 0, 0, Math.PI); g.stroke(); g.beginPath(); g.moveTo(16, 49); g.lineTo(16, 57); g.moveTo(48, 49); g.lineTo(48, 57); g.stroke();
      g.fillStyle = rgba('#d8fbff', .7); g.beginPath(); g.ellipse(32, 49, 16, 3.4, 0, 0, TAU); g.fill(); g.strokeStyle = '#ff9ae8'; g.lineWidth = .7; g.stroke(); } },
    flash: {box: [8, 40, 48, 18], raw: true, draw(g) { g.fillStyle = rad(g, 0, 0, 0, 1, [[0, rgba('#ffffff', 1)], [.4, rgba('#b8fff0', .7)], [1, rgba('#7dffd8', 0)]]); g.save(); g.translate(32, 49); g.scale(22, 7.5); g.beginPath(); g.arc(0, 0, 1, 0, TAU); g.fill(); g.restore(); } },
  };
  function arm(g, side) { const Lf = side === 0, hx = Lf ? 9 : 55, tx = Lf ? 26 : 38;
    g.fillStyle = rgba('#8ff6ff', .8); g.fillRect(Lf ? 7 : 47, 33, 10, 5); g.fillRect(Lf ? 5 : 54, 31.5, 5, 6);
    g.strokeStyle = '#eaffff'; g.lineWidth = .5; g.strokeRect(Lf ? 7 : 47, 33, 10, 5); g.strokeRect(Lf ? 5 : 54, 31.5, 5, 6);
    g.strokeStyle = '#fff3c4'; g.lineWidth = 2; g.lineCap = 'square'; g.beginPath(); g.moveTo(hx, 33); g.lineTo(tx, 46); g.stroke();
    g.fillStyle = Lf ? '#7dffb8' : '#ff7ae0'; g.fillRect(tx - 1.6, 44.6, 3.2, 3.2); }
  // lay the parts out in one sheet
  let ax = 0; const names = Object.keys(parts), W = names.reduce((a, n) => a + (parts[n].box[2] + MG * 2) * K + 2, 0), H = Math.max(...names.map(n => (parts[n].box[3] + MG * 2) * K));
  const A = mk(W, H); A.g.fillStyle = '#000'; A.g.fillRect(0, 0, W, H); const meta = {K, MG, parts: {}};
  for (const n of names) { const p = parts[n], [bx, by, bw, bh] = p.box, cw = (bw + MG * 2) * K, ch = (bh + MG * 2) * K, t = mk(cw, ch), g = t.g;
    g.fillStyle = '#000'; g.fillRect(0, 0, cw, ch); g.save(); g.scale(K, K); g.translate(MG - bx, MG - by); p.draw(g); g.restore();
    if (!p.raw) {
      // tint (cyan up top, spirit green below), scan lines, a soft bloom, a faint colour fringe
      g.save(); g.globalCompositeOperation = 'multiply'; g.fillStyle = lin(g, 0, 0, 0, ch, [[0, '#b8f4ff'], [.55, '#a8ffe8'], [1, '#a0ffc8']]); g.fillRect(0, 0, cw, ch);
      g.fillStyle = 'rgba(0,0,0,.42)'; for (let y = 0; y < ch; y += 4) g.fillRect(0, y, cw, 1.6); g.restore();
      const b = blurred(t.c, K * 1.1), b2 = blurred(t.c, K * .35); g.save(); g.globalCompositeOperation = 'lighter'; g.globalAlpha = .75; g.drawImage(b, 0, 0); g.globalAlpha = .35; g.drawImage(b2, 0, 0);
      g.globalAlpha = .16; g.drawImage(t.c, -K * .35, 0); g.restore(); }
    // fade to nothing at the cell's edge (so nothing shows where the sprite ends)
    g.save(); g.globalCompositeOperation = 'multiply'; const e = K * 1.2; for (const [x0, y0, x1, y1] of [[0, 0, e, 0], [cw, 0, cw - e, 0], [0, 0, 0, e], [0, ch, 0, ch - e]]) { g.fillStyle = lin(g, x0, y0, x1, y1, [[0, '#000'], [1, '#fff']]); if (y0 === y1) g.fillRect(Math.min(x0, x1), 0, e, ch); else g.fillRect(0, Math.min(y0, y1), cw, e); } g.restore();
    A.g.drawImage(t.c, ax, 0); meta.parts[n] = [ax, 0, cw, ch, bx - MG, by - MG, bw + MG * 2, bh + MG * 2]; ax += cw + 2; }
  out['add-holo'] = A.c; window.meta = meta;
};
