#!/usr/bin/env python3
"""Makes the worlds' film: for each world, a short run of frames from the owner's screen recording, starting at the very
frame its painting was made from and moving away from it, enlarged the same way the paintings were.

  make_film.py extract <recording.mov> <work folder>          the chosen frames, as they are in the recording (src/)
  make_film.py enlarge <work folder> <realesr-general-x4v3.pth>   each frame through the model the paintings went through (big/)
  make_film.py pack <work folder> <ghost-atlas/film>           one file per world

Between extract and pack the boat and its boatman are painted out of every frame (tools/boat/: the frames in src/ are
painted, and the part of each enlarged frame that changed is enlarged again and laid into big/).

The plug-in plays a run forwards and back again, slowly, so it always returns to its painting.
A file is: "GAFILM1\\n", one line of JSON (frames, size, seconds of the recording between frames, how long each frame is
given on screen), then for each frame a 4-byte little-endian length and a JPEG.
"""
import json, os, struct, sys
import cv2, numpy as np

# world: (first and last frame of its shot in the recording, the frame its painting came from, which way to go, step)
CLIPS = {
    "amber":   ((0, 114),     10,   +1, 3),
    "crimson": ((294, 474),   303,  +1, 3),
    "blossom": ((832, 1012),  841,  +1, 3),
    "lantern": ((1192, 1372), 1311, -1, 3),
    "opera":   ((1372, 1552), 1404, +1, 3),
    "glow":    ((2002, 2092), 2057, -1, 2),
    "snow":    ((2272, 2450), 2281, +1, 3),
    "aurora":  ((2450, 2630), 2611, -1, 3),
}
FRAMES = 32
FPS = 59.073
KEEP = (1320, 720)


def chosen(key):
    (a, b), ref, way, step = CLIPS[key]
    out = []
    k = ref
    while len(out) < FRAMES and a + 2 <= k <= b - 3:
        out.append(k); k += way * step
    return out


def extract(recording, work):
    want = {}
    for key in CLIPS:
        for n, k in enumerate(chosen(key)):
            want[k] = (key, n)
    cap = cv2.VideoCapture(recording)
    i = 0
    while True:                      # read straight through: seeking in this recording does not land on exact frames
        ok, f = cap.read()
        if not ok: break
        if i in want:
            key, n = want[i]
            os.makedirs(os.path.join(work, "src", key), exist_ok=True)
            cv2.imwrite(os.path.join(work, "src", key, f"{n:02d}.png"), f)
        i += 1
    for key in CLIPS:
        print(key, len(chosen(key)), "frames")


def enlarge(work, model_path):
    import torch
    sys.path.insert(0, os.path.dirname(os.path.abspath(model_path)))
    import importlib.util
    spec = importlib.util.spec_from_file_location("sr", os.path.join(os.path.dirname(os.path.abspath(model_path)), "sr.py"))
    sr = importlib.util.module_from_spec(spec); spec.loader.exec_module(sr)
    torch.set_num_threads(int(os.environ.get("FILM_THREADS", "2")))
    model = sr.load(model_path)
    for key in CLIPS:
        os.makedirs(os.path.join(work, "big", key), exist_ok=True)
        for n in range(len(chosen(key))):
            dst = os.path.join(work, "big", key, f"{n:02d}.png")
            if os.path.exists(dst): continue
            img = cv2.imread(os.path.join(work, "src", key, f"{n:02d}.png"))
            up = sr.run(model, img)
            cv2.imwrite(dst, cv2.resize(up, KEEP, interpolation=cv2.INTER_AREA))
            print(key, n, flush=True)


def pack(work, dest, quality=80):
    os.makedirs(dest, exist_ok=True)
    for key in CLIPS:
        n = len(chosen(key))
        frames = [cv2.imread(os.path.join(work, "big", key, f"{i:02d}.png")) for i in range(n)]
        if any(f is None for f in frames):
            print(key, "is not enlarged yet"); continue
        # a busy run is played more slowly than a calm one, so that every world moves at about the same gentle pace
        small = [cv2.cvtColor(cv2.resize(f, (320, 175), interpolation=cv2.INTER_AREA), cv2.COLOR_BGR2GRAY).astype(np.float32) for f in frames]
        change = float(np.median([np.abs(a - b).mean() for a, b in zip(small, small[1:])]))
        pace = float(min(0.42, max(0.14, 0.06 * change)))
        head = {"world": key, "frames": n, "width": KEEP[0], "height": KEEP[1], "between": CLIPS[key][3] / FPS, "pace": round(pace, 3)}
        with open(os.path.join(dest, key + ".gafilm"), "wb") as out:
            out.write(b"GAFILM1\n"); out.write(json.dumps(head).encode() + b"\n")
            for f in frames:
                ok, jpg = cv2.imencode(".jpg", f, [cv2.IMWRITE_JPEG_QUALITY, quality, cv2.IMWRITE_JPEG_PROGRESSIVE, 0, cv2.IMWRITE_JPEG_OPTIMIZE, 1])
                out.write(struct.pack("<I", len(jpg))); out.write(jpg.tobytes())
        size = os.path.getsize(os.path.join(dest, key + ".gafilm"))
        print(f"{key}: {n} frames, {size / 1048576:.1f} MB, each shown for {pace:.2f} s")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "extract": extract(sys.argv[2], sys.argv[3])
    elif cmd == "enlarge": enlarge(sys.argv[2], sys.argv[3])
    elif cmd == "pack": pack(sys.argv[2], sys.argv[3])
    else: print(__doc__)
