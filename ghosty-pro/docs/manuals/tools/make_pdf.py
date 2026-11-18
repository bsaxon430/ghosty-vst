"""Manual Markdown -> designed PDF (WeasyPrint).

Classic usage (unchanged, still works exactly as before):
  make_pdf.py MD OUT_PDF PRODUCT TAGLINE COVER_IMG ACCENT ["heading-substring=img|caption" ...]
  ACCENT: violet | green

Optional flags (anywhere on the line; all off by default, so old calls render as before):
  --style=v2            the newer template: 11 pt body, chapter openers, numbered figures,
                        Markdown images ![caption](path){: .small|.medium|.half|.tiny} become figures,
                        > **Tip:** / **Note:** / **Warning:** / **Try this:** become coloured boxes,
                        headings kept with what follows, cleaner tables.
                        Chapter headings "## 7. Title" get a number badge; "## Reference A. Title" an
                        appendix badge; the first paragraph after a chapter heading is set as its lead.
  --index=FILE          adds a 'Find it fast' page: one term per line ("TERM" or "TERM @ heading words");
                        each links to the first **TERM** (bold) in the manual (inside that heading's
                        chapter when given) and shows its page number.
  --version=3.1.0       version shown on the cover (default 3.1.0)
  --formats="VST3 · AU · CLAP · Standalone"   formats line on the cover
  --cover-note="..."    extra small line on the cover
Image paths in the Markdown are resolved relative to the Markdown file.
"""
import sys, re, os, markdown, weasyprint, html as H

opts = {}; pos = []
for a in sys.argv[1:]:
    if a.startswith('--') and '=' in a: k, v = a[2:].split('=', 1); opts[k] = v
    elif a.startswith('--'): opts[a[2:]] = '1'
    else: pos.append(a)
md_path, out_pdf, product, tagline, cover_img, accent = pos[:6]
figs = [a.split('=', 1) for a in pos[6:]]   # "heading-substring=img|caption"
v2 = opts.get('style') == 'v2'
version = opts.get('version', '3.1.0'); formats = opts.get('formats', 'VST3 · AU · CLAP · Standalone')
md_dir = os.path.dirname(os.path.abspath(md_path))

raw = open(md_path, encoding='utf-8').read(); src = raw
# drop the markdown's own title block + manual contents list (we make our own cover + contents)
src = re.sub(r'^## Contents\n.*?(?=^## )', '', src, flags=re.S|re.M)
src = re.sub(r'\A# [^\n]*\n', '', src)
if v2:   # the intro paragraph under the title belongs to the cover, not page 1
    src = re.sub(r'\A\s*\*\*[^\n]*\n', '', src)
body = markdown.markdown(src, extensions=['tables','fenced_code','toc','sane_lists','attr_list'])
# figures after the first heading that contains the key
for key, val in figs:
    img, cap = (val.split('|',1)+[''])[:2]
    pat = re.compile(r'(<h[23][^>]*>(?:(?!</h[23]>).)*' + re.escape(key) + r'(?:(?!</h[23]>).)*</h[23]>)', re.I|re.S)
    fig = f'<figure><img src="file://{img}"><figcaption>{H.escape(cap)}</figcaption></figure>'
    body, n = pat.subn(lambda m: m.group(1) + fig, body, count=1)

def absimg(s):
    return s if re.match(r'(file|https?):', s) else 'file://' + os.path.normpath(os.path.join(md_dir, H.unescape(s)))

