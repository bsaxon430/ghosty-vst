#!/usr/bin/env python3
"""Scores transcriptions against ground truth. Datasets live OUTSIDE the repo (CC BY 4.0; see tests/bench/README.md).

  score.py melody <vocadito_dir> <system_dir> [<system_dir> ...]
      per system: frame-level RPA / RCA / VR / VFA / OA (mir_eval.melody) against the F0 annotations, note-level P/R/F with onset 50 ms +
      pitch 50 cents (COnP), + offsets (COnPOff), onset-only (COn), against both annotators (mean); median |onset| / |offset| error of the
      matched notes (ms), median |pitch| error (cents, exact pitch incl. the note's cents), octave errors (chroma right, pitch wrong,
      per matched-by-onset pair), fragments (estimated notes starting strictly inside a reference note that are not its match, per
      reference note), and speed (ms of work per second of audio).
  score.py beat <avp_dir> <system_dir> [...]
      per system: hits found / extra (30 ms window), onset error, the right drum (4 classes: kd sd hhc hho), confusion, per-file split
      Personal / Fixed / Improvisation, speed.
A system dir holds <track>.json files in HumBench / refs.py format.
"""
import sys, os, json, glob, re
import numpy as np
import mir_eval

def hz_to_midi(f): return 69.0 + 12.0 * np.log2(np.maximum(np.asarray(f, float), 1e-9) / 440.0)

# ---------------------------------------------------------------- melody
def load_notes_csv(p):
    rows = [l.strip().split(",") for l in open(p) if l.strip()]
    on = np.array([float(r[0]) for r in rows]); hz = np.array([float(r[1]) for r in rows]); dur = np.array([float(r[2]) for r in rows])
    return on, dur, hz

def load_f0_csv(p):
    a = np.loadtxt(p, delimiter=","); return a[:, 0], a[:, 1]

def est_arrays(notes):
    if not notes: return np.zeros((0, 2)), np.zeros(0)
    n = np.array(notes, float); iv = np.stack([n[:, 0], n[:, 0] + n[:, 1]], 1)
    hz = 440.0 * 2 ** ((n[:, 2] + n[:, 3] / 100.0 - 69) / 12.0)
    return iv, hz

