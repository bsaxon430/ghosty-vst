#!/usr/bin/env python3
"""Measure every A/B proof clip the same way: melody kept, identity, artifacts, loudness, speed.

  measure.py --voices VOICES_DIR --seedvc SEEDVC_DIR [--stock STOCK_DIR] --dsp DSP_DIR \
             --seed-vc-root SEED_VC --campplus CKPT --out RESULTS_DIR

  pitch     the lab's YIN proxy (ghostvoice/signal_metrics.pitch_similarity) of each clip against the source performance:
            median |cents| off and the share of voiced frames within 100 cents (a semitone)
  identity  CAMPPlus 192-D cosine (Seed-VC's own identity encoder, the lab's enrolment) of each clip to voice A (both takes),
            voice B (both takes) and to the SOURCE singer (leak: how much of the performer's own voice is left).
            posAB = (1 - cosA) / ((1 - cosA) + (1 - cosB)): 0 = sits on A, 1 = sits on B. Same encoder family as the renderer,
            so a second, independent opinion (Resemblyzer GE2E, Apache-2.0) is added when it installs.
  artifacts clicks / s (2 ms blocks above 6 kHz jumping 18 dB over their 60 ms neighbourhood, the source's own rate shown too),
            buzz200, HNR, flatness, flutter, true peak, clip % (docs/research/tools/ghostvoice_measure.py, our own tool)
  loudness  ITU-R BS.1770 integrated LUFS
  speed     seconds per render and real-time factor (render seconds / audio seconds; < 1 = faster than real time)
"""
import argparse
import glob
import importlib.util
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
from scipy import signal

HERE = Path(__file__).resolve().parent
LAB = HERE.parent / 'lab'
sys.path.insert(0, str(LAB))
from ghostvoice.signal_metrics import pitch_similarity  # noqa: E402

_spec = importlib.util.spec_from_file_location('gvm', str(HERE.parents[1] / 'docs/research/tools/ghostvoice_measure.py'))
gvm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gvm)

MIXES = ['anchor_a', 'fusion_025', 'fusion_050', 'fusion_075', 'anchor_b']


def clicks_per_second(x, fs):
    if fs < 16000:
        return float('nan')
    sos = signal.butter(4, min(6000, fs * 0.4), btype='high', fs=fs, output='sos')
    h = signal.sosfilt(sos, x)
    blk = max(1, int(0.002 * fs))
    n = len(h) // blk
    if n < 60:
        return float('nan')
    e = 10 * np.log10(np.array([np.mean(h[i * blk:(i + 1) * blk] ** 2) for i in range(n)]) + 1e-14)
    floor = signal.medfilt(e, 31)  # ~60 ms neighbourhood
    hits = (e - floor > 18) & (e > e.max() - 50)
    events = np.sum(hits[1:] & ~hits[:-1]) + int(hits[0])
    return float(events / (len(x) / fs))


def find_clips(seedvc, stock, dsp):
    clips = {}
    for m in MIXES:
        w = sorted(glob.glob(str(Path(seedvc) / m / '*.wav')))
        if w:
            clips[f'seedvc_{m}'] = w[0]
    if stock:
        for m in ('stock_a', 'stock_b'):
            w = sorted(glob.glob(str(Path(stock) / m / '*.wav')))
            if w:
                clips[f'seedvc_{m}'] = w[0]
    for eng in ('natural', 'classic'):
        for m in MIXES:
            f = Path(dsp) / f'{eng}_{m}.wav'
            if f.is_file():
                clips[f'{eng}_{m}'] = str(f)
    return clips


