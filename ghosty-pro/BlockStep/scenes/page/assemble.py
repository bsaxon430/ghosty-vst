#!/usr/bin/env python3
"""How build 113's index.html (the painted scenes) was made from build 112's page + this folder.
Every edit is an exact-text replacement that must match the expected number of times, so nothing else can change.
NOTE: this starts again from build 112. Once the page has been edited after build 113, do not run it (it would throw
those edits away): edit data/index.html directly, and use swap_art.py to put repainted pictures into it."""
import base64, json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); WORK = os.path.dirname(HERE)
REPO = subprocess.run(['git', '-C', HERE, 'rev-parse', '--show-toplevel'], capture_output=True, check=True).stdout.decode().strip()
PAGE = 'ghosty-pro/BlockStep/data/index.html'; BASE = '2598b74'   # build 112, the last page before the scenes
src = subprocess.run(['git', '-C', REPO, 'show', f'{BASE}:{PAGE}'], capture_output=True, check=True).stdout.decode('utf-8')
s = src

def rep(old, new, count=1, where=None):
    global s
    n = s.count(old)
    assert n == count, f'expected {count} x, found {n}: {old[:90]!r}'
    s = s.replace(old, new)

# ---- 1. the look is remembered across plugin windows, like LINES and GHOST ANIMATION
rep("const LOOK = new Set(['gb_gridl', 'gb_ghosts'])", "const LOOK = new Set(['gb_gridl', 'gb_ghosts', 'gb_scene'])")

# ---- 2. the scene's style sheet
css = open(os.path.join(HERE, 'scene.css'), encoding='utf-8').read()
import io, re
from PIL import Image
def ui_png(m):   # url(ui:c-off) -> the painted control, as an 8-bit PNG inside the style sheet
    im = Image.open(os.path.join(WORK, 'out', 'ui-' + m.group(1) + '.png')).convert('RGBA')
    b = io.BytesIO(); im.quantize(colors=255, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG).save(b, 'PNG', optimize=True)
    return 'url(data:image/png;base64,' + base64.b64encode(b.getvalue()).decode('ascii') + ')'
css = re.sub(r'url\(ui:([a-z0-9-]+)\)', ui_png, css)
def font_data(m):   # url(font:tilt-neon) -> the typeface (SIL OFL 1.1), inside the style sheet like the page's other fonts
    f = os.path.join(WORK, 'node_modules', '@fontsource', m.group(1), 'files', m.group(1) + '-latin-400-normal.woff2')
    return 'url(data:font/woff2;base64,' + base64.b64encode(open(f, 'rb').read()).decode('ascii') + ')'
css = re.sub(r'url\(font:([a-z0-9-]+)\)', font_data, css)
rep('\n</style>\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=VT323', '\n' + css + '\n</style>\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=VT323')

# ---- 3. the background canvases: the Ghost Money swirl keeps its own (the ORIGINAL look) and stops while a scene is showing
rep('<canvas id="bg" aria-hidden="true"></canvas>', '<canvas id="bg" aria-hidden="true"></canvas><canvas id="scn" aria-hidden="true"></canvas>')
rep("function wantRun(){return ready&&!reduced.matches&&!window.__ghostsOff&&!document.hidden&&visible;}", "function wantRun(){return ready&&!reduced.matches&&!window.__ghostsOff&&!document.hidden&&visible&&!window.__scnOn;}")

# ---- 4. the pictures, as text blocks the scene code decodes when it needs them
arts = json.load(open(os.path.join(WORK, 'out', 'art.json')))   # id -> {file, mime}
blocks = []
for aid, a in arts.items():
    b = base64.b64encode(open(os.path.join(WORK, 'out', a['file']), 'rb').read()).decode('ascii')
    blocks.append(f'<script type="text/plain" id="art-{aid}" data-mime="{a["mime"]}">{b}</script>')
rep('\n\n<script>/* in the plugin: a script error is passed to the plugin', '\n' + '\n'.join(blocks) + '\n\n<script>/* in the plugin: a script error is passed to the plugin')

