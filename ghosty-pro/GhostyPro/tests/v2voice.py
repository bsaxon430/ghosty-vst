#!/usr/bin/env python3
"""SOUND ENGINE V2 re-voicing rules -> ProV2Tune.inc (spec column; trims come from `ProTests v2audit --fit`).
usage: v2voice.py specs.txt out.inc [old.inc]   (old.inc: keep trims / CLEAN of an earlier fitted table)
Each preset keeps its identity (tables, envelopes, filter, FX scene); the rules only add what the new engine has.
Loop: ProTests speclist > specs.txt; v2voice.py specs.txt ../ProV2Tune.inc [fitted.inc]; build; ProTests v2audit --fit fitted.inc
(8 shards, cat them); v2voice.py again with the fitted table; v2audit (no --fit) is the gate. Per-sound fixes: v2manual.txt.
Also writes <out>_report.txt (what each sound got) and ProV2Legacy.auto.txt (the Ghosty Classics -> ProV2Legacy.inc)."""
import re, sys

specs, out = sys.argv[1], sys.argv[2]
old = {}
if len(sys.argv) > 3:
    for line in open(sys.argv[3]):
        m = re.match(r'\{ "(.*?)", (-?[\d.]+)f, (-?\d), "(.*)" \},', line.strip())
        if m: old[m.group(1)] = (float(m.group(2)), int(m.group(3)), m.group(4))
manual = {}   # name -> (extra tokens to add, tokens to drop by key) from the audit loop (v2manual.txt: name|add tokens|drop keys)
try:
    for line in open(sys.argv[0].replace('v2voice.py', 'v2manual.txt')):
        if line.strip() and not line.startswith('#'):
            n, add, drop = (line.rstrip('\n').split('|') + ['', ''])[:3]
            manual[n.strip()] = (add.strip(), drop.split())
except FileNotFoundError:
    pass

def parse(spec):
    v, mods = {}, []
    for t in spec.split():
        if '=' not in t: continue
        k, x = t.split('=', 1)
        if k == 'mod': mods.append(x)
        else: v[k] = x
    return v, mods

def num(v, k, d=0.0):
    try: return float(v.get(k, d))
    except ValueError: return d

def has(name, *words): return any(w.lower() in name.lower() for w in words)

