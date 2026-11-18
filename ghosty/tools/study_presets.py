#!/usr/bin/env python3
"""Study .vital presets for AGGREGATE patterns only (per category): never prints or stores a whole patch.

  study_presets.py <out.md> <private-features.json> <folder or .vitalbank>... [--exclude SUBSTRING]...

Writes <out.md>: counts, medians and usage rates per category (oscillators, unison, warps, filters, envelopes, LFOs,
modulation routings and depths, macros, effects and their mixes, output level). Writes <private-features.json>: one
normalised feature vector per studied preset, used ONLY by check_distance.py to prove our presets are far from every
studied preset. That file is patch data: keep it outside the repository (the scratchpad) and never commit it."""
import io, json, os, re, statistics, sys, zipfile, hashlib
from collections import Counter, defaultdict

args = sys.argv[1:]
excl = [args[i + 1].lower() for i, a in enumerate(args) if a == "--exclude"]
paths = [a for i, a in enumerate(args) if a != "--exclude" and (i == 0 or args[i - 1] != "--exclude")]
out_md, out_feat, roots = paths[0], paths[1], paths[2:]

def presets():
    seen = set()
    def emit(name, text, src):
        h = hashlib.sha1(text).hexdigest()
        if h in seen: return
        seen.add(h)
        try: d = json.loads(text)
        except Exception: return
        if "settings" in d: yield name, d, src
    for r in roots:
        if any(e in r.lower() for e in excl): continue
        files = [r] if os.path.isfile(r) else [os.path.join(dp, f) for dp, _, fs in os.walk(r) for f in fs]
        for f in sorted(files):
            if any(e in f.lower() for e in excl): continue
            if f.endswith(".vital"):
                yield from emit(os.path.basename(f)[:-6], open(f, "rb").read(), f)
            elif f.endswith(".vitalbank"):
                z = zipfile.ZipFile(f)
                for n in z.namelist():
                    if n.endswith(".vital"):
                        yield from emit(os.path.basename(n)[:-6], z.read(n), f)

CATS = [("Bass", r"\b(ba|bass|sub|808|reese|wob)\b"), ("Lead", r"\b(ld|lead|saw lead)\b"), ("Pad", r"\b(pd|pad|atmo|atmos|drone|texture)\b"),
        ("Pluck", r"\b(pl|plk|pluck|mallet)\b"), ("Keys", r"\b(ky|key|keys|piano|ep|organ|bell|chord|ch)\b"),
        ("Sequence", r"\b(sq|seq|arp|sequence)\b"), ("SFX", r"\b(fx|sfx|riser|impact|noise)\b"), ("Brass/Vox", r"\b(br|brass|vox|voice|choir)\b")]

def category(name, d):
    style = (d.get("preset_style") or "").strip()
    if style: return style.title() if style.lower() != "sfx" else "SFX"
    n = re.sub(r"[\[\]_\-]", " ", name.lower())
    for c, rx in CATS:
        if re.search(rx, n): return c
    return "Other"

def g(s, k, default=0.0):
    v = s.get(k, default)
    return float(v) if isinstance(v, (int, float)) else default

FX = ["chorus", "compressor", "delay", "distortion", "eq", "filter_fx", "flanger", "phaser", "reverb"]
FX_MIX = {"chorus": "chorus_dry_wet", "delay": "delay_dry_wet", "distortion": "distortion_mix", "flanger": "flanger_dry_wet",
          "phaser": "phaser_dry_wet", "reverb": "reverb_dry_wet", "compressor": "compressor_mix", "filter_fx": "filter_fx_mix"}
# continuous controls compared by the no-copy check (raw knob values; ranges normalised in check_distance.py)
FEATURES = ["osc_%d_%s" % (o, p) for o in (1, 2, 3) for p in ("on", "level", "transpose", "tune", "unison_voices", "unison_detune",
             "stereo_spread", "unison_blend", "wave_frame", "distortion_type", "distortion_amount", "spectral_morph_type",
             "spectral_morph_amount", "pan")] + \
           ["filter_%s_%s" % (f, p) for f in ("1", "2") for p in ("on", "model", "style", "cutoff", "resonance", "drive", "blend", "keytrack", "mix")] + \
           ["env_%d_%s" % (e, p) for e in (1, 2, 3) for p in ("delay", "attack", "hold", "decay", "sustain", "release", "attack_power", "decay_power", "release_power")] + \
           ["lfo_%d_%s" % (l, p) for l in (1, 2) for p in ("frequency", "tempo", "sync", "sync_type", "smooth_time")] + \
           [x + "_on" for x in FX] + list(FX_MIX.values()) + \
           ["reverb_size", "reverb_decay_time", "delay_feedback", "distortion_drive", "distortion_type", "chorus_voices", "chorus_feedback",
            "volume", "polyphony", "portamento_time", "legato", "sub_on", "sub_level", "sample_on", "sample_level", "macro_control_1",
            "macro_control_2", "macro_control_3", "macro_control_4", "velocity_track", "stereo_routing"]