# ---- 5. the tracker reads its colours from the scene (T) instead of fixed Ghost Money colours
a = s.index('function drawCell(g, x, y, w, h, ce, lit, selected) {'); b = s.index('/* ---------- main loop ---------- */')
head, fn, tail = s[:a], s[a:b], s[b:]
fn0 = fn   # the ORIGINAL look draws with the page's own code, untouched: drawCell0 / draw0
assert fn0.count('function drawCell(g, x, y, w, h, ce, lit, selected) {') == 1 and fn0.count('function draw(now) {') == 1 and fn0.count('drawCell(g, ') == 3
fn0 = fn0.replace('drawCell(g, ', 'drawCell0(g, ').replace('function draw(now) {', 'function draw0(now) {')
def frep(old, new, count=1):
    global fn
    n = fn.count(old)
    assert n == count, f'draw(): expected {count} x, found {n}: {old[:90]!r}'
    fn = fn.replace(old, new)
frep("    const a = ce.m ? .22 : .38 + .55 * (ce.v / 100) + lit * .5, hh = Math.max(2, h);\n    g.fillStyle = `rgba(${cellRgb(ce)},${Math.min(1, a)})`;",
     "    const a = ce.m ? .22 : .38 + .55 * (ce.v / 100) + lit * .5, hh = Math.max(2, h), T = TH || THEMES.city;\n    g.fillStyle = `rgba(${T.rgb ? T.rgb(ce) : cellRgb(ce)},${Math.min(1, a)})`;")
frep("    if (selected) { g.strokeStyle = '#fff'; g.lineWidth = 1; g.strokeRect(x + .5, y + .5, w - 1, hh - 1); }\n    return;\n  }\n  glassCell(g, x, y, w, h, ce, lit, selected); return;",
     "    if (selected) { g.strokeStyle = (TH || THEMES.city).pick; g.lineWidth = 1; g.strokeRect(x + .5, y + .5, w - 1, hh - 1); }\n    return;\n  }\n  ((TH && TH.cell) || glassCell)(g, x, y, w, h, ce, lit, selected); return;")
frep("  g.clearRect(0, 0, W, H); g.fillStyle = HACK ? HK.bg : 'rgba(5,4,10,.38)'; g.fillRect(0, 0, W, H);\n  if (!HACK) rain(g, W, H);",
     "  const T = TH || THEMES.city;\n  g.clearRect(0, 0, W, H); g.fillStyle = HACK ? HK.bg : T.bg; g.fillRect(0, 0, W, H);\n  if (!HACK) { if (T.back) T.back(g, W, H, st, sl, now); if (T.rain) rain(g, W, H); }")
frep("g.fillStyle = L.on ? 'rgba(168,85,247,.12)' : 'rgba(255,255,255,.03)'; g.fillRect(0, ya, W, yb - ya);", "g.fillStyle = L.on ? T.loopFill : T.loopOff; g.fillRect(0, ya, W, yb - ya);")
frep("g.strokeStyle = L.on ? '#a855f7' : 'rgba(255,255,255,.25)'; g.lineWidth = 1.5; if (L.on) { g.shadowColor = '#a855f7'; g.shadowBlur = 10; }",
     "g.strokeStyle = L.on ? T.loopLine : T.loopLineOff; g.lineWidth = 1.5; if (L.on) { g.shadowColor = T.loopLine; g.shadowBlur = T.glow ? 10 : 0; }")
frep("g.fillStyle = HACK ? '#00ff41' : '#fbbf24'; g.globalAlpha = .9;", "g.fillStyle = HACK ? '#00ff41' : T.cue; g.globalAlpha = .9;")
frep("g.fillStyle = 'rgba(0,255,136,.09)'; g.fillRect(0, y, W, ROW);\n    g.save(); g.strokeStyle = 'rgba(0,255,136,.85)'; g.shadowColor = '#00ff88'; g.shadowBlur = 14;",
     "g.fillStyle = T.playFill; g.fillRect(0, y, W, ROW);\n    g.save(); g.strokeStyle = T.playLine; g.shadowColor = T.playGlow; g.shadowBlur = T.glow ? 14 : 0;")
