'use strict';
// ArpEZator web UI: prompt -> generator, chords, piano roll with GLIDE / FATTEN / STEP links, MASTER slider, mood ghost,
// transport (lookahead scheduler), MIDI import / export, WAV export, save slots, #selftest.

const $ = id => document.getElementById(id);
const A = ArpCore;   // BAR, PPQ, clamp come from theory.js
const LS = 'arpezator.v1';
const EXAMPLES = ['euphoric progressive intro', 'melodic minor pluck', 'dreamy lofi chill keys', 'dark techno rolling acid', 'epic trance 1/16 octave up', 'sad piano slow', 'happy house 124 bpm', 'tense cinematic triplet', 'hypnotic sparse bell in A minor', 'busy wild glide supersaw'];
const DEF_PROMPT = 'euphoric progressive melodic pluck';

let S = null, chords = [], plan = null, undoStack = [], genHist = [], genIdx = -1;
const toast = msg => { const t = $('toast'); t.textContent = msg; t.style.opacity = 1; clearTimeout(toast.h); toast.h = setTimeout(() => t.style.opacity = 0, 2400); };

function fromGen(g, keep) {
  const base = keep || {};
  return { v: 1, prompt: g.prompt, seed: g.seed, genre: g.genre, moods: g.moods, key: g.key, scale: g.scale, prog: g.prog, colour: g.colour, pedal: g.pedal, style: g.style, preset: g.preset,
    rate: g.rate, bpm: g.bpm, master: g.master, oct: g.oct, reg: g.reg, glide: g.glide, ratchet: g.ratchet, rolling: g.rolling, gate: g.gate, swing: g.swing, groove: g.groove ?? 0, fx: { ...g.fx },
    mix: base.mix || { arp: 0.8, chords: 0.45, bass: 0.5, kick: 0.5 }, fx2: base.fx2 || fx2Defaults(), sty: g.sty || null, arpSeed: g.arpSeed, progSeed: g.progSeed, notes: [], loop: base.loop ?? true, theme: base.theme || 'dark', edited: false, imported: false };
}
function save() { clearTimeout(save.h); save.h = setTimeout(() => { try { localStorage.setItem(LS, JSON.stringify(S)); } catch (e) {} }, 300); }
function load() { try { const s = JSON.parse(localStorage.getItem(LS)); if (s && s.v === 1 && s.prog && s.prog.length) return s; } catch (e) {} return null; }
function pushUndo() { undoStack.push(JSON.stringify(S)); if (undoStack.length > 40) undoStack.shift(); $('undo').disabled = false; }
$('undo').onclick = () => { const s = undoStack.pop(); if (!s) return; S = JSON.parse(s); rebuild(false); syncUI(); $('undo').disabled = !undoStack.length; toast('Undone'); };

// ---------------------------------------------------------------- derived state
function rebuild(regen) {
  chords = A.resolveProg(S);
  if (regen) { if (S.edited) { toast('Your note edits were replaced (UNDO brings them back)'); } S.notes = A.buildArp(S, chords); S.edited = false; if (S.sty && S.sty.amt > 0) A.guardArp(S.notes, guard, A.progTicks(S)); }
  plan = buildPlan(S, chords, S.notes);
  const m = A.analyzeMood(chords, S); moodTarget = FACES[m.mood]; $('moodName').textContent = MOOD_WORD[m.mood]; lastMood = m;
  if (eng) eng.apply(S);
  clampView(); drawChips(); drawRoll(); save();
}
// regenerate the arp from the current parameters (keeps an undo point if notes were hand-edited)
function regen() { if (S.edited) pushUndo(); rebuild(true); }

// ---------------------------------------------------------------- generator
function doGenerate(prompt, seed, push = true) {
  if (push) pushUndo();
  const keep = S ? { mix: S.mix, loop: S.loop, theme: S.theme, fx2: S.fx2 } : null;
  const g = A.generate(prompt, seed, styArg()); S = fromGen(g, keep);
  if (S.sty) A.guardProg(S, guard);
  if (push) { genHist = genHist.slice(0, genIdx + 1); genHist.push({ prompt, seed }); if (genHist.length > 60) genHist.shift(); genIdx = genHist.length - 1; }
  rebuild(true); syncUI();
}
const newSeed = () => 1 + Math.floor(Math.random() * 999998);
$('gen').onclick = async () => { const p = $('prompt').value.trim() || DEF_PROMPT; doGenerate(p, newSeed()); await setPlay(true, true); };
$('prompt').addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); $('gen').click(); $('prompt').blur(); } });
EXAMPLES.forEach(x => { const b = document.createElement('button'); b.textContent = x; b.onclick = () => { $('prompt').value = x; $('gen').click(); }; $('examples').appendChild(b); });
$('seed').addEventListener('keydown', e => { if (e.key === 'Enter') { const n = parseInt($('seed').value, 10); if (n >= 0) { doGenerate($('prompt').value.trim() || DEF_PROMPT, n); toast('Recalled seed ' + n); } $('seed').blur(); } });
$('prevGen').onclick = () => { if (genIdx <= 0) return toast('No earlier generation'); genIdx--; const h = genHist[genIdx]; $('prompt').value = h.prompt; doGenerate(h.prompt, h.seed, false); };
$('nextGen').onclick = () => { if (genIdx >= genHist.length - 1) return $('gen').click(); genIdx++; const h = genHist[genIdx]; $('prompt').value = h.prompt; doGenerate(h.prompt, h.seed, false); };

// ---------------------------------------------------------------- controls
const setP = el => el.style.setProperty('--p', ((el.value - el.min) / (el.max - el.min) * 100) + '%');
function typeIn(el, label, get, set) {
  const ask = () => { const v = prompt(label, String(get())); if (v === null) return; const n = parseFloat(v); if (Number.isFinite(n)) set(n); };
  el.addEventListener('contextmenu', e => { e.preventDefault(); ask(); });
  let tm = 0, x0 = 0, y0 = 0;
  el.addEventListener('pointerdown', e => { x0 = e.clientX; y0 = e.clientY; clearTimeout(tm); if (e.pointerType !== 'mouse') tm = setTimeout(ask, 600); });
  el.addEventListener('pointermove', e => { if (Math.abs(e.clientX - x0) + Math.abs(e.clientY - y0) > 8) clearTimeout(tm); });
  ['pointerup', 'pointercancel'].forEach(ev => el.addEventListener(ev, () => clearTimeout(tm)));
}
function slider(id, get, set, fmt, onDone) {
  const el = $(id), out = el.parentNode.querySelector('output');
  const show = () => { el.value = get(); setP(el); if (out) out.textContent = fmt ? fmt(get()) : Math.round(get() * 100); };
  el.addEventListener('input', () => { set(parseFloat(el.value)); show(); });
  if (onDone) el.addEventListener('change', onDone);
  typeIn(el, id.replace(/^(fx_|mix_)/, '').toUpperCase() + ' (' + el.min + ' to ' + el.max + ')', get, v => { set(clamp(v, +el.min, +el.max)); show(); if (onDone) onDone(); });
  sliders.push(show); return show;
}
const sliders = [];
function seg(id, items, get, set) {
  const box = $(id); box.innerHTML = '';
  for (const [val, label] of items) { const b = document.createElement('button'); b.textContent = label; b.onclick = () => { set(val); show(); }; b.dataset.v = val; box.appendChild(b); }
  const show = () => [...box.children].forEach(b => b.classList.toggle('on', b.dataset.v === String(get())));
  sliders.push(show); return show;
}
// MASTER: live morph (arp rebuilt on every move, cheap)
let masterUndo = false;
slider('master', () => S.master, v => { if (!masterUndo && S.edited) { pushUndo(); masterUndo = true; } S.master = v; rebuild(true); }, null, () => { masterUndo = false; });
slider('colour', () => S.colour, v => { S.colour = v; rebuild(true); }, v => Math.round(v * 100) + '%');
slider('gate', () => S.gate, v => { S.gate = v; rebuild(true); }, v => Math.round(v * 100) + '%');
slider('groove', () => S.groove ?? 0, v => { if (S.edited) pushUndo(); S.groove = v; rebuild(true); }, v => v < 0.005 ? 'EVEN' : Math.round(v * 100) + '%');
slider('swing', () => S.swing, v => { S.swing = v; plan = buildPlan(S, chords, S.notes); save(); }, v => Math.round(v * 100) + '%');
for (const k of ['tone', 'delay', 'reverb', 'pump']) slider('fx_' + k, () => S.fx[k], v => { S.fx[k] = v; if (eng) eng.apply(S); save(); });
for (const k of ['arp', 'chords', 'bass', 'kick']) slider('mix_' + k, () => S.mix[k], v => { S.mix[k] = v; if (eng) eng.apply(S); save(); });
seg('rateSeg', Object.keys(A.RATES).map(r => [r, r]), () => S.rate, v => { if (S.edited) pushUndo(); S.rate = v; rebuild(true); });
seg('octSeg', [[1, '1'], [2, '2'], [3, '3']], () => S.oct, v => { if (S.edited) pushUndo(); S.oct = v; rebuild(true); });
seg('presetSeg', A.PRESETS.map(p => [p, PRESET_LABEL[p]]), () => S.preset, v => { S.preset = v; if (eng) eng.apply(S); save(); preview(); });
seg('barsSeg', [[2, '2 BARS'], [4, '4 BARS'], [8, '8 BARS'], [16, '16']], () => Math.round(A.progTicks(S) / BAR), v => setBars(v));
const STYLE_LABEL = { motif: 'Motif (hypnotic, prog-house)', pedal: 'Pedal (trance)', up: 'Up', down: 'Down', updown: 'Up + down', converge: 'Converge (outside in)', broken: 'Broken thirds', walk: 'Random walk' };
A.STYLES.forEach(s => $('style').add(new Option(STYLE_LABEL[s], s)));
$('style').onchange = e => { if (S.edited) pushUndo(); S.style = e.target.value; rebuild(true); };
for (let k = 0; k < 12; k++) $('key').add(new Option(A.noteName(k, k, 'major') + ' / ' + A.noteName(k, k, 'minor') + 'm', k));
Object.keys(A.SCALES).forEach(s => $('scale').add(new Option(A.SCALE_LABEL[s] + ' (' + A.SCALE_FEEL[s] + ')', s)));
$('key').onchange = e => { pushUndo(); const k = +e.target.value; let d = A.mod12(k - S.key); if (d > 6) d -= 12; S.key = k; S.notes.forEach(n => n.p += d); rebuild(false); syncUI(); };
$('scale').onchange = e => { pushUndo(); S.scale = e.target.value; S.edited = false; rebuild(true); syncUI(); };
$('newChords').onclick = () => { pushUndo(); S.progSeed = newSeed(); S.prog = A.newProg(S, S.progSeed, styArg()); S.sty = stySnap(); if (S.sty) A.guardProg(S, guard); S.imported = false; S.edited = false; rebuild(true); syncUI(); };
$('newArp').onclick = () => { pushUndo(); S.arpSeed = newSeed(); S.sty = stySnap(); S.edited = false; rebuild(true); toast('New arp, same chords'); };
$('regOct').onclick = () => { S.reg = S.reg >= 1 ? -1 : S.reg + 1; regen(); syncUI(); };
function setBars(n) {
  pushUndo(); const cur = S.prog, out = []; let beats = 0, i = 0;
  while (beats < n * 4) { const s = { ...cur[i % cur.length] }; s.len = Math.min(s.len || 4, n * 4 - beats); out.push(s); beats += s.len; i++; }
  S.prog = out; S.edited = false; rebuild(true); syncUI();
}
function syncUI() {
  sliders.forEach(f => f());
  $('snap').classList.toggle('on', S.snapKey !== false);
  $('arpOn').classList.toggle('on', !S.arpOff); $('arpOn').textContent = S.arpOff ? 'ARP OFF' : 'ARP ON';
  $('style').value = S.style; $('key').value = S.key; $('scale').value = S.scale;
  $('seed').value = S.seed; $('bpm').textContent = S.bpm + ' BPM'; $('loop').classList.toggle('on', S.loop);
  $('regOct').textContent = 'REGISTER ' + (S.reg > 0 ? '+' : '') + S.reg;
  $('genInfo').textContent = S.imported ? 'chords from your MIDI file' : S.heard ? `learned by ear | ${A.keyName(S.key, S.scale)} | ${S.bpm} bpm | ${S.rate}` : `${A.keyName(S.key, S.scale)} ${A.SCALE_LABEL[S.scale].toLowerCase()} | ${S.genre} | ${S.style} | ${S.rate}`;
  document.body.classList.toggle('light', S.theme === 'light'); $('theme').textContent = S.theme === 'light' ? 'DARK' : 'LIGHT';
  $('undo').disabled = !undoStack.length;
}
$('theme').onclick = () => { S.theme = S.theme === 'light' ? 'dark' : 'light'; syncUI(); drawRoll(); save(); };
$('loop').onclick = () => { S.loop = !S.loop; syncUI(); save(); };
// tempo: drag, long-press / right-click to type
(() => { const b = $('bpm'); let y0 = 0, v0 = 0, drag = false;
  b.addEventListener('pointerdown', e => { b.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = S.bpm; drag = false; });
  b.addEventListener('pointermove', e => { if (!b.hasPointerCapture(e.pointerId)) return; const dy = y0 - e.clientY; if (Math.abs(dy) > 4) drag = true; if (drag) { S.bpm = clamp(Math.round(v0 + dy / 3), 60, 200); b.textContent = S.bpm + ' BPM'; if (eng) eng.apply(S); save(); } });
  const setBpm = v => { S.bpm = clamp(Math.round(v), 60, 200); syncUI(); if (eng) eng.apply(S); save(); };
  typeIn(b, 'Tempo (60 to 200 BPM)', () => S.bpm, setBpm);
  // a plain tap (no drag) opens the type box straight away
  b.addEventListener('pointerup', () => { if (drag) return; const v = prompt('Tempo (60 to 200 BPM)', S.bpm); if (v !== null && isFinite(parseFloat(v))) setBpm(parseFloat(v)); });
  // TAP TEMPO: average of the last taps; a 2 s pause starts over
  let taps = []; $('tap').onclick = () => { const now = performance.now(); if (taps.length && now - taps[taps.length - 1] > 2000) taps = []; taps.push(now); if (taps.length > 8) taps.shift();
    if (taps.length >= 2) { const avg = (taps[taps.length - 1] - taps[0]) / (taps.length - 1); let bpm = 60000 / avg; while (bpm < 60) bpm *= 2; while (bpm > 200) bpm /= 2; setBpm(bpm); }
    $('tap').classList.add('on'); setTimeout(() => $('tap').classList.remove('on'), 90); };
})();
$('arpOn').onclick = () => { S.arpOff = !S.arpOff; if (S.arpOff) { S.mixArpKeep = S.mix.arp; S.mix.arp = 0; } else S.mix.arp = S.mixArpKeep || 0.8; syncUI(); if (eng) eng.apply(S); save(); };
$('snap').onclick = () => { S.snapKey = S.snapKey === false; $('snap').classList.toggle('on', S.snapKey !== false); save(); };

