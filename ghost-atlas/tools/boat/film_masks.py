#!/usr/bin/env python3
"""The boat's mask in every frame of each world's film. The camera follows the boat, but not exactly: over a film's
thirty frames the boat drifts by ten to twenty-five pixels, and the figure moves about in it. So each frame gets the
painting's own mask (frame 00 is the painting's frame) moved to where the boat is in that frame, grown, with more room
round the figure, plus the places the oar and its blade reach while rowing (drawn by eye over the frames).
Where the boat is comes from two measurements (track.py: corners on the hull, and the hull's picture matched); both
places are covered, unless they disagree by so much that the corners must have followed the background."""
import os, sys, json, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
from masks import SHAPES, mask_for, P, L, E
HERO = {'amber': 's01a', 'crimson': 's02a', 'blossom': 's03b', 'lantern': 's04b', 'opera': 's05a', 'glow': 's06c', 'snow': 's07b', 'aurora': 's08a'}
GROW, FIGURE_MORE = 14, 22
# world: [(first frame, last frame, shape), ...] - where things reach beyond the painting's mask
REACH = {
    'amber':   [(0, 31, (P, [(760,450),(1085,560),(1085,625),(900,625),(770,520)])),                       # the oar swings out to the right
                (0, 31, (P, [(600,560),(840,560),(850,658),(600,658)]))],                                   # the hull's shadow below it
    'crimson': [(0, 31, (P, [(800,490),(1160,600),(1160,658),(900,658),(800,560)]))],                      # the oar dips
    'blossom': [(0, 31, (P, [(850,420),(1078,455),(1078,538),(960,538),(850,470)]))],                      # the oar lifts
    'lantern': [(0, 31, (P, [(720,440),(1010,520),(1010,590),(860,590),(720,500)]))],                      # the oar dips
    'opera':   [(0, 31, (P, [(290,440),(440,440),(600,500),(610,565),(290,580)])),                         # the blade comes out to the lower left
                (0, 31, (P, [(495,290),(570,290),(570,420),(495,420)])),                                   # and its handle over the shoulder
                (0, 31, (E, 942, 392, 85, 78))],                                                           # the lantern's light on the rain
    'glow':    [(0, 31, (P, [(280,395),(785,395),(785,448),(280,448)])),                                   # the hull sinks a little in the picture
                (0, 31, (E, 742, 255, 28, 28))],
    'snow':    [(21, 31, (P, [(835,429),(890,429),(985,479),(985,519),(930,519),(835,469)]))],             # the oar comes out to the right, late
    'aurora':  [(0, 31, (P, [(320,440),(445,440),(445,625),(320,625)])),                                   # the blade and its reflection
                (0, 31, (P, [(495,290),(560,290),(560,430),(495,430)]))],
}
def which(spec):
    """'snow' or 'snow:21-31' -> (world, the frame numbers meant)."""
    w, _, run = spec.partition(':')
    n = len(frames_of(w))
    if not run: return w, list(range(n))
    first, _, last = run.partition('-')
    return w, [i for i in range(int(first), int(last or first) + 1) if i < n]

def frames_of(w): return sorted(f for f in os.listdir(f'{where.film()}/src/{w}') if f.endswith('.png'))

def _read(name):
    path = os.path.join(os.environ.get('BOAT_WORK', ''), name)
    return json.load(open(path)) if os.path.exists(path) else {}
_TRACK, _MATCH = _read('track.json'), _read('track_template.json')

def places(w, i):
    """Where the boat may be in frame i, as shifts from frame 00."""
    if w not in _TRACK or w not in _MATCH: return [(0.0, 0.0)]
    k, t = _TRACK[w][i], _MATCH[w][i][:2]
    if abs(k[0] - t[0]) > 30 or abs(k[1] - t[1]) > 30: return [tuple(t)]
    return [tuple(k), tuple(t)]

def mask_of(w, i):
    base = list(SHAPES[HERO[w]])
    reach = [s for a, b, s in REACH.get(w, []) if a <= i <= b]
    body = np.maximum(mask_for(HERO[w], GROW, base + reach), mask_for(HERO[w], GROW + FIGURE_MORE, [base[1]]))      # (the second shape is the figure)
    m = np.zeros_like(body)
    for dx, dy in places(w, i):
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        # (a mask that reaches the picture's edge still reaches it after the move: the boat's reflection runs off the bottom)
        m = np.maximum(m, cv2.warpAffine(body, M, (body.shape[1], body.shape[0]), flags=cv2.INTER_NEAREST, borderMode=cv2.BORDER_REPLICATE))
    return m

if __name__ == '__main__':
    for w in (sys.argv[1:] or HERO):
        os.makedirs(f'{where.work()}/film/{w}', exist_ok=True)
        for i, n in enumerate(frames_of(w)): cv2.imwrite(f'{where.work()}/film/{w}/{n[:-4]}_mask.png', mask_of(w, i))
        print(w, len(frames_of(w)), 'frames', flush=True)
