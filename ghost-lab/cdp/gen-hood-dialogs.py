#!/usr/bin/env python3
"""Builds ghost-lab/Source/Core/HoodDialogs.inc: the ORIGINAL parameter pages of every CDP process Ghost Lab ships, exactly as
CDP's own front end (the Sound Loom) gets them from CDP's own `cdparams` program (dev/cdparams in the CDP8 source; it prints,
for a process and mode, every parameter with its display type, name, type, range, default and slider sub-range, plus the
check-button flags). Nothing about CDP is typed in by hand here: the process numbers come from CDP's processno.h and the
programs' own get_process_no tables, the mode counts from CDP's get_maxmode, the pages from cdparams.

   gen-hood-dialogs.py <dir-with-the-cdp-programs (incl. cdparams)> <cdp8-src dir> <HoodCatalog.inc> <out.inc>

cdparams is asked with a REFERENCE sound (what the Sound Loom would send for a 10 s mono 44100 Hz sound, analysed with
pvoc's defaults, 1024 points / overlap 3). Ranges that depend on the sound (its length, its sample rate, the window count)
are recomputed by Ghost Lab at run time by asking cdparams again with the real sound (hood::liveDialog)."""
import json, os, re, subprocess, sys

PROGS = ["pvoc", "stretch", "strange", "focus", "blur", "modify", "combine", "morph", "repitch", "formants", "envel"]
SRCDIR = {"pvoc": "pv", "envel": "env"}
# the Sound Loom sends these about the input file; reference: 10 s, mono, 44100 Hz; analysis 1024 points / overlap 3
SR, DUR, CHANS = 44100, 10.0, 1
POINTS, OVERLAP = 1024, 3
SNDFILE, ANALFILE, PITCHFILE, FORMANTFILE, ENVFILE = 1, 24, 25, 27, 16

def arg(x):
    return f"{x:.9f}" if isinstance(x, float) else str(x)   # the same spelling Ghost Lab uses at run time (hood::cdparamsArgs)

def run(exe, *a):
    r = subprocess.run([exe, *[arg(x) for x in a]], capture_output=True, text=True, timeout=20)
    return (r.stdout + r.stderr).replace('\r', '')

def process_numbers(src):
    """processno.h constants + each program's own name -> constant table (get_process_no in dev/<prog>/ap_<prog>.c)"""
    consts = {}
    for ln in open(os.path.join(src, 'dev/include/processno.h')):
        m = re.match(r'^#define\s+([A-Z_0-9]+)\s+\((\d+)\)', ln)
        if m: consts[m[1]] = int(m[2])
    out = {}
    for prog in PROGS:
        d = os.path.join(src, 'dev', SRCDIR.get(prog, prog))
        text = ''.join(open(os.path.join(d, f)).read() for f in os.listdir(d) if f.startswith('ap_') and f.endswith('.c'))
        body = re.search(r'int get_process_no\(.*?\n\{(.*?)\n\}', text, re.S)[1]
        for m in re.finditer(r'"([a-z0-9_]+)"\)\)\s*dz->process\s*=\s*([A-Z_0-9]+)', body):
            out[(prog, m[1])] = (m[2], consts[m[2]])
    return out

def vflags(src):
    """CDP's own option / flag table (set_legal_option_and_variant_structure in dev/cdp2k/parstruct.c):
       (process const, 0-based mode or None) -> (option letters, option types, flag letters, how many flags carry a value)"""
    text = open(os.path.join(src, 'dev/cdp2k/parstruct.c')).read()
    body = text[text.index('int set_legal_option_and_variant_structure'):]
    body = body[:body.index('\n}\n')]
    modeno = {}
    for ln in open(os.path.join(src, 'dev/include/modeno.h')):
        m = re.match(r'^#define\s+([A-Z_0-9]+)\s+\((\d+)\)', ln)
        if m: modeno[m[1]] = int(m[2])
    out, cur, inswitch = {}, None, False
    call = re.compile(r'set_vflgs\(ap,\s*"([a-z]*)"\s*,\s*(\d+)\s*,\s*"([A-Za-z0]*)"\s*,\s*"([a-z]*)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*"([A-Za-z0]*)"\s*\)')
    for ln in body.split('\n'):
        s = ln.strip()
        if s.startswith('//'): continue
        m = re.match(r'case\(([A-Z_0-9]+)\):(.*)$', s)
        if m:
            key = m[1]
            if key in modeno and cur is not None and inswitch: mode = modeno[key]
            else: cur, mode, inswitch = key, None, False
            rest = m[2]
            c = call.search(rest)
            if c: out[(cur, mode)] = (c[1], c[3], c[4], int(c[6]))
            continue
        if 'switch(mode)' in s: inswitch = True; continue
        if s.startswith('default:'):
            c = call.search(s)
            if c and cur is not None and inswitch: out[(cur, 'default')] = (c[1], c[3], c[4], int(c[6]))
            mode = 'default'
            continue
        c = call.search(s)
        if c and cur is not None: out[(cur, mode)] = (c[1], c[3], c[4], int(c[6]))
    return out

