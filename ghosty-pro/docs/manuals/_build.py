#!/usr/bin/env python3
"""Tiny Markdown -> standalone HTML converter for the manuals (no deps).
Usage: python3 _build.py file.md [...]   (writes file.html next to it)
Supports: # headings, paragraphs, bullet/numbered lists (one nesting level),
tables, > blockquotes, ``` code, ---, **bold**, *em*, `code`, [t](u),
and images on a line of their own: ![caption](path){: .small|.medium|.half} -> <figure>."""
import re, sys, html, pathlib

def inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'(?<![\w*])\*([^*\s][^*]*?)\*(?![\w*])', r'<em>\1</em>', s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)
    return s

def slug(t, seen):
    s = re.sub(r'[^a-z0-9]+', '-', re.sub(r'<[^>]+>', '', t).lower()).strip('-') or 'sec'
    n = s; i = 2
    while n in seen: n = f'{s}-{i}'; i += 1
    seen.add(n); return n

def cells(line):
    line = line.strip().strip('|')
    return [c.strip() for c in re.split(r'(?<!\\)\|', line)]

def convert(md):
    lines = md.split('\n'); out = []; toc = []; seen = set(); i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('```'):
            j = i + 1; buf = []
            while j < len(lines) and not lines[j].startswith('```'): buf.append(lines[j]); j += 1
            out.append('<pre><code>' + html.escape('\n'.join(buf)) + '</code></pre>'); i = j + 1; continue
        m = re.match(r'(#{1,4}) (.+)', l)
        if m:
            n = len(m.group(1)); t = inline(m.group(2)); a = slug(m.group(2), seen)
            if n in (2, 3): toc.append((n, a, t))
            out.append(f'<h{n} id="{a}">{t}</h{n}>'); i += 1; continue
        if re.match(r'-{3,}$', l.strip()): out.append('<hr>'); i += 1; continue
        im = re.match(r'!\[([^\]]*)\]\(([^)]+)\)\s*(?:\{:?\s*([^}]*)\})?\s*$', l.strip())
        if im:
            cls = ' '.join(c.lstrip('.') for c in (im.group(3) or '').split() if c.startswith('.'))
            out.append(f'<figure class="{cls}"><img src="{html.escape(im.group(2))}" alt="{html.escape(im.group(1))}" loading="lazy"><figcaption>{inline(im.group(1))}</figcaption></figure>')
            i += 1; continue
        if l.startswith('|') and i + 1 < len(lines) and re.match(r'\|[\s:|-]+\|?$', lines[i+1].strip()):
            h = cells(l); i += 2; rows = []
            while i < len(lines) and lines[i].startswith('|'): rows.append(cells(lines[i])); i += 1
            t = '<div class="tw"><table><thead><tr>' + ''.join(f'<th>{inline(c)}</th>' for c in h) + '</tr></thead><tbody>'
            for r in rows: t += '<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>'
            out.append(t + '</tbody></table></div>'); continue
        if l.startswith('>'):
            buf = []
            while i < len(lines) and lines[i].startswith('>'): buf.append(lines[i].lstrip('>').strip()); i += 1
            out.append('<blockquote>' + inline(' '.join(buf)) + '</blockquote>'); continue
        if re.match(r'\s*([-*]|\d+\.) ', l):
            # list with simple nesting by indent
            stack = []; res = []
            while i < len(lines) and (re.match(r'\s*([-*]|\d+\.) ', lines[i]) or (lines[i].startswith('  ') and lines[i].strip() and stack)):
                ln = lines[i]
                mm = re.match(r'(\s*)([-*]|\d+\.) (.*)', ln)
                if not mm:
                    res.append(' ' + inline(ln.strip())); i += 1; continue
                ind = len(mm.group(1)) // 2 if len(mm.group(1)) < 4 else 1 + (len(mm.group(1)) - 2) // 3
                ind = min(ind, 1) if len(mm.group(1)) < 4 else 1
                tag = 'ol' if mm.group(2)[0].isdigit() else 'ul'
                while len(stack) > ind + 1:
                    res.append('</li></' + stack.pop() + '>')
                if len(stack) == ind + 1: res.append('</li>')
                if len(stack) < ind + 1:
                    stack.append(tag); res.append('<' + tag + '>')
                res.append('<li>' + inline(mm.group(3))); i += 1
            while stack: res.append('</li></' + stack.pop() + '>')
            out.append(''.join(res)); continue
        if not l.strip(): i += 1; continue
        buf = []
        while i < len(lines) and lines[i].strip() and not re.match(r'(#{1,4} |```|\||>|\s*([-*]|\d+\.) |-{3,}$|!\[)', lines[i]):
            buf.append(lines[i].strip()); i += 1
        if not buf: buf = [l.strip()]; i += 1
        out.append('<p>' + inline(' '.join(buf)) + '</p>')
    return '\n'.join(out), toc