def med(xs): return round(statistics.median(xs), 3) if xs else None
def pct(n, d): return "%d%%" % round(100.0 * n / d) if d else "-"

by = defaultdict(list)
feats = []
for name, d, src in presets():
    s = d["settings"]
    c = category(name, d)
    by[c].append(s)
    mods = [(m.get("source", ""), m.get("destination", "")) for m in s.get("modulations", []) if m.get("source") and m.get("destination")]
    feats.append({"category": c, "values": {k: g(s, k) for k in FEATURES if k in s},
                  "mods": sorted(set("%s>%s" % m for m in mods))})

lines = ["# What the studied Vital presets have in common", "",
         "Aggregate numbers only (medians, rates, counts) from %d presets in the free Vital packs Brandon collected (kept private," % sum(len(v) for v in by.values()),
         "never in this repository). Excluded: W.A. Production and Ghosthack (their licences forbid instrument use) and ModeAudio",
         "(its licence limits use to music productions).",
         "No patch, wavetable or setting list from any preset is copied here or into Ghosty Pro. Vital stores envelope times as the",
         "4th root of seconds and cutoff as a MIDI note; the tables show the knob values and, for times, the seconds.", ""]
lines += ["| Category | Presets |", "|---|---|"] + ["| %s | %d |" % (c, len(v)) for c, v in sorted(by.items(), key=lambda x: -len(x[1]))] + [""]

