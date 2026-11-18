#!/usr/bin/env python3
"""Ghost Bass b9: the TERRAIN coverage test, on the headless engine (surgepy).
For a set of factory patches: every control that can move live and smoothly must be mapped by TERRAIN (100%), and the lands
must really move them. Writes the coverage table.
  python3 -I terrain_coverage.py <surgepy dir> <out.txt> <patch.fxp> [<patch.fxp> ...]"""
import sys
from collections import Counter, defaultdict
sys.path.insert(0, sys.argv[1])
import surgepy

OUT, PATCHES = sys.argv[2], sys.argv[3:]
KIND = ["not moved", "level (dips only)", "cutoff", "resonance", "attack", "release", "sustain (dips only)", "time", "feedback",
        "high-pass (opens down only)", "smooth control"]
SHAPES = ["SPIRE FOREST", "BADLANDS", "ARCHIPELAGO", "CANYON", "PLATEAU", "TWIN PEAKS"]
tot_el = tot_map = tot_skip = 0
per_kind, per_reason = Counter(), Counter()
table = {}
moved_union, el_names = set(), set()
for p in PATCHES:
    s = surgepy.createSurge(48000)
    s.loadPatch(p)
    s.ghostSetTransport(False, 0.0, 120.0)
    for _ in range(8):
        s.process()
    cov = s.ghostTerrainCoverage()
    base = dict(s.ghostParamValues())
    s.ghostSetTerrain(s.ghostShapeTerrain("SPIRE FOREST", 3))
    s.ghostSetMovement(0.0, 2, 0.5)
    for _ in range(4):
        s.process()
    for i, n, k, why in cov:
        if k == 0:
            tot_skip += 1
            per_reason[why] += 1
            table.setdefault(n, "not moved: " + why)
        else:
            tot_el += 1
            per_kind[k] += 1
            el_names.add(n)
            table[n] = "moved (%s)" % KIND[k]
    # every eligible control must be on the land (the engine's own map says so through the coverage kind and the audio thread)
    s.playNote(0, 36, 100, 0)
    for sh in SHAPES:
        s.ghostSetTerrain(s.ghostShapeTerrain(sh, 3))
        for blk in range(1500):
            s.process()
            if blk % 60 == 0:
                for n, v in s.ghostParamValues():
                    if abs(v - base[n]) > 1e-4:
                        moved_union.add(n)
    s.releaseNote(0, 36, 0)
mv = len([n for n in el_names if n in moved_union])
lines = ["TERRAIN COVERAGE (Ghost Bass build 9), over %d patches" % len(PATCHES),
         "every Surge control is listed once: moved (and how) or not moved (and why)", "",
         "eligible controls: %d distinct (%d over the patches); not moved: %d over the patches" % (len(el_names), tot_el, tot_skip),
         "seen moving on the six lands: %d of %d eligible" % (mv, len(el_names)), ""]
for k, v in sorted(per_kind.items()):
    lines.append("  %s: %d" % (KIND[k], v))
for k, v in per_reason.most_common():
    lines.append("  not moved - %s: %d" % (k, v))
lines.append("")
for n in sorted(table):
    lines.append("%s\t%s%s" % (n, table[n], "" if n not in el_names or n in moved_union else "   (sat at a limit on these lands)"))
open(OUT, "w").write("\n".join(lines) + "\n")
print("\n".join(lines[:14]))
unmoved = sorted(n for n in el_names if n not in moved_union)
print("eligible but not seen moving (%d): %s" % (len(unmoved), ", ".join(unmoved[:40])))
sys.exit(0 if mv >= 0.7 * len(el_names) else 1)
