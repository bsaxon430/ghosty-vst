#!/usr/bin/env python3
# GHOST ATLAS - the factory sounds, as they are written by hand.
#
# Each sound names its layers (an instrument from the starter library, the part it plays, and what is done to it), its
# three send effects and its master effects. Levels are written here only as trims: the real level of every layer is
# set afterwards from the instrument's measured loudness and its part, pitched layers are brought onto their key, and
# every sound is played, checked and brought to one loudness by
#
#     GhostAtlasTests polish presets/factory-drafts.json assets/data/factory.json presets/factory-report.tsv
#
# so what ships is balanced by measurement, not by guesswork. Run this script first; it writes the drafts.
#
#     python3 tools/make_presets.py [instruments.json]     (the optional file, from `GhostAtlasTests list`, checks every name)
#
# The sounds of the latest voyage are also written on their own (presets/factory-drafts-new.json): polished alone and
# set in after the rest by tools/add_factory.py, the sounds already in the bank are not played again.
import json, math, sys, os, csv

I = {
 # pianos and keys
 "splendid": "splendid-grand:Splendid Grand Piano", "salamander": "fp-salamander-grand:Salamander Grand Piano", "upright": "fp-upright-piano-kw:Upright Piano KW",
 "ssopiano": "sso:Grand Piano", "scary": "karoryfer-scarypiano:Scarypiano",
 "wurli": "greg-sullivan-epianos:Wurlitzer EP200", "pianet": "greg-sullivan-epianos:Pianet T", "cp80": "greg-sullivan-epianos:CP80",
 "fmp1": "fp-fm-piano-1:FM-Piano 1", "fmp2": "fp-fm-piano-2:FM-Piano 2", "txfm": "vcsl:TX81Z FM Piano", "txpiano": "vcsl:TX81Z Piano 1", "txclav": "vcsl:TX81Z Clavisynth",
 "church": "fp-church-organ:Church Organ Emulation", "drawbar": "fp-drawbar-organ:Drawbar Organ Emulation", "pipequiet": "vcsl:Pipe Organ Quiet", "pipeloud": "vcsl:Pipe Organ Loud",
 "ren8": "vcsl:Renaissance Organ 8'", "ren4": "vcsl:Renaissance Organ 4'", "greatflute": "sso:Great - Flute 4ft", "gedact": "sso:Swell - Gedact 8ft", "diapason": "sso:Great - Open Diapason 8ft",
 "gamba": "sso:Swell - Gamba 8ft", "bourdon16": "sso:Pedal - Bourdon 16ft", "celeste": "sso:Celeste", "harpsi8": "sso:Harpsichord 8'", "harpsi4": "sso:Harpsichord 4'",
 "cow_org_fancy": "karoryfer-cowsynth:Cowsynth Fancy Organ", "cow_org_lonely": "karoryfer-cowsynth:Cowsynth Lonely Organ", "cow_org_wheezy": "karoryfer-cowsynth:Cowsynth Wheezy Organ",
 # synths
 "cow_pad_fancy": "karoryfer-cowsynth:Cowsynth Fancy Pad", "cow_pad_lonely": "karoryfer-cowsynth:Cowsynth Lonely Pad", "cow_pad_shifty": "karoryfer-cowsynth:Cowsynth Shifty Pad",
 "cow_pad_asth": "karoryfer-cowsynth:Cowsynth Asthmatic Pad", "cow_pad_wheezy": "karoryfer-cowsynth:Cowsynth Wheezy Pad",
 "cow_pl_fancy": "karoryfer-cowsynth:Cowsynth Fancy Pluck", "cow_pl_lonely": "karoryfer-cowsynth:Cowsynth Lonely Pluck", "cow_pl_shifty": "karoryfer-cowsynth:Cowsynth Shifty Pluck",
 "cow_pl_wheezy": "karoryfer-cowsynth:Cowsynth Wheezy Pluck", "baggy": "karoryfer-cowsynth:Cowsynth Baggy", "doc": "karoryfer-cowsynth:Cowsynth Doc",
 "sbrass1": "fp-synth-brass-1:Synth Brass 1", "sbrass2": "fp-synth-brass-2:Synth Brass 2", "sbass1": "fp-synth-bass-1:Synth Bass 1", "sbass2": "fp-synth-bass-2:Synth Bass 2",
 "lately": "fp-lately-bass:Lately Bass", "ssquare": "fp-synth-square:Synth Square", "calliope": "fp-synth-calliope:Synth Calliope", "fifths": "fp-synth-fifths:Synth Fifths",
 "basslead": "fp-synth-bass-lead:Synth Bass Lead", "sstr1": "fp-synth-strings-1:Synth Strings 1", "sstr2": "fp-synth-strings-2:Synth Strings 2",
 "newage": "fp-new-age:New Age", "sweep": "fp-sweep-pad:Sweep Pad", "bowedpad": "fp-synth-pad-bowed:Synth Pad Bowed", "padchoir": "fp-synth-pad-choir:Synth Pad Choir",
 "soundtrack": "fp-synth-soundtrack:Synth Soundtrack", "scifi": "fp-synth-scifi:Synth Sci Fi", "goblins": "fp-synth-goblins:Synth Goblins", "crystal": "fp-synth-crystal:Synth Crystal",
 # wave-synth instruments
 "sawstack": "akwf:Saw Stack", "sawsolo": "akwf:Saw Solo", "brightsaw": "akwf:Bright Saw Stack", "squarestack": "akwf:Square Stack", "thinpulse": "akwf:Thin Pulse",
 "triangle": "akwf:Triangle", "sine": "akwf:Sine Sub", "sawpad": "akwf:Saw Pad", "sawpluck": "akwf:Saw Pluck", "squarepluck": "akwf:Square Pluck",
 "fmglass": "akwf:FM Glass", "fmbell": "akwf:FM Bell", "aah": "akwf:Voice Wave Aah", "ooh": "akwf:Voice Wave Ooh", "eeh": "akwf:Voice Wave Eeh", "chip": "akwf:Chip Lead",
 "overtone": "akwf:Overtone Pad", "theremin": "akwf:Theremin", "organwave": "akwf:Organ Wave", "softorgan": "akwf:Soft Organ Wave", "bitglass": "akwf:Bit Glass",
 "handdrawn": "akwf:Hand Drawn Pad", "cellowave": "akwf:Cello Wave", "clarwave": "akwf:Clarinet Wave", "grainwave": "akwf:Grain Wave", "fuzzwave": "akwf:Fuzz Wave",
 # bells and mallets
 "tubular": "fp-tubular-bells:Tubular Bells", "glass": "fp-glasses:Glass", "hang": "fp-hang-d-minor:Hang-D-minor", "kalimba": "fp-kalimba:Kalimba",
 "kal_kenya": "vcsl:Kalimba, Kenya", "kal_tanz": "vcsl:Kalimba, Tanzania", "mbira": "vcsl:Mbira Dza Vadzimu Nyamaropa, Zimbabwe, Low B",
 "balafon_soft": "vcsl:Balafon Soft Mallet", "balafon_hard": "vcsl:Balafon Hard Mallet", "belltree": "vcsl:Bell Tree Stroke", "belltree1": "vcsl:Bell Tree Individual",
 "glock": "sso:Glockenspiel", "vglock": "vcsl:Glockenspiel", "tubglock": "vcsl:Tubular Glockenspiel (medium)", "tubglock_q": "vcsl:Tubular Glockenspiel (quiet)",
 "gong": "vcsl:Gong 2", "handchimes": "vcsl:Hand Chimes", "marimba": "vcsl:Marimba", "marimbarolls": "sso:Marimba Rolls", "vibes": "vcsl:Vibraphone Soft Mallets",
 "vibes_hard": "vcsl:Vibraphone Hard Mallets", "vibes_bowed": "vcsl:Vibraphone Bowed", "ssovibes": "sso:Vibraphone", "chimes": "sso:Chimes", "crotales": "sso:Crotales",
 "xylo": "sso:Xylophone", "wineglass": "vcsl:Wine Glasses (slow)", "wineglass_q": "vcsl:Wine Glasses (quick)", "timpani": "sso:Timpani",
 # plucked
 "harp": "fp-concert-harp:Concert Harp", "ssoharp": "sso:Concert Harp", "folkharp": "vcsl:Folk Harp", "dantranh": "vcsl:Dan Tranh Normal", "dantranh_trem": "vcsl:Dan Tranh Tremolo",
 "dantranh_fx": "vcsl:Dan Tranh FX", "psaltery": "vcsl:Plucked Psaltery", "psaltery_bowed": "vcsl:Bowed Psaltery", "cello_pluck": "karoryfer-bigcat-cello:Cello Plucked",
 "pizz": "sso:All Strings Pizzicato", "vpizz": "vsco2-ce:Violin Section Pizzicato", "bpizz": "sso:Basses Pizzicato",
 # voices
 "largechorus": "sso:Large Chorus", "mixedchorus": "sso:Mixed Chorus", "ork_vox": "karoryfer-272-merry-orks:Ork Vocals", "ork_talk": "karoryfer-272-merry-orks:Ork Dialogue",
 "ork_pre": "karoryfer-272-merry-orks:Ork Predrops",
 # strings
 "allstr": "sso:All Strings Sustain", "allstr_trem": "sso:All Strings Tremolo", "allstr_stac": "sso:All Strings Staccato", "allstr_harm": "sso:All Strings Harmonics",
 "violins1": "sso:1st Violins Sustain", "violins1_harm": "sso:1st Violins Harmonics", "violas": "sso:Violas Sustain", "violas_harm": "sso:Violas Harmonics", "celli": "sso:Celli Sustain",
 "basses": "sso:Basses Sustain", "violinsolo": "sso:Violin Solo 1 Sustain", "cellosolo": "sso:Cello Solo Sustain", "cello": "karoryfer-bigcat-cello:Cello Bowed",
 "vsec_violins": "vsco2-ce:Violin Section Sustain Vibrato", "vsec_violas": "vsco2-ce:Viola Section Sustain Vibrato", "vsec_celli": "vsco2-ce:Cello Section Sustain Vibrato",
 "vsec_violins_trem": "vsco2-ce:Violin Section Tremolo", "vsec_violins_spic": "vsco2-ce:Violin Section Spiccato", "vsec_celli_spic": "vsco2-ce:Cello Section Spiccato",
 "soloviolin": "vsco2-ce:Solo Violin Vibrato", "blackheart": "karoryfer-string-cyborgs:Blackheart", "ironface": "karoryfer-string-cyborgs:Ironface", "zinc": "karoryfer-string-cyborgs:Zinc",
 "blackheart_m": "karoryfer-string-cyborgs:Blackheart Master", "ironface_m": "karoryfer-string-cyborgs:Ironface Master",
 # brass and woodwinds
 "allbrass": "sso:All Brass Sustain", "allbrass_stac": "sso:All Brass Staccato", "horns": "sso:Horns Sustain", "hornsolo": "sso:Horn Solo Sustain", "trombones": "sso:Trombones Sustain",
 "trumpets": "sso:Trumpets Sustain", "trumpets_stac": "sso:Trumpets Staccato", "trumpetsolo": "sso:Trumpet Solo Sustain", "tuba": "sso:Tuba Sustain", "basstrombone": "sso:Bass Trombone Solo Sustain",
 "allwinds": "sso:All Woodwinds Sustain", "flutes": "sso:Flutes Sustain", "flutesolo": "sso:Flute Solo 1 Sustain", "altoflute": "sso:Alto Flute Solo Sustain", "oboe": "sso:Oboe Solo Sustain",
 "coranglais": "sso:Cor Anglais Solo Sustain", "clarinet": "sso:Clarinet Solo Sustain", "clarinets": "sso:Clarinets Sustain", "bassclarinet": "sso:Bass Clarinet Solo Sustain",
 "bassoons": "sso:Bassoons Sustain", "contrabassoon": "sso:Contrabassoon Solo Sustain", "ocarina": "vcsl:Ocarina", "ocarina_vib": "vcsl:Ocarina Vibrato",
 "didge": "vcsl:Didgeridoo (sus main)",
 # Surge XT, played offline into multisampled instruments (surge-xt-renders: tools/factory/render_instruments.py)
 "sx_trance": "surge-xt-renders:Trance Saw", "sx_huge": "surge-xt-renders:Huge Saws", "sx_jupiter": "surge-xt-renders:Jupiter Stack", "sx_classic": "surge-xt-renders:Classic Lead",
 "sx_octsaw": "surge-xt-renders:Octave Saw Lead", "sx_pluck": "surge-xt-renders:Pluck Nice", "sx_tpluck": "surge-xt-renders:Trance Pluck", "sx_sawpluck": "surge-xt-renders:Saw Pluck Bright",
 "sx_widepad": "surge-xt-renders:Wide Pad", "sx_warmpad": "surge-xt-renders:Warm Poly Pad", "sx_teethpad": "surge-xt-renders:Saw Teeth Pad", "sx_growth": "surge-xt-renders:Growth Pad",
 "sx_computer": "surge-xt-renders:Space Computer Pad", "sx_primes": "surge-xt-renders:Prime Texture", "sx_dxep": "surge-xt-renders:DX Tine EP", "sx_fmbell": "surge-xt-renders:Fantasy FM Bell",
 "sx_glassbell": "surge-xt-renders:Glass Bell Pluck", "sx_bass": "surge-xt-renders:Analog Bass", "sx_sub": "surge-xt-renders:Pure Sub", "sx_movebass": "surge-xt-renders:Moving Bass",
 # Meta Open Sound and the Stargate sample pack
 "metapiano": "meta-open-sound:Meta Upright Piano", "metanylon": "meta-open-sound:Meta Nylon Guitar", "metasteel": "meta-open-sound:Meta Steel Guitar",
 "pinknoise": "stargate-sample-pack:Pink Noise", "whitenoise": "stargate-sample-pack:White Noise", "spookscape": "stargate-sample-pack:Spookscape",
 "cyborganic": "stargate-sample-pack:Cyborganic Soundscape", "whatsinthere": "stargate-sample-pack:What's In There", "gtrharmA": "stargate-sample-pack:Guitar Harmonics A",
 # set 2: OB-Xd, Dexed, Vaporizer2 and Odin 2, played into multisampled instruments (tools/factory/render_plugins.py)
 "s2_ob_big_pad": "synth-renders-2:OB Big Pad", "s2_ob_1981_pad": "synth-renders-2:OB 1981 Pad", "s2_ob_blade_brass": "synth-renders-2:OB Blade Brass",
 "s2_ob_cloud_strings": "synth-renders-2:OB Cloud Strings", "s2_ob_open_sky": "synth-renders-2:OB Open Sky", "s2_ob_jump_brass": "synth-renders-2:OB Jump Brass",
 "s2_ob_big_lead": "synth-renders-2:OB Big Lead", "s2_ob_super_80s_lead": "synth-renders-2:OB Super 80s Lead", "s2_ob_outrun_bass": "synth-renders-2:OB Outrun Bass",
 "s2_ob_house_bass": "synth-renders-2:OB House Bass", "s2_ob_heaven_pluck": "synth-renders-2:OB Heaven Pluck", "s2_ob_polar_pluck": "synth-renders-2:OB Polar Pluck",
 "s2_ob_synthwave_stab": "synth-renders-2:OB Synthwave Stab", "s2_ob_rhodes": "synth-renders-2:OB Rhodes", "s2_dx_rhodos": "synth-renders-2:DX Rhodos",
 "s2_dx_fm_rhodes": "synth-renders-2:DX FM Rhodes", "s2_dx_e_piano_13": "synth-renders-2:DX E-Piano 13", "s2_dx_dl_rhodes": "synth-renders-2:DX DL Rhodes",
 "s2_dx_glass_bell": "synth-renders-2:DX Glass Bell", "s2_dx_tine_chimes": "synth-renders-2:DX Tine Chimes", "s2_dx_lunar_bell": "synth-renders-2:DX Lunar Bell",
 "s2_dx_fm_chimes": "synth-renders-2:DX FM Chimes", "s2_dx_fm_bass": "synth-renders-2:DX FM Bass", "s2_dx_synbass_54": "synth-renders-2:DX Synbass 54",
 "s2_dx_torim_pluck": "synth-renders-2:DX Torim Pluck", "s2_dx_angel_pluck": "synth-renders-2:DX Angel Pluck", "s2_dx_celesta": "synth-renders-2:DX Celesta",
 "s2_dx_digi_pad": "synth-renders-2:DX Digi Pad", "s2_dx_syn_lead_32": "synth-renders-2:DX Syn Lead 32", "s2_vz_fat_edm_lead": "synth-renders-2:VZ Fat EDM Lead",
 "s2_vz_mighty_lead": "synth-renders-2:VZ Mighty Lead", "s2_vz_future_bass_lead": "synth-renders-2:VZ Future Bass Lead", "s2_vz_layer_lead": "synth-renders-2:VZ Layer Lead",
 "s2_vz_hypersaw": "synth-renders-2:VZ Hypersaw", "s2_vz_lord_of_synths": "synth-renders-2:VZ Lord Of Synths", "s2_vz_juno_dream_pad": "synth-renders-2:VZ Juno Dream Pad",
 "s2_vz_epic_pad": "synth-renders-2:VZ Epic Pad", "s2_vz_majestic_pad": "synth-renders-2:VZ Majestic Pad", "s2_vz_rich_pad": "synth-renders-2:VZ Rich Pad",
 "s2_vz_shimmer_pad": "synth-renders-2:VZ Shimmer Pad", "s2_vz_bladerunner_pad": "synth-renders-2:VZ Bladerunner Pad", "s2_vz_shores_of_eden": "synth-renders-2:VZ Shores Of Eden",
 "s2_vz_big_room_pluck": "synth-renders-2:VZ Big Room Pluck", "s2_vz_glassy_dance_pluck": "synth-renders-2:VZ Glassy Dance Pluck", "s2_vz_silver_pluck": "synth-renders-2:VZ Silver Pluck",
 "s2_vz_sky_plucks": "synth-renders-2:VZ Sky Plucks", "s2_vz_melodic_techno_bass": "synth-renders-2:VZ Melodic Techno Bass", "s2_vz_deep_detroit_bass": "synth-renders-2:VZ Deep Detroit Bass",
 "s2_vz_bright_house_piano": "synth-renders-2:VZ Bright House Piano", "s2_vz_bells": "synth-renders-2:VZ Bells", "s2_vz_clean_brass": "synth-renders-2:VZ Clean Brass",
 "s2_vz_happy_chords": "synth-renders-2:VZ Happy Chords", "s2_odin_edm_projektor": "synth-renders-2:Odin EDM Projektor", "s2_odin_unison_glide_lead": "synth-renders-2:Odin Unison Glide Lead",
 "s2_odin_synth_brass": "synth-renders-2:Odin Synth Brass", "s2_odin_synthwave_pad": "synth-renders-2:Odin Synthwave Pad", "s2_odin_warm_pad": "synth-renders-2:Odin Warm Pad",
 "s2_odin_choir_aahs": "synth-renders-2:Odin Choir Aahs", "s2_odin_synth_strings": "synth-renders-2:Odin Synth Strings", "s2_odin_fm_sphere": "synth-renders-2:Odin FM Sphere",
 "s2_odin_moonshade": "synth-renders-2:Odin Moonshade", "s2_odin_deep_bass": "synth-renders-2:Odin Deep Bass", "s2_odin_growly_fm_bass": "synth-renders-2:Odin Growly FM Bass",
 "s2_odin_analog_bass": "synth-renders-2:Odin Analog Bass", "s2_odin_house_stab": "synth-renders-2:Odin House Stab", "s2_odin_saw_pluck": "synth-renders-2:Odin Saw Pluck",
 "s2_odin_chord_pluck": "synth-renders-2:Odin Chord Pluck", "s2_odin_e_piano": "synth-renders-2:Odin E-Piano", "s2_odin_fm_piano": "synth-renders-2:Odin FM Piano",
 "s2_odin_glassy_bell": "synth-renders-2:Odin Glassy Bell", "s2_odin_ethereal_bells": "synth-renders-2:Odin Ethereal Bells", "s2_odin_floating_dreams": "synth-renders-2:Odin Floating Dreams",
 "s2_odin_pacific_nights": "synth-renders-2:Odin Pacific Nights",
 "cave_saws": "karoryfer-caveman-cosmonaut:Caveman Cosmonaut Supersawism", "cave_pad": "karoryfer-caveman-cosmonaut:Caveman Cosmonaut Evolving Pad",
}

