#!/usr/bin/env python3
"""Ghosty Pro's looks as Vital skins: GHOST (liquid lavender, the default), BLACK, PURPLE, LIGHT, and SKETCH (2.0.15: the
hand-drawn watercolor face: rice paper, graphite, soft muted washes; switched on by its own button).

  make_skins.py <vital default.vitalskin> <out dir>

Starts from Vital's own default skin (same keys, same section overrides), moves every colour into the look's palette
(neutrals become the look's surfaces / ink, accents keep their hue and are retuned for the background), then sets the
Ghosty Pro colours for the main surfaces, knobs (white face, violet arc and hand) and wave lines (pink -> violet), and
rounder corners. Vital's skin system draws it all; no drawing code changes."""
import colorsys, copy, json, os, sys

src, out = sys.argv[1], sys.argv[2]
BASE = json.load(open(src))
os.makedirs(out, exist_ok=True)

def parse(c):
    c = c.rjust(8, "0") if len(c) <= 8 else c
    return int(c[0:2], 16), int(c[2:4], 16) / 255, int(c[4:6], 16) / 255, int(c[6:8], 16) / 255
def fmt(a, r, g, b): return "%02x%02x%02x%02x" % (a, round(r * 255), round(g * 255), round(b * 255))

def transform(c, light, tint_h, tint_s, surface_l, ink_l, accent_v, accent_s):
    a, r, g, b = parse(c)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    hv, sv, vv = colorsys.rgb_to_hsv(r, g, b)
    if sv < 0.3 or vv < 0.35:   # a neutral (or any very dark colour: Vital's surfaces are slightly tinted): dark ones are surfaces, light ones are ink (swapped on a light look)
        if light:
            nl = surface_l + (1 - surface_l) * (0.35 - min(l, 0.35)) / 0.35 * 0.6 if l < 0.5 else ink_l + (1 - l) * 0.35
        else:
            nl = surface_l + l * (1 - surface_l) * 0.85 if l < 0.5 else l
        nr, ng, nb = colorsys.hls_to_rgb(tint_h, nl, tint_s if l < 0.5 or not light else 0.25)
        return fmt(a, nr, ng, nb)
    nr, ng, nb = colorsys.hsv_to_rgb(hv, min(1.0, sv * accent_s), min(1.0, vv * accent_v))   # an accent: same hue
    return fmt(a, nr, ng, nb)

def is_colour(k, v):
    return isinstance(v, str) and all(ch in "0123456789abcdefABCDEF" for ch in v) and len(v) <= 8

