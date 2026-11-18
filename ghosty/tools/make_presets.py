#!/usr/bin/env python3
"""Ghosty Pro's own presets on Vital's engine, written from scratch.

  make_presets.py <init.vital> <out dir>

<init.vital> is Vital's init sound as Ghosty Pro saves it (`GhostyProTool init-json`). Every preset starts from it and
changes Vital's own controls, modulations, LFO shapes and wavetables. Wavetables are built from our own harmonic recipes
as Vital "Wave Source" keyframes (Vital morphs between them itself). Levels are fitted afterwards by
`GhostyProTool fit-level` (the preset's own volume knob), and check_distance.py proves no preset is near a studied one.
Ranges follow the aggregate lessons in ghosty/docs/STUDY-PRESETS.md; no studied patch or table is used."""
import base64, copy, json, math, os, struct, sys

init_path, out_dir = sys.argv[1], sys.argv[2]
INIT = json.load(open(init_path))
N = 2048
os.makedirs(out_dir, exist_ok=True)

# ------------------------------------------------------------------ wave frames (our own recipes)
def additive(amps, phases=None):
    """one cycle from harmonic amplitudes {harmonic: amplitude}"""
    out = [0.0] * N
    for h, a in amps.items():
        if a == 0 or h >= N // 2: continue
        ph = (phases or {}).get(h, 0.0)
        w = 2 * math.pi * h / N
        for i in range(N):
            out[i] += a * math.sin(w * i + ph)
    return norm(out)

def norm(x):
    m = max(1e-9, max(abs(v) for v in x))
    dc = sum(x) / len(x)
    return [(v - dc) / m for v in x]

def saw(n=160, tilt=1.0): return additive({h: (1.0 / h) ** tilt * (-1) ** (h + 1) for h in range(1, n)})
def square(n=160, tilt=1.0): return additive({h: (1.0 / h) ** tilt for h in range(1, n, 2)})
def pulse(width, n=160): return additive({h: math.sin(math.pi * h * width) / h for h in range(1, n)}, {h: math.pi / 2 for h in range(1, n)})   # cosine terms: a real pulse
def formant(f1, f2, bw=2.5, n=120, base=1.0):
    return additive({h: base / h ** 0.6 * (math.exp(-((h - f1) / bw) ** 2) + 0.7 * math.exp(-((h - f2) / (bw * 1.6)) ** 2) + 0.15 / h) for h in range(1, n)})
def organ(draw): return additive({h: a for h, a in draw.items()})
def folded(drive):
    x = [math.sin(2 * math.pi * i / N) * drive for i in range(N)]
    return norm([math.sin(v) for v in x])
def tri_sine(mix): return norm([(1 - mix) * math.sin(2 * math.pi * i / N) + mix * (2 * abs(2 * (i / N) - 1) - 1) * -1 for i in range(N)])
def hollow(n=60): return additive({h: (1.0 / h) * (1.0 if h % 3 else 0.15) for h in range(1, n)})
def bell(): return additive({1: 1.0, 2: 0.45, 3: 0.12, 5: 0.32, 7: 0.22, 9: 0.08, 12: 0.06})
def soft_sync(ratio, n=120):
    x = [math.sin(2 * math.pi * (i / N) * ratio) * (1 - i / N) for i in range(N)]
    return norm(x)

def b64(frame): return base64.b64encode(struct.pack("<%df" % N, *frame)).decode()

def wavetable(name, frames):
    """frames: [(position 0..256, frame)]; Vital morphs between keyframes itself"""
    comp = {"type": "Wave Source", "interpolation": 1, "interpolation_style": 1,
            "keyframes": [{"position": int(p), "wave_data": b64(f)} for p, f in frames]}
    return {"author": "GhostBlock", "full_normalize": True, "remove_all_dc": True, "name": name, "version": INIT["settings"]["wavetables"][0].get("version", "1.0.7"),
            "groups": [{"components": [comp]}]}

TABLES = {
    "Ghost Saw Square": lambda: wavetable("Ghost Saw Square", [(0, saw()), (128, square()), (256, pulse(0.18))]),
    "Soft Saw": lambda: wavetable("Soft Saw", [(0, saw(60, 1.6)), (256, saw(160, 1.0))]),
    "Vowel Growl": lambda: wavetable("Vowel Growl", [(0, formant(3, 7)), (85, formant(5, 12)), (170, formant(8, 18)), (256, formant(4, 22))]),
    "Fold Sine": lambda: wavetable("Fold Sine", [(0, folded(0.6)), (128, folded(2.5)), (256, folded(6.0))]),
    "Drawbars": lambda: wavetable("Drawbars", [(0, organ({1: 1, 2: 0.6, 3: 0.4})), (128, organ({1: 1, 2: 0.8, 3: 0.6, 4: 0.5, 6: 0.3})), (256, organ({1: 0.8, 2: 1, 4: 0.7, 6: 0.5, 8: 0.45, 10: 0.3}))]),
    "Hollow Reed": lambda: wavetable("Hollow Reed", [(0, hollow(20)), (256, hollow(80))]),
    "Glass Bell": lambda: wavetable("Glass Bell", [(0, bell()), (256, additive({1: 1, 3: 0.5, 4: 0.3, 7: 0.25, 11: 0.12, 15: 0.06}))]),
    "Sub Round": lambda: wavetable("Sub Round", [(0, tri_sine(0.0)), (256, tri_sine(0.6))]),
    "Sync Sweep": lambda: wavetable("Sync Sweep", [(0, soft_sync(1.5)), (128, soft_sync(3.0)), (256, soft_sync(5.5))]),
    "Thin Pulse": lambda: wavetable("Thin Pulse", [(0, pulse(0.5)), (256, pulse(0.06))]),
}