def seedvc_speed(seedvc, stock):
    """seconds of the whole process (model load + render) from experiment.json, the pure conversion RTF from the upstream log"""
    out = {}
    exp = Path(seedvc) / 'experiment.json'
    if exp.is_file():
        for item in json.loads(exp.read_text()).get('renders', []):
            out[item['job_id']] = {'process_seconds': item.get('seconds')}
    for d in [Path(seedvc)] + ([Path(stock)] if stock else []):
        for log in glob.glob(str(d / '*' / 'inference.log')):
            job = Path(log).parent.name
            m = re.findall(r'RTF:\s*([0-9.]+)', Path(log).read_text(errors='replace'))
            if m:
                out.setdefault(job, {})['rtf'] = float(m[-1])
        for t in glob.glob(str(d / '*' / 'seconds.txt')):
            out.setdefault(Path(t).parent.name, {})['process_seconds'] = float(Path(t).read_text().strip())
    return out


def campplus_scores(args, clips, voices):
    import torch
    from ghostvoice.seedvc_style import load_seedvc_campplus, enroll_campplus_identity, encode_campplus_reference
    model = load_seedvc_campplus(args.seed_vc_root, args.campplus, 'cpu')
    A, _ = enroll_campplus_identity([voices / 'A1.wav', voices / 'A2.wav'], model, 'cpu')
    B, _ = enroll_campplus_identity([voices / 'B1.wav', voices / 'B2.wav'], model, 'cpu')
    S, _ = encode_campplus_reference(voices / 'source.wav', model, 'cpu')
    cos = lambda u, v: float(torch.nn.functional.cosine_similarity(u, v, dim=-1).item())
    out = {'_refs': {'a_to_b': cos(A, B), 'a_to_source': cos(A, S), 'b_to_source': cos(B, S)}}
    for k, f in clips.items():
        try:
            c, _ = encode_campplus_reference(f, model, 'cpu')
            ca, cb = cos(c, A), cos(c, B)
            out[k] = {'cos_a': ca, 'cos_b': cb, 'cos_source': cos(c, S), 'posAB': (1 - ca) / max(1e-9, (1 - ca) + (1 - cb))}
        except Exception as exc:
            out[k] = {'error': str(exc)}
    return out


def resemblyzer_scores(clips, voices):
    try:
        from resemblyzer import VoiceEncoder, preprocess_wav
    except Exception as exc:
        return {'_status': f'not installed: {exc}'}
    enc = VoiceEncoder('cpu')
    emb = lambda f: enc.embed_utterance(preprocess_wav(str(f)))
    A = (emb(voices / 'A1.wav') + emb(voices / 'A2.wav')) / 2
    B = (emb(voices / 'B1.wav') + emb(voices / 'B2.wav')) / 2
    S = emb(voices / 'source.wav')
    cos = lambda u, v: float(np.dot(u, v) / (np.linalg.norm(u) * np.linalg.norm(v) + 1e-9))
    out = {'_status': 'ok', '_refs': {'a_to_b': cos(A, B), 'a_to_source': cos(A, S), 'b_to_source': cos(B, S)}}
    for k, f in clips.items():
        try:
            c = emb(f)
            ca, cb = cos(c, A), cos(c, B)
            out[k] = {'cos_a': ca, 'cos_b': cb, 'cos_source': cos(c, S), 'posAB': (1 - ca) / max(1e-9, (1 - ca) + (1 - cb))}
        except Exception as exc:
            out[k] = {'error': str(exc)}
    return out


