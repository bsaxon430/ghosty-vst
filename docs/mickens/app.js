'use strict';
// Mr. Mickens web UI: state, knobs, bays, drum grid, FX, play / stutter / dice, WAV export. Settings are kept in
// this browser (localStorage) so a reload keeps your beat.

const $ = id => document.getElementById(id);
const DEF_FX = { crunch: 0.25, bits: 0, dry: 1, lp_cut: 0.45, lp_mix: 0, hp_cut: 0.55, hp_mix: 0, bp_cut: 0.5, bp_mix: 0, nt_cut: 0.5, nt_mix: 0, cb_cut: 0.4, cb_mix: 0,
  fm_vow: 0.3, fm_mix: 0, res: 0.5, chorus: 0, reverb: 0.12, size: 0.6, st_cut: 0.75, st_env: 0.5, st_dec: 0.35, duck_rel: 0.16, brk_lvl: 0.85, drm_lvl: 0.9, volume: 0.8 };
const PATTERNS = {
  'TWO STEP': ['X.........X.....', '....X.......X...', 'x.x.x.x.x.x.x.xo', '..............x.'],
  'HALFTIME': ['X......X..X.....', '........X.......', 'x.x.x.x.x.x.x.x.', '................'],
  'ROLLER': ['X.....X...X.....', '....X.......X..o', 'xxxxxxxxxxxxxxxx', '......x.......x.'],
  'CLEAR': ['................', '................', '................', '................'],
};
const patRows = name => PATTERNS[name].map(r => [...r].map(ch => ch === 'X' ? 127 : ch === 'x' ? 96 : ch === 'o' ? 60 : 0));
const bay = (brk, level, mute, dk) => ({ brk, nudge: 0, grid: 0, pitch: 0, level, decay: 1, pan: 0, rs: false, rc: false, amt: 0.3, dk, ds: 0, dh: 0, mute, fit: true });
function defaults() {
  return { v: 1, bpm: 172, theme: 'light', stutDiv: 1, stutter: false,
    bays: [bay('Amen|0', 0.85, false, 0.35), bay('Think|0', 0.6, true, 0), bay('Two Step|1', 0.6, true, 0), bay('Apache|0', 0.6, true, 0)],
    drums: { pat: patRows('TWO STEP'), sel: ['Hard Kick', 'Big Snare', 'Tight Hat', 'Short Open Hat'], lvl: [1, 0.9, 0.6, 0.55], mute: [false, false, false, false], all: false }, fx: { ...DEF_FX } };
}
let S = defaults();
try { const saved = JSON.parse(localStorage.getItem('mickens')); if (saved && saved.v === 1) { S = Object.assign(defaults(), saved); S.fx = { ...DEF_FX, ...saved.fx }; S.stutter = false; S.drums = { ...defaults().drums, ...saved.drums }; } } catch (e) {}
let saveTimer = 0; const save = () => { clearTimeout(saveTimer); saveTimer = setTimeout(() => { try { localStorage.setItem('mickens', JSON.stringify(S)); } catch (e) {} }, 300); };

const userBreaks = [];
const allBreaks = () => userBreaks.concat(BREAKS);
const getBreak = key => allBreaks().find(b => b.key === key) || null;

let ctx = null, eng = null, playing = false, timer = 0, nextT = 0, stepN = 0, curBay = 0;
const uiQueue = [];   // {t, step, slices[]} for the playhead
const toast = msg => { const t = $('toast'); t.textContent = msg; t.style.opacity = 1; clearTimeout(toast.h); toast.h = setTimeout(() => t.style.opacity = 0, 2200); };

