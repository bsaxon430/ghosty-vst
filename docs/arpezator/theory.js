'use strict';
// ArpEZator music core (no DOM, no audio): scales, chords, progression generator, voice leading, prompt parser,
// arpeggio engine, mood analysis, MIDI writer + reader + chord detection. Everything is deterministic from seeds.

const BAR = 384, PPQ = 96;                         // ticks: 96 per quarter, 384 per 4/4 bar
const RATES = { '1/8': 8, '1/16': 16, '1/16T': 24, '1/32': 32 };   // steps per bar
const SCALES = {
  minor: [0, 2, 3, 5, 7, 8, 10], major: [0, 2, 4, 5, 7, 9, 11], dorian: [0, 2, 3, 5, 7, 9, 10], phrygian: [0, 1, 3, 5, 7, 8, 10],
  lydian: [0, 2, 4, 6, 7, 9, 11], mixolydian: [0, 2, 4, 5, 7, 9, 10], harmonic: [0, 2, 3, 5, 7, 8, 11],
};
const SCALE_LABEL = { minor: 'Minor', major: 'Major', dorian: 'Dorian', phrygian: 'Phrygian', lydian: 'Lydian', mixolydian: 'Mixolydian', harmonic: 'Harmonic minor' };
const SCALE_FEEL = { minor: 'emotional', major: 'happy', dorian: 'cool', phrygian: 'dark', lydian: 'dreamy', mixolydian: 'bright', harmonic: 'drama' };
const MINORISH = s => s === 'minor' || s === 'dorian' || s === 'phrygian' || s === 'harmonic';
const SHARP = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'], FLAT = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
const useFlats = (key, scale) => MINORISH(scale) ? [2, 7, 0, 5, 10, 3, 8].includes(key) : [5, 10, 3, 8, 1, 6].includes(key);
const noteName = (pc, key, scale) => { const p = ((pc % 12) + 12) % 12; if (scale === 'phrygian' && p === (key + 1) % 12) return FLAT[p]; return (useFlats(key, scale) && !(MINORISH(scale) && p === (key + 11) % 12) ? FLAT : SHARP)[p]; };
const keyName = (key, scale) => MINORISH(scale) ? (useFlats(key, scale) ? FLAT : SHARP)[key] + 'm' : (useFlats(key, scale) ? FLAT : SHARP)[key];
const mod12 = x => ((x % 12) + 12) % 12;
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const lerp = (a, b, t) => a + (b - a) * t;

