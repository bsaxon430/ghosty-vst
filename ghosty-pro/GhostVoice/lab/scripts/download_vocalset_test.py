"""Download five previously selected VocalSet singing examples from a public mirror.

These links require internet access and may change; no recordings are bundled.
Source: VocalSet, Wilkins et al. 2018, CC BY 4.0. Mirrors may have
additional metadata or their own terms; do not assume commercial likeness rights.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from urllib.parse import quote
from urllib.request import Request, urlopen
import soundfile as sf

ROOT = 'https://huggingface.co/datasets/zhu3000/squillo-vocal-quality/resolve/main/vocalset/'
FILES = {
    'voice_a': 'F1 caro mio ben.wav',
    'voice_b': 'F2 caro mio ben.wav',
    'source_performance': 'F3 caro mio ben.wav',
    'voice_a_additional': 'F1 scale.wav',
    'voice_b_additional': 'F2 scale.wav',
}


def download(out: Path, *, timeout: int = 45, max_bytes: int = 50 * 1024 * 1024) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    manifest = {'dataset': 'VocalSet (Wilkins et al., ISMIR 2018)',
                'license': 'CC BY 4.0',
                'original_source': 'https://zenodo.org/records/1193957',
                'mirror': ROOT,
                'disclaimer': 'Downloaded files are test data only, not permission to commercially emulate individual singers.',
                'files': {}, 'errors': {}}
    for role, filename in FILES.items():
        dest = out / filename
        url = ROOT + quote(filename)
        try:
            if not dest.is_file():
                req = Request(url, headers={'User-Agent': 'GhostVoiceResearch/0.6'})
                temp = dest.with_suffix('.download')
                total = 0
                try:
                    with urlopen(req, timeout=timeout) as response, temp.open('wb') as stream:
                        while True:
                            block = response.read(1024 * 1024)
                            if not block:
                                break
                            total += len(block)
                            if total > max_bytes:
                                raise ValueError(f'Unexpectedly large response for {filename}')
                            stream.write(block)
                    info = sf.info(str(temp))
                    if not info.frames or info.samplerate < 8000:
                        raise ValueError(f'Not valid audio: {filename}')
                    temp.replace(dest)
                finally:
                    temp.unlink(missing_ok=True)
            info = sf.info(str(dest))
            manifest['files'][role] = {'path': str(dest.resolve()), 'url': url,
                                        'duration_seconds': round(info.duration, 3),
                                        'sample_rate': info.samplerate}
        except Exception as exc:
            manifest['errors'][role] = f'{type(exc).__name__}: {exc}'
    (out / 'dataset_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, default=Path('tests/fixtures/vocalset'))
    args = p.parse_args()
    result = download(args.output)
    print(json.dumps(result, indent=2))
    if result['errors']:
        raise SystemExit('Some files could not be downloaded; check links and use official VocalSet dataset if mirror is unavailable.')


if __name__ == '__main__':
    main()