// ---------------------------------------------------------------- knobs
const knobs = [];
function knob(parent, label, get, set, min, max, def, small, fmt) {
  const el = document.createElement('div'); el.className = 'knob' + (small ? ' small' : '');
  const cv = document.createElement('canvas'); const px = small ? 80 : 100; cv.width = px; cv.height = px; el.appendChild(cv);
  const sp = document.createElement('span'); sp.textContent = label; el.appendChild(sp); parent.appendChild(el);
  const k = { el, cv, get, set, min, max, def, label, sp, fmt };
  const draw = () => {
    const c = cv.getContext('2d'), W = cv.width, v = (get() - min) / (max - min), a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, th = W * 0.09, r = W / 2 - th / 2 - 2, cx = W / 2;
    c.clearRect(0, 0, W, W);
    c.beginPath(); c.arc(cx, cx, r - th / 2 - 2, 0, Math.PI * 2); c.fillStyle = getComputedStyle(document.body).getPropertyValue('--card2'); c.fill();
    c.lineWidth = th; c.lineCap = 'round'; c.beginPath(); c.arc(cx, cx, r, a0, a1); c.strokeStyle = 'rgba(255,255,255,.08)'; c.stroke();
    const bip = min < 0 && max > 0, mid = a0 + (a1 - a0) * (-min / (max - min)), ang = a0 + v * (a1 - a0), from = bip ? Math.min(mid, ang) : a0, to = bip ? Math.max(mid, ang) : ang;
    const segs = Math.max(1, Math.floor((to - from) / 0.12)); c.lineCap = 'butt';
    for (let i = 0; i < segs; i++) { const s0 = from + (to - from) * i / segs, s1 = from + (to - from) * (i + 1) / segs; c.beginPath(); c.arc(cx, cx, r, s0, s1 + 0.01); c.strokeStyle = rainbow(0.83 - (s0 - a0) / (a1 - a0) * 0.83); c.stroke(); }
    c.lineCap = 'round'; c.lineWidth = W * 0.045; c.beginPath(); c.moveTo(cx + Math.cos(ang) * W * 0.1, cx + Math.sin(ang) * W * 0.1); c.lineTo(cx + Math.cos(ang) * (r - th - 3), cx + Math.sin(ang) * (r - th - 3));
    c.strokeStyle = getComputedStyle(document.body).getPropertyValue('--ink'); c.stroke();
  };
  k.draw = draw;
  let startY = 0, startV = 0, lastTap = 0;
  el.addEventListener('pointerdown', e => { el.setPointerCapture(e.pointerId); startY = e.clientY; startV = get();
    const now = performance.now(); if (now - lastTap < 300) { set(def); draw(); changed(); } lastTap = now; sp.textContent = fmt ? fmt(get()) : (Math.round(get() * 100) / 100).toString(); });
  el.addEventListener('pointermove', e => { if (!el.hasPointerCapture(e.pointerId)) return; const dy = startY - e.clientY;
    let v = startV + dy / 160 * (max - min); v = Math.min(max, Math.max(min, v)); set(v); draw(); changed(); sp.textContent = fmt ? fmt(get()) : (Math.round(get() * 100) / 100).toString(); });
  const up = () => { sp.textContent = label; };
  el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
  knobs.push(k); draw(); return k;
}
function changed() { if (eng) eng.apply(); save(); }
const B = () => S.bays[curBay];