def maxmodes(src):
    text = open(os.path.join(src, 'dev/cdp2k/tklib1.c')).read()
    body = re.search(r'int get_maxmode\(int process\)\s*\{(.*?)\n\}', text, re.S)[1]
    return {m[1]: int(m[2]) for m in re.finditer(r'case\(([A-Z_0-9]+)\):\s*maxmode\s*=\s*(\d+)', body)}

def file_args(filetype, infilecnt=1):
    """what the Sound Loom sends cdparams about the input: filetype infilesize insams srate channels wanted wlength linecnt arate frametime nyquist duration"""
    insams = int(SR * DUR)
    if filetype == SNDFILE:
        return [infilecnt, filetype, insams * 2, insams, SR, CHANS, 0, 0, 0, 0.0, 0.0, SR / 2.0, DUR]
    if filetype in (ANALFILE, PITCHFILE, FORMANTFILE, ENVFILE):
        wanted = POINTS + 2
        decim = POINTS // OVERLAP
        frametime = decim / SR
        wlength = insams // decim
        arate = SR / decim
        if filetype == ANALFILE: chans, size = wanted, wlength * wanted
        elif filetype == PITCHFILE: chans, size = 1, wlength
        elif filetype == FORMANTFILE: chans, size = wanted, wlength * wanted
        else: chans, size = 1, wlength
        return [infilecnt, filetype, size * 4, size, SR, chans, wanted, wlength, 0, arate, frametime, SR / 2.0, DUR]
    raise ValueError(filetype)

def input_type(prog, name):
    if prog in ('modify', 'envel'): return SNDFILE
    if prog == 'pvoc': return SNDFILE if name == 'anal' else ANALFILE
    if (prog, name) in (('combine', 'make'), ('combine', 'make2')): return PITCHFILE
    return ANALFILE

def input_count(prog, name, catalog_proc):
    n = max((f['nin'] for f in catalog_proc['forms']), default=1)
    if (prog, name) == ('combine', 'make'): return 2
    if (prog, name) == ('combine', 'make2'): return 3
    return max(1, n)

def parse_dialog(text):
    """cdparams' lines -> items. First line: can a breakpoint file be given a specific length (0/1). Then one line per control.
       Quoted names may hold spaces; the rest are numbers / a type letter."""
    items, brklen, error = [], None, ''
    for ln in text.split('\n'):
        s = ln.strip()
        if not s: continue
        if s.startswith('ERROR'): error = s; break
        if s == 'ENDPARAMS': break
        if brklen is None and re.fullmatch(r'-?\d+', s): brklen = int(s); continue
        toks = re.findall(r'"[^"]*"|\S+', s)
        kind = toks[0]
        names = [t.strip('"') for t in toks[1:] if t.startswith('"')]
        rest = [t for t in toks[1:] if not t.startswith('"')]
        it = {'kind': kind, 'name': names[0] if names else ''}
        def nums(a): return [float(x) for x in a]
        if kind in ('LINEAR', 'LOG', 'POWTWO', 'PLOG'):
            it['type'] = rest[0]; it['lo'], it['hi'], it['default'], it['sublo'], it['subhi'] = nums(rest[1:6])
        elif kind == 'SWITCHED':
            it['name2'], it['name3'] = (names + ['', ''])[1:3]
            it['type'] = rest[0]; it['lo'], it['hi'], it['default'], it['lo2'], it['hi2'], it['default2'] = nums(rest[1:7])
        elif kind == 'FILENAME':
            it['ranges'] = int(rest[0]); it['lo'], it['hi'], it['lo2'], it['hi2'] = nums(rest[1:5])
        elif kind in ('FILE_OR_VAL', 'NUMERIC', 'LOGNUMERIC'):
            it['type'] = rest[0]; it['lo'], it['hi'], it['default'] = nums(rest[1:4])
        elif kind in ('CHECKBUTTON', 'VOWELS', 'GENERICNAME', 'STRING_A', 'STRING_B', 'STRING_C', 'STRING_D'):
            pass
        else:
            it['raw'] = s
        items.append(it)
    return brklen, items, error