LFO_SHAPES = {"sine": 0, "triangle": 1, "ramp": 2, "square": 3, "drift": 4}
ATT = lambda seconds: round(math.sqrt(seconds / 6.0), 3)      # where the ATTACK control sits for this many seconds
REL = lambda seconds: round(math.sqrt(seconds / 10.0), 3)     # where the RELEASE control sits to add this many seconds

def fx(unit, mix=1.0, on=True, **values):
    return {"unit": unit, "on": on, "mix": mix, "values": {k: float(v) for k, v in values.items()}}

# SPECTRAL FX (Poltergeist 2's rack). Its effects and their controls are named here as upstream names them, in their own
# units; presets/spectral-catalogue.json (written by `GhostAtlasTests spectral list`) says how each is kept in a sound.
RACK = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "presets", "spectral-catalogue.json"), encoding="utf-8"))

def _kept(knob, value):
    """A control's value in its own units -> as a sound keeps it (between 0 and 1)."""
    if knob["kind"] == "choice" and isinstance(value, str): value = knob["choices"].index(value)
    if knob["kind"] == "switch": value = 1.0 if value else 0.0
    v = min(knob["max"], max(knob["min"], float(value)))
    if knob["kind"] != "float": v = round(v)
    if knob["max"] <= knob["min"]: return 0.0
    if knob["log"] and knob["kind"] == "float": return math.log(v / knob["min"]) / math.log(knob["max"] / knob["min"])
    return (v - knob["min"]) / (knob["max"] - knob["min"])

def rack(*places, warp=None, window=2048, shape="Hann", overlap=4, chain=1.0, warp_first=False, mix=1.0, on=True):
    """SPECTRAL FX in a slot. places: up to four of (effect, {control: value}, [the place's mix]); a control that is not
    named starts where the Effects page starts it. warp: None for off, or WARP CORE's controls by name ({} = as it arrives)."""
    v = {"fft": float(RACK["windows"].index(window)), "window": float(RACK["shapes"].index(shape)), "overlap": float(overlap), "limit": 2.0,
         "chain": float(chain), "order": 1.0 if warp_first else 0.0}
    if len(places) > 4: sys.exit("the rack has four places")
    for s in range(4):
        n = "s" + str(s + 1)
        v[n + "_fx"], v[n + "_on"], v[n + "_mix"] = 0.0, 1.0, 1.0
        for k in range(8): v[f"{n}_p{k + 1}"] = 0.0
        if s >= len(places): continue
        name, controls = places[s][0], (places[s][1] if len(places[s]) > 1 else {})
        found = [e for e in RACK["effects"] if e["name"] == name]
        if not found: sys.exit("SPECTRAL FX has no effect called " + name)
        e = found[0]
        wrong = sorted(set(controls) - {k["name"] for k in e["knobs"]})
        if wrong: sys.exit(name + " has no control called " + ", ".join(wrong) + " (it has: " + ", ".join(k["name"] for k in e["knobs"]) + ")")
        v[n + "_fx"] = float(e["index"])
        if len(places[s]) > 2: v[n + "_mix"] = float(places[s][2])
        for k, knob in enumerate(e["knobs"]): v[f"{n}_p{k + 1}"] = round(_kept(knob, controls[knob["name"]]), 6) if knob["name"] in controls else knob["start"]
    v["w_on"] = 0.0 if warp is None else 1.0
    wrong = sorted(set(warp or {}) - {k["name"] for k in RACK["warp"]})
    if wrong: sys.exit("WARP CORE has no control called " + ", ".join(wrong) + " (it has: " + ", ".join(k["name"] for k in RACK["warp"]) + ")")
    for knob in RACK["warp"]:
        val = (warp or {}).get(knob["name"], knob["start"])
        if knob["kind"] == "choice" and isinstance(val, str): val = knob["choices"].index(val)
        v[knob["id"]] = float(val)
    return {"unit": "spectral", "on": on, "mix": mix, "values": v}

def L(inst, role, trim=0.0, **k):
    """One layer. trim: dB on top of the level its measured loudness and its part give it."""
    p, shape, ins = {"on": 1.0}, {}, []
    if trim: p["gain"] = float(trim)
    for key, val in k.items():
        if key == "oct": p["coarse"] = 12.0 * val
        elif key == "att": p["attack"] = ATT(val)
        elif key == "rel": p["release"] = REL(val) if val > 0 else float(val)
        elif key in ("lp", "lp24", "hp", "bp"): p["filter"] = {"lp": 1.0, "lp24": 2.0, "hp": 3.0, "bp": 4.0}[key]; p["cutoff"] = float(val)
        elif key == "lfo":
            for a, b in val.items():
                if a == "shape": p["lfoShape"] = float(LFO_SHAPES[b])
                elif a == "rate": p["lfoRate"] = float(b)
                elif a == "div": p["lfoSync"] = 1.0; p["lfoDiv"] = float(b)
                else: p["lfo" + a.capitalize()] = float(b)        # amp, pan, cutoff, pitch
        elif key == "rev": shape["reverse"] = bool(val)
        elif key == "loop": shape["loop"] = 1; shape["loopStart"], shape["loopEnd"], shape["loopFade"] = val
        elif key == "oneshot": shape["loop"] = 2
        elif key in ("startRange", "voices", "keyFadeIn", "keyFadeOut", "velFadeIn", "velFadeOut"): shape[key] = val
        elif key == "ins": ins = list(val)
        elif key in ("a", "b", "c"): p["send" + key.upper()] = float(val)
        else: p[key] = float(val)                                  # an engine control by its own id (pan, fine, width, reso, keyLo ...)
    d = {"instrument": I[inst], "role": role, "params": p}
    if shape: d["shape"] = shape
    if ins: d["inserts"] = ins
    return d

EMPTY = {"instrument": "", "params": {"on": 0.0}}
SOUNDS = []

def S(name, category, tags, description, *layers, chorus=None, echo=None, space=None, returns=(0.6, 0.5, 0.7), squeeze=0.0, master=None, set_aside=None):
    """One sound. chorus / echo: settings for the first two sends. space: settings for the hall. Any of the three can
    instead be (unit, settings) to put another unit there, or a unit already written out (rack(...)).
    squeeze: how much of the three-band squeeze on the master (0 = off)."""
    def send(unit, given):
        if isinstance(given, dict) and "unit" in given: return given
        return fx(given[0], **given[1]) if isinstance(given, tuple) else fx(unit, **(given or {}))
    sends = [send("chorus", chorus), send("echo", echo), send("halls", space)]
    m = master or [fx("eq", on=False), fx("ott", mix=squeeze if squeeze > 0 else 0.35, on=squeeze > 0), fx("lim")]
    SOUNDS.append({"format": "ghost-atlas-preset", "version": 1, "name": name, "author": "Ghost Atlas", "category": category, "description": description,
                   "tags": tags.split(), "layers": list(layers) + [EMPTY] * (8 - len(layers)), "sends": sends, "returns": list(returns), "masterFx": m, "masterDb": 0.0})
    if set_aside: SOUNDS[-1]["setAside"] = list(set_aside)      # checks that do not apply to this kind of sound (see `polish`)

# ============================================================================================ AMBER MARSH: pianos, keys
S("Linen Morning", "Pianos", "warm gentle intimate", "A grand with the top taken off and a bowed pad breathing under it",
  L("splendid", "primary", lp=9000, c=0.3), L("bowedpad", "atmosphere", att=0.9, c=0.5, width=1.4), space=dict(decay=2.6, size=0.55))
S("Marsh Light Grand", "Pianos", "bright clear wide", "The concert grand as it is, in a clear room",
  L("salamander", "primary", c=0.26), space=dict(decay=2.1, size=0.5, predelay=18), returns=(0.6, 0.5, 0.62))
S("Low Sun Upright", "Pianos", "warm vintage intimate", "An old upright with a little wobble and dust on it",
  L("upright", "primary", c=0.2, ins=[fx("retro", magic=0.32, noise=0.1, wobble=0.22, flutter=0.15)]), space=dict(decay=1.5, size=0.35))
S("Paper Boats", "Pianos", "gentle bright delicate", "Soft grand, water glasses on the harder notes, a celeste on each key",
  L("salamander", "primary", lp=5200, velCurve=-0.2, c=0.3), L("glass", "shimmer", velFadeIn=55, c=0.5), L("celeste", "transient", -3, c=0.4, b=0.15),
  echo=dict(div=7, fb=0.3), space=dict(decay=2.8))
S("Drowned Steinway", "Pianos", "cinematic slow ethereal", "A grand with its own notes swelling backwards behind it, in a shimmering hall",
  L("splendid", "primary", c=0.45), L("splendid", "atmosphere", -2, rev=True, loop=(0.2, 0.9, 0.3), att=1.2, c=0.85, width=1.6),
  space=("shimmer", dict(decay=8.0, shimmer=0.4, size=0.8)), returns=(0.6, 0.5, 0.6))
S("Reed And Hammer", "Pianos", "warm cinematic moody", "Grand piano with a solo cello leaning on every note and strings far behind",
  L("ssopiano", "primary", c=0.35), L("cello", "support", att=0.5, c=0.4), L("allstr", "atmosphere", att=1.5, c=0.6), space=dict(decay=3.2, size=0.65))
S("Felt In The Reeds", "Pianos", "dark gentle intimate", "A felted upright, close and quiet, with still air around it",
  L("upright", "primary", lp=3200, velSens=0.7, c=0.3), L("newage", "atmosphere", -2, att=1.4, c=0.7), space=dict(decay=3.0, damp=0.6))
S("Cathedral Grand", "Pianos", "wide slow epic", "A grand in a cathedral, with the organ holding the floor under it",
  L("salamander", "primary", c=0.5), L("church", "support", -5, lp=1500, att=0.4, c=0.6), space=dict(decay=5.5, size=0.85, predelay=35), returns=(0.6, 0.5, 0.75))
S("Dusk Wurli", "Keys", "warm vintage gentle", "A Wurlitzer through a warm tube with its tremolo on",
  L("wurli", "primary", a=0.25, c=0.25, lfo=dict(shape="triangle", rate=5.2, pan=0.55, amp=0.15), ins=[fx("tube", drive=0.4)]), space=dict(decay=1.8, size=0.45))
S("Tin Roof Pianet", "Keys", "bright vintage percussive", "A Pianet with tape echo answering it",
  L("pianet", "primary", b=0.3, c=0.2, ins=[fx("retro", magic=0.3, noise=0.06)]), echo=dict(div=7, fb=0.38, mode=0), space=dict(decay=1.6))
S("Stage Eighty", "Keys", "bright wide lush", "The electric grand of eighties stages, in an ensemble chorus, strings behind",
  L("cp80", "primary", a=0.45, c=0.3), L("sstr1", "atmosphere", -2, att=0.8, a=0.5, c=0.5), chorus=dict(mode=1, depth=0.6), space=dict(decay=2.6))
S("Glass FM", "Keys", "bright glassy wide", "Two FM pianos an octave apart in a slow chorus",
  L("fmp1", "primary", a=0.4, c=0.3), L("fmp2", "shimmer", 2, oct=1, a=0.5, c=0.4), chorus=dict(rate=0.3, depth=0.55), space=dict(decay=2.4))
S("Chapel In The Marsh", "Keys", "dark slow wide", "Two organs in a long stone room",
  L("church", "primary", c=0.55), L("pipequiet", "support", c=0.6), space=dict(decay=6.0, size=0.9, predelay=30), returns=(0.6, 0.5, 0.75))
S("Sunday Drawbars", "Keys", "warm vintage moving", "Drawbar organ, driven, through a turning speaker",
  L("drawbar", "primary", a=0.3, c=0.2, lfo=dict(shape="sine", rate=6.2, pan=0.6, amp=0.2, pitch=0.2), ins=[fx("tube", drive=0.5)]), space=dict(decay=1.6, size=0.4))