// ---------------------------------------------------------------- build UI
knob($('tempoKnob'), 'TEMPO', () => S.bpm, v => S.bpm = Math.round(v), 70, 200, 172, true, v => Math.round(v) + ' BPM');
const bk1 = $('bayKnobs1'), bk2 = $('bayKnobs2');
knob(bk1, 'NUDGE', () => B().nudge, v => B().nudge = Math.round(v), -16, 16, 0, false, v => (v > 0 ? '+' : '') + Math.round(v));
knob(bk1, 'PITCH', () => B().pitch, v => B().pitch = v, -12, 12, 0, false, v => v.toFixed(1) + ' st');
knob(bk1, 'LEVEL', () => B().level, v => B().level = v, 0, 1, 0.8);
knob(bk1, 'CHOP LEN', () => B().decay, v => B().decay = v, 0.05, 1, 1);
knob(bk1, 'PAN', () => B().pan, v => B().pan = v, -1, 1, 0);
knob(bk2, 'DUCK KICK', () => B().dk, v => B().dk = v, 0, 1, 0);
knob(bk2, 'DUCK SNARE', () => B().ds, v => B().ds = v, 0, 1, 0);
knob(bk2, 'DUCK HATS', () => B().dh, v => B().dh = v, 0, 1, 0);
knob(bk2, 'CHOP AMT', () => B().amt, v => B().amt = v, 0, 1, 0.3);
const fxk = (parent, id, label, small) => knob(parent, label, () => S.fx[id], v => S.fx[id] = v, id === 'duck_rel' ? 0.03 : 0, id === 'duck_rel' ? 0.6 : 1, DEF_FX[id], small);
fxk($('kCrunch'), 'crunch', 'CRUNCH'); fxk($('kCrunch'), 'bits', 'BITS');
fxk($('kSpace'), 'chorus', 'CHORUS'); fxk($('kSpace'), 'reverb', 'REVERB'); fxk($('kSpace'), 'size', 'SIZE');
fxk($('kMix'), 'brk_lvl', 'BREAKS'); fxk($('kMix'), 'drm_lvl', 'DRUMS'); fxk($('kMix'), 'duck_rel', 'DUCK REL'); fxk($('kMix'), 'volume', 'VOLUME');
[['', 'dry', 'DRY', 'res', 'RESO'], ['LOW', 'lp_cut', 'CUT', 'lp_mix', 'MIX'], ['HIGH', 'hp_cut', 'CUT', 'hp_mix', 'MIX'], ['BAND', 'bp_cut', 'CUT', 'bp_mix', 'MIX'],
 ['NOTCH', 'nt_cut', 'CUT', 'nt_mix', 'MIX'], ['COMB', 'cb_cut', 'PITCH', 'cb_mix', 'MIX'], ['VOWEL', 'fm_vow', 'VOWEL', 'fm_mix', 'MIX']].forEach(([t, a, la, b, lb]) => {
  const col = document.createElement('div'); col.className = 'col'; col.innerHTML = `<b>${t}</b>`; $('kFilt').appendChild(col); fxk(col, a, la, true); fxk(col, b, lb, true); });
fxk($('kStut'), 'st_cut', 'FILTER'); fxk($('kStut'), 'st_env', 'ENV AMT'); fxk($('kStut'), 'st_dec', 'ENV DECAY');

// bay tabs + break menu
const tabCols = ['#ff2650', '#a6ff2b', '#22e6ff', '#9a5bff'];
for (let i = 0; i < 4; i++) { const b = document.createElement('button'); b.textContent = 'BAY ' + (i + 1); b.style.setProperty('--tint', tabCols[i]); b.onclick = () => { curBay = i; showBay(); }; $('bayTabs').appendChild(b); }
function fillBreaks() {
  const sel = $('brkSel'); sel.innerHTML = ''; let grp = null, fam = null;
  for (const br of allBreaks()) { if (br.family !== fam) { fam = br.family; grp = document.createElement('optgroup'); grp.label = br.user ? 'Your breaks' : `${fam} (${br.bpm})`; sel.appendChild(grp); }
    const o = document.createElement('option'); o.value = br.key; o.textContent = br.name; grp.appendChild(o); }
}
function pick(key) { B().brk = key; const br = getBreak(key); if (br) renderBreak(br); showBay(); save(); }
$('brkSel').onchange = e => pick(e.target.value);
const stepBreak = d => { const L = allBreaks(), i = L.findIndex(b => b.key === B().brk); pick(L[(i + d + L.length) % L.length].key); };
$('prev').onclick = () => stepBreak(-1); $('next').onclick = () => stepBreak(1);
$('grid').onchange = e => { B().grid = e.target.selectedIndex; save(); };
const toggle = (id, field) => { $(id).onclick = () => { B()[field] = !B()[field]; showBay(); changed(); }; };
toggle('rs', 'rs'); toggle('rc', 'rc'); toggle('mute', 'mute'); toggle('fit', 'fit');
$('file').onchange = async e => { const f = e.target.files[0]; if (!f) return; await ensureAudio();
  try { const br = await breakFromFile(f, ctx); userBreaks.push(br); fillBreaks(); pick(br.key); toast('Loaded ' + br.name + ' (' + Math.round(br.bpm) + ' BPM)'); } catch (err) { toast("Can't read that file"); } e.target.value = ''; };