// ---------------------------------------------------------------- chord chips
let playTick = -1;
function drawChips() {
  const box = $('chips'); box.innerHTML = '';
  chords.forEach((c, i) => {
    const b = document.createElement('button'); b.innerHTML = `<b>${c.name}</b><i>${c.roman}${c.len !== 4 ? ' &middot; ' + c.len + ' beats' : ''}</i>`;
    if (c.chrom) b.classList.add('chrom'); b.title = 'tap: next alternative, long-press: list';
    b.style.gridColumn = c.len > 4 ? 'span ' + Math.min(4, Math.round(c.len / 4)) : '';
    let tm = 0, long = false;
    b.addEventListener('pointerdown', e => { long = false; clearTimeout(tm); tm = setTimeout(() => { long = true; chordMenu(i, e.clientX, e.clientY); }, 520); });
    ['pointerup', 'pointercancel', 'pointerleave'].forEach(ev => b.addEventListener(ev, () => clearTimeout(tm)));
    b.addEventListener('contextmenu', e => { e.preventDefault(); clearTimeout(tm); long = true; chordMenu(i, e.clientX, e.clientY); });
    b.onclick = () => { if (long) return; setAlt(i, (S.prog[i].alt || 0) + 1); };
    box.appendChild(b);
  });
  $('keyInfo').textContent = A.keyName(S.key, S.scale) + ' ' + A.SCALE_LABEL[S.scale].toLowerCase();
}
function setAlt(i, alt) { pushUndo(); S.prog[i].alt = alt % (S.prog[i].fixed ? 3 : A.ALT_NAMES.length); S.edited = false; rebuild(true); previewChord(i); }
function chordMenu(i, x, y) {
  const n = S.prog[i].fixed ? 3 : A.ALT_NAMES.length, items = [];
  for (let a = 0; a < n; a++) { const tmp = { ...S, prog: S.prog.map((p, j) => j === i ? { ...p, alt: a } : p) }; const c = A.resolveProg(tmp)[i];
    items.push([(a === (S.prog[i].alt || 0) % n ? '> ' : '') + c.name + '  (' + (S.prog[i].fixed ? ['as imported', 'plain triad', 'no slash bass'][a] : A.ALT_NAMES[a]) + ')', () => setAlt(i, a)]); }
  // make this slot a copy of another slot (then cycle it on its own): e.g. chord 1, chord 1, chord 4, chord 3
  const names = A.resolveProg(S).map(c => c.name);
  S.prog.forEach((p, j) => { if (j !== i) items.push(['= chord ' + (j + 1) + ' (' + names[j] + ')', () => copySlot(j, i)]); });
  items.push(['Set ALL slots to this chord', () => { pushUndo(); const src = JSON.stringify(S.prog[i]); S.prog = S.prog.map(p => ({ ...JSON.parse(src), len: p.len || 4 })); S.edited = false; rebuild(true); }]);
  showMenu(x, y, 'Chord ' + (i + 1), items);
}
function copySlot(from, to) { pushUndo(); S.prog[to] = { ...JSON.parse(JSON.stringify(S.prog[from])), len: S.prog[to].len || 4 }; S.edited = false; rebuild(true); previewChord(to); }
function showMenu(x, y, title, items) {
  const m = $('menu'); m.innerHTML = `<div class="mt">${title}</div>`;
  for (const [label, fn] of items) { const b = document.createElement('button'); b.textContent = label; b.onclick = () => { hideMenu(); fn(); }; m.appendChild(b); }
  m.style.display = 'block'; const r = m.getBoundingClientRect();
  m.style.left = clamp(x - r.width / 2, 8, innerWidth - r.width - 8) + 'px'; m.style.top = clamp(y + 8, 8, innerHeight - r.height - 8) + 'px';
  setTimeout(() => document.addEventListener('pointerdown', hideOut, true), 0);
}
function hideOut(e) { if (!$('menu').contains(e.target)) hideMenu(); }
function hideMenu() { $('menu').style.display = 'none'; document.removeEventListener('pointerdown', hideOut, true); }