lines, legacy, report = [], [], []
manual_clean = {}
for row in open(specs):
    p = row.rstrip('\n').split('|')
    if len(p) < 5: continue
    idx, cat, name, scene, spec = int(p[0]), p[1], p[2], p[3], p[4]
    v, mods = parse(spec)
    if cat == 'V2 Showcase': v = {'_showcase': '1'}; spec = ''   # written for V2 already: only its fitted trim
    add = {}                                                # key -> value (ordered)
    why = []
    showcase = '_showcase' in v
    o1tbl, o2on = v.get('o1_tbl', 'Saw'), v.get('o2_on', '0') in ('1', 'on', 'true')
    uni1 = int(num(v, 'o1_uni', 1))
    verb = num(v, 'verb')
    is_pad = cat in ('Pads', 'Trap Pads & Choirs') or has(name, 'Pad', 'Drone', 'Strings', 'Ensemble', 'Choir', 'Swell', 'Bloom', 'Horizon', 'Fog') and cat not in ('Bass', 'Trap Bass')
    is_bass = cat in ('Bass', 'Trap Bass') or (cat == 'Ghosty Classics' and has(name, 'Bass', '808'))
    is_lead = cat.startswith('Leads') or cat in ('Trap Leads & Flutes',) or (cat == 'Supersaws' and has(name, 'Lead', 'Unison', 'Hoover'))
    is_pluck = cat in ('Plucks', 'Bells & Mallets', 'Keys', 'Arps & Sequences', 'Trap Keys & Bells') or has(name, 'Pluck', 'Stab', 'Keys', 'Bell', 'Arp')
    is_fx = cat in ('FX & Atmos',) or has(name, 'Riser', 'Sweep', 'Wind', 'Static', 'Downlifter', 'Zap')
    sub_like = has(name, 'Sub', '808', 'Pure', 'Sine', 'Boom', 'Knock', 'Click', 'Flat', 'Drop', 'Tuba')

    if showcase:
        tr, cl, _ = old.get(name, (0.0, -1, ''))
        cl = manual_clean.get(name, 1)                   # a new sound loads with CLEAN on (as INIT does)
        lines.append(f'{{ "{name}", {tr:.2f}f, {cl}, "" }},')
        report.append(f'{idx}|{cat}|{name}|showcase'); continue
    # 1. GHOST VERB voicing (the law already switches the type): pre-delay, low cut, decay, size, high cut per kind of sound
    if verb > 0.001:
        if is_bass:   gv = dict(verb_pre=10, verb_lo=420, verb_decay=1.1, verb_size=.4, verb_hi=7000)
        elif is_fx:   gv = dict(verb_pre=40, verb_lo=200, verb_decay=4.5, verb_size=.9, verb_hi=9000)
        elif is_pad:  gv = dict(verb_pre=28, verb_lo=240, verb_decay=3.4, verb_size=.8, verb_hi=8500)
        elif is_lead or cat in ('Supersaws', 'Trap Brass', 'Brass & Strings'): gv = dict(verb_pre=32, verb_lo=280, verb_decay=2.2, verb_size=.65, verb_hi=9000)
        else:         gv = dict(verb_pre=16, verb_lo=260, verb_decay=1.8, verb_size=.6, verb_hi=9500)
        if (is_lead or is_pluck) and verb >= .3 and not is_pad: gv['verb_duck'] = .3     # the tail steps back while the note plays
        for k, x in gv.items():
            if k not in v: add[k] = x
        why.append('ghostverb')

    # 2. UNISON V2 stacks on saws / supersaws (the unison law is V2 already; Super detune is the default curve)
    if o1tbl == 'Saw' and uni1 >= 5 and (cat == 'Supersaws' or cat.startswith('Leads') or has(name, 'Supersaw', 'Saw')) and not is_bass and 'o1_stack' not in v:
        o2oct = int(num(v, 'o2_oct', 0)) if o2on else 0
        if has(name, 'Pluck', 'Stab', 'Chord', 'Pad', 'Velvet') or o2oct == 1:
            if num(v, 'sub_lvl') <= 0 and not is_pad and not has(name, 'Pluck', 'Stab'): add['o1_stack'] = 'Center_-12'; why.append('stack C-12')
        else:
            add['o1_stack'] = '12'; why.append('stack 12')

    # 3. INSIDE drive (the law): basses and leads get a little filter drive to use it (pure subs / 808s stay clean)
    if is_bass and not sub_like and num(v, 'f_drive') < .12 and v.get('f_type', 'lp12') != 'off':
        add['f_drive'] = .18; why.append('inside drive')
    if is_lead and num(v, 'f_drive') < .08 and not has(name, 'Sine', 'Whistle', 'Theremin', 'Flute', 'Pipe', 'Ocarina', 'Pan') and v.get('f_type', 'lp12') != 'off':
        add['f_drive'] = .12; why.append('inside drive')

    # 4. HYPER on wide leads
    wide_saw = cat == 'Supersaws' and not has(name, 'Pluck', 'Stab', 'Chord', 'Pad')
    if (wide_saw or (is_lead and (uni1 >= 3 or has(name, 'Hoover', 'Big Room', 'Hands Up', 'Anthem', 'Unison')))) and not has(name, 'Pluck', 'Arp'):
        add.update({'fx_hyper_on': 1, 'fx_hyper_mix': .25, 'fx_hyper_voices': 3, 'fx_hyper_detune': .22, 'fx_hyper_width': .8})
        why.append('hyper')

    # 5. WARPS where the spec wanted sync / PWM-like motion
    pwm = any(m.startswith(('lfo1>pos1', 'lfo2>pos1')) for m in mods) and o1tbl == 'Pulse'
    if pwm and 'o1_w1' not in v:
        add.update({'o1_w1': 'Pulse_Width', 'o1_w1a': .25}); why.append('warp PW')
    if (v.get('xmod') == 'sync' or o1tbl == 'Sync') and 'o1_w1' not in v and 'o1_w1' not in add:
        add.update({'o1_w1': 'Sync', 'o1_w1a': .2}); why.append('warp sync')

    # 6. NOISE types for breath / air
    if num(v, 'nz_lvl') > 0 and 'nz_type' not in v:
        if has(name, 'Breath', 'Air', 'Flute', 'Whistle', 'Choir', 'Vocal', 'Ahh', 'Ooh', 'Pipe', 'Ghost Flute', 'Ocarina'): add['nz_type'] = 'Breath'; why.append('breath noise')
        elif is_fx or has(name, 'Wind', 'Sweep', 'Riser', 'Static', 'Rain', 'Fog'): add['nz_type'] = 'Pink'; why.append('pink noise')

    # 7. ENGINES where they obviously fit, as a second oscillator under the preset's own sound (only when OSC 2 is free)
    if not o2on:
        eng = None
        if has(name, 'Harp', 'Koto', 'Guitar', 'Nylon', 'Sitar', 'Banjo', 'Zither'): eng, lvl = 'STRING', .3
        elif has(name, 'Marimba', 'Kalimba', 'Vibraphone', 'Steel', 'Mallet', 'Chime', 'Gamelan', 'Celest', 'Music Box', 'Tubular'): eng, lvl = 'MODAL', .3
        elif has(name, 'Vocal', 'Choir', 'Ahh', 'Ooh', 'Vox', 'Talking', 'Yoi'): eng, lvl = 'GRAIN_FORMANT', .28
        if eng:
            add.update({'o2_on': 1, 'o2_tbl': eng, 'o2_lvl': lvl}); why.append('engine ' + eng.lower())

    # the audit loop's per-sound fixes
    if name in manual:
        a, drop = manual[name]
        for k in drop: add.pop(k, None)
        for t in a.split():
            k, x = t.split('=', 1)
            if k == 'CLEAN': manual_clean[name] = int(x)
            else: add[k] = x
        if a or drop: why.append('manual')
    toks = ' '.join(f'{k}={x}' for k, x in add.items())
    tr, cl, _ = old.get(name, (0.0, -1, ''))
    if name in manual_clean: cl = manual_clean[name]
    if cat == 'Ghosty Classics': legacy.append(name)
    if toks or abs(tr) >= .01 or cl >= 0:
        lines.append(f'{{ "{name}", {tr:.2f}f, {cl}, "{toks}" }},')
    report.append(f'{idx}|{cat}|{name}|{",".join(why)}')

open(out, 'w').write('// generated by v2voice.py (rules) + ProTests v2audit --fit (trims): the factory sounds re-voiced for SOUND ENGINE V2\n'
                     '// { name, trim dB added to the CLASSIC trim, CLEAN (-1 as CLASSIC / 0 / 1), extra spec (fx_ tokens are applied after the FX scene) }\n'
                     + '\n'.join(lines) + '\n')
open(out.replace('.inc', '_report.txt'), 'w').write('\n'.join(report) + '\n')
open(out.replace('Tune.inc', 'Legacy.auto.txt'), 'w').write('\n'.join(legacy) + '\n')
print(len(lines), 'tuned,', len(legacy), 'legacy')
