#!/usr/bin/env python3
"""Draw numbered callouts (circles + leader lines) on screenshots with ImageMagick.
spec: (n, tx, ty, side_or_label)  side: 'T','B','L','R' -> label in the margin at the target's x / y
       or (lx, ly) explicit label position (image coords before margin).  Coordinates are in the
       source image's coordinates (after crop)."""
import subprocess, sys, json
M = 34          # margin
R = 12          # circle radius
COL = '#ffcf33'
def draw(src, out, crop, specs, margin=M, scale=1.0):
    x0, y0, w, h = crop
    cmd = ['convert', src, '-crop', f'{w}x{h}+{x0}+{y0}', '+repage']
    if scale != 1.0:
        cmd += ['-resize', f'{int(scale*100)}%']
    cmd += ['-bordercolor', '#ffffff', '-border', str(margin)]
    W, H = int(w*scale) + 2*margin, int(h*scale) + 2*margin
    draws = []
    for n, tx, ty, lab in specs:
        tx = (tx - x0) * scale + margin; ty = (ty - y0) * scale + margin
        if isinstance(lab, str):
            # side label, optional shift "T+20"
            side = lab[0]; sh = int(lab[1:]) if len(lab) > 1 else 0
            if side == 'T': lx, ly = tx + sh, margin / 2
            elif side == 'B': lx, ly = tx + sh, H - margin / 2
            elif side == 'L': lx, ly = margin / 2, ty + sh
            else: lx, ly = W - margin / 2, ty + sh
        else:
            lx = (lab[0] - x0) * scale + margin; ly = (lab[1] - y0) * scale + margin
        draws.append(('line', lx, ly, tx, ty, n))
    # lines first (dark outline then colour), then target dots, then circles
    for _, lx, ly, tx, ty, n in draws:
        cmd += ['-stroke', '#000000', '-strokewidth', '4', '-draw', f'line {lx},{ly} {tx},{ty}']
    for _, lx, ly, tx, ty, n in draws:
        cmd += ['-stroke', COL, '-strokewidth', '2', '-draw', f'line {lx},{ly} {tx},{ty}']
        cmd += ['-fill', COL, '-stroke', '#000000', '-strokewidth', '1.5', '-draw', f'circle {tx},{ty} {tx+3.5},{ty}']
    for _, lx, ly, tx, ty, n in draws:
        cmd += ['-fill', COL, '-stroke', '#1a1030', '-strokewidth', '2', '-draw', f'circle {lx},{ly} {lx+R},{ly}']
        fs = 14 if n < 10 else 12.5
        cmd += ['-stroke', 'none', '-fill', '#1a1030', '-font', 'DejaVu-Sans-Bold', '-pointsize', str(fs),
                '-draw', f"text {lx - (4.6 if n < 10 else 8.3)},{ly + 5} '{n}'"]
    cmd += [out]
    subprocess.run(cmd, check=True)

if __name__ == '__main__':
    spec = json.load(open(sys.argv[1]))
    for fig in spec:
        draw(fig['src'], fig['out'], fig['crop'], [tuple(s) for s in fig['callouts']],
             fig.get('margin', M), fig.get('scale', 1.0))
        print('wrote', fig['out'])