def score_melody_track(vdir, tid, est):
    out = {}
    # frames
    rt, rf = load_f0_csv(os.path.join(vdir, "Annotations", "F0", f"vocadito_{tid}_f0.csv"))
    fr = np.array(est["frames"], float) if est.get("frames") else np.zeros((0, 4))
    if len(fr):
        et, ef = fr[:, 0], fr[:, 1]
        (rv, rc, ev, ec) = mir_eval.melody.to_cent_voicing(rt, rf, et, ef, hop=None)
        out["VR"], out["VFA"] = mir_eval.melody.voicing_measures(rv, ev)
        out["RPA"] = mir_eval.melody.raw_pitch_accuracy(rv, rc, ev, ec); out["RCA"] = mir_eval.melody.raw_chroma_accuracy(rv, rc, ev, ec)
        out["OA"] = mir_eval.melody.overall_accuracy(rv, rc, ev, ec)
        # cents error of the voiced frames both agree on
        both = rv.astype(bool) & ev.astype(bool); d = np.abs(rc[both] - ec[both]); out["frame_cents"] = float(np.median(d[d < 100])) if np.any(d < 100) else np.nan
    # notes against both annotators
    ei, eh = est_arrays(est["notes"]); acc = {}
    for ann in ("A1", "A2"):
        ron, rdur, rhz = load_notes_csv(os.path.join(vdir, "Annotations", "Notes", f"vocadito_{tid}_notes{ann}.csv")); ri = np.stack([ron, ron + rdur], 1)
        if len(ei) == 0:
            for k in ("COnP_F", "COnPOff_F", "COn_F", "COnP_P", "COnP_R"): acc.setdefault(k, []).append(0.0)
            acc.setdefault("frag", []).append(0.0); acc.setdefault("oct", []).append(0.0); continue
        p, r, f, _ = mir_eval.transcription.precision_recall_f1_overlap(ri, rhz, ei, eh, onset_tolerance=0.05, pitch_tolerance=50.0, offset_ratio=None)
        acc.setdefault("COnP_F", []).append(f); acc.setdefault("COnP_P", []).append(p); acc.setdefault("COnP_R", []).append(r)
        _, _, f2, _ = mir_eval.transcription.precision_recall_f1_overlap(ri, rhz, ei, eh, onset_tolerance=0.05, pitch_tolerance=50.0, offset_ratio=0.2, offset_min_tolerance=0.05)
        acc.setdefault("COnPOff_F", []).append(f2)
        _, _, f3 = mir_eval.transcription.onset_precision_recall_f1(ri, ei, onset_tolerance=0.05)
        acc.setdefault("COn_F", []).append(f3)
        m = mir_eval.transcription.match_notes(ri, rhz, ei, eh, onset_tolerance=0.05, pitch_tolerance=50.0, offset_ratio=None)
        if m:
            mi = np.array(m)
            acc.setdefault("on_ms", []).extend(np.abs(ei[mi[:, 1], 0] - ri[mi[:, 0], 0]) * 1000)
            acc.setdefault("off_ms", []).extend(np.abs(ei[mi[:, 1], 1] - ri[mi[:, 0], 1]) * 1000)
            acc.setdefault("cents", []).extend(np.abs(hz_to_midi(eh[mi[:, 1]]) - hz_to_midi(rhz[mi[:, 0]])) * 100)
        # octave errors: matched by onset only, pitch off by ~an octave (or two)
        mo = mir_eval.transcription.match_note_onsets(ri, ei, onset_tolerance=0.05); octs = 0
        for a, b in mo:
            d = abs(hz_to_midi(eh[b]) - hz_to_midi(rhz[a])) * 100
            if abs(d - 1200) < 50 or abs(d - 2400) < 50: octs += 1
        acc.setdefault("oct", []).append(octs / max(1, len(ri)))
        # fragments: estimated notes that start strictly inside a reference note (> 50 ms after its onset, > 30 ms before its end) and are not the note matched to it
        matched_est = set(b for _, b in m); frag = 0
        for k in range(len(ri)):
            a0, a1 = ri[k]
            inside = [j for j in range(len(ei)) if ei[j, 0] > a0 + 0.05 and ei[j, 0] < a1 - 0.03 and j not in matched_est]
            frag += len(inside)
        acc.setdefault("frag", []).append(frag / max(1, len(ri)))
    for k, v in acc.items(): out[k] = float(np.median(v)) if k in ("on_ms", "off_ms", "cents") else float(np.mean(v))
    out["ms_per_s"] = est.get("ms", 0) / max(1e-9, est.get("seconds", 1) if est.get("seconds") else rt[-1])
    return out

def score_melody(vdir, sysdirs):
    cols = ["RPA", "RCA", "VR", "VFA", "OA", "frame_cents", "COnP_F", "COnPOff_F", "COn_F", "COnP_P", "COnP_R", "on_ms", "off_ms", "cents", "oct", "frag", "ms_per_s"]
    print("%-14s" % "system" + "".join("%9s" % c for c in cols))
    results = {}
    for sd in sysdirs:
        rows = []
        for tid in range(1, 41):
            p = os.path.join(sd, f"vocadito_{tid}.json")
            if not os.path.exists(p): continue
            rows.append(score_melody_track(vdir, tid, json.load(open(p))))
        if not rows: print("%-14s (nothing)" % os.path.basename(sd.rstrip("/"))); continue
        agg = {c: float(np.nanmean([r.get(c, np.nan) for r in rows])) for c in cols}; agg["tracks"] = len(rows); results[os.path.basename(sd.rstrip("/"))] = agg
        print("%-14s" % os.path.basename(sd.rstrip("/")) + "".join(("%9.3f" if c not in ("on_ms", "off_ms", "cents", "frame_cents", "ms_per_s") else "%9.1f") % agg[c] for c in cols) + "  (%d tracks)" % len(rows))
    return results

# ---------------------------------------------------------------- beatbox
AVP_CLASSES = {"kd": 0, "sd": 1, "hhc": 2, "hho": 3}
NAMES = ["KICK", "SNARE", "HI-HAT", "OPEN HAT", "CLAP"]

