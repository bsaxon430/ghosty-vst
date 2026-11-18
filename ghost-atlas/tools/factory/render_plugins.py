#!/usr/bin/env python3
# GHOST ATLAS PRESET FACTORY - plays synthesiser plug-ins' own patches into multisampled SFZ instruments.
#
# A patch is hosted with DawDreamer (github.com/DBraun/DawDreamer), given one note at a time across the keyboard and at
# one or two velocities, and recorded dry. Held sounds get a loop, crossfaded where the waveform agrees with itself;
# struck sounds ring out and are cut 72 dB down. Each instrument gets an SFZ map, and the set a renders.json saying
# which patch each instrument came from.
#
#     render_plugins.py <spec.json> <out folder> [--only name,name]
#
# spec.json: {"synths": {"<synth>": {"plugin": path, "flush": seconds, "off": {param name: value, ...}}},
#             "patches": [{"name", "synth", "preset" (an .fxp / state file, or {"program": n}), "kind": lead|pad|pluck|
#                          keys|bell|bass|evolve, "category", "octave": 0, "set": {param name: value}}]}
import json, math, os, sys, time
import numpy as np
import soundfile as sf
import dawdreamer as dd

SR = 48000
KINDS = {   # notes from, to, every; velocities; how long the key is held; how long to listen after it is let go
    "lead":   dict(lo=36, hi=96, step=3, vels=(70, 120), hold=5.0, tail=2.0, held=True),
    "pad":    dict(lo=36, hi=96, step=4, vels=(100,),    hold=8.0, tail=5.0, held=True),
    "evolve": dict(lo=36, hi=96, step=4, vels=(100,),    hold=10.0, tail=5.0, held=True),
    "bass":   dict(lo=24, hi=72, step=3, vels=(70, 120), hold=4.0, tail=1.5, held=True),
    "pluck":  dict(lo=36, hi=96, step=3, vels=(70, 120), hold=2.5, tail=4.0, held=False),
    "bell":   dict(lo=36, hi=96, step=3, vels=(70, 120), hold=3.0, tail=6.0, held=False),
    "keys":   dict(lo=24, hi=96, step=3, vels=(60, 110), hold=4.0, tail=3.0, held=False),
}

def db(x): return 20.0 * math.log10(max(x, 1e-12))

def flush(eng, proc, most=8.0):
    """Lets every voice and every tail die away before the next note (the plug-in keeps its state between renders)."""
    proc.clear_midi()
    waited = 0.0
    while waited < most:
        eng.render(0.5); a = eng.get_audio(); waited += 0.5
        if np.abs(a).max() < 1e-5: return

def env_db(mono, hop=480):
    n = len(mono) // hop
    if n == 0: return np.array([-200.0])
    blk = mono[: n * hop].reshape(n, hop)
    return 20 * np.log10(np.sqrt((blk ** 2).mean(axis=1)) + 1e-12)

def best_loop(a, start, end, search, xf):
    """Moves the loop's end to where the sound most resembles what comes before its start, so the crossfade joins two
    near-identical stretches. Returns (start, end, match 0..1)."""
    mono = a.mean(axis=1)
    ref = mono[start - xf: start]
    best, best_end = -2.0, end
    for e in range(end - search, end + search, 4):
        seg = mono[e - xf: e]
        if len(seg) != len(ref): continue
        d = np.linalg.norm(ref) * np.linalg.norm(seg)
        c = float(np.dot(ref, seg) / d) if d > 0 else 0.0
        if c > best: best, best_end = c, e
    return start, best_end, best

def bake_loop(a, start, end, xf):
    """The last xf frames before the loop's end are blended into the frames before its start (equal power), so the
    jump from end back to start lands on what the end has become."""
    out = a.copy()
    t = np.linspace(0.0, 1.0, xf)[:, None]
    fade_in, fade_out = np.sin(t * math.pi / 2), np.cos(t * math.pi / 2)
    out[end - xf: end] = a[end - xf: end] * fade_out + a[start - xf: start] * fade_in
    return out