def fmt(v, f='%.2f'):
    return '-' if v is None or (isinstance(v, float) and not math.isfinite(v)) else (f % v)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--voices', type=Path, required=True)
    p.add_argument('--seedvc', required=True)
    p.add_argument('--stock')
    p.add_argument('--dsp', required=True)
    p.add_argument('--seed-vc-root', required=True)
    p.add_argument('--campplus', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    clips = find_clips(a.seedvc, a.stock, a.dsp)
    src = a.voices / 'source.wav'
    fs_s, xs = gvm.read_mono(str(src))
    res = {'source': {'clicks_per_s': clicks_per_second(xs, fs_s), **gvm.measure(str(src))}, 'clips': {}}
    for k, f in clips.items():
        fs, x = gvm.read_mono(f)
        r = {'file': f, 'seconds': len(x) / fs, 'sample_rate': fs}
        r.update(gvm.measure(f))
        r['clicks_per_s'] = clicks_per_second(x, fs)
        try:
            r['pitch'] = pitch_similarity(src, f)
        except Exception as exc:
            r['pitch'] = {'status': 'error', 'reason': str(exc)}
        res['clips'][k] = r
    res['campplus'] = campplus_scores(a, clips, a.voices)
    res['resemblyzer'] = resemblyzer_scores(clips, a.voices)
    # speed
    sp = seedvc_speed(a.seedvc, a.stock)
    tim = json.loads((Path(a.dsp) / 'timings.json').read_text()) if (Path(a.dsp) / 'timings.json').is_file() else {}
    src_s = len(xs) / fs_s
    for k, r in res['clips'].items():
        eng, job = k.split('_', 1)
        if eng == 'seedvc':
            s = sp.get(job, {})
            r['render_process_seconds'] = s.get('process_seconds')
            r['rtf_conversion_only'] = s.get('rtf')
        else:
            ms = tim.get(f'{eng}_{job}_ms')
            r['render_process_seconds'] = (ms / 1000.0) if ms is not None else None
            r['rtf_conversion_only'] = (ms / 1000.0 / src_s) if ms is not None else None
    res['source_seconds'] = src_s
    res['seedvc_speed'] = sp
    (a.out / 'results.json').write_text(json.dumps(res, indent=2, default=float) + '\n')

    # the plain table
    L = ['| clip | melody: median cents off | within a semitone | CAMPPlus to A | to B | to source | A..B (0..1) | 2nd opinion A..B | clicks/s | buzz dB | HNR dB | flutter | LUFS | true peak | render s | RTF |',
         '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    rz = res['resemblyzer']
    L.append(f"| (source performance) | 0 | 1.00 | {fmt(res['campplus']['_refs']['a_to_source'])} | {fmt(res['campplus']['_refs']['b_to_source'])} | 1.00 | - | - | "
             f"{fmt(res['source']['clicks_per_s'])} | {fmt(res['source']['buzz200'], '%.1f')} | {fmt(res['source']['HNR'], '%.1f')} | {fmt(res['source']['flutter'])} | {fmt(res['source']['LUFS'], '%.1f')} | {fmt(res['source']['TP'], '%.1f')} | - | - |")
    for k, r in res['clips'].items():
        pc = r['pitch'] if r['pitch'].get('status') == 'ok' else {}
        c = res['campplus'].get(k, {})
        z = rz.get(k, {}) if isinstance(rz.get(k), dict) else {}
        L.append(f"| {k} | {fmt(pc.get('median_absolute_cents'), '%.0f')} | {fmt(pc.get('within_100_cents_fraction'))} | {fmt(c.get('cos_a'))} | {fmt(c.get('cos_b'))} | {fmt(c.get('cos_source'))} | {fmt(c.get('posAB'))} | {fmt(z.get('posAB'))} | "
                 f"{fmt(r['clicks_per_s'])} | {fmt(r['buzz200'], '%.1f')} | {fmt(r['HNR'], '%.1f')} | {fmt(r['flutter'])} | {fmt(r['LUFS'], '%.1f')} | {fmt(r['TP'], '%.1f')} | {fmt(r['render_process_seconds'], '%.1f')} | {fmt(r['rtf_conversion_only'])} |")
    md = '\n'.join(L) + '\n'
    md += f"\nCAMPPlus between the singers: A-B {fmt(res['campplus']['_refs']['a_to_b'])}. Second opinion (Resemblyzer): {rz.get('_status')}"
    if '_refs' in rz:
        md += f", A-B {fmt(rz['_refs']['a_to_b'])}"
    md += f".\nSource clip: {src_s:.1f} s.\n"
    (a.out / 'results.md').write_text(md)
    print(md)


if __name__ == '__main__':
    main()
