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

// ---------------------------------------------------------------- FX rack (arp, optionally chords): DRIVE > FILTER SWEEP > TAPE ECHO > CRYSTAL >
// SHIMMER > WIDE. Every block is plain Web Audio nodes (no worklets), off = dry path only. The offline WAV render builds the same rack.
const ECHO_SYNC = { '1/8': 0.5, '1/8d': 0.75, '1/4': 1 }, LFO_SYNC = { '1/4': 1, '1/2': 2, '1 bar': 4, '2 bars': 8, '4 bars': 16 };
const fx2Defaults = () => ({ chords: false, scene: 'Clean', drive: { on: 0, amt: 0.4, tone: 0.55 }, filt: { on: 0, type: 'lp', cut: 0.55, res: 0.35, rate: '2 bars', depth: 0.5 },
  echo: { on: 0, time: '1/8d', fb: 0.4, wob: 0.3, mix: 0.3 }, crys: { on: 0, mix: 0.4 }, shim: { on: 0, size: 0.6, amt: 0.5, mix: 0.35 }, wide: { on: 0, amt: 0.7 } });
const FX_SCENES = {
  Clean: {},
  Dreamy: { shim: { on: 1, size: 0.75, amt: 0.6, mix: 0.42 }, echo: { on: 1, time: '1/8d', fb: 0.45, wob: 0.25, mix: 0.28 } },
  Tape: { echo: { on: 1, time: '1/4', fb: 0.5, wob: 0.65, mix: 0.34 }, drive: { on: 1, amt: 0.35, tone: 0.42 }, filt: { on: 1, type: 'lp', cut: 0.66, res: 0.2, rate: '4 bars', depth: 0.18 } },
  Sweep: { filt: { on: 1, type: 'lp', cut: 0.48, res: 0.55, rate: '2 bars', depth: 0.75 } },
  Crystal: { crys: { on: 1, mix: 0.45 }, shim: { on: 1, size: 0.5, amt: 0.3, mix: 0.2 } },
  Wide: { wide: { on: 1, amt: 0.85 }, echo: { on: 1, time: '1/8', fb: 0.25, wob: 0.1, mix: 0.16 } },
};
function sceneFx(name, keepChords) { const d = fx2Defaults(), sc = FX_SCENES[name] || {}; for (const k in sc) d[k] = { ...d[k], ...sc[k] }; d.scene = name; d.chords = !!keepChords; return d; }
// two crossfaded delay taps swept by sawtooth LFOs. sign -1 with sweep A = period P: an octave up; sign +1 with A = 2P: played backwards
function grainShift(c, P, A, sign, t0) {
  const inp = c.createGain(), out = c.createGain(), win = new Float32Array(257);
  for (let i = 0; i < 257; i++) win[i] = Math.max(0, 1 - Math.abs(i / 128 - 1));
  for (let k = 0; k < 2; k++) {
    const d = c.createDelay(1), o = c.createOscillator(), og = c.createGain(), sh = c.createWaveShaper(), g = c.createGain();
    o.type = 'sawtooth'; o.frequency.value = 1 / P; og.gain.value = sign * A / 2; d.delayTime.value = A / 2 + 0.004; o.connect(og); og.connect(d.delayTime);
    sh.curve = win; o.connect(sh); g.gain.value = 0; sh.connect(g.gain);
    inp.connect(d); d.connect(g); g.connect(out); o.start(t0 + k * P / 2);
  }
  return { inp, out };
}
function softCurve(k) { const n = 1024, a = new Float32Array(n), t = Math.tanh(k); for (let i = 0; i < n; i++) { const x = i / (n - 1) * 2 - 1; a[i] = Math.tanh(k * x) / t; } return a; }
class FxRack {
  constructor(c) {
    const G = v => { const g = c.createGain(); g.gain.value = v; return g; }, t0 = c.currentTime;
    this.c = c; this.inp = G(1); this.out = G(1);
    const block = () => { const b = { i: G(1), o: G(1), dry: G(1), wet: G(0) }; b.i.connect(b.dry); b.dry.connect(b.o); b.wet.connect(b.o); return b; };
    // drive
    const dr = this.drive = block(); dr.sh = c.createWaveShaper(); dr.lp = c.createBiquadFilter(); dr.lp.type = 'lowpass'; dr.i.connect(dr.sh); dr.sh.connect(dr.lp); dr.lp.connect(dr.wet);
    // filter sweep (LFO on detune, in cents)
    const fl = this.filt = block(); fl.bq = c.createBiquadFilter(); fl.i.connect(fl.bq); fl.bq.connect(fl.wet);
    fl.lfo = c.createOscillator(); fl.lfo.frequency.value = 0.5; fl.lg = G(0); fl.lfo.connect(fl.lg); fl.lg.connect(fl.bq.detune); fl.lfo.start(t0);
    // tape echo: darkening, softly saturating feedback, wobbling delay time
    const ec = this.echo = block(); ec.wet.gain.value = 1; ec.send = G(0); ec.d = c.createDelay(2); ec.lp = c.createBiquadFilter(); ec.lp.type = 'lowpass'; ec.lp.frequency.value = 2600; ec.hp = c.createBiquadFilter(); ec.hp.type = 'highpass'; ec.hp.frequency.value = 160;
    ec.sat = c.createWaveShaper(); ec.sat.curve = softCurve(1.6); ec.fb = G(0);
    ec.i.connect(ec.send); ec.send.connect(ec.d); ec.d.connect(ec.lp); ec.lp.connect(ec.hp); ec.hp.connect(ec.sat); ec.sat.connect(ec.fb); ec.fb.connect(ec.d); ec.sat.connect(ec.wet);
    ec.lfo = c.createOscillator(); ec.lfo.frequency.value = 0.7; ec.lg = G(0); ec.lfo.connect(ec.lg); ec.lg.connect(ec.d.delayTime); ec.lfo.start(t0);
    // crystal: reversed grains, bright, a short echo that drifts across the stereo field
    const cr = this.crys = block(); cr.wet.gain.value = 1; cr.send = G(0); cr.gs = grainShift(c, 0.11, 0.22, 1, t0); cr.hp = c.createBiquadFilter(); cr.hp.type = 'highpass'; cr.hp.frequency.value = 700;
    cr.d = c.createDelay(2); cr.fb = G(0.42); cr.pan = c.createStereoPanner(); cr.plfo = c.createOscillator(); cr.plfo.frequency.value = 0.23; cr.pg = G(0.7); cr.plfo.connect(cr.pg); cr.pg.connect(cr.pan.pan); cr.plfo.start(t0);
    cr.i.connect(cr.send); cr.send.connect(cr.gs.inp); cr.gs.out.connect(cr.hp); cr.hp.connect(cr.d); cr.d.connect(cr.fb); cr.fb.connect(cr.d); cr.hp.connect(cr.pan); cr.d.connect(cr.pan); cr.pan.connect(cr.wet);
    // shimmer reverb: convolver whose tail is fed back an octave up
    const sm = this.shim = block(); sm.wet.gain.value = 1; sm.send = G(0); sm.sum = G(1); sm.conv = c.createConvolver(); sm.gs = grainShift(c, 0.06, 0.06, -1, t0);
    sm.hp = c.createBiquadFilter(); sm.hp.type = 'highpass'; sm.hp.frequency.value = 500; sm.lp = c.createBiquadFilter(); sm.lp.type = 'lowpass'; sm.lp.frequency.value = 8000; sm.fb = G(0); sm.ret = G(0.9);
    sm.i.connect(sm.send); sm.send.connect(sm.sum); sm.sum.connect(sm.conv); sm.conv.connect(sm.ret); sm.ret.connect(sm.wet); sm.conv.connect(sm.gs.inp); sm.gs.out.connect(sm.hp); sm.hp.connect(sm.lp); sm.lp.connect(sm.fb); sm.fb.connect(sm.sum);
    // wide: two short opposite-panned taps (Haas)
    const wd = this.wide = block(); wd.wet.gain.value = 1;
    for (const [dt, pn] of [[0.011, 1], [0.017, -1]]) { const d = c.createDelay(0.05), p = c.createStereoPanner(), g = G(0); d.delayTime.value = dt; p.pan.value = pn; wd.i.connect(d); d.connect(g); g.connect(p); p.connect(wd.wet); (wd.taps = wd.taps || []).push(g); }
    const chain = [dr, fl, ec, cr, sm, wd]; this.inp.connect(dr.i); for (let k = 0; k + 1 < chain.length; k++) chain[k].o.connect(chain[k + 1].i); wd.o.connect(this.out);
    this.irSize = -1; this.drAmt = -1;
  }
  apply(F, bpm) {
    const t = this.c.currentTime, set = (p, v) => p.setTargetAtTime(v, t, 0.03), beat = 60 / bpm, d = fx2Defaults();
    const f = k => ({ ...d[k], ...((F || {})[k] || {}) }), dr = f('drive'), fl = f('filt'), ec = f('echo'), cr = f('crys'), sm = f('shim'), wd = f('wide');
    const ins = (b, on) => { set(b.dry.gain, on ? 0 : 1); set(b.wet.gain, on ? 1 : 0); };
    ins(this.drive, dr.on); set(this.drive.wet.gain, dr.on ? 1 / (1 + dr.amt * 0.9) : 0); if (Math.abs(dr.amt - this.drAmt) > 0.01) { this.drive.sh.curve = softCurve(1 + dr.amt * 7); this.drAmt = dr.amt; } set(this.drive.lp.frequency, 900 * Math.pow(2, dr.tone * 4.2));
    ins(this.filt, fl.on); this.filt.bq.type = fl.type === 'hp' ? 'highpass' : 'lowpass'; set(this.filt.bq.frequency, 60 * Math.pow(2, fl.cut * 8.3)); set(this.filt.bq.Q, 0.7 + fl.res * 13);
    set(this.filt.lfo.frequency, 1 / (beat * (LFO_SYNC[fl.rate] || 8))); set(this.filt.lg.gain, fl.on ? fl.depth * 2600 : 0);
    const et = beat * (ECHO_SYNC[ec.time] || 0.75); set(this.echo.d.delayTime, et); set(this.echo.send.gain, ec.on ? ec.mix * 0.75 : 0); set(this.echo.fb.gain, ec.fb * 0.82); set(this.echo.lg.gain, ec.on ? ec.wob * 0.0035 : 0);
    set(this.crys.send.gain, cr.on ? cr.mix * 1.1 : 0); set(this.crys.d.delayTime, beat * 0.375);
    const sz = Math.round(sm.size * 10) / 10; if (sz !== this.irSize) { this.shim.conv.buffer = makeIR(this.c, 1.2 + sz * 3); this.irSize = sz; }
    set(this.shim.send.gain, sm.on ? sm.mix * 0.9 : 0); set(this.shim.fb.gain, sm.on ? sm.amt * 0.5 : 0);
    for (const g of this.wide.taps) set(g.gain, wd.on ? wd.amt * 0.55 : 0);
  }
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
    this.arpOut = c.createGain(); this.arpBus.connect(this.tone); this.tone.connect(this.arpOut);
    this.fxr = new FxRack(c); this.arpOut.connect(this.fxr.inp); this.fxr.out.connect(this.pumpG);
    this.chordBus = c.createGain(); this.chordDry = c.createGain(); this.chordFx = c.createGain(); this.chordFx.gain.value = 0;
    this.chordBus.connect(this.chordDry); this.chordDry.connect(this.pumpG); this.chordBus.connect(this.chordFx); this.chordFx.connect(this.fxr.inp);
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
    const F = S.fx2 || fx2Defaults(); this.fxr.apply(F, S.bpm); set(this.chordDry.gain, F.chords ? 0 : 1); set(this.chordFx.gain, F.chords ? 1 : 0);
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