frep("g.fillStyle = 'rgba(0,255,136,.18)'; g.fillRect(0, sy, W, 1); }", "g.fillStyle = T.playScan; g.fillRect(0, sy, W, 1); }")
frep("if (s < 1e-6) { g.fillStyle = 'rgba(168,85,247,.55)'; g.fillRect(0, y, W, 1); } else if (s % 4 < 1e-6) { g.fillStyle = 'rgba(255,255,255,.08)'; g.fillRect(GUT, y, W - GUT, 1); } else if (k > 4 && whole) { g.fillStyle = 'rgba(255,255,255,.05)';",
     "if (s < 1e-6) { g.fillStyle = T.bar; g.fillRect(0, y, W, 1); } else if (s % 4 < 1e-6) { g.fillStyle = T.beat; g.fillRect(GUT, y, W - GUT, 1); } else if (k > 4 && whole) { g.fillStyle = T.sub;")
frep("if (!chOn(p, c)) { g.fillStyle = 'rgba(0,0,0,.4)';", "if (!chOn(p, c)) { g.fillStyle = T.colOff;")
frep("if (isLocked(c)) { g.fillStyle = 'rgba(251,191,36,.06)';", "if (isLocked(c)) { g.fillStyle = T.colLock;")
frep("else if (c % 2) { g.fillStyle = 'rgba(255,255,255,.018)';", "else if (c % 2) { g.fillStyle = T.colAlt;")
frep("g.fillStyle = s < 1e-6 ? 'rgba(168,85,247,.85)' : s % 4 < 1e-6 ? 'rgba(255,255,255,.32)' : 'rgba(255,255,255,.14)';", "g.fillStyle = s < 1e-6 ? T.gBar : s % 4 < 1e-6 ? T.gBeat : T.gStep;")
frep("if (r1 >= p.rows.length - 1) { g.fillStyle = 'rgba(168,85,247,.85)';", "if (r1 >= p.rows.length - 1) { g.fillStyle = T.gBar;")
frep("g.fillStyle = 'rgba(255,255,255,.26)'; for (let c = c0; c <= c1 + 1; c++)", "g.fillStyle = T.gCol; for (let c = c0; c <= c1 + 1; c++)")
frep("  g.fillStyle = 'rgba(255,255,255,.13)';\n  if (ROW >= 12)", "  g.fillStyle = T.dot;\n  if (ROW >= 12)")
frep("if (pkd) { g.save(); g.strokeStyle = '#60a5fa'; g.lineWidth = 2; g.shadowColor = '#60a5fa'; g.shadowBlur = 12;", "if (pkd) { g.save(); g.strokeStyle = T.pick; g.lineWidth = 2; g.shadowColor = T.pick; g.shadowBlur = T.glow ? 12 : 0;")
frep("if (flick) { g.fillStyle = `rgba(0,255,136,${.35 * flick})`; g.fillRect(x - 3 * flick, cy, w, h); g.fillStyle = `rgba(168,85,247,${.35 * flick})`;",
     "if (flick) { g.fillStyle = `rgba(${T.flickA},${.35 * flick})`; g.fillRect(x - 3 * flick, cy, w, h); g.fillStyle = `rgba(${T.flickB},${.35 * flick})`;")
frep("g.font = `700 13px ${MONO}`; g.fillStyle = '#00ff88'; g.textAlign = 'center'; g.fillText(GLY[", "g.font = `700 13px ${MONO}`; g.fillStyle = T.fxGlyph; g.textAlign = 'center'; g.fillText(GLY[")
frep("g.fillStyle = 'rgba(96,165,250,.12)'; g.fillRect(x, y, w, h);\n    g.save(); g.strokeStyle = '#60a5fa'; g.lineWidth = 1.6; g.shadowColor = '#60a5fa'; g.shadowBlur = 10;",
     "g.fillStyle = T.selFill; g.fillRect(x, y, w, h);\n    g.save(); g.strokeStyle = T.sel; g.lineWidth = 1.6; g.shadowColor = T.sel; g.shadowBlur = T.glow ? 10 : 0;")