// ---------------------------------------------------------------- MIDI import
async function importMidi(file) {
  try {
    const midi = A.readMidi(new Uint8Array(await file.arrayBuffer())), r = A.chordsFromMidi(midi);
    useChords(r, midi.bpm);
  } catch (e) { toast("Couldn't read chords from that file: " + e.message); }
}
function useChords(r, bpm) {
    pushUndo(); S.key = r.key; S.scale = r.scale; S.imported = true; S.edited = false;
    const sc = A.SCALES[S.scale];
    S.prog = r.chords.map(c => { const root = A.mod12(c.root - r.key), bass = c.bass === null ? null : A.mod12(c.bass - r.key);
      return { fixed: { root, iv: c.iv, bass, chrom: c.iv.some(x => !sc.includes(A.mod12(root + x))) }, len: c.len, alt: 0, r: 0.5, r2: 0.9 }; });
    if (bpm && Math.abs(bpm - 120) > 0.5) S.bpm = clamp(Math.round(bpm), 60, 200);
    rebuild(true); syncUI(); toast(`Imported ${r.chords.length} chords in ${A.keyName(r.key, r.scale)}`);
}
$('midiFile').onchange = e => { const f = e.target.files[0]; if (f) importMidi(f); e.target.value = ''; };
(() => { const d = $('drop'), body = document.body;
  body.addEventListener('dragover', e => { e.preventDefault(); d.classList.add('over'); });
  body.addEventListener('dragleave', e => { if (e.target === body || e.target === d) d.classList.remove('over'); });
  body.addEventListener('drop', async e => { e.preventDefault(); d.classList.remove('over');
    const fs = [...e.dataTransfer.files], ents = [...(e.dataTransfer.items || [])].map(it => it.webkitGetAsEntry && it.webkitGetAsEntry()).filter(Boolean);
    // one .mid = import its chords (as before); many files, a folder or a zip = add to the library
    if (fs.length === 1 && !/\.midi?$/i.test(fs[0].name) && (/^audio\//.test(fs[0].type) || /\.(mp3|wav|m4a|aac|ogg|flac|aiff?)$/i.test(fs[0].name))) return hrFile(fs[0]);
    if (fs.length === 1 && /\.midi?$/i.test(fs[0].name) && !ents.some(x => x.isDirectory)) return importMidi(fs[0]);
    if (ents.some(x => x.isDirectory)) { const out = []; const walk = ent => new Promise(res => { if (ent.isFile) ent.file(f => { out.push(f); res(); }, () => res()); else { const rd = ent.createReader(), all = []; const more = () => rd.readEntries(async es => { if (!es.length) { for (const x of all) await walk(x); res(); } else { all.push(...es); more(); } }, () => res()); more(); } });
      for (const x of ents) await walk(x); return addToLibrary(out); }
    addToLibrary(fs); });
})();

// ---------------------------------------------------------------- audio + transport (lookahead scheduler like Mr. Mickens)
let ctx = null, eng = null, playing = false, timer = 0, nextTime = 0, curTick = 0;
const marks = [], lit = [];
async function ensureAudio() {
  if (ctx) { if (ctx.state !== 'running') await ctx.resume(); return; }
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch (e) {}
  try { const a = new Audio('data:audio/wav;base64,UklGRigAAABXQVZFZm10IBAAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAAAA'); a.loop = true; a.setAttribute('playsinline', ''); a.play().catch(() => {}); } catch (e) {}
  ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  eng = new Engine(ctx, ctx.destination); eng.apply(S);
  if (ctx.state !== 'running') await ctx.resume();
}
function tick() {
  if (ctx.state !== 'running') return;
  if (nextTime < ctx.currentTime - 0.05) nextTime = ctx.currentTime + 0.05;
  const td = 60 / S.bpm / PPQ;
  while (nextTime < ctx.currentTime + 0.12) {
    if (curTick >= plan.L) { if (S.loop) curTick = 0; else { setTimeout(() => setPlay(false), (nextTime - ctx.currentTime) * 1000 + 300); clearInterval(timer); timer = 0; return; } }
    schedTick(eng, S, plan, curTick, nextTime, td, (idx, t0, t1) => lit.push({ idx, t0, t1 }));
    if (curTick % 12 === 0) marks.push({ t: nextTime, tick: curTick });
    curTick++; nextTime += td;
  }
}
async function setPlay(on, restart) {
  await ensureAudio();
  if (on && playing && restart) { curTick = 0; nextTime = ctx.currentTime + 0.06; marks.length = 0; return; }
  if (on === playing) return; playing = on; $('play').textContent = on ? 'STOP' : 'PLAY'; $('play').classList.toggle('on', on);
  if (on) { curTick = 0; nextTime = ctx.currentTime + 0.06; marks.length = 0; tick(); clearInterval(timer); timer = setInterval(tick, 25); }
  else { clearInterval(timer); timer = 0; playTick = -1; lit.length = 0; drawRoll(); }
}
$('play').onclick = () => setPlay(!playing);
document.addEventListener('visibilitychange', () => { if (document.hidden && playing) setPlay(false); });
async function preview(p) {
  await ensureAudio(); if (playing) return;
  const t = ctx.currentTime + 0.02, pp = p ?? (S.notes[0] ? S.notes[0].p : 72);
  eng.lead([{ time: t, end: t + 0.25, p: pp, v: 0.8, gs: t, cv: 0 }]);
}
async function previewChord(i) { await ensureAudio(); if (playing) return; const c = chords[i], t = ctx.currentTime + 0.02; eng.chord(c.voicing, t, 0.9, 0.9); eng.bass(c.bassNote, t, 0.9, 1); }

// ---------------------------------------------------------------- piano roll
const roll = $('roll'), mini = $('mini');
let view = { start: 0, bars: 2 }, rows = { lo: 60, hi: 84 }, selLink = -1, frozenRows = null;
function fitCanvas(cv) { const r = cv.getBoundingClientRect(), d = Math.min(2, window.devicePixelRatio || 1), w = Math.round(r.width * d), h = Math.round(r.height * d); if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; } return d; }
const totalTicks = () => plan ? plan.L : BAR * 4;
function clampView() { const tb = Math.ceil(totalTicks() / BAR); view.bars = clamp(view.bars, 1, tb); view.start = clamp(Math.floor(view.start / BAR) * BAR, 0, Math.max(0, (tb - view.bars) * BAR)); }
const css = n => getComputedStyle(document.body).getPropertyValue(n).trim();
function geom() {
  const r = roll.getBoundingClientRect(), W = r.width, H = r.height, kx = 30, hy = 18;
  if (!frozenRows) { let lo = 127, hi = 0; for (const n of S.notes) { lo = Math.min(lo, n.p); hi = Math.max(hi, n.p); } if (lo > hi) { lo = 66; hi = 80; } lo -= 3; hi += 3;
    while (hi - lo < 20) { lo--; hi++; } rows = { lo, hi }; }
  const n = rows.hi - rows.lo + 1, rh = (H - hy) / n, vl = view.bars * BAR;
  return { W, H, kx, hy, rh, vl, x: t => kx + (t - view.start) / vl * (W - kx), y: p => hy + (rows.hi - p) * rh, t: x => view.start + (x - kx) / (W - kx) * vl, p: y => rows.hi - Math.floor((y - hy) / rh) };
}
function linkGeom(G, i) {
  const a = S.notes[i], b = S.notes[i + 1]; if (!b) return null;
  const ax0 = G.x(a.t), ax1 = G.x(a.t + a.d), amid = (ax0 + Math.max(ax1, ax0 + 3)) / 2, bx0 = G.x(b.t), bmid = (bx0 + Math.max(G.x(b.t + b.d), bx0 + 3)) / 2;
  const ya = G.y(a.p) + G.rh / 2, yb = G.y(b.p) + G.rh / 2, l = a.l, xs = Math.max(amid, Math.min(ax1, bx0 - 6));
  if (l.type === 'glide') { const xg = clamp(G.x(a.t + l.w * (b.t - a.t)), ax0 + 1, bx0 - 1); return { a, b, l, ya, yb, xs: Math.min(xs, xg), xg, bx0, amid, bmid, hx: xg, hy: ya }; }
  if (l.type === 'fatten') { const th = 2 + l.f * G.rh * 1.6; return { a, b, l, ya, yb, amid, bmid, bx0, th, hx: (amid + bmid) / 2, hy: (ya + yb) / 2 - th / 2 - 3 }; }
  const xm = (xs + bx0) / 2; return { a, b, l, ya, yb, xs, xm, bx0, hx: xm, hy: (ya + yb) / 2 };
}
function drawRoll() {
  if (!S || !roll.offsetWidth) return;
  const d = fitCanvas(roll), c = roll.getContext('2d'); c.setTransform(d, 0, 0, d, 0, 0);
  const G = geom(), { W, H, kx, hy, rh } = G, spb = A.RATES[S.rate], st = BAR / spb, vEnd = view.start + G.vl;
  c.clearRect(0, 0, W, H); c.fillStyle = css('--roll'); c.fillRect(0, 0, W, H);
  // rows
  for (let p = rows.lo; p <= rows.hi; p++) { const y = G.y(p), blk = [1, 3, 6, 8, 10].includes(p % 12); c.fillStyle = blk ? css('--rowB') : css('--rowA'); c.fillRect(kx, y, W - kx, rh); }
  // chord-tone rows inside each chord's span
  c.fillStyle = css('--chordRow');
  for (const ch of chords) { if (ch.tEnd <= view.start || ch.t >= vEnd) continue; const x0 = Math.max(kx, G.x(ch.t)), x1 = Math.min(W, G.x(ch.tEnd)); for (let p = rows.lo; p <= rows.hi; p++) if (ch.pcs.includes(p % 12)) c.fillRect(x0, G.y(p), x1 - x0, rh); }
  // grid
  for (let t = Math.ceil(view.start / st) * st; t <= vEnd; t += st) { const x = Math.round(G.x(t)) + 0.5, bar = t % BAR === 0, beat = t % PPQ === 0; if (!beat && G.vl / st > 80) continue;
    c.strokeStyle = bar ? css('--gridB') : beat ? css('--gridB') : css('--grid'); c.globalAlpha = bar ? 1 : beat ? 0.55 : 1; c.lineWidth = bar ? 1.5 : 1; c.beginPath(); c.moveTo(x, hy); c.lineTo(x, H); c.stroke(); }
  c.globalAlpha = 1;
  // header: bar numbers + chord names
  c.fillStyle = css('--card2'); c.fillRect(0, 0, W, hy);
  c.font = '700 10px system-ui'; c.textBaseline = 'middle';
  for (const ch of chords) { if (ch.tEnd <= view.start || ch.t >= vEnd) continue; const x0 = Math.max(kx, G.x(ch.t)); c.fillStyle = ch.chrom ? css('--pink') : css('--green'); c.fillText(ch.name, x0 + (ch.t % BAR === 0 ? 20 : 4), hy / 2); }
  for (let t = Math.ceil(view.start / BAR) * BAR; t < vEnd; t += BAR) { const x = G.x(t); c.fillStyle = css('--dim'); c.fillText(String(t / BAR + 1), x + 4, hy / 2); }
  // links (behind the notes)
  c.save(); c.beginPath(); c.rect(kx, hy, W - kx, H - hy); c.clip();
  const pink = css('--pink'), gold = css('--gold'), cyan = css('--cyan');
  for (let i = 0; i + 1 < S.notes.length; i++) {
    const a = S.notes[i], b = S.notes[i + 1]; if (b.t < view.start - BAR || a.t > vEnd) continue;
    const L = linkGeom(G, i), sel = i === selLink; c.lineCap = 'round'; c.lineJoin = 'round';
    if (L.l.type === 'glide') { c.strokeStyle = pink; c.lineWidth = sel ? 3.4 : 2.4; c.beginPath(); c.moveTo(L.xs, L.ya); c.lineTo(L.xg, L.ya); const k = (L.bx0 - L.xg) * 0.55; c.bezierCurveTo(L.xg + k, L.ya, L.bx0 - k, L.yb, L.bx0, L.yb); c.stroke(); }
    else if (L.l.type === 'fatten') { const k = (L.bmid - L.amid) * 0.5, t0 = 1.2, t1 = L.th / 2; c.fillStyle = gold; c.globalAlpha = sel ? 0.95 : 0.7; c.beginPath();
      c.moveTo(L.amid, L.ya - t0); c.bezierCurveTo(L.amid + k, L.ya - t0, L.bmid - k, L.yb - t1, L.bmid, L.yb - t1); c.lineTo(L.bmid, L.yb + t1); c.bezierCurveTo(L.bmid - k, L.yb + t1, L.amid + k, L.ya + t0, L.amid, L.ya + t0); c.closePath(); c.fill(); c.globalAlpha = 1; }
    else { c.strokeStyle = cyan; c.globalAlpha = sel ? 1 : 0.55; c.lineWidth = sel ? 2.4 : 1.4; c.beginPath(); c.moveTo(L.xs, L.ya); c.lineTo(L.xm, L.ya); c.lineTo(L.xm, L.yb); c.lineTo(L.bx0, L.yb); c.stroke(); c.globalAlpha = 1; }
    const hr = sel ? 6 : 3.2; c.fillStyle = L.l.type === 'glide' ? pink : L.l.type === 'fatten' ? gold : cyan; c.globalAlpha = sel ? 1 : 0.8; c.beginPath(); c.arc(L.hx, L.hy, hr, 0, 7); c.fill();
    if (sel) { c.strokeStyle = css('--ink'); c.lineWidth = 1.5; c.stroke(); } c.globalAlpha = 1;
  }
  // notes
  const now = ctx ? ctx.currentTime : 0, litSet = new Set(lit.filter(q => q.t0 <= now && q.t1 >= now).map(q => q.idx)), green = css('--green');
  S.notes.forEach((n, i) => {
    if (n.t + n.d < view.start || n.t > vEnd) return;
    const pl = i > 0 ? S.notes[i - 1].l : null, fat = pl && pl.type === 'fatten' ? pl.f : 0;
    const x = G.x(n.t), w = Math.max(3, G.x(n.t + n.d) - x - 1), hh = Math.min(rh * 1.8, rh * 0.74 * (1 + 0.9 * fat)), y = G.y(n.p) + rh / 2 - hh / 2;
    c.fillStyle = n.pass ? cyan : green; c.globalAlpha = 0.45 + 0.55 * A.playVel(S.notes, i);
    if (litSet.has(i)) { c.shadowColor = c.fillStyle; c.shadowBlur = 12; c.globalAlpha = 1; }
    c.beginPath(); c.roundRect ? c.roundRect(x, y, w, hh, Math.min(4, hh / 2)) : c.rect(x, y, w, hh); c.fill(); c.shadowBlur = 0; c.globalAlpha = 1;
  });
  // playhead
  if (playing && playTick >= view.start && playTick < vEnd) { const x = G.x(playTick); c.fillStyle = css('--ink'); c.globalAlpha = 0.7; c.fillRect(x - 1, hy, 2, H - hy); c.globalAlpha = 1; }
  c.restore();
  // keys
  for (let p = rows.lo; p <= rows.hi; p++) { const y = G.y(p), blk = [1, 3, 6, 8, 10].includes(p % 12); c.fillStyle = blk ? css('--keyB') : css('--keyW'); c.fillRect(0, y, kx - 2, rh - 0.5);
    if (p % 12 === 0 && rh >= 7) { c.fillStyle = '#5a4a80'; c.font = `700 ${Math.min(9, rh)}px system-ui`; c.fillText('C' + (Math.floor(p / 12) - 1), 2, y + rh / 2); } }
  c.fillStyle = css('--card2'); c.fillRect(0, 0, kx, hy);
  $('rollInfo').textContent = `bars ${view.start / BAR + 1}-${Math.min(totalTicks() / BAR, view.start / BAR + view.bars)} of ${totalTicks() / BAR} · ${S.notes.length} notes`;
  drawMini(); linkBar();
}
function drawMini() {
  const d = fitCanvas(mini), c = mini.getContext('2d'), W = mini.width / d, H = mini.height / d, L = totalTicks(); c.setTransform(d, 0, 0, d, 0, 0);
  c.clearRect(0, 0, W, H); c.fillStyle = css('--card2'); c.fillRect(0, 0, W, H);
  let lo = 127, hi = 0; for (const n of S.notes) { lo = Math.min(lo, n.p); hi = Math.max(hi, n.p); } const rg = Math.max(1, hi - lo);
  c.fillStyle = css('--green'); for (const n of S.notes) c.fillRect(n.t / L * W, H - 3 - (n.p - lo) / rg * (H - 6), Math.max(1, n.d / L * W), 2);
  c.strokeStyle = css('--cyan'); c.lineWidth = 1.5; c.strokeRect(view.start / L * W + 1, 1, view.bars * BAR / L * W - 2, H - 2);
  if (playing && playTick >= 0) { c.fillStyle = css('--ink'); c.fillRect(playTick / L * W, 0, 1.5, H); }
}
(() => { let drag = false; const go = e => { const r = mini.getBoundingClientRect(), L = totalTicks(); view.start = Math.floor(((e.clientX - r.left) / r.width * L - view.bars * BAR / 2) / BAR + 0.5) * BAR; clampView(); drawRoll(); };
  mini.addEventListener('pointerdown', e => { mini.setPointerCapture(e.pointerId); drag = true; $('follow').classList.remove('on'); go(e); });
  mini.addEventListener('pointermove', e => { if (drag) go(e); }); mini.addEventListener('pointerup', () => drag = false); })();
const ZOOMS = [1, 2, 4, 8, 16];
$('zoomIn').onclick = () => { const i = ZOOMS.indexOf(view.bars); view.bars = ZOOMS[Math.max(0, i - 1)]; clampView(); drawRoll(); };
$('zoomOut').onclick = () => { const i = ZOOMS.indexOf(view.bars); view.bars = ZOOMS[Math.min(ZOOMS.length - 1, i + 1)]; clampView(); drawRoll(); };
$('follow').onclick = () => $('follow').classList.toggle('on');

// roll pointer handling: tap empty = add, tap note = delete, drag note = move, link dot = bend / fatten, long-press = menu
let pd = null;
function hitTest(G, x, y) {
  if (y < G.hy && x > G.kx) { const t = G.t(x); const ci = chords.findIndex(c => t >= c.t && t < c.tEnd); if (ci >= 0) return { kind: 'chord', i: ci }; }
  let best = -1, bd = 16;
  for (let i = 0; i + 1 < S.notes.length; i++) { const L = linkGeom(G, i); if (!L || L.hx < G.kx - 4 || L.hx > G.W + 4) continue; const dd = Math.hypot(L.hx - x, L.hy - y); if (dd < bd && (i === selLink || dd < 11)) { bd = dd; best = i; } }
  if (best >= 0) return { kind: 'link', i: best };
  for (let i = S.notes.length - 1; i >= 0; i--) { const n = S.notes[i], nx = G.x(n.t), nw = Math.max(10, G.x(n.t + n.d) - nx), ny = G.y(n.p); if (x >= nx - 2 && x <= nx + nw && y >= ny - 3 && y <= ny + G.rh + 3) return { kind: 'note', i }; }
  return { kind: 'empty' };
}
const rel = e => { const r = roll.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
roll.addEventListener('contextmenu', e => e.preventDefault());
// SNAP TO KEY: notes you drag or tap land only on notes of the key (or the chord playing there); off = any note
function okPitch(p, t) { if (S.snapKey === false) return true; const pc = A.mod12(p - S.key), ch = A.chordAt(chords, t);
  return A.SCALES[S.scale].includes(pc) || (ch && ch.pcs.includes(A.mod12(p))); }
function snapPitch(p, t, dir) { if (okPitch(p, t)) return p; for (let k = 1; k < 7; k++) { const a = p + (dir >= 0 ? k : -k), b = p - (dir >= 0 ? k : -k); if (okPitch(a, t)) return a; if (okPitch(b, t)) return b; } return p; }
roll.addEventListener('pointerdown', e => {
  if (e.button === 2) { const [x, y] = rel(e), G = geom(), h = hitTest(G, x, y); if (h.kind === 'link') { selLink = h.i; drawRoll(); linkMenu(h.i, e.clientX, e.clientY); } else if (h.kind === 'note') noteMenu(h.i, e.clientX, e.clientY); else if (h.kind === 'chord') chordMenu(h.i, e.clientX, e.clientY); return; }
  roll.setPointerCapture(e.pointerId); const [x, y] = rel(e); frozenRows = { ...rows }; const G = geom(); const h = hitTest(G, x, y);
  pd = { x, y, cx: e.clientX, cy: e.clientY, h, moved: false, axis: null, start: view.start, undo: false };
  if (h.kind === 'link') { const n = S.notes[h.i]; pd.l0 = { ...n.l }; selLink = h.i; drawRoll(); }
  if (h.kind === 'note') { const n = S.notes[h.i]; pd.n0 = { t: n.t, p: n.p }; pd.note = n; }
  clearTimeout(pd.tm); if (h.kind !== 'empty') pd.tm = setTimeout(() => { if (pd && !pd.moved) { pd.long = true; if (h.kind === 'link') linkMenu(h.i, e.clientX, e.clientY); else if (h.kind === 'chord') chordMenu(h.i, e.clientX, e.clientY); else noteMenu(h.i, e.clientX, e.clientY); } }, 520);
});
roll.addEventListener('pointermove', e => {
  if (!pd || !roll.hasPointerCapture(e.pointerId) || pd.long) return;
  const [x, y] = rel(e), dx = x - pd.x, dy = y - pd.y, G = geom();
  if (!pd.moved && Math.hypot(dx, dy) < 7) return;
  if (!pd.moved) { pd.moved = true; clearTimeout(pd.tm); if (pd.h.kind !== 'empty') { pushUndo(); } }
  const spb = A.RATES[S.rate], st = BAR / spb;
  if (pd.h.kind === 'chord') return;
  if (pd.h.kind === 'note') {
    const n = pd.note, t = clamp(Math.round((pd.n0.t + (G.t(x) - G.t(pd.x))) / st) * st, 0, totalTicks() - st), p = clamp(snapPitch(pd.n0.p + Math.round(-dy / G.rh), t, -dy), 24, 108);
    if (t !== n.t || p !== n.p) { n.t = t; n.p = p; S.notes.sort((a, b) => a.t - b.t); S.edited = true; drawRoll(); if (p !== pd.lastP) { preview(p); pd.lastP = p; } }
  } else if (pd.h.kind === 'link') {
    const i = S.notes.indexOf(S.notes[pd.h.i]), a = S.notes[i], b = S.notes[i + 1], l = a.l;
    if (!pd.axis) pd.axis = l.type === 'glide' ? 'x' : l.type === 'fatten' ? 'y' : (Math.abs(dx) > Math.abs(dy) ? 'x' : 'y');
    if (pd.axis === 'x') { l.type = 'glide'; l.w = clamp((G.t(x) - a.t) / (b.t - a.t), 0, 0.97); }
    else { l.type = 'fatten'; l.f = clamp((pd.l0.type === 'fatten' ? pd.l0.f : 0.2) - dy / 70, 0, 1); }
    S.edited = true; plan = buildPlan(S, chords, S.notes); drawRoll();
  } else { // pan
    const L = totalTicks(); view.start = clamp(pd.start - (G.t(x) - G.t(pd.x)), 0, Math.max(0, L - view.bars * BAR)); $('follow').classList.remove('on'); drawRoll();
  }
});
roll.addEventListener('pointerup', e => {
  if (!pd) return; clearTimeout(pd.tm); const p0 = pd; pd = null;
  if (p0.long) { frozenRows = null; return; }
  if (p0.moved) { if (p0.h.kind === 'note') { const n = p0.note; S.notes = S.notes.filter(m => m === n || m.t !== n.t); } if (p0.h.kind === 'empty') view.start = Math.round(view.start / BAR) * BAR; frozenRows = null; plan = buildPlan(S, chords, S.notes); drawRoll(); save(); return; }
  const G = geom(), h = p0.h;
  if (h.kind === 'chord') { frozenRows = null; setAlt(h.i, (S.prog[h.i].alt || 0) + 1); return; }
  if (h.kind === 'note') { pushUndo(); S.notes.splice(h.i, 1); S.edited = true; selLink = -1; }
  else if (h.kind === 'link') { selLink = h.i; }
  else if (p0.x > G.kx && p0.y > G.hy) {
    const st = BAR / A.RATES[S.rate], t = Math.floor(G.t(p0.x) / st) * st, p = snapPitch(G.p(p0.y), t, 1);
    if (t >= 0 && t < totalTicks()) { pushUndo(); S.notes = S.notes.filter(n => n.t !== t); S.notes.push({ t, d: Math.max(6, Math.round(st * S.gate)), p, v: 0.75, pass: false, l: { type: 'step', w: 0.55, f: 0.5, c: 0 } }); S.notes.sort((a, b) => a.t - b.t); S.edited = true; selLink = -1; preview(p); }
  } else if (p0.x <= G.kx && p0.y > G.hy) preview(G.p(p0.y));
  frozenRows = null; plan = buildPlan(S, chords, S.notes); drawRoll(); save();
});
roll.addEventListener('pointercancel', () => { if (pd) clearTimeout(pd.tm); pd = null; frozenRows = null; });
function setLink(i, patch) { pushUndo(); Object.assign(S.notes[i].l, patch); S.edited = true; plan = buildPlan(S, chords, S.notes); drawRoll(); save(); }
function setAllLinks(l) { pushUndo(); S.notes.forEach(n => n.l = { ...l }); S.edited = true; plan = buildPlan(S, chords, S.notes); drawRoll(); save(); toast('All lines set to ' + l.type.toUpperCase()); }
function linkMenu(i, x, y) {
  const l = S.notes[i].l;
  showMenu(x, y, `Line ${i + 1} -> ${i + 2} (${l.type})`, [
    ['GLIDE (slide into the next note)', () => setLink(i, { type: 'glide' })], ['Glide early + long', () => setLink(i, { type: 'glide', w: 0.1 })], ['Glide late + quick', () => setLink(i, { type: 'glide', w: 0.85 })],
    ['FATTEN (next note louder)', () => setLink(i, { type: 'fatten', f: Math.max(0.5, l.f) })], ['STEP (abrupt change)', () => setLink(i, { type: 'step' })],
    ['Set ALL lines like this one', () => setAllLinks(l)], ['Reset ALL lines to STEP', () => setAllLinks({ type: 'step', w: 0.55, f: 0.5, c: 0 })],
  ]);
}
function noteMenu(i, x, y) {
  const n = S.notes[i];
  showMenu(x, y, 'Note', [['Delete', () => { pushUndo(); S.notes.splice(i, 1); S.edited = true; rebuild(false); }], ['Louder', () => { pushUndo(); n.v = clamp(n.v + 0.15, 0.1, 1); S.edited = true; rebuild(false); }],
    ['Softer', () => { pushUndo(); n.v = clamp(n.v - 0.15, 0.1, 1); S.edited = true; rebuild(false); }], ['Longer', () => { pushUndo(); n.d = Math.round(n.d * 1.5); S.edited = true; rebuild(false); }],
    ['Shorter', () => { pushUndo(); n.d = Math.max(6, Math.round(n.d / 1.5)); S.edited = true; rebuild(false); }], ['Octave up', () => { pushUndo(); n.p += 12; S.edited = true; rebuild(false); }]]);
}
function linkBar() {
  const ok = selLink >= 0 && selLink + 1 < S.notes.length, l = ok ? S.notes[selLink].l : null;
  ['lkGlide', 'lkFat', 'lkStep', 'lkAmt', 'lkAll'].forEach(id => $(id).disabled = !ok);
  $('lkGlide').classList.toggle('on', !!l && l.type === 'glide'); $('lkFat').classList.toggle('on', !!l && l.type === 'fatten'); $('lkStep').classList.toggle('on', !!l && l.type === 'step');
  $('lkInfo').textContent = !ok ? 'tap a line between two notes' : l.type === 'glide' ? 'GLIDE starts at ' + Math.round(l.w * 100) + '%' : l.type === 'fatten' ? 'FATTEN ' + Math.round(l.f * 100) + '%' : 'STEP';
  // STEP has no amount: the slider is greyed out and can't turn a STEP line into a GLIDE
  const amt = $('lkAmt'), step = !!l && l.type === 'step'; amt.disabled = !ok || step;
  if (ok) { amt.value = step ? 0 : l.type === 'fatten' ? l.f : l.w; amt.style.setProperty('--tint', step ? 'var(--line)' : l.type === 'fatten' ? 'var(--gold)' : 'var(--pink)'); setP(amt); }
  if (ok && step) $('lkInfo').textContent = 'STEP (no amount: pick GLIDE or FATTEN to use the slider)';
}
$('lkGlide').onclick = () => setLink(selLink, { type: 'glide' });
$('lkFat').onclick = () => setLink(selLink, { type: 'fatten' });
$('lkStep').onclick = () => setLink(selLink, { type: 'step' });
$('lkAll').onclick = () => setAllLinks(S.notes[selLink].l);
$('lkAmt').addEventListener('input', e => { const l = S.notes[selLink].l, v = +e.target.value; if (l.type === 'step') return; if (l.type === 'fatten') l.f = v; else l.w = Math.min(0.97, v); S.edited = true; plan = buildPlan(S, chords, S.notes); drawRoll(); save(); });

// ---------------------------------------------------------------- export + save
function download(blob, name) {
  const file = new File([blob], name, { type: blob.type });
  if (navigator.canShare && /iPhone|iPad|Android/i.test(navigator.userAgent) && navigator.canShare({ files: [file] })) { navigator.share({ files: [file], title: name }).catch(() => {}); return; }
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 4000); toast('Saved ' + name);
}
const fname = what => `ArpEZator ${A.keyName(S.key, S.scale)} ${S.bpm}bpm ${what}`;
function midiFile(what) {
  const tr = [];
  if (what !== 'chords') tr.push({ name: 'Arp', ch: 0, notes: A.arpMidiNotes(S.notes) });
  if (what !== 'arp') tr.push({ name: 'Chords', ch: 1, notes: A.chordMidiNotes(chords, false) });
  if (what === 'all') tr.push({ name: 'Bass', ch: 2, notes: chords.map(c => ({ t: c.t, d: c.len * PPQ - 2, p: c.bassNote, v: 0.7 })) });
  return A.writeMidi(S.bpm, tr);
}
$('midArp').onclick = () => download(new Blob([midiFile('arp')], { type: 'audio/midi' }), fname('arp') + '.mid');
$('midChords').onclick = () => download(new Blob([midiFile('chords')], { type: 'audio/midi' }), fname('chords') + '.mid');
$('midAll').onclick = () => download(new Blob([midiFile('all')], { type: 'audio/midi' }), fname('all') + '.mid');
$('wav').onclick = async () => { toast('Rendering...'); const loops = A.progTicks(S) <= BAR * 2 ? 2 : 1; const buf = await renderOffline(S, chords, S.notes, loops); download(wavBlob(buf), fname('') .trim() + '.wav'); };
let saveArmed = false;
for (let k = 1; k <= 4; k++) { const b = document.createElement('button'); b.id = 'slot' + k; b.textContent = k; b.onclick = () => slot(k); $('slots').appendChild(b); }
function slotKey(k) { return LS + '.slot' + k; }
function drawSlots() { for (let k = 1; k <= 4; k++) { let full = false; try { full = !!localStorage.getItem(slotKey(k)); } catch (e) {} $('slot' + k).classList.toggle('full', full); } $('saveArm').classList.toggle('on', saveArmed); }
$('saveArm').onclick = () => { saveArmed = !saveArmed; drawSlots(); };
function slot(k) {
  try {
    if (saveArmed) { localStorage.setItem(slotKey(k), JSON.stringify(S)); saveArmed = false; toast('Saved to slot ' + k); }
    else { const s = localStorage.getItem(slotKey(k)); if (!s) return toast('Slot ' + k + ' is empty (press SAVE TO first)'); pushUndo(); const th = S.theme; S = JSON.parse(s); S.theme = th; rebuild(false); syncUI(); toast('Loaded slot ' + k); }
  } catch (e) { toast('Saving is blocked in this browser'); }
  drawSlots();
}
$('reset').onclick = () => { if (!confirm('Start over with a fresh idea?')) return; pushUndo(); doGenerate(DEF_PROMPT, newSeed(), false); };
function fbText() { return `ArpEZator feedback\n\n(what I liked / what to change)\n\n---\nprompt: ${S.prompt}\nseed: ${S.seed}\n${A.keyName(S.key, S.scale)} ${S.scale}, ${S.bpm} bpm, ${S.rate}, style ${S.style}, master ${Math.round(S.master * 100)}\nchords: ${chords.map(c => c.name).join(' ')}\n${navigator.userAgent}`; }
$('fbMail').onclick = e => { e.preventDefault(); location.href = 'mailto:?subject=' + encodeURIComponent('ArpEZator feedback') + '&body=' + encodeURIComponent(fbText()); };
$('fbSms').onclick = e => { e.preventDefault(); location.href = 'sms:?&body=' + encodeURIComponent(fbText()); };

// ---------------------------------------------------------------- STYLE knob + profiles (built-in "Professor B5" or ones learned from the user's library)
const LS_STY = LS + '.style';
let styPref = { amt: 0, prof: 'Professor B5' }, profiles = [], lib = [], guard = { ch: new Set(), arp: new Set() };
try { styPref = { ...styPref, ...JSON.parse(localStorage.getItem(LS_STY) || '{}') }; } catch (e) {}
const curProf = () => profiles.find(p => p.name === styPref.prof) || A.BUILTIN_PROFILE;
const styArg = () => styPref.amt > 0 ? { amt: styPref.amt, prof: curProf() } : null;
function stySnap() { if (!(styPref.amt > 0)) return null; const P = curProf(), a = P.arp; return { amt: styPref.amt, name: P.name, arp: a ? { step: a.step, rest: a.rest, accent: a.accent, ratchet: a.ratchet, motif: a.motif } : null }; }
function saveSty() { try { localStorage.setItem(LS_STY, JSON.stringify(styPref)); } catch (e) {} }
function drawProfiles() {
  const sel = $('styleProf'); sel.innerHTML = ''; [A.BUILTIN_PROFILE, ...profiles].forEach(p => sel.add(new Option(p.name, p.name)));
  const P0 = curProf(); sel.value = P0.name; $('styleLbl').textContent = 'STYLE: ' + P0.name;   // (a missing profile falls back to the built-in, the choice is kept)
  const P = curProf(); $('profRename').disabled = $('profDel').disabled = !!P.builtin; $('profInfo').textContent = profSummary(P);
}
function profSummary(P) {
  const top = []; for (const f in P.fam || {}) for (const a in P.fam[f].tr) for (const b in P.fam[f].tr[a]) top.push([a + '>' + b, P.fam[f].tr[a][b]]);
  top.sort((x, y) => y[1] - x[1]); const r = P.arp && P.arp.rate ? Object.entries(P.arp.rate).sort((x, y) => y[1] - x[1])[0] : null;
  return `${P.name}${P.n ? ` (${P.n.chords} progressions, ${P.n.arps} arps)` : ''}: ${top.slice(0, 3).map(t => t[0]).join(', ')}${r ? ' · arps ' + r[0] + ' ' + Math.round(r[1] * 100) + '%' : ''}`;
}
function initStyleUI() {
  slider('styleAmt', () => styPref.amt, v => { styPref.amt = v; saveSty(); S.sty = stySnap(); rebuild(true); }, v => Math.round(v * 100) + '%');
  $('styleProf').onchange = e => { styPref.prof = e.target.value; saveSty(); drawProfiles(); S.sty = stySnap(); rebuild(true); toast('Style: ' + styPref.prof + (styPref.amt > 0 ? '' : ' (raise STYLE, then GENERATE)')); };
  drawProfiles();
}
$('learn').onclick = async () => {
  if (!lib.length) return toast('Add some MIDI files to the library first');
  const name = (prompt('Name this style', 'My style') || '').trim(); if (!name) return;
  if (name === A.BUILTIN_PROFILE.name) return toast('That name is taken by the built-in style');
  const P = A.learnProfile(lib, name, 1); P.n = P.n || {}; P.made = Date.now();
  try { await DB.put('prof', P); } catch (e) { toast('Could not store the style in this browser'); }
  profiles = profiles.filter(p => p.name !== name).concat([P]); styPref.prof = name; if (!(styPref.amt > 0)) styPref.amt = 0.7; saveSty(); drawProfiles(); sliders.forEach(f => f());
  S.sty = stySnap(); rebuild(true); toast(`Learned "${name}" from ${P.n.chords} progressions + ${P.n.arps} arps. Press GENERATE.`);
};
$('profRename').onclick = async () => { const P = curProf(); if (P.builtin) return; const nn = (prompt('Rename style', P.name) || '').trim(); if (!nn || nn === P.name || nn === A.BUILTIN_PROFILE.name || profiles.some(p => p.name === nn)) return;
  try { await DB.del('prof', P.name); P.name = nn; await DB.put('prof', P); } catch (e) {} styPref.prof = nn; saveSty(); drawProfiles(); };
$('profDel').onclick = async () => { const P = curProf(); if (P.builtin || !confirm('Delete the style "' + P.name + '"?')) return; try { await DB.del('prof', P.name); } catch (e) {} profiles = profiles.filter(p => p !== P); drawProfiles(); S.sty = stySnap(); rebuild(true); };

// ---------------------------------------------------------------- IndexedDB (library items + learned profiles stay in this browser)
const DB = (() => {
  let p = null;
  const open = () => p || (p = new Promise((res, rej) => { const r = indexedDB.open('arpezator', 1);
    r.onupgradeneeded = () => { const d = r.result; if (!d.objectStoreNames.contains('lib')) d.createObjectStore('lib', { keyPath: 'id' }); if (!d.objectStoreNames.contains('prof')) d.createObjectStore('prof', { keyPath: 'name' }); };
    r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); }));
  const tx = async (st, mode, fn) => { const d = await open(); return new Promise((res, rej) => { const t = d.transaction(st, mode), q = fn(t.objectStore(st)); t.oncomplete = () => res(q ? q.result : undefined); t.onerror = () => rej(t.error); }); };
  return { all: st => tx(st, 'readonly', s => s.getAll()), put: (st, v) => tx(st, 'readwrite', s => { (Array.isArray(v) ? v : [v]).forEach(x => s.put(x)); }), del: (st, k) => tx(st, 'readwrite', s => s.delete(k)), clear: st => tx(st, 'readwrite', s => s.clear()) };
})();