NUMERIC_KINDS = ('LINEAR', 'LOG', 'POWTWO', 'PLOG', 'SWITCHED', 'NUMERIC', 'LOGNUMERIC', 'FILE_OR_VAL')
SPECIAL_KINDS = ('FILENAME', 'VOWELS', 'GENERICNAME', 'STRING_A', 'STRING_B', 'STRING_C', 'STRING_D')

def map_to_form(items, form, flags):
    """Which usage-line word (the catalog's parameter, by name) each dialog item drives. CDP's command line and cdparams list
       the same things in the same order: special data first, then the parameters, then the option flags with a value (-xVAL,
       in order), then the bare flags (-x) as check buttons. Returns the mapping and a list of mismatches (for the report)."""
    pos = [p for p in form['params'] if p['kind'] in ('value', 'datafile', 'either')]   # (a 'dataout' word is an output file the engine names)
    opts = [p for p in form['params'] if p['kind'] == 'flagvalue']
    sws = [p for p in form['params'] if p['kind'] == 'switch']
    values = [i for i in items if i['kind'] in NUMERIC_KINDS or i['kind'] in SPECIAL_KINDS]
    checks = [i for i in items if i['kind'] == 'CHECKBUTTON']
    problems = []
    # the positional ones first, then the options; the value items are in that same order
    if not values and pos and all(p['kind'] == 'datafile' for p in pos):
        pass   # combine make / make2: the "parameters" are CDP's input files (pitch, formant, envelope data), no dialog values
    elif len(values) != len(pos) + len(opts):
        problems.append(f"{len(values)} dialog values vs {len(pos)} positional + {len(opts)} option words in the usage line")
    for k, it in enumerate(values):
        tgt = pos[k] if k < len(pos) else (opts[k - len(pos)] if k - len(pos) < len(opts) else None)
        it['param'] = tgt['name'] if tgt else ''
        it['flag'] = tgt['flag'] if tgt and tgt['kind'] == 'flagvalue' else ''
        it['optional'] = bool(tgt and (tgt['optional'] or tgt['kind'] == 'flagvalue'))
    # the option letters: CDP's own table (set_vflgs) is the source: the k-th option is flagged with its k-th letter, and
    # CDP's parser stores a -x value under the parameter at that position (readdata.c get_option_no). The usage line is
    # checked against it and the catalog word with that letter is the key the value is stored under.
    optletters, opttypes, varletters, nvarparams = flags if flags else ('', '', '', 0)
    letters = ['-' + c for c in optletters] + ['-' + c for c in varletters[:nvarparams]]
    byflag = {q['flag']: q for q in opts}
    for k, it in enumerate(values[len(pos):]):
        if k < len(letters):
            if it.get('flag') and it['flag'] != letters[k]:
                problems.append(f"option {k + 1} ({it['name']}) is {letters[k]} in CDP's own table but {it['flag']} in the usage line: the table decides")
            it['flag'] = letters[k]
            it['param'] = byflag[letters[k]]['name'] if letters[k] in byflag else letters[k]
            it['optional'] = True
    bare = ['-' + c for c in varletters[nvarparams:]]
    # the formant quick-search button belongs to the -fN|-pN formant choice and is the -i switch
    quick = [i for i in checks if i['name'] == 'FORMANT_QUICKSEARCH']
    checks = [i for i in checks if i['name'] != 'FORMANT_QUICKSEARCH']
    for it in quick: it['flag'] = it['param'] = '-i'
    if len(checks) != len(bare):
        problems.append(f"{len(checks)} check buttons vs {len(bare)} bare flags in CDP's own table")
    for k, it in enumerate(checks):
        it['flag'] = bare[k] if k < len(bare) else (sws[k]['flag'] if k < len(sws) else '')
        it['param'] = it['flag']   # a switch is stored under its flag word, as the usage catalog does
        if it['flag'] and it['flag'] not in [q['flag'] for q in sws]:
            it['extra'] = True     # CDP accepts it (its own table) but its usage text does not list it: still on the page
    return problems

