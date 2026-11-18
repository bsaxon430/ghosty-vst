#!/usr/bin/env python3
"""Builds ghost-lab/Source/Core/HoodCatalog.inc from the usage text the real CDP programs print.
   gen-hood-catalog.py <dir-with-the-cdp-programs> <out.inc>
Every program is started with no arguments / just its process name (CDP prints its own usage and exits), the text is kept
VERBATIM, and the parameter lists are read from the USAGE lines in that text. Nothing about CDP is typed in by hand,
except the short OVERRIDES list below for usage lines that are not in the usual shape."""
import re, subprocess, sys, os, json

PROGS = ["pvoc", "stretch", "strange", "focus", "blur", "modify", "combine", "morph", "repitch", "formants", "envel"]
# the programs that make the data files combine make / make2 need (pitch, formant and envelope data from an analysis): only
# the processes that make those files are on the page (the rest of those programs work on pitch / envelope files, not sounds)
ONLY = {"repitch": ["getpitch", "analenv"], "formants": ["get"], "envel": ["extract"]}
PROGTITLE = {'pvoc': 'SPECTRAL ANALYSIS AND RESYNTHESIS', 'modify': 'SOUND MODIFICATIONS (TIME DOMAIN)', 'envel': 'ENVELOPE OPERATIONS',
             'repitch': 'PITCH DATA FROM SPECTRAL FILES', 'formants': 'FORMANT DATA FROM SPECTRAL FILES'}
PROGRE = '|'.join(PROGS)
IN_TOK = re.compile(r'^(infile\d*|infl\d*|infile|inanalfile|sndfile|input_sndfile)$')
# output words that are data files, not sounds, and the extension the engine knows them by
DATA_OUT = {'pfil': '.frq', 'bfil': '.txt', 'outenvfile': '.evl', 'outbrkfile': '.txt', 'outpitchfile': '.frq', 'outtransposfile': '.trn'}
OUT_OVERRIDE = {('formants', 'get'): '.for'}   # formants get writes "outfile", which is a formant data file

def run(exe, *a):
    r = subprocess.run([exe, *a], capture_output=True, text=True, timeout=20)
    return (r.stdout + r.stderr).replace('\r', '')

def names_of(text):
    names, on = [], False
    for ln in text.split('\n'):
        if 'where NAME' in ln: on = True; continue
        if on and not ln.startswith('Type'):
            names += ln.split()
    return names

def title_of(text):
    for ln in text.split('\n'):
        s = ln.strip()
        if not s or s.startswith('CDP Release') or s.startswith('USAGE') or s.startswith('MODES') or s.startswith('OR:'): continue
        if re.match(r'^(' + PROGRE + r')\s', s) or s[0] in '[-(': continue
        return s
    return ''

def expand(spec):
    m = re.match(r'^(\d+)-(\d+)$', spec)
    if m: return list(range(int(m[1]), int(m[2]) + 1))
    if re.match(r'^\d+$', spec): return [int(spec)]
    return []

def usage_lines(prog, name, text):
    out, lines = [], text.split('\n')
    pat = re.compile(r'^\s*(?:USAGE:\s*|OR:\s*)?' + prog + r'\s+' + re.escape(name) + r'\b(.*)$')
    i = 0
    while i < len(lines):
        m = pat.match(lines[i])
        if m:
            rest = m[1]
            j = i + 1
            while j < len(lines) and lines[j].strip() and re.match(r'^\s{2,}[a-z\[\-]', lines[j]) and ':' not in lines[j] and not pat.match(lines[j]) and not lines[j].strip().startswith(('OR:', 'USAGE')):
                rest += ' ' + lines[j].strip(); j += 1
            out.append(rest.strip()); i = j
        else: i += 1
    return out