# ------------------------------------------------------------------ a preset
class P:
    def __init__(self, name, style, macros=("MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4")):
        self.d = copy.deepcopy(INIT)
        self.s = self.d["settings"]
        self.d["preset_name"] = name; self.d["preset_style"] = style; self.d["author"] = "GhostBlock"
        self.d["comments"] = "Ghosty Pro preset."
        for i, m in enumerate(macros): self.d["macro%d" % (i + 1)] = m
        self.name = name
        self.slot = 0
        # every osc into FILTER 1 (Vital's destination 0; with the filter off the sound passes straight through)
        for o in (1, 2, 3):
            self.set("osc_%d_destination" % o, 0)
    def set(self, k, v):
        if k not in self.s and not k.startswith(("modulation_",)): raise KeyError(self.name + ": unknown control " + k)
        self.s[k] = float(v); return self
    def sets(self, **kw):
        for k, v in kw.items(): self.set(k, v)
        return self
    def osc(self, o, table, level=0.7071, transpose=0, tune=0.0, voices=1, detune=2.5, frame=0, spread=1.0, blend=0.8,
            warp=0, warp_amt=0.5, morph=0, morph_amt=0.5, pan=0.0):
        self.s["wavetables"][o - 1] = TABLES[table]()
        p = "osc_%d_" % o
        return self.sets(**{p + "on": 1, p + "level": level, p + "transpose": transpose, p + "tune": tune, p + "unison_voices": voices,
                            p + "unison_detune": detune, p + "wave_frame": frame, p + "stereo_spread": spread, p + "unison_blend": blend,
                            p + "distortion_type": warp, p + "distortion_amount": warp_amt, p + "spectral_morph_type": morph,
                            p + "spectral_morph_amount": morph_amt, p + "pan": pan})
    def filt(self, f=1, model=0, style=1, cutoff=70, res=0.3, drive=0.0, blend=0.0, key=0.0, mix=1.0):
        p = "filter_%s_" % f
        return self.sets(**{p + "on": 1, p + "model": model, p + "style": style, p + "cutoff": cutoff, p + "resonance": res,
                            p + "drive": drive, p + "blend": blend, p + "keytrack": key, p + "mix": mix})
    def env(self, e, a=0.0, d=0.6, s=1.0, r=0.2, hold=0.0, delay=0.0):
        root = lambda secs: secs ** 0.25   # Vital stores envelope times as the 4th root of seconds
        p = "env_%d_" % e
        return self.sets(**{p + "attack": root(a), p + "decay": root(d), p + "sustain": s, p + "release": root(r), p + "hold": root(hold), p + "delay": root(delay)})
    def lfo(self, l, hz=None, tempo=None, mode=1, shape=None, smooth=0.0):
        p = "lfo_%d_" % l
        if tempo is not None: self.sets(**{p + "sync": 1, p + "tempo": tempo})
        else: self.sets(**{p + "sync": 0, p + "frequency": math.log2(hz)})
        self.set(p + "sync_type", mode)
        self.set(p + "smooth_time", smooth) if (p + "smooth_time") in self.s else None
        if shape is not None: self.s["lfos"][l - 1] = shape
        return self
    def mod(self, src, dst, amt, bipolar=False, power=0.0):
        if dst not in self.s: raise KeyError(self.name + ": unknown destination " + dst)
        i = self.slot; self.slot += 1
        self.s["modulations"][i] = {"source": src, "destination": dst}
        for k, v in (("amount", amt), ("bipolar", 1 if bipolar else 0), ("power", power), ("stereo", 0), ("bypass", 0)):
            self.s["modulation_%d_%s" % (i + 1, k)] = float(v)
        return self
    def fx(self, name, **kw):
        self.set(name + "_on", 1)
        for k, v in kw.items(): self.set(name + "_" + k, v)
        return self
    def voice(self, poly=8, glide=None, legato=False, vel=0.4, bend=2):
        self.sets(polyphony=poly, legato=1 if legato else 0, velocity_track=vel, pitch_bend_range=bend)
        if glide is not None: self.set("portamento_time", math.log2(glide))   # seconds (Vital stores log2)
        return self

def shape(name, pts, smooth=False):
    """an LFO shape from (x, value) points, value 1 = top (Vital stores y downwards)"""
    flat = []
    for x, v in pts: flat += [x, 1.0 - v]
    return {"name": name, "num_points": len(pts), "points": flat, "powers": [0.0] * len(pts), "smooth": smooth}

SAW_DOWN = shape("Fall", [(0, 1), (1, 0)])
WOBBLE = shape("Wobble", [(0, 0), (0.5, 1), (1, 0)], True)
STEPS = shape("Steps", [(0, 1), (0.25, 1), (0.25, 0.35), (0.5, 0.35), (0.5, 0.8), (0.75, 0.8), (0.75, 0.1), (1, 0.1)])
PUMP = shape("Pump", [(0, 0), (0.08, 1), (1, 1)])
SWELL = shape("Swell", [(0, 0), (0.6, 1), (1, 0.2)], True)

T = {"1/1": 6, "1/2": 7, "1/4": 8, "1/8": 9, "1/16": 10, "2/1": 5}   # Vital's synced-rate list index
presets = []
def add(p): presets.append(p); return p

# ------------------------------------------------------------------ BASS (the study: 2-3 oscs, analog filter low, LFO / macro motion, fast full-sustain amp)
p = add(P("Rubber Sub", "Bass", ("TONE", "PUNCH", "WIDTH", "DIRT")))
p.osc(1, "Sub Round", level=0.85, transpose=-12).osc(2, "Soft Saw", level=0.35, transpose=-12, voices=2, detune=1.2)
p.filt(cutoff=52, res=0.18, drive=1.0, key=0.3).env(1, a=0.002, d=0.4, s=1.0, r=0.06).env(2, a=0.0, d=0.18, s=0.0, r=0.1)
p.mod("env_2", "filter_1_cutoff", 0.28).mod("macro_control_1", "filter_1_cutoff", 0.35).mod("macro_control_2", "env_2_decay", 0.3)
p.mod("macro_control_3", "osc_2_unison_detune", 0.3).mod("macro_control_4", "distortion_drive", 0.5)
p.fx("distortion", type=1, drive=0.0, mix=1.0).fx("compressor", mix=0.6).voice(poly=1, glide=0.03, legato=True)

p = add(P("Velvet 808", "Bass", ("DECAY", "BITE", "SLIDE", "BOOM")))
p.osc(1, "Sub Round", level=0.9, transpose=-12, frame=40).osc(2, "Fold Sine", level=0.25, transpose=0, frame=60)
p.env(1, a=0.0, d=1.6, s=0.25, r=0.5).env(2, a=0.0, d=0.06, s=0.0, r=0.05)
p.mod("env_2", "osc_1_transpose", 0.08).mod("macro_control_1", "env_1_decay", 0.35).mod("macro_control_2", "osc_2_wave_frame", 0.6)
p.mod("macro_control_4", "distortion_drive", 0.45).fx("distortion", type=0, drive=4.0, mix=0.55).fx("eq", low_gain=2.0)
p.voice(poly=1, glide=0.08, legato=True, vel=0.3, bend=12)

