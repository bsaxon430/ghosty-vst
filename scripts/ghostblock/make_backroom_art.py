#!/usr/bin/env python3
"""Poltergeist 2 (GhostBlock) - the BACK ROOM: the wiring behind the machine, made from the machine's own painted art.

The room's wall is the hull's own patchwork (GHOSTS page), the six ghost chambers are cut from the GHOSTS page, the
valve windows from the MAIN page's wells, the plates from its label plates. Only the copper pipes are painted here.
Writes resources/ghostblock/machine/common/backroom/room.png (the whole static room, 2x) and backroom.json (its layout,
design px), which src/ghostblock/machine/backroom.cpp reads.

    python3 scripts/ghostblock/make_backroom_art.py            (from the repository root; needs Pillow)
"""
import json
import math
import os
import random
from PIL import Image, ImageDraw, ImageFilter, ImageChops

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'resources', 'ghostblock', 'machine')
OUT = os.path.join(ROOT, 'common', 'backroom')
os.makedirs(OUT, exist_ok=True)
S = 2
CW, CH = 1040, 744

ghosts = Image.open(os.path.join(ROOT, 'ghosts', 'machine.png')).convert('RGBA')
GH_AT = (46, 48)
main = Image.open(os.path.join(ROOT, 'main', 'machine.png')).convert('RGBA')
MAIN_AT = (47, 48)
layout = json.load(open(os.path.join(ROOT, 'ui-layout.json')))


def cut(img, at, x, y, w, h):
    return img.crop((round((x - at[0]) * S), round((y - at[1]) * S), round((x + w - at[0]) * S), round((y + h - at[1]) * S)))


def slice3(img, cap, new_w):
    w, h = img.size
    out = Image.new('RGBA', (new_w, h))
    out.paste(img.crop((0, 0, cap, h)), (0, 0))
    out.paste(img.crop((w - cap, 0, w, h)), (new_w - cap, 0))
    out.paste(img.crop((cap, 0, w - cap, h)).resize((new_w - 2 * cap, h), Image.LANCZOS), (cap, 0))
    return out


def slice3v(img, cap, new_h):
    w, h = img.size
    out = Image.new('RGBA', (w, new_h))
    out.paste(img.crop((0, 0, w, cap)), (0, 0))
    out.paste(img.crop((0, h - cap, w, h)), (0, new_h - cap))
    out.paste(img.crop((0, cap, w, h - cap)).resize((w, new_h - 2 * cap), Image.LANCZOS), (0, cap))
    return out


def plate(w, h):
    """a label plate of the MAIN page, stretched to w x h design px"""
    p = cut(main, MAIN_AT, 65, 309, 64, 26)
    p = slice3(p, 14, w * S)
    return slice3v(p, 12, h * S)


def scale_rgb(img, k, tint=(1.0, 1.0, 1.0)):
    r, g, b, a = img.split()
    r = r.point(lambda v: min(255, int(v * k * tint[0])))
    g = g.point(lambda v: min(255, int(v * k * tint[1])))
    b = b.point(lambda v: min(255, int(v * k * tint[2])))
    return Image.merge('RGBA', (r, g, b, a))


# ------------------------------------------------------------------------------------------------ the layout (design px)
COLX = [236 + 104 * j for j in range(6)]  # TO ghost 1..6
OUTX = 884                                # YOU HEAR
ROWY = [208 + 82 * i for i in range(6)]   # FROM ghost 1..6
HEADY = 112                               # the target chambers' row
LEFTX = 110                               # the source chambers' column
WELL_R = 24
PIPE = 12

# ------------------------------------------------------------------------------------------------ the wall
room = Image.new('RGBA', (CW * S, CH * S), (16, 12, 10, 255))
patch = cut(ghosts, GH_AT, 712, 252, 244, 200)  # blank patchwork of the hull
tiles = [patch, patch.transpose(Image.FLIP_LEFT_RIGHT), patch.transpose(Image.FLIP_TOP_BOTTOM),
         patch.transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)]