def main():
    bindir, src, catinc, out = sys.argv[1:5]
    cat = json.loads(''.join(re.findall(r'R"HOODJSON\((.*?)\)HOODJSON"', open(catinc).read(), re.S)))
    procs = {(p['prog'], p['name']): p for p in cat['processes']}
    pnums = process_numbers(src)
    mm = maxmodes(src)
    vf = vflags(src)
    cdparams = os.path.join(bindir, 'cdparams')
    dialogs, report = [], []
    for (prog, name), cp in procs.items():
        const, pno = pnums[(prog, name)]
        maxmode = mm.get(const, 0)
        cat_modes = sorted({m for f in cp['forms'] for m in f['modes']})
        modes = list(range(1, maxmode + 1)) if maxmode > 0 else [0]
        if maxmode > 0 and cat_modes and cat_modes != modes:
            report.append(f"{prog} {name}: usage text lists modes {cat_modes}, CDP's get_maxmode says {maxmode}")
        ftype = input_type(prog, name)
        nin = input_count(prog, name, cp)
        for mode in modes:
            text = run(cdparams, pno, mode, *file_args(ftype, nin))
            brklen, items, error = parse_dialog(text)
            # the usage form this mode runs with (for the command words)
            form = None
            for f in cp['forms']:
                if (not f['mode'] and mode == 0) or (mode in f['modes']): form = f; break
            if form is None and mode > 0:
                for f in cp['forms']:
                    if not f['mode']: form = f; break
            if error and maxmode > 0:
                continue   # a mode number CDP itself has no process for (modify loudness 9 and 10): not a page
            flags = vf.get((const, mode - 1 if maxmode > 0 else None)) or vf.get((const, 'default')) or vf.get((const, None))
            problems = map_to_form(items, form, flags) if form is not None else ['no usage form for this mode']
            if error: problems.append(error)
            notes = [pr for pr in problems if pr.startswith('option ')]      # CDP's table and its usage text disagree: the table decides
            problems = [pr for pr in problems if not pr.startswith('option ')]
            for pr in problems + notes: report.append(f"{prog} {name} mode {mode}: {pr}")
            dialogs.append({'prog': prog, 'name': name, 'const': const, 'procno': pno, 'mode': mode, 'maxmode': maxmode,
                            'brklen': brklen if brklen is not None else 0, 'error': error, 'items': items,
                            'problems': problems, 'notes': notes, 'raw': text.strip('\n')})
    js = json.dumps({'cdparams': 'CDP8 28bc42c7 dev/cdparams', 'reference': {'srate': SR, 'duration': DUR, 'channels': CHANS, 'points': POINTS, 'overlap': OVERLAP},
                     'dialogs': dialogs}, indent=0, ensure_ascii=True)
    with open(out, 'w') as f:
        f.write('// GENERATED by cdp/gen-hood-dialogs.py from what CDP\'s own cdparams program (the Sound Loom\'s dialog source) prints. Do not edit.\n')
        f.write('static const char* const kHoodDialogChunks[] = {\n')
        step = 8000
        for i in range(0, len(js), step):
            f.write('R"HOODJSON(' + js[i:i + step] + ')HOODJSON",\n')
        f.write('nullptr };\n')
    nitems = sum(len(d['items']) for d in dialogs)
    print(len(dialogs), 'dialogs (process x mode),', nitems, 'controls ->', out)
    if report:
        print('CHECK (' + str(len(report)) + '):')
        for r in report: print('  ' + r)

if __name__ == '__main__': main()
