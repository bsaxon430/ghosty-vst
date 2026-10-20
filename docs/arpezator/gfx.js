'use strict';
// ArpEZator drawings, all canvas paths: Professor Bernstein (a little ghost professor in a lab coat, chilling in a
// hammock and swirling a bubbling green vial) and the mood ghost whose face follows the progression.

function gEll(c, x, y, rx, ry, rot = 0) { c.beginPath(); c.ellipse(x, y, Math.abs(rx), Math.abs(ry), rot, 0, Math.PI * 2); }
function gSeed(seed) { let s = seed >>> 0; return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// ---------------------------------------------------------------- Professor Bernstein (logo), drawn in a 100x100 box
function drawProfessor(c, X, Y, W, t) {
  const s = W / 100;
  c.save(); c.translate(X, Y); c.scale(s, s);
  c.beginPath(); c.roundRect ? c.roundRect(0, 0, 100, 100, 18) : c.rect(0, 0, 100, 100); c.clip();
  let g = c.createLinearGradient(0, 0, 0, 100); g.addColorStop(0, '#2a1650'); g.addColorStop(1, '#0d0820'); c.fillStyle = g; c.fillRect(0, 0, 100, 100);
  g = c.createRadialGradient(66, 26, 2, 66, 26, 40); g.addColorStop(0, 'rgba(57,255,154,0.28)'); g.addColorStop(1, 'rgba(57,255,154,0)'); c.fillStyle = g; c.fillRect(0, 0, 100, 100);
  const R = gSeed(3);
  for (let i = 0; i < 14; i++) { const x = R() * 100, y = R() * 45, tw = 0.5 + 0.5 * Math.sin(t * 2.3 + i * 1.9); c.fillStyle = `rgba(230,220,255,${0.2 + 0.6 * tw})`; c.beginPath(); c.arc(x, y, 0.5 + 0.7 * tw, 0, 7); c.fill(); }
  // ground
  c.fillStyle = 'rgba(155,92,255,0.25)'; gEll(c, 50, 99, 60, 9); c.fill();
  // posts
  for (const x of [9, 91]) { g = c.createLinearGradient(x - 3, 0, x + 3, 0); g.addColorStop(0, '#6b3fc4'); g.addColorStop(1, '#3b2178'); c.fillStyle = g; c.beginPath(); c.roundRect ? c.roundRect(x - 2.6, 24, 5.2, 80, 2.6) : c.rect(x - 2.6, 24, 5.2, 80); c.fill();
    c.fillStyle = '#39ff9a'; c.beginPath(); c.arc(x, 24, 3.4, 0, 7); c.fill(); c.fillStyle = 'rgba(255,255,255,.6)'; c.beginPath(); c.arc(x - 1, 23, 1.1, 0, 7); c.fill(); }
  // everything hanging sways around the middle between the posts
  const sway = Math.sin(t * 1.25) * 0.05;
  c.save(); c.translate(50, 26); c.rotate(sway); c.translate(-50, -26);
  c.strokeStyle = '#d9c7ff'; c.lineWidth = 0.9; c.lineCap = 'round';
  for (const [a, b] of [[[9, 27], [19, 52]], [[9, 27], [22, 54]], [[91, 27], [81, 52]], [[91, 27], [78, 54]]]) { c.beginPath(); c.moveTo(...a); c.lineTo(...b); c.stroke(); }
  // the arm behind the head (lab-coat elbow)
  c.lineJoin = 'round';
  c.fillStyle = '#eafff4'; c.strokeStyle = '#1d1238'; c.lineWidth = 1.2;
  gEll(c, 24, 41, 6, 5, -0.5); c.fill(); c.stroke();
  // ghost head (tilted back, relaxed)
  c.save(); c.translate(37, 45); c.rotate(-0.22);
  g = c.createRadialGradient(-4, -5, 2, 0, 0, 15); g.addColorStop(0, '#ffffff'); g.addColorStop(1, '#d9ccff'); c.fillStyle = g;
  gEll(c, 0, 0, 13, 12.5); c.fill(); c.lineWidth = 1.2; c.strokeStyle = '#1d1238'; c.stroke();
  // professor hair: wild little white tufts
  c.fillStyle = '#ffffff'; c.strokeStyle = '#1d1238'; c.lineWidth = 0.9;
  for (const [x, y, r] of [[-12, -6, 3.6], [-13.5, -1, 3], [-11, -10, 3], [11, -9, 3.2], [13, -4, 3], [-6, -12.5, 2.6], [2, -13.4, 2.4]]) { gEll(c, x + Math.sin(t * 3 + x) * 0.25, y, r, r * 0.85); c.fill(); c.stroke(); }
  gEll(c, 0, 0.5, 12.2, 11.6); c.fillStyle = g; c.fill();
  // cheeks
  c.fillStyle = 'rgba(255,120,190,0.55)'; gEll(c, -7.5, 4.5, 2.8, 1.8); c.fill(); gEll(c, 7.5, 4.5, 2.8, 1.8); c.fill();
  // happy closed eyes + round glasses
  c.strokeStyle = '#1d1238'; c.lineWidth = 1.3;
  for (const ex of [-4.8, 4.8]) { c.beginPath(); c.arc(ex, 0.6, 2.2, Math.PI * 1.1, Math.PI * 1.9); c.stroke(); }
  c.lineWidth = 0.9; c.strokeStyle = '#3d2a6e';
  for (const ex of [-4.8, 4.8]) { c.beginPath(); c.arc(ex, -0.2, 4.2, 0, 7); c.stroke(); c.fillStyle = 'rgba(160,220,255,0.18)'; c.fill(); }
  c.beginPath(); c.moveTo(-0.8, -0.6); c.quadraticCurveTo(0, -1.6, 0.8, -0.6); c.stroke();
  c.strokeStyle = 'rgba(255,255,255,.8)'; c.lineWidth = 0.6; c.beginPath(); c.arc(-5.6, -1.2, 2.6, 3.6, 4.4); c.stroke(); c.beginPath(); c.arc(4, -1.2, 2.6, 3.6, 4.4); c.stroke();
  // smile
  c.strokeStyle = '#1d1238'; c.lineWidth = 1.1; c.beginPath(); c.arc(0, 5.2, 2.6, 0.2, Math.PI - 0.2); c.stroke();
  c.restore();
  // the raised arm (sleeve) holding the vial
  const swirl = Math.sin(t * 2.6) * 0.22;
  const hx = 61 + Math.sin(t * 2.6) * 1.2, hy = 37 + Math.cos(t * 2.6) * 0.8;
  c.lineCap = 'round'; c.strokeStyle = '#1d1238'; c.lineWidth = 8.4; c.beginPath(); c.moveTo(48, 58); c.quadraticCurveTo(57, 52, hx, hy + 3); c.stroke();
  c.strokeStyle = '#eafff4'; c.lineWidth = 6.2; c.beginPath(); c.moveTo(48, 58); c.quadraticCurveTo(57, 52, hx, hy + 3); c.stroke();
  // vial (erlenmeyer flask)
  c.save(); c.translate(hx, hy); c.rotate(swirl);
  const glow = 0.55 + 0.25 * Math.sin(t * 4);
  g = c.createRadialGradient(0, -6, 1, 0, -6, 16); g.addColorStop(0, `rgba(57,255,154,${0.45 * glow})`); g.addColorStop(1, 'rgba(57,255,154,0)'); c.fillStyle = g; c.fillRect(-18, -24, 36, 36);
  const flask = () => { c.beginPath(); c.moveTo(-1.8, -16); c.lineTo(-1.8, -10); c.lineTo(-7, -1); c.quadraticCurveTo(-8, 1.5, -5, 1.5); c.lineTo(5, 1.5); c.quadraticCurveTo(8, 1.5, 7, -1); c.lineTo(1.8, -10); c.lineTo(1.8, -16); c.closePath(); };
  flask(); c.fillStyle = 'rgba(220,240,255,0.25)'; c.fill();
  c.save(); flask(); c.clip();
  const lvl = -6.5, tilt = -swirl * 6;
  c.beginPath(); c.moveTo(-9, lvl - tilt); for (let x = -9; x <= 9; x += 1) c.lineTo(x, lvl + Math.sin(x * 0.9 + t * 7) * 0.5 - tilt * x / 9); c.lineTo(9, 3); c.lineTo(-9, 3); c.closePath();
  g = c.createLinearGradient(0, -8, 0, 2); g.addColorStop(0, '#7dffbf'); g.addColorStop(1, '#14c76a'); c.fillStyle = g; c.fill();
  for (let i = 0; i < 4; i++) { const ph = (t * 1.6 + i * 0.27) % 1; c.fillStyle = 'rgba(255,255,255,.7)'; c.beginPath(); c.arc(-3 + i * 2 + Math.sin(t * 5 + i) * 0.6, 1 - ph * 7, 0.55, 0, 7); c.fill(); }
  c.restore();
  flask(); c.strokeStyle = '#1d1238'; c.lineWidth = 0.9; c.stroke();
  c.fillStyle = 'rgba(255,255,255,0.55)'; c.fillRect(-4.6, -3.5, 1, 3);
  c.strokeStyle = '#1d1238'; c.lineWidth = 1; c.beginPath(); c.moveTo(-2.8, -16); c.lineTo(2.8, -16); c.stroke();
  c.restore();
  // hand
  c.fillStyle = '#ffffff'; c.strokeStyle = '#1d1238'; c.lineWidth = 0.9; gEll(c, hx, hy + 1.2, 3, 2.6); c.fill(); c.stroke();
  // bubbles rising out of the vial
  for (let i = 0; i < 6; i++) {
    const ph = (t * 0.55 + i / 6) % 1, x = hx + Math.sin(swirl) * 16 + Math.sin(t * 2 + i * 2.1) * (2 + ph * 3), y = hy - 17 - ph * 26, r = 0.8 + ph * 1.9;
    c.strokeStyle = `rgba(125,255,191,${0.9 * (1 - ph)})`; c.lineWidth = 0.7; c.beginPath(); c.arc(x, y, r, 0, 7); c.stroke();
    c.fillStyle = `rgba(57,255,154,${0.22 * (1 - ph)})`; c.fill();
  }
  // hammock bed (striped), in front of the ghost's body
  const bed = () => { c.beginPath(); c.moveTo(18, 51); c.quadraticCurveTo(50, 70, 82, 51); c.quadraticCurveTo(50, 90, 18, 51); c.closePath(); };
  c.save(); bed(); c.clip();
  for (let i = -12; i < 20; i++) { c.fillStyle = i % 2 ? '#39ff9a' : '#9b5cff'; c.beginPath(); const x = i * 6; c.moveTo(x, 40); c.lineTo(x + 6, 40); c.lineTo(x + 18, 95); c.lineTo(x + 12, 95); c.closePath(); c.fill(); }
  c.fillStyle = 'rgba(13,8,32,0.28)'; c.fillRect(0, 66, 100, 30);
  c.restore();
  bed(); c.strokeStyle = '#1d1238'; c.lineWidth = 1.3; c.stroke();
  c.strokeStyle = 'rgba(255,255,255,.55)'; c.lineWidth = 1; c.beginPath(); c.moveTo(19, 51.5); c.quadraticCurveTo(50, 70, 81, 51.5); c.stroke();
  // lab-coat collar under the chin
  c.fillStyle = '#eafff4'; c.strokeStyle = '#1d1238'; c.lineWidth = 1;
  c.beginPath(); c.moveTo(28, 54); c.lineTo(36, 52); c.lineTo(40, 57.5); c.lineTo(45, 52); c.lineTo(51, 54.5); c.quadraticCurveTo(40, 61, 28, 54); c.closePath(); c.fill(); c.stroke();
  c.fillStyle = '#9b5cff'; c.beginPath(); c.moveTo(37.6, 55); c.lineTo(40, 57.8); c.lineTo(42.4, 55); c.lineTo(40, 54.2); c.closePath(); c.fill();   // tiny bow tie
  // ghost tail draping over the right edge
  const wig = Math.sin(t * 2) * 1.2;
  c.fillStyle = '#ffffff'; c.strokeStyle = '#1d1238'; c.lineWidth = 1.1;
  c.beginPath(); c.moveTo(70, 56); c.quadraticCurveTo(80, 50, 85, 54); c.quadraticCurveTo(89, 58 + wig, 92, 57 + wig); c.quadraticCurveTo(90, 62 + wig, 86, 61); c.quadraticCurveTo(84, 64, 80, 61.5); c.quadraticCurveTo(76, 62, 73, 60); c.closePath(); c.fill(); c.stroke();
  c.restore();
  c.restore();
}

// ---------------------------------------------------------------- the mood ghost
const FACES = {
  happy: { eye: 1, smileEye: 1, brow: 0, mouth: 1, open: 0.45, wave: 0, blush: 1, tear: 0, sweat: 0, spark: 0.4, dream: 0, hue: [255, 214, 74] },
  dreamy: { eye: 0.35, smileEye: 0.2, brow: -0.25, mouth: 0.45, open: 0, wave: 0, blush: 0.7, tear: 0, sweat: 0, spark: 0.2, dream: 1, hue: [180, 140, 255] },
  sad: { eye: 0.75, smileEye: 0, brow: -1, mouth: -0.85, open: 0.05, wave: 0, blush: 0.1, tear: 1, sweat: 0, spark: 0, dream: 0, hue: [95, 168, 255] },
  tense: { eye: 1.3, smileEye: 0, brow: -0.85, mouth: -0.15, open: 0.2, wave: 1, blush: 0, tear: 0, sweat: 1, spark: 0, dream: 0, hue: [255, 96, 96] },
  epic: { eye: 0.95, smileEye: 0, brow: 1, mouth: 0.9, open: 0.9, wave: 0, blush: 0.35, tear: 0, sweat: 0, spark: 1, dream: 0, hue: [57, 255, 154] },
};
const MOOD_WORD = { happy: 'HAPPY', dreamy: 'DREAMY', sad: 'SAD', tense: 'TENSE', epic: 'EPIC' };
function lerpFace(cur, tgt, k) { const o = {}; for (const key in tgt) o[key] = Array.isArray(tgt[key]) ? tgt[key].map((v, i) => cur[key][i] + (v - cur[key][i]) * k) : cur[key] + (tgt[key] - cur[key]) * k; return o; }
function drawMoodGhost(c, X, Y, W, F, t, ear = 0) {
  const s = W / 100; c.save(); c.translate(X, Y); c.scale(s, s);
  const [hr, hg, hb] = F.hue.map(Math.round), bob = Math.sin(t * 2.1) * 2.5 * (1 - F.sweat * 0.5) + Math.sin(t * 23) * F.sweat * 0.5;
  let g = c.createRadialGradient(50, 48, 10, 50, 50, 50); g.addColorStop(0, `rgba(${hr},${hg},${hb},0.35)`); g.addColorStop(1, `rgba(${hr},${hg},${hb},0)`); c.fillStyle = g; c.fillRect(0, 0, 100, 100);
  c.translate(0, bob);
  // body with a wavy hem
  c.beginPath(); c.moveTo(20, 46); c.bezierCurveTo(20, 12, 80, 12, 80, 46); c.lineTo(80, 80);
  for (let i = 0; i <= 6; i++) { const x = 80 - i * 10, y = 82 + (i % 2 ? -5 : 2) + Math.sin(t * 4 + i) * 1.6; c.quadraticCurveTo(x + 5, y + 4, x, y); }
  c.closePath();
  g = c.createLinearGradient(30, 20, 70, 90); g.addColorStop(0, '#ffffff'); g.addColorStop(1, `rgb(${Math.round(200 + hr * 0.2)},${Math.round(200 + hg * 0.2)},${Math.round(215 + hb * 0.15)})`);
  c.fillStyle = g; c.fill(); c.lineWidth = 2.2; c.strokeStyle = '#1d1238'; c.stroke();
  // blush
  if (F.blush > 0.02) { c.fillStyle = `rgba(255,120,180,${0.55 * F.blush})`; gEll(c, 30, 55, 6, 3.5); c.fill(); gEll(c, 70, 55, 6, 3.5); c.fill(); }
  // eyes
  c.lineCap = 'round';
  for (const ex of [37, 63]) {
    const h = 8.5 * F.eye, ey = 44;
    if (F.smileEye < 0.98) { c.globalAlpha = 1 - F.smileEye; c.fillStyle = '#1d1238'; gEll(c, ex, ey, 5 * (F.eye > 1 ? 1.1 : 1), Math.max(0.8, h)); c.fill();
      c.fillStyle = '#fff'; gEll(c, ex + 1.6, ey - h * 0.35, 1.6, Math.max(0.4, 1.8 * Math.min(1, F.eye))); c.fill();
      if (F.spark > 0.3) { c.fillStyle = `rgba(255,255,255,${F.spark})`; c.beginPath(); c.arc(ex - 1.6, ey + h * 0.3, 0.9, 0, 7); c.fill(); }
      if (F.dream > 0.3) { c.fillStyle = `rgba(29,18,56,${F.dream})`; c.fillRect(ex - 6, ey - 10, 12, 10 - h * 0.6); c.strokeStyle = '#1d1238'; c.lineWidth = 2; c.beginPath(); c.moveTo(ex - 5.5, ey - h * 0.55); c.lineTo(ex + 5.5, ey - h * 0.55); c.stroke(); }
      c.globalAlpha = 1; }
    if (F.smileEye > 0.02) { c.globalAlpha = F.smileEye; c.strokeStyle = '#1d1238'; c.lineWidth = 2.6; c.beginPath(); c.arc(ex, ey + 2.5, 5, Math.PI * 1.15, Math.PI * 1.85); c.stroke(); c.globalAlpha = 1; }
    // brows: positive = determined (inner end down), negative = worried / sad (inner end up)
    const inner = ex < 50 ? 1 : -1, dy = F.brow * 3.2;
    c.strokeStyle = '#1d1238'; c.lineWidth = 2.2; c.beginPath(); c.moveTo(ex - 5 * inner, 34 - dy * 0.2 - (F.eye - 1) * 3); c.lineTo(ex + 5 * inner, 34 + dy - (F.eye - 1) * 3); c.stroke();
  }
  // mouth
  const my = 60;
  c.strokeStyle = '#1d1238'; c.lineWidth = 2.2;
  if (F.wave > 0.5) { c.beginPath(); for (let i = 0; i <= 12; i++) { const x = 41 + i * 1.5, y = my + 1 + Math.sin(i * 1.6 + t * 6) * 1.6 * F.wave; i ? c.lineTo(x, y) : c.moveTo(x, y); } c.stroke(); }
  else {
    const w = 9 + F.open * 2, cu = F.mouth * 7;
    if (F.open > 0.15) { c.beginPath(); c.moveTo(50 - w, my); c.quadraticCurveTo(50, my + cu * 0.35, 50 + w, my); c.quadraticCurveTo(50, my + cu + F.open * 9 * Math.sign(cu || 1), 50 - w, my); c.closePath();
      c.fillStyle = '#5a1630'; c.fill(); c.stroke();
      c.save(); c.clip(); c.fillStyle = '#ff7aa8'; gEll(c, 50, my + cu * 0.5 + F.open * 6, 5, 3.5); c.fill(); c.restore(); }
    else { c.beginPath(); c.moveTo(50 - w * 0.8, my); c.quadraticCurveTo(50, my + cu * 1.2, 50 + w * 0.8, my); c.stroke(); }
  }
  // tear / sweat / sparkles / dreamy z's
  if (F.tear > 0.05) { const ph = (t * 0.6) % 1; c.fillStyle = `rgba(110,190,255,${F.tear * (1 - ph * 0.6)})`; const x = 33, y = 50 + ph * 18; c.beginPath(); c.moveTo(x, y - 4); c.quadraticCurveTo(x + 3.2, y + 1, x, y + 2.6); c.quadraticCurveTo(x - 3.2, y + 1, x, y - 4); c.fill(); }
  if (F.sweat > 0.05) { c.fillStyle = `rgba(140,210,255,${F.sweat})`; const x = 76, y = 30 + Math.sin(t * 3) * 1.5; c.beginPath(); c.moveTo(x, y - 5); c.quadraticCurveTo(x + 4, y + 1, x, y + 3.5); c.quadraticCurveTo(x - 4, y + 1, x, y - 5); c.fill(); c.strokeStyle = '#1d1238'; c.lineWidth = 1; c.stroke(); }
  if (F.spark > 0.05) for (let i = 0; i < 4; i++) { const a = t * 1.5 + i * Math.PI / 2, x = 50 + Math.cos(a) * 44, y = 42 + Math.sin(a) * 34, z = (2 + Math.sin(t * 5 + i) * 1.2) * F.spark;
    c.fillStyle = `rgba(${hr},${hg},${hb},${F.spark})`; c.beginPath(); for (let k = 0; k < 8; k++) { const r = k % 2 ? z * 0.35 : z, an = k * Math.PI / 4; c.lineTo(x + Math.cos(an) * r, y + Math.sin(an) * r); } c.closePath(); c.fill(); }
  if (F.dream > 0.05) { c.fillStyle = `rgba(200,180,255,${F.dream})`; c.font = '700 11px system-ui'; for (let i = 0; i < 2; i++) { const ph = (t * 0.35 + i * 0.5) % 1; c.globalAlpha = F.dream * (1 - ph); c.fillText('z', 78 + ph * 10, 26 - ph * 18 + i * 2); } c.globalAlpha = 1; }
  // LISTEN: a little arm cups the right ear, sound waves drift in
  if (ear > 0.02) { c.globalAlpha = Math.min(1, ear); c.lineWidth = 2.2; c.strokeStyle = '#1d1238'; c.fillStyle = '#fff';
    c.beginPath(); c.moveTo(76, 62); c.quadraticCurveTo(90, 60, 86, 44); c.lineTo(80, 46); c.quadraticCurveTo(82, 56, 76, 56); c.closePath(); c.fill(); c.stroke();
    c.beginPath(); c.moveTo(80, 47); c.quadraticCurveTo(84, 30, 92, 36); c.quadraticCurveTo(90, 44, 86, 46); c.closePath(); c.fill(); c.stroke();
    c.strokeStyle = `rgba(61,227,255,${0.9 * Math.min(1, ear)})`; c.lineWidth = 1.8;
    for (let i = 0; i < 3; i++) { const ph = (t * 0.9 + i / 3) % 1, r = 16 - ph * 12; c.globalAlpha = Math.min(1, ear) * ph; c.beginPath(); c.arc(90, 40, r, -0.6, 0.6); c.stroke(); }
    c.globalAlpha = 1; }
  c.restore();
}
