'use strict';
// Mr. Mickens web engine (Web Audio). Same ideas as the plugin:
//  - 100 breaks: 20 classic break RHYTHMS x 5 variations, synthesised on an original vintage kit with "old record"
//    processing (no sampled recordings). Rendered on demand, so the page stays light on phones.
//  - 4 bays: every 16th triggers a slice; NUDGE (on a grid), RANDOM START, RANDOM CHOPS, FIT TEMPO, per-bay
//    ducking from the kick / snare / hats only.
//  - drums: kick / snare / hat / open hat, 16 steps.
//  - break FX: crunch + bits -> parallel low/high/band/notch/comb/formant (each with its own mix next to DRY)
//    -> chorus -> stutter filter -> reverb.

const SR = 44100;
const RHYTHMS = [
  ['Amen', 136, 'x.x.......xx....x.x.......x.....', '....x..x.x..x..x....x..x.x....x.', '...............................x', '', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', ''],
  ['Amen End', 136, 'x.x.......xx......xx......x.....', '....x..x.x..x..x.x..x..x.x....x.', '', '', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x...', '..........................x.....'],
  ['Think', 110, 'x......x.x.x....x......x.x......', '....x.......x.......x.......x.x.', '..x.......x.......x....x........', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x...', '..............................x.', '', ''],
  ['Apache', 118, 'x.....x...x.....x.....x.x.......', '....x.......x.......x.......x...', '......x.......x.......x...x.....', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Funky Drummer', 102, 'x.x...x...x..x..x.x...x...x..x..', '....x..x.x.x...x....x..x.x.x...x', '.x....x......x...x....x......x..', 'xxxxxxxxxxxxx.xxxxxxxxxxxxxxx.xx', '.............x...............x..', '', ''],
  ['Hot Pants', 115, 'x..x..x...x..x..x..x..x...x..x..', '....x.......x..x....x.......x..x', '', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Cold Sweat', 120, 'x.....x.x.......x.....x.x.......', '....x..x....x.......x..x....x...', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Impeach', 96, 'x......xx.x.....x......x..x.....', '....x.......x.......x.......x...', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '......x...............x.........', '', ''],
  ['Skull Snaps', 92, 'x...x..x..x.....x...x..x..x.....', '....x.......x.x.....x.......x.x.', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Roachclip', 100, 'x.x...x....x....x.x...x....x....', '....x..x.x..x.......x..x.x..x...', '', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Substitution', 104, 'x..x..x...x.....x..x..x...x.....', '....x.......x..x....x.......x..x', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Soul Pride', 112, 'x.....x.x..x....x.....x.x..x....', '....x.......x.......x.......x...', '..x...........x...x...........x.', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Tighten Up', 108, 'x.......x.x.....x.......x.x.....', '....x.......x.......x.......x...', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Mule', 98, 'x.x.....x.x..x..x.x.....x.x.....', '....x.......x.......x.......x..x', '.......x........x......x........', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Ride Break', 124, 'x.....x...x.....x.....x...x.....', '....x.......x.......x.......x...', '', '', '', 'x.xxx.xxx.xxx.xxx.xxx.xxx.xxx.xx', ''],
  ['Jungle Roller', 170, 'x.........x.....x.........x.....', '....x.......x.......x.......x...', '..x....x...x..x...x....x...x..x.', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Two Step', 172, 'x.........x.......x.......x.....', '....x.......x.......x.......x...', '.......x.x.......x.....x.x......', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Halftime', 86, 'x.........x.....x.....x...x.....', '........x...............x.......', '......x.......x.x.....x.......x.', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '', '', ''],
  ['Rolling', 174, 'x.....x.x.......x.....x.x.......', '....x.......x.......x.......x...', '.x..x.....x..x...x..x.....x..x..', 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx', '', '', ''],
  ['Dub Break', 90, 'x.....x....x....x.....x....x....', '....x.......x.......x.......x...', '', 'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.', '...........x...............x....', '', ''],
];
const VARIATIONS = ['Original', 'Ghosts', 'Fill', 'Dirty', 'Room'];
const TWO_PI = Math.PI * 2;
function rng32(seed) { let s = seed >>> 0; return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

// ---------------------------------------------------------------- the 100 built-in breaks
const BREAKS = [];
for (let r = 0; r < RHYTHMS.length; r++) for (let v = 0; v < 5; v++)
  BREAKS.push({ key: RHYTHMS[r][0] + '|' + v, family: RHYTHMS[r][0], name: RHYTHMS[r][0] + ' - ' + VARIATIONS[v], bpm: RHYTHMS[r][1], slices: 32, r, v, buf: null, peaks: null });

function renderBreak(br) {
  if (br.buf) return br.buf;
  const R = RHYTHMS[br.r], v = br.v, U = rng32(br.r * 31 + v * 7 + 1), N = () => U() * 2 - 1;
  const step = 60 / R[1] / 4 * SR, n = Math.round(step * 32), d = new Float32Array(n + Math.floor(SR * 0.5));
  const tune = (U() - 0.5) * 2;
  const lane = (s, i) => s && s.length > i && s[i] !== '.';
  const kick = (at, vel) => { const m = Math.floor(SR * 0.32); let ph = 0;
    for (let i = 0; i < m && at + i < d.length; i++) { const t = i / SR, f = (52 + 10 * tune) + 95 * Math.exp(-t * 38); ph += TWO_PI * f / SR; d[at + i] += vel * (Math.tanh(2.2 * Math.sin(ph) * Math.exp(-t * 11)) + N() * Math.exp(-t * 700) * 0.5); } };
  const snare = (at, vel) => { const m = Math.floor(SR * 0.28); let b1 = 0, b2 = 0, hp = 0, px = 0;
    const f = 2600 + 400 * tune, g = Math.tan(Math.PI * f / SR), k = 1.2, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    for (let i = 0; i < m && at + i < d.length; i++) { const t = i / SR, x = N(); const v3 = x - b2, v1 = a1 * b1 + a2 * v3, v2 = b2 + a2 * b1 + a3 * v3; b1 = 2 * v1 - b1; b2 = 2 * v2 - b2;
      hp = 0.9 * (hp + x - px); px = x; const body = (Math.sin(TWO_PI * (190 + 20 * tune) * t) + 0.5 * Math.sin(TWO_PI * 335 * t)) * Math.exp(-t * 26);
      d[at + i] += vel * (body * 0.55 + v1 * 1.6 * Math.exp(-t * 14) + hp * 0.5 * Math.exp(-t * 60)); } };
  const FS = [205.3, 304.4, 369.6, 522.7, 540, 800];
  const metal = (t, mul) => { let s = 0; for (const f of FS) s += Math.sin(TWO_PI * f * 1.7 * mul * t) > 0 ? 1 : -1; return s / 6; };
  const cym = (at, vel, len, decay, mul, nm) => { const m = Math.floor(SR * len); let hp = 0, px = 0;
    for (let i = 0; i < m && at + i < d.length; i++) { const t = i / SR, x = (metal(t, mul) * (1 - nm) + N() * nm) * Math.exp(-t * decay); hp = 0.94 * (hp + x - px); px = x; d[at + i] += vel * hp * 0.6; } };
  for (let i = 0; i < 32; i++) {
    const jit = Math.floor((U() - 0.5) * SR * 0.006), at = Math.max(0, Math.round(i * step) + jit), vv = 0.85 + U() * 0.15;
    if (lane(R[2], i)) kick(at, vv);
    if (lane(R[3], i)) snare(at, vv);
    let ghost = lane(R[4], i);
    if (v === 1 && !lane(R[3], i) && !lane(R[2], i) && i % 2 === 1 && U() < 0.35) ghost = true;
    if (ghost) snare(at, 0.28);
    if (v === 2 && i >= 28) snare(at, 0.55 + 0.1 * (i - 28));
    if (lane(R[5], i)) cym(at, (i % 2 ? 0.45 : 0.7) * vv, 0.09, 45, 1, 0.35);
    if (lane(R[6], i) || (v === 4 && i % 8 === 6)) cym(at, 0.55, 0.45, 7, 1, 0.4);
    if (lane(R[7], i)) cym(at, i % 2 ? 0.4 : 0.6, 0.6, 5, 1.9, 0.12);
    if (lane(R[8], i) || (v === 2 && i === 0)) cym(at, 0.7, 1.4, 2.2, 0.73, 0.6);
  }
  const room = v === 4 ? 0.35 : 0.14, taps = [0.011, 0.019, 0.027, 0.041, 0.063].map(x => Math.floor(SR * x)), tg = [0.5, 0.38, 0.3, 0.2, 0.12];
  const wet = new Float32Array(d.length);
  for (let k = 0; k < 5; k++) for (let i = taps[k]; i < d.length; i++) wet[i] += d[i - taps[k]] * tg[k];
  const drive = v === 3 ? 3.2 : 1.5, lpA = v === 3 ? 0.35 : 0.6, td = Math.tanh(drive); let lp = 0, lp2 = 0;
  for (let i = 0; i < d.length; i++) { let x = d[i] + wet[i] * room; x = Math.tanh(x * drive) / td; lp += lpA * (x - lp); lp2 += lpA * (lp - lp2); x = lp2;
    if (U() < 0.0004) x += (U() - 0.5) * 0.25; x += (U() - 0.5) * 0.004; if (v === 3) x = Math.round(x * 48) / 48; d[i] = x; }
  for (let i = n; i < d.length; i++) d[i - n] += d[i] * 0.6;
  const out = d.subarray(0, n); let pk = 1e-6; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(out[i])); for (let i = 0; i < n; i++) out[i] *= 0.89 / pk;
  br.buf = makeBuffer(out); br.peaks = peaksOf(out); return br.buf;
}
function makeBuffer(data, rate = SR) { const b = new AudioBuffer({ length: data.length, sampleRate: rate, numberOfChannels: 1 }); b.copyToChannel(data instanceof Float32Array ? data : Float32Array.from(data), 0); return b; }
function peaksOf(data, cols = 300) { const p = new Float32Array(cols), n = data.length; for (let c = 0; c < cols; c++) { let m = 0; for (let i = Math.floor(c * n / cols); i < Math.floor((c + 1) * n / cols); i++) m = Math.max(m, Math.abs(data[i])); p[c] = m; } return p; }

// your own break (file picker): mono, normalised, bar count guessed from the length
async function breakFromFile(file, ctx) {
  const ab = await file.arrayBuffer(); const au = await ctx.decodeAudioData(ab);
  const n = Math.min(au.length, au.sampleRate * 30), mono = new Float32Array(n);
  for (let c = 0; c < au.numberOfChannels; c++) { const ch = au.getChannelData(c); for (let i = 0; i < n; i++) mono[i] += ch[i] / au.numberOfChannels; }
  let pk = 1e-6; for (const x of mono) pk = Math.max(pk, Math.abs(x)); for (let i = 0; i < n; i++) mono[i] *= 0.89 / pk;
  const secs = n / au.sampleRate; let bars = 1, best = 1e9;
  for (const b of [1, 2, 4, 8]) { const bpm = b * 240 / secs, e = Math.abs(Math.log(bpm / 140)); if (bpm >= 70 && bpm <= 190 && e < best) { best = e; bars = b; } }
  return { key: 'user|' + file.name + '|' + n, family: 'Your breaks', name: file.name.replace(/\.[^.]+$/, ''), bpm: bars * 240 / secs, slices: bars * 16, user: true, buf: makeBuffer(mono, au.sampleRate), peaks: peaksOf(mono) };
}

// ---------------------------------------------------------------- drums (same recipes as Ghosty Beats)
const DRUMS = {
  kick: { 'Hard Kick': ['kick', 0.5, 55, 320, 70, 8, 0.9, 6], 'Punch Kick': ['kick', 0.45, 52, 190, 48, 9, 0.6, 3], 'Techno Kick': ['kick', 0.6, 46, 260, 60, 6, 0.3, 4.5],
          'Sub Kick': ['kick', 0.8, 42, 70, 25, 4, 0.05, 1.3], '808 Punchy': ['kick', 1.0, 48, 260, 60, 2.6, 0.7, 2.8], '808 Dirty': ['kick', 1.4, 46, 130, 30, 1.8, 0.5, 5.5] },
  snare: { 'Big Snare': ['snare', 0.7, 170, 14, 6, 0.75, 0.6], 'Tight Snare': ['snare', 0.2, 260, 40, 34, 0.9, 0.4], 'Drill Snare': ['snare', 0.3, 240, 30, 20, 0.9, 0.45],
           'Boom Bap Snare': ['snare', 0.45, 180, 20, 11, 0.8, 0.7, 0.45], 'Big Clap': ['clap', 0.8, 0.014, 7, 4], 'Clap': ['clap', 0.5, 0.011, 14, 3] },
  hat: { 'Tight Hat': ['hat', 0.05, 1.4, 130, 0.3, 0.97], 'Hat': ['hat', 0.12, 1, 55, 0.4, 0.95], 'Trap Hat': ['hat', 0.07, 1.25, 85, 0.35, 0.96] },
  open: { 'Short Open Hat': ['hat', 0.35, 1.1, 12, 0.4, 0.95], 'Open Hat': ['hat', 0.7, 1, 6, 0.4, 0.95], 'Long Open Hat': ['hat', 1.2, 0.95, 3.2, 0.45, 0.95] },
};
const LANES = ['kick', 'snare', 'hat', 'open'];
const drumCache = {};
function drumBuffer(lane, name) {
  const id = lane + '|' + name; if (drumCache[id]) return drumCache[id];
  const spec = DRUMS[lane][name], len = spec[1], n = Math.max(64, Math.floor(SR * len)), b = new Float32Array(n);
  const U = rng32(4242 + name.length * 13), noise = () => U() * 2 - 1;
  const hp = (a) => { let y = 0, px = 0; for (let i = 0; i < n; i++) { y = a * (y + b[i] - px); px = b[i]; b[i] = y; } };
  const lp = (a) => { let y = 0; for (let i = 0; i < n; i++) { y += a * (b[i] - y); b[i] = y; } };
  if (spec[0] === 'kick') { const [, , f0, f1, pk, ak, click, sat] = spec; let ph = 0;
    for (let i = 0; i < n; i++) { const t = i / SR, f = f0 + f1 * Math.exp(-t * pk); ph += TWO_PI * f / SR; b[i] = Math.tanh(sat * Math.sin(ph) * Math.exp(-t * ak)) + noise() * Math.exp(-t * 900) * click; } }
  else if (spec[0] === 'snare') { const [, , tone, tk, nk, hpA, mix, lpA] = spec; const no = new Float32Array(n); for (let i = 0; i < n; i++) no[i] = noise();
    let y = 0, px = 0; for (let i = 0; i < n; i++) { y = hpA * (y + no[i] - px); px = no[i]; no[i] = y; }
    for (let i = 0; i < n; i++) { const t = i / SR; b[i] = (Math.sin(TWO_PI * tone * t) + 0.55 * Math.sin(TWO_PI * tone * 1.78 * t)) * Math.exp(-t * tk) * mix + no[i] * Math.exp(-t * nk); }
    if (lpA) lp(lpA); }
  else if (spec[0] === 'clap') { const [, , spread, tail, hits] = spec; const no = new Float32Array(n); for (let i = 0; i < n; i++) no[i] = noise();
    const g = Math.tan(Math.PI * 1200 / SR), k = 1 / 0.9, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2; let s1 = 0, s2 = 0;
    for (let i = 0; i < n; i++) { const v3 = no[i] - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; no[i] = v1; }
    for (let i = 0; i < n; i++) { const t = i / SR; let e = 0; for (let h = 0; h < hits; h++) { const o = h * spread; if (t >= o) e = Math.max(e, Math.exp(-(t - o) * 160)); }
      const last = (hits - 1) * spread; if (t > last) e = Math.max(e, Math.exp(-(t - last) * tail) * 0.6); b[i] = no[i] * e; } }
  else { const [, , mul, k, nm, hpA] = spec; const FS = [205.3, 304.4, 369.6, 522.7, 540, 800];
    for (let i = 0; i < n; i++) { const t = i / SR; let m = 0; for (const f of FS) m += Math.sin(TWO_PI * f * 1.7 * mul * t) > 0 ? 1 : -1; b[i] = ((m / 6) * (1 - nm) + noise() * nm) * Math.exp(-t * k); }
    hp(hpA); hp(hpA); }
  let mx = 1e-6; for (const v of b) mx = Math.max(mx, Math.abs(v)); for (let i = 0; i < n; i++) b[i] *= 0.89 / mx;
  for (let i = 0; i < 96 && i < n; i++) b[n - 1 - i] *= i / 96;
  return (drumCache[id] = makeBuffer(b));
}

// ---------------------------------------------------------------- engine
const cutHz = v => 30 * Math.pow(600, Math.min(1, Math.max(0, v)));
const GRID = [1, 2, 4, 8, 16];
const STUT = [2, 1, 0.5, 0.25, 2 / 3];   // stutter period in 16ths: 1/8, 1/16, 1/32, 1/64, 1/16T
const FORMANTS = [[800, 1150, 2900], [400, 1600, 2700], [300, 2300, 3000], [450, 800, 2830], [325, 700, 2530]];

class Engine {
  constructor(ctx, dest, state, getBreak) {
    this.ctx = ctx; this.S = state; this.getBreak = getBreak;
    const C = ctx, G = (v = 1) => { const g = C.createGain(); g.gain.value = v; return g; };
    this.master = G(0.8); this.limiter = C.createDynamicsCompressor();
    Object.assign(this.limiter, {}); this.limiter.threshold.value = -6; this.limiter.knee.value = 4; this.limiter.ratio.value = 12; this.limiter.attack.value = 0.002; this.limiter.release.value = 0.12;
    // soft clip after the limiter so nothing ever goes past full scale
    this.pre = G(0.5); this.clip = C.createWaveShaper(); const cc = new Float32Array(1025);
    for (let i = 0; i < 1025; i++) { const x = (i / 512 - 1) * 2, ax = Math.abs(x); cc[i] = ax < 0.8 ? x : Math.sign(x) * (0.8 + 0.18 * Math.tanh((ax - 0.8) / 0.18)); }
    this.clip.curve = cc; this.master.connect(this.limiter).connect(this.pre).connect(this.clip).connect(dest);
    // break bus -> crunch -> parallel filters
    this.breakBus = G(); this.crunch = C.createWaveShaper(); this.crunch.oversample = '2x';
    this.breakBus.connect(this.crunch);
    this.filtSum = G();
    this.dry = G(); this.crunch.connect(this.dry).connect(this.filtSum);
    const branch = (node) => { const g = G(0); this.crunch.connect(node); node.connect(g).connect(this.filtSum); return g; };
    this.lp = C.createBiquadFilter(); this.lp.type = 'lowpass'; this.lpMix = branch(this.lp);
    this.hp = C.createBiquadFilter(); this.hp.type = 'highpass'; this.hpMix = branch(this.hp);
    this.bp = C.createBiquadFilter(); this.bp.type = 'bandpass'; this.bpMix = branch(this.bp);
    this.nt = C.createBiquadFilter(); this.nt.type = 'notch'; this.ntMix = branch(this.nt);
    this.comb = C.createDelay(0.1); this.combFb = G(0.7); this.comb.connect(this.combFb).connect(this.comb); this.cbMix = branch(this.comb);
    this.formIn = G(); this.formSum = G(1.5); this.form = [0, 1, 2].map(j => { const f = C.createBiquadFilter(); f.type = 'bandpass'; f.Q.value = 7; const g = G([1, 0.5, 0.2][j]); this.formIn.connect(f).connect(g).connect(this.formSum); return f; });
    this.fmMix = G(0); this.crunch.connect(this.formIn); this.formSum.connect(this.fmMix).connect(this.filtSum);
    // chorus
    this.chOut = G(); this.filtSum.connect(this.chOut); this.chWet = G(0);
    this.chorusDl = [0.007, 0.011].map((t, i) => { const d = C.createDelay(0.05); d.delayTime.value = t; const lfo = C.createOscillator(); lfo.frequency.value = [0.9, 1.3][i]; const depth = G(0.0015);
      lfo.connect(depth).connect(d.delayTime); lfo.start(); this.filtSum.connect(d).connect(this.chWet); return depth; });
    this.chWet.connect(this.chOut);
    // stutter filter (opens fully when the stutter is off)
    this.stutF = C.createBiquadFilter(); this.stutF.type = 'lowpass'; this.stutF.frequency.value = 20000; this.stutF.Q.value = 0.8;
    this.chOut.connect(this.stutF);
    // reverb
    this.revIn = G(); this.stutF.connect(this.revIn); this.breaksLvl = G(0.85);
    this.revIn.connect(this.breaksLvl); this.conv = C.createConvolver(); this.revWet = G(0); this.revIn.connect(this.conv).connect(this.revWet).connect(this.breaksLvl);
    this.breaksLvl.connect(this.master);
    // drums
    this.drumsLvl = G(0.9); this.drumsLvl.connect(this.master);
    this.drumGain = LANES.map(() => { const g = G(1); g.connect(this.drumsLvl); return g; });
    // bays
    this.bays = [0, 1, 2, 3].map(() => { const lvl = G(), pan = C.createStereoPanner(), dk = G(), ds = G(), dh = G();
      lvl.connect(pan).connect(dk).connect(ds).connect(dh).connect(this.breakBus); return { lvl, pan, duck: [dk, ds, dh], last: null, offset: 0, lastBar: -1, slice: -1 }; });
    this.lastOpen = null; this.revSize = -1;
    this.apply();
  }
  apply() {   // state -> node values (call whenever a knob moves)
    const S = this.S, F = S.fx, C = this.ctx, now = C.currentTime, set = (p, v) => p.setTargetAtTime(v, now, 0.015);
    // crunch + bits curve
    const n = 2048, curve = new Float32Array(n), g = 1 + F.crunch * F.crunch * 18, c = 1 / (1 + F.crunch * 1.3), q = Math.pow(2, 14 - F.bits * 11);
    for (let i = 0; i < n; i++) { const x = i / (n - 1) * 2 - 1; let y = Math.tanh(x * g) * c; if (F.bits > 0.01) y = Math.round(y * q) / q; curve[i] = y; }
    this.crunch.curve = curve;
    set(this.dry.gain, F.dry);
    set(this.lp.frequency, cutHz(F.lp_cut)); set(this.lp.Q, 0.7 + F.res * 12); set(this.lpMix.gain, F.lp_mix);
    set(this.hp.frequency, cutHz(F.hp_cut)); set(this.hp.Q, 0.7 + F.res * 12); set(this.hpMix.gain, F.hp_mix);
    set(this.bp.frequency, cutHz(F.bp_cut)); set(this.bp.Q, 0.5 + F.res * 8); set(this.bpMix.gain, F.bp_mix * 1.4);
    set(this.nt.frequency, cutHz(F.nt_cut)); set(this.nt.Q, 0.5 + F.res * 4); set(this.ntMix.gain, F.nt_mix);
    set(this.comb.delayTime, 1 / (40 * Math.pow(40, F.cb_cut))); set(this.combFb.gain, 0.55 + F.res * 0.4); set(this.cbMix.gain, F.cb_mix * 0.6);
    const fp = Math.min(0.999, Math.max(0, F.fm_vow)) * 4, fa = Math.floor(fp), ft = fp - fa;
    this.form.forEach((f, j) => set(f.frequency, FORMANTS[fa][j] + (FORMANTS[fa + 1][j] - FORMANTS[fa][j]) * ft)); set(this.fmMix.gain, F.fm_mix);
    set(this.chWet.gain, F.chorus * 0.6); this.chorusDl.forEach(d => set(d.gain, 0.0015 * (0.25 + F.chorus * 0.5) * 2));
    if (Math.abs(this.revSize - F.size) > 0.02) { this.revSize = F.size; this.conv.buffer = this.impulse(F.size); }
    set(this.revWet.gain, F.reverb * 0.9);
    set(this.breaksLvl.gain, F.brk_lvl); set(this.drumsLvl.gain, F.drm_lvl); set(this.master.gain, F.volume * 1.1);
    LANES.forEach((l, i) => set(this.drumGain[i].gain, S.drums.lvl[i]));
    this.bays.forEach((b, i) => { const P = S.bays[i]; set(b.lvl.gain, P.mute ? 0 : P.level); set(b.pan.pan, P.pan); });
  }
  impulse(size) {
    const C = this.ctx, len = Math.floor(C.sampleRate * (0.6 + size * 2.6)), b = C.createBuffer(2, len, C.sampleRate), U = rng32(77);
    for (let ch = 0; ch < 2; ch++) { const d = b.getChannelData(ch); for (let i = 0; i < len; i++) { const t = i / len; d[i] = (U() * 2 - 1) * Math.pow(1 - t, 2.2 + (1 - size) * 2) * (i < 200 ? i / 200 : 1); } }
    return b;
  }
  // one 16th: drums, ducking, and a slice for every bay (or stutter repeats)
  step(s, T, stepDur) {
    const S = this.S, st = ((s % 16) + 16) % 16;
    S.drums.pat.forEach((row, d) => { const v = row[st]; if (v) this.hit(d, v / 127, T); });
    let envDone = false;
    for (let b = 0; b < 4; b++) {
      const P = S.bays[b], bay = this.bays[b], br = this.getBreak(P.brk); if (!br || P.mute) continue;
      if (!br.buf) renderBreak(br);
      if (S.stutter) {
        if (bay.slice < 0) bay.slice = 0;
        if (this.stutStart === undefined) this.stutStart = T;
        const per = STUT[S.stutDiv] * stepDur; let k = Math.ceil((T - this.stutStart) / per - 1e-6);
        for (let tt = this.stutStart + k * per; tt < T + stepDur - 1e-6; tt = this.stutStart + (++k) * per) { this.slice(b, br, bay.slice, tt, per, true); if (!envDone) this.stutEnv(tt, per); }
        envDone = true;
        continue;
      }
      const grid = GRID[P.grid], bar = Math.floor(s / 16);
      if (P.rs && bar !== bay.lastBar) bay.offset = Math.floor(Math.random() * Math.max(1, br.slices / grid)) * grid;
      if (!P.rs) bay.offset = 0;
      bay.lastBar = bar;
      let slice = (((s + P.nudge * grid + bay.offset) % br.slices) + br.slices) % br.slices;
      if (P.rc && Math.random() < P.amt) slice = Math.floor(Math.random() * br.slices);
      bay.slice = slice;
      this.slice(b, br, slice, T, null, false);
    }
    if (!S.stutter && this.stutStart !== undefined) { this.stutStart = undefined; this.stutF.frequency.cancelScheduledValues(T); this.stutF.frequency.setTargetAtTime(20000, T, 0.01); }
  }
  slice(b, br, slice, T, forceLen, stut) {
    const C = this.ctx, P = this.S.bays[b], bay = this.bays[b];
    const rate = Math.pow(2, P.pitch / 12) * (P.fit ? this.S.bpm / br.bpm : 1);
    const L = br.buf.duration / br.slices;
    let lenBuf = L * P.decay + (P.decay >= 0.99 ? L * 0.6 : 0); let real = lenBuf / rate;
    if (forceLen) real = Math.min(real, forceLen);
    if (bay.last) { const o = bay.last; try { o.g.gain.cancelScheduledValues(T); o.g.gain.setTargetAtTime(0, T, 0.003); o.src.stop(T + 0.03); } catch (e) {} }
    const src = C.createBufferSource(); src.buffer = br.buf; src.playbackRate.value = rate;
    const g = C.createGain(); g.gain.setValueAtTime(0, T); g.gain.linearRampToValueAtTime(1, T + 0.002);
    const end = T + Math.max(0.01, real); g.gain.setValueAtTime(1, Math.max(T + 0.003, end - 0.006)); g.gain.linearRampToValueAtTime(0, end);
    src.connect(g).connect(bay.lvl); src.start(T, Math.min(br.buf.duration - 0.001, slice * L)); src.stop(end + 0.02);
    bay.last = { src, g }; (this.onSlice || (() => {}))(b, slice, T);
  }
  stutEnv(T, per) {
    const F = this.S.fx, f = this.stutF.frequency, top = cutHz(Math.min(1, F.st_cut + F.st_env * 0.5)), base = cutHz(F.st_cut);
    f.cancelScheduledValues(T); f.setValueAtTime(top, T); f.setTargetAtTime(base, T + 0.001, Math.max(0.005, F.st_dec * 0.3) / 3);
  }
  hit(d, vel, T) {
    if (this.S.drums.all || (this.S.drums.mute && this.S.drums.mute[d])) return;   // muted drums don't play or duck
    const C = this.ctx, S = this.S, buf = drumBuffer(LANES[d], S.drums.sel[d]);
    if (d === 2 && this.lastOpen) { try { this.lastOpen.gain.cancelScheduledValues(T); this.lastOpen.gain.setTargetAtTime(0, T, 0.01); } catch (e) {} }
    const src = C.createBufferSource(); src.buffer = buf; const g = C.createGain(); g.gain.value = 0.3 + 0.7 * vel;
    src.connect(g).connect(this.drumGain[d]); src.start(T); if (d === 3) this.lastOpen = g;
    // per-element sidechain: only the bays that asked for it duck, only from this drum
    const e = d === 0 ? 0 : d === 1 ? 1 : 2, rel = S.fx.duck_rel;
    S.bays.forEach((P, b) => { const a = [P.dk, P.ds, P.dh][e]; if (a <= 0.001) return; const p = this.bays[b].duck[e].gain;
      p.cancelScheduledValues(T); p.setTargetAtTime(1 - a, T, 0.0015); p.setTargetAtTime(1, T + 0.006, rel / 4); });
    (this.onHit || (() => {}))(d, T);
  }
}

function wavBlob(buf) {
  const ch = buf.numberOfChannels, n = buf.length, out = new DataView(new ArrayBuffer(44 + n * ch * 2)); let o = 0;
  const w = s => { for (let i = 0; i < s.length; i++) out.setUint8(o++, s.charCodeAt(i)); }, u32 = v => { out.setUint32(o, v, true); o += 4; }, u16 = v => { out.setUint16(o, v, true); o += 2; };
  w('RIFF'); u32(36 + n * ch * 2); w('WAVE'); w('fmt '); u32(16); u16(1); u16(ch); u32(buf.sampleRate); u32(buf.sampleRate * ch * 2); u16(ch * 2); u16(16); w('data'); u32(n * ch * 2);
  const data = []; for (let c = 0; c < ch; c++) data.push(buf.getChannelData(c));
  for (let i = 0; i < n; i++) for (let c = 0; c < ch; c++) { const v = Math.max(-1, Math.min(1, data[c][i])); out.setInt16(o, v * 32767, true); o += 2; }
  return new Blob([out], { type: 'audio/wav' });
}