p = add(P("Neon Reese", "Bass", ("MOVE", "OPEN", "WIDE", "GRIT")))
p.osc(1, "Ghost Saw Square", level=0.6, transpose=-12, voices=4, detune=3.2, frame=20).osc(2, "Ghost Saw Square", level=0.5, transpose=-12, tune=0.11, voices=3, detune=2.4, frame=70)
p.osc(3, "Sub Round", level=0.6, transpose=-24)
p.filt(model=2, style=1, cutoff=58, res=0.32, drive=3.0).env(1, a=0.004, d=1.0, s=1.0, r=0.12)
p.lfo(1, hz=0.35, mode=1, shape=WOBBLE).mod("lfo_1", "osc_1_wave_frame", 0.35, True).mod("lfo_1", "filter_1_cutoff", 0.08, True)
p.mod("macro_control_1", "lfo_1_frequency", 0.4).mod("macro_control_2", "filter_1_cutoff", 0.3).mod("macro_control_3", "osc_1_unison_detune", 0.35)
p.mod("macro_control_4", "distortion_mix", 0.6).fx("distortion", type=2, drive=6.0, mix=0.25).fx("chorus", dry_wet=0.18).fx("compressor", mix=0.8)
p.voice(poly=1, glide=0.05, legato=True)

p = add(P("Wobble Ghost", "Bass", ("WIDTH", "DEPTH", "VOWEL", "SMASH")))
p.osc(1, "Vowel Growl", level=0.75, transpose=-12, voices=2, detune=1.8, warp=2, warp_amt=0.3).osc(3, "Sub Round", level=0.55, transpose=-24)
p.filt(cutoff=60, res=0.42, drive=2.5).env(1, a=0.003, d=1.0, s=1.0, r=0.08)
p.lfo(1, tempo=T["1/8"], mode=1, shape=WOBBLE).mod("lfo_1", "filter_1_cutoff", 0.42).mod("lfo_1", "osc_1_wave_frame", 0.55)
p.mod("macro_control_1", "osc_1_unison_detune", 0.3).mod("macro_control_2", "modulation_1_amount", 0.4).mod("macro_control_3", "osc_1_distortion_amount", 0.5)
p.mod("macro_control_4", "distortion_drive", 0.55).fx("distortion", type=3, drive=3.0, mix=0.4).fx("compressor", mix=1.0).fx("eq", high_gain=-3.0)
p.voice(poly=1, legato=True)

p = add(P("House Organ Bass", "Bass", ("BODY", "CLICK", "TONE", "DRIVE")))
p.osc(1, "Drawbars", level=0.8, transpose=-12, frame=90).osc(2, "Sub Round", level=0.45, transpose=-12)
p.filt(cutoff=80, res=0.1, key=0.4).env(1, a=0.001, d=0.25, s=0.55, r=0.07).env(2, a=0.0, d=0.08, s=0.0, r=0.05)
p.mod("env_2", "filter_1_cutoff", 0.25).mod("macro_control_1", "osc_2_level", 0.4).mod("macro_control_2", "modulation_1_amount", 0.5)
p.mod("macro_control_3", "osc_1_wave_frame", 0.5).mod("macro_control_4", "distortion_drive", 0.4).fx("distortion", type=1, drive=2.0, mix=0.3)
p.voice(poly=2, vel=0.5)

p = add(P("Square Bounce", "Bass", ("PLUCK", "PW", "OCTAVE", "SPACE")))
p.osc(1, "Thin Pulse", level=0.7, transpose=-12, frame=50).osc(2, "Thin Pulse", level=0.4, transpose=0, frame=150, tune=-0.06)
p.filt(model=0, style=0, cutoff=48, res=0.38).env(1, a=0.001, d=0.35, s=0.35, r=0.1).env(2, a=0.0, d=0.22, s=0.1, r=0.12)
p.mod("env_2", "filter_1_cutoff", 0.4).mod("macro_control_1", "env_2_decay", 0.4).mod("macro_control_2", "osc_1_wave_frame", 0.6)
p.mod("macro_control_3", "osc_2_level", 0.5).mod("macro_control_4", "delay_dry_wet", 0.2).fx("delay", dry_wet=0.0, sync=1, tempo=T["1/8"], feedback=0.3)
p.voice(poly=1, glide=0.02, legato=True, vel=0.5)

p = add(P("Phonk Cowbell Bass", "Bass", ("BELL", "DECAY", "TONE", "CRUNCH")))
p.osc(1, "Glass Bell", level=0.55, transpose=0).osc(2, "Sub Round", level=0.8, transpose=-12)
p.filt(cutoff=90, res=0.2).env(1, a=0.0, d=0.9, s=0.1, r=0.25)
p.mod("macro_control_1", "osc_1_level", 0.4).mod("macro_control_2", "env_1_decay", 0.4).mod("macro_control_3", "filter_1_cutoff", 0.35)
p.mod("macro_control_4", "distortion_mix", 0.6).fx("distortion", type=4, drive=0.0, mix=0.2).fx("eq", band_gain=-2.0)
p.voice(poly=1, vel=0.4, bend=12)

p = add(P("Tape Mono", "Bass", ("WARMTH", "WOBBLE", "CUTOFF", "AGE")))
p.osc(1, "Soft Saw", level=0.75, transpose=-12, voices=2, detune=0.8).osc(2, "Hollow Reed", level=0.4, transpose=-12)
p.filt(model=1, style=1, cutoff=55, res=0.25, drive=4.0).env(1, a=0.003, d=1.0, s=0.9, r=0.1)
p.lfo(2, hz=5.5, mode=1).mod("lfo_2", "osc_1_tune", 0.03, True).mod("macro_control_2", "modulation_1_amount", 0.6)
p.mod("macro_control_1", "filter_1_drive", 0.4).mod("macro_control_3", "filter_1_cutoff", 0.4).mod("macro_control_4", "chorus_dry_wet", 0.4)
p.fx("chorus", dry_wet=0.12, voices=1).voice(poly=1, glide=0.06, legato=True)

