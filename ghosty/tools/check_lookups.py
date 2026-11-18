#!/usr/bin/env python3
"""Every stepped Vital parameter with a name list must have a name for every value (min..max), or a host asking for a
value's name reads past the end of the list (undefined behaviour: the Intel-Mac AU crash of Ghosty Pro 2 build 7).
Usage: check_lookups.py <repo root> [params.tsv from "GhostyProTool dump-params"]. With the tsv, the real min / max of
every parameter (templates expanded, C++ constants resolved) are checked too. Exit 1 if any list is too short."""
import re, sys, os
root = sys.argv[1] if len(sys.argv) > 1 else "."
sizes = {}
for path in ("src/interface/look_and_feel/synth_strings.h", "src/common/synth_parameters.cpp"):
    src = open(os.path.join(root, path)).read()
    for m in re.finditer(r'(k\w+)\[[^\]]*\]\s*=\s*\{(.*?)\};', src, re.S):
        sizes[m.group(1)] = len(re.findall(r'"(?:[^"\\]|\\.)*"', m.group(2)))
params = open(os.path.join(root, "src/common/synth_parameters.cpp")).read()
entry = re.compile(r'\{\s*"([a-z_0-9]+)",\s*0x[0-9a-fA-F]+,\s*([^,]+),\s*([^,]+),[^{}]*?ValueDetails::(\w+),[^{}]*?(?:strings::)?(k\w+)\s*\}', re.S)
consts = {"RandomLfo::kNumStyles": None, "kNumFilterModels": None}
bad, checked = 0, 0
for m in entry.finditer(params):
    name, lo, hi, scale, lookup = m.groups()
    if scale != "kIndexed" or lookup not in sizes:
        continue
    try:
        top = float(eval(hi.strip(), {}, {"RandomLfo": type("R", (), {"kNumStyles": 0})}))  # simple numbers only
    except Exception:
        continue   # max from a C++ constant: those lists are sized by the same constant
    checked += 1
    if top + 1 > sizes[lookup]:
        print("TOO SHORT: %s max %g, %s has %d names" % (name, top, lookup, sizes[lookup]))
        bad += 1
if len(sys.argv) > 2:
    # Vital's parameter groups: the same short name ("style", "sync") means different things in different groups
    groups = [("parameter_list", r"^{}$"), ("env_parameter_list", r"^env_\d+_{}$"), ("lfo_parameter_list", r"^lfo_\d+_{}$"),
              ("random_lfo_parameter_list", r"^random_\d+_{}$"), ("filter_parameter_list", r"^filter_(\d+|fx)_{}$"),
              ("osc_parameter_list", r"^osc_\d+_{}$"), ("mod_parameter_list", r"^modulation_\d+_{}$")]
    bounds = [(params.find("ValueDetailsLookup::" + g + "[]"), pat) for g, pat in groups]
    bounds.sort()
    rules = []
    for m in entry.finditer(params):
        name, lo, hi, scale, lookup = m.groups()
        if scale != "kIndexed" or lookup not in sizes:
            continue
        pat = None
        for pos, gp in bounds:
            if 0 <= pos <= m.start(): pat = gp
        if pat:
            rules.append((re.compile(pat.format(re.escape(name))), lookup))
    for line in open(sys.argv[2]):
        f = line.rstrip("\n").split("\t")
        if len(f) < 5 or int(f[4]) != 0:   # 0 = kIndexed
            continue
        full, top = f[0], float(f[2])
        for rx, lookup in rules:
            if rx.match(full):
                checked += 1
                if top + 1 > sizes[lookup]:
                    print("TOO SHORT: %s max %g, %s has %d names" % (full, top, lookup, sizes[lookup]))
                    bad += 1
print("name lists: %d stepped parameters checked, %d too short" % (checked, bad))
sys.exit(1 if bad else 0)
