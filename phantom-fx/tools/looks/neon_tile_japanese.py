# Adds the Japanese line to the four NEON rack signs that shipped without one (GRID, CRYSTAL, SPECTRA, SQUEEZE),
# laid out exactly as the other 20: label strip from row 97 (top line) / 99, English name core at rows 106-120, Japanese at 130-144.
import sys, numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
src, out = sys.argv[1], sys.argv[2]
FONT = '/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc'
WORDS = { 'GRID': 'グリッド', 'CRYSTAL': 'クリスタル', 'SPECTRA': 'スペクトル', 'SQUEEZE': '圧縮' }
REF = { 'GRID': 'GATE', 'CRYSTAL': 'SPACE', 'SPECTRA': 'SPACE', 'SQUEEZE': 'LIMIT' }   # same border colour: the Japanese colour
STATES = [ 'on', 'off', 'selected', 'selected-off' ]
def load(u, s): return np.asarray(Image.open(f'{src}/{u}-{s}@2x.jpg').convert('RGB')).astype(np.float32)
def refColour(u, s):
    a = load(REF[u], s); band = a[129:146, 36:194]; L = band.sum(2); m = L > np.percentile(L, 97)
    return band[m].mean(0)
def jpMask(word, w, h):
    # Noto Sans CJK JP Medium, sized to the reference lines (kana ~15 px tall, ~17.5 px a character), centred, core rows 130..144
    f = ImageFont.truetype(FONT, 17, index=0)
    im = Image.new('L', (w, h), 0); d = ImageDraw.Draw(im)
    bb = d.textbbox((0, 0), word, font=f); tw, th = bb[2] - bb[0], bb[3] - bb[1]
    x = (w - tw) / 2.0 - bb[0] - 0.5; y = 130 + (15 - th) / 2.0 - bb[1]
    d.text((x, y), word, font=f, fill=255); return np.asarray(im).astype(np.float32) / 255.0
for u, word in WORDS.items():
    for s in STATES:
        T = load(u, s); H, W, _ = T.shape; N = T.copy(); x0, x1 = 31, 197
        # the label's background without the name: each row of the old strip (119..146) from its empty ends (x 33..52 and 177..196,
        # beyond any name's glow), blended across; that column of colours, top to bottom, is stretched from 28 rows to 48 (99..146)
        # so the strip keeps its own shading (dark under the top line, lighter towards the bottom frame's glow)
        prof = np.stack([(T[y, 33:53].mean(0) + T[y, 177:197].mean(0)) / 2.0 for y in range(119, 147)])
        def bgAt(rows):  # rows of the old strip -> a (len, width, 3) background
            v = np.interp(rows, np.arange(119, 147), np.arange(28)); lo = np.floor(v).astype(int); hi = np.minimum(lo + 1, 27); f = (v - lo)[:, None]
            return np.repeat((prof[lo] * (1 - f) + prof[hi] * f)[:, None, :], x1 + 1 - x0, axis=1)
        oldBg = bgAt(np.arange(119, 147))
        name = np.clip(T[119:147, x0:x1 + 1] - oldBg, 0, None)                       # the English name and its glow alone
        name[26:] = 0                                                                 # (rows 145..146: the frame's own glow, not the name's)
        N[97:99, x0:x1 + 1] = T[117:119, x0:x1 + 1]                                   # the label's top line, 20 rows up
        N[99:147, x0:x1 + 1] = bgAt(np.linspace(119, 146, 48))                        # the strip, now from row 99
        N[96:124, x0:x1 + 1] += name                                                  # the name 23 rows up (core 129..143 -> 106..120)
        m = jpMask(word, W, H); glow = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.6))).astype(np.float32) / 255.0
        c = refColour(u, s); a = np.clip(m + 0.35 * glow, 0, 1)[..., None]
        reg = N[126:146, x0:x1 + 1]; N[126:146, x0:x1 + 1] = reg * (1 - a[126:146, x0:x1 + 1]) + c * a[126:146, x0:x1 + 1]
        Image.fromarray(np.clip(N, 0, 255).astype(np.uint8)).save(f'{out}/{u}-{s}@2x.jpg', quality=95, subsampling=0)
    print(u, word)
