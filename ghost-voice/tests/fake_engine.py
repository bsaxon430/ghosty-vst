#!/usr/bin/env python3
# Ghost Voice tests: a stand-in for helper/ghostvoice_engine.py that speaks the same protocol without any model.
# render: writes one WAV per mix (a 220 Hz sine whose level is 0.1 + 0.8 * mix, so the MOTION crossfade can be checked), reports
# a made-up "measurement" that the test then checks is shown as given. analyze: reports a cosine. measure: made-up cosines
# for the given takes. Standard library only.
import json, math, struct, sys, wave
from pathlib import Path


def write(path, mix, seconds=2.0, fs=44100):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
        amp = 0.1 + 0.8 * mix
        w.writeframes(b''.join(struct.pack('<h', int(32767 * amp * math.sin(2 * math.pi * 220 * i / fs))) for i in range(int(seconds * fs))))


def main():
    cmd, job = sys.argv[1], json.loads(Path(sys.argv[2]).read_text())
    if cmd == 'analyze':
        print('GV_ANALYSIS ' + json.dumps({'cos_a_b': 0.42, 'a': {'takes': len(job['a'])}, 'b': {'takes': len(job['b'])}}))
        print('GV_DONE'); return 0
    if cmd == 'measure':
        for m, wav in job.get('measure', []):
            print('GV_IDENTITY %.4f %s' % (m, json.dumps({'cos_a': 0.8 - 0.5 * m, 'cos_b': 0.3 + 0.5 * m, 'cos_perf': 0.5, 'cos_a_b': 0.42, 'file': wav})))
        print('GV_DONE'); return 0
    delay = float(job.get('fake_delay', 0) or 0)
    if delay: import time; time.sleep(delay)
    if not job.get('rights'):
        print('GV_ERROR no rights'); return 3
    out = Path(job['out_dir']); out.mkdir(parents=True, exist_ok=True)
    (out / 'seen.json').write_text(json.dumps(job))
    for i, m in enumerate(job['mixes']):
        print(f'GV_PROGRESS {(i + 0.5) / len(job["mixes"]):.3f} singing {round(m * 100)} % B', flush=True)
        p = out / f'mix_{round(m * 100):03d}.wav'
        write(p, m)
        print(f'GV_RESULT {m:.4f} {p}')
        nov = float(job.get('novelty', 0))
        print('GV_IDENTITY %.4f %s' % (m, json.dumps({'cos_a': 0.9 - 0.6 * m - 0.3 * nov, 'cos_b': 0.3 + 0.6 * m - 0.3 * nov, 'cos_perf': 0.2, 'cos_a_b': 0.42})))
        print(f'GV_TIME {m:.4f} 0.10')
    print('GV_DONE')
    return 0


if __name__ == '__main__':
    sys.exit(main())