# ------------------------------------------------------------------ LEAD (2 oscs, envelope on the cutoff, glide often, chorus + reverb)
p = add(P("Ghost Whistle", "Lead", ("VIBRATO", "AIR", "GLIDE", "SPACE")))
p.osc(1, "Sub Round", level=0.75, transpose=12, frame=30).osc(2, "Hollow Reed", level=0.25, transpose=24)
p.filt(cutoff=96, res=0.15).env(1, a=0.04, d=1.0, s=0.9, r=0.35)
p.lfo(1, hz=5.2, mode=1).lfo(2, hz=0.3, mode=1)
p.mod("lfo_1", "osc_1_tune", 0.035, True).mod("macro_control_1", "modulation_1_amount", 0.6).mod("macro_control_2", "osc_2_level", 0.4)
p.mod("macro_control_3", "portamento_time", 0.3).mod("macro_control_4", "reverb_dry_wet", 0.3)
p.fx("reverb", dry_wet=0.22, size=0.6).fx("delay", dry_wet=0.12, sync=1, tempo=T["1/4"], feedback=0.35).voice(poly=1, glide=0.08, legato=True)

p = add(P("Supersaw Halo", "Lead", ("DETUNE", "BRIGHT", "WIDTH", "VERB")))
p.osc(1, "Ghost Saw Square", level=0.6, voices=7, detune=3.6, frame=0).osc(2, "Ghost Saw Square", level=0.4, transpose=12, voices=5, detune=2.8)
p.filt(cutoff=100, res=0.12).env(1, a=0.01, d=1.0, s=0.85, r=0.3).env(2, a=0.0, d=0.5, s=0.3, r=0.3)
p.mod("env_2", "filter_1_cutoff", 0.18).mod("macro_control_1", "osc_1_unison_detune", 0.35).mod("macro_control_2", "filter_1_cutoff", 0.25)
p.mod("macro_control_3", "osc_1_stereo_spread", 0.4).mod("macro_control_4", "reverb_dry_wet", 0.35)
p.fx("chorus", dry_wet=0.3).fx("reverb", dry_wet=0.24, size=0.7).fx("compressor", mix=0.5).voice(poly=8)

p = add(P("Acid Ghost", "Lead", ("CUTOFF", "RES", "ENV", "SQUELCH")))
p.osc(1, "Ghost Saw Square", level=0.75, frame=0)
p.filt(model=4, style=1, cutoff=55, res=0.68, drive=4.0).env(1, a=0.001, d=0.4, s=0.6, r=0.1).env(2, a=0.0, d=0.25, s=0.0, r=0.1)
p.mod("env_2", "filter_1_cutoff", 0.45).mod("velocity", "filter_1_cutoff", 0.15).mod("macro_control_1", "filter_1_cutoff", 0.4)
p.mod("macro_control_2", "filter_1_resonance", 0.3).mod("macro_control_3", "modulation_1_amount", 0.4).mod("macro_control_4", "distortion_drive", 0.5)
p.fx("distortion", type=1, drive=0.0, mix=0.5).fx("delay", dry_wet=0.14, sync=1, tempo=T["1/8"], feedback=0.4).voice(poly=1, glide=0.05, legato=True, vel=0.6)

p = add(P("Square Hero", "Lead", ("PWM", "VIB", "OCTAVE", "ECHO")))
p.osc(1, "Thin Pulse", level=0.65, frame=40).osc(2, "Thin Pulse", level=0.35, transpose=-12, frame=120)
p.filt(cutoff=88, res=0.2).env(1, a=0.006, d=1.0, s=0.9, r=0.2)
p.lfo(1, hz=0.6, mode=1, shape=WOBBLE).lfo(2, hz=5.8, mode=0)
p.mod("lfo_1", "osc_1_wave_frame", 0.3).mod("lfo_2", "osc_1_tune", 0.025, True).mod("macro_control_1", "modulation_1_amount", 0.5)
p.mod("macro_control_2", "modulation_2_amount", 0.6).mod("macro_control_3", "osc_2_level", 0.5).mod("macro_control_4", "delay_dry_wet", 0.3)
p.fx("delay", dry_wet=0.12, sync=1, tempo=T["1/4"], feedback=0.3).fx("reverb", dry_wet=0.15).voice(poly=1, glide=0.04, legato=True)

p = add(P("Formant Singer", "Lead", ("VOWEL", "BREATH", "VIB", "HALL")))
p.osc(1, "Vowel Growl", level=0.7, voices=2, detune=0.6, frame=30).osc(2, "Sub Round", level=0.3)
p.filt(model=5, style=0, cutoff=72, res=0.3).sets(filter_1_formant_x=0.3, filter_1_formant_y=0.6).env(1, a=0.05, d=1.0, s=0.9, r=0.4)
p.lfo(1, hz=0.25, mode=1, shape=SWELL).lfo(2, hz=5.0, mode=1)
p.mod("lfo_1", "filter_1_formant_x", 0.4).mod("lfo_2", "osc_1_tune", 0.03, True).mod("macro_control_1", "filter_1_formant_y", 0.5)
p.mod("macro_control_2", "osc_1_wave_frame", 0.5).mod("macro_control_3", "modulation_2_amount", 0.6).mod("macro_control_4", "reverb_dry_wet", 0.35)
p.fx("reverb", dry_wet=0.28, size=0.75).fx("chorus", dry_wet=0.2).voice(poly=1, glide=0.1, legato=True)

p = add(P("Sync Scream", "Lead", ("SYNC", "BITE", "RISE", "SPREAD")))
p.osc(1, "Sync Sweep", level=0.7, voices=3, detune=1.5, frame=40).osc(2, "Soft Saw", level=0.3, transpose=-12)
p.filt(cutoff=94, res=0.25, drive=2.0).env(1, a=0.002, d=1.0, s=0.9, r=0.18).env(2, a=0.0, d=0.6, s=0.2, r=0.3)
p.mod("env_2", "osc_1_wave_frame", 0.6).mod("macro_control_1", "osc_1_wave_frame", 0.4).mod("macro_control_2", "distortion_mix", 0.5)
p.mod("macro_control_3", "osc_1_transpose", 0.15).mod("macro_control_4", "osc_1_stereo_spread", 0.4)
p.fx("distortion", type=1, drive=3.0, mix=0.3).fx("delay", dry_wet=0.1, sync=1, tempo=T["1/8"], feedback=0.25).fx("reverb", dry_wet=0.18).voice(poly=1, glide=0.03, legato=True)