def dx7_voice(syx, voice):
    """One voice of a DX7 cartridge (32-voice bulk dump), unpacked from its 128 bytes into the 155 values a DX7 edits."""
    d = open(syx, "rb").read()
    if d[0] != 0xF0 or len(d) < 4104: raise RuntimeError(syx + " is not a 32-voice cartridge")
    v = d[6 + voice * 128: 6 + (voice + 1) * 128]
    u = [0] * 155
    for op in range(6):                      # stored OP6 first, as the DX7 does
        b, o = v[op * 17: op * 17 + 17], op * 21
        u[o:o + 8] = b[0:8]; u[o + 8], u[o + 9], u[o + 10] = b[8], b[9], b[10]
        u[o + 11], u[o + 12] = b[11] & 3, (b[11] >> 2) & 3
        u[o + 13], u[o + 20] = b[12] & 7, (b[12] >> 3) & 15
        u[o + 14], u[o + 15] = b[13] & 3, (b[13] >> 2) & 7
        u[o + 16] = b[14]; u[o + 17], u[o + 18] = b[15] & 1, (b[15] >> 1) & 31; u[o + 19] = b[16]
    u[126:134] = v[102:110]; u[134] = v[110] & 31; u[135], u[136] = v[111] & 7, (v[111] >> 3) & 1
    u[137:141] = v[112:116]; u[141], u[142], u[143] = v[116] & 1, (v[116] >> 1) & 7, (v[116] >> 4) & 7
    u[144] = v[117]
    return u, v[118:128].decode("ascii", "replace").strip()

def set_dx7_voice(proc, syx, voice):
    """Dexed is given the voice through its own parameters (a VST3 plug-in hears no system exclusive): each is the DX7
    value over that control's range, as Dexed's parameter layout defines it (source/parameter/DexedApvts.cpp)."""
    u, name = dx7_voice(syx, voice)
    by = {proc.get_parameter_name(i).strip().lower(): i for i in range(proc.get_plugin_parameter_size())}
    def put(label, norm):
        i = by.get(label.lower())
        if i is None: raise RuntimeError("Dexed has no parameter " + label)
        proc.set_parameter(i, float(min(1.0, max(0.0, norm))))
    for op in range(6):
        o = (5 - op) * 21; P = f"OP{op + 1} "
        for j in range(4):
            put(P + f"Eg Rate{j + 1}", u[o + j] / 99); put(P + f"Eg Level{j + 1}", u[o + 4 + j] / 99)
        put(P + "Breakpoint", u[o + 8] / 99); put(P + "L Scale Depth", u[o + 9] / 99); put(P + "R Scale Depth", u[o + 10] / 99)
        put(P + "L Key Scale", u[o + 11] / 3); put(P + "R Key Scale", u[o + 12] / 3); put(P + "Rate Scaling", u[o + 13] / 7)
        put(P + "A Mod Sens.", u[o + 14] / 3) if ("op%d a mod sens." % (op + 1)) in by else put(P + "Amp Mode Sens", u[o + 14] / 3)
        put(P + "Key Velocity", u[o + 15] / 7); put(P + "Output Level", u[o + 16] / 99); put(P + "Mode", u[o + 17])
        put(P + "Frequency Coarse", u[o + 18] / 31); put(P + "Frequency Fine", u[o + 19] / 99); put(P + "Detune", u[o + 20] / 14)
        put(P + "Switch", 1.0) if (P + "switch").lower() in by else put(f"Op{op + 1} switch", 1.0)
    for j in range(4):
        put(f"Pitch Eg Rate{j + 1}", u[126 + j] / 99); put(f"Pitch Eg Level{j + 1}", u[130 + j] / 99)
    put("Algorithm", u[134] / 31); put("Feedback", u[135] / 7); put("Osc Key Sync", u[136]); put("Lfo Rate", u[137] / 99)
    put("Lfo Delay", u[138] / 99); put("Lfo Pm Depth", u[139] / 99); put("Lfo Amp Depth", u[140] / 99); put("Lfo Key Sync", u[141])
    put("Lfo Waveform", u[142] / 5); put("Pitch Mod Sens", u[143] / 7); put("Transpose", u[144] / 48)
    return name

JUCE_B64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"      # JUCE's MemoryBlock::toBase64Encoding

