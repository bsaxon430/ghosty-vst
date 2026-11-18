#!/usr/bin/env python3
"""Writes docs/CONTROLS.md from Source/Core/HoodCatalog.inc (the programs' usage text) and Source/Core/HoodDialogs.inc
(the ORIGINAL pages: what CDP's own cdparams prints for every process and mode).
   gen-hood-controls.py HoodCatalog.inc HoodDialogs.inc CONTROLS.md"""
import re, json, sys
cat, dia, out = sys.argv[1], sys.argv[2], sys.argv[3]
def load(p): return json.loads(''.join(re.findall(r'R"HOODJSON\((.*?)\)HOODJSON"', open(p).read(), re.S)))
C = load(cat); D = load(dia)
P = C['processes']; DL = D['dialogs']
nforms = sum(len(p['forms']) for p in P); nparams = sum(len(f['params']) for p in P for f in p['forms'])
nmodes = sum(len({m for f in p['forms'] for m in f['modes']}) for p in P)
ncontrols = sum(len(d['items']) for d in DL)
nvary = sum(1 for d in DL for i in d['items'] if i.get('type') in ('D', 'I') or i['kind'] == 'FILE_OR_VAL')
nchecks = sum(1 for d in DL for i in d['items'] if i['kind'] == 'CHECKBUTTON')
KIND = {'LINEAR': 'slider', 'LOG': 'slider (log)', 'PLOG': 'slider (pitch-log)', 'POWTWO': 'slider (powers of 2)', 'NUMERIC': 'number box',
        'LOGNUMERIC': 'number box (log)', 'SWITCHED': 'two-way choice + slider', 'FILE_OR_VAL': 'number or data file', 'FILENAME': 'data file',
        'CHECKBUTTON': 'check button', 'STRING_A': 'text (shuffle string)', 'STRING_B': 'text', 'STRING_C': 'text', 'STRING_D': 'text', 'VOWELS': 'text (vowels)', 'GENERICNAME': 'name'}
def num(v):
    if v is None: return ''
    if abs(v - round(v)) < 1e-9 and abs(v) < 1e15: return str(int(round(v)))
    return ('%.6f' % v).rstrip('0').rstrip('.')
L = []
L.append("# Ghost Lab UNDER THE HOOD: every CDP control, and where it is now\n")
L.append("Ghost Lab runs the real CDP8 programs (`pvoc stretch strange focus blur modify combine morph`, plus `repitch formants envel` for the data "
         "files combine make / make2 need, all unmodified, pinned at commit 28bc42c7, see `cdp/CDP-PINNED.txt`), plus CDP's own `cdparams` program: the one CDP's own front end (the Sound Loom) asks for the "
         "parameter page of a process. UNDER THE HOOD lists every process of those programs, grouped the way CDP groups them (by program), and shows "
         "each one's **ORIGINAL page**: every control exactly as cdparams defines it for that process and mode (CDP's own control name, its kind: "
         "slider / number box / check button / data file, its range, its default, the slider's starting sub-range, whether it may be time-varying), "
         "in our colours, with our plain words printed small under CDP's name. What you set goes to the program exactly as it is: no scaling, no "
         "clamping, no hidden processing. RUN shows the command line(s) that ran and CDP's own words. The result is the processed sound (CDP's output "
         "as it is: no gain, no fades), and UNDO brings back the sound before it.\n")
L.append("Nothing here is typed in by hand: `cdp/gen-hood-catalog.py` keeps each program's usage text (verbatim; INFORMATION on the page shows it) and "
         "reads the command words from its USAGE lines; `cdp/gen-hood-dialogs.py` asks cdparams for every process and mode (process numbers from CDP's "
         "`processno.h` and the programs' own name tables, mode counts from CDP's `get_maxmode`, option letters from CDP's own `set_vflgs` table) and "
         "keeps what it prints. The tests check that every page is byte for byte what the shipped cdparams prints, and that runs from the page are "
         "bit-exact with the same CDP commands typed by hand.\n")
L.append("## Coverage\n")
L.append(f"- **{len(P)} processes**: every process of the 8 sound programs, plus the 4 data-file processes of `repitch` (getpitch, analenv), `formants` (get) and `envel` (extract); "
         f"**{nmodes} modes**, **{len(DL)} ORIGINAL pages** (one per process and mode)")