# ------------------------------------------------------------------ PAD (slow attack ~0.16 s, long release, LFO on the wave position, chorus + reverb)
p = add(P("Haunted Choir", "Pad", ("MOTION", "VOWEL", "SHIMMER", "SPACE")))
p.osc(1, "Vowel Growl", level=0.6, voices=6, detune=1.6, frame=60).osc(2, "Hollow Reed", level=0.4, transpose=12, voices=4, detune=2.0)
p.filt(cutoff=84, res=0.2).env(1, a=0.9, d=2.0, s=0.9, r=2.4)
p.lfo(1, hz=0.12, mode=1, shape=SWELL, smooth=0.3).mod("lfo_1", "osc_1_wave_frame", 0.5).mod("lfo_1", "filter_1_cutoff", 0.1, True)
p.mod("macro_control_1", "lfo_1_frequency", 0.35).mod("macro_control_2", "osc_1_wave_frame", 0.4).mod("macro_control_3", "osc_2_level", 0.4)
p.mod("macro_control_4", "reverb_dry_wet", 0.3).fx("chorus", dry_wet=0.45).fx("reverb", dry_wet=0.35, size=0.85, decay_time=1.5).voice(poly=8, vel=0.2)

p = add(P("Glass Fog", "Pad", ("FOG", "SPARKLE", "DRIFT", "TAIL")))
p.osc(1, "Glass Bell", level=0.5, voices=4, detune=0.9).osc(2, "Sub Round", level=0.45, transpose=-12)
p.osc(3, "Fold Sine", level=0.25, transpose=12, frame=90)
p.filt(cutoff=76, res=0.15).env(1, a=0.6, d=2.5, s=0.75, r=3.0)
p.lfo(1, hz=0.08, mode=1, shape=WOBBLE).mod("lfo_1", "osc_3_wave_frame", 0.6).mod("macro_control_1", "filter_1_cutoff", -0.3)
p.mod("macro_control_2", "osc_3_level", 0.5).mod("macro_control_3", "osc_1_unison_detune", 0.4).mod("macro_control_4", "reverb_decay_time", 0.4)
p.fx("reverb", dry_wet=0.4, size=0.9, decay_time=2.5).fx("phaser", dry_wet=0.2, frequency=-2.5).voice(poly=10, vel=0.2)

p = add(P("Warm Blanket", "Pad", ("WARM", "MOVE", "WIDE", "AIR")))
p.osc(1, "Soft Saw", level=0.55, voices=8, detune=2.0).osc(2, "Soft Saw", level=0.45, transpose=-12, voices=4, detune=1.4)
p.filt(model=0, style=1, cutoff=66, res=0.12).env(1, a=0.35, d=2.0, s=1.0, r=1.6).env(2, a=1.2, d=2.0, s=0.6, r=1.5)
p.mod("env_2", "filter_1_cutoff", 0.2).mod("macro_control_1", "filter_1_cutoff", 0.3).mod("macro_control_2", "osc_1_wave_frame", 0.5)
p.mod("macro_control_3", "chorus_dry_wet", 0.4).mod("macro_control_4", "osc_2_level", 0.4)
p.fx("chorus", dry_wet=0.5, voices=3).fx("reverb", dry_wet=0.25, size=0.6).voice(poly=10, vel=0.15)

p = add(P("Tidal Organ Pad", "Pad", ("TIDE", "DRAWBAR", "SPIN", "ROOM")))
p.osc(1, "Drawbars", level=0.6, voices=3, detune=0.8, frame=60).osc(2, "Drawbars", level=0.35, transpose=12, frame=200)
p.filt(cutoff=90, res=0.1).env(1, a=0.25, d=2.0, s=1.0, r=1.2)
p.lfo(1, tempo=T["2/1"], mode=1, shape=WOBBLE).mod("lfo_1", "osc_1_wave_frame", 0.5).mod("lfo_1", "osc_2_wave_frame", -0.4)
p.mod("macro_control_1", "modulation_1_amount", 0.4).mod("macro_control_2", "osc_1_wave_frame", 0.4).mod("macro_control_3", "flanger_dry_wet", 0.4)
p.mod("macro_control_4", "reverb_dry_wet", 0.3).fx("flanger", dry_wet=0.12).fx("reverb", dry_wet=0.22, size=0.55).voice(poly=10, vel=0.2)

p = add(P("Night Drone", "Pad", ("DARK", "PULSE", "NOISE", "VAST")))
p.osc(1, "Hollow Reed", level=0.55, transpose=-12, voices=5, detune=1.0).osc(2, "Fold Sine", level=0.4, frame=30, morph=5, morph_amt=0.4)
p.filt(model=2, style=1, cutoff=58, res=0.35).env(1, a=1.8, d=3.0, s=1.0, r=4.0)
p.lfo(1, tempo=T["1/1"], mode=1, shape=PUMP).lfo(2, hz=0.05, mode=1, shape=SWELL)
p.mod("lfo_2", "filter_1_cutoff", 0.25).mod("lfo_1", "osc_2_level", 0.3).mod("macro_control_1", "filter_1_cutoff", -0.3)
p.mod("macro_control_2", "modulation_2_amount", 0.5).mod("macro_control_3", "osc_2_spectral_morph_amount", 0.5).mod("macro_control_4", "reverb_size", 0.3)
p.fx("reverb", dry_wet=0.45, size=0.95, decay_time=3.0).fx("delay", dry_wet=0.1, sync=1, tempo=T["1/2"], feedback=0.5).voice(poly=8, vel=0.1)