// ---------------------------------------------------------------- MIDI library
async function loadLibrary() {
  try { lib = (await DB.all('lib')) || []; profiles = (await DB.all('prof')) || []; } catch (e) { lib = []; profiles = []; }
  lib.sort((a, b) => a.name.localeCompare(b.name)); guard = A.buildGuard(lib); drawLib(); drawProfiles();
}
// zip: read the central directory, inflate with the browser's DecompressionStream (JSZip from cdnjs as a fallback)
async function unzip(buf) {
  if (typeof DecompressionStream === 'undefined') {
    if (!window.JSZip) await new Promise((res, rej) => { const sc = document.createElement('script'); sc.src = 'https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js'; sc.onload = res; sc.onerror = () => rej(new Error('zip support did not load')); document.head.appendChild(sc); });
    const z = await JSZip.loadAsync(buf), out = []; for (const n in z.files) if (/\.midi?$/i.test(n) && !/(^|\/)(__MACOSX|\.)/.test(n)) out.push({ name: n.split('/').pop(), bytes: await z.files[n].async('uint8array') }); return out;
  }
  const u8 = new Uint8Array(buf), d = new DataView(buf); let e = -1;
  for (let i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) if (d.getUint32(i, true) === 0x06054b50) { e = i; break; }
  if (e < 0) throw new Error('not a zip file');
  const n = d.getUint16(e + 10, true), out = []; let o = d.getUint32(e + 16, true);
  for (let k = 0; k < n && d.getUint32(o, true) === 0x02014b50; k++) {
    const meth = d.getUint16(o + 10, true), csz = d.getUint32(o + 20, true), nl = d.getUint16(o + 28, true), xl = d.getUint16(o + 30, true), cl = d.getUint16(o + 32, true), lho = d.getUint32(o + 42, true);
    const name = new TextDecoder().decode(u8.subarray(o + 46, o + 46 + nl)); o += 46 + nl + xl + cl;
    if (!/\.midi?$/i.test(name) || /(^|\/)(__MACOSX|\.)/.test(name)) continue;
    const ds = lho + 30 + d.getUint16(lho + 26, true) + d.getUint16(lho + 28, true), raw = u8.subarray(ds, ds + csz);
    let bytes = null; if (meth === 0) bytes = raw.slice(); else if (meth === 8) bytes = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer());
    if (bytes) out.push({ name: name.split('/').pop(), bytes });
  }
  return out;
}
// files (File objects or {name, bytes}) -> parsed items in IndexedDB
async function addToLibrary(files) {
  const ents = [];
  for (const f of files) {
    try {
      if (f.bytes) ents.push(f);
      else if (/\.zip$/i.test(f.name)) ents.push(...await unzip(await f.arrayBuffer()));
      else if (/\.midi?$/i.test(f.name)) ents.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
    } catch (e) { toast(f.name + ': ' + e.message); }
  }
  if (!ents.length) return toast('No .mid files found');
  const add = [], bad = [];
  for (const en of ents) {
    try { const it = A.libItem(en.name, en.bytes); it.id = it.name + '|' + en.bytes.length + '|' + A.hashStr(String.fromCharCode.apply(null, en.bytes.subarray(0, 4096))); add.push(it); }
    catch (e) { bad.push(en.name); }
  }
  try { await DB.put('lib', add); } catch (e) { toast('This browser blocks storage; the library lasts until reload'); }
  const ids = new Set(add.map(x => x.id)); lib = lib.filter(x => !ids.has(x.id)).concat(add).sort((a, b) => a.name.localeCompare(b.name));
  guard = A.buildGuard(lib); drawLib();
  toast(`Added ${add.length} file${add.length === 1 ? '' : 's'}${bad.length ? `, skipped ${bad.length} (no chords or notes)` : ''}`);
  return { added: add.length, skipped: bad.length };
}
['libFiles', 'libDir', 'libZip'].forEach(id => $(id).onchange = e => { const fs = [...e.target.files]; e.target.value = ''; if (fs.length) addToLibrary(fs); });
$('libSearch').addEventListener('input', () => drawLib());
$('libClear').onclick = async () => { if (!lib.length || !confirm('Remove all ' + lib.length + ' files from the library? (learned styles are kept)')) return; try { await DB.clear('lib'); } catch (e) {} lib = []; guard = A.buildGuard(lib); drawLib(); };
const libDesc = it => `${A.keyName(it.key, it.scale)} · ${it.bars} bar${it.bars === 1 ? '' : 's'} · ${it.kind === 'arp' ? 'arp ' + it.rate : it.chords.length + ' chords'}`;
function drawLib() {
  const q = $('libSearch').value.trim().toLowerCase(), box = $('libList'); box.innerHTML = '';
  const nA = lib.filter(x => x.kind === 'arp').length; $('libInfo').textContent = lib.length ? `${lib.length} files · ${lib.length - nA} progressions · ${nA} arps` : 'empty';
  const show = lib.filter(it => !q || (it.name + ' ' + libDesc(it) + ' ' + (it.kind === 'arp' ? 'arp' : 'chords')).toLowerCase().includes(q)).slice(0, 300);
  for (const it of show) {
    const row = document.createElement('div'); row.className = 'li' + (it.kind === 'arp' ? ' arp' : '');
    const pl = document.createElement('button'); pl.textContent = '▶'; pl.title = 'listen'; pl.onclick = () => audition(it, pl);
    const tx = document.createElement('div'); tx.className = 'lt'; const b = document.createElement('b'); b.textContent = it.name; const i = document.createElement('i'); i.textContent = libDesc(it); tx.append(b, i);
    const use = document.createElement('button'); use.textContent = it.kind === 'arp' ? 'USE ARP' : 'USE CHORDS'; use.style.setProperty('--tint', it.kind === 'arp' ? 'var(--cyan)' : 'var(--green)');
    use.onclick = () => it.kind === 'arp' ? useArp(it) : useChords({ key: it.key, scale: it.scale, chords: it.chords }, it.bpm);
    row.append(pl, tx, use); box.appendChild(row);
  }
}
// listen to a library item with the built-in synth (first 4 bars), on its own output so STOP cuts it at once
let aud = null;
async function audition(it, btn) {
  await ensureAudio(); const was = aud && aud.btn === btn;
  if (aud) { try { aud.g.disconnect(); } catch (e) {} aud.btn.textContent = '▶'; aud.btn.classList.remove('on'); clearTimeout(aud.tm); aud = null; }
  if (was) return; if (playing) await setPlay(false);
  const g = ctx.createGain(); g.connect(ctx.destination); const e2 = new Engine(ctx, g); e2.apply(S);
  const td = 60 / S.bpm / PPQ, t0 = ctx.currentTime + 0.05, lim = BAR * 4;
  if (it.kind === 'arp') { for (const n of it.notes) if (n.t < lim) e2.lead([{ time: t0 + n.t * td, end: t0 + (n.t + Math.max(6, n.d)) * td, p: n.p, v: 0.55 + 0.4 * n.v, gs: t0 + n.t * td, cv: 0 }]); }
  else { let b = 0; it.chords.forEach((c, i) => { if (b * PPQ < lim) { const vo = it.vo && it.vo[i] && it.vo[i].length >= 2 ? it.vo[i] : c.iv.map(x => 48 + A.mod12(c.root) + x); const T = t0 + b * PPQ * td, D = Math.min(c.len, 16 - b) * PPQ * td;
    e2.chord(vo.filter(p => p >= 45), T, D, 0.9); e2.bass(36 + A.mod12(c.bass ?? c.root) + (A.mod12(c.bass ?? c.root) < 2 ? 12 : 0), T, D, 1); } b += c.len; }); }
  btn.textContent = '■'; btn.classList.add('on');
  const len = Math.min(lim, (it.kind === 'arp' ? it.bars : it.chords.reduce((a, c) => a + c.len, 0) / 4) * BAR);
  aud = { g, btn, tm: setTimeout(() => { if (aud && aud.btn === btn) { btn.textContent = '▶'; btn.classList.remove('on'); aud = null; } }, len * td * 1000 + 1500) };
}
// an arp file -> the roll: moved to the current key, snapped to its grid, repeated / cut to the progression; overlapping notes become GLIDE lines
function useArp(it) {
  pushUndo(); const L = A.progTicks(S), st = BAR / A.RATES[it.rate], len = Math.max(1, it.bars) * BAR; let sh = A.mod12(S.key - it.key); if (sh > 6) sh -= 12;
  const out = [], seen = new Set();
  for (let base = 0; base < L; base += len) for (const n of it.notes) { const t = base + Math.round(n.t / st) * st, p = clamp(n.p + sh, 24, 108); if (t >= L || seen.has(t + ':' + p)) continue; seen.add(t + ':' + p);
    out.push({ t, d: Math.max(6, Math.round(n.d / st * st) || st), p, v: clamp(n.v || 0.75, 0.1, 1), pass: false, l: { type: 'step', w: 0.55, f: 0.5, c: 0 }, end: n.t + n.d + base }); }
  out.sort((a, b) => a.t - b.t || a.p - b.p);
  for (let i = 0; i + 1 < out.length; i++) if (out[i].end > out[i + 1].t + 2 && out[i + 1].t > out[i].t) out[i].l = { type: 'glide', w: 0.6, f: 0.5, c: 0 };
  out.forEach(n => delete n.end);
  S.rate = it.rate; S.notes = out; S.edited = true; selLink = -1; rebuild(false); syncUI(); toast(`Arp loaded (${out.length} notes, moved ${sh >= 0 ? '+' : ''}${sh} to ${A.keyName(S.key, S.scale)})`);
}