S("Sugar Plum", "Keys", "bright delicate gentle", "Celeste with a glockenspiel riding on top",
  L("celeste", "primary", c=0.45), L("glock", "shimmer", 3, c=0.55), space=dict(decay=3.0, size=0.6))
S("Clavi Dust", "Keys", "bright vintage percussive", "A digital clavinet gone a little to dust, with a sixteenth echo",
  L("txclav", "primary", b=0.25, c=0.2, ins=[fx("retro", magic=0.45, noise=0.08)]), echo=dict(div=3, fb=0.3), space=dict(decay=1.4))
S("Flute Stop", "Keys", "gentle airy steady", "Two soft stops of a small organ",
  L("greatflute", "primary", c=0.5), L("gedact", "support", c=0.5), space=dict(decay=4.0, size=0.7))
S("Harpsichord Hours", "Keys", "bright percussive vintage", "Harpsichord, eight foot with four foot above it, in a wooden room",
  L("harpsi8", "primary", c=0.3), L("harpsi4", "shimmer", 4, c=0.35), space=dict(decay=1.7, size=0.4))
S("Paper Organ", "Keys", "warm steady wide", "A drawn organ wave with a soft one under it, slowly turning",
  L("organwave", "primary", lp=5200, a=0.4, c=0.3), L("softorgan", "support", oct=-1, c=0.3), chorus=dict(mode=1, rate=0.25), space=dict(decay=2.4))

# ============================================================================================ CRIMSON LOOM: analog synths, leads, plucks
S("Dye Works", "Analog Synths", "bright wide steady", "A fat transistor pad with its own octave sparkling above",
  L("cow_pad_fancy", "primary", a=0.3, c=0.4), L("baggy", "shimmer", 3, oct=1, b=0.3, c=0.4), space=dict(decay=2.6))
S("Brass Furnace", "Analog Synths", "bright wide powerful", "Two synth brasses slightly apart with saws swelling behind",
  L("sbrass1", "primary", a=0.3, c=0.3), L("sbrass2", "support", fine=7, a=0.3, c=0.3), L("sawstack", "atmosphere", att=0.5, lp=3000, c=0.4),
  squeeze=0.25, space=dict(decay=2.2))
S("Transistor Radio", "Analog Synths", "warm vintage lo-fi", "A wheezing transistor organ heard through an old radio",
  L("cow_org_wheezy", "primary", a=0.35, c=0.3, ins=[fx("retro", magic=0.55, noise=0.12, wobble=0.3)]), space=dict(decay=1.8))
S("Loom Bass", "Analog Synths", "deep dark steady", "A round synth bass with a harder one on the front of each note",
  L("sbass1", "primary", keyHi=64, width=0.4, c=0.05), L("lately", "transient", keyHi=64, width=0.3, c=0.05), L("sine", "support", oct=-1, keyHi=60, width=0.0),
  returns=(0.6, 0.5, 0.4), squeeze=0.2)
S("Red Thread", "Analog Synths", "warm wide moving", "Five saws through a slowly opening filter, squares an octave below",
  L("sawstack", "primary", lp=2200, reso=0.3, a=0.35, c=0.3, lfo=dict(shape="sine", rate=0.12, cutoff=0.3)), L("squarestack", "support", oct=-1, lp=1800, c=0.2),
  chorus=dict(rate=0.35, depth=0.5), space=dict(decay=2.2))
S("Slow Vat", "Analog Synths", "warm slow evolving", "A saw pad whose filter drifts by itself, a darker pad under it",
  L("sawpad", "primary", lp24=1400, reso=0.25, c=0.45, lfo=dict(shape="drift", rate=0.2, cutoff=0.4)), L("cow_pad_shifty", "atmosphere", att=1.2, c=0.5), space=dict(decay=3.4))
S("Pulse Weaver", "Analog Synths", "bright rhythmic wide", "Thin pulses with a dotted echo bouncing side to side",
  L("thinpulse", "primary", lp=6000, b=0.4, c=0.25), L("squarestack", "support", lp=2400, c=0.2), echo=dict(div=7, fb=0.45, ping=1), space=dict(decay=1.8))
S("Triangle Loom", "Analog Synths", "gentle warm steady", "Plain triangles and a sine below: a soft electric keyboard that never was",
  L("triangle", "primary", a=0.4, c=0.35), L("sine", "support", oct=-1, c=0.2), chorus=dict(rate=0.5, depth=0.45), space=dict(decay=2.4))
S("Hot Iron", "Analog Synths", "bright powerful harsh", "Seven bright saws pushed through a hot transistor stage",
  L("brightsaw", "primary", lp=7000, c=0.25, ins=[fx("wreck", style=3, drive=0.4)]), L("sbass2", "support", oct=-1, keyHi=72, width=0.4), squeeze=0.3, space=dict(decay=1.6))
S("Ember Lead", "Leads", "bright steady singing", "A square lead with a slow vibrato and its octave under it",
  L("ssquare", "primary", b=0.3, c=0.25, lfo=dict(shape="sine", rate=5.5, pitch=0.3)), L("basslead", "support", oct=-1, c=0.2), echo=dict(div=9, fb=0.35), space=dict(decay=2.0))
S("Calliope Steam", "Leads", "airy bright gentle", "A steam-organ whistle with an ocarina inside it",
  L("calliope", "primary", b=0.25, c=0.35), L("ocarina", "support", c=0.4), echo=dict(div=7, fb=0.3), space=dict(decay=2.6))
S("Fifths Of Fire", "Leads", "bright wide powerful", "A lead in fifths over synth brass",
  L("fifths", "primary", a=0.3, b=0.25, c=0.3), L("sbrass2", "support", c=0.3), echo=dict(div=9, fb=0.3), space=dict(decay=2.0))
S("Weaver's Whistle", "Leads", "gentle airy singing", "An ocarina with vibrato and a theremin shadowing it",
  L("ocarina_vib", "primary", c=0.45, b=0.2), L("theremin", "support", -3, att=0.15, c=0.4, lfo=dict(shape="sine", rate=5.0, pitch=0.25)), space=dict(decay=3.0))
S("Scarlet Saw", "Leads", "bright singing powerful", "One saw with vibrato through a tube, five more behind it",
  L("sawsolo", "primary", lp=6000, reso=0.3, b=0.3, c=0.25, lfo=dict(shape="sine", rate=5.8, pitch=0.22), ins=[fx("tube", drive=0.45)]), L("sawstack", "support", -3, lp=3500, c=0.3),
  echo=dict(div=9, fb=0.4), space=dict(decay=2.2))
S("Chip Ember", "Leads", "bright percussive playful", "A game-console lead with a plucked square on the front of each note",
  L("chip", "primary", b=0.35, c=0.2), L("squarepluck", "transient", c=0.2), echo=dict(div=6, fb=0.4, mode=3), space=dict(decay=1.4))
S("Theremin Thread", "Leads", "ethereal singing slow", "A theremin with a hummed vowel behind it, in a shimmering hall",
  L("theremin", "primary", att=0.15, c=0.5, lfo=dict(shape="sine", rate=5.2, pitch=0.3)), L("ooh", "atmosphere", c=0.6), space=("shimmer", dict(decay=6.0, shimmer=0.35)), returns=(0.6, 0.5, 0.55))
S("Solo Reed", "Leads", "warm singing intimate", "A solo clarinet with a clarinet wave giving it a steadier core",
  L("clarinet", "primary", c=0.4), L("clarwave", "support", -4, att=0.08, lp=2600, c=0.3), space=dict(decay=2.6))
S("Crimson Harp", "Plucks", "warm ringing gentle", "Concert harp with a folk harp inside it",
  L("harp", "primary", c=0.4), L("folkharp", "support", c=0.4), space=dict(decay=3.2, size=0.65))
S("Shuttle Pluck", "Plucks", "bright rhythmic short", "A transistor pluck and its octave with a dotted echo",
  L("cow_pl_fancy", "primary", b=0.35, c=0.25), L("cow_pl_lonely", "shimmer", 3, oct=1, b=0.4, c=0.3), echo=dict(div=7, fb=0.42, ping=1), space=dict(decay=1.8))
S("Pizzicato Garden", "Plucks", "warm short playful", "A whole string orchestra plucking, with a section close by",
  L("pizz", "primary", c=0.35), L("vpizz", "support", c=0.35), space=dict(decay=1.7, size=0.45))
S("Dan Tranh Rain", "Plucks", "bright ringing delicate", "A Vietnamese zither with its own tremolo far behind and a triplet echo",
  L("dantranh", "primary", b=0.3, c=0.35), L("dantranh_trem", "atmosphere", c=0.6), echo=dict(div=5, fb=0.35), space=dict(decay=2.6))
S("Psaltery Thread", "Plucks", "bright ringing ethereal", "A plucked psaltery with the same instrument bowed behind it",
  L("psaltery", "primary", c=0.4), L("psaltery_bowed", "atmosphere", c=0.6), space=dict(decay=3.4))
S("Copper Wire", "Plucks", "bright short glassy", "A wiry synth pluck with crystal dust on top",
  L("cow_pl_shifty", "primary", b=0.3, c=0.25), L("crystal", "shimmer", 2, c=0.5), echo=dict(div=6, fb=0.35), space=dict(decay=2.0))
S("Saw Tooth Loom", "Plucks", "bright rhythmic powerful", "A saw pluck with a square pluck an octave down and a dotted echo",
  L("sawpluck", "primary", b=0.4, c=0.25), L("squarepluck", "support", oct=-1, c=0.15), echo=dict(div=7, fb=0.45, ping=1), squeeze=0.25, space=dict(decay=1.6))
S("Cello Fingers", "Plucks", "warm deep short", "A plucked cello with plucked basses under the low notes",
  L("cello_pluck", "primary", c=0.3), L("bpizz", "support", keyHi=60, c=0.3), space=dict(decay=1.9, size=0.5))
S("Harp And Glass", "Plucks", "bright ringing ethereal", "Harp with water glasses on top and a slow pad opening behind",
  L("ssoharp", "primary", c=0.4), L("glass", "shimmer", 2, c=0.55), L("sweep", "atmosphere", att=1.5, c=0.6), space=dict(decay=3.2))

# ============================================================================================ BLOSSOM FLOOD: choirs, vocal pads
S("Orchard Mass", "Choirs", "wide slow epic", "A large choir with a second choir inside it and strings behind, in a long hall",
  L("largechorus", "primary", c=0.5), L("mixedchorus", "support", c=0.5), L("allstr", "atmosphere", att=1.2, c=0.6), space=dict(decay=4.5, size=0.8, predelay=30), returns=(0.6, 0.5, 0.72))
S("Petal Sopranos", "Choirs", "bright airy ethereal", "The choir with its vowels moved up, flutes breathing around it",
  L("mixedchorus", "primary", c=0.5, ins=[fx("shift", formant=3)]), L("flutes", "atmosphere", att=0.8, c=0.6), space=("shimmer", dict(decay=6.0, shimmer=0.3)), returns=(0.6, 0.5, 0.55))
S("Low Monks", "Choirs", "dark deep slow", "The choir with its vowels moved down, basses and a pedal stop under it",
  L("largechorus", "primary", keyHi=72, c=0.55, ins=[fx("shift", formant=-4)]), L("basses", "support", keyHi=60, c=0.5), L("bourdon16", "support", -3, keyHi=55, c=0.5),
  space=dict(decay=6.0, size=0.9, damp=0.55), returns=(0.6, 0.5, 0.72))
S("Vowel Garden", "Choirs", "moving wide ethereal", "A choir whose vowels turn slowly from one to the next",
  L("mixedchorus", "primary", c=0.5, ins=[fx("formant", move=1, mdiv=12, mdepth=0.6, mouth=0.55, reso=0.4)]), L("padchoir", "support", c=0.5), space=dict(decay=3.8))
S("Blossom Aahs", "Choirs", "lush wide warm", "Choir and string machine through an ensemble chorus",
  L("largechorus", "primary", a=0.4, c=0.45), L("sstr2", "support", a=0.45, c=0.45), chorus=dict(mode=1, depth=0.55), space=dict(decay=3.2))
S("Flooded Hymn", "Choirs", "ethereal slow evolving", "A choir with itself running backwards behind it and wine glasses ringing above",
  L("mixedchorus", "primary", c=0.5), L("mixedchorus", "atmosphere", rev=True, loop=(0.25, 0.9, 0.3), att=1.5, c=0.8, width=1.5), L("wineglass", "shimmer", 4, c=0.6), space=dict(decay=5.0, size=0.8))
S("Orchard Children", "Choirs", "bright gentle delicate", "The choir an octave up with its vowels held where they were, a celeste on each note",
  L("mixedchorus", "primary", oct=1, c=0.5, ins=[fx("shift", formant=-4)]), L("celeste", "transient", c=0.5), space=dict(decay=3.4))
S("Wave Choir", "Choirs", "lush steady wide", "Sung vowels made from single waves, with the real choir inside them",
  L("aah", "primary", c=0.5), L("largechorus", "support", c=0.5), L("ooh", "atmosphere", oct=-1, c=0.5), chorus=dict(mode=1), space=dict(decay=3.6))
S("Drowned Choir", "Vocal Pads", "ethereal slow dark", "A choir pad under water, glasses above it, voices far off",
  L("padchoir", "primary", att=0.6, c=0.6), L("glass", "shimmer", 3, c=0.7, b=0.3), L("ork_pre", "atmosphere", -3, c=0.8), space=dict(decay=5.0, damp=0.5))
S("Breath On Water", "Vocal Pads", "airy gentle slow", "A choir pad with an alto flute breathing in it",
  L("padchoir", "primary", c=0.55), L("altoflute", "atmosphere", att=0.8, c=0.6), L("newage", "support", -2, c=0.5), space=dict(decay=4.0))
S("Silk Voices", "Vocal Pads", "warm slow lush", "A choir with the edges taken off, arriving slowly over a lonely pad",
  L("mixedchorus", "primary", lp=2600, att=1.2, c=0.55), L("cow_pad_lonely", "support", c=0.5), space=("shimmer", dict(decay=7.0, shimmer=0.3)), returns=(0.6, 0.5, 0.55))
S("Choir Of Glass", "Vocal Pads", "ethereal glassy slow", "Singing wine glasses with a bright choir far behind",
  L("wineglass", "primary", c=0.6), L("largechorus", "atmosphere", att=1.2, c=0.8, ins=[fx("shift", formant=4)]), space=dict(decay=5.5))
S("Petal Drift", "Vocal Pads", "gentle wide evolving", "A hummed vowel wave over a slow sweep and a choir pad",
  L("ooh", "primary", c=0.55), L("sweep", "atmosphere", c=0.6), L("padchoir", "support", c=0.5), space=dict(decay=4.2))
S("Lullaby For The Flood", "Vocal Pads", "gentle warm intimate", "A soft choir with a celeste on each note and bowed vibraphone behind",
  L("mixedchorus", "primary", lp=3200, att=0.5, c=0.5), L("celeste", "transient", -2, c=0.5), L("vibes_bowed", "atmosphere", c=0.6), space=dict(decay=3.6))
S("Reed Voices", "Vocal Pads", "warm steady moody", "A choir pad with clarinets inside it",
  L("padchoir", "primary", c=0.5), L("clarinets", "support", att=0.6, c=0.5), space=dict(decay=3.4))
S("Vowel Tide", "Vocal Pads", "moving wide lush", "A sung wave whose mouth opens and closes with the bar",
  L("aah", "primary", a=0.4, c=0.5, ins=[fx("formant", move=1, mdiv=14, mdepth=0.7, reso=0.45)]), L("eeh", "support", att=0.5, c=0.5), chorus=dict(mode=1), space=dict(decay=3.8))

# ============================================================================================ LANTERN SANCTUM: bells, vocal chops, vocal textures
S("Thousand Lights", "Bells", "ringing bright gentle", "Tubular bells with a kalimba on the front and crystal dust behind",
  L("tubular", "primary", c=0.5), L("kalimba", "transient", b=0.3, c=0.4), L("crystal", "atmosphere", att=0.5, c=0.6), space=dict(decay=4.0))