if v2:
    # Markdown images -> numbered figures
    def figure(m):
        tag = m.group(1)
        alt = re.search(r'alt="([^"]*)"', tag); srcm = re.search(r'src="([^"]*)"', tag); cls = re.search(r'class="([^"]*)"', tag)
        c = cls.group(1) if cls else ''
        return f'<figure class="fig {c}"><img src="{absimg(srcm.group(1))}"><figcaption>{alt.group(1) if alt else ""}</figcaption></figure>'
    body = re.sub(r'<p>\s*(<img [^>]*>)\s*</p>', figure, body)
    # callout boxes
    def box(m):
        inner = m.group(1); k = re.match(r'\s*<p>\s*<strong>(Tip|Note|Warning|Try this):</strong>', inner)
        if not k: return m.group(0)
        kind = k.group(1).lower().replace(' ', '')
        inner = inner.replace(k.group(0), f'<p><span class="lbl">{k.group(1)}</span>', 1)
        return f'<div class="box {kind}">{inner}</div>'
    body = re.sub(r'<blockquote>(.*?)</blockquote>', box, body, flags=re.S)
    # chapter openers: "7. The four lanes" -> number badge + title; "Reference A. ..." -> appendix badge
    def h2(m):
        ident, text = m.group(1), m.group(2)
        n = re.match(r'(\d+)\.\s+(.*)', text); r = re.match(r'Reference ([A-Z])\.\s+(.*)', text)
        if n: return f'<h2 id="{ident}" class="ch" data-t="{H.escape(re.sub("<[^>]+>", "", n.group(2)), True)}"><span class="num">{n.group(1)}</span>{n.group(2)}</h2>'
        if r: return f'<h2 id="{ident}" class="ch ref" data-t="{H.escape(re.sub("<[^>]+>", "", r.group(2)), True)}"><span class="num">{r.group(1)}</span><span class="kick">Reference</span>{r.group(2)}</h2>'
        return m.group(0)
    body = re.sub(r'<h2 id="([^"]+)">(.*?)</h2>', h2, body)
    # each chapter's first paragraph is its 'what this is for' lead
    body = re.sub(r'(</h2>\s*)<p>', r'\1<p class="lead">', body)

toc_items = re.findall(r'<h2 id="([^"]+)"[^>]*>(.*?)</h2>', body)
def toc_label(t):
    t = re.sub(r'<span class="kick">.*?</span>', '', t)
    return re.sub(r'<span class="num">(.*?)</span>', r'<span class="tn">\1</span>', t)
toc = ''.join(f'<li class="{"r" if "kick" in t else ""}"><a href="#{i}">{toc_label(t)}</a></li>' for i, t in toc_items)

index_html = ''
if v2 and opts.get('index'):
    terms = [l.strip() for l in open(opts['index'], encoding='utf-8') if l.strip() and not l.startswith('#')]
    entries = []
    for k, line in enumerate(terms):
        term, _, sect = (p.strip() for p in line.partition('@'))
        start = 0
        if sect:
            hm = re.search(r'<h2 [^>]*>(?:(?!</h2>).)*' + re.escape(H.escape(sect, False)) + r'(?:(?!</h2>).)*</h2>', body, re.I | re.S)
            if hm: start = hm.end()
            else: print('index: heading not found:', sect, file=sys.stderr)
        pat = re.compile(r'<strong>' + re.escape(H.escape(term, False)) + r'</strong>')
        m = pat.search(body, start) or pat.search(body)
        if not m: print('index: term not found:', term, file=sys.stderr); continue
        ident = f'ix{k}'
        body = body[:m.start()] + f'<strong id="{ident}">' + body[m.start() + 8:]
        entries.append((term, ident))
    entries.sort(key=lambda e: re.sub(r'[^A-Za-z0-9]+', ' ', e[0]).strip().lower() or e[0])
    index_html = ('<div class="findit" id="ixpage"><h2 class="ch ix" data-t="Find it fast"><span class="num">&#8594;</span>Find it fast</h2>'
                  '<p class="lead">Every control and feature by its name on screen, with the page where it is explained.</p><ul>'
                  + ''.join(f'<li><a href="#{i}">{H.escape(t)}</a></li>' for t, i in entries) + '</ul></div>')
    toc += '<li class="r"><a href="#ixpage"><span class="tn">&#8594;</span>Find it fast</a></li>'