def mode_labels(text):
    """the MODES list in CDP's own text: '1) PITCHSHIFT   4) SCRAMBLE', '(1) mean channel amp', '1   freeze channel amplitudes'"""
    labels, on = {}, False
    for ln in text.split('\n'):
        if re.search(r'\bMODES?\b', ln, re.I) and not ln.strip().startswith(('USAGE', 'OR:')) and not re.match(r'^(' + PROGRE + r')\s', ln.strip()):
            on = True; continue
        if not on: continue
        if not ln.strip():
            if labels: on = False
            continue
        hit = False
        for m in re.finditer(r'(?:^|\s)\(?(\d{1,2})\)\s*([A-Za-z][A-Za-z0-9 \-/&,\'\.:()]*?)(?=\s{2,}|\s\(?\d{1,2}\)|$)', ln):
            labels.setdefault(int(m[1]), m[2].strip()); hit = True
        m = re.match(r'^\s{0,3}(\d{1,2})[\s\t]+([A-Za-z].*?)\s*$', ln)
        if m and not hit: labels.setdefault(int(m[1]), m[2].strip()); hit = True
        if not hit and ln.startswith(('USAGE', 'OR:')): on = False
    return labels

def param_help(text):
    """'NAME:  text' blocks (continued on the following indented lines)"""
    h, cur = {}, None
    for ln in text.split('\n'):
        m = re.match(r'^([A-Za-z][A-Za-z0-9_\-]*(?:[,/][A-Za-z0-9_\-]+)*)\s*:\s*(.*)$', ln)
        if m and not ln.startswith('USAGE') and not ln.startswith('OR'):
            cur = m[1].lower(); h[cur] = m[2].strip(); continue
        if cur and ln.strip() and re.match(r'^\s{4,}', ln): h[cur] += ' ' + ln.strip()
        elif not ln.strip(): cur = None
        m2 = re.match(r'^(-[A-Za-z]|[A-Za-z][A-Za-z0-9_\-]*)\s{2,}(\S.*)$', ln)
        if m2 and not m and not re.match(r'^(' + PROGRE + r')$', m2[1]):
            h.setdefault(m2[1].lower(), m2[2].strip())
    return h

def parse_tokens(rest, modeword):
    toks = rest.split()
    mode = ''
    if toks and (re.match(r'^\d+(-\d+)?$', toks[0]) or toks[0] == 'mode'):
        mode = toks.pop(0)
    params, nin, outkind, data_in = [], 0, 'none', []
    multi, outext = False, ''
    seen_out = False   # words before the output file go before it on the command line (combine make pitchfile formantfile outfile)
    skip = False
    for t in toks:
        if re.fullmatch(r'\[?(infile\d+|infl\d+)|\.+\]?|etc\.?', t) and t not in ('infile1', 'infile2', 'infl'):
            multi = True; continue
        if t in ('....]', '...]', 'etc.'): multi = True; continue
        if IN_TOK.match(t): nin += 1; continue
        if t in ('outfile', 'outfl'): outkind = 'sound'; seen_out = True; continue
        if t == 'outtextfile': outkind = 'text'; seen_out = True; continue
        if t == 'outpanfile': outkind = 'text'; seen_out = True; continue
        if t in DATA_OUT: params.append(dict({'name': t, 'kind': 'dataout', 'optional': False, 'flag': '', 'ext': DATA_OUT[t], 'main': False}, pre=not seen_out)); continue
        opt = t.startswith('[') and t.endswith(']')
        core = t.strip('[]')
        if '|' in core and core.startswith('-'):   # -fN|-pN
            params.append(dict({'name': core, 'kind': 'either', 'optional': opt, 'flag': core}, pre=not seen_out)); continue
        if core.startswith('-'):
            m = re.match(r'^-([A-Za-z])(.*)$', core)
            if m and m[2]: params.append(dict({'name': m[2], 'kind': 'flagvalue', 'optional': True, 'flag': '-' + m[1]}, pre=not seen_out))
            else: params.append({'name': core, 'kind': 'switch', 'optional': True, 'flag': core})
            continue
        if re.search(r'file$', core) and core not in ('infile', 'outfile'):
            params.append(dict({'name': core, 'kind': 'datafile', 'optional': opt, 'flag': ''}, pre=not seen_out)); continue
        params.append(dict({'name': core, 'kind': 'value', 'optional': opt, 'flag': ''}, pre=not seen_out))
    for q in params: q.setdefault('pre', False)
    # a data file written beside (or instead of) the sound: the main output when there is no outfile, else an extra output word
    for q in params:
        if q['kind'] == 'dataout' and outkind == 'none' and not outext:
            outkind, outext, q['main'] = 'data', q['ext'], True
    return mode, nin, outkind, params, multi, outext