S("Lantern Vibes", "Bells", "warm gentle moving", "Vibraphone with its motor on and the bars bowed behind",
  L("vibes", "primary", c=0.45, lfo=dict(shape="sine", rate=4.5, amp=0.3)), L("vibes_bowed", "atmosphere", c=0.6), space=dict(decay=3.0))
S("Paper Lantern", "Bells", "bright delicate percussive", "A kalimba with water glasses and a dotted echo",
  L("kal_kenya", "primary", b=0.3, c=0.4), L("glass", "shimmer", 3, c=0.5), echo=dict(div=7, fb=0.35), space=dict(decay=2.6))
S("Temple Hang", "Bells", "warm ringing slow", "A hang drum in a temple, bowed metal behind it",
  L("hang", "primary", c=0.5), L("vibes_bowed", "atmosphere", c=0.6), space=dict(decay=5.0, size=0.8))
S("Crotale Rain", "Bells", "bright glassy ethereal", "Crotales and glockenspiel with a bell tree behind, in a shimmering hall",
  L("crotales", "primary", c=0.5), L("glock", "support", c=0.5), L("belltree", "atmosphere", c=0.7), space=("shimmer", dict(decay=6.0, shimmer=0.35)), returns=(0.6, 0.5, 0.5))
S("Mbira Dusk", "Bells", "warm percussive rhythmic", "Mbira and soft balafon with a triplet echo",
  L("mbira", "primary", b=0.3, c=0.35), L("balafon_soft", "support", c=0.35), echo=dict(div=5, fb=0.4), space=dict(decay=2.2))
S("Music Box Shrine", "Bells", "delicate vintage bright", "A music box that wobbles a little, celeste inside it",
  L("tubglock", "primary", c=0.4, ins=[fx("retro", magic=0.35, wobble=0.4, noise=0.05)]), L("celeste", "support", c=0.4), space=dict(decay=2.6))
S("Wine Glass Halo", "Bells", "ethereal glassy slow", "Singing wine glasses, hand chimes on each note, crotales at the top",
  L("wineglass", "primary", c=0.6), L("handchimes", "transient", c=0.5), L("crotales", "shimmer", 3, c=0.6), space=dict(decay=5.0))
S("Bronze Hour", "Bells", "dark ringing cinematic", "Orchestral chimes and tubular bells with soft horns far behind",
  L("chimes", "primary", c=0.5), L("tubular", "support", c=0.5), L("horns", "atmosphere", lp=1200, att=0.8, c=0.6), space=dict(decay=4.6, size=0.8))
S("Marimba Embers", "Bells", "warm percussive woody", "Marimba with rolls glowing behind it and a hard balafon on the front",
  L("marimba", "primary", c=0.35), L("marimbarolls", "atmosphere", c=0.5), L("balafon_hard", "transient", -2, c=0.3), space=dict(decay=2.0))
S("FM Carillon", "Bells", "bright glassy ringing", "An FM bell with glockenspiel on top and tubular bells inside",
  L("fmbell", "primary", c=0.5), L("vglock", "shimmer", 3, c=0.5), L("tubular", "support", c=0.5), space=dict(decay=4.0))
S("Glass Rain", "Bells", "delicate glassy bright", "Water glasses and a quiet music box with an echo",
  L("glass", "primary", b=0.3, c=0.5), L("tubglock_q", "shimmer", 4, b=0.3, c=0.5), echo=dict(div=7, fb=0.4), space=dict(decay=3.2))
S("Ork Sermon", "Vocal Chops", "harsh short playful", "Shouted metal vocals through a tube, with an echo",
  L("ork_vox", "primary", b=0.3, c=0.25, ins=[fx("tube", drive=0.5)]), echo=dict(div=6, fb=0.35), space=dict(decay=1.6),
  set_aside=["Even across the keys", "No loop seams"])      # a kit of shouted phrases: each key is a different shout, and consonants are not seams
S("Chant Cutter", "Vocal Chops", "rhythmic short wide", "A choir cut into a rhythm that follows the song's tempo",
  L("mixedchorus", "primary", rel=-0.4, c=0.35, ins=[fx("gate", pattern=4)]), L("largechorus", "support", rel=-0.4, c=0.35, ins=[fx("gate", pattern=4)]), space=dict(decay=2.2),
  set_aside=["No loop seams"])      # the gate's cuts are the sound, not seams (pattern 4, TRANCE: the gate's first pattern stands open and cuts nothing)
S("Lantern Stabs", "Vocal Chops", "short powerful percussive", "Choir hits with synth brass on the front",
  L("largechorus", "primary", rel=-0.6, c=0.3), L("sbrass2", "transient", rel=-0.5, c=0.25), space=dict(decay=1.3, size=0.4))
S("Talking Choir", "Vocal Chops", "rhythmic moving playful", "A choir whose mouth moves in eighth notes",
  L("mixedchorus", "primary", c=0.35, ins=[fx("formant", move=1, mdiv=6, mdepth=0.85, reso=0.55)]), space=dict(decay=2.0))
S("Whisper Wall", "Vocal Textures", "breathy dark evolving", "Voices smeared into a wall, an alto flute breathing through it",
  L("ork_pre", "primary", c=0.7, ins=[fx("spectra", smear=0.6, mix=0.8)]), L("altoflute", "atmosphere", att=1.0, c=0.7), space=dict(decay=7.0, size=0.9), returns=(0.6, 0.5, 0.75))
S("Breathing Paper", "Vocal Textures", "ethereal evolving slow", "A choir running backwards, broken into grains",
  L("mixedchorus", "primary", rev=True, loop=(0.2, 0.9, 0.3), att=0.8, c=0.6, ins=[fx("crystal", mix=0.5, pitch=0, reverse=0.8, density=0.6)]), L("altoflute", "atmosphere", att=1.0, c=0.6),
  space=dict(decay=5.0))
S("Frozen Vespers", "Vocal Textures", "slow ethereal dark", "A choir smeared until its words are gone, in a shimmering hall",
  L("largechorus", "primary", att=0.8, c=0.6, ins=[fx("spectra", smear=0.85, scatter=0.2, mix=0.85)]), space=("shimmer", dict(decay=9.0, shimmer=0.4)), returns=(0.6, 0.5, 0.6))
S("Ghost Syllables", "Vocal Textures", "evolving glassy strange", "Shouts turned backwards and scattered an octave up",
  L("ork_vox", "primary", rev=True, loop=(0.1, 0.9, 0.25), c=0.6, ins=[fx("crystal", mix=0.55, pitch=12, reverse=0.6)]), space=("portal", dict(decay=5.0, size=0.7)), returns=(0.6, 0.5, 0.55),
  set_aside=["Even across the keys", "In tune"])      # shouts turned backwards: every key is a different shout, and none is a pitch
S("Vowel Fog", "Vocal Textures", "slow breathy evolving", "A sung wave arriving slowly, its vowel drifting, with far-off voices",
  L("eeh", "primary", att=2.0, c=0.6, ins=[fx("formant", move=1, mdiv=15, mdepth=0.6)]), L("ork_pre", "atmosphere", c=0.8), space=dict(decay=6.0))

# ============================================================================================ EMERALD OPERA: strings, cinematic
S("Emerald Adagio", "Strings", "lush slow wide", "Full strings with the celli doubled and a chamber section close by",
  L("allstr", "primary", c=0.5), L("celli", "support", c=0.5), L("vsec_violins", "support", -2, c=0.5), space=dict(decay=3.6, size=0.75, predelay=25))
S("Chamber At Dusk", "Strings", "warm intimate gentle", "A few violas and celli in a small room, one violin above them",
  L("vsec_violas", "primary", c=0.35), L("vsec_celli", "support", c=0.35), L("soloviolin", "support", -4, c=0.4), space=dict(decay=1.8, size=0.45))
S("Tremolo Storm", "Strings", "dark moving cinematic", "The whole string orchestra trembling over held basses",
  L("allstr_trem", "primary", c=0.45), L("vsec_violins_trem", "support", c=0.45), L("basses", "support", keyHi=60, c=0.4), space=dict(decay=3.0))
S("Solo Cello Letter", "Strings", "warm intimate singing", "One cello close up, the section a long way behind it",
  L("cello", "primary", c=0.4), L("celli", "atmosphere", att=1.0, c=0.6), space=dict(decay=2.8))
S("Glass Harmonics", "Strings", "airy glassy ethereal", "Violin and viola harmonics with wine glasses behind them",
  L("violins1_harm", "primary", c=0.55), L("violas_harm", "support", c=0.55), L("wineglass", "atmosphere", c=0.6), space=dict(decay=4.2))
S("Spiccato Clockwork", "Strings", "short rhythmic bright", "Bouncing bows from violins and celli with a sixteenth echo",
  L("vsec_violins_spic", "primary", b=0.2, c=0.35), L("vsec_celli_spic", "support", c=0.35), L("allstr_stac", "support", c=0.35), echo=dict(div=3, fb=0.25), space=dict(decay=1.8))
S("String Machine 77", "Strings", "lush vintage wide", "Two string machines slightly apart through an ensemble chorus",
  L("sstr1", "primary", a=0.55, c=0.4), L("sstr2", "support", fine=6, a=0.55, c=0.4), chorus=dict(mode=1, depth=0.6, rate=0.45), space=dict(decay=2.8))
S("Rain In The House", "Strings", "moody wide cinematic", "Full strings with a bowed machine inside them and a harp on each note",
  L("allstr", "primary", c=0.5), L("blackheart", "support", c=0.4), L("harp", "transient", b=0.2, c=0.4), space=dict(decay=3.4))
S("First Desk", "Strings", "singing warm intimate", "A solo violin with the first violins far behind",
  L("violinsolo", "primary", c=0.45), L("violins1", "atmosphere", att=0.8, c=0.6), space=dict(decay=3.0))
S("Low Strings Breathing", "Strings", "dark deep slow", "Celli and basses with a bass clarinet inside them",
  L("celli", "primary", c=0.5), L("basses", "support", c=0.45), L("bassclarinet", "atmosphere", c=0.5), space=dict(decay=3.6, damp=0.55))
S("Opera House Brass", "Cinematic", "powerful wide epic", "The whole brass section with horns and trombones doubled, in a big house",
  L("allbrass", "primary", c=0.5), L("horns", "support", c=0.5), L("trombones", "support", c=0.5), space=dict(decay=3.8, size=0.8, predelay=30))
S("Noble Horns", "Cinematic", "warm wide epic", "A horn section with a solo horn inside it and strings behind",
  L("horns", "primary", c=0.5), L("hornsolo", "support", c=0.5), L("allstr", "atmosphere", att=1.0, c=0.6), space=dict(decay=3.6, size=0.75))
S("Curtain Rise", "Cinematic", "epic powerful wide", "Strings, brass, a drum on each note and a choir at the back",
  L("allstr", "primary", c=0.5), L("allbrass", "support", c=0.5), L("timpani", "transient", keyHi=60, c=0.4), L("largechorus", "atmosphere", c=0.6),
  space=dict(decay=4.0, size=0.85, predelay=30), returns=(0.6, 0.5, 0.72))
S("Woodland Overture", "Cinematic", "airy warm gentle", "The woodwind choir with clarinets inside it and bassoons under the low notes",
  L("allwinds", "primary", c=0.45), L("clarinets", "support", -2, c=0.45), L("bassoons", "support", keyHi=59, c=0.45), space=dict(decay=3.0))
S("Flute In The Rain", "Cinematic", "airy singing gentle", "A solo flute, a harp on each note, strings arriving late",
  L("flutesolo", "primary", c=0.5), L("ssoharp", "transient", c=0.45), L("allstr", "atmosphere", att=1.5, c=0.6), space=dict(decay=3.6))
S("Low Brass Menace", "Cinematic", "dark deep powerful", "Tuba, bass trombone, basses and contrabassoon: the bottom of the pit",
  L("tuba", "primary", keyHi=64, c=0.45), L("basstrombone", "support", keyHi=64, c=0.45), L("basses", "support", keyHi=60, c=0.45), L("contrabassoon", "atmosphere", keyHi=60, c=0.4),
  space=dict(decay=3.4, damp=0.6))
S("Thunder Under The Stage", "Cinematic", "deep percussive dark", "Timpani with plucked basses on the front and a gong left ringing",
  L("timpani", "primary", c=0.5), L("bpizz", "transient", c=0.4), L("gong", "atmosphere", c=0.7), space=dict(decay=4.5, size=0.85))
S("Oboe Lament", "Cinematic", "singing moody intimate", "A solo oboe with a cor anglais under it and celli behind",
  L("oboe", "primary", c=0.45), L("coranglais", "support", c=0.45), L("celli", "atmosphere", att=1.0, c=0.6), space=dict(decay=3.2))
S("Staccato Run", "Cinematic", "short powerful bright", "Short brass, short trumpets and short strings together",
  L("allbrass_stac", "primary", c=0.4), L("trumpets_stac", "support", c=0.4), L("allstr_stac", "support", c=0.4), space=dict(decay=2.2, size=0.6))
S("Underscore", "Cinematic", "dark slow tense", "Muted celli and a bass clarinet over a slow synthetic bed",
  L("celli", "primary", lp=2400, att=0.6, c=0.5), L("bassclarinet", "support", c=0.45), L("soundtrack", "atmosphere", c=0.6), space=dict(decay=4.0, damp=0.55))
S("Trumpet Voluntary", "Cinematic", "bright powerful ceremonial", "A solo trumpet over the section and an open diapason",
  L("trumpetsolo", "primary", c=0.5), L("trumpets", "support", c=0.5), L("diapason", "support", -2, c=0.5), space=dict(decay=4.2, size=0.85))

# ============================================================================================ GLOW TIDE: experimental, granular
S("Rings On Dark Water", "Experimental", "dark evolving strange", "A goblin choir of a synth over bowed metal strings",
  L("goblins", "primary", c=0.6, lfo=dict(shape="drift", rate=0.15, pan=0.5)), L("zinc", "support", c=0.4), space=dict(decay=4.6))
S("Scary Lullaby", "Experimental", "dark strange cinematic", "A prepared piano with itself running backwards behind it, through a doorway",
  L("scary", "primary", c=0.5), L("scary", "atmosphere", rev=True, loop=(0.15, 0.9, 0.3), att=1.0, c=0.8), space=("portal", dict(decay=5.0, size=0.7, shape=2)), returns=(0.6, 0.5, 0.55))
S("Didgeri Drone", "Experimental", "deep dark evolving", "A didgeridoo smeared into a drone over basses",
  L("didge", "primary", c=0.5, ins=[fx("spectra", smear=0.5, mix=0.6)]), L("basses", "support", lp=600, keyHi=60, c=0.4), space=dict(decay=4.0))
S("Bent Zither", "Experimental", "strange glassy evolving", "A zither's effects scattered into grains with an echo",
  L("dantranh_fx", "primary", b=0.35, c=0.5, ins=[fx("crystal", mix=0.45, pitch=7, reverse=0.5)]), echo=dict(div=8, fb=0.45), space=dict(decay=3.4))
S("Iron Tide", "Experimental", "harsh dark powerful", "Bowed iron through a hot tube with a science-fiction wind behind",
  L("ironface", "primary", oct=1, c=0.4, ins=[fx("tube", drive=0.6)]), L("scifi", "atmosphere", c=0.6), space=dict(decay=3.4))
S("Bowed Metal", "Experimental", "ethereal slow strange", "Bowed vibraphone and bowed psaltery with a gong swelling backwards",
  L("vibes_bowed", "primary", c=0.55), L("psaltery_bowed", "support", c=0.55), L("gong", "atmosphere", rev=True, loop=(0.2, 0.95, 0.3), att=1.5, c=0.8), space=dict(decay=5.5))
S("Signal From Below", "Experimental", "strange evolving glassy", "A science-fiction pad with a wandering sine and a long echo",
  L("scifi", "primary", c=0.5), L("doc", "support", b=0.5, c=0.4, lfo=dict(shape="drift", rate=0.3, pitch=0.5)), echo=dict(div=10, fb=0.55), space=dict(decay=4.0))