function showBay() {
  const P = B(); [...$('bayTabs').children].forEach((b, i) => b.classList.toggle('on', i === curBay));
  $('bayCard').style.setProperty('--tint', tabCols[curBay]); hold.textContent = 'HOLD TO PLAY BAY ' + (curBay + 1);
  $('brkSel').value = P.brk; $('grid').selectedIndex = P.grid;
  for (const [id, f] of [['rs', 'rs'], ['rc', 'rc'], ['mute', 'mute'], ['fit', 'fit']]) $(id).classList.toggle('on', !!P[f]);
  knobs.forEach(k => k.draw()); const br = getBreak(P.brk); if (br) renderBreak(br);
}

// bay pads: HOLD + 8 slice pads (one per beat of the 2-bar break)
const hold = $('hold');
hold.addEventListener('pointerdown', async e => { hold.setPointerCapture(e.pointerId); await ensureAudio(); eng.holdBay(curBay, true); hold.classList.add('on'); });
['pointerup', 'pointercancel'].forEach(ev => hold.addEventListener(ev, () => { if (eng) eng.holdBay(curBay, false); hold.classList.remove('on'); }));
for (let i = 0; i < 8; i++) { const b = document.createElement('button'); b.textContent = i + 1; b.style.setProperty('--tint', rainbow(i / 8));
  b.addEventListener('pointerdown', async e => { e.preventDefault(); await ensureAudio(); const br = getBreak(B().brk); if (br) eng.sliceBay(curBay, Math.round(i * br.slices / 8)); b.classList.add('on'); });
  ['pointerup', 'pointercancel', 'pointerleave'].forEach(ev => b.addEventListener(ev, () => b.classList.remove('on'))); $('spads').appendChild(b); }

// drum lanes
const laneNames = ['KICK', 'SNARE', 'HAT', 'OPEN HAT'], cells = [], muteBtns = [];
LANES.forEach((lane, d) => {
  const L = document.createElement('div'); L.className = 'lane';
  const head = document.createElement('div'); head.className = 'head'; const sel = document.createElement('select');
  for (const n of Object.keys(DRUMS[lane])) { const o = document.createElement('option'); o.textContent = n; sel.appendChild(o); }
  sel.value = S.drums.sel[d]; sel.onchange = () => { S.drums.sel[d] = sel.value; save(); if (ctx) eng.hit(d, 0.9, ctx.currentTime + 0.01); };
  const nm = document.createElement('button'); nm.textContent = laneNames[d]; nm.className = 'lanemute'; nm.title = 'Tap to mute this drum';
  nm.onclick = () => { S.drums.mute[d] = !S.drums.mute[d]; drawMutes(); save(); }; muteBtns[d] = nm;
  const hit = document.createElement('button'); hit.textContent = 'HIT'; hit.className = 'hitbtn';
  hit.addEventListener('pointerdown', async e => { e.preventDefault(); await ensureAudio(); eng.hit(d, 0.95, ctx.currentTime + 0.005); hit.classList.add('on'); });
  ['pointerup', 'pointercancel', 'pointerleave'].forEach(ev => hit.addEventListener(ev, () => hit.classList.remove('on')));
  head.append(nm, hit, sel); const kh = document.createElement('div'); head.appendChild(kh);
  knob(kh, '', () => S.drums.lvl[d], v => S.drums.lvl[d] = v, 0, 1, [1, 0.9, 0.6, 0.55][d], true);
  const grid = document.createElement('div'); grid.className = 'steps'; cells[d] = [];
  for (let s = 0; s < 16; s++) { const c = document.createElement('div'); grid.appendChild(c); cells[d].push(c); }
  let paint = -1, pressT = 0, pressCell = -1;
  const at = e => { const r = grid.getBoundingClientRect(); return Math.max(0, Math.min(15, Math.floor((e.clientX - r.left) / r.width * 16))); };
  grid.addEventListener('pointerdown', e => { grid.setPointerCapture(e.pointerId); const s = at(e); pressCell = s; const v = S.drums.pat[d][s];
    paint = v ? 0 : 118; S.drums.pat[d][s] = paint; drawCells(); save();
    clearTimeout(pressT); pressT = setTimeout(() => { if (pressCell === s) { S.drums.pat[d][s] = 60; paint = 60; drawCells(); save(); } }, 450); });
  grid.addEventListener('pointermove', e => { if (!grid.hasPointerCapture(e.pointerId)) return; const s = at(e); if (s !== pressCell) { pressCell = -1; clearTimeout(pressT); }
    if (S.drums.pat[d][s] !== paint) { S.drums.pat[d][s] = paint; drawCells(); save(); } });
  grid.addEventListener('pointerup', () => { clearTimeout(pressT); pressCell = -1; });
  L.append(head, grid); $('lanes').appendChild(L);
});
let nowStep = -1;
function drawCells() { cells.forEach((row, d) => row.forEach((c, s) => { const v = S.drums.pat[d][s];
  c.style.background = v ? rainbow(s / 16, 0.9, 1, 0.35 + 0.65 * v / 127) : ''; c.classList.toggle('now', s === nowStep && playing); })); }
