// Dumps chord-generator outputs from the web app's theory.js so the C++ port can be checked against them.
// usage: node dump_js.mjs /path/to/theory.js > chords_ref.txt
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const A = require(process.argv[2]);
const R9 = x => Math.round(x * 1e9);
const slotStr = s => [s.tok, R9(s.r), R9(s.r2), s.alt || 0, s.len || 4, s.q || '', s.sl || ''].join(',');
const PROMPTS = ['euphoric progressive pluck', 'dreamy lofi chill keys', 'dark techno rolling acid', 'epic trance 1/16 octave up', 'sad piano slow in F minor',
  'melodic minor pluck', 'happy house 124 bpm 8 bars', '', 'euphoric happy progressive 4 bars', 'bittersweet euphoric progressive 4 bars', 'dark hypnotic progressive',
  'tense cinematic triplet', 'hypnotic sparse bell in A minor', 'busy wild glide supersaw', 'c#m busy', 'a happy song in eb major', 'dreamy lydian 16 bars',
  'phrygian dark 2 bars', 'sus chords house', 'lush jazzy neo soul', 'Euphoric Progressive in F# minor 128 BPM', 'chill mixolydian sunset 8 bars'];
const SEEDS = [1, 128, 4242, 99999];
const AMTS = [0, 0.5, 1];
const out = [];
for (const pr of PROMPTS) for (const sd of SEEDS) for (const amt of AMTS) {
  if (amt > 0 && sd === 4242) continue;
  const sty = amt > 0 ? { amt, prof: A.BUILTIN_PROFILE } : null;
  const g = A.generate(pr, sd, sty);
  out.push(`CASE ${pr}|${sd}|${amt}`);
  out.push(`G genre=${g.genre};moods=${g.moods.join(',')};key=${g.key};scale=${g.scale};bars=${g.bars};colour=${R9(g.colour)};style=${g.style};preset=${g.preset};rate=${g.rate};bpm=${g.bpm};master=${R9(g.master)};oct=${g.oct};reg=${g.reg};glide=${+g.glide};pedal=${+g.pedal};gate=${R9(g.gate)};swing=${R9(g.swing)};groove=${R9(g.groove)};arpSeed=${g.arpSeed};progSeed=${g.progSeed}`);
  out.push('P ' + g.prog.map(slotStr).join(';'));
  const ch = A.resolveProg({ ...g });
  out.push('C ' + ch.map(c => [c.name, c.roman, c.voicing.join('.'), c.bassNote, c.t, c.len].join(',')).join(';'));
  out.push('N ' + A.newProg(g, sd + 5, sty).map(slotStr).join(';'));
  out.push('M ' + A.analyzeMood(ch, g).mood);
  // chord alternatives of slot 0 (all 8)
  out.push('A ' + [0, 1, 2, 3, 4, 5, 6, 7].map(a => { const s = { ...g, prog: g.prog.map((p, j) => j === 0 ? { ...p, alt: a } : p) }; return A.resolveProg(s)[0].name; }).join(','));
}
console.log(out.join('\n'));