S("Fuzz Garden", "Experimental", "harsh moving strange", "A broken wave through a filter that opens and closes, voices underneath",
  L("fuzzwave", "primary", lp=3000, reso=0.4, c=0.35, lfo=dict(shape="triangle", div=3, cutoff=0.5)), L("ork_pre", "atmosphere", c=0.7), space=dict(decay=2.6))
S("Glow Plankton", "Granular", "glassy evolving ethereal", "Water glasses scattered into grains an octave up, a kalimba running backwards",
  L("glass", "primary", c=0.6, ins=[fx("crystal", mix=0.5, pitch=12, density=0.6, reverse=0.5, spread=0.8)]), L("kalimba", "atmosphere", rev=True, loop=(0.1, 0.9, 0.3), c=0.7), space=dict(decay=5.0))
S("Tide Pool Grains", "Granular", "evolving ethereal strange", "A choir broken into grains a fifth up",
  L("mixedchorus", "primary", c=0.55, ins=[fx("crystal", mix=0.55, pitch=7, texture=0.7, density=0.6)]), L("padchoir", "atmosphere", c=0.6), space=dict(decay=4.6))
S("Shattered Harp", "Granular", "glassy rhythmic strange", "A harp whose notes come back in backwards splinters",
  L("harp", "primary", b=0.3, c=0.4, ins=[fx("crystal", mix=0.5, pitch=12, reverse=1.0, splice=90, fb=0.5)]), echo=dict(div=7, fb=0.4), space=dict(decay=3.4))
S("Grain Of Salt", "Granular", "gentle evolving intimate", "A piano trailed by grains of itself, a bowed pad underneath",
  L("splendid", "primary", c=0.4, ins=[fx("crystal", mix=0.4, pitch=0, delay=400, density=0.7, reverse=0.6)]), L("bowedpad", "atmosphere", att=1.0, c=0.6), space=dict(decay=3.6))
S("Phosphor Dust", "Granular", "bright glassy delicate", "Crystal synth with its grains an octave above and crotales on the front",
  L("crystal", "primary", c=0.55, ins=[fx("crystal", mix=0.5, pitch=12, density=0.7)]), L("crotales", "transient", c=0.5), space=dict(decay=4.2))
S("Frozen Wave", "Granular", "slow ethereal evolving", "A sweeping pad smeared and scattered, in a shimmering hall",
  L("sweep", "primary", c=0.55, ins=[fx("spectra", smear=0.8, scatter=0.4, mix=0.8)]), space=("shimmer", dict(decay=9.0, shimmer=0.4)), returns=(0.6, 0.5, 0.6))
S("Grain Wave Drift", "Granular", "evolving dark strange", "A grainy wave with a hummed vowel behind it, splintering",
  L("grainwave", "primary", c=0.5, ins=[fx("crystal", mix=0.45, pitch=-12, reverse=0.7)]), L("ooh", "atmosphere", c=0.6), space=dict(decay=4.4))

# ============================================================================================ SNOW ORCHARD: ambient, soundscapes
S("The Quiet", "Ambient", "gentle airy slow", "Still air, a slow sweep, a hang drum now and then",
  L("newage", "primary", c=0.7), L("sweep", "atmosphere", att=1.5, c=0.6), L("hang", "transient", b=0.4, c=0.5), space=dict(decay=5.0))
S("First Snow", "Ambient", "glassy slow gentle", "A bowed pad with singing wine glasses over it and a far celeste",
  L("bowedpad", "primary", att=0.8, c=0.6), L("wineglass", "atmosphere", 3, c=0.65), L("celeste", "transient", -6, c=0.8), space=dict(decay=6.0, size=0.85))
S("Orchard Sleep", "Ambient", "warm slow gentle", "Bowed vibraphone, still air and an alto flute",
  L("vibes_bowed", "primary", c=0.6), L("newage", "support", c=0.6), L("altoflute", "atmosphere", att=1.0, c=0.6), space=dict(decay=5.0))
S("White Field", "Ambient", "dark slow wide", "Dark strings that take two seconds to arrive, flutes as breath, a very long hall",
  L("sstr2", "primary", lp=1800, att=2.0, c=0.6), L("flutes", "atmosphere", att=1.5, c=0.7), space=dict(decay=8.0, size=0.9, damp=0.5), returns=(0.6, 0.5, 0.75))
S("Breath Clouds", "Ambient", "airy ethereal slow", "An ocarina breathing in over a choir pad, in a shimmering hall",
  L("ocarina", "primary", att=0.4, c=0.6), L("padchoir", "atmosphere", c=0.6), space=("shimmer", dict(decay=8.0, shimmer=0.35)), returns=(0.6, 0.5, 0.6))
S("Hearth", "Ambient", "warm vintage intimate", "A soft electric piano by the fire, a bowed pad behind it",
  L("wurli", "primary", lp=2400, velSens=0.7, c=0.4, ins=[fx("retro", magic=0.3, noise=0.08, wobble=0.2)]), L("bowedpad", "atmosphere", -3, att=1.0, c=0.6), space=dict(decay=3.4))
S("Snow Bells Far Off", "Ambient", "delicate glassy slow", "A quiet music box and chimes heard from across the orchard",
  L("tubglock_q", "primary", c=0.9), L("chimes", "support", -3, c=0.9), L("cow_pad_lonely", "atmosphere", att=1.5, c=0.6), space=dict(decay=7.0, size=0.9), returns=(0.6, 0.5, 0.8))
S("Blue Hour", "Ambient", "moody slow warm", "A lonely pad with a few celli inside it",
  L("cow_pad_lonely", "primary", c=0.55), L("vsec_celli", "support", att=0.8, c=0.55), space=dict(decay=4.6))
S("Slow Breath Organ", "Ambient", "dark gentle steady", "A soft organ wave arriving slowly, a hummed vowel behind",
  L("softorgan", "primary", att=1.5, lp=1500, c=0.6), L("ooh", "atmosphere", c=0.6), space=dict(decay=5.5))
S("Wind In Bare Branches", "Soundscapes", "airy evolving dark", "A soundtrack bed with high violin harmonics moving through it",
  L("soundtrack", "primary", c=0.6), L("violins1_harm", "atmosphere", c=0.7, lfo=dict(shape="drift", rate=0.2, amp=0.4, pan=0.6)), space=dict(decay=6.0))
S("Goblin Market", "Soundscapes", "strange evolving playful", "Goblin voices, a bell tree behind them, a zither's noises on each note",
  L("goblins", "primary", c=0.6), L("belltree", "atmosphere", c=0.7), L("dantranh_fx", "transient", c=0.5), space=dict(decay=4.6))
S("Under The Ice", "Soundscapes", "dark deep evolving", "A science-fiction wind over muffled basses, through a doorway",
  L("scifi", "primary", c=0.6), L("basses", "support", lp=400, keyHi=60, c=0.5), space=("portal", dict(decay=6.0, size=0.8, shape=2)), returns=(0.6, 0.5, 0.6))
S("Sleeping Machines", "Soundscapes", "dark lo-fi steady", "An old transistor organ humming low, hiss and a far-off wind",
  L("cow_org_lonely", "primary", keyHi=72, lp=900, c=0.5, ins=[fx("retro", magic=0.5, noise=0.2)]), L("scifi", "atmosphere", c=0.7), space=dict(decay=4.6))
S("Distant Procession", "Soundscapes", "dark slow cinematic", "A choir heard from far away, a gong on each note, basses under it",
  L("largechorus", "primary", lp=1200, att=2.0, c=1.0), L("gong", "transient", -5, c=0.8), L("basses", "support", keyHi=60, c=0.6), space=dict(decay=8.0, size=0.95), returns=(0.6, 0.5, 0.85))
S("Marimba Weather", "Soundscapes", "woody evolving strange", "Marimba rolls like rain on a roof, goblin voices in the weather",
  L("marimbarolls", "primary", c=0.5), L("goblins", "atmosphere", c=0.7), space=dict(decay=4.2))

# ============================================================================================ AURORA LAKE: pads, digital synths, hybrids
S("Sky Comes Down", "Pads", "evolving wide ethereal", "A slow soundtrack pad, a science-fiction wind and a lonely pad an octave above",
  L("soundtrack", "primary", c=0.6), L("scifi", "atmosphere", att=0.8, c=0.7), L("cow_pad_lonely", "shimmer", 3, oct=1, a=0.5, c=0.5), space=dict(decay=5.0))
S("Northern Veil", "Pads", "airy wide slow", "A sweeping pad over a string machine with violin harmonics on top",
  L("sweep", "primary", c=0.55), L("sstr2", "support", c=0.5), L("violins1_harm", "shimmer", 5, c=0.6), space=dict(decay=4.6))
S("Green Curtain", "Pads", "warm moving wide", "Still air and a fat pad whose filter moves like a curtain",
  L("newage", "primary", c=0.55), L("cow_pad_fancy", "support", lp=2600, a=0.3, c=0.5, lfo=dict(shape="sine", rate=0.1, cutoff=0.4)), space=dict(decay=4.2))
S("Lake Mirror", "Pads", "glassy slow gentle", "A bowed pad with wine glasses and celli inside it",
  L("bowedpad", "primary", c=0.55), L("wineglass", "atmosphere", c=0.6), L("celli", "support", att=0.8, c=0.5), space=dict(decay=4.6))
S("Slow Light", "Pads", "ethereal slow warm", "A breathing transistor pad with a choir pad behind it, in a shimmering hall",
  L("cow_pad_asth", "primary", c=0.55), L("padchoir", "atmosphere", c=0.6), space=("shimmer", dict(decay=8.0, shimmer=0.35)), returns=(0.6, 0.5, 0.6))
S("Warm Front", "Pads", "warm lush wide", "A string machine with soft synth brass arriving late, in a chorus",
  L("sstr1", "primary", a=0.45, c=0.45), L("sbrass1", "support", att=1.0, lp=1400, a=0.4, c=0.45), chorus=dict(rate=0.3, depth=0.55), space=dict(decay=3.6))
S("Polar Night", "Pads", "dark deep slow", "A dark pad over held basses with a quiet wind",
  L("cow_pad_shifty", "primary", lp=1200, c=0.55), L("basses", "support", keyHi=60, c=0.45), L("scifi", "atmosphere", -3, c=0.7), space=dict(decay=5.0, damp=0.55))
S("Halo", "Pads", "bright ethereal glassy", "A choir pad with crystal light on top and water glasses on each note",
  L("padchoir", "primary", c=0.55), L("crystal", "shimmer", 3, c=0.6), L("glass", "transient", -3, c=0.6), space=dict(decay=4.6))
S("Wide Water", "Pads", "lush wide warm", "A slow saw pad with a sweep behind it and a wheezing pad inside, through an ensemble",
  L("sawpad", "primary", lp=3200, a=0.5, c=0.5), L("sweep", "atmosphere", c=0.6), L("cow_pad_wheezy", "support", a=0.4, c=0.5), chorus=dict(mode=1, depth=0.55), space=dict(decay=4.0))
S("Overtone Sky", "Pads", "glassy evolving wide", "A pad made of overtones, a hand-drawn wave inside it and a hummed vowel behind",
  L("overtone", "primary", c=0.55), L("handdrawn", "support", c=0.5), L("ooh", "atmosphere", c=0.6), space=dict(decay=4.6))
S("Glass FM Dawn", "Digital Synths", "bright glassy gentle", "An FM wave and an FM piano together in a slow chorus",
  L("fmglass", "primary", a=0.4, c=0.4), L("fmp1", "support", a=0.4, c=0.4), chorus=dict(rate=0.3), space=dict(decay=2.8))
S("Bit Lantern", "Digital Synths", "bright playful percussive", "A bit-reduced bell with a game-console lead inside and a sixteenth echo",
  L("bitglass", "primary", b=0.35, c=0.3), L("chip", "support", -4, lp=3000, c=0.3), echo=dict(div=3, fb=0.4, mode=3), space=dict(decay=2.0))
S("Crystal Lattice", "Digital Synths", "glassy bright ethereal", "Crystal synth with an FM bell on the front and an overtone pad behind",
  L("crystal", "primary", c=0.5), L("fmbell", "transient", c=0.5), L("overtone", "atmosphere", c=0.6), space=dict(decay=4.0))
S("TX Morning", "Digital Synths", "bright vintage wide", "Two voices of an eighties FM module in a chorus",
  L("txfm", "primary", a=0.45, c=0.35), L("txpiano", "support", a=0.45, c=0.35), chorus=dict(rate=0.35, depth=0.55), space=dict(decay=2.4))
S("Hand Drawn Sky", "Digital Synths", "evolving wide glassy", "A hand-drawn wave as a pad, an FM wave on the front, in a shimmering hall",
  L("handdrawn", "primary", c=0.5), L("fmglass", "transient", c=0.5), space=("shimmer", dict(decay=7.0, shimmer=0.35)), returns=(0.6, 0.5, 0.55))
S("Sci-Fi Chorale", "Digital Synths", "strange wide evolving", "A science-fiction wind that sings: a vowel wave inside it and a long echo",
  L("scifi", "primary", c=0.5), L("aah", "support", c=0.5), echo=dict(div=10, fb=0.45), space=dict(decay=4.0))
S("Digital Rain", "Digital Synths", "bright rhythmic glassy", "Square plucks falling through a ping-pong echo with bit glass above",
  L("squarepluck", "primary", b=0.5, c=0.3), L("bitglass", "shimmer", 3, oct=1, b=0.5, c=0.4), echo=dict(div=3, fb=0.55, ping=1), space=dict(decay=2.4))
S("Iron Bows", "Hybrid Instruments", "dark powerful cinematic", "Three bowed machines: one in front, one inside it, one far behind",
  L("blackheart_m", "primary", c=0.45), L("ironface", "support", c=0.45), L("zinc", "atmosphere", c=0.6), space=dict(decay=3.6))
S("Aurora Keys", "Hybrid Instruments", "bright wide lush", "An FM piano with a string machine behind it and water glasses on top",
  L("fmp1", "primary", a=0.35, c=0.4), L("sstr2", "atmosphere", att=0.8, a=0.4, c=0.5), L("glass", "shimmer", 2, c=0.55), space=dict(decay=3.2))
S("Cello Circuit", "Hybrid Instruments", "warm moody singing", "A real cello with a cello wave inside it and a wind behind",
  L("cello", "primary", c=0.45), L("cellowave", "support", c=0.45), L("scifi", "atmosphere", -2, c=0.7), space=dict(decay=3.4))
S("Harp Of The North", "Hybrid Instruments", "ringing ethereal wide", "A harp with a bowed pad opening behind it and crystal on top",
  L("harp", "primary", c=0.45), L("bowedpad", "atmosphere", att=0.8, c=0.6), L("crystal", "shimmer", 2, c=0.6), space=dict(decay=4.0))
S("Brass Aurora", "Hybrid Instruments", "warm wide epic", "Real horns with synth brass inside them and a saw pad behind",
  L("horns", "primary", c=0.5), L("sbrass1", "support", a=0.3, c=0.45), L("sawpad", "atmosphere", lp=2400, c=0.6), space=dict(decay=3.8))
S("Voice Machine", "Hybrid Instruments", "strange moving wide", "A choir with a square wave inside it and a mouth that moves in quarter notes",
  L("mixedchorus", "primary", c=0.45, ins=[fx("formant", move=1, mdiv=9, mdepth=0.7)]), L("ssquare", "support", -3, lp=1800, c=0.4), space=dict(decay=3.0))
S("Glass Orchestra", "Hybrid Instruments", "lush glassy cinematic", "Full strings with a vibraphone inside them and water glasses on each note",
  L("allstr", "primary", c=0.5), L("vibes", "support", c=0.5), L("glass", "transient", c=0.55), space=dict(decay=3.8))
S("Clarinet Circuit", "Hybrid Instruments", "warm steady intimate", "A clarinet wave with the real clarinet inside it, in a slow chorus",
  L("clarwave", "primary", att=0.05, lp=3200, a=0.35, c=0.4), L("clarinet", "support", c=0.4), chorus=dict(rate=0.3, depth=0.4), space=dict(decay=2.8))
S("Piano From Elsewhere", "Hybrid Instruments", "ethereal gentle cinematic", "A concert grand with a hummed vowel behind it and an FM bell above",
  L("salamander", "primary", c=0.4), L("ooh", "atmosphere", c=0.6), L("fmbell", "shimmer", 3, c=0.55), space=dict(decay=3.8))