L.append(f"- **{ncontrols} controls** on those pages ({nvary} may be time-varying breakpoint files, {nchecks} check buttons); **{nforms} usage forms**, **{nparams} command words**")
L.append("- `modify loudness`: CDP's table has 12 modes but no process for 9 and 10 (its usage text lists 1-8, 11, 12), so those two are not pages.")
L.append("- Ghost Lab FX (the mixer-insert version) has the same UNDER THE HOOD page: every process renders on the captured track sound; none is left out.\n")
L.append("## How a page works\n")
L.append("- **Mode**: the list at the top, CDP's own mode words (from its usage text). The page below changes with it.")
L.append("- **Slider controls** (LINEAR / LOG / PLOG / POWTWO) open on CDP's starting sub-range; **FULL RANGE** opens them to everything CDP accepts; "
         "a typed value outside the starting range opens it too. The number box and the slider are the same value.")
L.append("- **Number boxes** (NUMERIC / LOGNUMERIC): CDP's front end shows these as entries, so the page does too, with the range beside them.")
L.append("- **DEFAULT** puts CDP's own default back for that control; **SET DEFAULTS** for the whole page. A page opens at CDP's defaults.")
L.append("- **BRK** (on controls whose CDP type is D or I): time-varying. The control becomes a text box for time value pairs (`0 1.5  2 0.7`) or the "
         "path of a breakpoint file; Ghost Lab writes the pairs to a file and gives CDP its name, as CDP expects.")
L.append("- **FORMANT_BANDS** (strange glis, focus focus, blur spread): CDP's PITCHWISE / FRQWISE choice with its own range for each, sent as `-pN` / `-fN`; "
         "FORMANT_QUICKSEARCH is CDP's `-i`.")
L.append("- **Optional flags with a value** (`-r`, `-l`...): the box shows CDP's default and **SEND** is off, so CDP behaves as without the flag; tick SEND to send the value.")
L.append("- **Check buttons** are CDP's bare flags (`-x`), sent when ticked. A few are in CDP's own table but not in its usage text (brassage `-d`); they are on the page and marked.")
L.append("- **Data files** (FILENAME / STRING): type the data or paste a path, drop a file on the field, or **PICK** (a file made in this session, or browse).")
L.append("- **MAKE FROM SOUND** (build 8) sits beside every pitch / formant / envelope file field (`combine make`, `combine make2`): it runs CDP's own extractor on the "
         "current sound or SPECIMEN 002 and fills the field. Pitch data = `pvoc anal` + `repitch getpitch 1` (its binary pitch file, .frq); formant data = "
         "`pvoc anal` + `formants get -p4` (.for); envelope data = `pvoc anal` + `repitch analenv` (.evl, the window-loudness envelope of the analysis, which is what make2 wants). "
         "The same analysis settings (POINTS / OVERLAP) are used throughout, as make needs. The files live in the session's data folder; the page shows what was made and the commands.")
L.append("- **The data-file processes' own pages** (`repitch getpitch`, `repitch analenv`, `formants get`, `envel extract`) run like any other: the sound stays (getpitch's "
         "detected-pitch tone is heard), the data file is kept in the session's data folder and listed under the command (`# DATA frq: ...`), so PICK can use it.")
L.append("- **Several sounds** (`modify sausage`, and `combine interleave` / `max`, `modify loudness 7-8`): a MORE SOUNDS box on the page: + CURRENT SOUND, + SPECIMEN 002, "
         "+ EARLIER RESULT (any step of the history), + FILE, or drop files on it. The current sound is always the first input; the others follow in the order listed. "
         "`modify loudness 7` reports which file is loudest; `8` writes one file per input, so only the first is heard.")
L.append("- **Ranges follow the sound**: the baked pages were asked for a 10 s mono 44100 Hz sound analysed with 1024 points / overlap 3 (CDP's defaults). "
         "With a sound loaded, Ghost Lab asks the shipped cdparams again with the real sound (length, rate, channels, your POINTS / OVERLAP), "
         "so limits like a start time's ceiling or a blur's window count are the real ones; the header says which.")
L.append("- **Spectral programs** (`blur combine focus morph strange stretch`) work on an analysis, so CDP's own `pvoc anal 1 in.wav in.ana -cPOINTS -oOVERLAP` runs first "
         "and `pvoc synth` after. Stereo sounds run once per channel. `modify` programs read the sound as it is. Two-sound processes use the specimen and SPECIMEN 002.")
L.append("- **Text answers** (`stretch time 2`, `modify speed 3/4`, `modify findpan`, `modify spaceform`) leave the sound as it is and show CDP's answer under the command.")
L.append("- **Engine call:** `Hood.cpp: renderHood` -> `Cdp::run`; the pages: `hood::dialogs()` / `hood::liveDialog` (cdparams) and `HoodPage.h`; the step is saved with the session (`Op::Hood`).\n")
L.append("### Where CDP's own table and its usage text disagree\n")
for d in DL:
    for n in d.get('notes', []): L.append(f"- `{d['prog']} {d['name']}` mode {d['mode']}: {n}")
