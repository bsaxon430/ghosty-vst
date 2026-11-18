#!/usr/bin/env python3
"""Poltergeist 2 (GhostBlock) - SUB: builds the SUB sign and the SUB panel from the machine's own painted art.

Nothing new is painted: the panel's wells, label plates and switch seats are cut from the MAIN page
(resources/ghostblock/machine/main/machine.png), its roller from the preset ledger, the sign from the FX sign.
Writes resources/ghostblock/machine/common/sub/*.png and sub.json (design px; PNGs at 2x).

    python3 scripts/ghostblock/make_sub_art.py            (from the repository root; needs Pillow)
"""
import json
import math
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'resources', 'ghostblock', 'machine')
OUT = os.path.join(ROOT, 'common', 'sub')
os.makedirs(OUT, exist_ok=True)
S = 2  # png px per design px

main = Image.open(os.path.join(ROOT, 'main', 'machine.png')).convert('RGBA')
MAIN_AT = (47, 48)  # where machine.png sits on the 1040 x 744 canvas
ledger = Image.open(os.path.join(ROOT, 'common', 'ui', 'ledger-back.png')).convert('RGBA')
sign_fx = [Image.open(os.path.join(ROOT, 'fx', 'ui', n)).convert('RGBA') for n in ('sign-fx.png', 'sign-fx-lit.png')]


def cut(x, y, w, h):
    """a piece of the MAIN page, by its design rect"""
    return main.crop(((x - MAIN_AT[0]) * S, (y - MAIN_AT[1]) * S, (x + w - MAIN_AT[0]) * S, (y + h - MAIN_AT[1]) * S))


def slice3(img, cap, new_w):
    """stretch the middle of img (png px) so it becomes new_w wide; the caps keep their size"""
    w, h = img.size
    out = Image.new('RGBA', (new_w, h))
    out.paste(img.crop((0, 0, cap, h)), (0, 0))
    out.paste(img.crop((w - cap, 0, w, h)), (new_w - cap, 0))
    out.paste(img.crop((cap, 0, w - cap, h)).resize((new_w - 2 * cap, h), Image.LANCZOS), (cap, 0))
    return out


def slice3v(img, cap, new_h):
    return slice3(img.transpose(Image.TRANSPOSE), cap, new_h).transpose(Image.TRANSPOSE)


# ------------------------------------------------------------------------------------------------ the panel
PANEL = dict(x=556, y=250, w=404, h=254)  # where the board hangs on the canvas
CELL_W, CELL_H = 64, 80
ROW_Y = (46, 128)
X0 = 10
# the cells of the MAIN page the two rows are made of: (design x, design y, kind)
VOICE_Y, TONE_Y = 342, 428
row1 = [(714, VOICE_Y, 'switch'), (778, VOICE_Y, 'wheel'), (906, VOICE_Y, 'sel3'), (842, VOICE_Y, 'wheel'), (778, TONE_Y, 'wheel'), (714, VOICE_Y, 'switch')]
row2 = [(194, TONE_Y, 'wheel'), (322, TONE_Y, 'wheelS'), (130, TONE_Y, 'sel5'), (258, TONE_Y, 'wheelS'), (386, TONE_Y, 'wheelS'), (450, TONE_Y, 'wheelS')]
names1 = ['on', 'level', 'octave', 'tune', 'glide', 'phase']
names2 = ['width', 'drive', 'mode', 'harmonics', 'blend', 'room']

board = Image.new('RGBA', (PANEL['w'] * S, PANEL['h'] * S), (0, 0, 0, 0))
# the oiled board: the ledger's plain gutter, tiled
gut = ledger.crop((44, 216, 264, 1156))
body = Image.new('RGBA', board.size)
for yy in range(0, board.size[1], gut.size[1]):
    for xx in range(0, board.size[0], gut.size[0]):
        body.paste(gut, (xx, yy))
board.paste(body.crop((0, 0, board.size[0], board.size[1] - 0)), (0, 0))
d = ImageDraw.Draw(board)
ink = (23, 17, 14, 255)

controls = []
for r, (row, names) in enumerate(((row1, names1), (row2, names2))):
    for i, ((cx, cy, kind), name) in enumerate(zip(row, names)):
        ox, oy = X0 + i * CELL_W, ROW_Y[r]
        board.paste(cut(cx, cy, CELL_W, CELL_H), (ox * S, oy * S))
        ax, ay = PANEL['x'] + ox, PANEL['y'] + oy
        if kind == 'switch':
            c = dict(type='switch', rect=[ax + 10, ay + 4, 44, 46], label=[ax + 1, ay + 54, 62, 24], sprite='switch-arm')
        elif kind in ('sel3', 'sel5'):
            c = dict(type='selector', rect=[ax + 14, ay + 10, 36, 36], centre=[ax + 32, ay + 28], label=[ax + 1, ay + 52, 62, 24], sprite='sel-knob')
        else:
            ly = 52 if kind == 'wheelS' or cy == TONE_Y and cx != 194 else 54
            c = dict(type='wheel', rect=[ax + 12, ay + 8, 40, 40], centre=[ax + 32, ay + 28], label=[ax + 1, ay + ly, 62, 24], sprite=['tap-a', 'tap-b', 'tap-c'][(i + r) % 3])
        if kind == 'sel5':
            # the well was painted for three steps (ticks at -52, 0, +52 degrees); MODE has five: two more, at +-104
            for deg in (-104, 104):
                a = math.radians(deg)
                px, py = (ox + 32) * S, (oy + 28) * S
                r0, r1 = 17.2 * S, 20.6 * S
                d.line([(px + math.sin(a) * r0, py - math.cos(a) * r0), (px + math.sin(a) * r1, py - math.cos(a) * r1)], fill=(240, 226, 178, 255), width=3)
        c['id'] = 'sub-' + name
        c['glow'] = 'glow-round'
        c['label_glow'] = 'glow-plate'
        controls.append(c)