# ============================================================================================ THE SECOND VOYAGE
# Sounds made on the libraries that came aboard in 0.1.6: a 1977 Rhodes, saxophones, guitars, basses, a church organ and
# its glass, choirs and voices, and a handful of single recordings (drones, glass, a far piano).
NEW_FROM = len(SOUNDS)
I.update({
 "rhodes": "jrhodes3d:Rhodes Mark I", "tine": "musescore-general:Tine Piano", "clav": "musescore-general:Clavinet", "accordion": "musescore-general:Accordion",
 "gt_organ": "gtown-church:Grebbestad Church Organ", "gt_mandolin": "gtown-church:Mandolin", "glassharm": "gtown-church:Glass Harmonica", "toyglock": "gtown-church:Toy Glockenspiel",
 "waterbowl": "gtown-church:Water Bowl",
 "weresax": "karoryfer-weresax:Weresax Alto", "saxcordion": "karoryfer-weresax:Saxcordion", "sxnth": "karoryfer-weresax:Sxnth",
 "bearsax": "karoryfer-bear-sax:Bear Sax", "bearduo": "karoryfer-bear-sax:Bear Sax Duo",
 "sopsax": "musescore-general:Soprano Sax", "altosax": "musescore-general:Alto Sax", "tenorsax": "musescore-general:Tenor Sax", "barisax": "musescore-general:Baritone Sax",
 "harmon": "musescore-general:Harmon Mute Trumpet", "whistle": "musescore-general:Whistle", "panflute": "musescore-general:Pan Flute", "shaku": "musescore-general:Shakuhachi",
 "bottle": "musescore-general:Bottle Chiff", "harmonica": "musescore-general:Harmonica",
 "emily": "karoryfer-emilyguitar:Emily Guitar", "emily5": "karoryfer-emilyguitar:Emily Guitar Five Voices", "nylon": "musescore-general:Nylon String Guitar",
 "steel": "musescore-general:Steel String Guitar", "jazzgtr": "musescore-general:Jazz Guitar", "cleangtr": "musescore-general:Clean Guitar", "mutedgtr": "musescore-general:Palm Muted Guitar",
 "twelve": "musescore-general:12-String Guitar", "uke": "musescore-general:Ukulele", "banjo": "musescore-general:Banjo", "sitar": "musescore-general:Sitar", "koto": "musescore-general:Koto",
 "shamisen": "musescore-general:Shamisen", "dulcimer": "musescore-general:Dulcimer", "steeldrums": "musescore-general:Steel Drums",
 "rubner_arco": "dsmolken-double-bass:Rubner Double Bass Arco", "rubner_pizz": "dsmolken-double-bass:Rubner Double Bass Pizzicato",
 "abass": "musescore-general:Acoustic Bass", "fbass": "musescore-general:Fingered Bass", "pbass": "musescore-general:Picked Bass", "fretless": "musescore-general:Fretless Bass",
 "slap": "musescore-general:Slap Bass",
 "choir_aahs": "musescore-general:Choir Aahs", "voice_oohs": "musescore-general:Voice Oohs", "synth_voice": "musescore-general:Synth Voice", "solo_vox": "musescore-general:Solo Vox",
 "space_voice": "musescore-general:Space Voice", "halo": "musescore-general:Halo Pad",
 "fantasia": "musescore-general:Fantasia", "warmpad": "musescore-general:Warm Pad", "bowedglass": "musescore-general:Bowed Glass", "atmos": "musescore-general:Atmosphere",
 "icerain": "musescore-general:Ice Rain", "echodrops": "musescore-general:Echo Drops",
 "choirwash": "sonic-pi-ambient:Choir Wash", "lowdrone": "sonic-pi-ambient:Low Drone", "glasshum": "sonic-pi-ambient:Glass Hum", "glassrub": "sonic-pi-ambient:Glass Rub",
 "hauntedhum": "sonic-pi-ambient:Haunted Hum", "lunar": "sonic-pi-ambient:Lunar Landing", "farpiano": "sonic-pi-ambient:Far Piano", "sauna": "sonic-pi-ambient:Sauna Steam",
 "gtrharm": "sonic-pi-ambient:Guitar Harmonics", "droneg": "sonic-pi-ambient:Drone in G", "struckbell": "sonic-pi-ambient:Struck Bell", "bdpad1": "sonic-pi-ambient:Black Dog Pad 1",
 "bdpad2": "sonic-pi-ambient:Black Dog Pad 2", "voicetone": "sonic-pi-ambient:Black Dog Voice Tone",
})

# ---- Amber Marsh
S("Late Set", "Pianos", "warm intimate vintage", "A grand in a small club, a plucked double bass under the left hand",
  L("splendid", "primary", c=0.22), L("rubner_pizz", "support", keyHi=55, width=0.3, c=0.12), space=dict(decay=1.5, size=0.35))
S("Far Shore Piano", "Pianos", "ethereal slow cinematic", "A concert grand close by, another piano answering from the far shore, glass humming between",
  L("salamander", "primary", c=0.4), L("farpiano", "atmosphere", c=0.7, width=1.5), L("glasshum", "atmosphere", -2, att=0.8, c=0.7), space=dict(decay=4.5, size=0.8))
S("Seventy-Seven", "Keys", "warm vintage intimate", "A 1977 Rhodes straight from the harp, turning slowly from side to side",
  L("rhodes", "primary", a=0.2, c=0.22, lfo=dict(shape="sine", rate=4.6, pan=0.5)), space=dict(decay=1.7, size=0.4))
S("Suitcase At Dusk", "Keys", "warm lush gentle", "The Rhodes in a slow chorus, with a warm pad opening behind it",
  L("rhodes", "primary", a=0.3, c=0.3), L("warmpad", "atmosphere", att=1.0, c=0.6, width=1.5), chorus=dict(rate=0.3, depth=0.45), space=dict(decay=3.0))
S("Clavinet Row", "Keys", "bright percussive vintage", "A clavinet through a warm tube, a muted guitar on the front of each note",
  L("clav", "primary", a=0.2, c=0.15, ins=[fx("tube", drive=0.45)]), L("mutedgtr", "transient", c=0.15), space=dict(decay=1.2, size=0.3))
S("Quayside Accordion", "Keys", "warm vintage moving", "An accordion with a reed organ made of saxophones breathing inside it",
  L("accordion", "primary", c=0.3), L("saxcordion", "support", c=0.35), space=dict(decay=1.9, size=0.45))
S("Grebbestad", "Keys", "wide slow powerful", "The organ of a village church in Sweden, heard from the back of its own nave",
  L("gt_organ", "primary", c=0.6), L("bourdon16", "support", -3, c=0.5), space=dict(decay=5.0, size=0.85, predelay=30), returns=(0.6, 0.5, 0.72))

# ---- Crimson Loom
S("Sxnth", "Analog Synths", "bright wide evolving", "A synthesiser built out of saxophones, with saws opening slowly behind it",
  L("sxnth", "primary", a=0.3, c=0.35), L("sawpad", "atmosphere", lp=2600, c=0.5), space=dict(decay=2.8))
S("Night Alto", "Leads", "warm singing intimate", "An alto saxophone, close, in a small room",
  L("weresax", "primary", c=0.3), space=dict(decay=1.6, size=0.4))
S("Bear On The Bridge", "Leads", "deep warm singing", "A baritone saxophone with a second one a breath behind it",
  L("bearsax", "primary", c=0.3), L("bearduo", "support", -2, width=1.4, c=0.4), space=dict(decay=2.0, size=0.5))
S("Four Reeds", "Leads", "bright wide powerful", "Soprano, alto, tenor and baritone saxophones on one key",
  L("altosax", "primary", c=0.3), L("tenorsax", "support", pan=-0.35, c=0.3), L("barisax", "support", oct=-1, pan=0.3, c=0.3), L("sopsax", "shimmer", 3, oct=1, c=0.35),
  space=dict(decay=2.0, size=0.5))
S("Harmon Blue", "Leads", "moody vintage intimate", "A muted trumpet out front, a Rhodes a long way behind it",
  L("harmon", "primary", b=0.15, c=0.35), L("rhodes", "atmosphere", lp=2400, c=0.6), echo=dict(div=9, fb=0.25), space=dict(decay=2.6))
S("Whistler", "Leads", "bright airy gentle", "Someone whistling, a pan flute breathing under it, a quarter-note echo",
  L("whistle", "primary", b=0.3, c=0.4), L("panflute", "support", c=0.4), echo=dict(div=9, fb=0.35), space=dict(decay=2.8))
S("Emily", "Plucks", "warm intimate gentle", "An acoustic guitar played with the fingers, in a wooden room",
  L("emily", "primary", c=0.25), space=dict(decay=1.6, size=0.4))
S("Steel And Nylon", "Plucks", "bright wide gentle", "Two guitars, nylon in the middle and steel out wide, harmonics above",
  L("nylon", "primary", c=0.3), L("steel", "support", width=1.5, c=0.3), L("gtrharm", "shimmer", c=0.5), space=dict(decay=2.0))
S("Twelve Strings", "Plucks", "bright wide lush", "A twelve-string with five more guitars inside it, in a slow chorus",
  L("twelve", "primary", a=0.4, c=0.3), L("emily5", "support", a=0.3, c=0.3), chorus=dict(rate=0.35, depth=0.5), space=dict(decay=2.2))
S("Porch Strings", "Plucks", "bright percussive vintage", "Banjo and mandolin together, a double bass plucked under them",
  L("banjo", "primary", c=0.2), L("gt_mandolin", "support", pan=0.3, c=0.25), L("rubner_pizz", "support", keyHi=55, width=0.3, c=0.1), space=dict(decay=1.4, size=0.35))
S("Silk Road", "Plucks", "ringing strange wide", "A sitar in front, a koto answering, a hammered dulcimer glinting, a drone under all of it",
  L("sitar", "primary", c=0.35), L("koto", "support", pan=0.35, c=0.4), L("dulcimer", "shimmer", pan=-0.35, c=0.45), L("droneg", "atmosphere", -3, c=0.6), space=dict(decay=3.0))
S("Small Island", "Plucks", "bright gentle warm", "A ukulele with steel drums behind it and a kalimba on top",
  L("uke", "primary", c=0.25), L("steeldrums", "support", c=0.35), L("kalimba", "shimmer", c=0.4), space=dict(decay=1.9))
S("Walking Bass", "Plucks", "deep warm intimate", "A double bass, plucked, with the room it was played in",
  L("rubner_pizz", "primary", keyHi=67, width=0.4, c=0.15), space=dict(decay=1.2, size=0.3), returns=(0.6, 0.5, 0.5))
S("Fretless Evening", "Plucks", "deep warm singing", "A fretless bass in a slow chorus with a sine under it",
  L("fretless", "primary", keyHi=67, width=0.4, a=0.2, c=0.12), L("sine", "support", oct=-1, keyHi=60, width=0.0), chorus=dict(rate=0.4, depth=0.3), returns=(0.6, 0.5, 0.45))

# ---- Blossom Flood
S("Aahs In Blossom", "Choirs", "lush wide slow", "A choir singing aah up close, the full chorus standing far behind it",
  L("choir_aahs", "primary", c=0.5), L("largechorus", "atmosphere", c=0.7, width=1.5), space=dict(decay=4.5, size=0.8))
S("Oohs At Dusk", "Choirs", "dark gentle intimate", "Voices humming ooh, a mixed chorus inside them, one vowel held behind",
  L("voice_oohs", "primary", c=0.45), L("mixedchorus", "support", c=0.5), L("ooh", "atmosphere", c=0.6), space=dict(decay=3.8))
S("Two Choirs", "Choirs", "wide lush cinematic", "Aahs to the left and oohs to the right, strings rising between them",
  L("choir_aahs", "primary", pan=-0.45, c=0.5), L("voice_oohs", "support", pan=0.45, c=0.5), L("allstr", "atmosphere", att=1.5, c=0.6), space=dict(decay=4.2, size=0.75))
S("Cathedral Of Petals", "Choirs", "epic wide slow", "Two choirs, an organ on the floor, string harmonics and glass in the roof",
  L("choir_aahs", "primary", voices=16, c=0.55), L("largechorus", "support", voices=16, c=0.6), L("gt_organ", "support", -4, voices=16, lp=1400, c=0.6),
  L("allstr_harm", "shimmer", voices=16, c=0.65), L("glass", "shimmer", voices=16, velFadeIn=50, c=0.6), space=dict(decay=6.5, size=0.9, predelay=35), returns=(0.6, 0.5, 0.75))
S("Ring Of Voices", "Vocal Pads", "ethereal wide slow", "A ring of voices that opens slowly, space voices inside it, a choir pad behind, in a shimmering hall",
  L("halo", "primary", lp=5200, c=0.55), L("space_voice", "support", lp=4200, c=0.55), L("padchoir", "atmosphere", c=0.6), space=("shimmer", dict(decay=7.0, shimmer=0.3)), returns=(0.6, 0.5, 0.55))
S("Space Voices", "Vocal Pads", "strange wide evolving", "Voices from a long way off, a synth voice under them, a dark pad behind",
  L("space_voice", "primary", c=0.5), L("synth_voice", "support", c=0.5), L("bdpad2", "atmosphere", c=0.6), echo=dict(div=10, fb=0.4), space=dict(decay=4.0))
S("Vox Humana", "Vocal Pads", "warm singing moving", "One voice out front with a choir pad behind it, in a slow chorus",
  L("solo_vox", "primary", a=0.3, c=0.4), L("padchoir", "atmosphere", c=0.55), chorus=dict(rate=0.3, depth=0.45), space=dict(decay=3.2))
S("Choir Machine", "Vocal Pads", "bright wide steady", "A synthetic choir with a string machine inside it and an eeh riding above",
  L("synth_voice", "primary", a=0.3, c=0.45), L("sstr1", "support", a=0.4, c=0.45), L("eeh", "shimmer", c=0.5), space=dict(decay=3.4))

# ---- Lantern Sanctum
S("Water Bowl", "Bells", "ringing dark strange", "A bowl of water, struck and left to ring, glass singing over it",
  L("waterbowl", "primary", c=0.5), L("glassharm", "shimmer", c=0.6), L("glasshum", "atmosphere", -2, c=0.7), space=dict(decay=4.5, size=0.75))
S("Toy Box", "Bells", "bright delicate gentle", "A toy glockenspiel, a kalimba inside it, a celeste on top",
  L("toyglock", "primary", c=0.4), L("kalimba", "support", c=0.4), L("celeste", "shimmer", c=0.5), space=dict(decay=2.4))
S("Steel Lanterns", "Bells", "bright ringing warm", "Steel drums with a marimba under them and a vibraphone ringing behind",
  L("steeldrums", "primary", c=0.35), L("marimba", "support", c=0.35), L("vibes", "atmosphere", c=0.55), space=dict(decay=2.6))
S("One Bell", "Bells", "ringing wide slow", "One struck bell, a tubular bell inside it, bowed glass left behind",
  L("struckbell", "primary", c=0.5), L("tubular", "support", c=0.5), L("bowedglass", "atmosphere", att=0.8, c=0.65), space=dict(decay=5.0, size=0.8))
S("Singing Glass", "Bells", "glassy ethereal slow", "Wine glasses with a glass harmonica inside them, a wet finger on a rim behind",
  L("wineglass", "primary", c=0.5), L("glassharm", "support", c=0.55), L("glassrub", "atmosphere", -2, c=0.65), space=dict(decay=4.5))
S("Breath On Glass", "Vocal Textures", "airy strange gentle", "A voice with no words, glass humming under it, steam in the room",
  L("voicetone", "primary", c=0.5), L("glasshum", "atmosphere", c=0.6), L("sauna", "atmosphere", -4, c=0.5), space=dict(decay=4.0))
S("Choir Wash", "Vocal Textures", "wide ethereal slow", "A sung chord washing in over a held vowel, in a shimmering hall",
  L("choirwash", "primary", c=0.6), L("ooh", "atmosphere", c=0.6), space=("shimmer", dict(decay=7.0, shimmer=0.3)), returns=(0.6, 0.5, 0.55))