// ---------------------------------------------------------------- FX rack UI
const FX_HINT = { echo: 'repeats that wobble like tape', filt: 'filter moving in time', shim: 'big space, octave-up shimmer', crys: 'backwards sparkly grains', drive: 'warm saturation', wide: 'spreads the sound left / right' };
function buildFxUI() {
  const sc = $('scenes'); Object.keys(FX_SCENES).forEach(n => { const b = document.createElement('button'); b.textContent = n.toUpperCase(); b.dataset.v = n; b.onclick = () => { S.fx2 = sceneFx(n, S.fx2.chords); fxChanged(); toast('FX scene: ' + n); }; sc.appendChild(b); });
  sliders.push(() => { [...sc.children].forEach(b => b.classList.toggle('on', b.dataset.v === S.fx2.scene)); $('fxScene').textContent = S.fx2.scene || 'custom';
    $('fxChords').classList.toggle('on', !!S.fx2.chords); $('fxChords').textContent = 'FX ON CHORDS: ' + (S.fx2.chords ? 'ON' : 'OFF');
    for (const k of ['drive', 'filt', 'echo', 'crys', 'shim', 'wide']) { const on = !!S.fx2[k].on; $('fxon_' + k).classList.toggle('on', on); $('fxb_' + k).classList.toggle('off', !on); } });
  const box = $('fxBlocks');
  const defs = [['echo', 'TAPE ECHO', [['fb', 'FEEDBACK'], ['wob', 'WOBBLE'], ['mix', 'MIX']], ['time', Object.keys(ECHO_SYNC)]], ['filt', 'FILTER SWEEP', [['cut', 'CUTOFF'], ['res', 'RESO'], ['depth', 'LFO DEPTH']], ['rate', Object.keys(LFO_SYNC)], ['type', ['lp', 'hp']]],
    ['shim', 'SHIMMER', [['size', 'SIZE'], ['amt', 'SHIMMER'], ['mix', 'MIX']]], ['crys', 'CRYSTAL', [['mix', 'MIX']]], ['drive', 'DRIVE', [['amt', 'AMOUNT'], ['tone', 'TONE']]], ['wide', 'WIDE', [['amt', 'WIDTH']]]];
  for (const [k, label, ctls, seg1, seg2] of defs) {
    const b = document.createElement('div'); b.className = 'fxb'; b.id = 'fxb_' + k;
    b.innerHTML = `<div class="fxh"><button id="fxon_${k}">${label}</button><small>${FX_HINT[k]}</small></div>` + (seg1 ? `<div class="seg" id="fxs_${k}"></div>` : '') + (seg2 ? `<div class="seg" id="fxs2_${k}"></div>` : '') +
      ctls.map(([c, l]) => `<div class="ctl"><span>${l}</span><input type="range" id="fx2_${k}_${c}" min="0" max="1" step="0.01" style="--tint:var(--purple)"><output></output></div>`).join('');
    box.appendChild(b);
    $('fxon_' + k).onclick = () => { S.fx2[k].on = S.fx2[k].on ? 0 : 1; S.fx2.scene = 'custom'; fxChanged(); };
    for (const [c] of ctls) slider(`fx2_${k}_${c}`, () => S.fx2[k][c], v => { S.fx2[k][c] = v; S.fx2.scene = 'custom'; fxChanged(true); });
    if (seg1) seg('fxs_' + k, seg1[1].map(x => [x, x.toUpperCase()]), () => S.fx2[k][seg1[0]], v => { S.fx2[k][seg1[0]] = v; S.fx2.scene = 'custom'; fxChanged(); });
    if (seg2) seg('fxs2_' + k, seg2[1].map(x => [x, x === 'lp' ? 'LOW-PASS' : 'HIGH-PASS']), () => S.fx2[k][seg2[0]], v => { S.fx2[k][seg2[0]] = v; S.fx2.scene = 'custom'; fxChanged(); });
  }
  $('fxChords').onclick = () => { S.fx2.chords = !S.fx2.chords; fxChanged(); };
}
function fxChanged(quiet) { if (eng) eng.apply(S); save(); if (!quiet) syncUI(); else { $('fxScene').textContent = S.fx2.scene; [...$('scenes').children].forEach(b => b.classList.toggle('on', b.dataset.v === S.fx2.scene)); } }