A2 = '#5b2fd6' if accent == 'violet' else '#0b7a5c'; A1 = '#a98bff' if accent == 'violet' else '#3cff8f'
css = """
@page { size: Letter; margin: 22mm 18mm 20mm 18mm;
  @top-left { content: string(product); font: 600 8.5pt 'DejaVu Sans'; color: #8a82a8; }
  @top-right { content: string(section); font: 8.5pt 'DejaVu Sans'; color: #8a82a8; }
  @bottom-center { content: counter(page); font: 9pt 'DejaVu Sans'; color: #8a82a8; } }
@page cover { margin: 0; @top-left{content:none} @top-right{content:none} @bottom-center{content:none} }
@page toc { @top-right{content:none} }
body { font: 10.6pt/1.55 'DejaVu Sans', sans-serif; color: #1f1a33; }
.cover { page: cover; height: 279.4mm; background: #0d0a16; color: #fff; position: relative; padding: 0; }
.cover .band { height: 6mm; background: ACC; }
.cover .inner { padding: 26mm 20mm 0 20mm; }
.cover h1 { font: 800 40pt 'DejaVu Sans'; margin: 0 0 4mm; letter-spacing: .5pt; string-set: product content(); }
.cover .tag { font: 15pt 'DejaVu Sans'; color: #cfc6ee; margin: 0 0 10mm; }
.cover img { width: 100%; border-radius: 4mm; border: 1px solid #3a3160; }
.cover .meta { position: absolute; bottom: 18mm; left: 20mm; right: 20mm; font: 10pt 'DejaVu Sans'; color: #a99fd0; }
.cover .meta b { color: ACC; }
.tocpage { page: toc; page-break-after: always; }
.tocpage h2 { border: none; color: #1f1a33; font-size: 20pt; margin-top: 0; padding: 0; background: none; }
.tocpage ol { padding-left: 0; list-style: none; columns: 1; }
.tocpage li { margin: 2.2mm 0; font-size: 11pt; }
.tocpage a { color: #1f1a33; text-decoration: none; }
.tocpage a::after { content: leader('.') target-counter(attr(href), page); color: #8a82a8; }
h2 { string-set: section content(); page-break-before: always; font: 700 18pt 'DejaVu Sans'; color: #fff; background: #1b1530; border-left: 3mm solid ACC; padding: 4mm 5mm; margin: 0 0 5mm; }
h3 { font: 700 13pt 'DejaVu Sans'; color: ACC2; margin: 7mm 0 2mm; border-bottom: 1px solid #e3def2; padding-bottom: 1mm; }
h4 { font: 700 11pt 'DejaVu Sans'; margin: 5mm 0 1mm; }
p { margin: 0 0 2.6mm; } ul, ol { margin: 0 0 3mm; padding-left: 6mm; } li { margin: .8mm 0; }
strong { color: #120e22; }
code { font: 9pt 'DejaVu Sans Mono'; background: #f1eef9; padding: .3mm 1.2mm; border-radius: 1mm; }
pre { background: #f6f4fb; padding: 3mm; border-radius: 2mm; font-size: 8.6pt; white-space: pre-wrap; }
table { width: 100%; border-collapse: collapse; margin: 2mm 0 4mm; font-size: 9.2pt; page-break-inside: auto; }
th { background: #241c3d; color: #fff; text-align: left; padding: 1.6mm 2mm; font-weight: 700; }
td { padding: 1.4mm 2mm; border-bottom: 1px solid #e6e1f3; vertical-align: top; }
tr:nth-child(even) td { background: #faf9fd; } tr { page-break-inside: avoid; }
blockquote { margin: 3mm 0; padding: 3mm 4mm; background: #f2fbf6; border-left: 2mm solid #22b573; border-radius: 1.5mm; color: #173b2a; }
blockquote p { margin: 0; }
figure { margin: 3mm 0 5mm; page-break-inside: avoid; } figure img { width: 100%; border-radius: 2mm; border: 1px solid #d9d3ea; }
figcaption { font-size: 8.6pt; color: #6a6388; margin-top: 1.2mm; text-align: center; }
hr { border: none; border-top: 1px solid #e3def2; margin: 5mm 0; }
a { color: ACC2; }
"""
css_v2 = """
body { font: 11pt/1.5 'DejaVu Sans', sans-serif; counter-reset: fig; }
hr { display: none; }
h2.ch { string-set: section attr(data-t); page-break-before: always; background: none; border: none; color: #1b1530;
  font: 700 23pt/1.15 'DejaVu Sans'; padding: 0 0 4mm; margin: 0 0 6mm; border-bottom: 1.4mm solid ACC; }
h2.ch .num { display: block; font: 800 34pt/1 'DejaVu Sans'; color: ACC2; margin-bottom: 2mm; }
h2.ch.ref .num, h2.ch.ix .num { color: #8a82a8; }
h2.ch .kick { display: block; font: 700 9pt 'DejaVu Sans'; letter-spacing: 1.5pt; text-transform: uppercase; color: #8a82a8; margin-bottom: 1mm; }
p.lead { font-size: 12pt; line-height: 1.5; color: #2b2347; margin-bottom: 4mm; }
h2, h3, h4 { page-break-after: avoid; break-after: avoid; }
h3 { font-size: 13.5pt; margin-top: 8mm; }
p { orphans: 3; widows: 3; }
strong { font-weight: 700; color: #120e22; }
table { font-size: 9.6pt; margin: 3mm 0 5mm; }
th { background: #241c3d; padding: 1.8mm 2.2mm; } td { padding: 1.6mm 2.2mm; line-height: 1.4; }
thead { display: table-header-group; }
figure.fig { margin: 4mm auto 6mm; page-break-inside: avoid; break-inside: avoid; text-align: center; }
figure.fig img { width: 100%; border-radius: 1.5mm; border: 1px solid #d9d3ea; }
figure.fig figcaption { counter-increment: fig; font-size: 9pt; color: #5d5680; margin-top: 1.6mm; text-align: center; }
figure.fig figcaption::before { content: "Figure " counter(fig) ". "; font-weight: 700; color: ACC2; }
figure.breakafter { page-break-after: always; break-after: page; }
figure.medium { width: 66%; } figure.small { width: 44%; } figure.tiny { width: 30%; }
figure.half { display: inline-block; width: 48.6%; vertical-align: top; margin: 2mm 0 4mm; }
.box { margin: 4mm 0; padding: 3mm 4mm; border-radius: 1.8mm; border-left: 2.2mm solid; page-break-inside: avoid; }
.box p { margin: 0; } .box p + p { margin-top: 2mm; }
.box .lbl { font-weight: 800; margin-right: 1.5mm; text-transform: uppercase; font-size: 9pt; letter-spacing: .6pt; }
.box.tip { background: #eefaf3; border-color: #1fa463; color: #12392a; } .box.tip .lbl { color: #148a50; }
.box.trythis { background: #f1edff; border-color: #7a52f0; color: #261b4d; } .box.trythis .lbl { color: #5b2fd6; }
.box.note { background: #edf5fd; border-color: #2f80d1; color: #13304d; } .box.note .lbl { color: #2369b0; }
.box.warning { background: #fff4e8; border-color: #e07a12; color: #4a2a08; } .box.warning .lbl { color: #b85d00; }
.tocpage li { margin: 1.05mm 0; font-size: 10.2pt; }
.tocpage li .tn { display: inline-block; width: 8mm; color: ACC2; font-weight: 700; }
.tocpage li.r { font-size: 9.8pt; }
.tocpage li.r .tn { color: #8a82a8; }
.findit ul { columns: 3; column-gap: 6mm; list-style: none; padding: 0; font-size: 8.4pt; }
.findit li { margin: .35mm 0; break-inside: avoid; }
.findit a { color: #1f1a33; text-decoration: none; }
.findit a::after { content: leader('.') target-counter(attr(href), page); color: #8a82a8; }
.cover .inner { padding-top: 22mm; }
.cover .lede { font: 11.5pt/1.5 'DejaVu Sans'; color: #d9d2f5; margin: -4mm 0 9mm; }
.cover .lede strong { color: #fff; }
"""
if v2: css += css_v2
css = css.replace('ACC2', A2).replace('ACC', A1)
lede = ''
if v2:
    m = re.search(r'\A# [^\n]*\n\s*(\*\*[^\n]*)', raw)
    if m: lede = '<div class=lede>' + markdown.markdown(m.group(1))[3:-4] + '</div>'
note = f'<br>{H.escape(opts["cover-note"])}' if opts.get('cover-note') else ''
doc = f"""<html><head><meta charset=utf-8><style>{css}</style></head><body>
<div class=cover><div class=band></div><div class=inner><h1>{H.escape(product)}</h1><div class=tag>{H.escape(tagline)}</div>{lede}
<img src="file://{os.path.abspath(cover_img)}"></div><div class=meta><b>User manual</b> &nbsp;·&nbsp; version {H.escape(version)} &nbsp;·&nbsp; {H.escape(formats)}<br>GhostBlock{note}</div></div>
<div class=tocpage><h2 style="string-set:none">Contents</h2><ol>{toc}</ol></div>
{body}{index_html}</body></html>"""
weasyprint.HTML(string=doc, base_url='/').write_pdf(out_pdf)
print('ok', out_pdf, os.path.getsize(out_pdf))