function drawMutes() {
  muteBtns.forEach((b, d) => { const m = S.drums.mute[d] || S.drums.all; b.classList.toggle('on', m); b.textContent = m ? 'MUTED' : laneNames[d]; });
  cells.forEach((row, d) => row[0].parentNode.style.opacity = (S.drums.mute[d] || S.drums.all) ? 0.35 : 1);
  $('muteAll').classList.toggle('on', S.drums.all); $('muteAll').textContent = S.drums.all ? 'DRUMS MUTED' : 'MUTE ALL DRUMS';
}
$('muteAll').onclick = () => { S.drums.all = !S.drums.all; drawMutes(); save(); };
Object.keys(PATTERNS).forEach(n => { const b = document.createElement('button'); b.textContent = n; b.onclick = () => { S.drums.pat = patRows(n); drawCells(); save(); }; $('pats').appendChild(b); });

// ---------------------------------------------------------------- audio + transport
async function ensureAudio() {
  if (ctx) { if (ctx.state !== 'running') await ctx.resume(); return; }
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch (e) {}   // iPhone: play even with the silent switch on
  try { const a = new Audio('data:audio/wav;base64,UklGRigAAABXQVZFZm10IBAAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAAAA'); a.loop = true; a.setAttribute('playsinline', ''); a.play().catch(() => {}); } catch (e) {}   // older iPhones
  ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  eng = new Engine(ctx, ctx.destination, S, getBreak);
  eng.onSlice = (b, slice, t) => uiQueue.push({ t, b, slice });
  if (ctx.state !== 'running') await ctx.resume();
}
function tick() {
  const spb = 60 / S.bpm / 4;
  if (ctx.state !== 'running') return;
  if (nextT < ctx.currentTime - 0.05) nextT = ctx.currentTime + 0.05;   // the phone paused the audio: carry on from now
  while (nextT < ctx.currentTime + 0.12) { eng.step(stepN, nextT, spb); uiQueue.push({ t: nextT, step: stepN % 16 }); nextT += 60 / S.bpm / 4; stepN++; }
}
async function setPlay(on) {
  await ensureAudio();
  if (on === playing) return; playing = on; $('play').textContent = on ? 'STOP' : 'PLAY'; $('play').classList.toggle('on', on);
  if (on) { stepN = 0; nextT = ctx.currentTime + 0.06; eng.bays.forEach(b => b.lastBar = -1); tick(); timer = setInterval(tick, 25); }
  else { clearInterval(timer); nowStep = -1; drawCells(); }
}
$('play').onclick = () => setPlay(!playing);
document.addEventListener('pointerdown', () => { if (ctx && ctx.state !== 'running') ctx.resume(); }, true);
$('go').onclick = async () => { await ensureAudio(); $('start').remove(); setPlay(true); };
$('dice').onclick = () => { S.bays.forEach(b => b.nudge = Math.floor(Math.random() * 33) - 16); showBay(); save(); toast('New starts for every bay'); };
$('theme').onclick = () => { S.theme = S.theme === 'dark' ? 'light' : 'dark'; applyTheme(); save(); };
function applyTheme() { document.body.classList.toggle('dark', S.theme === 'dark'); $('theme').textContent = S.theme === 'dark' ? 'LIGHT' : 'DARK'; knobs.forEach(k => k.draw()); }
let latch = false;
const stut = on => { S.stutter = on; $('stutter').classList.toggle('on', on); };
$('stutter').addEventListener('pointerdown', e => { $('stutter').setPointerCapture(e.pointerId); stut(latch ? !S.stutter : true); });
$('stutter').addEventListener('pointerup', () => { if (!latch) stut(false); });
$('stutter').addEventListener('pointercancel', () => { if (!latch) stut(false); });
$('latch').onclick = () => { latch = !latch; $('latch').classList.toggle('on', latch); if (!latch) stut(false); };
$('stutDiv').selectedIndex = S.stutDiv; $('stutDiv').onchange = e => { S.stutDiv = e.target.selectedIndex; save(); };
$('reset').onclick = () => { if (!confirm('Reset everything to the start?')) return; S = defaults(); if (eng) { eng.S = S; eng.apply(); } $('stutDiv').selectedIndex = 1; drawCells(); drawMutes(); showBay(); applyTheme(); save(); };

