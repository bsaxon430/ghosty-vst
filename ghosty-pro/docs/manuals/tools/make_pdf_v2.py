#!/usr/bin/env python3
"""Designed PDF for a GhostBlock manual (v2 template: chapter openers, numbered figures,
Tip / Note / Warning / Try this boxes, a 'Find it fast' control index with page numbers).

Usage:
  make_pdf_v2.py manual.md out.pdf "Product" "Tagline" cover.png [violet|green]

Markdown conventions (all optional):
  ![Caption](img/x.png)            -> numbered figure "Figure N  Caption" kept with its caption
  > **Tip:** ... / **Note:** / **Warning:** / **Try this:**   -> coloured boxes
  # Part title                     -> a part divider page (the first '# ' line is the title and is dropped)
  ## Chapter                       -> a new chapter page with a number ("Reference" chapters get a letter)
  In chapters whose title starts with "Reference", every table row whose first cell is **BOLD**
  becomes an entry of the 'Find it fast' index (control -> page), grouped by the nearest ### heading.
Needs: python-markdown, weasyprint.
"""
import sys, re, os, html as H
import markdown, weasyprint

md_path, out_pdf, product, tagline, cover_img = sys.argv[1:6]
accent = sys.argv[6] if len(sys.argv) > 6 else 'violet'
base = os.path.dirname(os.path.abspath(md_path))
src = open(md_path, encoding='utf-8').read()
src = re.sub(r'\A# [^\n]*\n', '', src)                  # the title: we make our own cover
if re.search(r'^# ', src, re.M):                           # text before the first part (intro, contents list)
    src = src[re.search(r'^# ', src, re.M).start():]       # is replaced by the cover and our own contents
else:
    src = re.sub(r'^## Contents\n.*?(?=^#{1,2} )', '', src, flags=re.S | re.M)
body = markdown.markdown(src, extensions=['tables', 'fenced_code', 'toc', 'sane_lists', 'attr_list'])

# ---- figures: a paragraph that is only an image -> numbered figure
fig_n = [0]
def figure(m):
    alt, srcp = m.group(2), m.group(1)
    fig_n[0] += 1
    cls = ' class="small"' if '|small' in alt else (' class="half"' if '|half' in alt else '')
    alt = alt.replace('|small', '').replace('|half', '')
    p = srcp if srcp.startswith(('/', 'file:')) else os.path.join(base, srcp)
    return (f'<figure{cls}><img src="file://{p}" alt="{alt}"><figcaption><b>Figure {fig_n[0]}</b>'
            f'&ensp;{alt}</figcaption></figure>')
body = re.sub(r'<p>\s*<img alt="([^"]*)" src="([^"]+)"\s*/?>\s*</p>',
              lambda m: figure(type('M', (), {'group': lambda s, i: (None, m.group(2), m.group(1))[i]})()), body)

# ---- callout boxes
KINDS = {'tip': 'Tip', 'note': 'Note', 'warning': 'Warning', 'try': 'Try this', 'demo': 'Demo'}
def box(m):
    inner = m.group(1)
    k = re.match(r'\s*<p>\s*<strong>(Tip|Note|Warning|Try this|Demo)[:.]?</strong>:?\s*', inner)
    if not k: return '<aside class="box note">' + inner + '</aside>'
    kind = {'Tip': 'tip', 'Note': 'note', 'Warning': 'warning', 'Try this': 'try', 'Demo': 'demo'}[k.group(1)]
    inner = '<p>' + inner[k.end():]
    return f'<aside class="box {kind}"><div class="boxh">{KINDS[kind]}</div>{inner}</aside>'
body = re.sub(r'<blockquote>(.*?)</blockquote>', box, body, flags=re.S)

# ---- parts (# headings left in the body) and chapters
body = re.sub(r'<h1 id="([^"]+)">(.*?)</h1>', r'<div class="part" id="\1"><div class="pl">PART</div><h1>\2</h1></div>', body)