def juce_b64(b):
    out = []
    pad = b + b"\0\0"
    for i in range(0, len(b), 3):                 # 24 bits at a time: four characters, least significant bits first
        v = pad[i] | (pad[i + 1] << 8) | (pad[i + 2] << 16)
        out.append(JUCE_B64[v & 63] + JUCE_B64[(v >> 6) & 63] + JUCE_B64[(v >> 12) & 63] + JUCE_B64[(v >> 18) & 63])
    return f"{len(b)}." + "".join(out)[: (len(b) * 8 + 5) // 6]

def juce_unb64(s):
    size, _, body = s.partition("."); size = int(size)
    idx = {c: i for i, c in enumerate(JUCE_B64)}
    body += "." * (-len(body) % 4)
    out = bytearray()
    for i in range(0, len(body), 4):
        v = idx[body[i]] | (idx[body[i + 1]] << 6) | (idx[body[i + 2]] << 12) | (idx[body[i + 3]] << 18)
        out += bytes((v & 255, (v >> 8) & 255, (v >> 16) & 255))
    return bytes(out[:size])

def set_plugin_xml_state(proc, xml):
    """Gives a JUCE plug-in hosted as VST3 a state made of its own XML (what its getStateInformation writes with
    copyXmlToBinary): the host's wrapper round it is taken from the plug-in's own saved state and the XML put in place
    of what was inside, keeping the wrapper's private data that follows."""
    import re, struct, tempfile
    tmp = tempfile.mktemp(suffix=".state")
    proc.save_state(tmp); outer = open(tmp, "rb").read()
    xml_outer = outer[8:].rstrip(b"\0").decode("utf-8")
    comp = re.search(r"<IComponent>([^<]*)</IComponent>", xml_outer).group(1)
    inner = juce_unb64(comp)
    old_size = struct.unpack("<i", inner[4:8])[0] + 9 if inner[:4] == b"VC2!" else 0
    trailer = inner[old_size:]
    body = xml.encode("utf-8") + b"\0"
    own = struct.pack("<ii", 0x21324356, len(body) - 1) + body
    new_xml = xml_outer.replace(comp, juce_b64(own + trailer))
    nb = new_xml.encode("utf-8") + b"\0"
    open(tmp, "wb").write(struct.pack("<ii", 0x21324356, len(nb) - 1) + nb)
    proc.load_state(tmp)
    os.remove(tmp)

def load_vaporizer_patch(proc, vvp):
    """A Vaporizer2 patch (.vvp: the XML its preset browser loads), with its delays, reverbs and arpeggiator switched
    off: the space comes from Ghost Atlas."""
    import re
    xml = open(vvp, encoding="utf-8").read()
    xml = re.sub(r'(id="m_b(?:Delay|Reverb|ARP)OnOff[A-Za-z0-9_]*" text=")[^"]*"', r'\1Off"', xml)
    set_plugin_xml_state(proc, xml)

def valuetree_xml(data):
    """A JUCE ValueTree written with writeToStream (an Odin 2 .odin patch), as the XML createXml would give."""
    import struct
    from xml.sax.saxutils import quoteattr
    pos = 0
    def cstr():
        nonlocal pos
        e = data.index(b"\0", pos); s = data[pos:e].decode("utf-8"); pos = e + 1; return s
    def cint():
        nonlocal pos
        n = data[pos]; pos += 1; neg = n & 0x80; n &= 0x7f
        v = int.from_bytes(data[pos:pos + n], "little"); pos += n
        return -v if neg else v
    def var():
        nonlocal pos
        n = cint(); kind = data[pos]; raw = data[pos + 1: pos + n]; pos += n
        if kind == 1: return str(struct.unpack("<i", raw)[0])
        if kind == 2: return "1"
        if kind == 3: return "0"
        if kind == 4:
            x = struct.unpack("<d", raw)[0]; r = repr(x)
            return r[:-2] if r.endswith(".0") else r
        if kind == 5: return raw.rstrip(b"\0").decode("utf-8")
        if kind == 6: return str(struct.unpack("<q", raw)[0])
        return ""
    def tree():
        name = cstr(); props = [(cstr(), var()) for _ in range(cint())]
        kids = [tree() for _ in range(cint())]
        a = "".join(f" {k}={quoteattr(v)}" for k, v in props)
        return f"<{name}{a}/>" if not kids else f"<{name}{a}>" + "".join(kids) + f"</{name}>"
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + tree()

def load_odin_patch(proc, path, fx_off=True):
    """An Odin 2 patch (.odin) as the plug-in's state, with its delay, reverb, chorus and phaser switched off."""
    import re
    xml = valuetree_xml(open(path, "rb").read())
    if fx_off:
        for fxname in ("delay", "reverb", "chorus", "phaser", "flanger", "arp"):
            xml = re.sub(r'(<PARAM id="%s_on" value=")[^"]*"' % fxname, r'\g<1>0"', xml)
    set_plugin_xml_state(proc, xml)

def render_patch(spec, synth, outdir, eng_cache):
    k = dict(KINDS[spec["kind"]]); k.update(spec.get("range", {}))
    plug = synth["plugin"]
    eng = dd.RenderEngine(SR, 256)
    proc = eng.make_plugin_processor("p", plug)
    pre = spec.get("preset")
    if isinstance(pre, dict) and "syx" in pre:
        pass        # a DX7 cartridge voice: set through the plug-in's parameters once the graph is up (below)
    elif isinstance(pre, str) and pre.lower().endswith((".fxp", ".fxb")):
        if not proc.load_preset(pre): raise RuntimeError("could not load " + pre)
    elif isinstance(pre, str) and pre.lower().endswith(".vstpreset"):
        if not proc.load_vst3_preset(pre): raise RuntimeError("could not load " + pre)
    elif isinstance(pre, str) and pre.lower().endswith(".vvp"):
        load_vaporizer_patch(proc, pre)
    elif isinstance(pre, str) and pre.lower().endswith(".odin"):
        load_odin_patch(proc, pre)
    elif isinstance(pre, str):
        proc.load_state(pre)
    names = {proc.get_parameter_name(i): i for i in range(proc.get_plugin_parameter_size())}
    for nm, v in list(synth.get("off", {}).items()) + list(spec.get("set", {}).items()):
        if nm in names: proc.set_parameter(names[nm], float(v))
        else: print(f"    (no parameter {nm})")
    eng.load_graph([(proc, [])])
    if isinstance(pre, dict) and "syx" in pre:
        print(f"    DX7 voice: {set_dx7_voice(proc, pre['syx'], int(pre['voice']))}")
        eng.render(0.5)
    if synth.get("settle"):        # a plug-in that loads its patch on a thread of its own: give it real time to finish
        for _ in range(int(synth["settle"] * 2)): eng.render(0.5); time.sleep(0.5)
    flush(eng, proc)
    safe = spec["name"].replace(" ", "_").replace("/", "-").replace("'", "")
    sdir = os.path.join(outdir, "samples", safe); os.makedirs(sdir, exist_ok=True)
    notes = list(range(k["lo"], k["hi"] + 1, k["step"]))
    regions, stats = [], dict(loops=[], rel=[], peaks=[])
    for n in notes:
        for v in k["vels"]:
            proc.clear_midi()
            proc.add_midi_note(n, v, 0.0, k["hold"])
            eng.render(k["hold"] + k["tail"]); a = eng.get_audio().T.astype(np.float64)
            flush(eng, proc)
            mono = a.mean(axis=1)
            peak = np.abs(a).max(); stats["peaks"].append(peak)
            if peak < 1e-4: print(f"    {spec['name']} {n} v{v}: silent"); continue
            # start: the first frame within 60 dB of the peak, less 1 ms
            on = int(np.argmax(np.abs(mono) > peak * 1e-3)); on = max(0, on - SR // 1000)
            a = a[on:]
            e = env_db(a.mean(axis=1)); pk = e.max()
            off_frame = int(k["hold"] * SR) - on
            loop = None
            if k["held"]:
                sustain_level = e[min(len(e) - 1, int(0.9 * off_frame / 480))]
                if sustain_level > pk - 30:      # it holds: loop the last stretch before the key is let go
                    ls = int(min(2.0 if k["hold"] <= 5 else 3.5, k["hold"] * 0.45) * SR)
                    le_target = off_frame - int(0.15 * SR)
                    xf = int(0.12 * SR) if spec["kind"] in ("lead", "bass") else int(0.35 * SR)
                    ls = max(ls, xf + 1)
                    s0, e0, match = best_loop(a, ls, le_target, int(0.03 * SR), min(xf, 2048))
                    a = bake_loop(a, s0, e0, xf)
                    a = a[: e0 + int(0.02 * SR)]            # what is after the loop is never played
                    loop = (s0, e0, match); stats["loops"].append(match)
                    # how long the patch takes to die away once let go, for the SFZ's release
                    after = e[off_frame // 480:]
                    below = np.where(after < e[max(0, off_frame // 480 - 2)] - 60)[0]
                    stats["rel"].append((below[0] * 480 / SR) if len(below) else k["tail"])
            if loop is None:
                below = np.where(e > pk - 72)[0]
                end = (below[-1] + 1) * 480 if len(below) else len(a)
                a = a[: min(len(a), end)]
                still = env_db(a[-int(0.05 * SR):].mean(axis=1)).max() > pk - 60
                # a sound still ringing when the recording stops is faded over its last second, not cut
                fade = min(len(a) // 4, SR) if still else min(len(a), int(0.01 * SR))
                a[-fade:] *= (np.cos(np.linspace(0, math.pi / 2, fade)) ** 2)[:, None]
            fn = f"{safe}_{n:03d}_v{v}.flac"
            regions.append(dict(file=f"samples/{safe}/{fn}", key=n, vel=v, loop=loop, audio=a))
    if not regions: raise RuntimeError("nothing was heard")
    # one gain for the whole instrument: its loudest note peaks at -1 dBFS (nothing clips in 24-bit, the notes keep their
    # balance against each other)
    top = max(float(np.abs(r["audio"]).max()) for r in regions)
    gain = (10 ** (-1 / 20)) / top
    for r in regions:
        sf.write(os.path.join(outdir, r["file"]), (r.pop("audio") * gain).astype(np.float32), SR, subtype="PCM_24")
    rel = round(float(np.median(stats["rel"])), 2) if stats["rel"] else 0.1
    rel = min(rel, {"lead": 0.8, "bass": 0.35}.get(spec["kind"], 6.0))     # a tail that never fell 60 dB is not a release
    lines = [f"// {spec['name']}: {synth['label']} patch \"{spec.get('patch', '')}\", rendered offline by the Ghost Atlas Preset Factory",
             f"// {len(notes)} notes x {len(k['vels'])} velocities, {SR} Hz, dry", "<control> default_path=",
             f"<global> ampeg_release={rel if k['held'] else 0.25} amp_veltrack=30" + (f" transpose={12 * spec['octave']}" if spec.get("octave") else "")]
    vels = sorted(set(r["vel"] for r in regions))
    vbounds = {}
    for i, v in enumerate(vels):
        lo = 1 if i == 0 else (vels[i - 1] + v) // 2 + 1
        hi = 127 if i == len(vels) - 1 else (v + vels[i + 1]) // 2
        vbounds[v] = (lo, hi)
    keys = sorted(set(r["key"] for r in regions))
    for r in regions:
        i = keys.index(r["key"])
        lo = 0 if i == 0 else (keys[i - 1] + r["key"]) // 2 + 1
        hi = 127 if i == len(keys) - 1 else (r["key"] + keys[i + 1]) // 2
        s = f"<region> sample={r['file']} pitch_keycenter={r['key']} lokey={lo} hikey={hi} lovel={vbounds[r['vel']][0]} hivel={vbounds[r['vel']][1]}"
        s += f" loop_mode=loop_continuous loop_start={r['loop'][0]} loop_end={r['loop'][1] - 1}" if r["loop"] else " loop_mode=one_shot"
        lines.append(s)
    open(os.path.join(outdir, spec["name"] + ".sfz"), "w").write("\n".join(lines) + "\n")
    mb = sum(os.path.getsize(os.path.join(sdir, f)) for f in os.listdir(sdir)) / 1048576
    info = dict(name=spec["name"], synth=spec["synth"], patch=spec.get("patch", ""), kind=spec["kind"], category=spec["category"],
                notes=len(keys), velocities=len(vels), release=rel, mb=round(mb, 1),
                loop_match=round(float(np.mean(stats["loops"])), 3) if stats["loops"] else None, peak_db=round(db(max(stats["peaks"])), 1), gain_db=round(db(gain), 1))
    print(f"  {spec['name']:28s} {info}", flush=True)
    return info

def main():
    spec = json.load(open(sys.argv[1])); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    done_path = os.path.join(out, "renders.json")
    done = json.load(open(done_path)) if os.path.exists(done_path) else []
    for p in spec["patches"]:
        if only and p["name"] not in only: continue
        t0 = time.time()
        try:
            info = render_patch(p, spec["synths"][p["synth"]], out, None)
            done = [d for d in done if d["name"] != p["name"]] + [info]
            json.dump(done, open(done_path, "w"), indent=1)
            print(f"    {time.time() - t0:.0f}s", flush=True)
        except Exception as ex:  # noqa
            print(f"  FAILED {p['name']}: {ex}", flush=True)

if __name__ == "__main__":
    main()