L.append("")
L.append("## The ORIGINAL pages\n")
L.append("Columns: CDP's control (its own name, from cdparams), its kind, type (i / d fixed int / number; I / D may be a breakpoint file), range, default, "
         "the slider's starting sub-range, our plain words (the command word it drives, with the first words of CDP's own help), and the command argument.\n")
byproc = {}
for d in DL: byproc.setdefault((d['prog'], d['name']), []).append(d)
for prog in dict.fromkeys(p['prog'] for p in P):
    pt = next(p['progTitle'] for p in P if p['prog'] == prog)
    L.append(f"## {prog}  ({pt})\n")
    for p in [q for q in P if q['prog'] == prog]:
        L.append(f"### {p['prog']} {p['name']}: {p['title']}\n")
        helps = {q['name']: q.get('help', '') for f in p['forms'] for q in f['params']}
        flags = {q['flag']: q['name'] for f in p['forms'] for q in f['params'] if q.get('flag')}
        for d in byproc.get((p['prog'], p['name']), []):
            label = ''
            if d['mode'] > 0:
                lab = C and next((pp for pp in P if pp['prog'] == d['prog'] and pp['name'] == d['name']), None)
                label = lab['labels'].get(str(d['mode']), '') if lab else ''
                L.append(f"**Mode {d['mode']}**{(': ' + label) if label else ''}  (`{p['prog']} {p['name']} {d['mode']} ...`)\n")
            else:
                L.append(f"**No modes**  (`{p['prog']} {p['name']} ...`)\n")
            if not d['items']:
                form = next((f for f in p['forms'] if (not f['mode'] and d['mode'] == 0) or d['mode'] in f['modes']), None)
                if form and form['params']:
                    L.append("Inputs CDP reads as files (no controls on the page): " + ', '.join(f"`{q['name']}`" for q in form['params']) + "\n")
                else:
                    L.append("No settings of its own: RUN runs it as it is.\n")
                continue
            L.append("| CDP control | Kind | Type | Range | Default | Starting range | Plain words (the command word) | Argument |\n|---|---|---|---|---|---|---|---|")
            for it in d['items']:
                kind = KIND.get(it['kind'], it['kind'])
                typ = it.get('type', '')
                if it['kind'] == 'SWITCHED':
                    rng = f"{it['name2']} {num(it['lo'])} to {num(it['hi'])}; {it['name3']} {num(it['lo2'])} to {num(it['hi2'])}"
                    dflt = f"{num(it['default'])} / {num(it['default2'])}"; sub = ''
                elif it['kind'] == 'FILENAME':
                    rng = (f"{num(it['lo'])} to {num(it['hi'])}" if it.get('ranges', 0) else '') + (f"; {num(it['lo2'])} to {num(it['hi2'])}" if it.get('ranges', 0) > 1 else '')
                    dflt = ''; sub = ''
                elif it['kind'] == 'CHECKBUTTON' or it['kind'].startswith('STRING') or it['kind'] in ('VOWELS', 'GENERICNAME'):
                    rng = dflt = sub = ''
                else:
                    rng = f"{num(it['lo'])} to {num(it['hi'])}"; dflt = num(it.get('default'))
                    sub = f"{num(it['sublo'])} to {num(it['subhi'])}" if 'sublo' in it and (it['sublo'] != it['lo'] or it['subhi'] != it['hi']) else ''
                word = it.get('param', '')
                help_ = helps.get(word, '') or helps.get(flags.get(it.get('flag', ''), ''), '')
                help_ = re.sub(r'\s+', ' ', help_).strip()
                if len(help_) > 70: help_ = help_[:68].rstrip() + '...'
                plain = f"`{word}`" + (f"  {help_}" if help_ else '')
                if it['kind'] == 'CHECKBUTTON': arg = f"`{it['flag']}` when ticked" + (' (in CDP\'s table, not its usage text)' if it.get('extra') else '')
                elif it['kind'] == 'SWITCHED': arg = '`-pN` or `-fN`'
                elif it.get('flag'): arg = f"`{it['flag']}VALUE` when SEND is ticked"
                else: arg = 'as set' + (' (a file name when BRK)' if typ in ('D', 'I') or it['kind'] == 'FILE_OR_VAL' else '')
                name = it['name'].replace('|', '\\|')
                L.append(f"| {name} | {kind} | {typ} | {rng} | {dflt} | {sub} | {plain} | {arg} |")
            L.append("")
open(out, 'w').write('\n'.join(L) + '\n')
print(len(L), 'lines')