frep("g.fillStyle = 'rgba(8,7,14,.55)'; g.fillRect(0, HEAD, GUT, H - HEAD);", "g.fillStyle = T.gut; g.fillRect(0, HEAD, GUT, H - HEAD);")
frep("sg.addColorStop(0, 'rgba(5,4,10,.7)'); sg.addColorStop(1, 'rgba(5,4,10,0)');", "sg.addColorStop(0, `rgba(${T.gutShade},.7)`); sg.addColorStop(1, `rgba(${T.gutShade},0)`);")
frep("gr.addColorStop(0, '#a855f7'); gr.addColorStop(1, '#00ff88'); g.fillStyle = L.on ? gr : 'rgba(255,255,255,.25)';", "gr.addColorStop(0, T.loopA); gr.addColorStop(1, T.loopB); g.fillStyle = L.on ? gr : T.loopLineOff;")
frep("g.font = `600 12px ${MONO}`; g.fillStyle = '#00ff88'; g.fillText(GLY[ri(0, GLY.length - 1)]", "g.font = `600 12px ${MONO}`; g.fillStyle = T.numPlay; g.fillText(GLY[ri(0, GLY.length - 1)]")
frep("g.fillStyle = r === ph ? '#00ff88' : '#c4b5fd';", "g.fillStyle = r === ph ? T.numPlay : T.numBar;")
frep("g.fillStyle = r === ph ? '#00ff88' : sbeat ? 'rgba(245,243,255,.8)' : 'rgba(156,163,175,.6)';", "g.fillStyle = r === ph ? T.numPlay : sbeat ? T.numBeat : T.numStep;")
frep("g.fillStyle = r === ph ? '#00ff88' : `rgba(${GTINT[k] || '156,163,175'},.8)`;", "g.fillStyle = r === ph ? T.numPlay : `rgba(${GTINT[k] || '156,163,175'},.8)`;")
frep("g.fillStyle = 'rgba(11,10,19,.92)'; g.fillRect(0, 0, W, HEAD);", "g.fillStyle = T.head; g.fillRect(0, 0, W, HEAD);")
frep("hg.addColorStop(0, 'rgba(168,85,247,0)'); hg.addColorStop(.5, 'rgba(168,85,247,.7)'); hg.addColorStop(1, 'rgba(168,85,247,0)');",
     "hg.addColorStop(0, `rgba(${T.headLine},0)`); hg.addColorStop(.5, `rgba(${T.headLine},.7)`); hg.addColorStop(1, `rgba(${T.headLine},0)`);")
frep("if (chEd.has(c)) { g.fillStyle = 'rgba(168,85,247,.28)';", "if (chEd.has(c)) { g.fillStyle = T.headSel;")
frep("g.fillStyle = lk ? '#fbbf24' : !on ? 'rgba(156,163,175,.5)' : lt > 0 ? INST[L2.i].c : 'rgba(245,243,255,.85)';", "g.fillStyle = lk ? T.headLock : !on ? T.headOff : lt > 0 ? INST[L2.i].c : T.headTxt;")
frep("  g.fillStyle = 'rgba(156,163,175,.9)'; g.fillText(CH > Math.floor((W - GUT) / colW)", "  g.fillStyle = T.headStep; g.fillText(CH > Math.floor((W - GUT) / colW)")
frep("if (!RM.matches && kickPulse > .6 && chance(.3)) {", "if (!RM.matches && T.glitch && kickPulse > .6 && chance(.3)) {")
frep("g.fillStyle = chance(.5) ? 'rgba(168,85,247,.12)' : 'rgba(0,255,136,.10)'; g.fillRect(0, y, W, h); }", "g.fillStyle = chance(.5) ? T.glA : T.glB; g.fillRect(0, y, W, h); }")
# (the page's row loops use a local T for the step's time, which hid the theme: give that one its own name)
frep("T = tmG.t[r], s = T % 16", "tq = tmG.t[r], s = tq % 16", 3); frep("Math.round(T / 16)", "Math.round(tq / 16)")
frep("  // glitch slices on big hits\n", "  if (!HACK && T.over) T.over(g, W, H, st, sl, now);\n  // glitch slices on big hits\n")
frep("function drawCell(g, x, y, w, h, ce, lit, selected) {\n", "function drawCell(g, x, y, w, h, ce, lit, selected) {\n  if (!TH) { drawCell0(g, x, y, w, h, ce, lit, selected); return; }\n")
frep("function draw(now) {\n", "function draw(now) {\n  if (!TH) { draw0(now); return; }\n")
s = head + fn0 + fn + tail