// 4 bars to WAV (rendered offline with the same engine), then share (phone) or download
$('wav').onclick = async () => {
  await ensureAudio(); toast('Rendering...');
  const bars = 4, spb = 60 / S.bpm / 4, len = Math.ceil((bars * 16 * spb + 2) * 44100);
  const off = new OfflineAudioContext(2, len, 44100), snap = JSON.parse(JSON.stringify(S)); snap.stutter = false;
  const e2 = new Engine(off, off.destination, snap, getBreak);
  for (let s = 0; s < bars * 16; s++) e2.step(s, 0.05 + s * spb, spb);
  const buf = await off.startRendering(), blob = wavBlob(buf), name = `Mr Mickens ${S.bpm}bpm.wav`, file = new File([blob], name, { type: 'audio/wav' });
  if (navigator.canShare && navigator.canShare({ files: [file] })) { try { await navigator.share({ files: [file], title: 'Mr. Mickens beat' }); return; } catch (e) { if (e.name === 'AbortError') return; } }
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click(); a.remove(); toast('Saved ' + name);
};

// ---------------------------------------------------------------- animation (logo, strip, waveform, playhead)
const logo = $('logo'), strip = $('strip'), wave = $('wave');
const slices = [-1, -1, -1, -1];
function fitCanvas(cv) { const r = cv.getBoundingClientRect(), d = Math.min(2, window.devicePixelRatio || 1); const w = Math.round(r.width * d), h = Math.round(r.height * d); if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; } return d; }
function drawWave() {
  const d = fitCanvas(wave), c = wave.getContext('2d'), W = wave.width, H = wave.height; c.clearRect(0, 0, W, H);
  c.fillStyle = getComputedStyle(document.body).getPropertyValue('--wave'); c.beginPath(); c.roundRect ? c.roundRect(0, 0, W, H, 6 * d) : c.rect(0, 0, W, H); c.fill();
  const br = getBreak(B().brk); if (!br || !br.peaks) return;
  const sl = br.slices, cur = slices[curBay];
  if (playing && cur >= 0) { c.fillStyle = rainbow(cur / sl, 0.9, 1, 0.3); c.fillRect(W * cur / sl, 0, W / sl, H); }
  for (let i = 1; i < sl; i++) { c.fillStyle = i % 4 ? 'rgba(255,255,255,.06)' : 'rgba(255,255,255,.15)'; c.fillRect(Math.floor(W * i / sl), 0, 1, H); }
  const p = br.peaks, cy = H / 2, hh = H * 0.46;
  for (let i = 0; i < p.length; i++) { const x = W * i / p.length; c.fillStyle = rainbow(i / p.length * 0.9, 0.75); c.fillRect(x, cy - p[i] * hh, Math.max(1, W / p.length), p[i] * hh * 2); }
  c.fillStyle = 'rgba(255,255,255,.75)'; c.font = `700 ${10 * d}px system-ui`; c.textAlign = 'right'; c.fillText(Math.round(br.bpm) + ' BPM', W - 6 * d, H - 6 * d);
}
function frame(ms) {
  const t = ms / 1000;
  const lc = logo.getContext('2d'); lc.clearRect(0, 0, logo.width, logo.height); drawLogo(lc, 0, 0, logo.width, t);
  const d = fitCanvas(strip), sc = strip.getContext('2d'); sc.setTransform(d, 0, 0, d, 0, 0); drawStrip(sc, strip.width / d, strip.height / d, t); sc.setTransform(1, 0, 0, 1, 0, 0);
  if (ctx) { const now = ctx.currentTime; let ns = nowStep; while (uiQueue.length && uiQueue[0].t <= now) { const q = uiQueue.shift(); if (q.step !== undefined) ns = q.step; else slices[q.b] = q.slice; }
    if (ns !== nowStep) { nowStep = ns; drawCells(); } }
  drawWave();
  requestAnimationFrame(frame);
}