// ---------------------------------------------------------------- LISTEN: hear music (mic or a file), learn tempo / key / chord loop / feel,
// then write a NEW arp (and voice the chords) in that style with the normal generator. The melody it heard is never copied. All local.
const HR_MAX = 12, HR_MIN = 4;
const HR = { st: 'off', mic: null, res: null, pre: null, chroma: new Float32Array(12), ear: 0, timer: 0, fbuf: null };
const hrName = (c, h) => A.noteName(c.root, h.key, h.scale) + Listen.SUF[c.type];
const hrKey = h => A.noteName(h.key, h.key, h.scale) + ' ' + h.scale;
function hrShow(st, sub) {
  HR.st = st; $('lsBox').hidden = st === 'off'; $('listen').classList.toggle('on', st === 'mic'); $('listenTxt').textContent = st === 'mic' ? 'STOP' : 'LISTEN';
  $('lsState').textContent = { mic: 'LISTENING...', busy: 'THINKING...', res: 'HEARD IT!', err: 'HMM...' }[st] || ''; if (sub !== undefined) $('lsSub').textContent = sub; $('lsRes').hidden = st !== 'res';
}
const hrSession = t => { try { if (navigator.audioSession) navigator.audioSession.type = t; } catch (e) {} };
async function hrStart() {
  if (HR.st === 'mic') return hrStop();
  if (HR.st === 'busy') return;
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return hrShow('err', "This browser can't use the microphone here (it needs https). Try LISTEN TO A FILE.");
  await setPlay(false); await ensureAudio(); hrSession('play-and-record');
  try { HR.mic = await Listen.startMic(ctx); }
  catch (e) { hrSession('playback'); return hrShow('err', e && (e.name === 'NotAllowedError' || e.name === 'SecurityError') ? 'Microphone access was blocked. Allow it in your browser settings, or use LISTEN TO A FILE.' : 'No microphone available (' + (e && e.name) + '). Try LISTEN TO A FILE.'); }
  HR.fbuf = new Float32Array(HR.mic.analyser.frequencyBinCount); HR.chroma = new Float32Array(12);
  hrShow('mic', `Play music near your device. About ${HR_MAX} s; tap the ring or STOP to finish early.`);
  HR.timer = setInterval(() => { if (HR.mic && HR.mic.seconds >= HR_MAX) hrStop(); }, 150);
}
function hrStop() {
  clearInterval(HR.timer); const m = HR.mic; HR.mic = null; if (!m) return;
  const secs = m.seconds, x = m.stop(); hrSession('playback');
  if (secs < HR_MIN) return hrShow('err', `Only ${secs.toFixed(1)} s heard. Let it listen for at least ${HR_MIN} s.`);
  hrAnalyze(x, m.sr);
}
async function hrFile(file) {
  if (HR.mic) { clearInterval(HR.timer); HR.mic.stop(); HR.mic = null; hrSession('playback'); }
  hrShow('busy', 'Reading ' + file.name + '...'); await setPlay(false);
  try { await ensureAudio(); const ab = await file.arrayBuffer(); const buf = await new Promise((res, rej) => { const p = ctx.decodeAudioData(ab, res, rej); if (p && p.then) p.then(res, rej); });
    hrAnalyze(Listen.mono(buf), buf.sampleRate); }
  catch (e) { hrShow('err', "Couldn't decode that file. Try an mp3, wav or m4a."); }
}
function hrAnalyze(x, sr) {
  hrShow('busy', 'Working out tempo, key and chords...');
  setTimeout(() => { let r; try { r = Listen.analyze(x, sr); } catch (e) { r = { ok: false, why: e.message }; }
    if (!r.ok) return hrShow('err', r.why === 'too quiet' ? 'That was too quiet. Turn the music up or move closer, then LISTEN again.' : `Couldn't analyse that (${r.why}).`);
    HR.res = r; HR.pre = JSON.stringify(S); HR.chroma = Float32Array.from(r.chroma); hrResult(r); hrApply(newSeed(), false); }, 40);
}
function hrResult(h) {
  const pct = v => Math.round(v * 100), lvl = h.conf >= 0.7 ? 'high' : h.conf >= 0.4 ? 'medium' : 'low';
  $('lsHeard').innerHTML = `<span class="lc">HEARD (${lvl} confidence${lvl === 'low' ? ': try a louder or longer clip' : ''})</span><br><b>${h.bpmInt} BPM</b> &middot; <b>${hrKey(h)}</b><span class="pr">${h.chords.map(c => hrName(c, h)).join(' &ndash; ')}</span>`;
  $('lsConf').innerHTML = [['TEMPO', h.tempoConf, 'var(--cyan)'], ['KEY', h.keyConf, 'var(--purple)'], ['CHORDS', h.chordConf, 'var(--green)']].map(([n, v, c]) => `<div>${n} ${pct(v)}%<i style="--p:${pct(v)}%;--c:${c}"></i></div>`).join('');
  $('lsFeel').textContent = `Feel: ${h.rate === '1/16T' ? '1/16 triplets' : h.rate} arp, groove ${pct(h.groove)}%, ${h.centroid < 1100 ? 'dark' : h.centroid > 2600 ? 'bright' : 'warm'} tone -> ${h.oct} octave${h.oct > 1 ? 's' : ''}, ${h.preset}. New arp written from scratch.`;
  hrShow('res', `${h.dur.toFixed(1)} s analysed in ${h.ms} ms. Now playing a new idea in that style.`);
}
function hrPrompt(h) {
  const g = h.bpmInt < 96 ? (h.centroid < 1600 ? 'lofi' : 'cinematic') : h.bpmInt < 121 ? 'house' : h.bpmInt < 133 ? 'progressive' : 'trance';
  const mood = h.centroid < 1100 ? 'dark' : h.scale === 'minor' ? 'emotional' : 'happy';
  return `${mood} ${g} in ${A.noteName(h.key, h.key, h.scale).toLowerCase()} ${h.scale} ${h.bpmInt} bpm ${h.rate === '1/16T' ? 'triplet' : h.rate}`;
}
// the heard chord as a generator slot: a numeral token when it has one (so the generator voices / colours it its own way), else a fixed chord
function hrSlot(c, h, R) {
  const rel = A.mod12(c.root - h.key), tri = c.iv.slice(0, 3), tok = A.degToken(rel, tri, h.scale === 'minor' ? 'minor' : 'major');
  const q = { '7': 'sev', m7: 'sev', maj7: 'sev', add9: 'add9', sus2: 'sus', sus4: 'sus' }[c.type];
  if (tok) { const slot = { tok, r: R(), r2: R(), alt: 0, len: 4 }, a = A.altChord(slot, h.scale, 0);
    if (A.mod12(a.root) === rel && (A.triadQuality(a.iv) === A.triadQuality(tri) || q === 'sus')) { if (q) slot.q = q; return slot; } }
  const sc = A.SCALES[h.scale], iv = c.iv.map(x => c.type === 'add9' && x === 2 ? 14 : x);
  return { fixed: { root: rel, iv, bass: null, chrom: iv.some(x => !sc.includes(A.mod12(rel + x))) }, len: 4, alt: 0, r: 0.5, r2: 0.9 };
}
function hrApply(seed, keepChords, play = true) {
  const h = HR.res; if (!h) return; pushUndo();
  if (keepChords) { S = JSON.parse(HR.pre); S.bpm = h.bpmInt; S.key = h.key; S.scale = h.scale; }
  else {
    const prev = JSON.parse(HR.pre), p = hrPrompt(h), g = A.generate(p, seed, styArg()), R = A.rng((seed ^ 0x11573) >>> 0);
    S = fromGen(g, { mix: prev.mix, loop: prev.loop, theme: prev.theme, fx2: prev.fx2 });
    Object.assign(S, { bpm: h.bpmInt, key: h.key, scale: h.scale, rate: h.rate, groove: h.groove, oct: h.oct, preset: h.preset, heard: true });
    if (h.rate === '1/16T') S.swing = 0;
    S.prog = h.chords.map(c => hrSlot(c, h, R)); if (S.sty) A.guardProg(S, guard);
    $('prompt').value = p;
  }
  rebuild(true); syncUI();
  if (play) setPlay(true, true);
}
$('listen').onclick = hrStart;
$('lsCv').onclick = () => { if (HR.st === 'mic') hrStop(); };
$('listenFile').onchange = e => { const f = e.target.files[0]; if (f) hrFile(f); e.target.value = ''; };
$('lsClose').onclick = () => { if (HR.mic) { clearInterval(HR.timer); HR.mic.stop(); HR.mic = null; hrSession('playback'); } hrShow('off'); };
$('lsUse').onclick = () => { hrShow('off'); toast('Kept. LISTEN again any time.'); };
$('lsAgain').onclick = () => { hrApply(newSeed(), false); toast('Another idea from what it heard'); };
$('lsKeep').onclick = () => { hrApply(0, true); toast(`Your chords, now at ${HR.res.bpmInt} BPM in ${hrKey(HR.res)}`); };
function drawListen(t) {
  const cv = $('lsCv'), c = cv.getContext('2d'), W = cv.width, cx = W / 2, cy = W / 2, css = getComputedStyle(document.body), col = n => css.getPropertyValue(n).trim();
  c.setTransform(1, 0, 0, 1, 0, 0); c.clearRect(0, 0, W, W);
  let ch = HR.chroma;
  if (HR.st === 'mic' && HR.mic) { const live = Listen.liveChroma(HR.mic.analyser, HR.mic.sr, HR.fbuf); for (let i = 0; i < 12; i++) ch[i] = ch[i] * 0.75 + (live[i] || 0) * 0.25; }
  const busy = HR.st === 'busy';
  for (let i = 0; i < 12; i++) {
    const v = busy ? 0.25 + 0.75 * Math.max(0, Math.cos(t * 5 - i * Math.PI / 6)) ** 3 : ch[i], a0 = -Math.PI / 2 + (i - 0.5) * Math.PI / 6 + 0.04, a1 = a0 + Math.PI / 6 - 0.08, r0 = W * 0.31, r1 = r0 + W * 0.03 + W * 0.1 * v;
    c.beginPath(); c.arc(cx, cy, r1, a0, a1); c.arc(cx, cy, r0, a1, a0, true); c.closePath(); c.fillStyle = `hsla(${i * 30 + 180},85%,62%,${0.2 + 0.8 * v})`; c.fill();
    if (HR.st === 'res' && HR.res && i === HR.res.key) { c.lineWidth = W * 0.012; c.strokeStyle = col('--ink'); c.stroke(); }
  }
  const R = W * 0.465, p = HR.st === 'mic' && HR.mic ? Math.min(1, HR.mic.seconds / HR_MAX) : HR.st === 'res' ? 1 : 0;
  c.lineCap = 'round'; c.lineWidth = W * 0.03; c.strokeStyle = col('--line'); c.beginPath(); c.arc(cx, cy, R, 0, 7); c.stroke();
  if (p > 0) { c.strokeStyle = HR.st === 'res' ? col('--green') : col('--cyan'); c.beginPath(); c.arc(cx, cy, R, -Math.PI / 2, -Math.PI / 2 + p * 2 * Math.PI); c.stroke(); }
  if (busy) { c.strokeStyle = col('--pink'); c.beginPath(); c.arc(cx, cy, R, t * 6, t * 6 + 1.2); c.stroke(); }
  drawMoodGhost(c, cx - W * 0.25, cy - W * 0.26, W * 0.5, face, t, HR.st === 'mic' || busy ? 1 : 0.35);
}