# ---- 'Find it fast' index from the reference chapters
index = []
SKIP = ('factory content', 'tooltip texts', 'keyboard shortcuts')
out = []; in_ref = False; ctx = ''; n = 0
for piece in re.split(r'(<h2 id="[^"]+">.*?</h2>|<h3 id="[^"]+">.*?</h3>|<tr>.*?</tr>)', body, flags=re.S):
    m2 = re.match(r'<h2 id="[^"]+">(.*?)</h2>', piece)
    m3 = re.match(r'<h3 id="[^"]+">(.*?)</h3>', piece)
    if m2:
        t2 = re.sub('<[^>]+>', '', m2.group(1)).strip().lower()
        in_ref = t2.startswith('reference') and not any(k in t2 for k in SKIP); ctx = ''
    elif m3:
        ctx = re.sub('<[^>]+>', '', m3.group(1)).strip()
        ctx = re.sub(r'^[A-Z]?\d+(\.\d+)*\s*', '', ctx)
    elif in_ref and piece.startswith('<tr>'):
        c = re.match(r'<tr>\s*<td>(.*?)</td>', piece, flags=re.S)
        if c:
            names = re.findall(r'<strong>(.*?)</strong>', c.group(1))
            if names and c.group(1).strip().startswith('<strong>'):
                n += 1; aid = f'fx{n}'
                piece = piece.replace('<tr>', f'<tr id="{aid}">', 1)
                for raw in names[:4]:
                    raw = H.unescape(re.sub('<[^>]+>', '', raw)).strip()
                    for nm in re.split(r', | / ', raw):
                        nm = nm.strip().rstrip('.')
                        if 1 < len(nm) < 40: index.append((nm, ctx, aid))
    out.append(piece)
body = ''.join(out)

def sortkey(e):
    return (re.sub(r'[^a-z0-9]', '', e[0].lower()) or e[0].lower(), e[1].lower())
seen = set(); entries = []
for e in sorted(index, key=sortkey):
    k = (e[0].upper(), e[1])
    if k in seen: continue
    seen.add(k); entries.append(e)
idx_html = ''
letter = ''
for nm, ctx, aid in entries:
    k0 = sortkey((nm, ctx, aid))[0][:1]
    L = k0.upper() if k0.isalpha() else '#'
    if L != letter:
        letter = L; idx_html += f'<li class="il">{H.escape(L)}</li>'
    idx_html += (f'<li><a href="#{aid}"><b>{H.escape(nm).replace("-", "&#8209;")}</b> <span class="ic">{H.escape(ctx)}</span></a></li>')

# ---- contents (chapters + parts)
toc = ''
for m in re.finditer(r'<div class="part" id="([^"]+)"><div class="pl">PART</div><h1>(.*?)</h1></div>|<h2 id="([^"]+)">(.*?)</h2>', body):
    if m.group(1): toc += f'<li class="tp"><a href="#{m.group(1)}">{m.group(2)}</a></li>'
    else:          toc += f'<li><a href="#{m.group(3)}">{m.group(4)}</a></li>'