// start
fillBreaks(); applyTheme(); showBay(); drawCells(); drawMutes();
// warm up the 4 bays' breaks in the background so the first PLAY doesn't stutter
S.bays.forEach((b, i) => setTimeout(() => { const br = getBreak(b.brk); if (br) renderBreak(br); }, 200 + i * 150));
requestAnimationFrame(frame);
document.addEventListener('visibilitychange', () => { if (document.hidden && playing) setPlay(false); });

// #selftest: renders every break and 4 bars offline and logs the levels (used to test the page headless)
if (location.hash === '#selftest') (async () => {
  const t0 = performance.now(); let bad = 0;
  for (const br of BREAKS) { renderBreak(br); let pk = 0; const d = br.buf.getChannelData(0); for (let i = 0; i < d.length; i++) pk = Math.max(pk, Math.abs(d[i])); if (!(pk > 0.1)) bad++; }
  console.log(`SELFTEST breaks=${BREAKS.length} bad=${bad} ms=${Math.round(performance.now() - t0)}`);
  for (const lane of LANES) for (const n of Object.keys(DRUMS[lane])) { const d = drumBuffer(lane, n).getChannelData(0); if (!d.every(Number.isFinite)) console.log('SELFTEST bad drum ' + n); }
  const spb = 60 / S.bpm / 4, len = Math.ceil((64 * spb + 2) * 44100);
  for (const cfg of ['default', 'allfx', 'stutter']) {
    const snap = JSON.parse(JSON.stringify(S));
    if (cfg !== 'default') { Object.assign(snap.fx, { crunch: 0.7, bits: 0.4, lp_mix: 0.6, hp_mix: 0.5, bp_mix: 0.5, nt_mix: 0.4, cb_mix: 0.6, fm_mix: 0.7, res: 0.9, chorus: 0.6, reverb: 0.6 }); snap.bays.forEach(b => { b.mute = false; b.rs = true; b.rc = true; b.dk = 1; b.ds = 0.5; }); }
    snap.stutter = cfg === 'stutter';
    const off = new OfflineAudioContext(2, len, 44100), e2 = new Engine(off, off.destination, snap, getBreak);
    for (let s = 0; s < 64; s++) e2.step(s, 0.05 + s * spb, spb);
    const buf = await off.startRendering(); let pk = 0, rms = 0, nan = false; const L = buf.getChannelData(0);
    for (let i = 0; i < L.length; i++) { const x = L[i]; if (!Number.isFinite(x)) nan = true; pk = Math.max(pk, Math.abs(x)); rms += x * x; }
    console.log(`SELFTEST ${cfg} peak=${pk.toFixed(3)} rms=${Math.sqrt(rms / L.length).toFixed(4)} nan=${nan}`);
  }
  console.log('SELFTEST done');
})();