# ------------------------------------------------------------------ KEYS (decay ~0.7 s to ~0.3, release ~0.27 s, resonance ~0.5, envelope on cutoff, delay)
p = add(P("Ghost Rhodes", "Keys", ("TINE", "BARK", "BELL", "ROOM")))
p.osc(1, "Glass Bell", level=0.5, frame=0).osc(2, "Sub Round", level=0.6, frame=60)
p.filt(cutoff=82, res=0.25, key=0.3).env(1, a=0.001, d=1.4, s=0.35, r=0.3).env(2, a=0.0, d=0.3, s=0.0, r=0.2)
p.lfo(1, hz=4.5, mode=1).mod("env_2", "osc_1_level", 0.3).mod("velocity", "filter_1_cutoff", 0.2)
p.mod("macro_control_1", "osc_1_level", 0.4).mod("macro_control_2", "distortion_mix", 0.4)
p.mod("macro_control_3", "modulation_3_amount", 0.3).mod("macro_control_4", "reverb_dry_wet", 0.3)
p.fx("chorus", dry_wet=0.18).fx("reverb", dry_wet=0.16, size=0.45).fx("distortion", type=0, drive=0.0, mix=0.0).voice(poly=12, vel=0.7)

p = add(P("Lofi Felt Piano", "Keys", ("FELT", "WOW", "HISS", "TAPE")))
p.osc(1, "Sub Round", level=0.7, frame=120).osc(2, "Drawbars", level=0.3, transpose=12, frame=30)
p.filt(model=0, style=0, cutoff=72, res=0.45, key=0.4).env(1, a=0.002, d=0.8, s=0.25, r=0.35).env(2, a=0.0, d=0.5, s=0.1, r=0.3)
p.lfo(2, hz=0.7, mode=1, shape=WOBBLE).mod("env_2", "filter_1_cutoff", 0.3).mod("lfo_2", "osc_1_tune", 0.02, True)
p.mod("macro_control_1", "filter_1_cutoff", -0.3).mod("macro_control_2", "modulation_2_amount", 0.6).mod("macro_control_3", "sample_level", 0.3)
p.mod("macro_control_4", "chorus_dry_wet", 0.4).sets(sample_on=1, sample_level=0.05)
p.fx("chorus", dry_wet=0.15).fx("delay", dry_wet=0.1, sync=1, tempo=T["1/4"], feedback=0.4).fx("reverb", dry_wet=0.18).voice(poly=12, vel=0.6)

p = add(P("Club Chord Stab", "Keys", ("CUTOFF", "LENGTH", "WIDTH", "ECHO")))
p.osc(1, "Ghost Saw Square", level=0.55, voices=4, detune=2.2, frame=60).osc(2, "Drawbars", level=0.35, frame=180)
p.filt(cutoff=70, res=0.5).env(1, a=0.001, d=0.45, s=0.3, r=0.25).env(2, a=0.0, d=0.3, s=0.05, r=0.2)
p.mod("env_2", "filter_1_cutoff", 0.35).mod("macro_control_1", "filter_1_cutoff", 0.35).mod("macro_control_2", "env_1_decay", 0.35)
p.mod("macro_control_3", "osc_1_stereo_spread", 0.4).mod("macro_control_4", "delay_dry_wet", 0.25)
p.fx("delay", dry_wet=0.12, sync=1, tempo=T["1/8"], feedback=0.35).fx("reverb", dry_wet=0.15).fx("compressor", mix=0.5).voice(poly=12, vel=0.5)

p = add(P("Bell Organ", "Keys", ("CHIME", "PERC", "SPIN", "HALL")))
p.osc(1, "Drawbars", level=0.6, frame=140).osc(2, "Glass Bell", level=0.35, transpose=12)
p.filt(cutoff=96, res=0.2).env(1, a=0.003, d=1.0, s=0.8, r=0.2).env(2, a=0.0, d=0.15, s=0.0, r=0.1)
p.mod("env_2", "osc_2_level", 0.4).mod("macro_control_1", "osc_2_level", 0.4).mod("macro_control_2", "modulation_1_amount", 0.5)
p.mod("macro_control_3", "phaser_dry_wet", 0.4).mod("macro_control_4", "reverb_dry_wet", 0.3)
p.fx("phaser", dry_wet=0.15).fx("reverb", dry_wet=0.18, size=0.6).voice(poly=12, vel=0.4)

p = add(P("Digital Marimba", "Keys", ("WOOD", "MALLET", "SIZE", "SPACE")))
p.osc(1, "Glass Bell", level=0.6, frame=200).osc(2, "Fold Sine", level=0.3, transpose=12, frame=20)
p.filt(cutoff=92, res=0.15).env(1, a=0.0, d=0.55, s=0.0, r=0.4).env(2, a=0.0, d=0.08, s=0.0, r=0.05)
p.mod("env_2", "osc_2_wave_frame", 0.6).mod("velocity", "osc_2_level", 0.3).mod("macro_control_1", "osc_1_wave_frame", -0.5)
p.mod("macro_control_2", "env_2_decay", 0.4).mod("macro_control_3", "env_1_decay", 0.4).mod("macro_control_4", "reverb_dry_wet", 0.3)
p.fx("reverb", dry_wet=0.2, size=0.5).voice(poly=12, vel=0.7)

# ------------------------------------------------------------------ PLUCK (sustain 0, decay ~0.6 s, filter starts low and opens, delay + reverb at low mixes)
p = add(P("Crystal Pluck", "Pluck", ("SNAP", "LENGTH", "SHINE", "ECHO")))
p.osc(1, "Ghost Saw Square", level=0.55, voices=5, detune=2.4, frame=30).osc(2, "Glass Bell", level=0.3, transpose=12)
p.filt(cutoff=44, res=0.3).env(1, a=0.0, d=0.6, s=0.0, r=0.35).env(2, a=0.0, d=0.28, s=0.0, r=0.2)
p.mod("env_2", "filter_1_cutoff", 0.55).mod("velocity", "filter_1_cutoff", 0.15).mod("macro_control_1", "env_2_decay", 0.4)
p.mod("macro_control_2", "env_1_decay", 0.4).mod("macro_control_3", "osc_2_level", 0.4).mod("macro_control_4", "delay_dry_wet", 0.3)
p.fx("delay", dry_wet=0.14, sync=1, tempo=T["1/8"], feedback=0.38).fx("reverb", dry_wet=0.25, size=0.55).voice(poly=12, vel=0.5)

