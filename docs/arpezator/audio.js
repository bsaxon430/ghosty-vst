'use strict';
// ArpEZator sound (Web Audio): 5 lead presets with per-note glide (one voice slides through a chain of GLIDE-linked
// notes), a soft chord pad, an offbeat bass, a soft kick that pumps the rest, plus tone filter, ping-pong delay and
// reverb. The same engine renders WAVs offline (OfflineAudioContext).

const mtof = p => 440 * Math.pow(2, (p - 69) / 12);
// osc: [type, cents, semitones, gain]
const LEADS = {
  pluck: { osc: [['sawtooth', -9, 0, 0.5], ['sawtooth', 9, 0, 0.5], ['square', 0, -12, 0.2]], cut: 420, env: 5200, q: 3.5, fdec: 0.17, att: 0.002, dec: 0.24, sus: 0.14, rel: 0.14, gain: 0.34 },
  supersaw: { osc: [['sawtooth', -26, 0, 0.3], ['sawtooth', -12, 0, 0.3], ['sawtooth', 0, 0, 0.3], ['sawtooth', 12, 0, 0.3], ['sawtooth', 26, 0, 0.3], ['sawtooth', 0, -12, 0.16]], cut: 1500, env: 4200, q: 1.1, fdec: 0.32, att: 0.006, dec: 0.45, sus: 0.6, rel: 0.22, gain: 0.2 },
  keys: { osc: [['triangle', 0, 0, 0.75], ['sine', 3, 12, 0.2], ['sine', -3, 0, 0.35]], cut: 2000, env: 2800, q: 0.7, fdec: 0.45, att: 0.003, dec: 0.9, sus: 0.2, rel: 0.3, gain: 0.6 },
  bell: { fm: 3.5, idx: 2.6, osc: [['sine', 0, 0, 0.8], ['sine', 2, 12, 0.1]], cut: 9000, env: 0, q: 0.5, fdec: 0.3, att: 0.002, dec: 1.4, sus: 0, rel: 0.5, gain: 0.46 },
  acid: { osc: [['sawtooth', 0, 0, 1]], cut: 240, env: 3000, q: 12, fdec: 0.2, att: 0.003, dec: 0.35, sus: 0.55, rel: 0.07, gain: 0.25 },
};
const PRESET_LABEL = { pluck: 'PLUCK', supersaw: 'SUPERSAW', keys: 'SOFT KEYS', bell: 'BELL', acid: 'ACID' };

function makeIR(ctx, secs) {
  const sr = ctx.sampleRate, n = Math.floor(sr * secs), b = ctx.createBuffer(2, n, sr), R = ArpCore.rng(77);
  for (let ch = 0; ch < 2; ch++) { const d = b.getChannelData(ch); let lp = 0;
    for (let i = 0; i < n; i++) { const t = i / sr, a = Math.min(1, 0.002 + t * 1.5) * 0.25 + 0.0; const k = 0.75 - 0.6 * (i / n); lp += k * ((R() * 2 - 1) - lp); d[i] = lp * Math.exp(-t * 2.4) * (t < 0.012 ? t / 0.012 : 1) * (1 + a); } }
  return b;
}