# the roller on top (the ledger's), the title bar, the preset rail
roll = slice3(ledger.crop((0, 146, 904, 188)), 120, PANEL['w'] * S).resize((PANEL['w'] * S, 16 * S), Image.LANCZOS)
board.alpha_composite(roll, (0, 0))
plate = cut(779, 310, 62, 24)  # a label plate of the MAIN page (MASTER's)
title = slice3(plate, 24, 150 * S).resize((150 * S, 22 * S), Image.LANCZOS)
board.paste(title, (X0 * S, 20 * S))
read = slice3(plate, 24, 190 * S).resize((190 * S, 22 * S), Image.LANCZOS)
board.paste(read, ((X0 + 156) * S, 20 * S))
board.paste(slice3(plate, 20, 30 * S).resize((30 * S, 22 * S), Image.LANCZOS), (364 * S, 20 * S))  # the close plate
pp = slice3(plate, 20, 46 * S).resize((46 * S, 22 * S), Image.LANCZOS)
presets = ['PURE', 'DEEP', '808', 'ANALOG', 'REESE BODY', 'SPEAKER', 'RUMBLE', 'MONSTER']
for i, nm in enumerate(presets):
    board.paste(pp, ((X0 + 48 * i + 1) * S, 218 * S))
    controls.append(dict(id='sub-preset%d' % (i + 1), type='plunger', name=nm, rect=[PANEL['x'] + X0 + 48 * i + 1, PANEL['y'] + 218, 46, 22],
                         hit=[PANEL['x'] + X0 + 48 * i, PANEL['y'] + 212, 48, 34], index=i))
# an iron frame round it all
W, H = board.size
for k, col in enumerate(((23, 17, 14, 255), (58, 50, 44, 255), (23, 17, 14, 255))):
    d.rectangle([k, 16 * S + k, W - 1 - k, H - 1 - k], outline=col)
board.save(os.path.join(OUT, 'sub-panel.png'))

controls.append(dict(id='sub-title', type='plate', name='SUB BASS', rect=[PANEL['x'] + X0 + 4, PANEL['y'] + 23, 142, 16], text='fixed'))
controls.append(dict(id='sub-readout', type='plate', name='', rect=[PANEL['x'] + X0 + 160, PANEL['y'] + 23, 182, 16], text='state'))
controls.append(dict(id='sub-close', type='plunger', name='X', rect=[PANEL['x'] + 364, PANEL['y'] + 20, 30, 22], hit=[PANEL['x'] + 362, PANEL['y'] + 14, 34, 34]))

# ------------------------------------------------------------------------------------------------ the sign
SIGN = dict(rect=[966, 208, 52, 26], hit=[962, 204, 60, 34])
CHAIN = 6
for img, name in zip(sign_fx, ('sign-sub.png', 'sign-sub-lit.png')):
    w, h = img.size  # 112 x 102: 17 design px of chain, then the 56 x 34 plate
    narrow = slice3(img, 14 * S, SIGN['rect'][2] * S)
    chains = narrow.crop((0, (17 - CHAIN) * S, narrow.size[0], 17 * S))
    plate_s = slice3v(narrow.crop((0, 17 * S, narrow.size[0], h)), 9 * S, SIGN['rect'][3] * S)
    out = Image.new('RGBA', (SIGN['rect'][2] * S, (SIGN['rect'][3] + CHAIN) * S))
    out.paste(chains, (0, 0))
    out.paste(plate_s, (0, CHAIN * S))
    out.save(os.path.join(OUT, name))

manifest = {
    'about': 'Poltergeist 2 ghost-machine: the SUB sign and the SUB panel (the sub-bass layer, src/ghostblock/sub), made by scripts/ghostblock/make_sub_art.py from the '
             'machine\'s own art. Same conventions as ui.json (design px, PNGs at 2x). "panel" = where the board hangs; "controls" = its parts, in canvas coordinates '
             '(they exist only while the panel is open). The sign itself is in ui-layout.json (pages.common: sub-sign).',
    'png_scale': 2,
    'panel': [PANEL['x'], PANEL['y'], PANEL['w'], PANEL['h']],
    'assets': [
        {'name': 'sub-panel', 'file': 'sub-panel.png', 'w': PANEL['w'], 'h': PANEL['h'], 'pivot': [0, 0],
         'use': 'The SUB panel: an oiled board under a brass roller with two rows of six wells and plates (cut from the MAIN page), a title bar and a rail of eight preset plates.'},
        {'name': 'sign-sub', 'file': 'sign-sub.png', 'w': SIGN['rect'][2], 'h': SIGN['rect'][3] + CHAIN, 'pivot': [SIGN['rect'][2] / 2, 0],
         'use': 'Hanging sign SUB (unlit), on two short chains under the VOICES plate.'},
        {'name': 'sign-sub-lit', 'file': 'sign-sub-lit.png', 'w': SIGN['rect'][2], 'h': SIGN['rect'][3] + CHAIN, 'pivot': [SIGN['rect'][2] / 2, 0],
         'use': 'Hanging sign SUB, lit: SUB is on.'},
    ],
    'controls': controls,
}
with open(os.path.join(OUT, 'sub.json'), 'w') as f:
    json.dump(manifest, f, indent=1)
    f.write('\n')
print('wrote', OUT, len(controls), 'controls')