function rng(seed) { let s = seed >>> 0; return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
function hashStr(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
function pickW(R, table) { let tot = 0; for (const k in table) tot += Math.max(0, table[k]); let u = R() * tot; for (const k in table) { u -= Math.max(0, table[k]); if (u < 0) return k; } return Object.keys(table)[0]; }

// ---------------------------------------------------------------- chord naming
function chordSuffix(iv) {
  const s = new Set(iv.map(mod12)), has = x => s.has(x);
  const maj3 = has(4), min3 = has(3) && !has(4), p5 = has(7), d5 = has(6) && !has(7), a5 = has(8) && !has(7) && maj3;
  const m7 = has(10), M7 = has(11), dim7 = has(9) && d5 && min3 && !m7;
  const n9 = iv.some(x => x >= 12 && mod12(x) === 2) || (has(2) && (maj3 || min3)), b9 = has(1), e11 = iv.some(x => x >= 12 && mod12(x) === 5) || (has(5) && (maj3 || min3));
  const s11 = iv.some(x => x >= 12 && mod12(x) === 6) && p5, six = has(9) && !dim7 && !m7 && !M7;
  if (!maj3 && !min3) {
    let b = has(2) && has(5) ? 'sus2sus4' : has(5) ? 'sus4' : has(2) ? 'sus2' : p5 ? '5' : '';
    if (m7) b = (has(2) && !has(5) ? '9sus' : '7' + b);
    return b;
  }
  if (min3 && d5) { if (dim7) return 'dim7'; if (m7) return 'm7b5'; return 'dim'; }
  if (a5) return M7 ? 'maj7#5' : 'aug';
  let q;
  if (maj3) {
    if (M7) q = n9 ? 'maj9' : 'maj7'; else if (m7) q = n9 ? '9' : '7'; else if (six) q = n9 ? '6/9' : '6'; else q = n9 ? 'add9' : '';
    if (s11) q += (q ? '' : 'add') + '#11';
    if (b9) q += 'b9';
  } else {
    if (M7) q = 'm(maj7)'; else if (m7) q = e11 ? 'm11' : n9 ? 'm9' : 'm7'; else if (six) q = 'm6'; else q = n9 ? 'm(add9)' : 'm';
    if (e11 && !m7) q += '(add11)';
    if (b9) q += 'b9';
  }
  return q;
}
const triadQuality = iv => { const s = new Set(iv.map(mod12)); if (s.has(4)) return s.has(8) && !s.has(7) ? 'aug' : 'maj'; if (s.has(3)) return s.has(6) && !s.has(7) ? 'dim' : 'min'; return 'sus'; };
const ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];

// ---------------------------------------------------------------- progression templates (relative to the template's family)
// tags: what moods / genres a template suits. Minor templates use minor numerals, major ones major numerals.
const TEMPLATES = {
  minor: [
    ['i VI III VII', 'progressive trance epic sad euphoric deadmau5 house pop'], ['i VII VI VII', 'trance epic dark'], ['VI VII i i', 'epic trance euphoric cinematic'],
    ['i iv VI V', 'sad tense cinematic'], ['i VI iv VII', 'progressive sad deadmau5'], ['i III VII VI', 'progressive epic euphoric'], ['iv VI i VII', 'progressive deadmau5 dreamy'],
    ['i v VI iv', 'sad chill lofi'], ['VI iv i VII', 'progressive dreamy deadmau5 chill'], ['i i VI VII', 'trance techno epic'], ['i VII v VI', 'dark sad'],
    ['i iv i VII', 'dark techno'], ['i VI VII v', 'progressive deadmau5 dark'], ['VI III VII i', 'epic euphoric progressive'], ['i iv VII III', 'sad chill lofi dreamy'],
    ['i III iv VI', 'dreamy sad cinematic'], ['i VII i VI', 'dark techno tense'], ['i bII i VII', 'tense dark techno'], ['VI VII v i', 'deadmau5 progressive sad'],
    ['i VI III V', 'tense epic cinematic trance'], ['iv i VI VII', 'progressive euphoric house'], ['i iv v iv', 'dark techno chill'],
  ],
  major: [
    ['I V vi IV', 'pop happy euphoric progressive'], ['vi IV I V', 'euphoric progressive pop sad house deadmau5 trance'], ['IV I V vi', 'euphoric progressive house happy'],
    ['I vi IV V', 'happy pop'], ['IV V iii vi', 'euphoric dreamy sad pop'], ['I iii IV V', 'happy dreamy'], ['IV vi I V', 'euphoric progressive dreamy'],
    ['I IV vi V', 'happy pop house'], ['vi V IV V', 'euphoric sad trance'], ['IV I vi V', 'dreamy progressive chill'], ['I V IV IV', 'happy house'],
    ['I bVII IV I', 'happy house epic'], ['IV iv I I', 'dreamy sad cinematic chill'], ['ii V I vi', 'lofi chill jazz happy'], ['I vi ii V', 'lofi chill happy'],
    ['IV iii ii I', 'lofi dreamy chill'], ['I iii vi IV', 'dreamy sad'], ['vi IV V iii', 'sad dreamy progressive'], ['IV V vi I', 'euphoric epic progressive trance'],
    ['I bVI bVII I', 'epic cinematic euphoric'], ['vi ii IV V', 'progressive house chill'],
  ],
  lydian: [['I II I II', 'dreamy cinematic chill'], ['I II vii iii', 'dreamy'], ['I II vi I', 'dreamy happy'], ['vi II I I', 'dreamy euphoric']],
  dorian: [['i IV i IV', 'chill lofi house techno'], ['i ii i IV', 'chill lofi'], ['i IV VII i', 'house chill'], ['i v IV i', 'dark chill techno']],
  mixolydian: [['I VII IV I', 'happy house epic'], ['I v IV I', 'happy chill'], ['I VII IV IV', 'happy house']],
  phrygian: [['i II i i', 'tense dark techno'], ['i II VII i', 'tense dark cinematic'], ['i II i VII', 'dark techno']],
};
const VAMPS = { minor: ['i VI', 'i VII', 'VI VII', 'i iv', 'i III', 'iv i'], major: ['I IV', 'vi IV', 'IV V', 'I V', 'vi V'], lydian: ['I II'], dorian: ['i IV'], mixolydian: ['I VII'], phrygian: ['i II'] };
const tplFamily = scale => scale === 'harmonic' ? 'minor' : scale;

// resolve a numeral token into a chord base: { cs (7-note scale it is built from), dd (degree in cs), root (semis from key), chrom }
function resolveToken(tok, scale) {
  const m = /^(b|#)?(VII|VI|IV|V|III|II|I|vii|vi|iv|v|iii|ii|i)(°|o)?$/.exec(tok); if (!m) return null;
  const up = m[2] === m[2].toUpperCase(), d = ROMAN.indexOf(m[2].toUpperCase()), S = SCALES[scale];
  const minorFam = MINORISH(scale);
  if (m[1] === 'b') {   // borrowed from the parallel minor (or phrygian for bII)
    const cs = d === 1 ? SCALES.phrygian : SCALES.minor, root = cs[d];
    return { cs: cs.slice(), dd: d, root, chrom: !S.includes(root), tok };
  }
  let cs = scale === 'harmonic' && d !== 4 && d !== 6 ? SCALES.minor : S, root = cs[d], chrom = false;
  const q = triadQuality([0, mod12(S[(d + 2) % 7] - S[d]), mod12(S[(d + 4) % 7] - S[d])]);
  if (up && q === 'min' && minorFam && d === 4) { cs = SCALES.harmonic; chrom = scale !== 'harmonic'; }         // V in minor: harmonic minor
  else if (!up && q === 'maj' && !minorFam && (d === 3 || d === 0)) { cs = SCALES.minor; root = cs[d]; chrom = true; }   // iv in major: borrowed
  return { cs: cs.slice(), dd: d, root, chrom, tok };
}
// chord tones from a base: chord-scale tone k steps above degree dd
function csTone(b, k) { const cs = b.cs, i = b.dd + k; return cs[i % 7] + 12 * Math.floor(i / 7) - cs[b.dd]; }
// extension ladder 0..4 (triad, add9/sus, 7th, 7+9, 11/6-9/maj9#11), only diatonic, only consonant extensions
function ladder(b, L, r) {
  const third = csTone(b, 2), fifth = csTone(b, 4), sev = csTone(b, 6), ninth = csTone(b, 8), sec = csTone(b, 1), fourth = csTone(b, 3), six = csTone(b, 5), elev = csTone(b, 10);
  const ok9 = ninth === 14, okSus2 = sec === 2 && fifth === 7, okSus4 = fourth === 5 && fifth === 7 && third !== 5;
  const tri = [0, third, fifth];
  if (L <= 0) return tri;
  if (L === 1) {
    if (r < 0.4 && ok9) return [0, third, fifth, 14];
    if (r < 0.7 && okSus2) return [0, 2, 7];
    if (okSus4 && third === 4 && r >= 0.7) return [0, 5, 7];
    return ok9 ? [0, third, fifth, 14] : okSus2 ? [0, 2, 7] : tri;
  }
  if (L === 2) return [0, third, fifth, sev];
  if (L === 3) return ok9 ? (r < 0.5 ? [0, third, sev, 14] : [0, third, fifth, sev, 14]) : [0, third, fifth, sev];
  if (third === 3 && elev === 17 && ok9) return [0, 3, 7, 10, 14, 17].filter((x, i) => i !== 2 || r < 0.3);   // m11
  if (third === 4 && six === 9 && ok9 && sev === 11 && r < 0.5) return [0, 4, 9, 14];                    // 6/9
  if (third === 4 && sev === 11 && ok9 && elev === 18) return [0, 4, 11, 14, 18];                         // maj9#11 (lydian colour)
  return ladder(b, 3, r);
}
const colourLevel = (colour, r) => colour < 0.04 ? 0 : clamp(Math.round(colour * 4 + (r - 0.5) * 1.4), 0, 4);

// the alternatives a chord chip cycles through
const ALT_NAMES = ['auto', 'sus', '7th', 'lush', 'over 3rd', 'swap down', 'swap up', 'borrow'];
function altChord(slot, scale, colour) {
  if (slot.fixed) {   // imported from MIDI: as-is, plain triad, no slash
    const f = slot.fixed, a = slot.alt % 3;
    let iv = f.iv.slice();
    if (a === 1) { const t = iv.filter(x => x < 12 && [0, 3, 4, 6, 7, 8, 2, 5].includes(x)); iv = t.length >= 2 ? t : iv; }
    return { root: f.root, iv, bass: a === 2 ? null : f.bass, deg: f.deg ?? -1, chrom: !!f.chrom, roman: f.roman || '' };
  }
  let b = resolveToken(slot.tok, scale) || resolveToken(MINORISH(scale) ? 'i' : 'I', scale);
  const alt = slot.alt % ALT_NAMES.length, r = slot.r;
  if (alt === 5 || alt === 6) { const dd = (b.dd + (alt === 5 ? 5 : 2)) % 7, cs = SCALES[scale]; b = { cs: cs.slice(), dd, root: cs[dd], chrom: false }; }
  if (alt === 7) {   // flip to the parallel mode's chord on the same degree
    const other = MINORISH(scale) ? SCALES.major : SCALES.minor; b = { cs: other.slice(), dd: b.dd, root: other[b.dd], chrom: true };
  }
  let iv;
  const L = colourLevel(colour, r);
  if (alt === 1) { const fifth = csTone(b, 4), sec = csTone(b, 1), fourth = csTone(b, 3); iv = sec === 2 && fifth === 7 ? [0, 2, 7] : fourth === 5 && fifth === 7 ? [0, 5, 7] : ladder(b, 1, 0.1); }
  else if (alt === 2) iv = ladder(b, 2, r);
  else if (alt === 3) iv = ladder(b, 4, r);
  else iv = ladder(b, L, r);
  // a raised 3rd / 7th of a harmonic or borrowed chord is chromatic; mark it
  const keyS = SCALES[scale], abs = iv.map(x => mod12(b.root + x));
  const chrom = b.chrom || abs.some(p => !keyS.includes(p));
  const bass = alt === 4 ? mod12(b.root + iv[1]) : null;
  return { root: b.root, iv, bass, deg: b.dd, chrom, cs: b.cs };
}
function romanOf(ch, scale) {
  const S = SCALES[scale], q = triadQuality(ch.iv); let deg = S.indexOf(mod12(ch.root)), acc = '';
  if (deg < 0) { const M = SCALES.major; let d2 = M.indexOf(mod12(ch.root + 1)); if (d2 >= 0) { deg = d2; acc = 'b'; } else { d2 = M.indexOf(mod12(ch.root - 1)); deg = Math.max(0, d2); acc = '#'; } }
  let n = ROMAN[deg]; if (q === 'min' || q === 'dim') n = n.toLowerCase(); if (q === 'dim') n += '°'; if (q === 'aug') n += '+'; if (q === 'sus') n += 'sus';
  return acc + n;
}

// ---------------------------------------------------------------- voice leading
const VCENTER = 62;
function voicings(pcs) {
  const out = [], n = pcs.length;
  for (let inv = 0; inv < n; inv++) {
    const rot = pcs.slice(inv).concat(pcs.slice(0, inv));
    for (let lo = 48; lo < 62; lo++) {
      if (mod12(lo) !== rot[0]) continue;
      const v = [lo]; for (let i = 1; i < n; i++) { let p = v[i - 1] + 1; while (mod12(p) !== rot[i]) p++; v.push(p); }
      out.push(v);
      if (n >= 4) { const d = v.slice(); d[n - 2] -= 12; d.sort((a, b) => a - b); if (d[0] >= 43) out.push(d); }   // drop-2
    }
  }
  return out;
}
function vlCost(a, b) {   // symmetric nearest-note distance, per voice
  let s = 0; for (const x of a) { let m = 99; for (const y of b) m = Math.min(m, Math.abs(x - y)); s += m; }
  for (const y of b) { let m = 99; for (const x of a) m = Math.min(m, Math.abs(x - y)); s += m; }
  return s / (a.length + b.length);
}
function voiceChord(pcsIn, prev, rootPc) {
  let pcs = [...new Set(pcsIn)];
  if (pcs.length > 5) pcs = pcs.slice(0, 5);
  if (pcs.length === 5) { const fifth = mod12(rootPc + 7), i = pcs.indexOf(fifth); if (i > 0) pcs.splice(i, 1); else pcs.pop(); }
  let best = null, bc = 1e9;
  for (const v of voicings(pcs)) {
    let semi = 0; for (let i = 1; i < v.length; i++) if (v[i] - v[i - 1] === 1) semi++;
    const ctr = (v[0] + v[v.length - 1]) / 2;
    let c = semi * 2.5 + Math.abs(ctr - VCENTER) * 0.18 + (v[v.length - 1] > 76 ? 3 : 0);
    if (prev) c += vlCost(v, prev) * 1.6; else c += mod12(v[0]) === rootPc ? 0 : 1.2;
    if (c < bc) { bc = c; best = v; }
  }
  return best;
}

// resolve a whole progression: names, voicings, bass, tick ranges
function resolveProg(S) {
  const out = []; let prev = null, prevBass = null, t = 0;
  S.prog.forEach((slot, i) => {
    const c = altChord(slot, S.scale, S.colour);
    let bass = c.bass;
    if (bass === null && S.pedal && !slot.fixed && i > 0) {   // tonic pedal under chords that don't clash with it
      const abs = c.iv.map(x => mod12(c.root + x)); if (!abs.some(p => p === 1 || p === 11) && c.root !== 0) bass = 0;
    }
    if (bass === null && !slot.fixed && prevBass !== null && slot.r2 !== undefined && slot.r2 < 0.3 && S.colour > 0.3) {   // walk the bass by step when the root would leap
      const third = mod12(c.root + c.iv[1]), dr = Math.min(mod12(c.root - prevBass), mod12(prevBass - c.root)), dt = Math.min(mod12(third - prevBass), mod12(prevBass - third));
      if (dr >= 5 && dt <= 2 && dt > 0 && triadQuality(c.iv) !== 'sus') bass = third;
    }
    const absPcs = c.iv.map(x => mod12(S.key + c.root + x));
    const voicing = voiceChord(absPcs, prev, mod12(S.key + c.root));
    prev = voicing;
    const bpc = mod12(S.key + (bass === null ? c.root : bass)); let bn = 36 + bpc; if (bn < 38) bn += 12;
    prevBass = bass === null ? c.root : bass;
    const name = noteName(S.key + c.root, S.key, S.scale) + chordSuffix(c.iv) + (bass !== null && mod12(bass) !== mod12(c.root) ? '/' + noteName(S.key + bass, S.key, S.scale) : '');
    const len = slot.len || 4;
    out.push({ ...c, bass, name, roman: c.roman || romanOf(c, S.scale), voicing, bassNote: bn, pcs: absPcs, t, len, tEnd: t + len * PPQ });
    t += len * PPQ;
  });
  return out;
}
const progTicks = S => S.prog.reduce((a, s) => a + (s.len || 4) * PPQ, 0);

// ---------------------------------------------------------------- prompt parser
const LEX = [
  [/euphor|uplift|anthem|festival|levels|avicii|big ?room|soar|hands up|joy|ecsta|rave|celebrat/, f => f.tags.euphoric = 1],
  [/happy|bright|sunny|summer|fun\b|cheer|feel ?good|upbeat|playful|cute/, f => f.tags.happy = 1],
  [/dream|float|ethereal|airy|lush|heaven|cloud|space|shimmer|nostalg|magic|wonder|sparkl|glow/, f => f.tags.dreamy = 1],
  [/sad|melanchol|emotion|cry|tear|lonely|bitter|heartbreak|blue|rain|moody|somber|sombre|longing|miss/, f => f.tags.sad = 1],
  [/dark|evil|sinister|night|underground|grim|shadow|haunt|spooky|ghost/, f => f.tags.dark = 1],
  [/tense|tension|suspense|anxious|horror|scary|creepy|thriller|edgy|drama|villain|danger/, f => f.tags.tense = 1],
  [/epic|hero|massive|huge|anthemic|power|triumph|orchestral|giant|stadium|climax/, f => f.tags.epic = 1],
  [/chill|relax|calm|mellow|soft|gentle|lazy|smooth|laid ?back|sunset|beach|cozy|cosy/, f => f.tags.chill = 1],
  [/progressive|\bprog\b/, f => f.genre = f.genre || 'progressive'],
  [/deadmau5|mau5|deadmouse|dead mouse|strobe|prydz|kaskade|melodic/, f => f.genre = f.genre || 'deadmau5'],
  [/trance|psy\b|goa/, f => f.genre = f.genre || 'trance'],
  [/house|disco|club|groove|garage/, f => f.genre = f.genre || 'house'],
  [/techno|industrial|warehouse|berlin|minimal techno|hypnotic techno/, f => f.genre = f.genre || 'techno'],
  [/lo-?fi|jazz|hip ?hop|neo ?soul|study|coffee/, f => f.genre = f.genre || 'lofi'],
  [/cinematic|film|score|movie|trailer|game music|soundtrack/, f => f.genre = f.genre || 'cinematic'],
  [/\bpop\b|radio|catchy/, f => f.genre = f.genre || 'pop'],
  [/ambient|drone|meditat|zen/, f => f.genre = f.genre || 'ambient'],
  [/1\/32|32nd|thirty|very fast arp|machine ?gun/, f => f.rate = '1/32'],
  [/1\/16 ?t\b|triplet|1\/12|shuffl|swung 16/, f => f.rate = '1/16T'],
  [/1\/16|16th|sixteenth/, f => f.rate = f.rate || '1/16'],
  [/1\/8|8th|eighth/, f => f.rate = f.rate || '1/8'],
  [/rolling|roll\b|offbeat|off-beat/, f => f.flags.rolling = 1],
  [/pedal|trance ?gate|gated/, f => f.style = 'pedal'],
  [/up ?down|bounce|ping ?pong|wave/, f => f.style = f.style || 'updown'],
  [/converg|inward|inside out|outside in/, f => f.style = f.style || 'converge'],
  [/random|walk|wander|chaotic|chaos|glitch/, f => f.style = f.style || 'walk'],
  [/broken|skip|leap/, f => f.style = f.style || 'broken'],
  [/motif|hook|riff|hypnotic|repetitive|sequence|loop/, f => f.style = f.style || 'motif'],
  [/ascend|rising|\bup\b(?! ?down)(?!.*octave)|climb/, f => f.style = f.style || 'up'],
  [/descend|falling|\bdown\b|cascade/, f => f.style = f.style || 'down'],
  [/pluck/, f => f.preset = 'pluck'],
  [/supersaw|super saw|\bsaws?\b|trance lead|big lead|hoover/, f => f.preset = 'supersaw'],
  [/piano|keys|rhodes|\bep\b|organ/, f => f.preset = 'keys'],
  [/bell|glass|music ?box|chime|mallet|celest|marimba|kalimba/, f => f.preset = 'bell'],
  [/acid|303|squelch|wobble/, f => { f.preset = 'acid'; f.flags.glide = 1; }],
  [/octave up|up an octave|higher|high pitch|\bhigh\b|sky/, f => f.reg = 1],
  [/octave down|down an octave|lower|\blow\b|deep|bass/, f => f.reg = -1],
  [/three octaves|3 octaves|huge range|very wide/, f => f.oct = 3],
  [/two octaves|2 octaves|\bwide\b/, f => f.oct = f.oct || 2],
  [/one octave|1 octave|narrow|tight/, f => f.oct = 1],
  [/sparse|minimal|simple|easy|basic|few notes|stripped|space between/, f => f.master = 0.2],
  [/busy|complex|crazy|wild|intricate|dense|lots of notes|insane|frantic|nerdy/, f => f.master = 0.85],
  [/ratchet|stutter|repeat/, f => f.flags.ratchet = 1],
  [/glide|legato|slide|portamento|smooth/, f => f.flags.glide = 1],
  [/lush|jazzy|rich|7ths?\b|sevenths|9ths?\b|ninths|extended|neo soul/, f => f.colour = 0.85],
  [/triads|plain|simple chords|basic chords|power chords/, f => f.colour = 0.08],
  [/sus\b|suspended|sus2|sus4|open chords/, f => { f.flags.sus = 1; f.colour = f.colour ?? 0.4; }],
  [/fast|quick|energetic|driving|uptempo|urgent|hard/, f => f.speed = 1],
  [/slow|downtempo|sleepy/, f => f.speed = -1],
  [/\b(2|4|8|16) ?bars?\b/, (f, m) => f.bars = +m[1]],
  [/dorian/, f => f.scale = 'dorian'], [/phrygian/, f => f.scale = 'phrygian'], [/lydian/, f => f.scale = 'lydian'],
  [/mixolydian|mixo/, f => f.scale = 'mixolydian'], [/harmonic|arabic|exotic|egypt/, f => f.scale = 'harmonic'],
];
function parsePrompt(p) {
  const s = ' ' + String(p || '').toLowerCase().replace(/♯/g, '#').replace(/♭/g, 'b').replace(/[,;!?()"]/g, ' ') + ' ';
  const f = { tags: {}, genre: null, key: null, scale: null, bpm: null, rate: null, oct: null, reg: null, master: null, colour: null, preset: null, style: null, bars: null, speed: 0, flags: {} };
  for (const [re, fn] of LEX) { const m = re.exec(s); if (m) fn(f, m); }
  const bpm = /\b(\d{2,3}) ?(bpm|beats)/.exec(s) || /(?:^|\s)(\d{2,3})(?=\s)/.exec(s);
  if (bpm && +bpm[1] >= 60 && +bpm[1] <= 200) f.bpm = +bpm[1];
  // key: "in a minor", "key of f#", "c#m", "ebm", "a minor", "d major", "f#"
  const L = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
  const km = /(?:\bin |\bkey of |\bkey )([a-g])(#|b|-sharp| sharp|-flat| flat)?(?: ?(major|minor|maj|min|m)\b)?/.exec(s)
    || /\b([a-g])(#|b)?(?: ?(major|minor|maj\b|min\b)|(m)\b)/.exec(s) || /(?:^|\s)([a-g])(#|b)(?=\s)/.exec(s);
  if (km) {
    let k = L[km[1]]; const acc = km[2] || ''; if (acc.includes('#') || acc.includes('sharp')) k++; else if (acc === 'b' || acc.includes('flat')) k--;
    f.key = mod12(k); const q = km[3] || km[4];
    if (q && /^m(in(or)?)?$/.test(q)) f.scale = f.scale || 'minor'; else if (q && q.startsWith('maj')) f.scale = f.scale || 'major';
  }
  if (!f.scale) { if (/\bminor\b/.test(s)) f.scale = 'minor'; else if (/\bmajor\b/.test(s)) f.scale = 'major'; }
  return f;
}

// ---------------------------------------------------------------- genre / mood profiles for the generator
const GENRES = {
  progressive: { bpm: 126, styles: { motif: 4, pedal: 2, broken: 1, updown: 1, converge: 1 }, presets: { pluck: 4, supersaw: 1, bell: 1 }, colour: 0.5, scales: { minor: 6, major: 2, dorian: 1 }, oct: 2, master: 0.5, pedal: 0.35, bars: { 4: 6, 8: 4, 2: 1 } },
  deadmau5: { bpm: 128, styles: { motif: 5, pedal: 2, broken: 1, converge: 1 }, presets: { pluck: 4, keys: 1, bell: 1 }, colour: 0.55, scales: { minor: 7, major: 1, dorian: 1, phrygian: 1 }, oct: 2, master: 0.5, pedal: 0.45, bars: { 4: 6, 8: 4, 2: 1 } },
  trance: { bpm: 138, styles: { pedal: 4, up: 2, updown: 2, motif: 2 }, presets: { supersaw: 3, pluck: 2 }, colour: 0.3, scales: { minor: 6, major: 1, harmonic: 1 }, oct: 2, master: 0.6, pedal: 0.25, bars: { 4: 5, 8: 4 } },
  house: { bpm: 124, styles: { broken: 2, motif: 3, up: 1, walk: 1 }, presets: { pluck: 3, keys: 2 }, colour: 0.6, scales: { minor: 3, major: 3, dorian: 2 }, oct: 2, master: 0.5, pedal: 0.2, bars: { 4: 6, 2: 2, 8: 2 } },
  techno: { bpm: 130, styles: { walk: 2, motif: 4, pedal: 2 }, presets: { acid: 3, pluck: 3 }, colour: 0.15, scales: { minor: 3, phrygian: 3, dorian: 1 }, oct: 1, master: 0.5, pedal: 0.5, bars: { 2: 3, 4: 4 } },
  lofi: { bpm: 84, styles: { broken: 2, up: 2, updown: 1, motif: 1 }, presets: { keys: 5, bell: 1 }, colour: 0.85, scales: { major: 3, dorian: 2, minor: 2 }, oct: 2, master: 0.35, pedal: 0, bars: { 4: 6, 8: 2 }, swing: 0.35, rate: '1/8' },
  cinematic: { bpm: 94, styles: { updown: 3, up: 2, converge: 1, motif: 1 }, presets: { bell: 2, keys: 2, supersaw: 1 }, colour: 0.4, scales: { minor: 4, harmonic: 2, major: 1, lydian: 1 }, oct: 3, master: 0.55, pedal: 0.2, bars: { 4: 4, 8: 4 } },
  pop: { bpm: 116, styles: { up: 2, broken: 2, updown: 1, motif: 2 }, presets: { pluck: 2, keys: 2 }, colour: 0.35, scales: { major: 4, minor: 2 }, oct: 2, master: 0.45, pedal: 0.1, bars: { 4: 7, 8: 2 } },
  ambient: { bpm: 90, styles: { updown: 2, walk: 1, up: 1, motif: 1 }, presets: { bell: 3, keys: 2 }, colour: 0.8, scales: { lydian: 3, major: 2, dorian: 1 }, oct: 2, master: 0.3, pedal: 0.4, bars: { 4: 3, 8: 4 }, rate: '1/8' },
};
const MOODS = {
  euphoric: { scales: { major: 3, minor: 2, lydian: 1 }, presets: { supersaw: 2, pluck: 2 }, colour: 0.05, master: 0.1, bpm: 2, oct: 0 },
  happy: { scales: { major: 5, mixolydian: 2, lydian: 1 }, presets: { pluck: 1, keys: 1 }, colour: -0.05, master: 0, bpm: 2 },
  dreamy: { scales: { lydian: 3, major: 3, dorian: 1, minor: 1 }, presets: { bell: 3, keys: 1 }, colour: 0.25, master: -0.08, bpm: -4 },
  sad: { scales: { minor: 5, dorian: 2 }, presets: { keys: 2, pluck: 1 }, colour: 0.1, master: -0.05, bpm: -4 },
  dark: { scales: { minor: 4, phrygian: 3 }, presets: { pluck: 2, acid: 1 }, colour: -0.15, master: 0, bpm: 0 },
  tense: { scales: { harmonic: 4, phrygian: 3, minor: 1 }, presets: { pluck: 1, bell: 1 }, colour: 0.05, master: 0.05, bpm: 0 },
  epic: { scales: { minor: 5, major: 2 }, presets: { supersaw: 3 }, colour: 0, master: 0.12, bpm: 2, oct: 1 },
  chill: { scales: { major: 2, dorian: 2, minor: 1 }, presets: { keys: 2, bell: 1 }, colour: 0.2, master: -0.15, bpm: -10 },
};
const STYLES = ['motif', 'pedal', 'up', 'down', 'updown', 'converge', 'broken', 'walk'];
const PRESETS = ['pluck', 'supersaw', 'keys', 'bell', 'acid'];

function generate(prompt, seed) {
  const f = parsePrompt(prompt), R = rng((seed >>> 0) ^ hashStr(String(prompt || '').trim().toLowerCase()) ^ 0x9e3779b9);
  for (let i = 0; i < 3; i++) R();
  const moods = Object.keys(f.tags);
  const genre = f.genre || pickW(R, moods.includes('chill') ? { progressive: 2, deadmau5: 2, house: 2, lofi: 1 } : { progressive: 4, deadmau5: 4, trance: 1, house: 1 });
  const G = GENRES[genre];
  const add = (a, b, w = 1) => { const o = { ...a }; for (const k in b) o[k] = (o[k] || 0) + b[k] * w; return o; };
  let scales = { ...G.scales }, presets = { ...G.presets };
  let colour = G.colour, master = G.master, bpm = G.bpm, oct = G.oct;
  for (const m of moods) { const M = MOODS[m]; scales = add(scales, M.scales, 2); presets = add(presets, M.presets, 1.5); colour += M.colour; master += M.master; bpm += M.bpm; oct += M.oct || 0; }
  const scale = f.scale || pickW(R, scales);
  const key = f.key ?? +pickW(R, MINORISH(scale) ? { 5: 3, 9: 3, 1: 2, 7: 2, 2: 2, 0: 1, 4: 1, 11: 1, 8: 1, 3: 1, 6: 1, 10: 1 } : { 0: 2, 5: 2, 7: 2, 2: 2, 9: 1, 8: 2, 3: 2, 10: 1, 1: 1, 4: 1, 6: 1, 11: 1 });
  const bars = f.bars || +pickW(R, G.bars);
  // progression: template weighted by tag matches
  const want = moods.concat([genre]);
  const fam = tplFamily(scale), pool = (TEMPLATES[fam] || TEMPLATES.minor).concat(fam === 'lydian' || fam === 'mixolydian' ? TEMPLATES.major.slice(0, 6) : fam === 'dorian' || fam === 'phrygian' ? TEMPLATES.minor.slice(0, 8) : []);
  const tw = {}; pool.forEach(([t, tags], i) => { const tg = tags.split(' '); tw[i] = 1 + 3 * want.filter(w => tg.includes(w)).length; });
  const tpl = pool[+pickW(R, tw)][0].split(' ');
  let toks;
  if (bars === 2) { const vs = VAMPS[fam] || VAMPS.minor; toks = R() < 0.5 ? tpl.slice(0, 2) : vs[Math.floor(R() * vs.length)].split(' '); }
  else if (bars === 4) toks = tpl.slice();
  else {
    const u = R();
    if (u < 0.45) { const v = tpl.slice(); const alts = MINORISH(scale) ? ['VII', 'v', 'VI', 'iv', 'III'] : ['V', 'IV', 'iii', 'vi', 'ii']; let r = alts[Math.floor(R() * alts.length)]; if (r === v[3]) r = alts[(alts.indexOf(r) + 1) % alts.length]; v[3] = r; toks = tpl.concat(v); }
    else if (u < 0.75) toks = tpl.flatMap(t => [t, t]);
    else { const t2 = pool[+pickW(R, tw)][0].split(' '); toks = tpl.concat(t2); }
    if (bars === 16) toks = toks.concat(toks);
  }
  if (bars === 16 && toks.length < 16) toks = toks.concat(toks).concat(toks).concat(toks).slice(0, 16);
  colour = clamp((f.colour ?? colour) + (R() - 0.5) * 0.16, 0, 1);
  const prog = toks.map(tok => ({ tok, r: R(), r2: R(), alt: 0, len: 4 }));
  if (f.flags.sus) prog.forEach((p, i) => { if (i % 2 === 1) p.alt = 1; });
  const style = f.style || pickW(R, G.styles);
  const preset = f.preset || pickW(R, presets);
  let rate = f.rate || G.rate || (R() < 0.12 && genre !== 'lofi' ? '1/8' : '1/16');
  if (moods.includes('chill') && !f.rate && R() < 0.4) rate = '1/8';
  if (f.speed > 0) bpm += 8; if (f.speed < 0) bpm -= 14;
  bpm = clamp(Math.round(f.bpm || bpm + (R() - 0.5) * 4), 60, 200);
  master = clamp(f.master ?? master + (R() - 0.5) * 0.1, 0, 1);
  oct = clamp(f.oct || Math.round(oct), 1, 3);
  const glide = !!f.flags.glide || preset === 'acid';
  const pedal = R() < G.pedal;
  const gate = preset === 'keys' || preset === 'bell' ? 0.85 : preset === 'supersaw' ? 0.7 : preset === 'acid' ? 0.6 : 0.5;
  const fx = { tone: preset === 'acid' ? 0.5 : 0.72, delay: moods.includes('dreamy') || genre === 'ambient' ? 0.45 : genre === 'techno' ? 0.2 : 0.32, reverb: moods.includes('dreamy') || genre === 'cinematic' || genre === 'ambient' ? 0.5 : 0.3,
    pump: genre === 'lofi' || genre === 'ambient' || genre === 'cinematic' ? 0.1 : 0.45 };
  return { prompt: String(prompt || ''), seed: seed >>> 0, genre, moods, key, scale, bars, colour, prog, style, preset, rate, bpm, master, oct, reg: f.reg ?? 0, glide, ratchet: !!f.flags.ratchet, rolling: !!f.flags.rolling,
    pedal, gate, swing: G.swing || 0, fx, arpSeed: Math.floor(R() * 1e9), progSeed: Math.floor(R() * 1e9) };
}
// new progression only (same key/scale/genre), used by NEW CHORDS
function newProg(S, seed) {
  const R = rng(seed), fam = tplFamily(S.scale), pool = TEMPLATES[fam] || TEMPLATES.minor;
  const want = (S.moods || []).concat([S.genre || 'progressive']);
  const tw = {}; pool.forEach(([t, tags], i) => { const tg = tags.split(' '); tw[i] = 1 + 3 * want.filter(w => tg.includes(w)).length; });
  const bars = Math.max(2, Math.round(progTicks(S) / BAR)) || 4;
  let toks = pool[+pickW(R, tw)][0].split(' ');
  if (bars === 2) toks = toks.slice(0, 2); else if (bars >= 8) { const t2 = pool[+pickW(R, tw)][0].split(' '); toks = toks.concat(t2); if (bars === 16) toks = toks.concat(toks); }
  return toks.map(tok => ({ tok, r: R(), r2: R(), alt: 0, len: 4 }));
}

// ---------------------------------------------------------------- the arpeggio engine
function chordAt(chords, t) { for (const c of chords) if (t >= c.t && t < c.tEnd) return c; return chords[chords.length - 1]; }
function buildArp(S, chords) {
  const spb = RATES[S.rate] || 16, st = BAR / spb, total = chords.length ? chords[chords.length - 1].tEnd : BAR * 4, nBars = Math.ceil(total / BAR);
  const m = clamp(S.master, 0, 1), R = rng(S.arpSeed >>> 0), style = S.style || 'motif';
  const U = n => { const a = new Array(n); for (let i = 0; i < n; i++) a[i] = R(); return a; };
  // DNA: every random number is drawn up front so the MASTER slider morphs the same idea instead of re-rolling it
  const dna = { cyc: R(), phase: R() < 0.55, eat: R() < 0.45, resp: Math.floor(R() * 4), pedalTop: R() < 0.65, acc: R(), start: R(), motifMax: R(), glideU: R(), fatU: R() };
  const uHit = U(spb * 2), uVar = U(spb), uAcc = U(spb * 2), uRat = U(spb * 2), uPass = U(spb * 2), uOct = U(spb * 2), uGate = U(spb), uWalk = U(64), uMotif = U(16), uGl = U(spb * 2), uHum = U(spb * 4);
  const bl = spb / 4, triplet = spb === 24;
  const strength = pos => pos === 0 ? 4 : pos % bl === 0 ? 3 : (!triplet && pos % (bl / 2) === 0) || (triplet && pos % 3 === 0) ? 2 : 1;
  // rhythm (bar A + a variation B used in "response" bars)
  const hitP = s => s === 4 ? 1 : s === 3 ? 0.97 : s === 2 ? (spb === 8 ? 1 : 0.78 + 0.22 * m) : spb === 32 ? 0.12 + 0.6 * m : triplet ? 0.3 + 0.55 * m : 0.32 + 0.62 * m;
  const rhythm = (off) => { const r = []; for (let p = 0; p < spb; p++) { let h = uHit[(p + off) % (spb * 2)] < hitP(strength(p)); if (S.rolling && strength(p) >= 3) h = uHit[(p + off + 3) % (spb * 2)] < 0.12; else if (S.rolling && strength(p) === 1) h = true; r.push(h); } return r; };
  const rA = rhythm(0), rB = rA.map((h, p) => uVar[p] < 0.12 + 0.3 * m && strength(p) < 3 ? !h : h);
  for (const r of [rA, rB]) { let n = r.filter(Boolean).length; for (let p = 0; n < Math.max(2, spb / 4); p = (p + Math.max(1, bl / 2)) % spb) { if (!r[p]) { r[p] = true; n++; } } }
  // motif (cycle) for the motif style: short contour, 3..7 notes, phasing against the bar = the hypnotic prog-house thing
  const lens = m < 0.33 ? [3, 4, 4, 3] : m < 0.66 ? [3, 4, 6, 5] : [5, 6, 7, 3];
  const cycLen = lens[Math.floor(dna.cyc * 4)];
  const mMax = 3 + Math.round(dna.motifMax * 2 + m * 2);
  const motif = [dna.start < 0.5 ? 0 : dna.start < 0.8 ? 2 : 1];
  for (let i = 1; i < cycLen; i++) { const u = uMotif[i]; let st2 = u < 0.32 ? 1 : u < 0.52 ? 2 : u < 0.72 ? -1 : u < 0.86 ? 3 : -2; let v = motif[i - 1] + st2; if (v < 0) v = -v; if (v > mMax) v = 2 * mMax - v; motif.push(clamp(v, 0, mMax)); }
  if (new Set(motif).size === 1) motif[motif.length - 1] = motif[0] + 2;
  const walk = [0]; for (let i = 1; i < 64; i++) { const u = uWalk[i]; let v = walk[i - 1] + (u < 0.35 ? 1 : u < 0.7 ? -1 : u < 0.85 ? 2 : -2); if (v < 0) v = 1; if (v > 7) v = 6; walk.push(v); }
  const seqIdx = (c, P) => {
    switch (style) {
      case 'up': return c % P;
      case 'down': return P - 1 - (c % P);
      case 'updown': { const per = Math.max(1, 2 * P - 2), k = c % per; return k < P ? k : per - k; }
      case 'converge': { const k = c % P; return k % 2 === 0 ? k / 2 : P - 1 - (k - 1) / 2; }
      case 'broken': { const per = Math.max(2, 2 * (P - 2)), k = c % per; return Math.min(P - 1, Math.floor(k / 2) + (k % 2) * 2); }
      case 'walk': { const per = Math.max(1, 2 * P - 2), k = walk[c % 64] % per; return k < P ? k : per - k; }
      case 'pedal': { if (c % 2 === 0) return dna.pedalTop ? P - 1 : 0; const k = (c >> 1) % Math.max(1, P - 1); return dna.pedalTop ? k : k + 1; }
      default: return motif[c % cycLen];
    }
  };
  const inKey = (p, ch) => (ch.cs || SCALES[S.scale]).includes(mod12(p - S.key)) || ch.pcs.includes(mod12(p));
  const octEff = clamp((S.oct || 2) + (m > 0.74 ? 1 : 0), 1, 3);
  const notes = []; let c = 0, prevP = null;
  const reg = 12 + 12 * (S.reg || 0);
  for (let b = 0; b < nBars; b++) {
    const phraseEnd = b % 4 === 3, resp = m >= 0.2 && (phraseEnd || (b % 2 === 1 && m > 0.5));
    const rr = resp ? rB : rA; if (!dna.phase) c = 0;
    for (let pos = 0; pos < spb; pos++) {
      const t = b * BAR + pos * st; if (t >= total) break;
      const s = strength(pos), hit = rr[pos];
      if (!hit) { if (dna.eat && style !== 'pedal') c++; continue; }
      const ch = chordAt(chords, t), tones = ch.voicing.length;
      const base = ch.voicing.map(x => x + reg);
      const P = clamp(Math.round(tones * octEff * (0.55 + 0.45 * m)), 3, tones * octEff);
      let idx = seqIdx(c, P); c++;
      // call & response
      if (resp) {
        const half = pos >= spb / 2, lastQ = pos >= spb * 3 / 4;
        if (dna.resp === 0 && half) idx = Math.min(P - 1, idx + (m > 0.7 ? 2 : 1));
        else if (dna.resp === 1 && (m > 0.7 ? half : lastQ)) idx += tones;
        else if (dna.resp === 2 && half) idx = P - 1 - Math.min(idx, P - 1);
        else if (dna.resp === 3 && lastQ) idx = Math.min(P - 1, P - (spb - pos));
      }
      let p = base[idx % tones] + 12 * Math.floor(idx / tones);
      if (m > 0.6 && s === 1 && uOct[(b * 3 + pos) % (spb * 2)] < (m - 0.6) * 0.55) p += 12;
      // passing / neighbour tone (stays in key): on weak steps, step from the previous note toward this one
      let pass = false;
      if (s === 1 && prevP !== null && uPass[(b * 5 + pos) % (spb * 2)] < 0.04 + 0.22 * m && Math.abs(p - prevP) >= 3) {
        const dir = p > prevP ? 1 : -1; let q = prevP + dir; while (!inKey(q, ch)) q += dir; if (q !== p) { p = q; pass = true; }
      }
      while (p > 98) p -= 12; while (p < 50) p += 12;
      const accented = (() => { const g16 = Math.round(pos * 16 / spb); if (m < 0.35 || dna.acc < 0.5) return s >= 3; return [0, 3, 6, 8, 11, 14].includes(g16) && (pos * 16) % spb === 0; })();
      let v = (accented ? 0.86 : s === 2 ? 0.7 : 0.6) + (uHum[(b * 7 + pos) % (spb * 4)] - 0.5) * 0.06;
      const gt = clamp((S.gate || 0.5) * (s >= 3 ? 1 : 0.85 + 0.3 * uGate[pos]), 0.12, 1);
      const rat = (spb <= 16 && s < 4 && uRat[(b * 11 + pos) % (spb * 2)] < Math.max(0, m - 0.55) * (S.ratchet ? 1.2 : 0.42)) ? (spb === 8 && uRat[pos] < 0.05 ? 3 : 2) : 1;
      for (let k = 0; k < rat; k++) notes.push({ t: t + Math.round(k * st / rat), d: Math.max(6, Math.round(st * gt / rat)), p, v: clamp(v * (k ? 0.82 : 1), 0.1, 1), pass, l: { type: 'step', w: 0.55, f: 0.5, c: 0 } });
      prevP = p;
    }
  }
  // default links: a few glides and fattens so the flow is visible (and audible) out of the box
  for (let i = 0; i + 1 < notes.length; i++) {
    const a = notes[i], b = notes[i + 1], bp = Math.floor(b.t / BAR), barStart = b.t % BAR === 0, ivl = Math.abs(b.p - a.p);
    const u = uGl[i % (spb * 2)];
    if (S.glide && ivl > 0 && ivl <= 7 && u < 0.32) a.l = { type: 'glide', w: 0.5 + 0.3 * uGl[(i + 3) % (spb * 2)], f: 0.5, c: 0 };
    else if (barStart && bp % 4 === 0 && b.v > 0.8 && dna.fatU < 0.85) a.l = { type: 'fatten', w: 0.55, f: 0.55, c: 0 };
    else if (barStart && bp % 4 === 3 + 0 && ivl > 0 && ivl <= 7) a.l = { type: 'glide', w: 0.6, f: 0.5, c: 0 };
  }
  // the last note before each phrase restart glides into it (end of 4 bars), to show the curve
  for (let i = 0; i + 1 < notes.length; i++) { const b = notes[i + 1]; if (b.t % (BAR * 4) === 0 && Math.abs(b.p - notes[i].p) > 0 && Math.abs(b.p - notes[i].p) <= 9 && notes[i].l.type === 'step' && dna.glideU < 0.7) notes[i].l = { type: 'glide', w: 0.62, f: 0.5, c: 0 }; }
  return notes;
}
// velocity a note plays with: its own + the fatten of the link coming into it
const playVel = (notes, i) => { const pl = i > 0 ? notes[i - 1].l : null; return clamp(notes[i].v * (pl && pl.type === 'fatten' ? 1 + 0.7 * pl.f : 1), 0.05, 1); };

// ---------------------------------------------------------------- mood analysis (drives the ghost's face)
function analyzeMood(chords, S) {
  let val = 0, ten = 0, lush = 0, n = 0, heroic = 0;
  const modeB = { lydian: 0.45, major: 0.3, mixolydian: 0.2, dorian: -0.1, minor: -0.3, harmonic: -0.35, phrygian: -0.5 }[S.scale] || 0;
  const modeT = { harmonic: 0.25, phrygian: 0.3 }[S.scale] || 0;
  chords.forEach((c, i) => {
    const s = new Set(c.iv.map(mod12)), w = (i === 0 ? 1.6 : 1) * (c.len || 4) / 4, q = triadQuality(c.iv);
    n += w;
    if (q === 'maj') val += w; else if (q === 'min') val -= w * 0.8; else if (q === 'dim') { val -= 0.5 * w; ten += 1.1 * w; } else if (q === 'aug') ten += 0.9 * w; else { lush += 0.35 * w; ten += 0.12 * w; }
    if (s.has(4) && s.has(10)) ten += 0.65 * w;
    if (s.has(1)) ten += 0.8 * w; if (s.has(6) && s.has(7)) ten += 0.3 * w;
    if (s.has(11) && s.has(4)) lush += 0.9 * w; if (c.iv.some(x => x >= 12)) lush += 0.75 * w; if (s.has(9) && !s.has(10)) lush += 0.4 * w; if (s.has(10) && s.has(3)) lush += 0.3 * w;
    if (c.chrom) ten += 0.35 * w;
    if (c.bass !== null && c.bass !== undefined) lush += 0.15 * w;
    const nx = chords[(i + 1) % chords.length]; if (nx && q !== 'min' && q !== 'dim' && triadQuality(nx.iv) !== 'min' && mod12(nx.root - c.root) === 2) heroic += w;   // VI-VII / IV-V lift
  });
  val = val / n + modeB; ten = ten / n + modeT; lush = lush / n; heroic = heroic / n;
  const energy = clamp((S.bpm - 80) / 70 * 0.5 + S.master * 0.45 + (S.preset === 'supersaw' ? 0.15 : 0) - (S.rate === '1/8' ? 0.12 : 0), 0, 1);
  const sc = {
    happy: val * 1.25 + 0.15 - lush * 0.65 - ten * 1.2 - (energy > 0.75 ? 0.1 : 0),
    dreamy: lush * 1.0 + (val > -0.35 ? 0.25 : -0.15) - energy * 0.62 - ten * 0.8,
    sad: -val * 1.15 - energy * 0.25 + lush * 0.1 - ten * 0.5,
    tense: ten * 2.1 - 0.35,
    epic: energy * 1.4 + heroic * 0.9 + Math.abs(val) * 0.15 - lush * 0.3 - 0.68,
  };
  let mood = 'happy', best = -1e9; for (const k in sc) if (sc[k] > best) { best = sc[k]; mood = k; }
  return { mood, scores: sc, valence: val, tension: ten, lush, energy };
}

// ---------------------------------------------------------------- MIDI writer (SMF type 1, 96 PPQ)
function vlq(n) { const b = [n & 0x7f]; while ((n >>= 7)) b.unshift((n & 0x7f) | 0x80); return b; }
function writeMidi(bpm, tracks) {
  const bytes = [], str = s => [...s].map(ch => ch.charCodeAt(0) & 0xff), u32 = v => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
  const chunk = (id, data) => { bytes.push(...str(id), ...u32(data.length), ...data); };
  chunk('MThd', [0, 1, 0, tracks.length + 1, 0, PPQ]);
  const us = Math.round(60e6 / bpm), name = 'ArpEZator';
  chunk('MTrk', [0, 0xff, 0x03, name.length, ...str(name), 0, 0xff, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255, 0, 0xff, 0x58, 4, 4, 2, 24, 8, 0, 0xff, 0x2f, 0]);
  for (const tr of tracks) {
    const ev = [];
    for (const n of tr.notes) { const v = clamp(Math.round(n.v * 127), 1, 127); ev.push([n.t, 1, 0x90 | tr.ch, n.p, v]); ev.push([n.t + Math.max(1, n.d), 0, 0x80 | tr.ch, n.p, 0]); }
    ev.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const d = [0, 0xff, 0x03, tr.name.length, ...str(tr.name)]; let last = 0;
    for (const e of ev) { d.push(...vlq(e[0] - last), e[2], e[3], e[4]); last = e[0]; }
    d.push(0, 0xff, 0x2f, 0); chunk('MTrk', d);
  }
  return new Uint8Array(bytes);
}
// arp notes for MIDI: glides become overlapping (legato) notes so mono synths slide, fatten becomes velocity
function arpMidiNotes(notes) {
  return notes.map((n, i) => { const nx = notes[i + 1]; let d = n.d; if (nx && n.l.type === 'glide') d = Math.max(d, nx.t - n.t + 6); return { t: n.t, d, p: n.p, v: playVel(notes, i) }; });
}
function chordMidiNotes(chords, withBass) {
  const out = []; for (const c of chords) { for (const p of c.voicing) out.push({ t: c.t, d: c.len * PPQ - 2, p, v: 0.62 }); if (withBass) out.push({ t: c.t, d: c.len * PPQ - 2, p: c.bassNote, v: 0.7 }); } return out;
}

// ---------------------------------------------------------------- MIDI reader + chord detection (for Josh's chord packs)
function readMidi(buf) {
  const d = buf instanceof Uint8Array ? buf : new Uint8Array(buf); let o = 0;
  const u32 = () => { const v = (d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) >>> 0; o += 4; return v; }, u16 = () => { const v = d[o] << 8 | d[o + 1]; o += 2; return v; };
  const id = () => String.fromCharCode(d[o], d[o + 1], d[o + 2], d[o + 3]);
  if (id() !== 'MThd') throw new Error('not a MIDI file');
  o += 4; const hl = u32(), fmt = u16(), ntr = u16(); let ppq = u16(); o = 8 + hl; if (ppq & 0x8000) ppq = 96;
  const notes = []; let tempo = 500000, ts = [4, 4];
  for (let tr = 0; tr < ntr && o < d.length; tr++) {
    while (o < d.length && id() !== 'MTrk') { o += 4; const l = u32(); o += l; }
    if (o >= d.length) break;
    o += 4; const len = u32(), end = o + len; let t = 0, run = 0; const on = {};
    const rv = () => { let v = 0, b; do { b = d[o++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80 && o < end); return v; };
    while (o < end) {
      t += rv(); let st = d[o];
      if (st & 0x80) o++; else st = run;
      if (st === 0xff) { const ty = d[o++], l = rv(); if (ty === 0x51 && l === 3) tempo = d[o] << 16 | d[o + 1] << 8 | d[o + 2]; if (ty === 0x58) ts = [d[o], 1 << d[o + 1]]; o += l; if (ty === 0x2f) break; continue; }
      if (st === 0xf0 || st === 0xf7) { const l = rv(); o += l; continue; }
      run = st; const ty = st & 0xf0, ch = st & 15;
      if (ty === 0xc0 || ty === 0xd0) { o++; continue; }
      const a = d[o++], b = d[o++];
      const k = ch * 128 + a;
      if (ty === 0x90 && b > 0) { (on[k] = on[k] || []).push({ t, v: b }); }
      else if (ty === 0x80 || (ty === 0x90 && b === 0)) { const s = on[k] && on[k].shift(); if (s) notes.push({ t: s.t, d: Math.max(1, t - s.t), p: a, v: s.v / 127, ch }); }
    }
    for (const k in on) for (const s of on[k]) notes.push({ t: s.t, d: Math.max(1, t - s.t), p: k % 128, v: s.v / 127, ch: Math.floor(k / 128) });
    o = end;
  }
  notes.sort((a, b) => a.t - b.t || a.p - b.p);
  return { fmt, ppq, notes, bpm: Math.round(60e6 / tempo * 10) / 10, ts };
}
const CHORD_TPL = [
  ['', [0, 4, 7], 1], ['m', [0, 3, 7], 1], ['dim', [0, 3, 6], 0.7], ['aug', [0, 4, 8], 0.5], ['sus2', [0, 2, 7], 0.8], ['sus4', [0, 5, 7], 0.8],
  ['7', [0, 4, 7, 10], 0.9], ['maj7', [0, 4, 7, 11], 0.95], ['m7', [0, 3, 7, 10], 0.95], ['m7b5', [0, 3, 6, 10], 0.8], ['add9', [0, 2, 4, 7], 0.9], ['madd9', [0, 2, 3, 7], 0.9],
  ['9', [0, 2, 4, 7, 10], 0.8], ['maj9', [0, 2, 4, 7, 11], 0.85], ['m9', [0, 2, 3, 7, 10], 0.85], ['6', [0, 4, 7, 9], 0.7], ['m6', [0, 3, 7, 9], 0.6], ['m11', [0, 3, 5, 7, 10], 0.7], ['7sus4', [0, 5, 7, 10], 0.7],
];
function identifyChord(pcsArr, bassPc) {
  const pcs = new Set(pcsArr); let best = null, bs = -1e9;
  for (let r = 0; r < 12; r++) for (const [nm, tpl, prior] of CHORD_TPL) {
    const T = new Set(tpl.map(x => (x + r) % 12)); let match = 0, miss = 0, extra = 0;
    for (const x of T) pcs.has(x) ? match++ : miss++; for (const x of pcs) if (!T.has(x)) extra++;
    const sc = match * 2 - miss * (tpl.includes(7) && !pcs.has((r + 7) % 12) && miss === 1 ? 1 : 2.2) - extra * 1.4 + prior + (r === bassPc ? 0.8 : 0) + (pcs.has(r) ? 0.5 : -2);
    if (sc > bs) { bs = sc; best = { root: r, nm, tpl }; }
  }
  const rel = [...pcs].map(p => mod12(p - best.root)).sort((a, b) => a - b);
  const third = rel.includes(3) || rel.includes(4);
  const iv = rel.map(x => (x === 2 && third) ? 14 : (x === 5 && third) ? 17 : x === 1 ? 13 : (x === 6 && rel.includes(7)) ? 18 : x).sort((a, b) => a - b);
  return { root: best.root, iv, bass: bassPc !== best.root && pcs.has(bassPc) ? bassPc : null };
}
const KS_MAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88], KS_MIN = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
function detectKey(hist, firstRoot, firstMinor) {
  let best = { key: 0, scale: 'minor' }, bs = -1e9, home = null, hs = -1e9;
  const corr = (a, b) => { const ma = a.reduce((x, y) => x + y) / 12, mb = b.reduce((x, y) => x + y) / 12; let n = 0, da = 0, db = 0; for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; } return n / Math.sqrt(da * db || 1); };
  for (let k = 0; k < 12; k++) for (const [sc, prof] of [['major', KS_MAJ], ['minor', KS_MIN]]) {
    const rot = prof.map((_, i) => prof[mod12(i - k)]); const c = corr(hist, rot) + (k === firstRoot ? 0.2 : 0);
    if (c > bs) { bs = c; best = { key: k, scale: sc }; }
    if (k === firstRoot && (sc === 'minor') === !!firstMinor) { hs = c; home = { key: k, scale: sc }; }
  }
  return home && hs > bs - 0.25 ? home : best;   // chord packs usually start on the home chord
}
function chordsFromMidi(midi) {
  const q = midi.ppq / 2;   // half-beat grid
  const ns = midi.notes.filter(n => n.ch !== 9).map(n => ({ s: Math.round(n.t / q), e: Math.max(Math.round(n.t / q) + 1, Math.round((n.t + n.d) / q)), p: n.p }));
  if (!ns.length) throw new Error('no notes in this file');
  const t0 = Math.floor(Math.min(...ns.map(n => n.s)) / 8) * 8;   // start at the bar containing the first note
  const end = Math.max(...ns.map(n => n.e));
  const segs = [];
  for (let t = t0; t < end; t++) {
    const on = ns.filter(n => n.s <= t && n.e > t); const pcs = [...new Set(on.map(n => n.p % 12))].sort((a, b) => a - b);
    const key = pcs.join(','), bass = on.length ? Math.min(...on.map(n => n.p)) % 12 : -1;
    const last = segs[segs.length - 1];
    if (last && last.key === key && last.bass === bass) last.len++; else segs.push({ key, pcs, bass, len: 1 });
  }
  // short or empty segments fold into the previous chord
  const merged = [];
  for (const s of segs) {
    const last = merged[merged.length - 1];
    if (last && (s.pcs.length < 2 || s.len < 2)) { last.len += s.len; continue; }
    if (s.pcs.length < 2) { if (last) last.len += s.len; else merged.push({ ...s, empty: true }); continue; }
    merged.push({ ...s });
  }
  const real = merged.filter(s => !s.empty);
  if (!real.length) throw new Error('no chords found');
  if (merged[0].empty && merged.length > 1) { merged[1].len += merged[0].len; merged.shift(); }
  const hist = new Array(12).fill(0); for (const n of ns) hist[n.p % 12] += n.e - n.s;
  const chords = merged.map(s => ({ ...identifyChord(s.pcs, s.bass), len: Math.max(1, Math.round(s.len / 2)) }));
  const out = []; for (const c of chords) { const l = out[out.length - 1]; if (l && l.root === c.root && l.iv.join() === c.iv.join() && l.bass === c.bass) l.len += c.len; else out.push(c); }
  let beats = 0; const cut = []; for (const c of out) { if (beats >= 64) break; const l = Math.min(c.len, 64 - beats); cut.push({ ...c, len: l }); beats += l; }
  const rem = (4 - beats % 4) % 4; cut[cut.length - 1].len += rem;
  const key = detectKey(hist, cut[0].root, cut[0].iv.includes(3) && !cut[0].iv.includes(4));
  return { key: key.key, scale: key.scale, bpm: midi.bpm, chords: cut };
}

const ArpCore = { BAR, PPQ, RATES, SCALES, SCALE_LABEL, SCALE_FEEL, MINORISH, STYLES, PRESETS, ALT_NAMES, GENRES, rng, hashStr, mod12, clamp, noteName, keyName, chordSuffix, triadQuality, resolveToken, altChord,
  resolveProg, progTicks, voiceChord, vlCost, parsePrompt, generate, newProg, buildArp, chordAt, playVel, analyzeMood, writeMidi, readMidi, arpMidiNotes, chordMidiNotes, chordsFromMidi, identifyChord, detectKey };
if (typeof module !== 'undefined') module.exports = ArpCore;
