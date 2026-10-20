'use strict';
// ArpEZator LISTEN: hear a few seconds of music (mic or file) and estimate tempo, key, chord loop and feel.
// Everything runs in this browser; no audio leaves the device. Pure functions on a mono Float32Array + sample rate.
const Listen = (() => {
  const PC = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const m12 = x => ((x % 12) + 12) % 12, cl = (x, a, b) => Math.max(a, Math.min(b, x));
  // ---- FFT (radix 2, in place)
  function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) { let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let len = 2; len <= n; len <<= 1) {
      const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a), h = len >> 1;
      for (let i = 0; i < n; i += len) { let cr = 1, ci = 0;
        for (let k = 0; k < h; k++) { const p = i + k, q = p + h, xr = re[q] * cr - im[q] * ci, xi = re[q] * ci + im[q] * cr;
          re[q] = re[p] - xr; im[q] = im[p] - xi; re[p] += xr; im[p] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
    }
  }
  function stft(x, N, hop, cb) {
    const win = new Float32Array(N); for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);
    const re = new Float32Array(N), im = new Float32Array(N), mag = new Float32Array(N / 2); let f = 0;
    for (let s = 0; s + N <= x.length; s += hop, f++) {
      for (let i = 0; i < N; i++) { re[i] = x[s + i] * win[i]; im[i] = 0; }
      fft(re, im); for (let k = 0; k < N / 2; k++) mag[k] = Math.hypot(re[k], im[k]);
      cb(f, mag);
    }
    return f;
  }
  // AudioBuffer -> mono, then ~22 kHz (box filter + decimate)
  function mono(buf) { const n = buf.length, o = new Float32Array(n); for (let c = 0; c < buf.numberOfChannels; c++) { const d = buf.getChannelData(c); for (let i = 0; i < n; i++) o[i] += d[i] / buf.numberOfChannels; } return o; }
  function down(x, sr) {
    const f = Math.max(1, Math.round(sr / 22050)); if (f === 1) return { x, sr };
    const o = new Float32Array(Math.floor(x.length / f)); for (let i = 0; i < o.length; i++) { let s = 0; for (let j = 0; j < f; j++) s += x[i * f + j]; o[i] = s / f; }
    return { x: o, sr: sr / f };
  }
  // ---- chord templates (harmonic-weighted so overtones of chord tones don't read as extra notes)
  const TYPES = { maj: [0, 4, 7], min: [0, 3, 7], sus2: [0, 2, 7], sus4: [0, 5, 7], '7': [0, 4, 7, 10], m7: [0, 3, 7, 10], maj7: [0, 4, 7, 11], add9: [0, 4, 7, 2] };
  const SUF = { maj: '', min: 'm', sus2: 'sus2', sus4: 'sus4', '7': '7', m7: 'm7', maj7: 'maj7', add9: 'add9' };
  const PRIOR = { maj: 0, min: 0, sus2: -0.05, sus4: -0.05, '7': -0.035, m7: -0.035, maj7: -0.04, add9: -0.045 };
  const HARM = [[0, 1], [12, 0.45], [19, 0.3], [24, 0.2]];
  const TPL = []; for (let r = 0; r < 12; r++) for (const t in TYPES) { const v = new Float32Array(12); for (const iv of TYPES[t]) for (const [h, w] of HARM) v[m12(r + iv + h)] += w; TPL.push({ root: r, type: t, iv: TYPES[t], v: norm(v) }); }
  function norm(v) { let s = 0; for (const x of v) s += x * x; s = Math.sqrt(s) || 1; return v.map(x => x / s); }
  const dot = (a, b) => { let s = 0; for (let i = 0; i < 12; i++) s += a[i] * b[i]; return s; };
  // key profiles: Krumhansl-Kessler and Temperley (Kostka-Payne), averaged
  const KK = { major: [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88], minor: [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17] };
  const TP = { major: [0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400], minor: [0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330] };
  function corr(a, b) { const ma = a.reduce((s, x) => s + x, 0) / 12, mb = b.reduce((s, x) => s + x, 0) / 12; let n = 0, da = 0, db = 0; for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; } return n / (Math.sqrt(da * db) || 1); }
  const SC = { major: [0, 2, 4, 5, 7, 9, 11], minor: [0, 2, 3, 5, 7, 8, 10] };

  function analyze(input, srIn) {
    const t0 = (typeof performance !== 'undefined' ? performance.now() : 0);
    let { x, sr } = down(input, srIn);
    if (x.length > sr * 75) { const s = Math.floor((x.length - sr * 60) / 2); x = x.subarray(s, s + sr * 60); }   // long file: the middle minute
    let rms = 0; for (let i = 0; i < x.length; i++) rms += x[i] * x[i]; rms = Math.sqrt(rms / Math.max(1, x.length));
    const dur = x.length / sr;
    if (dur < 3 || rms < 2e-4) return { ok: false, why: dur < 3 ? 'too short' : 'too quiet', conf: 0, dur, rms };
    if (rms < 0.05) { const g = 0.1 / rms; x = x.map(v => v * g); }
    // ---- onset strength (log spectral flux), 1024 / 256
    const H1 = 256, N1 = 1024, fr = sr / H1, kMax = Math.min(N1 / 2, Math.round(9000 / sr * N1)), kLow = Math.round(200 / sr * N1);
    let prev = new Float32Array(N1 / 2); const flux = [], low = [];
    stft(x, N1, H1, (f, mag) => { let s = 0, l = 0; for (let k = 1; k < kMax; k++) { const v = Math.log(1 + 100 * mag[k]), d = v - prev[k]; if (d > 0) { s += d; if (k < kLow) l += d; } prev[k] = v; } flux.push(s); low.push(l); if (f === 0) { flux[0] = 0; low[0] = 0; } });
    const nO = flux.length, o = new Float32Array(nO), W = Math.round(fr * 0.4);
    for (let i = 0; i < nO; i++) { let s = 0, c = 0; for (let j = Math.max(0, i - W); j <= Math.min(nO - 1, i + W); j++) { s += flux[j]; c++; } o[i] = Math.max(0, flux[i] - s / c); }
    const oMean = o.reduce((s, v) => s + v, 0) / nO || 1; for (let i = 0; i < nO; i++) o[i] /= oMean;
    // ---- tempo: autocorrelation + comb over multiples, log-normal prior around 120
    const maxLag = Math.min(nO - 2, Math.ceil(60 * fr / 70 * 4) + 2), ac = new Float32Array(maxLag + 1);
    for (let L = 0; L <= maxLag; L++) { let s = 0; for (let i = 0; i + L < nO; i++) s += (o[i] - 1) * (o[i + L] - 1); ac[L] = s / (nO - L); }
    const a0 = ac[0] || 1; for (let L = 0; L <= maxLag; L++) ac[L] /= a0;
    const acAt = l => { if (l >= maxLag) return 0; const i = Math.floor(l), f = l - i; return ac[i] * (1 - f) + ac[i + 1] * f; };
    const acPk = l => Math.max(acAt(l - 1), acAt(l), acAt(l + 1));
    const comb = b => { const L = 60 * fr / b; let s = 0, w = 0; for (let k = 1; k <= 4; k++) { if (k * L + 1 >= maxLag) break; s += acPk(k * L) / k; w += 1 / k; } return w ? s / w : 0; };
    const prior = b => Math.exp(-0.5 * (Math.log2(b / 120) / 0.9) ** 2);
    let best = 0, bScore = -1; const scores = [];
    for (let b = 70; b <= 180; b += 0.5) { const s = comb(b) * (0.6 + 0.4 * prior(b)); scores.push(s); if (s > bScore) { bScore = s; best = b; } }
    // octave check: a candidate at half / double tempo inside the range wins if it is nearly as strong and closer to the prior
    for (const alt of [best / 2, best * 2, best * 2 / 3, best * 1.5]) if (alt >= 70 && alt <= 180) { const s = comb(alt) * (0.6 + 0.4 * prior(alt)); if (s > bScore * 0.92 && prior(alt) > prior(best) * 1.15 && (alt === best / 2 || alt === best * 2)) { best = alt; bScore = s; } }
    { const fine = b => { const L = 60 * fr / b; let s = 0; for (let k = 1; k <= 8 && k * L + 1 < maxLag; k++) s += acAt(k * L); return s; };   // fine tempo: sharp (unsmoothed) comb over all multiples
      let bb = best, sb = fine(best); for (let b = best - 1.5; b <= best + 1.501; b += 0.05) { const s = fine(b); if (s > sb) { sb = s; bb = b; } } best = bb; }
    const sorted = scores.slice().sort((a, b) => a - b), med = sorted[sorted.length >> 1];
    const tempoConf = cl((bScore - med - 0.15) / 0.4, 0, 1) * cl(1 - 3 * Math.max(0, med), 0, 1);
    let bpm = Math.round(best * 10) / 10;
    // ---- beats: best phase of a comb at the beat period
    const P = 60 * fr / best; let ph = 0, phS = -1;
    for (let p = 0; p < P; p++) { let s = 0; for (let t = p; t < nO; t += P) { const i = Math.round(t); s += (o[i] || 0) + 0.5 * ((o[i - 1] || 0) + (o[i + 1] || 0)); } if (s > phS) { phS = s; ph = p; } }
    const fT = i => (i * H1 + N1 / 2) / sr, beats = []; for (let t = ph; t < nO; t += P) beats.push(fT(t));
    // ---- chroma, 4096 / 1024, semitones 45..96; the 3rd / 5th harmonics of lower notes are subtracted so overtones don't read as notes
    const N2 = 4096, H2 = 1024, nb = N2 / 2, bS = new Int16Array(nb).fill(-1), bW = new Float32Array(nb);
    for (let k = 1; k < nb; k++) { const f = k * sr / N2, m = 69 + 12 * Math.log2(f / 440), s = Math.round(m); if (s >= 45 && s <= 100) { bS[k] = s; bW[k] = Math.max(0, 1 - 2 * Math.abs(m - s)); } }
    const chroma = [], cT = [], cE = []; let cent = 0, centW = 0;
    stft(x, N2, H2, (f, mag) => {
      const E = new Float32Array(101); let tot = 0, cs = 0;
      for (let k = 1; k < nb; k++) { const v = mag[k]; tot += v; cs += v * k; if (bS[k] >= 0) E[bS[k]] += v * bW[k]; }
      cent += cs * sr / N2; centW += tot;
      const c = new Float32Array(12); let e = 0;
      for (let s = 45; s <= 100; s++) { const v = Math.max(0, E[s] - 0.33 * (E[s - 19] || 0) - 0.2 * (E[s - 28] || 0)); E[s] = v; if (s <= 96) { c[s % 12] += v; e += v; } }
      chroma.push(c); cT.push((f * H2 + N2 / 2) / sr); cE.push(e);
    });
    const centroid = centW ? cent / centW : 1500;
    const eMax = Math.max(...cE, 1e-9);
    const frameC = chroma.map((c, i) => { const m = Math.max(...c) || 1, w = Math.sqrt(cE[i] / eMax); return c.map(v => Math.sqrt(v / m) * w); });
    // ---- key: profile correlation over the whole clip
    const tot = new Float32Array(12); frameC.forEach(c => { for (let i = 0; i < 12; i++) tot[i] += c[i]; });
    const keys = []; for (const sc of ['major', 'minor']) for (let k = 0; k < 12; k++) { const rot = i => tot[m12(i + k)]; const a = Array.from({ length: 12 }, (_, i) => rot(i)); keys.push({ key: k, scale: sc, r: (corr(a, KK[sc]) + corr(a, TP[sc])) / 2 }); }
    keys.sort((a, b) => b.r - a.r);
    let K = keys[0]; const rel = k => k.scale === 'major' ? { key: m12(k.key + 9), scale: 'minor' } : { key: m12(k.key + 3), scale: 'major' };
    const other = keys.find(k => !(k.key === K.key && k.scale === K.scale) && !(k.key === rel(K).key && k.scale === rel(K).scale));
    let keyConf = cl((K.r - 0.35) / 0.4, 0, 1) * cl((K.r - other.r) / 0.12, 0.15, 1);
    // ---- beat-synchronous chroma
    const bc = []; for (let i = 0; i + 1 < beats.length; i++) { const v = new Float32Array(12); let n = 0; for (let j = 0; j < cT.length; j++) if (cT[j] >= beats[i] && cT[j] < beats[i + 1]) { for (let q = 0; q < 12; q++) v[q] += frameC[j][q]; n++; } bc.push(n ? v : null); }
    // downbeat: the beat offset where the harmony changes most (plus kick energy as a tie-break)
    const nov = bc.map((v, i) => i && v && bc[i - 1] ? 1 - dot(norm(v), norm(bc[i - 1])) : 0);
    const lowAt = t => { const i = Math.round((t * sr - N1 / 2) / H1); return low[i] || 0; };
    let off = 0, offS = -1e9; for (let ofs = 0; ofs < 4; ofs++) { let s = 0, n = 0, kk = 0; for (let i = 0; i < nov.length; i++) { if (i % 4 === ofs) { s += nov[i]; n++; kk += lowAt(beats[i]); } else s -= nov[i] / 3; } const v = s / Math.max(1, n) + 1e-4 * kk; if (v > offS) { offS = v; off = ofs; } }
    // bars (a partial bar counts if it holds at least 2 beats)
    const bars = []; for (let s = off - 4; s < bc.length; s += 4) { const v = new Float32Array(12); let n = 0; for (let i = Math.max(0, s); i < Math.min(bc.length, s + 4); i++) if (bc[i]) { for (let q = 0; q < 12; q++) v[q] += bc[i][q]; n++; } if (n >= 2) bars.push(norm(v)); }
    // template scores + key-aware prior, then Viterbi over bars
    const inKey = (r, iv, k) => iv.every(x => SC[k.scale].includes(m12(r + x - k.key)));
    const emis = bars.map(v => TPL.map(T => dot(v, T.v) + PRIOR[T.type] + (inKey(T.root, T.iv, K) ? 0.03 : 0)));
    let path = []; if (bars.length) {
      const nS = TPL.length, stay = 0.04, sc = 25; let dp = emis[0].map(e => e * sc); const bk = [];
      for (let b = 1; b < bars.length; b++) { const nd = new Array(nS), bb = new Array(nS); let mx = -1e9, mi = 0; dp.forEach((v, i) => { if (v > mx) { mx = v; mi = i; } });
        for (let s = 0; s < nS; s++) { const st = dp[s] + stay * sc; if (st >= mx) { nd[s] = st; bb[s] = s; } else { nd[s] = mx; bb[s] = mi; } nd[s] += emis[b][s] * sc; } dp = nd; bk.push(bb); }
      let s = dp.indexOf(Math.max(...dp)); path = [s]; for (let b = bk.length - 1; b >= 0; b--) { s = bk[b][s]; path.unshift(s); }
    }
    const barCh = path.map(i => TPL[i]), chordConf = barCh.length ? cl((path.reduce((a, s, b) => a + emis[b][s], 0) / path.length - 0.6) / 0.3, 0, 1) : 0;
    // major vs relative minor: which tonic chord shows up more (first / last bar count extra)
    const isT = (c, k) => c && c.root === k.key && (k.scale === 'minor' ? c.iv[1] === 3 : c.iv[1] === 4);
    const tScore = k => barCh.reduce((a, c, i) => a + (isT(c, k) ? 1 + (i === 0 || i === barCh.length - 1 ? 0.5 : 0) : 0), 0);
    // key refinement with the chords: profile fit + how many bars are diatonic + how many sit on the tonic chord
    if (barCh.length) { const kS = k => k.r + 0.15 * barCh.filter(c => inKey(c.root, c.iv.slice(0, 3), k)).length / barCh.length * 2 + 0.12 * tScore(k) / barCh.length;
      const top = keys.slice(0, 6).map(k => ({ k, v: kS(k) })).sort((a, b) => b.v - a.v); K = top[0].k;
      const o2 = top.find(t => !(t.k.key === K.key && t.k.scale === K.scale) && !(t.k.key === rel(K).key && t.k.scale === rel(K).scale));
      keyConf = cl((K.r - 0.35) / 0.4, 0, 1) * cl(o2 ? (top[0].v - o2.v) / 0.06 : 1, 0.15, 1); }
    { const R2 = rel(K); if (tScore(R2) > tScore(K) + 0.6) K = { ...R2, r: K.r }; }
    // ---- the loop: smallest period (2, 4, 8 bars) that repeats, the most repeated window, started on the tonic if it has one
    const ids = barCh.map(c => c.root * 16 + Object.keys(TYPES).indexOf(c.type)), B = ids.length;
    const per = L => { let m = 0, n = 0; for (let i = 0; i + L < B; i++) { n++; if (ids[i] === ids[i + L]) m++; } return n ? m / n : -1; };
    let L = B >= 4 ? 4 : Math.max(1, B); for (const c of [2, 4, 8]) { const p = per(c); if (c <= B && p >= 0.75) { L = c; break; } if (c === 8 && B >= 12 && p > per(4) + 0.2) L = 8; }
    if (L === 1 && B >= 2) L = 2;
    let ws = 0, wS = -1; for (let s = 0; s + L <= B; s++) { let a = 0; for (let i = 0; i < B; i++) if (ids[i] === ids[s + (((i - s) % L) + L) % L]) a++; if (a > wS) { wS = a; ws = s; } }
    let loop = barCh.slice(ws, ws + L); const ti = loop.findIndex(c => isT(c, K)); if (ti > 0) loop = loop.slice(ti).concat(loop.slice(0, ti));
    // ---- feel: onset density, triplets, syncopation, brightness
    let nOn = 0; const thr = 1.5; for (let i = 2; i < nO - 2; i++) if (o[i] > thr && o[i] >= o[i - 1] && o[i] >= o[i + 1] && o[i] >= o[i - 2] && o[i] >= o[i + 2]) nOn++;
    const density = nOn / Math.max(1, dur * best / 60);
    const trip = acPk(P / 3) > 0.15 && acPk(P / 3) > acPk(P / 4) * 1.35 && acPk(P / 3) > acPk(P / 2) * 0.9;
    let on = 0, offE = 0; for (let t = ph; t + P < nO; t += P) for (let q = 0; q < 4; q++) { const i = Math.round(t + q * P / 4), v = Math.max(o[i] || 0, o[i - 1] || 0, o[i + 1] || 0); if (q === 1 || q === 3) offE += v; else on += v; }
    const sync = offE / Math.max(1e-6, on + offE);
    const rate = trip ? '1/16T' : density < 1.4 ? '1/8' : '1/16';
    const groove = Math.round(cl((sync - 0.22) * 2.2, 0, 0.8) * 100) / 100;
    const oct = centroid < 1100 ? 1 : centroid > 2600 ? 3 : 2;
    const preset = centroid < 1100 ? 'keys' : centroid > 2600 ? 'supersaw' : 'pluck';
    if (bpm < 70) bpm *= 2; if (bpm > 180) bpm /= 2;
    const conf = Math.round((tempoConf * 0.35 + keyConf * 0.35 + chordConf * 0.3) * 100) / 100;
    const name = c => PC[c.root] + SUF[c.type];
    return { ok: true, bpm, bpmInt: Math.round(bpm), tempoConf, key: K.key, scale: K.scale, keyConf, keyR: K.r, chroma: Array.from(tot, v => v / (Math.max(...tot) || 1)), chords: loop.map(c => ({ root: c.root, type: c.type, iv: c.iv.slice(), name: name(c) })), bars: barCh.map(name), chordConf,
      dbg: { bScore, med, keys: keys.slice(0, 6).map(k => k.key + k.scale[1] + k.r.toFixed(3)) }, density, triplet: trip, sync, rate, groove, centroid, oct, preset, conf, dur, ms: Math.round((typeof performance !== 'undefined' ? performance.now() : 0) - t0) };
  }

  // ---- live mic capture: returns { analyser, stop() -> Float32Array, sr }
  async function startMic(ctx) {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 1 } });
    const src = ctx.createMediaStreamSource(stream), an = ctx.createAnalyser(); an.fftSize = 4096; an.smoothingTimeConstant = 0.6;
    const proc = ctx.createScriptProcessor(4096, 1, 1), mute = ctx.createGain(); mute.gain.value = 0; const chunks = []; let n = 0;
    proc.onaudioprocess = e => { const d = e.inputBuffer.getChannelData(0); chunks.push(new Float32Array(d)); n += d.length; };
    src.connect(an); src.connect(proc); proc.connect(mute); mute.connect(ctx.destination);
    return { analyser: an, sr: ctx.sampleRate, get seconds() { return n / ctx.sampleRate; },
      stop() { try { proc.onaudioprocess = null; src.disconnect(); proc.disconnect(); mute.disconnect(); } catch (e) {} stream.getTracks().forEach(t => t.stop());
        const out = new Float32Array(n); let p = 0; for (const c of chunks) { out.set(c, p); p += c.length; } return out; } };
  }
  // live 12-bin chroma from an AnalyserNode (for the ring)
  function liveChroma(an, sr, buf) {
    an.getFloatFrequencyData(buf); const c = new Float32Array(12), N = an.fftSize;
    for (let k = 4; k < buf.length; k++) { const f = k * sr / N; if (f < 100 || f > 2100) continue; const m = 69 + 12 * Math.log2(f / 440); c[m12(Math.round(m))] += Math.pow(10, buf[k] / 20); }
    const mx = Math.max(...c) || 1; return c.map(v => v / mx);
  }
  // test audio: drums (kick on beats, snare on 2 + 4, hats on 8ths) + a saw pad playing one chord per bar (or plain noise)
  async function synth({ bpm = 120, prog = [], dur = 12, sr = 44100, noise = false, swing = 0, lead = 0 }) {
    const OC = window.OfflineAudioContext || window.webkitOfflineAudioContext, c = new OC(1, Math.round(sr * dur), sr), nl = noise ? Math.round(sr * dur) : sr, nb = c.createBuffer(1, nl, sr), nd = nb.getChannelData(0);
    let z = 12345; for (let i = 0; i < nl; i++) { z = (z * 1103515245 + 12345) >>> 0; nd[i] = z / 2147483648 - 1; }
    const out = c.createGain(); out.gain.value = 0.5; out.connect(c.destination);
    if (noise) { const s = c.createBufferSource(); s.buffer = nb; s.loop = true; s.connect(out); s.start(0); return c.startRendering(); }
    const beat = 60 / bpm, hit = (t, f0, f1, d, g) => { t -= lead; if (t < 0) return; const o = c.createOscillator(), a = c.createGain(); o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(f1, t + d); a.gain.setValueAtTime(g, t); a.gain.exponentialRampToValueAtTime(0.001, t + d); o.connect(a); a.connect(out); o.start(t); o.stop(t + d + 0.01); };
    const nz = (t, d, g, hp) => { t -= lead; if (t < 0) return; const s = c.createBufferSource(), f = c.createBiquadFilter(), a = c.createGain(); s.buffer = nb; f.type = 'highpass'; f.frequency.value = hp; a.gain.setValueAtTime(g, t); a.gain.exponentialRampToValueAtTime(0.001, t + d); s.connect(f); f.connect(a); a.connect(out); s.start(t, Math.random() * 0.5); s.stop(t + d + 0.01); };
    for (let i = 0; i * beat < dur; i++) { const t = i * beat; hit(t, 150, 45, 0.25, 0.9); if (i % 2) nz(t, 0.15, 0.35, 1500); nz(t + beat / 2 + swing * beat / 4, 0.05, 0.15, 7000); nz(t, 0.04, 0.1, 7000); }
    const bar = beat * 4; for (let b = 0; b * bar < dur + lead; b++) { const ch = prog[b % prog.length], t = Math.max(0, b * bar - lead); if (b * bar + bar - lead <= 0.1) continue;
      for (const m of ch) { const o = c.createOscillator(), f = c.createBiquadFilter(), a = c.createGain(); o.type = 'sawtooth'; o.frequency.value = 440 * Math.pow(2, (m - 69) / 12); f.type = 'lowpass'; f.frequency.value = 1800;
        a.gain.setValueAtTime(0, t); a.gain.linearRampToValueAtTime(0.07, t + 0.05); const e = b * bar + bar - lead; a.gain.setValueAtTime(0.07, e - 0.08); a.gain.linearRampToValueAtTime(0, e - 0.01); o.connect(f); f.connect(a); a.connect(out); o.start(t); o.stop(e); } }
    return c.startRendering();
  }
  return { analyze, mono, startMic, liveChroma, synth, PC, TYPES, SUF };
})();