ACC = '#a98bff' if accent == 'violet' else '#3cff8f'
ACC2 = '#5b2fd6' if accent == 'violet' else '#0b7a5c'
css = """
@page { size: Letter; margin: 21mm 17mm 19mm 17mm;
  @top-left { content: string(product); font: 600 8.5pt 'DejaVu Sans'; color: #8a82a8; }
  @top-right { content: string(section); font: 8.5pt 'DejaVu Sans'; color: #8a82a8; }
  @bottom-center { content: counter(page); font: 9pt 'DejaVu Sans'; color: #8a82a8; } }
@page cover { margin: 0; @top-left{content:none} @top-right{content:none} @bottom-center{content:none} }
@page front { @top-right{content:none} }
@page part { @top-left{content:none} @top-right{content:none} @bottom-center{content:none} }
html { font-family: 'DejaVu Sans', sans-serif; }
body { font-size: 10.8pt; line-height: 1.5; color: #1f1a33; counter-reset: chap; }
.cover { page: cover; height: 279.4mm; background: #0d0a16; color: #fff; position: relative; }
.cover .band { height: 6mm; background: ACC; }
.cover .inner { padding: 22mm 18mm 0 18mm; }
.cover h1 { font: 800 40pt 'DejaVu Sans'; margin: 0 0 3mm; letter-spacing: .5pt; string-set: product content(); }
.cover .tag { font: 15pt 'DejaVu Sans'; color: #cfc6ee; margin: 0 0 9mm; }
.cover img { width: 100%; border-radius: 3mm; border: 1px solid #3a3160; }
.cover .meta { position: absolute; bottom: 16mm; left: 18mm; right: 18mm; font: 10pt 'DejaVu Sans'; color: #a99fd0; }
.cover .meta b { color: ACC; }
.front { page: front; break-before: page; }
.front h2.fh { counter-increment: none; string-set: none; break-before: avoid; font-size: 20pt; color: #1f1a33; background: none; border: none; padding: 0; margin: 0 0 4mm; }
.front h2.fh::before { content: none; }
.toc ol { padding: 0; list-style: none; margin: 0; }
.toc li { margin: 1.05mm 0; font-size: 10.4pt; }
.toc li.tp { margin-top: 3.6mm; font-weight: 700; color: ACC2; text-transform: uppercase; font-size: 9.5pt; letter-spacing: .6pt; }
.toc li.tp a { color: ACC2; }
.toc a { color: #1f1a33; text-decoration: none; }
.toc li:not(.tp) a::after { content: leader('.') target-counter(attr(href), page); color: #8a82a8; }
.toc li.tp a::after { content: leader(' ') target-counter(attr(href), page); }
.findit p.lead { color: #5a5378; font-size: 9.5pt; margin-bottom: 3mm; }
.findit .cols { columns: 2; column-gap: 8mm; }
.findit ol { padding: 0; margin: 0; list-style: none; font-size: 8.4pt; line-height: 1.3; }
.findit li { margin: 0 0 .6mm; break-inside: avoid; }
.findit li.il { font: 700 10pt 'DejaVu Sans'; color: ACC2; margin: 2.2mm 0 .8mm; border-bottom: 1px solid #e3def2; }
.findit a { color: #1f1a33; text-decoration: none; }

.findit a::after { content: leader(' ') target-counter(attr(href), page); color: #5a5378; }
.findit .ic { color: #8a82a8; font-size: 7.6pt; }
.part { page: part; break-before: page; break-after: page; height: 230mm; background: #14101f; color: #fff; margin: -21mm -17mm 0 -17mm; padding: 90mm 20mm 0 20mm; }
.part .pl { font: 700 10pt 'DejaVu Sans'; letter-spacing: 3pt; color: ACC; }
.part h1 { font: 800 30pt 'DejaVu Sans'; margin: 3mm 0 0; color: #fff; string-set: section content(); }
h2 { string-set: section content(); break-before: page; counter-increment: chap; counter-reset: fig;
     font: 700 19pt 'DejaVu Sans'; color: #fff; background: #1b1530; border-left: 3mm solid ACC; padding: 4.5mm 5mm 4mm; margin: 0 0 5mm; }
h2::before { content: "CHAPTER " counter(chap); display: block; font: 700 8pt 'DejaVu Sans'; letter-spacing: 2.4pt; color: ACC; margin-bottom: 1.2mm; }
h2.ref::before { content: "REFERENCE"; }
h3 { font: 700 13pt 'DejaVu Sans'; color: ACC2; margin: 6mm 0 2mm; border-bottom: 1px solid #e3def2; padding-bottom: 1mm; break-after: avoid; }
h4 { font: 700 11pt 'DejaVu Sans'; margin: 4.5mm 0 1.2mm; break-after: avoid; color: #241c3d; }
h2 + p, h3 + p { break-before: avoid; }
p { margin: 0 0 2.5mm; orphans: 2; widows: 2; }
ul, ol { margin: 0 0 3mm; padding-left: 6mm; } li { margin: .7mm 0; }
strong { color: #120e22; }
code { font: 9pt 'DejaVu Sans Mono'; background: #f1eef9; padding: .3mm 1.2mm; border-radius: 1mm; }
pre { background: #f6f4fb; padding: 3mm; border-radius: 2mm; font-size: 8.6pt; white-space: pre-wrap; }
table { width: 100%; border-collapse: collapse; margin: 2mm 0 4mm; font-size: 9.1pt; line-height: 1.38; }
thead { display: table-header-group; }
th { background: #241c3d; color: #fff; text-align: left; padding: 1.5mm 2mm; font-weight: 700; }
td { padding: 1.3mm 2mm; border-bottom: 1px solid #e6e1f3; vertical-align: top; overflow-wrap: break-word; hyphens: auto; }
td:first-child { min-width: 22mm; }
tr:nth-child(even) td { background: #faf9fd; } tr { break-inside: avoid; }
.box { margin: 3mm 0 3.5mm; padding: 2.6mm 4mm 2.6mm 4mm; border-radius: 1.8mm; border-left: 2mm solid; break-inside: avoid; }
.box p { margin: 0 0 1.4mm; } .box p:last-child { margin: 0; }
.box ol, .box ul { margin: 1mm 0 0; }
.boxh { font: 700 8pt 'DejaVu Sans'; letter-spacing: 1.6pt; text-transform: uppercase; margin-bottom: 1mm; }
.box.tip { background: #eefaf3; border-color: #22b573; color: #173b2a; } .tip .boxh { color: #138a55; }
.box.note { background: #eef3fd; border-color: #3d7be0; color: #172a4a; } .note .boxh { color: #2a62c0; }
.box.warning { background: #fff3ec; border-color: #ef7a2c; color: #4a2410; } .warning .boxh { color: #c95a12; }
.box.try { background: #f4effe; border-color: ACC2; color: #26164f; } .try .boxh { color: ACC2; }
.box.demo { background: #f6f5f8; border-color: #8a82a8; color: #3a3550; } .demo .boxh { color: #6a6388; }
figure { margin: 3mm 0 4.5mm; break-inside: avoid; text-align: center; }
figure img { max-width: 100%; max-height: 150mm; border-radius: 1.6mm; border: 1px solid #d9d3ea; }
figure.half img { max-width: 62%; }
figure.small { float: right; width: 40%; margin: 1mm 0 3mm 5mm; } figure.small img { max-width: 100%; max-height: 120mm; }
h2, h3 { clear: both; }
figcaption { font-size: 8.6pt; color: #5a5378; margin-top: 1.4mm; text-align: center; break-before: avoid; }
figcaption b { color: ACC2; }
hr { border: none; border-top: 1px solid #e3def2; margin: 4mm 0; }
a { color: ACC2; text-decoration: none; }
""".replace('ACC2', ACC2).replace('ACC', ACC)