TEMPLATE = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>@@TITLE@@ - manual</title>
<style>
:root{--bg:#14121a;--fg:#e8e6ee;--mut:#a39fb3;--acc:#b58cff;--card:#1e1b27;--line:#34303f}
@media (prefers-color-scheme: light){:root{--bg:#fbfaf7;--fg:#201d28;--mut:#6b667a;--acc:#6a38d0;--card:#f1eef8;--line:#d9d4e6}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:17px/1.6 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{display:flex;gap:32px;max-width:1160px;margin:0 auto;padding:0 16px}
nav{position:sticky;top:0;align-self:flex-start;width:230px;flex:none;max-height:100vh;overflow:auto;padding:24px 0;font-size:14px}
nav a{display:block;color:var(--mut);text-decoration:none;padding:3px 0}nav a.l3{padding-left:14px;font-size:13px}nav a:hover{color:var(--acc)}
main{max-width:820px;min-width:0;padding:24px 0 80px;flex:1}
h1{font-size:2em;margin:.3em 0}h2{margin-top:2em;border-bottom:1px solid var(--line);padding-bottom:.2em}h3{margin-top:1.6em}
a{color:var(--acc)}code{background:var(--card);padding:1px 5px;border-radius:4px;font-size:.9em}
pre{background:var(--card);padding:12px;border-radius:8px;overflow:auto}pre code{background:none;padding:0}
blockquote{margin:1em 0;padding:.6em 1em;background:var(--card);border-left:4px solid var(--acc);border-radius:4px}
.tw{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:15px}th,td{border:1px solid var(--line);padding:6px 9px;text-align:left;vertical-align:top}th{background:var(--card)}
hr{border:0;border-top:1px solid var(--line)}
figure{margin:1.2em 0;text-align:center}figure img{max-width:100%;height:auto;border-radius:6px;border:1px solid var(--line)}figcaption{font-size:14px;color:var(--mut);margin-top:.3em}figure.medium{max-width:66%;margin-left:auto;margin-right:auto}figure.small{max-width:45%;margin-left:auto;margin-right:auto}figure.half{display:inline-block;width:48%;vertical-align:top;margin:.6em .5%}
@media (max-width:860px){nav{display:none}.wrap{display:block}}
</style></head><body><div class="wrap"><nav>@@TOC@@</nav><main>
@@BODY@@
</main></div></body></html>'''

for f in sys.argv[1:]:
    p = pathlib.Path(f); body, toc = convert(p.read_text())
    m = re.search(r'^# (.+)$', p.read_text(), re.M); title = html.escape(m.group(1) if m else p.stem)
    t = ''.join(f'<a class="l{n}" href="#{a}">{x}</a>' for n, a, x in toc)
    p.with_suffix('.html').write_text(TEMPLATE.replace('@@TITLE@@', title).replace('@@TOC@@', t).replace('@@BODY@@', body))
    print('wrote', p.with_suffix('.html'))