// ---------------------------------------------------------------- animation
const logo = $('logo'), moodCv = $('mood');
let moodTarget = FACES.happy, face = JSON.parse(JSON.stringify(FACES.happy)), lastMood = null, lastFrame = 0;
function frame(ms) {
  const t = ms / 1000, dt = Math.min(0.1, t - lastFrame); lastFrame = t;
  const lc = logo.getContext('2d'); lc.setTransform(1, 0, 0, 1, 0, 0); lc.clearRect(0, 0, logo.width, logo.height); drawProfessor(lc, 0, 0, logo.width, t);
  face = lerpFace(face, moodTarget, 1 - Math.exp(-dt * 4));
  const mc = moodCv.getContext('2d'); mc.setTransform(1, 0, 0, 1, 0, 0); mc.clearRect(0, 0, moodCv.width, moodCv.height); HR.ear += ((HR.st === 'mic' || HR.st === 'busy' ? 1 : 0) - HR.ear) * Math.min(1, dt * 6); drawMoodGhost(mc, 0, 0, moodCv.width, face, t, HR.ear);
  if (HR.st !== 'off') drawListen(t);
  if (playing && ctx) {
    const now = ctx.currentTime; while (marks.length && marks[0].t <= now) playTick = marks.shift().tick;
    while (lit.length && lit[0].t1 < now - 0.05) lit.shift();
    if ($('follow').classList.contains('on') && playTick >= 0 && (playTick < view.start || playTick >= view.start + view.bars * BAR)) { view.start = Math.floor(playTick / (view.bars * BAR)) * view.bars * BAR; clampView(); }
    drawRoll();
    const ci = chords.findIndex(c => playTick >= c.t && playTick < c.tEnd); [...$('chips').children].forEach((b, i) => b.classList.toggle('now', i === ci));
  }
  requestAnimationFrame(frame);
}
addEventListener('resize', () => drawRoll());

// ---------------------------------------------------------------- start
S = load();
if (S) { S.fx = { tone: 0.72, delay: 0.3, reverb: 0.3, pump: 0.4, ...S.fx }; S.fx2 = { ...fx2Defaults(), ...S.fx2 }; if (!A.GENRES[S.genre]) S.genre = 'melodic'; if (S.groove === undefined) S.groove = 0; S.mix = { arp: 0.8, chords: 0.45, bass: 0.5, kick: 0.5, ...S.mix }; if (!S.notes || !S.notes.length) S.edited = false; chords = A.resolveProg(S); if (!S.notes || !S.notes.length) S.notes = A.buildArp(S, chords); }
else { const g = A.generate(DEF_PROMPT, 128); S = fromGen(g); }
initStyleUI(); buildFxUI(); loadLibrary();
view.bars = innerWidth < 560 ? (S.rate === '1/32' ? 1 : 2) : Math.min(8, Math.ceil(A.progTicks(S) / BAR));
$('prompt').value = S.prompt || '';
rebuild(false); syncUI(); drawSlots();
genHist = [{ prompt: S.prompt, seed: S.seed }]; genIdx = 0;
requestAnimationFrame(frame);

// ---------------------------------------------------------------- example render helper (used to make MP3 demos headless)
window.renderExample = async (prompt, seed, loops = 2, styleAmt = null, scene = null, profName = null) => {
  if (profName) styPref.prof = profName; if (styleAmt !== null) styPref.amt = styleAmt; if (scene && S) S.fx2 = sceneFx(scene, false);
  doGenerate(prompt, seed); window.lastExample = { chords: chords.map(c => c.name + (c.len !== 4 ? '(' + c.len + ')' : '')).join(' '), key: A.keyName(S.key, S.scale), bpm: S.bpm, rate: S.rate, style: S.style, notes: S.notes.length }; const buf = await renderOffline(S, chords, S.notes, loops); const b = await wavBlob(buf).arrayBuffer();
  let s = ''; const u = new Uint8Array(b); for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s);
};