# ---- Emerald Opera
S("Rubner Bows", "Strings", "deep warm singing", "One double bass, bowed, with the cellos standing above it",
  L("rubner_arco", "primary", c=0.4), L("celli", "support", c=0.45), space=dict(decay=3.0, size=0.6))
S("Strings And Singers", "Strings", "lush wide cinematic", "Full strings with a choir folded inside them",
  L("allstr", "primary", c=0.5), L("choir_aahs", "support", c=0.55), L("voice_oohs", "atmosphere", c=0.6), space=dict(decay=4.2))
S("Pan Pipes At The Opera", "Cinematic", "airy gentle wide", "Pan pipes out front, flutes inside them, strings far behind",
  L("panflute", "primary", c=0.45), L("flutes", "support", c=0.45), L("allstr", "atmosphere", att=1.4, c=0.6), space=dict(decay=3.6))
S("Shakuhachi Rain", "Cinematic", "airy moody slow", "A shakuhachi with a blown bottle under it and a koto on the front of each note",
  L("shaku", "primary", c=0.5), L("bottle", "support", c=0.5), L("koto", "transient", c=0.5), space=dict(decay=4.2, size=0.75))
S("Sax And Strings", "Cinematic", "warm cinematic singing", "An alto saxophone over a string section, a harp on each note",
  L("weresax", "primary", c=0.4), L("allstr", "atmosphere", att=1.2, c=0.55), L("harp", "transient", c=0.45), space=dict(decay=3.4))

# ---- Glow Tide
S("Lunar Landing", "Experimental", "strange airy evolving", "A radio from the moon, a low drone under it, ice falling through an echo",
  L("lunar", "primary", c=0.5), L("lowdrone", "atmosphere", c=0.6), L("icerain", "shimmer", b=0.4, c=0.5), echo=dict(div=7, fb=0.45, ping=1), space=dict(decay=4.0),
  set_aside=["In tune"])      # a radio's noise: it has no note to be in tune with
S("Wet Finger", "Experimental", "glassy strange slow", "A finger on the rim of a glass, a haunted hum under it, bits of glass above",
  L("glassrub", "primary", c=0.55), L("hauntedhum", "atmosphere", c=0.6), L("bitglass", "shimmer", 2, oct=1, c=0.5), space=dict(decay=4.5))

# ---- Snow Orchard
S("Glass Weather", "Ambient", "glassy slow gentle", "Glass humming, a piano far away, steam",
  L("glasshum", "primary", c=0.6), L("farpiano", "support", c=0.7), L("sauna", "atmosphere", -4, c=0.5), space=("shimmer", dict(decay=7.0, shimmer=0.3)), returns=(0.6, 0.5, 0.55))
S("Black Dog", "Ambient", "dark warm slow", "Two dark pads given by The Black Dog, with a warm one under them",
  L("bdpad2", "primary", c=0.55), L("bdpad1", "support", c=0.55), L("warmpad", "atmosphere", att=1.2, c=0.6), space=dict(decay=4.5))
S("Low Country", "Soundscapes", "dark deep evolving", "Two drones a fifth apart and a wind that comes and goes",
  L("lowdrone", "primary", c=0.55), L("droneg", "support", c=0.55), L("hauntedhum", "atmosphere", c=0.6), space=dict(decay=5.0, size=0.85))

# ---- Aurora Lake
S("Fantasia", "Digital Synths", "bright glassy lush", "The bell-and-voice pad of nineties keyboards, ice falling behind it, in a chorus",
  L("fantasia", "primary", a=0.4, c=0.4), L("icerain", "shimmer", b=0.3, c=0.5), chorus=dict(rate=0.3, depth=0.5), space=dict(decay=3.2))
S("Echo Drops", "Digital Synths", "bright rhythmic glassy", "Drops that answer themselves from side to side, a warm pad holding them",
  L("echodrops", "primary", fine=-16, b=0.5, c=0.35), L("warmpad", "atmosphere", att=1.0, c=0.55), echo=dict(div=7, fb=0.5, ping=1), space=dict(decay=3.0))
S("Slow Front", "Pads", "warm wide slow", "A warm pad, an atmosphere inside it, a string machine behind and a choir pad far off",
  L("warmpad", "primary", a=0.3, c=0.5), L("atmos", "support", c=0.5), L("sstr2", "atmosphere", att=1.0, a=0.4, c=0.55), L("padchoir", "atmosphere", -2, att=1.2, c=0.6), space=dict(decay=4.2))
S("Bowed Glass", "Pads", "glassy ethereal slow", "Glass played with a bow, wine glasses on top, a wet finger on a rim behind",
  L("bowedglass", "primary", c=0.55), L("wineglass", "shimmer", c=0.6), L("glassrub", "atmosphere", -2, c=0.65), space=("shimmer", dict(decay=6.5, shimmer=0.3)), returns=(0.6, 0.5, 0.55))
S("Rhodes Aurora", "Hybrid Instruments", "warm lush wide", "A Rhodes with a choir behind it, a string machine under that, and a bell pad above",
  L("rhodes", "primary", a=0.3, c=0.35), L("choir_aahs", "atmosphere", c=0.6), L("sstr2", "atmosphere", att=0.8, a=0.4, c=0.5), L("fantasia", "shimmer", c=0.55), space=dict(decay=3.4))
S("Guitar From Elsewhere", "Hybrid Instruments", "ethereal gentle cinematic", "An acoustic guitar with a halo of voices behind it, a bowed pad, and harmonics above",
  L("emily", "primary", c=0.4), L("halo", "atmosphere", c=0.6), L("bowedpad", "atmosphere", att=0.9, c=0.6), L("gtrharm", "shimmer", c=0.55), space=dict(decay=3.8))
S("Sax Machine", "Hybrid Instruments", "warm strange singing", "A real saxophone with the saxophone synth inside it and a saw pad behind",
  L("weresax", "primary", c=0.4), L("sxnth", "support", -3, c=0.45), L("sawpad", "atmosphere", lp=2400, c=0.55), space=dict(decay=3.0))
# (eight layers at once: each is held to twelve voices, so that fast playing costs what a five-layer sound does)
S("Eight Lanterns", "Hybrid Instruments", "lush epic cinematic", "All eight layers alight: grand and Rhodes, choir and strings, harp and glass, a sine on the floor and bowed glass in the roof",
  L("salamander", "primary", voices=16, c=0.4), L("rhodes", "support", voices=12, a=0.3, c=0.4), L("choir_aahs", "atmosphere", voices=12, c=0.6),
  L("allstr", "atmosphere", voices=12, att=1.4, c=0.6), L("harp", "transient", voices=12, c=0.45), L("glass", "shimmer", voices=12, velFadeIn=45, c=0.55),
  L("sine", "support", -4, voices=8, oct=-1, keyHi=55, width=0.0), L("bowedglass", "atmosphere", voices=12, att=1.5, c=0.65), space=dict(decay=4.2, size=0.75))

# ============================================================================================ THE THIRD VOYAGE: SPECTRAL FX
# Sounds made round SPECTRAL FX (0.1.12), at least one for each world; all carry the tag "spectral", so typing that word
# in the search box finds them. Where the rack sits, and why:
#   - on a send, when the sound is struck and played by hand: the note itself answers at once, and what the rack makes of
#     it follows a window later (the piano's cloud, the bells' answer, the two combs that climb and sink);
#   - on the master, when the sound is a pad, a choir, strings or weather, which nobody plays to the millisecond;
#   - on a layer, for WARP CORE, which takes no time at all.
# A rack on a send is given the whole of that send (all of what it makes comes back; the layers' own sound is the dry).
NEW_FROM = len(SOUNDS)
RACK_END = [fx("ott", mix=0.35, on=False), fx("lim")]        # after a rack on the master: the squeeze (off) and the limiter, as everywhere

def hall(decay, mix, size=85, damping=50):
    """The rack's own reverb, as a hall, last in its row."""
    return ("Reverb", {"Space": "Hall", "Decay": decay, "Size": size, "Pre-delay": 0, "Damping": damping, "Mix": mix})

# ---- Amber Marsh
S("Held Breath", "Pianos", "ethereal slow intimate spectral", "A grand piano, and behind it everything it has just played, smeared into a cloud that hangs for seconds",
  L("salamander", "primary", b=0.8, c=0.22),
  echo=rack(("Temporal Smear", {"Amount": 70, "Length": 16}), ("Spectral Bloom", {"Amount": 25, "Spread": 200}), hall(8, 70)),
  space=dict(decay=2.6, size=0.55), returns=(0.6, 0.8, 0.6))
S("Slow Descent", "Keys", "warm vintage moving spectral", "A Rhodes with a comb sinking through it, slowly and for ever, never reaching the bottom",
  L("rhodes", "primary", a=1.0, c=0.3),
  chorus=rack(("Frequency Shift", {"Shift": -0.12}), window=256), space=dict(decay=2.8), returns=(0.7, 0.5, 0.6))

# ---- Crimson Loom
S("Endless Stair", "Analog Synths", "bright wide moving spectral", "Five saws and a square under them, with a comb climbing through the chord that never reaches the top",
  L("sawstack", "primary", lp=5200, a=1.0, c=0.35), L("squarestack", "support", oct=-1, lp=1600, a=0.6, c=0.2),
  chorus=rack(("Frequency Shift", {"Shift": 0.25}), window=256), space=dict(decay=2.6), returns=(0.8, 0.5, 0.7))

# ---- Blossom Flood
S("Hymn In Fog", "Choirs", "ethereal slow lush spectral", "A full choir with its edges smeared away and a breath spread round every voice, in a long hall",
  L("largechorus", "primary", c=0.4), L("voice_oohs", "support", c=0.4),
  master=[rack(("Temporal Smear", {"Amount": 60, "Length": 12}), ("Spectral Bloom", {"Amount": 35, "Spread": 300}), hall(6, 35))] + RACK_END,
  space=dict(decay=4.2, size=0.75))

# ---- Lantern Sanctum
S("Second Strike", "Bells", "ringing glassy strange spectral", "Tubular bells, each stroke answered by its own overtones arriving one after another, from the lowest up",
  L("tubular", "primary", b=0.9, c=0.35), L("glock", "shimmer", 2, velFadeIn=50, b=0.7, c=0.4),
  echo=rack(("Spectral Dispersion", {"Amount": 55, "Curve": 0}), hall(5, 45)),
  space=dict(decay=3.4, size=0.65), returns=(0.6, 1.0, 0.65))
S("Turned Bell", "Bells", "ringing strange bright spectral", "Orchestral chimes with the top of every stroke turned upside down: a bell that was never cast",
  L("chimes", "primary", c=0.45, ins=[rack(warp={"Warp": 6, "Freq High": 18000, "Dry/Wet": 0.75})]),      # (all of it turned over rings too sharply: 16.8 dB)
  space=dict(decay=3.6, size=0.7))

# ---- Emerald Opera
S("Unfolding", "Strings", "cinematic slow lush spectral", "A string section whose notes open from the bottom up, every overtone arriving a little after the one below it",
  L("allstr", "primary", att=0.3, c=0.45), L("basses", "support", -3, keyHi=55, c=0.3),
  master=[rack(("Spectral Dispersion", {"Amount": 40, "Curve": 0}))] + RACK_END, space=dict(decay=3.6, size=0.7))

# ---- Glow Tide
S("Corrupted Tide", "Experimental", "strange harsh evolving spectral", "A science-fiction pad saved as a picture too many times: blocks, pieces thrown out of place, and a slow drift",
  L("scifi", "primary", c=0.4), L("goblins", "atmosphere", -2, att=0.8, c=0.5),
  # (Bin Shuffle moves different pieces every frame. Bin Teleport moves the same ones every time, and leaves a whistle: 23 dB)
  master=[rack(("Spectral Pixelation", {"Amount": 55, "Block Size": 18}), ("Bin Shuffle", {"Amount": 0.05}), ("Frequency Drift", {"Rate": 0.2, "Depth": 2.5}))] + RACK_END,
  space=dict(decay=3.4))
S("Wrong Rain", "Granular", "glassy strange rhythmic spectral", "Ice rain and its echoes, caught and held a moment at a time, with holes where the sound drops out",
  L("icerain", "primary", c=0.45), L("echodrops", "support", -2, c=0.4),
  master=[rack(("Frame Hold", {"Amount": 80, "Rate": 14, "Length": 7}), ("Spectral Dropout", {"Amount": 40, "Density": 40}), chain=0.8)] + RACK_END,
  space=dict(decay=3.0))

# ---- Snow Orchard
S("White Out", "Ambient", "slow ethereal airy spectral", "A warm pad, a glass hum and a far choir with their edges blown away until one long cloud is left",
  L("warmpad", "primary", att=1.0, c=0.5), L("glasshum", "atmosphere", c=0.55), L("choirwash", "atmosphere", -2, c=0.6),
  master=[rack(("Temporal Smear", {"Amount": 75, "Length": 32}), ("Spectral Bloom", {"Amount": 30, "Spread": 420}), hall(9, 40, 90))] + RACK_END,
  space=dict(decay=5.5, size=0.85))

# ---- Aurora Lake
S("Northern Mirror", "Pads", "glassy ethereal strange spectral", "A saw pad and a bell pad, and among them their own overtones turned upside down round the middle of the keyboard",
  L("sawpad", "primary", lp=3800, c=0.45), L("fantasia", "shimmer", c=0.5),
  master=[rack(("Spectral Mirror", {"Amount": 30, "Mirror Centre": 1100}), ("Spectral Bloom", {"Amount": 25, "Spread": 200}))] + RACK_END, space=dict(decay=3.8))

# ============================================================================================ THE FOURTH VOYAGE: NEON (EDM)
# Ten sounds in the manner of the big commercial ROMplers: every one built from parts with a job each (a body, an edge on
# the front of the note, width, air, a floor), most of them from Surge XT's own patches played into multisampled
# instruments (surge-xt-renders), with Meta's piano and the Stargate pack's noise and soundscapes where they help. All
# carry the tag "edm", so typing that word in the search box lists them. The gated and echoing ones are written for a
# host that is playing: the gate and the echo follow its tempo.
NEW_FROM = len(SOUNDS)
AIR = dict(hp=6500, width=1.9, loop=(0.15, 0.85, 0.3))       # noise as air: only its top, wide, looped so it never runs out

# ---- a supersaw lead
S("Neon Horizon", "Leads", "bright wide powerful edm", "The euphoric trance lead: a wall of detuned saws, a second stack a few cents away on each side, the octave on top, a pluck on the front of every note and air around it",
  L("sx_trance", "primary", c=0.22, b=0.3),
  L("sx_huge", "support", -2, fine=-9, pan=-0.3, lp=9000, c=0.22, b=0.3),
  L("sx_huge", "support", -2, fine=9, pan=0.3, lp=9000, c=0.22, b=0.3),
  L("sx_octsaw", "shimmer", lp=11000, width=1.5, c=0.3),
  L("sx_tpluck", "transient", hp=300, c=0.15),
  L("pinknoise", "atmosphere", -8, c=0.3, **AIR),
  chorus=dict(rate=0.25, depth=0.35), echo=dict(div=7, fb=0.32, ping=1, highcut=7000), space=dict(mode=2, decay=2.4, size=0.6, predelay=20),
  returns=(0.5, 0.45, 0.55), squeeze=0.3)

# ---- a trance pluck
S("Afterglow Pluck", "Plucks", "bright rhythmic short edm", "The trance pluck: a tight saw pluck with a brighter one inside it, a glass bell an octave up and a dotted echo throwing it from side to side",
  L("sx_tpluck", "primary", b=0.45, c=0.22),
  L("sx_sawpluck", "support", -2, width=1.4, b=0.4, c=0.22),
  L("sx_glassbell", "shimmer", oct=1, velFadeIn=40, b=0.5, c=0.3),
  L("sx_pluck", "transient", hp=500, c=0.15),
  echo=dict(div=7, fb=0.42, ping=1, highcut=6500), space=dict(mode=2, decay=1.9, size=0.5),
  returns=(0.5, 0.6, 0.55), squeeze=0.25)