class Engine {
  constructor(ctx, out) {
    this.ctx = ctx; const c = ctx;
    this.master = c.createGain(); this.master.gain.value = 0.85;
    this.comp = c.createDynamicsCompressor(); this.comp.threshold.value = -14; this.comp.knee.value = 8; this.comp.ratio.value = 3.5; this.comp.attack.value = 0.004; this.comp.release.value = 0.18;
    this.master.connect(this.comp); this.comp.connect(out);
    this.pumpG = c.createGain(); this.pumpG.connect(this.master);
    this.arpBus = c.createGain();
    this.tone = c.createBiquadFilter(); this.tone.type = 'lowpass'; this.tone.Q.value = 0.6;
    this.arpOut = c.createGain(); this.arpBus.connect(this.tone); this.tone.connect(this.arpOut); this.arpOut.connect(this.pumpG);
    this.chordBus = c.createGain(); this.chordBus.connect(this.pumpG);
    this.bassBus = c.createGain(); this.bassBus.connect(this.pumpG);
    this.kickBus = c.createGain(); this.kickBus.connect(this.master);
    // ping-pong delay with a darkening feedback loop
    this.dSend = c.createGain(); this.dL = c.createDelay(2); this.dR = c.createDelay(2); this.fb = c.createGain(); this.fbF = c.createBiquadFilter(); this.fbF.type = 'lowpass'; this.fbF.frequency.value = 3200;
    this.pL = c.createStereoPanner(); this.pL.pan.value = -0.65; this.pR = c.createStereoPanner(); this.pR.pan.value = 0.65; this.dRet = c.createGain(); this.dRet.gain.value = 0.9;
    this.arpOut.connect(this.dSend); this.dSend.connect(this.dL); this.dL.connect(this.dR); this.dR.connect(this.fbF); this.fbF.connect(this.fb); this.fb.connect(this.dL);
    this.dL.connect(this.pL); this.dR.connect(this.pR); this.pL.connect(this.dRet); this.pR.connect(this.dRet); this.dRet.connect(this.pumpG);
    // reverb
    this.rSend = c.createGain(); this.conv = c.createConvolver(); this.conv.buffer = makeIR(c, 2.6); this.rRet = c.createGain(); this.rRet.gain.value = 0.8;
    this.arpOut.connect(this.rSend); this.chordRev = c.createGain(); this.chordBus.connect(this.chordRev); this.chordRev.connect(this.rSend);
    this.dRet.connect(this.rSend); this.rSend.connect(this.conv); this.conv.connect(this.rRet); this.rRet.connect(this.pumpG);
    this.preset = 'pluck'; this.S = null;
  }
  apply(S) {
    this.S = S; const t = this.ctx.currentTime, fx = S.fx, mx = S.mix, set = (p, v) => p.setTargetAtTime(v, t, 0.03);
    this.preset = S.preset;
    set(this.arpOut.gain, mx.arp); set(this.chordBus.gain, mx.chords * 0.9); set(this.bassBus.gain, mx.bass); set(this.kickBus.gain, mx.kick);
    set(this.tone.frequency, 250 * Math.pow(2, fx.tone * 6.3));
    const beat = 60 / S.bpm; set(this.dL.delayTime, beat * 0.75); set(this.dR.delayTime, beat * 0.75);
    set(this.dSend.gain, fx.delay * 0.55); set(this.fb.gain, 0.25 + fx.delay * 0.3);
    set(this.rSend.gain, fx.reverb * 0.9); set(this.chordRev.gain, 0.6);
  }
  // one lead voice through a chain of notes [{time, end, p, v, gs, cv}] (gs = when the glide into this note starts)
  lead(chain) {
    const c = this.ctx, P = LEADS[this.preset] || LEADS.pluck, n0 = chain[0], last = chain[chain.length - 1];
    const T0 = n0.time, Tend = Math.max(last.end, last.time + 0.012), stopAt = Tend + P.rel * 5 + 0.05, f0 = mtof(n0.p);
    const vca = c.createGain(); vca.gain.value = 0; vca.connect(this.arpBus);
    const flt = c.createBiquadFilter(); flt.type = 'lowpass'; flt.Q.value = P.q; flt.connect(vca);
    let det = null;
    if (chain.length > 1) {
      det = c.createConstantSource(); det.offset.value = 0; det.start(T0); det.stop(stopAt);
      for (let i = 1; i < chain.length; i++) {
        const a = chain[i - 1], b = chain[i], ca = (a.p - n0.p) * 100, cb = (b.p - n0.p) * 100;
        const gs = Math.max(a.time + 0.004, Math.min(b.gs, b.time - 0.004)), dur = Math.max(0.003, b.time - gs);
        if (ca === cb) continue;
        const arr = new Float32Array(24), k = Math.pow(2, -(b.cv || 0) * 1.6);
        for (let j = 0; j < 24; j++) arr[j] = ca + (cb - ca) * Math.pow(j / 23, k);
        try { det.offset.setValueCurveAtTime(arr, gs, dur); } catch (e) { det.offset.setValueAtTime(cb, b.time); }
      }
    }
    const oscs = [];
    let carrier = null;
    for (const [type, cents, semis, g] of P.osc) {
      const o = c.createOscillator(); o.type = type; o.frequency.value = f0 * Math.pow(2, semis / 12); o.detune.value = cents;
      if (det) det.connect(o.detune);
      const og = c.createGain(); og.gain.value = g; o.connect(og); og.connect(flt); oscs.push(o); if (!carrier) carrier = o;
    }
    if (P.fm) {
      const mo = c.createOscillator(); mo.frequency.value = f0 * P.fm; if (det) det.connect(mo.detune);
      const mg = c.createGain(); mg.gain.setValueAtTime(f0 * P.idx * n0.v, T0); mg.gain.setTargetAtTime(f0 * 0.12, T0, 0.22);
      mo.connect(mg); mg.connect(carrier.frequency); oscs.push(mo);
    }
    const vel = v => Math.pow(v, 1.3);
    const peak = P.gain * vel(n0.v), envAmt = (this.preset === 'acid' && n0.v > 0.8 ? 1.7 : 1) * P.env * (0.45 + 0.55 * n0.v);
    vca.gain.setValueAtTime(0, T0); vca.gain.linearRampToValueAtTime(peak, T0 + P.att); vca.gain.setTargetAtTime(peak * P.sus, T0 + P.att, P.dec / 3);
    flt.frequency.setValueAtTime(Math.min(18000, P.cut + envAmt), T0); flt.frequency.setTargetAtTime(P.cut, T0 + 0.001, P.fdec / 3);
    for (let i = 1; i < chain.length; i++) {   // legato notes: soft re-accent, no new attack
      const b = chain[i], pk = P.gain * vel(b.v), t2 = Math.min(b.time + 0.03, Tend - 0.002);
      vca.gain.setTargetAtTime(pk * (P.sus + (1 - P.sus) * 0.45), b.time, 0.008); if (t2 > b.time) vca.gain.setTargetAtTime(pk * P.sus, t2, P.dec / 3);
      flt.frequency.setTargetAtTime(Math.min(18000, P.cut + envAmt * 0.5 * b.v), b.time, 0.005); if (t2 > b.time) flt.frequency.setTargetAtTime(P.cut, t2, P.fdec / 3);
    }
    vca.gain.setTargetAtTime(0, Tend, P.rel / 3);
    for (const o of oscs) { o.start(T0); o.stop(stopAt); }
  }
  chord(pitches, T, dur, v) {
    const c = this.ctx, vca = c.createGain(), flt = c.createBiquadFilter(); flt.type = 'lowpass'; flt.frequency.value = 1100; flt.Q.value = 0.4;
    flt.connect(vca); vca.connect(this.chordBus);
    const end = T + dur, stop = end + 1.2;
    vca.gain.setValueAtTime(0, T); vca.gain.linearRampToValueAtTime(v, T + 0.12); vca.gain.setTargetAtTime(v * 0.75, T + 0.12, 0.4); vca.gain.setTargetAtTime(0, end, 0.18);
    for (const p of pitches) for (const cents of [-11, 11]) {
      const o = c.createOscillator(); o.type = 'sawtooth'; o.frequency.value = mtof(p); o.detune.value = cents;
      const g = c.createGain(); g.gain.value = 0.045; o.connect(g); g.connect(flt); o.start(T); o.stop(stop);
    }
  }
  bass(p, T, dur, v) {
    const c = this.ctx, vca = c.createGain(), flt = c.createBiquadFilter(); flt.type = 'lowpass'; flt.frequency.value = 420; flt.Q.value = 1;
    flt.connect(vca); vca.connect(this.bassBus);
    const s = c.createOscillator(); s.type = 'sine'; s.frequency.value = mtof(p); const w = c.createOscillator(); w.type = 'sawtooth'; w.frequency.value = mtof(p);
    const sg = c.createGain(); sg.gain.value = 0.8; const wg = c.createGain(); wg.gain.value = 0.28; s.connect(sg); w.connect(wg); sg.connect(flt); wg.connect(flt);
    vca.gain.setValueAtTime(0, T); vca.gain.linearRampToValueAtTime(0.5 * v, T + 0.006); vca.gain.setTargetAtTime(0.32 * v, T + 0.01, 0.08); vca.gain.setTargetAtTime(0, T + dur, 0.025);
    for (const o of [s, w]) { o.start(T); o.stop(T + dur + 0.2); }
  }
  kick(T, beat, pump) {
    const c = this.ctx;
    if (this.S.mix.kick > 0.001) {
      const o = c.createOscillator(), g = c.createGain(); o.type = 'sine'; o.frequency.setValueAtTime(150, T); o.frequency.exponentialRampToValueAtTime(46, T + 0.09);
      g.gain.setValueAtTime(0.0001, T); g.gain.linearRampToValueAtTime(0.9, T + 0.002); g.gain.exponentialRampToValueAtTime(0.001, T + 0.38); o.connect(g); g.connect(this.kickBus); o.start(T); o.stop(T + 0.4);
    }
    if (pump > 0.001) { const p = this.pumpG.gain; p.setValueAtTime(1 - 0.72 * pump, T); p.setTargetAtTime(1, T + 0.015, beat * 0.17); }
  }
}