# reference chapters get a different opener label
body = re.sub(r'<h2 id="([^"]+)">(Reference[^<]*)</h2>', r'<h2 class="ref" id="\1">\2</h2>', body)

doc = f"""<html><head><meta charset=utf-8><style>{css}</style></head><body>
<div class=cover><div class=band></div><div class=inner><h1>{H.escape(product)}</h1><div class=tag>{H.escape(tagline)}</div>
<img src="file://{os.path.abspath(cover_img)}"></div><div class=meta><b>User manual</b> &nbsp;·&nbsp; version 3.1.0 &nbsp;·&nbsp; VST3 · AU · CLAP · Standalone<br>GhostBlock</div></div>
<div class="front toc"><h2 class=fh>Contents</h2><ol>{toc}<li><a href="#findit">Find it fast: every control A to Z</a></li></ol></div>
{body}
<div class="front findit" id="findit"><h2 class=fh style="string-set: section 'Find it fast'">Find it fast: every control A to Z</h2>
<p class=lead>Every control in the reference chapters, A to Z, with the page it is described on. The grey word says which page or panel of Spectrum it belongs to.</p>
<div class=cols><ol>{idx_html}</ol></div></div>
</body></html>"""
weasyprint.HTML(string=doc, base_url=base).write_pdf(out_pdf)
print('ok', out_pdf, os.path.getsize(out_pdf), 'figures', fig_n[0], 'index entries', len(entries))