# ---- a cinematic pad
S("Cathedral Of Light", "Pads", "lush wide cinematic edm", "A wide analog pad opening slowly over a warm poly pad, violins swelling in behind, string harmonics high above, in a shimmering hall",
  L("sx_widepad", "primary", att=0.6, c=0.45),
  L("sx_warmpad", "support", att=0.8, lp=4000, c=0.45),
  L("vsec_violins", "atmosphere", att=1.6, rel=1.5, c=0.6),
  L("sx_growth", "atmosphere", -2, att=1.2, lfo=dict(shape="drift", rate=0.15, pan=0.4), c=0.6),
  L("allstr_harm", "shimmer", att=1.8, c=0.65),
  chorus=dict(rate=0.2, depth=0.4), space=("shimmer", dict(decay=7.5, shimmer=0.35, size=0.8)),
  returns=(0.5, 0.5, 0.6))

# ---- an electric piano hybrid
S("Glass Tine House", "Keys", "warm bright lush edm", "The house keyboard: an FM tine piano with a real upright under it, a Rhodes for warmth, a bell on the hard notes and a soft pad far behind",
  L("sx_dxep", "primary", c=0.3, a=0.25),
  L("metapiano", "support", -3, lp=6000, c=0.3),
  L("rhodes", "support", -4, lp=3500, a=0.3, c=0.3),
  L("sx_fmbell", "shimmer", velFadeIn=70, c=0.35),
  L("sx_warmpad", "atmosphere", -3, att=0.8, lp=2200, c=0.5),
  chorus=dict(rate=0.35, depth=0.45), echo=dict(div=9, fb=0.25, ping=1), space=dict(decay=2.4, size=0.55),
  returns=(0.6, 0.4, 0.55))

# ---- an FM bell
S("Crystal Signal", "Bells", "glassy bright ringing edm", "Two FM bells struck together, a glass pluck on the front of each, a celeste above and a computer pad humming far behind, in a long shimmer",
  L("sx_fmbell", "primary", b=0.35, c=0.35),
  L("sx_glassbell", "support", -2, width=1.4, b=0.35, c=0.35),
  L("celeste", "shimmer", oct=1, velFadeIn=50, c=0.4),
  L("sx_computer", "atmosphere", -3, att=1.0, lp=3000, c=0.55),
  echo=dict(div=9, fb=0.3, ping=1, highcut=8000), space=("shimmer", dict(decay=6.0, shimmer=0.3)),
  returns=(0.6, 0.55, 0.55))

# ---- an analog bass
S("Iron Floor", "Analog Synths", "deep dark powerful edm", "A round analog bass with a pure sub held in the middle under it and a saw pluck giving every note its bite",
  L("sx_bass", "primary", keyHi=72, width=0.6, c=0.05, ins=[fx("tube", drive=0.35)]),
  L("sx_sub", "support", keyHi=67, width=0.0, lp=180, c=0.0),
  L("sx_sawpluck", "transient", -3, hp=700, keyHi=72, width=0.8, c=0.05),
  space=dict(mode=0, decay=0.8, size=0.3), returns=(0.6, 0.5, 0.3), squeeze=0.35)

# ---- an evolving atmosphere
S("Slow Aurora", "Ambient", "evolving wide ethereal edm", "A pad that grows by itself, a prime-number texture turning inside it, a haunted synth soundscape and noise drifting round the room",
  L("sx_growth", "primary", att=1.0, c=0.55),
  L("sx_primes", "support", att=1.2, lfo=dict(shape="drift", rate=0.1, pan=0.5, cutoff=0.3), lp=5000, c=0.55),
  L("spookscape", "atmosphere", -2, lp=6000, c=0.65),
  L("sx_computer", "shimmer", oct=1, att=2.0, lp=9000, c=0.6),
  L("pinknoise", "atmosphere", -10, lfo=dict(shape="sine", rate=0.07, pan=0.8, amp=0.5), c=0.5, **AIR),
  chorus=dict(rate=0.15, depth=0.5), space=("shimmer", dict(decay=9.0, shimmer=0.4, size=0.85)),
  returns=(0.5, 0.5, 0.65))

# ---- an orchestral hybrid
S("Brass And Machine", "Hybrid Instruments", "epic cinematic powerful edm", "A full string section and the horns doubled by a synth stack and a saw-teeth pad: the orchestra and the synthesiser playing one part",
  L("allstr", "primary", c=0.5),
  L("horns", "support", -2, keyLo=40, keyHi=76, c=0.5),
  L("sx_jupiter", "support", -3, lp=3200, c=0.45),
  L("sx_teethpad", "atmosphere", att=0.8, lp=2600, c=0.55),
  L("timpani", "transient", -4, keyHi=55, c=0.4),
  space=dict(decay=3.6, size=0.75, predelay=25), returns=(0.6, 0.5, 0.65), squeeze=0.2)

# ---- a digital texture
S("Data Rain", "Digital Synths", "glassy strange rhythmic edm", "A prime-number digital texture and a computer pad, a glass pluck on each note, splinters of it played back reversed an octave up",
  L("sx_primes", "primary", c=0.4),
  L("sx_computer", "support", -2, width=1.5, c=0.45),
  L("sx_glassbell", "transient", -2, b=0.5, c=0.35, ins=[fx("crystal", mix=0.35, pitch=12, splice=120, delay=180, fb=0.35, reverse=0.7)]),
  L("bitglass", "shimmer", oct=1, c=0.4),
  echo=dict(div=3, fb=0.35, ping=1, mode=3), space=dict(decay=3.0, size=0.6),
  returns=(0.5, 0.5, 0.55))

# ---- a rhythmic gated sound
S("Pulse Engine", "Analog Synths", "rhythmic bright powerful edm", "A big analog stack and a saw wall chopped by the trance gate in time with the song, a sub under the gate and a dotted echo filling the gaps",
  L("sx_jupiter", "primary", c=0.25, b=0.35, ins=[fx("gate", pattern=4, smooth=0.6)]),
  L("sx_huge", "support", -2, width=1.5, c=0.25, b=0.35, ins=[fx("gate", pattern=4, smooth=0.6)]),
  L("sx_sub", "support", -4, keyHi=60, width=0.0, lp=160, ins=[fx("gate", pattern=4, smooth=0.6)]),
  echo=dict(div=7, fb=0.4, ping=1, highcut=6500), space=dict(mode=2, decay=2.2, size=0.55),
  returns=(0.5, 0.55, 0.55), squeeze=0.3,
  set_aside=["No loop seams"])      # the gate's own edges, rounded (smooth 0.6), read as seams: the cut is the sound

# ============================================================================================ THE FIFTH VOYAGE: NEON (EDM), SET 2
# Ten more in the same manner, from the second set of synth renders (OB-Xd, Dexed, Vaporizer2, Odin 2) and Caveman
# Cosmonaut's own presets. Tagged "edm" like the fourth voyage.
S("Big Room Anthem", "Leads", "bright wide powerful edm", "The festival lead: a fat EDM saw lead over a hypersaw wall, Odin's lead a few cents off on each side, a big-room pluck on every note and air around it",
  L("s2_vz_fat_edm_lead", "primary", c=0.22, b=0.3),
  L("s2_vz_hypersaw", "support", -2, width=1.6, lp=10000, c=0.22, b=0.3),
  L("s2_odin_edm_projektor", "support", -3, fine=-7, pan=-0.25, c=0.22),
  L("s2_vz_big_room_pluck", "transient", hp=400, c=0.15),
  L("pinknoise", "atmosphere", -9, c=0.3, **AIR),
  chorus=dict(rate=0.25, depth=0.3), echo=dict(div=7, fb=0.3, ping=1, highcut=7000), space=dict(mode=2, decay=2.2, size=0.6),
  returns=(0.5, 0.45, 0.55), squeeze=0.3)
S("Future Glow", "Plucks", "bright glassy rhythmic edm", "The future-bass pluck: a glassy dance pluck with a silver one inside it and a DX7 glass bell an octave up, thrown across the room by a dotted echo",
  L("s2_vz_glassy_dance_pluck", "primary", b=0.45, c=0.25),
  L("s2_vz_silver_pluck", "support", -2, width=1.4, b=0.4, c=0.25),
  L("s2_dx_glass_bell", "shimmer", oct=1, velFadeIn=40, b=0.5, c=0.3),
  echo=dict(div=7, fb=0.42, ping=1, highcut=7000), space=dict(mode=2, decay=2.0, size=0.55),
  returns=(0.5, 0.6, 0.55), squeeze=0.25)
S("Juno Skyline", "Pads", "lush warm wide edm", "A Juno-style dream pad over a big Oberheim pad, Odin's synthwave pad behind and a shimmering pad high above, in a long hall",
  L("s2_vz_juno_dream_pad", "primary", att=0.4, c=0.45),
  L("s2_ob_big_pad", "support", att=0.6, lp=5000, c=0.45),
  L("s2_odin_synthwave_pad", "atmosphere", att=1.0, c=0.55),
  L("s2_vz_shimmer_pad", "shimmer", att=1.5, c=0.6),
  chorus=dict(rate=0.2, depth=0.35), space=("shimmer", dict(decay=7.0, shimmer=0.3, size=0.8)), returns=(0.5, 0.5, 0.6))
S("Ninety-Two Rhodes", "Keys", "warm bright lush edm", "The nineties house electric piano: a DX7 Rhodes with a second DX7 tine inside it, an Oberheim Rhodes under it for warmth, in a chorus",
  L("s2_dx_rhodos", "primary", c=0.3, a=0.3),
  L("s2_dx_dl_rhodes", "support", -2, c=0.3, a=0.3),
  L("s2_ob_rhodes", "support", -4, lp=3000, c=0.3),
  L("s2_dx_e_piano_13", "transient", -3, hp=1200, c=0.25),
  chorus=dict(rate=0.4, depth=0.5), echo=dict(div=9, fb=0.22, ping=1), space=dict(decay=2.0, size=0.5), returns=(0.6, 0.4, 0.55))
S("Outrun Night", "Analog Synths", "deep warm rhythmic edm", "The synthwave bass: an Oberheim outrun bass with Odin's deep bass holding the floor and a synthwave stab on the front of every note",
  L("s2_ob_outrun_bass", "primary", keyHi=72, width=0.6, c=0.05),
  L("s2_odin_deep_bass", "support", -2, keyHi=67, width=0.0, lp=220, c=0.0),
  L("s2_ob_synthwave_stab", "transient", -4, hp=600, keyHi=72, c=0.05),
  space=dict(mode=0, decay=0.8, size=0.3), returns=(0.6, 0.5, 0.3), squeeze=0.3)
S("Jump Start Brass", "Analog Synths", "bright powerful vintage edm", "The eighties synth-brass stab: an Oberheim brass with a Vaporizer brass and Odin's synth brass inside it, a Blade Runner brass pad far behind",
  L("s2_ob_jump_brass", "primary", c=0.25),
  L("s2_vz_clean_brass", "support", -2, pan=-0.25, c=0.25),
  L("s2_odin_synth_brass", "support", -2, pan=0.25, c=0.25),
  L("s2_ob_blade_brass", "atmosphere", -3, att=0.6, c=0.5),
  chorus=dict(rate=0.3, depth=0.3), space=dict(decay=2.4, size=0.6), returns=(0.5, 0.5, 0.55), squeeze=0.25)
S("Tine Cathedral", "Bells", "glassy ringing ethereal edm", "DX7 tine chimes struck with FM chimes inside them, Odin's ethereal bells above and Vaporizer's bells on the front of each stroke, in a shimmering hall",
  L("s2_dx_tine_chimes", "primary", b=0.35, c=0.35),
  L("s2_dx_fm_chimes", "support", -2, width=1.4, c=0.35),
  L("s2_odin_ethereal_bells", "shimmer", oct=1, velFadeIn=50, c=0.4),
  L("s2_vz_bells", "transient", -3, hp=1500, c=0.3),
  echo=dict(div=9, fb=0.3, ping=1, highcut=8000), space=("shimmer", dict(decay=6.5, shimmer=0.3)), returns=(0.6, 0.55, 0.55))
S("Eden Drift", "Ambient", "evolving wide ethereal edm", "A Vaporizer soundscape that changes as it is held, Odin's floating dreams inside it, an FM sphere turning behind and a haunted texture far off",
  L("s2_vz_shores_of_eden", "primary", att=0.8, c=0.55),
  L("s2_odin_floating_dreams", "support", att=1.0, c=0.55),
  L("s2_odin_fm_sphere", "atmosphere", -2, lfo=dict(shape="drift", rate=0.1, pan=0.5), c=0.6),
  L("whatsinthere", "atmosphere", -6, lp=5000, c=0.6),
  chorus=dict(rate=0.15, depth=0.45), space=("shimmer", dict(decay=9.0, shimmer=0.35, size=0.85)), returns=(0.5, 0.5, 0.65))
S("Cave Rave", "Leads", "bright wide strange edm", "A supersaw made from a 1983 transistor organ, a layered Vaporizer lead inside it and an eighties Oberheim lead on top",
  L("cave_saws", "primary", c=0.22, b=0.3),
  L("s2_vz_layer_lead", "support", -3, lp=9000, c=0.22, b=0.3),
  L("s2_ob_super_80s_lead", "shimmer", oct=1, lp=10000, c=0.3),
  chorus=dict(rate=0.3, depth=0.35), echo=dict(div=7, fb=0.3, ping=1, highcut=7000), space=dict(mode=2, decay=2.2, size=0.55),
  returns=(0.5, 0.45, 0.55), squeeze=0.25)
S("Techno Pressure", "Analog Synths", "deep dark rhythmic edm", "The melodic-techno bass: a Vaporizer bass with Odin's growling FM bass under it and a DX7 FM bass giving each note its click",
  L("s2_vz_melodic_techno_bass", "primary", keyHi=72, width=0.5, c=0.05),
  L("s2_odin_growly_fm_bass", "support", -3, keyHi=67, width=0.0, lp=400, c=0.0),
  L("s2_dx_fm_bass", "transient", -4, hp=900, keyHi=72, c=0.05),
  space=dict(mode=0, decay=0.8, size=0.3), returns=(0.6, 0.5, 0.3), squeeze=0.35)

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "presets", "factory-drafts.json")
    names = [s["name"] for s in SOUNDS]
    twice = sorted({n for n in names if names.count(n) > 1})
    if twice: sys.exit("a name is used twice: " + ", ".join(twice))
    if len(sys.argv) > 1:      # every instrument a sound names must be in the library listing
        text = open(sys.argv[1], encoding="utf-8").read()
        if text.lstrip().startswith("["):
            rows = json.load(open(sys.argv[1], encoding="utf-8"))
        else:
            lines = text.split("\n"); start = [i for i, l in enumerate(lines) if l.startswith("id\t")][0]
            rows = [r for r in csv.DictReader(lines[start:], delimiter="\t") if r.get("name")]
        have = {r["id"].split(":")[0] + ":" + r["name"] for r in rows}
        missing = sorted({v for v in I.values() if v not in have})
        if missing: sys.exit("not in the library: " + "; ".join(missing))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(SOUNDS, open(out, "w", encoding="utf-8"), indent=1)
    # (the sounds of the latest voyage on their own as well, so they can be polished without playing the others again)
    # (every sound the bank does not hold yet: a sound that did not pass last time is tried again with the new ones)
    bank = os.path.join(here, "..", "assets", "data", "factory.json")
    shipped = {s["name"] for s in json.load(open(bank, encoding="utf-8"))} if os.path.exists(bank) else set()
    new = [s for s in SOUNDS[NEW_FROM:] if s["name"] not in shipped] if not shipped else [s for s in SOUNDS if s["name"] not in shipped]
    json.dump(new, open(os.path.join(os.path.dirname(out), "factory-drafts-new.json"), "w", encoding="utf-8"), indent=1)
    print(len(new), "not in the bank yet:", ", ".join(s["name"] for s in new))
    by = {}
    for s in SOUNDS: by[s["category"]] = by.get(s["category"], 0) + 1
    print(len(SOUNDS), "sounds:", ", ".join(f"{k} {v}" for k, v in sorted(by.items())))

if __name__ == "__main__":
    main()