def load_avp(csvp):
    """AVP csv: time,class,... some lines run together; a regex pulls (time, class) pairs."""
    txt = open(csvp, errors="ignore").read()
    out = []
    for m in re.finditer(r"(\d+\.?\d*(?:e-?\d+)?)\s*,\s*(kd|sd|hhc|hho)", txt):
        out.append((float(m.group(1)), AVP_CLASSES[m.group(2)]))
    out.sort(); return out

def score_beat(avp_dir, sysdirs, win=0.03):
    files = sorted(glob.glob(os.path.join(avp_dir, "AVP_Dataset", "*", "*", "*.wav")))
    print("%-14s %7s %7s %7s %7s %7s | %6s %6s %6s %6s | %8s %8s %8s %8s | %7s" % ("system", "truth", "found%", "extra%", "on_ms", "right%", "kd", "sd", "hhc", "hho", "Personal", "Fixed", "Improv", "Single", "ms/s"))
    results = {}
    for sd in sysdirs:
        truth = found = extra = 0; right = 0; on_err = []; conf = np.zeros((4, 5), int); per = {}; ms = 0.0; sec = 0.0
        for wav in files:
            key = os.path.splitext(os.path.basename(wav))[0]; p = os.path.join(sd, key + ".json")
            if not os.path.exists(p): continue
            est = json.load(open(p)); ref = load_avp(os.path.splitext(wav)[0] + ".csv")
            hits = sorted(est["hits"]); used = [False] * len(hits); ms += est.get("ms", 0); sec += est.get("seconds", 0)
            kind = "Improv" if "Improvisation" in key else "Single"; subset = "Personal" if "Personal" in key else "Fixed"
            for t, c in ref:
                truth += 1; best = -1; bd = 1e9
                for j, h in enumerate(hits):
                    if not used[j] and abs(h[0] - t) < bd: bd = abs(h[0] - t); best = j
                for k in (kind, subset): per.setdefault(k, [0, 0, 0])[0] += 1
                if best >= 0 and bd <= win:
                    used[best] = True; found += 1; on_err.append(bd * 1000); d = int(hits[best][1]); conf[c, min(d, 4)] += 1
                    for k in (kind, subset): per[k][1] += 1
                    if d == c:
                        right += 1
                        for k in (kind, subset): per[k][2] += 1
            extra += sum(1 for u in used if not u)
        if truth == 0: print("%-14s (nothing)" % os.path.basename(sd.rstrip("/"))); continue
        name = os.path.basename(sd.rstrip("/"))
        pc = {k: 100.0 * conf[i, i] / max(1, conf[i].sum()) for k, i in AVP_CLASSES.items()}
        pr = {k: 100.0 * v[2] / max(1, v[1]) for k, v in per.items()}
        print("%-14s %7d %7.1f %7.1f %7.1f %7.1f | %6.1f %6.1f %6.1f %6.1f | %8.1f %8.1f %8.1f %8.1f | %7.1f" % (name, truth, 100.0 * found / truth, 100.0 * extra / truth, float(np.median(on_err)) if on_err else 0, 100.0 * right / max(1, found), pc["kd"], pc["sd"], pc["hhc"], pc["hho"], pr.get("Personal", 0), pr.get("Fixed", 0), pr.get("Improv", 0), pr.get("Single", 0), ms / max(1e-9, sec)))
        print("    confusion (rows = truth kd sd hhc hho; cols = KICK SNARE HI-HAT OPEN HAT CLAP):"); [print("      %-4s %s" % (k, " ".join("%5d" % v for v in conf[i]))) for k, i in AVP_CLASSES.items()]
        results[name] = {"truth": truth, "found": 100.0 * found / truth, "extra": 100.0 * extra / truth, "right": 100.0 * right / max(1, found), "per_class": pc, "per_split": pr, "conf": conf.tolist(), "ms_per_s": ms / max(1e-9, sec)}
    return results

if __name__ == "__main__":
    what = sys.argv[1]
    r = score_melody(sys.argv[2], sys.argv[3:]) if what == "melody" else score_beat(sys.argv[2], sys.argv[3:])
    if os.environ.get("SCORE_JSON"): json.dump(r, open(os.environ["SCORE_JSON"], "w"), indent=1)
