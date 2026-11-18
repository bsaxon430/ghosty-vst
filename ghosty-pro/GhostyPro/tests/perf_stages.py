#!/usr/bin/env python3
# Per-stage CPU split for `ProTests bench --only <scenario>` from a perf profile (test tooling only, not shipped).
#   perf record --call-graph dwarf -F 1000 -o s.data ProTests bench --only big-ultra --secs 5
#   perf script -i s.data -F comm,tid,time,ip,sym,srcline --inline | python3 perf_stages.py   (srcline: needs a -g build, e.g. RelWithDebInfo)
# Keeps only samples under ProProcessor::processBlock (the timed part; patch loading / table building is dropped), then gives
# each sample to the first stage found walking from the leaf up, and lists the top leaf functions.
import sys, re, collections

STAGES = [   # (stage, regex on a frame's symbol), checked leaf -> root
    ("ceiling / clean / limiter", r"TruePeak|Ceiling|Limiter|clean::|LevelMatch|applyVoiceLaw|matchStage|cleanLayerParams"),
    ("engine oversampling (decimator / align)", r"Decimator|AlignDelay|Upsampler"),
    ("filter + drive", r"FilterChain|Filter|Drive|Oversampler|Svf|Ladder|saturat"),
    ("FX (Ghost FX rack, verb, echo...)", r"gpfx::|GhostVerb|FxChain|ghosty::Fx|Hyper|Reverb|Chorus|Phaser|Echo|Haunt|Crush|Character|Tape"),
    ("oscillators + voice", r"ProVoice|MipFrame|Wavetable|warp|Warp|Unison|Noise|Sub"),
    ("synth / MIDI / mod", r"Synthesiser|Performer|Arp|Mod|Macro|Lfo|LFO|Envelope|ADSR"),
]

def main():
    total = 0
    stage = collections.Counter(); leaf = collections.Counter()
    stack = []; lines = []
    srcl = collections.Counter()
    def flush():
        nonlocal total
        if not stack: return
        if not any("ProProcessor::processBlock" in s for s in stack): return
        total += 1
        leaf[stack[0]] += 1
        if lines and lines[0]: srcl[lines[0]] += 1
        for s in stack:
            if "ProProcessor::processBlock" in s: stage["processBlock (own code, mixing, buffers)"] += 1; return
            for name, rx in STAGES:
                if re.search(rx, s): stage[name] += 1; return
        stage["other"] += 1
    for line in sys.stdin:
        if not line.strip():
            flush(); stack = []; lines = []; continue
        if line.startswith("\t"):
            parts = line.strip().split(None, 1)
            if len(parts) == 2:
                sym = re.sub(r"\+0x[0-9a-f]+ \(.*\)$", "", parts[1]).strip()
                sym = re.sub(r" \(.*\)$", "", sym)
                stack.append(sym); lines.append("")
        elif line.startswith("  ") and lines:
            lines[-1] = line.strip().replace(" (inlined)", "")
    flush()
    if total == 0:
        print("no samples under ProProcessor::processBlock"); return
    print(f"samples under processBlock: {total}")
    print("| Stage | share of processBlock time |\n|---|---:|")
    for k, v in stage.most_common(): print(f"| {k} | {100.0 * v / total:.1f} % |")
    print("\n| Hot leaf function | share |\n|---|---:|")
    for k, v in leaf.most_common(10):
        k = k if len(k) < 110 else k[:107] + "..."
        print(f"| `{k}` | {100.0 * v / total:.1f} % |")
    if srcl:
        print("\n| Hot source line (leaf) | share |\n|---|---:|")
        for k, v in srcl.most_common(12): print(f"| `{k}` | {100.0 * v / total:.1f} % |")

main()
