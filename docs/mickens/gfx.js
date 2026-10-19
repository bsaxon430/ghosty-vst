'use strict';
// Mr. Mickens drawings for the web version (same art as the plugin): the cute unicorn logo, the puking / pooping
// unicorns, pixel water and 8-bit jetskis. All drawn with canvas paths, no images.

function hsv(h, s, v, a) {
  h = ((h % 1) + 1) % 1; const i = Math.floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  const [r, g, b] = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
  return `rgba(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)},${a === undefined ? 1 : a})`;
}
const rainbow = (t, sat = 0.9, bri = 1, a = 1) => hsv(t, sat, bri, a);
function seeded(seed) { let s = seed >>> 0; return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

function ell(c, x, y, w, h) { c.beginPath(); c.ellipse(x + w / 2, y + h / 2, Math.abs(w / 2), Math.abs(h / 2), 0, 0, Math.PI * 2); }
function rrect(c, x, y, w, h, r) { c.beginPath(); c.roundRect ? c.roundRect(x, y, w, h, r) : c.rect(x, y, w, h); }
function tri(c, a, b, d) { c.moveTo(a[0], a[1]); c.lineTo(b[0], b[1]); c.lineTo(d[0], d[1]); c.closePath(); }
function star(c, x, y, z, col) {
  c.beginPath(); for (let k = 0; k < 8; k++) { const r = k % 2 ? z * 0.35 : z, a = k * Math.PI / 4 - Math.PI / 2; const px = x + Math.cos(a) * r, py = y + Math.sin(a) * r; k ? c.lineTo(px, py) : c.moveTo(px, py); }
  c.closePath(); c.fillStyle = col; c.fill();
}
const onCircle = (cx, cy, r, ang) => [cx + Math.sin(ang) * r, cy - Math.cos(ang) * r];   // JUCE getPointOnCircumference

// ---------------------------------------------------------------- the logo
function drawLogo(c, X, Y, W, t) {
  const s = W / 100, P = (x, y) => [X + x * s, Y + y * s];
  c.save();
  rrect(c, X, Y, W, W, 16 * s); c.clip();
  let gr = c.createLinearGradient(0, Y, 0, Y + W); gr.addColorStop(0, '#3b2466'); gr.addColorStop(1, '#1a0f2e'); c.fillStyle = gr; c.fillRect(X, Y, W, W);
  const rnd = seeded(5);
  for (let i = 0; i < 18; i++) { const x = rnd() * 100, y = rnd() * 100, tw = 0.5 + 0.5 * Math.sin(t * 3 + i * 1.7); const [px, py] = P(x, y); star(c, px, py, (0.8 + 1.6 * tw) * s, rainbow(rnd(), 0.35, 1, 0.4 + 0.6 * tw)); }
  // curly pastel mane
  c.lineCap = 'round'; c.lineJoin = 'round';
  for (let i = 0; i < 26; i++) {
    const u = i / 25, a = Math.PI * (0.8 + 1.4 * u), len = 15 + 5 * Math.sin(i * 2.1), bounce = Math.sin(t * 4 + i * 0.8) * 0.08;
    const [cx, cy] = P(50, 46); c.beginPath();
    for (let k = 0; k <= 12; k++) { const f = k / 12, rr = (18 + len * f) * s; const [qx, qy] = onCircle(cx, cy, rr, a + Math.PI / 2 + bounce * f + Math.sin(f * 9 + i) * 0.12 * f); k ? c.lineTo(qx, qy) : c.moveTo(qx, qy); }
    c.strokeStyle = rainbow(u * 0.85 + 0.02, 0.45); c.lineWidth = 4.2 * s; c.stroke();
  }
  // ears
  c.beginPath(); tri(c, P(27, 32), P(26, 14), P(39, 25)); tri(c, P(73, 32), P(74, 14), P(61, 25)); c.fillStyle = '#d9b8ff'; c.fill();
  c.beginPath(); tri(c, P(29, 29), P(28.5, 18), P(36, 25)); tri(c, P(71, 29), P(71.5, 18), P(64, 25)); c.fillStyle = '#ffb8e6'; c.fill();
  // head + snout + nostrils + cheeks
  gr = c.createLinearGradient(X + 40 * s, Y + 24 * s, X + 60 * s, Y + 76 * s); gr.addColorStop(0, '#f1e4ff'); gr.addColorStop(1, '#b48ae8');
  ell(c, ...P(25, 22), 50 * s, 48 * s); c.fillStyle = gr; c.fill();
  ell(c, ...P(35, 56), 30 * s, 22 * s); c.fillStyle = '#e8d2ff'; c.fill();
  c.fillStyle = '#9a6fd0'; ell(c, ...P(42, 62), 4 * s, 3.5 * s); c.fill(); ell(c, ...P(54, 62), 4 * s, 3.5 * s); c.fill();
  c.fillStyle = 'rgba(255,127,196,0.53)'; ell(c, ...P(26, 52), 9 * s, 6 * s); c.fill(); ell(c, ...P(65, 52), 9 * s, 6 * s); c.fill();
  // horn with crawling rainbow bands
  c.save(); c.beginPath(); tri(c, P(44, 25), P(56, 25), P(51, 2)); c.clip();
  for (let i = -2; i < 12; i++) { const y = 25 - (i * 3 + (t * 4) % 3); c.beginPath(); c.moveTo(...P(40, y + 1.5)); c.lineTo(...P(60, y - 1)); c.lineTo(...P(60, y - 4)); c.lineTo(...P(40, y - 1.5)); c.closePath(); c.fillStyle = rainbow(i / 7, 0.55); c.fill(); }
  c.beginPath(); c.moveTo(...P(47, 24)); c.lineTo(...P(50.5, 5)); c.strokeStyle = 'rgba(255,255,255,0.5)'; c.lineWidth = 0.9 * s; c.stroke(); c.restore();
  // glasses with slow pastel hypno-spirals
  for (let e = 0; e < 2; e++) {
    const [cx, cy] = P(e ? 61 : 39, 44), rad = 10 * s;
    c.save(); c.beginPath(); c.arc(cx, cy, rad, 0, Math.PI * 2); c.clip();
    for (let k = 8; k >= 1; k--) { const rr = rad * k / 8; c.beginPath(); c.arc(cx, cy, rr, 0, Math.PI * 2); c.fillStyle = rainbow(k / 8 - t * 0.25, 0.45); c.fill(); }
    const spin = (e ? -1 : 1) * t * 2.2;
    for (let arm = 0; arm < 3; arm++) { c.beginPath(); for (let k = 0; k <= 24; k++) { const u = k / 24, ang = spin + arm * Math.PI * 2 / 3 + u * 6; const [qx, qy] = onCircle(cx, cy, rad * u, ang); k ? c.lineTo(qx, qy) : c.moveTo(qx, qy); }
      c.strokeStyle = 'rgba(255,255,255,0.55)'; c.lineWidth = 1.3 * s; c.stroke(); }
    c.beginPath(); c.arc(cx, cy, 3.2 * s, 0, Math.PI * 2); c.fillStyle = '#2a1a40'; c.fill();
    c.beginPath(); c.arc(cx - 1.4 * s, cy - 1.6 * s, 1.2 * s, 0, Math.PI * 2); c.fillStyle = '#fff'; c.fill();
    ell(c, cx - rad * 0.65, cy - rad * 0.72, rad * 0.5, rad * 0.32); c.fillStyle = 'rgba(255,255,255,0.7)'; c.fill();
    c.restore();
    star(c, cx + rad * 0.62, cy - rad * 0.55, (1.4 + 0.8 * Math.sin(t * 5 + e * 2)) * s, '#fff');
    c.beginPath(); c.arc(cx, cy, rad, 0, Math.PI * 2); c.strokeStyle = '#8a5530'; c.lineWidth = 2.2 * s; c.stroke();
  }
  c.beginPath(); c.moveTo(...P(49, 43)); c.quadraticCurveTo(...P(50, 41), ...P(51, 43)); c.strokeStyle = '#8a5530'; c.lineWidth = 1.8 * s; c.stroke();
  // smile
  c.beginPath(); c.moveTo(...P(44, 72)); c.quadraticCurveTo(...P(50, 76.5), ...P(56, 72)); c.strokeStyle = '#7a4fb0'; c.lineWidth = 1.4 * s; c.stroke();
  // handlebar mustache
  { const w = Math.sin(t * 3) * 0.6; c.beginPath(); c.moveTo(...P(50, 69));
    c.bezierCurveTo(...P(45, 67), ...P(40, 68), ...P(36, 72)); c.bezierCurveTo(...P(33, 75), ...P(29, 74 + w), ...P(29.5, 71 + w)); c.bezierCurveTo(...P(30, 69.5 + w), ...P(32, 69.5 + w), ...P(32, 71 + w));
    c.bezierCurveTo(...P(32, 72.5), ...P(34, 72.5), ...P(36.5, 70)); c.bezierCurveTo(...P(40, 66.5), ...P(46, 66), ...P(50, 67.5));
    c.bezierCurveTo(...P(54, 66), ...P(60, 66.5), ...P(63.5, 70)); c.bezierCurveTo(...P(66, 72.5), ...P(68, 72.5), ...P(68, 71 - w)); c.bezierCurveTo(...P(68, 69.5 - w), ...P(70, 69.5 - w), ...P(70.5, 71 - w)); c.bezierCurveTo(...P(71, 74 - w), ...P(67, 75), ...P(64, 72));
    c.bezierCurveTo(...P(60, 68), ...P(55, 67), ...P(50, 69)); c.closePath(); c.fillStyle = '#9a5a2e'; c.fill(); c.strokeStyle = '#c98a52'; c.lineWidth = 0.6 * s; c.stroke(); }
  // rainbow ribbon from the smile
  for (let i = 0; i < 6; i++) { const x0 = 46.5 + i * 1.4; c.beginPath(); c.moveTo(...P(x0, 75));
    for (let k = 1; k <= 8; k++) c.lineTo(...P(x0 + Math.sin(t * 3 + k * 0.8) * 2 + k * 1.2 + (i - 2.5) * k * 0.25, 75 + k * 3.2));
    c.strokeStyle = rainbow(i / 6, 0.55); c.lineWidth = 2 * s; c.stroke(); }
  for (let k = 0; k < 6; k++) { const u = (t * 0.6 + k / 6) % 1; const [px, py] = P(52 + k * 3 - 6 + u * 8, 80 + u * 18); star(c, px, py, (1 + u) * s, `rgba(255,255,255,${1 - u})`); }
  c.restore();
}

// ---------------------------------------------------------------- unicorns, puke, poop
function drawUnicorn(c, ax, ay, s, flip, t) {
  c.save(); c.translate(ax, ay); c.scale(flip ? -s : s, s);
  const bob = Math.sin(t * 6) * 1.5, white = '#f7f2ff', shade = '#cfc2e6';
  c.lineCap = 'round'; c.lineJoin = 'round';
  for (let i = 0; i < 4; i++) { const lx = -26 + i * 15, sw = Math.sin(t * 8 + i * 1.7) * 4; rrect(c, lx + sw * 0.3, 8 + bob, 7, 26, 3); c.fillStyle = i % 2 ? shade : white; c.fill();
    rrect(c, lx + sw * 0.3 - 0.5, 31 + bob, 8, 5, 2); c.fillStyle = '#3a2350'; c.fill(); }
  for (let i = 0; i < 6; i++) { c.beginPath(); c.moveTo(-34, -6 + bob); c.bezierCurveTo(-50, -14 + i * 3, -54, 6 + i * 3 + Math.sin(t * 5 + i) * 3, -44, 22 + i * 2); c.strokeStyle = rainbow(i / 6); c.lineWidth = 3; c.stroke(); }
  ell(c, -38, -18 + bob, 70, 34); c.fillStyle = white; c.fill();
  ell(c, -30, 2 + bob, 54, 12); c.fillStyle = 'rgba(207,194,230,0.6)'; c.fill();
  c.beginPath(); c.moveTo(14, -10 + bob); c.lineTo(30, -38 + bob); c.lineTo(42, -32 + bob); c.lineTo(32, 4 + bob); c.closePath(); c.fillStyle = white; c.fill();
  ell(c, 26, -46 + bob, 30, 20); c.fill(); rrect(c, 44, -40 + bob, 18, 13, 6); c.fill();
  ell(c, 57, -36 + bob, 3, 3); c.fillStyle = '#ffb3d9'; c.fill();
  const hg = c.createLinearGradient(40, -44 + bob, 50, -72 + bob); hg.addColorStop(0, '#ffd23f'); hg.addColorStop(1, '#ff8fe0');
  c.beginPath(); tri(c, [36, -44 + bob], [44, -46 + bob], [50, -72 + bob]); c.fillStyle = hg; c.fill();
  c.strokeStyle = '#b8860b'; c.lineWidth = 1; for (let i = 1; i < 4; i++) { c.beginPath(); c.moveTo(38 + i * 2.5, -46 - i * 6.5 + bob); c.lineTo(44 + i * 1.2, -47 - i * 6 + bob); c.stroke(); }
  c.beginPath(); tri(c, [30, -44 + bob], [34, -56 + bob], [37, -44 + bob]); c.fillStyle = shade; c.fill();
  for (let i = 0; i < 6; i++) { c.beginPath(); c.moveTo(32 - i, -44 + bob); c.bezierCurveTo(20 - i * 2, -36 + i * 2 + bob, 22 - i * 3 + Math.sin(t * 6 + i) * 2, -16 + i * 3 + bob, 12 - i * 2, -6 + i * 2 + bob); c.strokeStyle = rainbow(i / 6); c.lineWidth = 3.2; c.stroke(); }
  c.beginPath(); c.moveTo(50, -30 + bob); c.bezierCurveTo(54, -33 + bob, 58, -33 + bob, 61, -30 + bob); c.bezierCurveTo(63, -28 + bob, 64, -30 + bob, 63.5, -32 + bob);
  c.bezierCurveTo(65, -29 + bob, 63, -26 + bob, 60, -28 + bob); c.bezierCurveTo(57, -30 + bob, 53, -30 + bob, 48, -27 + bob); c.bezierCurveTo(46, -29 + bob, 47, -32 + bob, 50, -30 + bob); c.closePath(); c.fillStyle = '#7a4522'; c.fill();
  ell(c, 37.5, -42 + bob, 7.5, 8.5); c.fillStyle = '#2a1a40'; c.fill();
  c.fillStyle = '#fff'; ell(c, 38.5, -41 + bob, 3, 3); c.fill(); ell(c, 42, -37 + bob, 1.4, 1.4); c.fill();
  ell(c, 44, -33 + bob, 7, 4); c.fillStyle = 'rgba(255,127,196,0.53)'; c.fill();
  c.strokeStyle = '#6b3a1c'; c.lineWidth = 1.6; ell(c, 36, -43.5 + bob, 9, 9); c.stroke();
  c.lineWidth = 1.4; c.beginPath(); c.moveTo(45, -39 + bob); c.lineTo(50, -38 + bob); c.moveTo(36, -39 + bob); c.lineTo(31, -41 + bob); c.stroke();
  c.restore();
}
function drawPuke(c, fx, fy, tx, ty, t, s) {
  const lift = 70 * s; c.lineCap = 'round';
  for (let i = 0; i < 6; i++) { const off = (i - 2.5) * 3.2 * s; c.beginPath(); c.moveTo(fx, fy + off * 0.4);
    c.bezierCurveTo(fx + 40 * s, fy - lift + off, tx - 60 * s, ty - lift * 0.6 + off, tx, ty + off); c.strokeStyle = rainbow(i / 6); c.lineWidth = 3.4 * s; c.stroke(); }
  const b = [fx + 40 * s, fy - lift], cc = [tx - 60 * s, ty - lift * 0.6];
  for (let k = 0; k < 14; k++) { const u = (t * 0.7 + k / 14) % 1, v = 1 - u;
    const x = fx * v * v * v + b[0] * 3 * v * v * u + cc[0] * 3 * v * u * u + tx * u * u * u, y = fy * v * v * v + b[1] * 3 * v * v * u + cc[1] * 3 * v * u * u + ty * u * u * u;
    const r = (2 + (k % 3)) * s; c.beginPath(); c.arc(x, y + Math.sin(k + t * 9) * 3 * s, r, 0, Math.PI * 2); c.fillStyle = rainbow(k / 14 + t * 0.2, 0.9, 1, 0.95); c.fill(); }
  for (let i = 0; i < 6; i++) { ell(c, tx - (22 - i * 3) * s, ty - 2 * s + i * 0.6 * s, (44 - i * 6) * s, 6 * s); c.fillStyle = rainbow(i / 6, 0.9, 1, 0.8); c.fill(); }
}
function drawPoop(c, fx, fy, groundY, t, s) {
  for (let k = 0; k < 4; k++) { const u = (t * 1.1 + k * 0.25) % 1, y = fy + (groundY - fy - 30 * s) * u * u; ell(c, fx - 4 * s, y - 4 * s, 8 * s, 9 * s); c.fillStyle = rainbow(k / 4 + t * 0.3); c.fill(); }
  const px = fx - 6 * s;
  for (let i = 0; i < 4; i++) { const w = (38 - i * 8) * s, h = 10 * s, y = groundY - 9 * s - i * 8.5 * s;
    rrect(c, px - w / 2, y, w, h, h / 2); c.fillStyle = rainbow(0.05 + i * 0.2 + t * 0.1); c.fill();
    rrect(c, px - w / 2 + 4 * s, y + 2 * s, w * 0.35, 2.5 * s, 1.2 * s); c.fillStyle = 'rgba(255,255,255,0.35)'; c.fill(); }
  c.beginPath(); tri(c, [px - 5 * s, groundY - 42 * s], [px + 5 * s, groundY - 42 * s], [px + 2 * s, groundY - 52 * s]); c.fillStyle = rainbow(0.85 + t * 0.1); c.fill();
  for (let k = 0; k < 7; k++) { const a = t * 2 + k * 0.9, r = (30 + 12 * Math.sin(a * 1.3)) * s; star(c, px + Math.cos(a) * r, groundY - 26 * s + Math.sin(a) * r * 0.5, (2.5 + 1.5 * Math.sin(t * 7 + k)) * s * 2, rainbow(k / 7, 0.4)); }
}

// ---------------------------------------------------------------- 8-bit jetskis
function drawPixelWater(c, x, y, w, h, t, px) {
  const cols = Math.floor(w / px) + 1, rows = Math.floor(h / px);
  for (let col = 0; col < cols; col++) { const crest = Math.round(1.5 + 1.5 * Math.sin((col + t * 14) * 0.35));
    for (let r = crest; r < rows; r++) { c.fillStyle = r === crest ? '#eaffff' : r < crest + 2 ? '#7fd4ff' : r < crest + 4 ? '#3f86ff' : '#2b4fd8'; c.fillRect(x + col * px, y + r * px, px + 0.5, px + 0.5); } }
}
const JET = ['..........HH........', '.........HHHM.......', '.........HEHM.......', '..........HHM.......', '..........PP........', '....Y....PPP........', '...YY...PPPPBBBB....', 'BBBBBBBBBBBBBBBBB...', 'WWWWWWWWWWWWWWWWBB..', '.DDDDDDDDDDDDDDDD...'];
function drawJetski(c, x, y, px, faceRight, hue, t) {
  const body = rainbow(hue, 0.75), shirt = rainbow(hue + 0.5, 0.6), dark = rainbow(hue, 0.75, 0.4);
  for (let r = 0; r < 10; r++) for (let col = 0; col < 20; col++) { const ch = JET[r][col]; if (ch === '.') continue;
    c.fillStyle = ch === 'B' ? body : ch === 'W' ? '#fff' : ch === 'D' ? dark : ch === 'Y' ? '#9ff3ff' : ch === 'H' ? '#e2c8ff' : ch === 'E' ? '#2a1a40' : ch === 'M' ? rainbow(r / 4 + t, 0.6) : shirt;
    const cx = faceRight ? 19 - col : col; c.fillRect(x + cx * px, y + r * px, px + 0.5, px + 0.5); }
  const rr = seeded(Math.floor(t * 20) + Math.floor(hue * 100));
  for (let i = 0; i < 7; i++) { const dx = 18 + Math.floor(rr() * 6), dy = 6 + Math.floor(rr() * 4), cx = faceRight ? 19 - dx : dx; c.fillStyle = `rgba(255,255,255,${0.6 + rr() * 0.4})`; c.fillRect(x + cx * px, y + dy * px, px, px); }
}

// the whole unicorn strip (W x H canvas units)
function drawStrip(c, W, H, t) {
  c.clearRect(0, 0, W, H);
  rrect(c, 0, 0, W, H, 12); c.fillStyle = 'rgba(255,255,255,0.06)'; c.fill();
  c.save(); rrect(c, 0, 0, W, H, 12); c.clip();
  const s = Math.min(0.9, H / 115), gy = H - 20, trot = Math.sin(t * 1.3) * 6;
  const ax = 62 * s / 0.9 + trot, ay = gy - 36 * s, bx = W - 58 * s / 0.9 - trot;
  drawPuke(c, ax + 60 * s, ay - 36 * s, W / 2 + 20 * s, gy - 2, t, s);
  drawUnicorn(c, ax, ay, s, false, t);
  drawPoop(c, bx + 36 * s, ay - 8 * s, gy, t, s);
  drawUnicorn(c, bx, ay, s, true, t + 1.3);
  const px = 3, wy = H - 18; drawPixelWater(c, 0, wy, W, 18, t, px);
  const span = W + 80;
  for (let k = 0; k < 3; k++) { const right = k !== 1, sp = 70 + k * 25, off = k * 140; let x = ((t * sp + off) % span) - 50; if (!right) x = W + 10 - (x + 50);
    drawJetski(c, Math.floor(x / px) * px, wy - 21 + Math.floor(Math.sin(t * 6 + k) * 1.5) * px, px, right, k * 0.31 + 0.8, t); }
  c.restore();
}