for c, items in sorted(by.items(), key=lambda x: -len(x[1])):
    n = len(items)
    if n < 3: continue
    L = ["## %s (%d presets)" % (c, n), ""]
    oscs_on = [sum(g(s, "osc_%d_on" % o, 1.0 if o == 1 else 0.0) >= 0.5 for o in (1, 2, 3)) for s in items]
    L.append("- Oscillators on: median %s; 1 osc %s, 2 oscs %s, 3 oscs %s; sub on %s; sampler (noise) on %s." % (
        med(oscs_on), pct(oscs_on.count(1), n), pct(oscs_on.count(2), n), pct(oscs_on.count(3), n),
        pct(sum(g(s, "sub_on") >= 0.5 for s in items), n), pct(sum(g(s, "sample_on") >= 0.5 for s in items), n)))
    uni, det, spread, warp_t, morph_t, tables = [], [], [], Counter(), Counter(), Counter()
    comp_types = Counter()
    for s in items:
        for o in (1, 2, 3):
            if g(s, "osc_%d_on" % o, 1.0 if o == 1 else 0.0) < 0.5: continue
            uni.append(g(s, "osc_%d_unison_voices" % o, 1)); det.append(g(s, "osc_%d_unison_detune" % o, 4.472))
            spread.append(g(s, "osc_%d_stereo_spread" % o, 1))
            warp_t[int(g(s, "osc_%d_distortion_type" % o))] += 1; morph_t[int(g(s, "osc_%d_spectral_morph_type" % o))] += 1
        for w in s.get("wavetables", []):
            for grp in w.get("groups", []):
                for comp in grp.get("components", []):
                    comp_types[comp.get("type", "?")] += 1
    L.append("- Unison voices per active osc: median %s (1 voice in %s of oscs, 5+ in %s); detune knob median %s; stereo spread median %s." % (
        med(uni), pct(uni.count(1.0), len(uni)), pct(sum(u >= 5 for u in uni), len(uni)), med(det), med(spread)))
    L.append("- Wave warp type (Vital's list index) most used: %s; spectral morph type most used: %s." % (
        ", ".join("%d (%s)" % (k, pct(v, sum(warp_t.values()))) for k, v in warp_t.most_common(3)),
        ", ".join("%d (%s)" % (k, pct(v, sum(morph_t.values()))) for k, v in morph_t.most_common(3))))
    L.append("- Wavetable building blocks used (Vital component types, counts): %s." % ", ".join("%s %d" % kv for kv in comp_types.most_common(6)))
    f_on = [s for s in items if g(s, "filter_1_on") >= 0.5 or g(s, "filter_2_on") >= 0.5]
    models = Counter(int(g(s, "filter_1_model")) for s in items if g(s, "filter_1_on") >= 0.5)
    L.append("- Filter on: %s; filter 1 models (Vital index): %s; cutoff median %s (MIDI note), resonance median %s, drive median %s, keytrack median %s." % (
        pct(len(f_on), n), ", ".join("%d:%d" % kv for kv in models.most_common(4)),
        med([g(s, "filter_1_cutoff") for s in items if g(s, "filter_1_on") >= 0.5]),
        med([g(s, "filter_1_resonance") for s in items if g(s, "filter_1_on") >= 0.5]),
        med([g(s, "filter_1_drive") for s in items if g(s, "filter_1_on") >= 0.5]),
        med([g(s, "filter_1_keytrack") for s in items if g(s, "filter_1_on") >= 0.5])))
    secs = lambda v: round(v ** 4, 3)
    a = med([g(s, "env_1_attack", 0.1495) for s in items]); de = med([g(s, "env_1_decay", 1.0) for s in items])
    su = med([g(s, "env_1_sustain", 1.0) for s in items]); re_ = med([g(s, "env_1_release", 0.5476) for s in items])
    L.append("- Amp envelope (env 1) medians: attack %s s, decay %s s, sustain %s, release %s s." % (secs(a), secs(de), su, secs(re_)))
    nm = [sum(1 for m in s.get("modulations", []) if m.get("source") and m.get("destination")) for s in items]
    pairs, srcs, dst_kind = Counter(), Counter(), Counter()
    for s in items:
        for m in s.get("modulations", []):
            src, dst = m.get("source", ""), m.get("destination", "")
            if not src or not dst: continue
            sk = re.sub(r"_\d+$", "", src); dk = re.sub(r"^(osc|filter|env|lfo|random|modulation)_(\d+|fx)_", r"\1_", dst)
            srcs[sk] += 1; dst_kind[dk] += 1; pairs["%s -> %s" % (sk, dk)] += 1
    L.append("- Modulations per preset: median %s (max %s). Sources: %s." % (med(nm), max(nm) if nm else 0, ", ".join("%s %d" % kv for kv in srcs.most_common(6))))
    L.append("- Most common routings (kind of source -> kind of target): %s." % "; ".join("%s (%d)" % kv for kv in pairs.most_common(8)))
    amts = [abs(g(s, "modulation_%d_amount" % i)) for s in items for i in range(1, 65) if g(s, "modulation_%d_amount" % i) != 0.0]
    L.append("- Modulation depth (|amount|): median %s, 75th percentile %s." % (med(amts), round(sorted(amts)[int(0.75 * (len(amts) - 1))], 3) if amts else None))
    macros_used = [sum(1 for k in (1, 2, 3, 4) if any(m.get("source") == "macro_control_%d" % k for m in s.get("modulations", []))) for s in items]
    L.append("- Macros wired: median %s of 4; all four in %s of presets." % (med(macros_used), pct(macros_used.count(4), n)))
    fx = []
    for x in FX:
        on = [s for s in items if g(s, x + "_on") >= 0.5]
        if not on: continue
        mix = med([g(s, FX_MIX[x]) for s in on]) if x in FX_MIX else None
        fx.append("%s %s%s" % (x.replace("filter_fx", "fx filter"), pct(len(on), n), (" (mix %s)" % mix) if mix is not None else ""))
    L.append("- Effects on: " + ", ".join(fx) + ".")
    L.append("- Reverb size median %s, decay median %s; delay feedback median %s; distortion drive median %s." % (
        med([g(s, "reverb_size") for s in items if g(s, "reverb_on") >= 0.5]), med([g(s, "reverb_decay_time") for s in items if g(s, "reverb_on") >= 0.5]),
        med([g(s, "delay_feedback") for s in items if g(s, "delay_on") >= 0.5]), med([g(s, "distortion_drive") for s in items if g(s, "distortion_on") >= 0.5])))
    vols = [g(s, "volume", 5473.04) for s in items]
    L.append("- Master volume knob median %s (Vital's default 5473 = 0 dB on its scale); polyphony median %s; glide on in %s; legato in %s." % (
        med(vols), med([g(s, "polyphony", 8) for s in items]), pct(sum(g(s, "portamento_time", -10) > -9.9 for s in items), n), pct(sum(g(s, "legato") >= 0.5 for s in items), n)))
    lines += L + [""]

lines += ["## Lessons for Ghosty Pro's own presets", "",
          "(Drawn from the numbers above; every preset of ours is designed from scratch and checked to be far from every studied one.)", ""]
open(out_md, "w").write("\n".join(lines) + "\n")
json.dump(feats, open(out_feat, "w"))
print("studied", sum(len(v) for v in by.values()), "presets in", len(by), "categories")