// ---------------------------------------------------------------- plan (events by tick) + scheduling
function buildPlan(S, chords, notes) {
  const L = chords.length ? chords[chords.length - 1].tEnd : ArpCore.BAR * 4, at = new Array(L);
  const push = (t, e) => { if (t < 0 || t >= L) return; (at[t] || (at[t] = [])).push(e); };
  for (let i = 0; i < notes.length;) {
    let j = i; while (j + 1 < notes.length && notes[j].l.type === 'glide' && notes[j + 1].t > notes[j].t) j++;
    const ch = []; for (let k = i; k <= j; k++) ch.push({ t: notes[k].t, d: notes[k].d, p: notes[k].p, v: ArpCore.playVel(notes, k), w: k > i ? notes[k - 1].l.w : 0, c: k > i ? (notes[k - 1].l.c || 0) : 0, idx: k });
    push(notes[i].t, { k: 'arp', ch }); i = j + 1;
  }
  const PPQ = ArpCore.PPQ;
  for (const c of chords) {
    push(c.t, { k: 'chord', c });
    if (S.bpm < 100) push(c.t, { k: 'bass', p: c.bassNote, d: c.len * PPQ - 10 });
    else for (let b = 0; b < c.len; b++) push(c.t + b * PPQ + PPQ / 2, { k: 'bass', p: c.bassNote, d: 40 });
  }
  for (let t = 0; t < L; t += PPQ) push(t, { k: 'beat' });
  return { L, at };
}
const swingOff = (S, t) => { if (!S.swing || S.rate === '1/16T') return 0; const g = S.rate === '1/8' ? 48 : 24; return (t % (2 * g)) >= g ? S.swing * g * 0.6 : 0; };
function schedTick(eng, S, plan, tick, time, td, onNote) {
  const evs = plan.at[tick]; if (!evs) return;
  for (const e of evs) {
    if (e.k === 'arp') {
      if (S.mix.arp <= 0.001) continue;
      const ch = e.ch.map(n => ({ ...n, time: time + (n.t - tick + swingOff(S, n.t)) * td }));
      ch.forEach((n, i) => { n.end = n.time + n.d * td; n.cv = n.c; n.gs = i ? ch[i - 1].time + n.w * (n.time - ch[i - 1].time) : n.time; });
      eng.lead(ch); if (onNote) for (const n of ch) onNote(n.idx, n.time, n.end);
    } else if (e.k === 'chord') { if (S.mix.chords > 0.001) eng.chord(e.c.voicing, time, e.c.len * ArpCore.PPQ * td, 0.9); }
    else if (e.k === 'bass') { if (S.mix.bass > 0.001) eng.bass(e.p, time + swingOff(S, tick) * td, e.d * td, 1); }
    else if (e.k === 'beat') eng.kick(time, ArpCore.PPQ * td, S.fx.pump);
  }
}
// render N loops offline -> AudioBuffer
async function renderOffline(S, chords, notes, loops = 1, tail = 1.6) {
  const plan = buildPlan(S, chords, notes), td = 60 / S.bpm / ArpCore.PPQ, len = Math.ceil((plan.L * loops * td + tail) * 44100);
  const off = new OfflineAudioContext(2, len, 44100), eng = new Engine(off, off.destination); eng.apply(S);
  for (let k = 0; k < loops; k++) for (let t = 0; t < plan.L; t++) schedTick(eng, S, plan, t, 0.02 + (k * plan.L + t) * td, td);
  return off.startRendering();
}
function wavBlob(buf) {
  const ch = buf.numberOfChannels, n = buf.length, out = new DataView(new ArrayBuffer(44 + n * ch * 2)); let o = 0;
  const w = s => { for (let i = 0; i < s.length; i++) out.setUint8(o++, s.charCodeAt(i)); }, u32 = v => { out.setUint32(o, v, true); o += 4; }, u16 = v => { out.setUint16(o, v, true); o += 2; };
  w('RIFF'); u32(36 + n * ch * 2); w('WAVE'); w('fmt '); u32(16); u16(1); u16(ch); u32(buf.sampleRate); u32(buf.sampleRate * ch * 2); u16(ch * 2); u16(16); w('data'); u32(n * ch * 2);
  const data = []; for (let c = 0; c < ch; c++) data.push(buf.getChannelData(c));
  for (let i = 0; i < n; i++) for (let c = 0; c < ch; c++) { const v = Math.max(-1, Math.min(1, data[c][i])); out.setInt16(o, v * 32767, true); o += 2; }
  return new Blob([out], { type: 'audio/wav' });
}
