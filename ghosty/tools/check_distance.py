#!/usr/bin/env python3
"""No-copy check: every Ghosty Pro preset must be far from every studied preset in Vital's parameter space.

  check_distance.py <private-features.json> <params.tsv> <ghosty/presets> <out.md>

<private-features.json> comes from study_presets.py (kept outside the repository). <params.tsv> is `GhostyProTool dump-params`
(name, min, max, default, scale). Distance between two presets = 0.8 x the mean range-normalised difference over the
studied controls (oscillators, filters, envelopes, LFOs, effects, levels, macros) + 0.2 x (1 - Jaccard overlap of their
modulation routings). A preset fails when it is closer to any studied preset than the threshold: the larger of 0.08 and
the 25th percentile of the distances between each studied preset and its nearest OTHER studied preset (so ours must be
further from every studied sound than most of those sounds are from each other). Writes <out.md> with numbers only."""
import json, os, sys, statistics

feat_path, params_path, presets_dir, out_md = sys.argv[1:5]
ranges, defaults = {}, {}
for line in open(params_path):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 4:
        ranges[p[0]] = max(1e-9, float(p[2]) - float(p[1])); defaults[p[0]] = float(p[3])
studied = json.load(open(feat_path))
keys = sorted({k for s in studied for k in s["values"]} & set(ranges))

def vec(values): return [values.get(k, defaults.get(k, 0.0)) for k in keys]
def dist(a, b):
    pa = sum(abs(x - y) / ranges[k] for k, x, y in zip(keys, a["v"], b["v"])) / len(keys)
    ma, mb = set(a["mods"]), set(b["mods"])
    jac = len(ma & mb) / len(ma | mb) if (ma | mb) else 1.0
    return 0.8 * min(1.0, pa * 4.0) + 0.2 * (1.0 - jac)   # x4: a typical difference of a quarter of the range counts as "fully different"

S = [{"v": vec(s["values"]), "mods": s["mods"], "cat": s["category"]} for s in studied]
nn = []
for i, a in enumerate(S):
    nn.append(min(dist(a, b) for j, b in enumerate(S) if j != i))
threshold = max(0.08, sorted(nn)[len(nn) // 4])

ours = []
for f in sorted(os.listdir(presets_dir)):
    if not f.endswith(".vital"): continue
    d = json.load(open(os.path.join(presets_dir, f)))
    s = d["settings"]
    vals = {k: float(s[k]) for k in keys if k in s and isinstance(s[k], (int, float))}
    mods = sorted({"%s>%s" % (m["source"], m["destination"]) for m in s.get("modulations", []) if m.get("source") and m.get("destination")})
    o = {"v": vec(vals), "mods": mods}
    best = min(((dist(o, b), b["cat"]) for b in S), key=lambda x: x[0])
    ours.append((f[:-6], d.get("preset_style", ""), best[0], best[1]))

fails = [o for o in ours if o[2] < threshold]
L = ["# Ghosty Pro presets vs the studied presets (no-copy check)", "",
     "Distance 0 = the same patch; 1 = nothing in common. Studied presets: %d (Brandon's free packs, minus the excluded licences)." % len(S),
     "Between the studied presets themselves, the distance to the nearest other studied preset is: median %.3f, 25th percentile %.3f, minimum %.3f." % (
         statistics.median(nn), sorted(nn)[len(nn) // 4], min(nn)),
     "Threshold: %.3f. Every Ghosty Pro preset must be further than that from EVERY studied preset." % threshold, "",
     "| Ghosty Pro preset | Category | Nearest studied preset: distance | that preset's category | Result |", "|---|---|---|---|---|"]
for name, cat, d, c in ours:
    L.append("| %s | %s | %.3f | %s | %s |" % (name.split(" ", 1)[-1], cat, d, c, "far enough" if d >= threshold else "TOO CLOSE"))
L += ["", "Result: %d of %d presets pass; closest distance %.3f." % (len(ours) - len(fails), len(ours), min(o[2] for o in ours) if ours else 0)]
open(out_md, "w").write("\n".join(L) + "\n")
print("\n".join(L[-1:]))
sys.exit(1 if fails else 0)