LOOKS = {
    # name: (light?, tint hue, tint sat, surface lightness, ink lightness, accent value x, accent sat x, hand-picked)
    # GHOST: Brandon's target look (docs/design/ghostypro-target-look.png): near-black rounded panels with thin neon outlines,
    # neon green for what is active / primary (envelopes, LFOs, filters, knob arcs), purple for the oscillators and keys
    "GHOST": (False, 0.33, 0.12, 0.035, 0.94, 1.0, 1.05, {
        "Background": "ff050607", "Body": "ff0b0d0f", "Body Heading Background": "ff0e1113", "Body Text": "ffe6efe2",
        "Heading Text": "ff7cff3a", "Border": "5c4cd91a", "Label Background": "ff121518", "Label Connection": "00000000",
        "Popup Background": "ff0b0d0f", "Popup Border": "ff2f6b17", "Popup Selector Background": "ff111417", "Preset Text": "ffffffff",
        "Text Component Background": "ff121518", "Text Component Text": "ffdfe8da", "Text Editor Background": "ff0e1113",
        "Text Editor Border": "ff2f6b17", "Text Editor Caret": "ff6cff2a", "Text Editor Selection": "556cff2a",
        "Rotary Body": "ff15181b", "Rotary Body Border": "ff2a3326", "Rotary Hand": "ffeaffdf", "Rotary Arc": "ff6cff2a",
        "Rotary Arc Unselected": "ff1f281b", "Rotary Arc Disabled": "ff3a4635", "Rotary Arc Unselected Disabled": "ff171c15",
        "Linear Slider": "ff6cff2a", "Linear Slider Unselected": "ff1c2418", "Linear Slider Thumb": "ffeaffdf",
        "Linear Slider Disabled": "ff3a4635", "Linear Slider Thumb Disabled": "ff5a6655",
        "Widget Background": "ff07090a", "Widget Primary 1": "ff6cff2a", "Widget Primary 2": "ff3fcf14", "Widget Primary Disabled": "ff3a4635",
        "Widget Secondary 1": "786cff2a", "Widget Secondary 2": "183fcf14", "Widget Secondary Disabled": "203a4635",
        "Widget Accent 1": "bbeaffdf", "Widget Accent 2": "14eaffdf", "Widget Center Line": "ff6cff2a",
        "Power Button On": "ff6cff2a", "Power Button Off": "ff3a4040", "Icon Button Off": "ff7d877a", "Icon Button Off Hover": "ffb9c4b5",
        "Icon Button Off Pressed": "ff7d877a", "Icon Button On": "ff6cff2a", "Icon Button On Hover": "ff9dff6a", "Icon Button On Pressed": "ff4fd61a",
        "Icon Selector Icon": "ff7d877a", "UI Button": "ff121518", "UI Button Hover": "ff181d1a", "UI Button Press": "ff0e1113",
        "UI Button Text": "ffdfe8da", "UI Action Button": "ff6cff2a", "UI Action Button Hover": "ff9dff6a", "UI Action Button Press": "ff4fd61a",
        "Modulation Button Selected": "ff14181a", "Modulation Button Unselected": "ff0e1113", "Modulation Button Dragging": "ffc04dff",
        "Modulation Meter": "ff6cff2a", "Modulation Meter Control": "ff9dff6a", "Modulation Meter Left": "ff6cff2a", "Modulation Meter Right": "ffc04dff",
        "Shadow": "aa000000", "Overlay Screen": "aa050607", "Lighten Screen": "14ffffff"}),
    "BLACK": (False, 0.75, 0.35, 0.05, 0.92, 1.0, 1.0, {
        "Background": "ff07040d", "Body": "ff110a1b", "Body Heading Background": "ff160e23", "Body Text": "fff4eefc", "Heading Text": "ffa77bff",
        "Rotary Body": "ff1b1129", "Rotary Body Border": "ff3a2a55", "Rotary Hand": "ffff2bd6", "Rotary Arc": "ffa77bff",
        "Rotary Arc Unselected": "ff2a1d40", "Widget Background": "ff030205", "Widget Primary 1": "ffff2bd6", "Widget Primary 2": "ff22e6ff",
        "Widget Secondary 1": "50ff2bd6", "Widget Secondary 2": "5022e6ff", "Widget Center Line": "ffa77bff", "Power Button On": "ffa77bff",
        "UI Action Button": "ffa77bff", "Label Background": "ff1b1129", "Text Component Background": "ff1b1129", "Popup Background": "ff110a1b",
        "Linear Slider": "ffa77bff", "Icon Button On": "ffa77bff"}),
    # SKETCH: warm rice paper, graphite ink, sepia headings; the colours are watercolor washes (muted, watery), one per
    # part of the synth: indigo oscillators, rose envelopes, sage LFOs, ochre filters, plum modulation
    "SKETCH": (True, 0.11, 0.32, 0.9, 0.16, 0.8, 0.55, {
        "Background": "ffe6dcc6", "Body": "fff3ecdd", "Body Heading Background": "ffece2cf", "Body Text": "ff2f2a26",
        "Heading Text": "ff5a4636", "Border": "5a8d8273", "Label Background": "ffe9dfca", "Label Connection": "00000000",
        "Popup Background": "fff6f0e3", "Popup Border": "ff8d8273", "Popup Selector Background": "ffe2d7c1", "Preset Text": "ff2f2a26",
        "Text Component Background": "ffe9dfca", "Text Component Text": "ff2f2a26", "Text Editor Background": "fff6f0e3",
        "Text Editor Border": "ff8d8273", "Text Editor Caret": "ff2f2a26", "Text Editor Selection": "555b76a3",
        "Rotary Body": "fff6f0e3", "Rotary Body Border": "ff8d8273", "Rotary Hand": "ff2f2a26", "Rotary Arc": "ff5b76a3",
        "Rotary Arc Unselected": "ffe2d7c1", "Rotary Arc Disabled": "ffc8bda9", "Rotary Arc Unselected Disabled": "ffece2cf",
        "Linear Slider": "ff5b76a3", "Linear Slider Unselected": "ffe2d7c1", "Linear Slider Thumb": "ff2f2a26",
        "Linear Slider Disabled": "ffc8bda9", "Linear Slider Thumb Disabled": "ffb8ad9b",
        "Widget Background": "ffe9dfca", "Widget Primary 1": "ff5b76a3", "Widget Primary 2": "ff8ea3c8", "Widget Primary Disabled": "ffb8ad9b",
        "Widget Secondary 1": "555b76a3", "Widget Secondary 2": "148ea3c8", "Widget Secondary Disabled": "20b8ad9b",
        "Widget Accent 1": "aa2f2a26", "Widget Accent 2": "142f2a26", "Widget Center Line": "ff8d8273",
        "Power Button On": "ff6f9a64", "Power Button Off": "ffb8ad9b", "Icon Button Off": "ff8d8273", "Icon Button Off Hover": "ff5a4636",
        "Icon Button Off Pressed": "ff8d8273", "Icon Button On": "ff5b76a3", "Icon Button On Hover": "ff4a6590", "Icon Button On Pressed": "ff3f5680",
        "Icon Selector Icon": "ff8d8273", "UI Button": "ffe9dfca", "UI Button Hover": "ffe2d7c1", "UI Button Press": "ffd8ccb4",
        "UI Button Text": "ff2f2a26", "UI Action Button": "ff5b76a3", "UI Action Button Hover": "ff4a6590", "UI Action Button Press": "ff3f5680",
        "Modulation Button Selected": "ffe2d7c1", "Modulation Button Unselected": "ffece2cf", "Modulation Button Dragging": "ffc8707e",
        "Shadow": "22403020", "Overlay Screen": "88efe7d6", "Lighten Screen": "22ffffff"}),
}
# 2.0.16: three looks. The watercolor one (made here as "SKETCH") is called LIGHT in the plug-in; PURPLE and the old flat
# LIGHT were removed.
FILES = {"GHOST": "default.vitalskin", "BLACK": "ghosty_black.vitalskin", "SKETCH": "ghosty_sketch.vitalskin"}