def defaults_and_range(desc):
    d = r = ''
    m = re.search(r'[Dd]efault[:\s]*\(?\s*(-?\d+(?:\.\d+)?)', desc)
    if m: d = m[1]
    m = re.search(r'[Rr]ange[:\s]*\(?\s*(-?\d+(?:\.\d+)?)\s*(?:-|to|:)\s*(-?\d+(?:\.\d+)?)', desc)
    if m: r = m[1] + ' to ' + m[2]
    return d, r

def main():
    cdpdir, out = sys.argv[1], sys.argv[2]
    procs = []
    for prog in PROGS:
        exe = os.path.join(cdpdir, prog)
        top = run(exe)
        for name in names_of(top):
            if prog in ONLY and name not in ONLY[prog]: continue
            text = run(exe, name)
            hlp = param_help(text)
            labels = mode_labels(text)
            forms = []
            for rest in usage_lines(prog, name, text):
                mode, nin, outkind, params, multi, outext = parse_tokens(rest, 'mode')
                if (prog, name) in OUT_OVERRIDE and outkind == 'sound': outkind, outext = 'data', OUT_OVERRIDE[(prog, name)]
                for p in params:
                    key = p['name'].lower().strip('-')
                    desc = hlp.get(p['flag'].lower(), '') if p['kind'] == 'flagvalue' else ''
                    desc = desc or hlp.get(key, '')
                    if not desc:
                        for k, v in hlp.items():
                            if key in re.split(r'[,/]', k) or k.startswith(key) or key.startswith(k) and len(k) > 2: desc = v; break
                    p['help'] = desc
                    p['default'], p['range'] = defaults_and_range(desc)
                nums = expand(mode)
                if mode == 'mode':
                    nums = sorted(labels) if labels else [1]
                forms.append({'usage': rest, 'mode': mode, 'modes': nums, 'nin': nin, 'out': outkind, 'outext': outext, 'multi': multi, 'params': params})
            procs.append({'prog': prog, 'name': name, 'title': title_of(text) or title_of(top), 'progTitle': (lambda t: t if t and not t.lower().startswith('where') and prog not in PROGTITLE else PROGTITLE.get(prog, prog.upper()))(title_of(top)),
                          'usage': text.strip('\n'), 'labels': {str(k): v for k, v in labels.items()}, 'forms': forms})
    # C++ include: one raw-string JSON; the app parses it at start (tiny)
    js = json.dumps({'cdp': 'CDP8 28bc42c7 (Release 7.1 programs)', 'processes': procs}, indent=0, ensure_ascii=True)
    with open(out, 'w') as f:
        f.write('// GENERATED by cdp/gen-hood-catalog.py from the usage text the real CDP programs print. Do not edit.\n')
        # raw string literals have a length limit on MSVC only; split in chunks to be safe everywhere
        f.write('static const char* const kHoodCatalogChunks[] = {\n')
        step = 8000
        for i in range(0, len(js), step):
            f.write('R"HOODJSON(' + js[i:i + step] + ')HOODJSON",\n')
        f.write('nullptr };\n')
    n = sum(len(p['forms']) for p in procs)
    print(len(procs), 'processes,', n, 'usage forms,', sum(len(f['params']) for p in procs for f in p['forms']), 'parameters ->', out)

if __name__ == '__main__': main()