pw, ph = patch.size
for ty in range(0, CH * S, ph):
    for tx in range(0, CW * S, pw):
        t = tiles[((tx // pw) % 2) + 2 * ((ty // ph) % 2)]
        room.alpha_composite(t, (tx, ty))
# the purple rain streaks of the outside hull do not belong indoors: pull the purple out
px = room.load()
for y in range(room.height):
    for x in range(room.width):
        r, g, b, a = px[x, y]
        if b > r + 25 and b > g + 25:
            m = (r + g + b) // 3
            px[x, y] = (m, m - 4 if m > 4 else m, m - 10 if m > 10 else m, a)
room = scale_rgb(room, 0.44, (1.0, 0.95, 0.88))

# the light: two warm lamps high up, darker towards the floor and the corners
light = Image.new('L', (CW * S, CH * S), 0)
ld = ImageDraw.Draw(light)
for (cx, cy, r, v) in [(150, 40, 420, 120), (900, 40, 420, 110), (520, 380, 560, 70)]:
    for k in range(24, 0, -1):
        rr = r * k / 24
        ld.ellipse(((cx - rr) * S, (cy - rr) * S, (cx + rr) * S, (cy + rr) * S), fill=int(v * (1 - k / 24) + 4))
light = light.filter(ImageFilter.GaussianBlur(30))
warm = Image.new('RGBA', room.size, (255, 178, 96, 0))
warm.putalpha(light)
room.alpha_composite(warm)
shade = Image.new('L', room.size, 0)
sd = ImageDraw.Draw(shade)
for k in range(40):
    sd.rectangle((k * 6, k * 6, room.width - k * 6, room.height - k * 6), outline=170 - k * 4, width=6)
sd.rectangle((0, (CH - 70) * S, room.width, room.height), fill=120)
shade = shade.filter(ImageFilter.GaussianBlur(40))
dark = Image.new('RGBA', room.size, (6, 4, 3, 0))
dark.putalpha(shade)
room.alpha_composite(dark)


# ------------------------------------------------------------------------------------------------ copper pipes
def pipe_profile(n):
    """colours across a pipe n px wide: dark edges, a warm body, a highlight off-centre"""
    out = []
    for i in range(n):
        u = (i + 0.5) / n * 2 - 1  # -1..1
        body = math.sqrt(max(0.0, 1 - u * u))
        hi = math.exp(-((u + 0.35) / 0.18) ** 2)
        r = 40 + 130 * body + 110 * hi
        g = 22 + 70 * body + 90 * hi
        b = 12 + 36 * body + 60 * hi
        out.append((int(min(255, r)), int(min(255, g)), int(min(255, b)), 255))
    return out


rng = random.Random(7)


def hpipe(img, x0, x1, y):
    n = PIPE * S
    prof = pipe_profile(n)
    p = img.load()
    for yy in range(n):
        Y = int(y * S - n / 2 + yy)
        c = prof[yy]
        for X in range(int(x0 * S), int(x1 * S)):
            k = 0.88 + 0.12 * math.sin(X * 0.021 + Y * 0.3) + rng.uniform(-0.05, 0.05)
            p[X, Y] = (int(c[0] * k), int(c[1] * k), int(c[2] * k), 255)
    d = ImageDraw.Draw(img, 'RGBA')
    d.line((x0 * S, (y + PIPE / 2 + 1) * S, x1 * S, (y + PIPE / 2 + 1) * S), fill=(0, 0, 0, 90), width=3 * S)  # its shadow on the wall


def vpipe(img, x, y0, y1):
    n = PIPE * S
    prof = pipe_profile(n)
    p = img.load()
    for xx in range(n):
        X = int(x * S - n / 2 + xx)
        c = prof[xx]
        for Y in range(int(y0 * S), int(y1 * S)):
            k = 0.88 + 0.12 * math.sin(Y * 0.021 + X * 0.3) + rng.uniform(-0.05, 0.05)
            p[X, Y] = (int(c[0] * k), int(c[1] * k), int(c[2] * k), 255)
    d = ImageDraw.Draw(img, 'RGBA')
    d.line(((x + PIPE / 2 + 1) * S, y0 * S, (x + PIPE / 2 + 1) * S, y1 * S), fill=(0, 0, 0, 90), width=3 * S)


def flange(img, x, y, vertical):
    """a bolted collar round a pipe"""
    d = ImageDraw.Draw(img, 'RGBA')
    w, h = (PIPE + 8, 7) if vertical else (7, PIPE + 8)
    box = ((x - w / 2) * S, (y - h / 2) * S, (x + w / 2) * S, (y + h / 2) * S)
    d.rounded_rectangle(box, radius=2 * S, fill=(92, 54, 30, 255), outline=(30, 16, 8, 255), width=S)
    d.line((box[0] + S, box[1] + S, box[2] - S, box[1] + S) if vertical else (box[0] + S, box[1] + S, box[0] + S, box[3] - S), fill=(210, 150, 90, 160), width=S)
    for (bx, by) in ([(x - w / 2 + 3, y), (x + w / 2 - 3, y)] if vertical else [(x, y - h / 2 + 3), (x, y + h / 2 - 3)]):
        d.ellipse(((bx - 1.6) * S, (by - 1.6) * S, (bx + 1.6) * S, (by + 1.6) * S), fill=(220, 170, 100, 255), outline=(40, 20, 10, 255))


pipes = Image.new('RGBA', room.size, (0, 0, 0, 0))
# columns first (behind): from the target chamber down to the diagonal; YOU HEAR: the whole height
for j in range(6):
    vpipe(pipes, COLX[j], HEADY + 30, ROWY[j])
vpipe(pipes, OUTX, HEADY + 24, ROWY[5])
for i in range(6):
    hpipe(pipes, LEFTX + 36, OUTX, ROWY[i])
for j in range(6):
    flange(pipes, COLX[j], HEADY + 40, True)
flange(pipes, OUTX, HEADY + 34, True)
for i in range(6):
    flange(pipes, LEFTX + 46, ROWY[i], False)
    for j in range(i):  # the cells a ghost cannot reach: just a collar on the pipe that passes
        flange(pipes, COLX[j], ROWY[i], False)
room.alpha_composite(pipes)

# ------------------------------------------------------------------------------------------------ the chambers
cham = {c['op']: c for c in layout['pages']['ghosts']['chambers']}
ctl = {c['id']: c for c in layout['pages']['ghosts']['controls']}


def chamber(op):
    x, y, w, h = ctl['ch%d' % (op + 1)]['rect']
    pad = 8
    im = cut(ghosts, GH_AT, x - pad, y - pad, w + 2 * pad, h + 2 * pad)
    im = scale_rgb(im, 0.8, (1.04, 0.98, 0.9))
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).ellipse((4 * S, 2 * S, im.width - 4 * S, im.height - 2 * S), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(5 * S))
    im.putalpha(ImageChops.multiply(im.getchannel('A'), mask))
    return im


ch_imgs = [chamber(o) for o in range(6)]
CHW, CHH = ch_imgs[0].width // S, ch_imgs[0].height // S
for o in range(6):
    im = ch_imgs[o]
    room.alpha_composite(im, (int((COLX[o] - im.width / S / 2) * S), int((HEADY - im.height / S / 2) * S)))
    room.alpha_composite(im, (int((LEFTX - im.width / S / 2) * S), int((ROWY[o] - im.height / S / 2) * S)))


# ------------------------------------------------------------------------------------------------ the valve windows
def well(cx, cy, r):
    im = cut(main, MAIN_AT, cx - r, cy - r, 2 * r, 2 * r)
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.width - 1, im.height - 1), fill=255)
    im.putalpha(ImageChops.multiply(im.getchannel('A'), mask))
    return im