p = add(P("Kalimba Ghost", "Pluck", ("TINE", "BODY", "DECAY", "SPACE")))
p.osc(1, "Fold Sine", level=0.65, frame=10).osc(2, "Glass Bell", level=0.35, transpose=24)
p.filt(cutoff=86, res=0.15).env(1, a=0.0, d=0.9, s=0.0, r=0.6).env(2, a=0.0, d=0.04, s=0.0, r=0.03)
p.mod("env_2", "osc_1_wave_frame", 0.5).mod("macro_control_1", "osc_2_level", 0.4).mod("macro_control_2", "osc_1_wave_frame", 0.3)
p.mod("macro_control_3", "env_1_decay", 0.4).mod("macro_control_4", "reverb_dry_wet", 0.35)
p.fx("reverb", dry_wet=0.24, size=0.5).fx("delay", dry_wet=0.08, sync=1, tempo=T["1/4"], feedback=0.3).voice(poly=12, vel=0.6)

p = add(P("Trap Bell Pluck", "Pluck", ("BRIGHT", "TAIL", "DETUNE", "WIDE")))
p.osc(1, "Glass Bell", level=0.6, voices=3, detune=0.7).osc(2, "Thin Pulse", level=0.2, transpose=12, frame=200)
p.filt(cutoff=60, res=0.25).env(1, a=0.0, d=1.2, s=0.0, r=0.8).env(2, a=0.0, d=0.4, s=0.0, r=0.3)
p.mod("env_2", "filter_1_cutoff", 0.4).mod("macro_control_1", "filter_1_cutoff", 0.3).mod("macro_control_2", "env_1_decay", 0.35)
p.mod("macro_control_3", "osc_1_unison_detune", 0.4).mod("macro_control_4", "chorus_dry_wet", 0.4)
p.fx("chorus", dry_wet=0.15).fx("delay", dry_wet=0.12, sync=1, tempo=T["1/8"], feedback=0.45).fx("reverb", dry_wet=0.28, size=0.65).voice(poly=12, vel=0.5)

p = add(P("Hollow Pizz", "Pluck", ("HOLLOW", "SNAP", "PITCH", "ROOM")))
p.osc(1, "Hollow Reed", level=0.65, voices=2, detune=0.5).osc(2, "Sub Round", level=0.35, transpose=-12)
p.filt(model=0, style=0, cutoff=40, res=0.4).env(1, a=0.0, d=0.35, s=0.0, r=0.2).env(2, a=0.0, d=0.15, s=0.0, r=0.1)
p.mod("env_2", "filter_1_cutoff", 0.6).mod("env_2", "osc_1_transpose", 0.02).mod("macro_control_1", "osc_1_wave_frame", 0.6)
p.mod("macro_control_2", "env_2_decay", 0.4).mod("macro_control_3", "modulation_2_amount", 0.6).mod("macro_control_4", "reverb_dry_wet", 0.3)
p.fx("reverb", dry_wet=0.2, size=0.4).voice(poly=12, vel=0.6)

p = add(P("Future Pluck Chord", "Pluck", ("CUT", "DECAY", "SATURATE", "STEREO")))
p.osc(1, "Ghost Saw Square", level=0.5, voices=6, detune=3.0, frame=100).osc(2, "Vowel Growl", level=0.3, frame=200)
p.filt(model=3, style=1, cutoff=46, res=0.35).env(1, a=0.0, d=0.5, s=0.05, r=0.3).env(2, a=0.0, d=0.22, s=0.0, r=0.2)
p.mod("env_2", "filter_1_cutoff", 0.5).mod("macro_control_1", "filter_1_cutoff", 0.3).mod("macro_control_2", "env_1_decay", 0.4)
p.mod("macro_control_3", "distortion_mix", 0.5).mod("macro_control_4", "osc_1_stereo_spread", 0.4)
p.fx("distortion", type=1, drive=0.0, mix=0.2).fx("compressor", mix=0.7).fx("reverb", dry_wet=0.2).fx("delay", dry_wet=0.1, sync=1, tempo=T["1/16"], feedback=0.3).voice(poly=12, vel=0.4)

# ------------------------------------------------------------------ SEQUENCE / FX
p = add(P("Gated Ghosts", "Sequence", ("GATE", "SMOOTH", "TONE", "ECHO")))
p.osc(1, "Ghost Saw Square", level=0.55, voices=5, detune=2.0, frame=80).osc(2, "Hollow Reed", level=0.35, transpose=12)
p.filt(cutoff=74, res=0.3).env(1, a=0.01, d=1.0, s=1.0, r=0.3)
p.lfo(1, tempo=T["1/16"], mode=1, shape=STEPS).mod("lfo_1", "osc_1_level", 0.5).mod("lfo_1", "filter_1_cutoff", 0.15)
p.mod("macro_control_1", "modulation_1_amount", 0.5).mod("macro_control_2", "lfo_1_smooth_time", 0.4).mod("macro_control_3", "filter_1_cutoff", 0.3)
p.mod("macro_control_4", "delay_dry_wet", 0.3).fx("delay", dry_wet=0.12, sync=1, tempo=T["1/8"], feedback=0.4).fx("reverb", dry_wet=0.2).voice(poly=8)

p = add(P("Pump Chords", "Sequence", ("PUMP", "OPEN", "WIDTH", "SPACE")))
p.osc(1, "Soft Saw", level=0.6, voices=6, detune=2.4).osc(2, "Drawbars", level=0.3, transpose=12, frame=100)
p.filt(cutoff=78, res=0.2).env(1, a=0.005, d=1.0, s=1.0, r=0.25)
p.lfo(1, tempo=T["1/4"], mode=1, shape=PUMP).mod("lfo_1", "osc_1_level", 0.6).mod("lfo_1", "osc_2_level", 0.4)
p.mod("macro_control_1", "modulation_1_amount", 0.4).mod("macro_control_2", "filter_1_cutoff", 0.35).mod("macro_control_3", "osc_1_stereo_spread", 0.4)
p.mod("macro_control_4", "reverb_dry_wet", 0.3).fx("reverb", dry_wet=0.2).fx("chorus", dry_wet=0.2).voice(poly=10)