// ---------------------------------------------------------------- #selftest
if (location.hash === '#selftest') (async () => {
  const log = (...a) => console.log('SELFTEST', ...a); let pass = 0, fail = 0;
  const ok = (cond, what) => { if (cond) pass++; else { fail++; log('FAIL', what); } };
  const words = ['euphoric', 'dreamy', 'dark', 'sad', 'happy', 'tense', 'epic', 'chill', 'trance', 'progressive', 'melodic', 'house', 'techno', 'lofi', 'cinematic', 'in F minor', 'in D major', 'c#m', 'dorian', 'lydian', 'fast', 'slow', '1/8', '1/16', 'triplet', '1/32', 'octave up', 'sparse', 'busy', 'rolling', 'pluck', 'bell', 'acid glide', 'lush', '8 bars', '2 bars', 'pedal', 'random walk', 'harmonic', 'phrygian'];
  const R = A.rng(99); let vlSum = 0, vlN = 0, vlMax = 0, outKey = 0, totalNotes = 0, emptyBars = 0, moods = {};
  const t0 = performance.now();
  for (let k = 0; k < 50; k++) {
    const pr = [0, 1, 2].map(() => words[Math.floor(R() * words.length)]).join(' '), seed = Math.floor(R() * 1e6);
    const g = A.generate(pr, seed), s = { ...g }, ch = A.resolveProg(s), notes = A.buildArp(s, ch);
    // determinism
    const g2 = A.generate(pr, seed), n2 = A.buildArp({ ...g2 }, A.resolveProg({ ...g2 }));
    ok(JSON.stringify(n2) === JSON.stringify(notes) && JSON.stringify(g2) === JSON.stringify(g), 'determinism ' + pr);
    // musical sanity: every note is in the key, or a tone of the chord under it (borrowed chords), or its chord's scale
    for (const n of notes) { const c = A.chordAt(ch, n.t), pc = A.mod12(n.p - s.key); if (!(A.SCALES[s.scale].includes(pc) || c.pcs.includes(A.mod12(n.p)) || (c.cs && c.cs.includes(pc)))) outKey++; }
    totalNotes += notes.length;
    const bars = Math.ceil(ch[ch.length - 1].tEnd / BAR); for (let b = 0; b < bars; b++) if (notes.filter(n => n.t >= b * BAR && n.t < (b + 1) * BAR).length < 2) emptyBars++;
    for (let i = 1; i < ch.length; i++) { const v = A.vlCost(ch[i].voicing, ch[i - 1].voicing); vlSum += v; vlN++; vlMax = Math.max(vlMax, v); }
    ok(notes.every(n => Number.isFinite(n.t) && Number.isFinite(n.p) && n.d > 0 && n.p >= 24 && n.p <= 108), 'note ranges ' + pr);
    const m = A.analyzeMood(ch, s).mood; moods[m] = (moods[m] || 0) + 1;
  }
  ok(outKey === 0, 'notes out of key: ' + outKey); ok(emptyBars === 0, 'empty bars: ' + emptyBars); ok(vlSum / vlN < 3.5 && vlMax < 7, 'voice leading');
  log(`generated=50 notes=${totalNotes} outOfKey=${outKey} emptyBars=${emptyBars} voiceLeading avg=${(vlSum / vlN).toFixed(2)} max=${vlMax.toFixed(2)} semis/voice moods=${JSON.stringify(moods)} ms=${Math.round(performance.now() - t0)}`);
  // different seeds give different results
  const a1 = JSON.stringify(A.buildArp(A.generate('euphoric progressive', 1), A.resolveProg(A.generate('euphoric progressive', 1)))), a2 = JSON.stringify(A.buildArp(A.generate('euphoric progressive', 2), A.resolveProg(A.generate('euphoric progressive', 2))));
  ok(a1 !== a2, 'new seed -> new result');
  // prompt parser
  const pp = A.parsePrompt('dreamy progressive 1/16T in A minor 128 bpm octave up');
  ok(pp.key === 9 && pp.scale === 'minor' && pp.bpm === 128 && pp.rate === '1/16T' && pp.reg === 1 && pp.tags.dreamy && pp.genre === 'progressive', 'parser ' + JSON.stringify(pp));
  ok(A.parsePrompt('a happy song in eb major').key === 3, 'parser eb major'); ok(A.parsePrompt('c#m busy').key === 1, 'parser c#m');
  // mood checks on designed progressions
  const mk = (scale, toks, extra = {}) => { const s = { key: 0, scale, colour: 0, pedal: false, bpm: 120, master: 0.4, preset: 'pluck', rate: '1/16', prog: toks.split(' ').map(tok => ({ tok, r: 0.5, r2: 0.9, alt: 0, len: 4 })), ...extra }; return A.analyzeMood(A.resolveProg(s), s).mood; };
  const mh = mk('major', 'I V vi IV'), ms = mk('minor', 'i iv v i', { bpm: 90 }), mt = mk('harmonic', 'i iv V i', { colour: 0.6 }), md = mk('lydian', 'I II I II', { colour: 1, bpm: 90, master: 0.2 }), me = mk('minor', 'VI VII i i', { bpm: 140, master: 0.8, preset: 'supersaw' });
  log(`moods: I-V-vi-IV=${mh} i-iv-v-i=${ms} harmonic i-iv-V7-i=${mt} lydian lush=${md} VI-VII-i fast=${me}`);
  ok(mh === 'happy', 'mood happy'); ok(ms === 'sad', 'mood sad'); ok(mt === 'tense', 'mood tense'); ok(md === 'dreamy', 'mood dreamy'); ok(me === 'epic', 'mood epic');
  // MIDI round trip
  const gm = A.generate('dark minor pluck acid glide', 7), sm = { ...gm }, cm = A.resolveProg(sm), nm = A.buildArp(sm, cm);
  const bytes = A.writeMidi(sm.bpm, [{ name: 'Arp', ch: 0, notes: A.arpMidiNotes(nm) }, { name: 'Chords', ch: 1, notes: A.chordMidiNotes(cm, true) }]), back = A.readMidi(bytes);
  const arpBack = back.notes.filter(n => n.ch === 0), exp = A.arpMidiNotes(nm);
  ok(arpBack.length === exp.length && arpBack.every((n, i) => n.p === exp[i].p && n.t === exp[i].t), 'midi arp round trip');
  ok(Math.abs(back.bpm - sm.bpm) < 0.2, 'midi tempo');
  const cb = A.chordsFromMidi({ ...back, notes: back.notes.filter(n => n.ch === 1) });
  const names = cb.chords.map(c => A.noteName(c.root, 0, 'major') + A.chordSuffix(c.iv)).join(' '), want = cm.map(c => A.noteName(sm.key + c.root, 0, 'major') + A.chordSuffix(c.iv)).join(' ');
  log(`midi bytes=${bytes.length} notes back=${back.notes.length} chords in: ${want} | detected: ${names} key=${A.keyName(cb.key, cb.scale)}`);
  ok(cb.chords.length === cm.length && cb.chords.every((c, i) => A.mod12(c.root) === A.mod12(sm.key + cm[i].root)), 'midi chord detection roots');
  // offline render of 4 bars
  const s4 = { ...gm, mix: { arp: 0.8, chords: 0.45, bass: 0.5, kick: 0.5 }, notes: nm }; s4.prog = s4.prog.slice(0, 4).map(p => ({ ...p, len: 4 }));
  const c4 = A.resolveProg(s4), n4 = A.buildArp(s4, c4); n4.forEach((n, i) => { if (i % 5 === 0) n.l = { type: 'glide', w: 0.3, f: 0.5, c: 0 }; if (i % 7 === 0) n.l = { type: 'fatten', w: 0.5, f: 0.9, c: 0 }; });
  for (const preset of A.PRESETS) {
    const tr = performance.now(), buf = await renderOffline({ ...s4, preset }, c4, n4, 1, 1); let pk = 0, rms = 0, nan = false; const L = buf.getChannelData(0);
    for (let i = 0; i < L.length; i++) { const x = L[i]; if (!Number.isFinite(x)) nan = true; else { pk = Math.max(pk, Math.abs(x)); rms += x * x; } }
    rms = Math.sqrt(rms / L.length); log(`render ${preset} 4 bars: peak=${pk.toFixed(3)} rms=${rms.toFixed(4)} nan=${nan} ms=${Math.round(performance.now() - tr)}`);
    ok(!nan && pk > 0.05 && pk <= 1.0 && rms > 0.01, 'render ' + preset);
  }
  // ---- style: synthetic library written with writeMidi (no outside files), learned profile, STYLE 0 = classic, STYLE 100 follows the profile
  const synth = []; const mkProg = (toks, scale, key) => ({ key, scale, colour: 0.3, pedal: false, prog: toks.split(' ').map((tok, i) => ({ tok, r: 0.3 + 0.05 * i, r2: 0.9, alt: 0, len: 4 })) });
  const pats = ['i v VI III', 'i v III VI', 'VI III i v', 'i v VI v', 'III VI i v', 'i v VI III i v VI VII'];
  pats.forEach((t, i) => { const sp = mkProg(t, 'minor', (i * 5) % 12), cp = A.resolveProg(sp); synth.push(A.libItem('synth prog ' + i + '.mid', A.writeMidi(120, [{ name: 'Chords', ch: 0, notes: A.chordMidiNotes(cp, true) }]))); });
  const synRates = []; for (let i = 0; i < 4; i++) { const ga = A.generate('hypnotic pluck in A minor', 300 + i), ca = A.resolveProg(ga); synRates.push(ga.rate); synth.push(A.libItem('synth ' + i + ' arp.mid', A.writeMidi(124, [{ name: 'Arp', ch: 0, notes: A.arpMidiNotes(A.buildArp(ga, ca)) }]))); }
  ok(synth.filter(x => x.kind === 'chords').length === pats.length && synth.filter(x => x.kind === 'arp').length === 4, 'library: synthetic files parsed + classified');
  ok(synth.filter(x => x.kind === 'chords').every((x, i) => x.chords.length === pats[i].split(' ').length && (pats[i][0] !== 'i' || A.mod12(x.key) === (i * 5) % 12)), 'library: chord count + key per file');
  ok(synth.filter(x => x.kind === 'arp').every((x, i) => x.rate === synRates[i]), 'library: arp rate detected ' + synRates.join(','));
  const SP = A.learnProfile(synth, 'Selftest style', 1), FPm = SP.fam.minor;
  ok(FPm && FPm.tr.i && FPm.tr.i.v >= 0.9 && SP.arp && Object.keys(SP.arp.rate).length > 0, 'learn: transitions + arp rate ' + JSON.stringify(FPm && FPm.tr.i));
  log('learned profile: ' + profSummary(SP) + ' colour=' + JSON.stringify(SP.colour) + ' arp=' + JSON.stringify(SP.arp));
  // STYLE 0 = the classic generator, byte for byte (hash recorded from the previous version)
  const baseHash = (sty, leg) => { const P = ['euphoric progressive pluck', 'dreamy lofi chill keys', 'dark techno rolling acid', 'epic trance 1/16 octave up', 'sad piano slow in F minor', 'melodic minor pluck', 'happy house 124 bpm 8 bars', ''], out = [];
    for (const pr of P) for (const sd of [1, 128, 4242, 99999]) { const g = { ...A.generate(pr, sd, sty), ...(leg ? { groove: 1 } : {}) }, ch = A.resolveProg(g), n = A.buildArp(g, ch);
      out.push(A.hashStr(JSON.stringify({ prog: g.prog, key: g.key, scale: g.scale, bpm: g.bpm, rate: g.rate, style: g.style, preset: g.preset, oct: g.oct, master: g.master, colour: g.colour, v: ch.map(c => c.voicing), n })));
      out.push(A.hashStr(JSON.stringify(A.newProg(g, sd + 5, sty)))); }
    return A.hashStr(out.join(',')); };
  // (GROOVE 100 = the previous version's humanised rhythm, byte for byte; the default even grid has its own fixed hash)
  ok(baseHash(null, true) === 1667418328, 'GROOVE 100 = previous version'); ok(baseHash(null) === 2991085046 && baseHash({ amt: 0, prof: SP }) === 2991085046, 'STYLE 0 identical to the generator without style');
  // GROOVE 0: every bar has a note on every step of the rate grid, one length for all (ties are links, not longer notes)
  let uneven = 0, gb = 0; for (const [pr, sty] of [['euphoric progressive pluck', null], ['sparse dreamy bell', null], ['tense cinematic triplet', null], ['dark techno 1/32', null], ['euphoric progressive', { amt: 1, prof: A.BUILTIN_PROFILE }]]) for (let sd = 1; sd <= 12; sd++) {
    const g = A.generate(pr, sd, sty), ch = A.resolveProg(g), n = A.buildArp(g, ch), spb = A.RATES[g.rate], st = BAR / spb; if (g.groove) uneven++; if (new Set(n.map(x => x.d)).size > 1) uneven++;
    for (let b = 0; b * BAR < A.progTicks(g); b++) { gb++; const on = n.filter(x => x.t >= b * BAR && x.t < (b + 1) * BAR).map(x => x.t).join(), want = Array.from({ length: spb }, (_, k) => b * BAR + Math.round(k * st)).join(); if (on !== want) uneven++; } }
  ok(uneven === 0, `GROOVE 0 even grid (${gb} bars, ${uneven} problems)`); log(`groove 0: ${gb} bars checked, ${uneven} uneven`);
  ok(A.parsePrompt('rolling glitchy acid').groove > 0 && A.parsePrompt('euphoric progressive pluck').groove === 0, 'prompt words raise GROOVE');
  // STYLE 100 follows the profile's most likely next chord more often than STYLE 0
  const best = {}; for (const a in FPm.tr) best[a] = Object.entries(FPm.tr[a]).sort((x, y) => y[1] - x[1])[0][0];
  const follow = amt => { let hit = 0, n = 0; for (let sd = 1; sd <= 60; sd++) { const g = A.generate('emotional minor pluck', sd, amt ? { amt, prof: SP } : null); for (let i = 1; i < g.prog.length; i++) { const a = g.prog[i - 1].tok; if (best[a]) { n++; if (g.prog[i].tok === best[a]) hit++; } } } return hit / Math.max(1, n); };
  const f0 = follow(0), f1 = follow(1); log(`style follow rate: STYLE 0 = ${(f0 * 100).toFixed(1)}%  STYLE 100 = ${(f1 * 100).toFixed(1)}%`);
  ok(f1 > f0 + 0.15, 'STYLE 100 follows the learned transitions');
  ok(JSON.stringify(A.generate('emotional minor pluck', 9, { amt: 1, prof: SP })) === JSON.stringify(A.generate('emotional minor pluck', 9, { amt: 1, prof: SP })), 'style determinism');
  // copy guard: a library file that equals a generated idea is caught and the idea is changed; 60 STYLE 100 ideas end with no copies
  const gc = A.generate('emotional minor pluck', 21, { amt: 1, prof: SP }), cc = A.resolveProg(gc), nc = A.buildArp(gc, cc);
  const twin = [A.libItem('twin.mid', A.writeMidi(120, [{ name: 'C', ch: 0, notes: A.chordMidiNotes(cc, false) }])), A.libItem('twin arp.mid', A.writeMidi(120, [{ name: 'A', ch: 0, notes: nc.map(n => ({ t: n.t, d: n.d, p: n.p, v: n.v })) }]))];
  const G2 = A.buildGuard(synth.concat(twin)), hits0 = A.chordHits(cc, G2).length, sc2 = { ...gc, prog: gc.prog.map(p => ({ ...p })) }, ch2 = A.guardProg(sc2, G2), hits1 = A.chordHits(A.resolveProg(sc2), G2).length;
  const an = nc.map(n => ({ ...n })), aw0 = A.arpWins(an, A.progTicks(gc)).filter(w => G2.arp.has(w)).length, achg = A.guardArp(an, G2, A.progTicks(gc)), aw1 = A.arpWins(an, A.progTicks(gc)).filter(w => G2.arp.has(w)).length;
  ok(hits0 > 0 && hits1 === 0 && ch2 > 0, `guard chords: ${hits0} copied windows -> ${hits1} after ${ch2} changes`); ok(aw0 > 0 && aw1 === 0, `guard arp: ${aw0} copied windows -> ${aw1} after ${achg} changes`);
  let left = 0, changed = 0; for (let sd = 1; sd <= 60; sd++) { const g = A.generate('emotional minor pluck', sd, { amt: 1, prof: SP }); changed += A.guardProg(g, G2); left += A.chordHits(A.resolveProg(g), G2).length;
    const n = A.buildArp(g, A.resolveProg(g)); A.guardArp(n, G2, A.progTicks(g)); left += A.arpWins(n, A.progTicks(g)).filter(w => G2.arp.has(w)).length; }
  ok(left === 0, 'no exact copies in 60 STYLE 100 ideas'); log(`guard: 60 ideas, ${changed} chord changes, ${left} copies left`);
  // built-in profile works too, and stays in key
  let outB = 0; for (let sd = 1; sd <= 30; sd++) { const g = A.generate(['euphoric progressive', 'dreamy lydian', 'dark phrygian'][sd % 3], sd, { amt: 1, prof: A.BUILTIN_PROFILE }), ch = A.resolveProg(g); for (const n of A.buildArp(g, ch)) { if (!Number.isFinite(n.p)) { outB++; continue; } const c = A.chordAt(ch, n.t), pc = A.mod12(n.p - g.key); if (!(A.SCALES[g.scale].includes(pc) || c.pcs.includes(A.mod12(n.p)) || (c.cs && c.cs.includes(pc)))) outB++; } }
  ok(outB === 0, 'Professor B5 STYLE 100: notes in key (' + outB + ' out)');
  // library storage round trip
  try { await DB.put('lib', { ...synth[0], id: '__selftest' }); const back = (await DB.all('lib')).find(x => x.id === '__selftest'); await DB.del('lib', '__selftest'); ok(back && back.chords.length === synth[0].chords.length, 'IndexedDB round trip'); }
  catch (e) { ok(false, 'IndexedDB round trip ' + e.message); }
  // FX: every scene renders (arp + chords through the rack), no NaN, sane peaks, and differs from Clean
  let cleanRms = 0;
  for (const scn of Object.keys(FX_SCENES)) {
    const tr = performance.now(), buf = await renderOffline({ ...s4, preset: 'pluck', fx2: sceneFx(scn, scn === 'Dreamy' || scn === 'Tape') }, c4, n4, 1, 1.5); let pk = 0, rms = 0, nan = false;
    for (let chn = 0; chn < 2; chn++) { const L = buf.getChannelData(chn); for (let i = 0; i < L.length; i++) { const x = L[i]; if (!Number.isFinite(x)) nan = true; else { pk = Math.max(pk, Math.abs(x)); rms += x * x; } } }
    rms = Math.sqrt(rms / buf.length / 2); if (scn === 'Clean') cleanRms = rms;
    log(`render FX ${scn}: peak=${pk.toFixed(3)} rms=${rms.toFixed(4)} nan=${nan} ms=${Math.round(performance.now() - tr)}`);
    ok(!nan && pk > 0.05 && pk <= 1.0 && rms > 0.01 && (scn === 'Clean' || Math.abs(rms - cleanRms) > 1e-4), 'render FX ' + scn);
  }
  // LISTEN: synthetic drums + saw pad rendered offline -> tempo / key / chord loop; noise -> low confidence; result -> a new arp in that style
  const hear = async spec => { const b = await Listen.synth(spec); return Listen.analyze(Listen.mono(b), b.sampleRate); };
  const near = (b, w) => [w, w * 2, w / 2].some(x => Math.abs(b - x) <= 2);
  const cyc = (got, want) => { let best = 0; for (let r = 0; r < want.length; r++) { let m = 0; want.forEach((w, i) => { const g = got[(i + r) % got.length]; if (g && got.length === want.length && g.root === w[0] && (g.iv[1] === 3) === w[1]) m++; }); best = Math.max(best, m); } return best; };
  const AmFCG = [[45, 57, 60, 64], [41, 57, 60, 65], [48, 55, 60, 64], [43, 55, 59, 62]], wAm = [[9, true], [5, false], [0, false], [7, false]];
  const lsLog = (n, r) => log(`listen ${n}: ok=${r.ok} bpm=${r.bpm} (${(r.tempoConf || 0).toFixed(2)}) key=${r.ok ? A.keyName(r.key, r.scale === 'minor' ? 'minor' : 'major') : '-'} (${(r.keyConf || 0).toFixed(2)}) loop=${r.chords ? r.chords.map(c => c.name).join(' ') : '-'} bars=${r.bars ? r.bars.join(' ') : '-'} (${(r.chordConf || 0).toFixed(2)}) rate=${r.rate} groove=${r.groove} conf=${r.conf} ms=${r.ms}`);
  for (const [n, spec] of [['120 Am-F-C-G', { bpm: 120, prog: AmFCG, dur: 12 }], ['120 Am-F-C-G, starts mid-bar', { bpm: 120, prog: AmFCG, dur: 12, lead: 2.7 }]]) {
    const r = await hear(spec); lsLog(n, r);
    ok(r.ok && near(r.bpm, 120), `listen ${n}: tempo ${r.bpm}`); ok((r.key === 9 && r.scale === 'minor') || (r.key === 0 && r.scale === 'major'), `listen ${n}: key A minor / C major`);
    ok(cyc(r.chords, wAm) >= 3, `listen ${n}: chords ${r.chords.map(c => c.name)}`); if (!spec.lead) window.__heard120 = r; }
  const r92 = await hear({ bpm: 92, prog: [[39, 55, 58, 63], [46, 58, 62, 65], [48, 55, 60, 63], [44, 56, 60, 63]], dur: 16 }); lsLog('92 Eb-Bb-Cm-Ab', r92);
  ok(r92.ok && near(r92.bpm, 92), 'listen 92: tempo ' + r92.bpm); ok((r92.key === 3 && r92.scale === 'major') || (r92.key === 0 && r92.scale === 'minor'), 'listen 92: key Eb major');
  ok(cyc(r92.chords, [[3, false], [10, false], [0, true], [8, false]]) >= 3, 'listen 92: chords ' + r92.chords.map(c => c.name));
  let rn = null; try { rn = await hear({ noise: true, dur: 8 }); } catch (e) { log('noise threw ' + e.message); }
  if (rn) lsLog('noise', rn); ok(rn && (!rn.ok || rn.conf < 0.4), 'listen noise: low confidence ' + (rn && rn.conf));
  let rq = null; try { rq = Listen.analyze(new Float32Array(44100 * 5), 44100); } catch (e) {} ok(rq && !rq.ok, 'listen silence: handled');
  { const keep = JSON.stringify(S), u = undoStack.length; HR.res = window.__heard120; HR.pre = keep; hrApply(777, false, false);
    const names = chords.map(c => A.mod12(S.key + c.root)), okKey = S.key === HR.res.key && S.bpm === HR.res.bpmInt && S.rate === HR.res.rate;
    let out = 0; for (const n of S.notes) { const c = A.chordAt(chords, n.t), pc = A.mod12(n.p - S.key); if (!(A.SCALES[S.scale].includes(pc) || c.pcs.includes(A.mod12(n.p)))) out++; }
    const a1 = JSON.stringify(S.notes); hrApply(778, false, false); const a2 = JSON.stringify(S.notes), roots2 = chords.map(c => A.mod12(S.key + c.root));
    ok(okKey && S.prog.length === HR.res.chords.length && names.join() === HR.res.chords.map(c => c.root).join() && S.notes.length > 8 && out === 0, `listen -> generator: ${chords.map(c => c.name).join(' ')} ${S.notes.length} notes, ${out} out of key`);
    ok(a1 !== a2 && roots2.join() === names.join(), 'listen TRY ANOTHER: new arp, same heard chords');
    hrApply(0, true, false); ok(S.bpm === HR.res.bpmInt && S.key === HR.res.key && JSON.stringify(S.prog) === JSON.stringify(JSON.parse(keep).prog), 'listen KEEP MY CHORDS: only tempo + key');
    S = JSON.parse(keep); rebuild(false); syncUI(); undoStack.length = u; HR.res = null; }
  log(`done pass=${pass} fail=${fail}`);
})();