WR = 24.5
W_LINK = well(226.5, 284, WR)  # brass rim with ticks: a link
W_FB = well(98, 285, WR)     # copper studs: a ghost into itself
W_OUT = well(165, 285, WR)   # the plain dark one: what you hear


def put_well(im, x, y):
    sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
    sh.putalpha(im.getchannel('A').point(lambda v: v * 0.6))
    sh = sh.filter(ImageFilter.GaussianBlur(3 * S))
    room.alpha_composite(sh, (int((x - WR + 2) * S), int((y - WR + 3) * S)))
    room.alpha_composite(im, (int((x - WR) * S), int((y - WR) * S)))


cells = []
for i in range(6):
    for j in range(6):
        if j < i:
            continue
        put_well(W_FB if i == j else W_LINK, COLX[j], ROWY[i])
        cells.append({'from': i, 'to': j, 'kind': 'fb' if i == j else 'link', 'centre': [COLX[j], ROWY[i]]})
    put_well(W_OUT, OUTX, ROWY[i])
    cells.append({'from': i, 'to': -1, 'kind': 'out', 'centre': [OUTX, ROWY[i]]})

# ------------------------------------------------------------------------------------------------ plates
plates = {
    'title': [330, 10, 380, 40],
    'exit': [846, 14, 178, 36],
    'out-head': [OUTX - 58, HEADY - 18, 116, 36],
    'legend': [LEFTX - 60, HEADY - 30, 120, 60],
    'hint': [110, 696, 820, 34],
}
for k, (x, y, w, h) in plates.items():
    p = plate(w, h)
    sh = Image.new('RGBA', p.size, (0, 0, 0, 0))
    sh.putalpha(p.getchannel('A').point(lambda v: v * 0.55))
    sh = sh.filter(ImageFilter.GaussianBlur(3 * S))
    room.alpha_composite(sh, ((x + 2) * S, (y + 3) * S))
    room.alpha_composite(p, (x * S, y * S))

room.convert('RGB').save(os.path.join(OUT, 'room.png'), optimize=True)

doc = {
    'about': 'Poltergeist 2 ghost-machine: the BACK ROOM (the wiring), made by scripts/ghostblock/make_backroom_art.py from the '
             'machine\'s own art. Design px (1040 x 744), room.png at 2x. Rows = the ghost that haunts (FROM), columns = the '
             'ghost it haunts (TO); a ghost can only haunt itself (feedback, the diagonal) or a higher number; the last column is '
             'how loud you hear each ghost. backroom.cpp draws the live parts (numbers, glows, the coloured flow in the pipes).',
    'png_scale': S,
    'room': 'room.png',
    'cols': COLX,
    'out_x': OUTX,
    'rows': ROWY,
    'head_y': HEADY,
    'left_x': LEFTX,
    'chamber': [CHW, CHH],
    'well_r': 22,
    'pipe': PIPE,
    'cells': cells,
    'plates': plates,
    'doors': [[456, 130, 66, 76], [580, 130, 58, 70], [526, 206, 46, 40]],
}
json.dump(doc, open(os.path.join(OUT, 'backroom.json'), 'w'), indent=1)
print('wrote', os.path.join(OUT, 'room.png'))