p = add(P("Spirit Riser", "SFX", ("RISE", "NOISE", "SPIN", "WASH")))
p.osc(1, "Sync Sweep", level=0.85, voices=6, detune=3.5).osc(2, "Fold Sine", level=0.55, frame=0)
p.filt(cutoff=64, res=0.4).env(1, a=1.2, d=2.0, s=1.0, r=2.0).env(2, a=3.0, d=1.0, s=1.0, r=2.0)
p.mod("env_2", "filter_1_cutoff", 0.6).mod("env_2", "osc_1_wave_frame", 0.8).mod("env_2", "osc_1_transpose", 0.1)
p.mod("macro_control_1", "env_2_attack", 0.3).mod("macro_control_2", "sample_level", 0.4).mod("macro_control_3", "phaser_dry_wet", 0.5)
p.mod("macro_control_4", "reverb_dry_wet", 0.4).sets(sample_on=1, sample_level=0.08)
p.fx("phaser", dry_wet=0.3).fx("reverb", dry_wet=0.35, size=0.85).voice(poly=4)

p = add(P("Haunted Impact", "SFX", ("BOOM", "METAL", "TAIL", "WARP")))
p.osc(1, "Sub Round", level=0.9, transpose=-24).osc(2, "Glass Bell", level=0.4, transpose=-5, morph=4, morph_amt=0.6)
p.env(1, a=0.0, d=2.5, s=0.0, r=2.0).env(2, a=0.0, d=0.4, s=0.0, r=0.3)
p.mod("env_2", "osc_1_transpose", 0.25).mod("macro_control_1", "env_1_decay", 0.4).mod("macro_control_2", "osc_2_spectral_morph_amount", 0.4)
p.mod("macro_control_3", "reverb_decay_time", 0.4).mod("macro_control_4", "osc_2_distortion_amount", 0.5)
p.fx("reverb", dry_wet=0.35, size=0.9, decay_time=2.0).fx("distortion", type=0, drive=2.0, mix=0.3).voice(poly=4, vel=0.3)

p = add(P("Seance Texture", "SFX", ("STIR", "BREATH", "DEPTH", "FADE")))
p.osc(1, "Vowel Growl", level=0.5, voices=8, detune=4.0, morph=6, morph_amt=0.5).osc(2, "Hollow Reed", level=0.3, transpose=-12, morph=9, morph_amt=0.4)
p.filt(model=6, style=0, cutoff=70, res=0.45).env(1, a=1.0, d=2.0, s=1.0, r=3.0)
p.lfo(1, hz=0.07, mode=1, shape=SWELL).lfo(2, hz=0.19, mode=1, shape=WOBBLE)
p.mod("lfo_1", "osc_1_spectral_morph_amount", 0.4).mod("lfo_2", "filter_1_cutoff", 0.2, True).mod("macro_control_1", "lfo_1_frequency", 0.4)
p.mod("macro_control_2", "osc_2_level", 0.4).mod("macro_control_3", "filter_1_resonance", 0.3).mod("macro_control_4", "env_1_release", 0.3)
p.fx("reverb", dry_wet=0.4, size=0.9, decay_time=2.5).fx("flanger", dry_wet=0.2).voice(poly=6, vel=0.1)

# ------------------------------------------------------------------ character passes (each made more its own sound;
# also what keeps them far from every studied preset, see check_distance.py)
def tweak(name, fn):
    for p in presets:
        if p.name == name: fn(p); return
    raise KeyError(name)
tweak("Rubber Sub", lambda p: p.osc(3, "Thin Pulse", level=0.12, transpose=12, frame=200).filt(model=3, style=1, cutoff=50, res=0.22, drive=1.5, key=0.3)
      .lfo(2, hz=0.4, mode=1, shape=WOBBLE).mod("lfo_2", "osc_2_wave_frame", 0.25).fx("eq", low_gain=1.5, high_gain=-4.0))
tweak("Sync Scream", lambda p: p.osc(3, "Sub Round", level=0.4, transpose=-12).filt(model=6, style=0, cutoff=86, res=0.35, mix=0.55)
      .fx("phaser", dry_wet=0.18, feedback=0.5).fx("compressor", mix=0.6))
tweak("Digital Marimba", lambda p: p.osc(3, "Thin Pulse", level=0.15, transpose=19, frame=230).filt(f=2, model=0, style=0, cutoff=48, res=0.1, blend=2.0)
      .set("osc_3_destination", 1).fx("compressor", mix=0.5).fx("delay", dry_wet=0.08, sync=1, tempo=T["1/8"], feedback=0.2))
tweak("Crystal Pluck", lambda p: p.osc(3, "Sub Round", level=0.35, transpose=-12).filt(model=3, style=1, cutoff=42, res=0.32)
      .fx("flanger", dry_wet=0.1, feedback=0.3))
tweak("Kalimba Ghost", lambda p: p.osc(3, "Vowel Growl", level=0.12, transpose=12, frame=120).filt(model=5, style=0, cutoff=80, res=0.2)
      .fx("chorus", dry_wet=0.14).fx("eq", band_gain=2.0))
tweak("Hollow Pizz", lambda p: p.osc(3, "Glass Bell", level=0.18, transpose=12).filt(model=1, style=1, cutoff=44, res=0.42, drive=1.5)
      .fx("chorus", dry_wet=0.12).fx("delay", dry_wet=0.07, sync=1, tempo=T["1/8"], feedback=0.25))
tweak("Pump Chords", lambda p: p.osc(3, "Sub Round", level=0.35, transpose=-12).filt(model=2, style=1, cutoff=82, res=0.25, drive=1.0)
      .fx("distortion", type=1, drive=0.0, mix=0.12).fx("phaser", dry_wet=0.1, feedback=0.3))
tweak("Gated Ghosts", lambda p: p.osc(3, "Fold Sine", level=0.25, transpose=-12, frame=60).filt(model=7, style=0, cutoff=74, res=0.4)
      .fx("distortion", type=4, drive=0.0, mix=0.15).fx("flanger", dry_wet=0.12))

# ------------------------------------------------------------------ write
for i, p in enumerate(presets):
    for k in list(p.s.keys()):
        if isinstance(p.s[k], float) and (math.isnan(p.s[k]) or math.isinf(p.s[k])): raise ValueError(p.name + " " + k)
    path = os.path.join(out_dir, "%03d %s.vital" % (i + 1, p.name))
    json.dump(p.d, open(path, "w"))
print("wrote", len(presets), "presets")
