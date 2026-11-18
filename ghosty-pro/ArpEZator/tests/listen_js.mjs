// Runs the web app's listen.js analyze() on raw float32 mono files (44.1 kHz) written by the C++ tests.
// usage: node listen_js.mjs /path/to/listen.js file1.f32 [file2.f32 ...]
import { readFileSync } from 'node:fs';
const src = readFileSync(process.argv[2], 'utf8');
const Listen = new Function(src + '; return Listen;')();
for (const f of process.argv.slice(3)) {
  const b = readFileSync(f), x = new Float32Array(b.buffer, b.byteOffset, b.length / 4);
  const r = Listen.analyze(x, 44100);
  console.log(`${f.split('/').pop()}: ok=${r.ok} bpm=${r.bpm} key=${r.key} ${r.scale} loop=${r.chords ? r.chords.map(c => c.name).join(' ') : '-'} bars=${r.bars ? r.bars.join(' ') : '-'} rate=${r.rate} groove=${r.groove} conf=${r.conf} why=${r.why || ''}`);
}