# the falling glyphs take the scene's colours
rep("len: 6 + ri(0, 14), ch: [], c: chance(.72) ? '0,255,136' : '168,85,247'});", "len: 6 + ri(0, 14), ch: [], c: chance(.72) ? ((TH && TH.rainA) || '0,255,136') : ((TH && TH.rainB) || '168,85,247')});")
rep("g.fillStyle = k === 0 ? `rgba(220,255,235,${a})` : `rgba(${d.c},${a})`;", "g.fillStyle = k === 0 ? `rgba(${(TH && TH.rainHead) || '220,255,235'},${a})` : `rgba(${d.c},${a})`;")

# ---- 6. every hit tells the scene (signs flare, the drummer plays); the notes and the sound are not touched
rep("hitT.set(st * 32 + c, now); chLit[c] = {t: now, i: ce.i};", "hitT.set(st * 32 + c, now); chLit[c] = {t: now, i: ce.i}; if (SCN) SCN.hit(ce.i, ce.v);")
rep("hitT.set(e.s * 32 + c, now); chLit[c] = {t: now, i: ce.i};", "hitT.set(e.s * 32 + c, now); chLit[c] = {t: now, i: ce.i}; if (SCN) SCN.hit(ce.i, ce.v);")

# ---- 7. the scene code, at the end of the page's script
js = open(os.path.join(HERE, 'scene.js'), encoding='utf-8').read()
meta = json.load(open(os.path.join(WORK, 'out', 'meta.json')))
assert js.count('/*SMETA*/{}/*END*/') == 1
js = js.replace('/*SMETA*/{}/*END*/', '/*SMETA*/' + json.dumps(meta, separators=(',', ':')) + '/*END*/')
rep("{ const rs = renderSyn; renderSyn = function () { rs(); acidUi(); }; }\n\n</script>\n\n</body></html>", "{ const rs = renderSyn; renderSyn = function () { rs(); acidUi(); }; }\n" + js + "\n</script>\n\n</body></html>")

rep('<meta name="theme-color" content="#05040a">', '<meta name="theme-color" content="#07051a">')

# ---- 8. words that spoke of the old swirling-ghost background
rep("OFF: the buttons float over the ghosts\">GLASS UI: ON</button>", "OFF: the buttons float over the scene\">GLASS UI: ON</button>")
rep("toast(GLASS ? 'GLASS UI on' : 'GLASS UI off: the buttons float over the ghosts'); } }", "toast(GLASS ? 'GLASS UI on' : 'GLASS UI off: the buttons float over the scene'); } }")
rep("b.title = 'GHOST ANIMATION: the swirling ghosts and the falling glyphs behind everything. OFF: they stand still (lighter on your Mac)';", "b.title = 'GHOST ANIMATION: everything that moves in the scene (the drummer, the rain or petals, the signs that flare). OFF: the scene stands still (lighter on your Mac)';")
rep("toast(GHOFF ? 'Ghost animation off: the background stands still (lighter on your Mac)' : 'Ghost animation on');", "toast(GHOFF ? 'Ghost animation off: the scene stands still (lighter on your Mac)' : 'Ghost animation on');")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, PAGE)
open(out, 'w', encoding='utf-8').write(s)
print(f'wrote {out}: {len(s.encode()) / 1e6:.2f} MB (was {len(src.encode()) / 1e6:.2f} MB), art {sum(os.path.getsize(os.path.join(WORK, "out", a["file"])) for a in arts.values()) / 1e6:.2f} MB')