for name, (light, th, ts, sl, il, av, asx, picks) in LOOKS.items():
    skin = copy.deepcopy(BASE)
    tf = lambda c: transform(c, light, th, ts, sl, il, av, asx)
    for k, v in list(skin.items()):
        if k != "synth_version" and is_colour(k, v): skin[k] = tf(v)
    for sec, ov in skin.get("overrides", {}).items():
        if isinstance(ov, dict):
            for k, v in list(ov.items()):
                if is_colour(k, v) and v not in ("0",): ov[k] = tf(v)
    skin.update(picks)
    if name == "GHOST":
        purple = {"Widget Primary 1": "ffc04dff", "Widget Primary 2": "ff8a2be2", "Widget Secondary 1": "70c04dff",
                  "Widget Secondary 2": "188a2be2", "UI Action Button": "ffc04dff", "Power Button On": "ffff4dd2"}
        green = {"Widget Primary 1": "ff6cff2a", "Widget Primary 2": "ff3fcf14", "Widget Secondary 1": "786cff2a",
                 "Widget Secondary 2": "183fcf14", "Rotary Arc": "ff6cff2a", "Power Button On": "ff6cff2a", "Widget Center Line": "ff6cff2a",
                 "Icon Button On": "ff6cff2a", "Icon Button On Hover": "ff9dff6a", "Icon Button On Pressed": "ff4fd61a"}
        ov = skin["overrides"]
        for sec in ("Oscillator", "Sample", "Wavetable Editor"):
            ov.setdefault(sec, {}).update(purple)
        for sec in ("Envelope", "Lfo", "RandomLfo", "Filter", "Modulation Matrix", "Modulation Drag Drop"):
            ov.setdefault(sec, {}).update(green)
        ov.setdefault("Header", {}).update({"Widget Primary 1": "ff6cff2a", "Widget Primary 2": "646cff2a", "Widget Secondary 1": "ffc04dff",
                                            "Widget Secondary 2": "ff8a2be2", "Widget Background": "ff07090a", "Popup Selector Background": "ff111417"})
    if name == "SKETCH":   # one watercolor per part of the synth (the line, then its wash under the curve)
        def wcset(c1, c2):
            return {"Widget Primary 1": "ff" + c1, "Widget Primary 2": "ff" + c2, "Widget Secondary 1": "55" + c1,
                    "Widget Secondary 2": "14" + c2, "Widget Center Line": "ff" + c1, "Icon Button On": "ff" + c1, "Power Button On": "ff" + c1}
        ov = skin["overrides"]
        for sec, (c1, c2) in {"Oscillator": ("5b76a3", "8ea3c8"), "Sample": ("5b76a3", "8ea3c8"), "Wavetable Editor": ("5b76a3", "8ea3c8"),
                              "Envelope": ("b0596a", "d898a3"), "Lfo": ("5f8a55", "9cbf92"), "RandomLfo": ("5f8a55", "9cbf92"),
                              "Filter": ("a9862f", "d6bc78"), "Modulation Matrix": ("7a5a8c", "b49cc4"),
                              "Modulation Drag Drop": ("7a5a8c", "b49cc4"), "Sub": ("8a6a4f", "c4a688")}.items():
            ov.setdefault(sec, {}).update(wcset(c1, c2))
        ov.setdefault("Header", {}).update({"Widget Background": "ffe9dfca", "Widget Primary 1": "ff5b76a3", "Widget Primary 2": "ff8ea3c8"})
    # Ghosty Pro's rounder cards and knobs
    skin["Body Rounding"] = 12.0 if name != "GHOST" else 10.0
    skin["Widget Rounded Corner"] = 10.0
    skin["Label Rounding"] = 10.0
    skin["Widget Fill Boost"] = 1.2 if light else 1.8   # the glow under the wave lines
    skin["Widget Line Width"] = 2.4 if name != "SKETCH" else 1.8   # SKETCH: a finer, pencil-weight line
    # the logo section draws our ghost (vital_ring / vital_v resources): ghost body white, ring violet
    logo = skin["overrides"].setdefault("Logo", {})
    ghost_body = ("ff7b46e8", "ff9a7bf0") if light else ("fffbfaff", "ffeeeaf8")
    if name == "SKETCH": ghost_body = ("ff5b76a3", "ff8ea3c8")   # (Vital's logo colours; the button now draws our ghost)
    logo.update({"Widget Primary 1": picks.get("Widget Primary 2", "ff7b46e8"), "Widget Primary 2": picks.get("Widget Primary 1", "ffd23c9e"),
                 "Widget Secondary 1": ghost_body[0], "Widget Secondary 2": ghost_body[1], "Shadow": "00000000"})
    # 2.0.4 (Brandon: "dark where I circled, top right"): the volume meter + master volume slider and the scope in Vital's header
    # were near-black on near-black (ticks at 8% white, an invisible thumb, a panel the colour of the strip). Lift them: a
    # visible panel, ticks and meter track at ~35% white, and a neon thumb (UI colours only).
    if not light:
        hdr = skin["overrides"].setdefault("Header", {})
        hdr.update({"Lighten Screen": "59ffffff", "Widget Background": {"GHOST": "ff182019", "BLACK": "ff1f142a", "PURPLE": "ff3a2a5a"}[name]})
        hdr["Linear Slider Thumb"] = {"GHOST": "ff6cff2a", "BLACK": "ffa77bff", "PURPLE": "ffe0ccff"}[name]
    # the keyboard: white keys = Secondary 1, black keys = Secondary 2, pressed = Primary 1 (Vital's MidiKeyboard)
    kb = skin["overrides"].setdefault("Keyboard", {})
    keys = {"GHOST": ("fff6f0ff", "ff0b0d0f", "ffc04dff", "ff2a2433"), "LIGHT": ("fff6f7fa", "ff2a3040", "ff5636cc", "ffb4bac6"),
            "BLACK": ("ffd9d2e6", "ff110a1b", "ffa77bff", "ff3a2a55"), "PURPLE": ("ffefe8fb", "ff2e2148", "ffc4a6ff", "ff7a65a3"),
            "SKETCH": ("fff8f3e8", "ff3a332d", "ffc8707e", "ffb8ad9b")}[name]
    kb.update({"Widget Secondary 1": keys[0], "Widget Secondary 2": keys[1], "Widget Primary 1": keys[2], "Widget Accent 1": keys[3]})
    # GHOST ORBIT knobs (build 4): purple ring = the base value, acid green = the pointer, the leading ticks and the
    # modulation rings (Vital's modulation meters). Same in every section, so a knob always reads the same way.
    base, green, unlit, rim = {"GHOST": ("ffc04dff", "ff6cff2a", "ff221a2c", "ff3a2452"),
                               "BLACK": ("ffa77bff", "ff6cff2a", "ff2a1d40", "ff3a2a55"),
                               "PURPLE": ("ffe0ccff", "ff8dff5a", "ff3a2a5a", "ff8a74b8"),
                               "LIGHT": ("ff5636cc", "ff2e9e0a", "ffd6dae2", "ffb4bac6"),
                               "SKETCH": ("ff5b76a3", "ffb0596a", "ffe2d7c1", "ff9a8f80")}[name]
    skin.update({"Rotary Arc": base, "Rotary Hand": green, "Rotary Arc Unselected": unlit, "Rotary Body Border": rim,
                 "Modulation Meter": green, "Modulation Meter Control": green, "Modulation Meter Left": green,
                 "Modulation Meter Right": base})
    if name == "SKETCH": skin["Rotary Hand"] = "ff2f2a26"   # the pointer: a graphite line
    # build 5 (Brandon): the EFFECTS tab keeps Vital's own colour per effect (Delay, Reverb, ...) on its knob arcs
    effects = {"Chorus", "Compressor", "Delay", "Distortion", "Effects Filter", "Equalizer", "Flanger", "Phaser", "Reverb"}
    for sec, ov in skin["overrides"].items():
        if isinstance(ov, dict):
            for k in (() if sec in effects else ("Rotary Arc",)) + ( "Rotary Hand", "Rotary Arc Unselected", "Modulation Meter", "Modulation Meter Control",
                      "Modulation Meter Left", "Modulation Meter Right"):
                ov.pop(k, None)
    # 2.0.4 (Brandon: nothing may show Vital's own colours): every section override keeps Vital's per-section hue (teal, pink, red,
    # orange, yellow, blue). Move any such hue onto our two: green or purple (same saturation and brightness), so no page looks stock.
    def to_palette(c):
        a, r, g, b = parse(c)
        h, sv, vv = colorsys.rgb_to_hsv(r, g, b)
        if sv < 0.3 or vv < 0.3 or 0.22 < h < 0.42 or 0.7 < h < 0.85:
            return c
        target = 0.77 if (h >= 0.85 or h < 0.06 or 0.55 <= h <= 0.7) else 0.30   # pink/red/blue -> purple; orange/yellow/teal -> green
        nr, ng, nb = colorsys.hsv_to_rgb(target, sv, vv)
        return fmt(a, nr, ng, nb)
    def walk(d):
        for k, v in d.items():
            if isinstance(v, dict): walk(v)
            elif is_colour(k, v): d[k] = to_palette(v)
    def mute(c):   # SKETCH: Vital's own section hues (the effects) stay, as watercolor: less saturated, never bright
        a, r, g, b = parse(c)
        h, sv, vv = colorsys.rgb_to_hsv(r, g, b)
        if sv < 0.5 or vv < 0.3:   # (already watery: our own section colours and Vital's greys)
            return c
        nr, ng, nb = colorsys.hsv_to_rgb(h, max(0.45, sv * 0.6), min(vv, 0.74))
        return fmt(a, nr, ng, nb)
    if name == "SKETCH":
        to_palette = mute
    walk(skin.get("overrides", {}))
    if isinstance(skin.get("colors"), dict): walk(skin["colors"])
    json.dump(skin, open(os.path.join(out, FILES[name]), "w"), indent=1)
print("wrote", len(LOOKS), "skins")
