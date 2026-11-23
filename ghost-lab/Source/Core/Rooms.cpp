// Ghost Lab - AGPL-3.0-only from build 12 (see LICENSE, NOTICE.md). The rooms: glue between the lab's steps and the
// engines that moved in from Ghost Matter (Rooms.h). Nothing here makes or changes sound itself: it sets the engines' own
// controls, runs them, and hands the result to the lab's usual finishing stage.
#include "Rooms.h"
#include "MakeStage.h"
#include "StretchStage.h"
#include "Ddsp.h"
#include "Space.h"
#include "Spectre.h"
#include "Hood.h"   // (Source/Matter/Hood: the engines' own control tables)
#include <juce_dsp/juce_dsp.h>

namespace glab::rooms
{
static const RoomInfo kRooms[] = {
    { Room::Lab, Op::Count, "THE LAB", "Pull, twist, freeze and breed your sound.", "", "" },
    { Room::Grow, Op::Grow, "GROW", "Grow a brand new sound out of nothing: a struck body, a voice, or strange noise.",
      "Start here when you have no sound, or want raw material nobody else has.", "Flues" },
    { Room::Stretch, Op::XStretch, "STRETCH", "Slow the sound down so far it turns into a wash: a pad, a drone, a whole atmosphere.",
      "Best on voices, chords, bells and guitars. Drums and cymbals turn to hiss.", "PaulXStretch" },
    { Room::Reborn, Op::Reborn, "REBORN", "A neural instrument listens to the tune and the loudness of your sound, then plays it back as itself.",
      "Best on one clear voice or melody (humming, a lead, a bass line). Chords confuse it.", "DDSP" },
    { Room::Space, Op::Space, "SPACE", "Put the sound somewhere around your head: left, right, near, far, in a room.",
      "Best in headphones. Hold a key on your keyboard (or press LISTEN) and move anything: you hear it live.", "Pertylizer" },
    { Room::Spectre, Op::Spectre, "SPECTRE", "Rebuilds your sound from up to 1,024 sine waves, so you can bend every one of them live.",
      "Works on anything: voices, pads, basses, guitars and drums. Turn NOISE down for the glassy, all-sine-wave sound.", "Ghost Lab's own resynthesis" },
};

const RoomInfo& info (Room r) { return kRooms[juce::jlimit (0, (int) Room::Count - 1, (int) r)]; }
Room roomOf (Op op) { for (auto& r : kRooms) if (r.op == op && op != Op::Count) return r.room; return Room::Lab; }
bool isRoomOp (Op op) { return roomOf (op) != Room::Lab; }

const juce::StringArray& growSources() { static const juce::StringArray s { "PHYSICAL", "VOICE", "NOISE" }; return s; }
const juce::StringArray& growBodies (int source)
{
    return matter::bodyNames ((matter::Source) juce::jlimit (0, 2, source));
}
bool rebornAvailable() { return matter::neural::available(); }
static juce::StringArray& pretendInstruments() { static juce::StringArray a; return a; }
void pretendInstrumentsForShots (const juce::StringArray& names) { pretendInstruments() = names; }
juce::StringArray rebornInstruments()
{
    if (! pretendInstruments().isEmpty()) return pretendInstruments();   // (screenshots of the page on a build without them)
    return rebornAvailable() ? matter::neural::modelNames() : juce::StringArray();
}

juce::Range<float> instrumentRange (const juce::String& name)
{
    struct R { const char* n; float lo, hi; };
    // where each real instrument sits (MIDI notes): the model was trained on recordings in this range, so it sounds most
    // like itself there
    static const R table[] = { { "flute", 60, 96 }, { "violin", 55, 96 }, { "trumpet", 54, 86 }, { "saxophone", 49, 80 }, { "bassoon", 34, 72 },
                               { "clarinet", 52, 88 }, { "melodica", 53, 89 }, { "sitar", 48, 84 }, { "trombone", 40, 74 }, { "tuba", 28, 60 },
                               { "vowels", 45, 79 } };
    for (auto& r : table) if (name.containsIgnoreCase (r.n)) return { r.lo, r.hi };
    return { 48.0f, 84.0f };
}

// ------------------------------------------------------------------------------------------------ the originals' controls
const juce::StringArray& stretchModules()
{
    static const juce::StringArray m { "Harmonics", "Tonal vs Noise", "Frequency shift", "Pitch shift", "Ratios", "Spread", "Filter", "Free filter", "Compressor" };
    return m;
}
juce::String moduleKey (int module) { return "pxs.enab_specmodule" + juce::String (module); }

static juce::String plainWords (const juce::String& key)
{
    static const std::map<juce::String, juce::String> w {
        { "pxs.mainvolume0", "how loud the result is" }, { "pxs.stretchamount0", "how many times longer it gets" },
        { "pxs.fftsize0", "low = grainy slices, high = one smooth wash" }, { "pxs.freeze0", "holds one moment forever" },
        { "pxs.window", "the shape of each slice's edges" }, { "pxs.onsetdetect_0", "keeps sharp hits sharp instead of blurring them" },
        { "pxs.pitchshift0", "moves every note up or down" }, { "pxs.freqshift0", "slides everything up or down: goes metallic" },
        { "pxs.spread0", "every note bleeds into its neighbours" }, { "pxs.compress0", "evens out the loud and quiet parts" },
        { "pxs.numharmonics0", "how many overtones are kept" }, { "pxs.harmonicsfreq0", "the note the overtones are built on" },
        { "pxs.harmonicsbw0", "how wide each overtone is" }, { "pxs.harmonicsgauss0", "softer-edged overtones" },
        { "pxs.octavemixm2_0", "a copy two octaves down" }, { "pxs.octavemixm1_0", "a copy one octave down: weight" },
        { "pxs.octavemix0_0", "the sound at its own pitch" }, { "pxs.octavemix1_0", "a copy one octave up: shimmer" },
        { "pxs.octavemix15_0", "a copy an octave and a fifth up" }, { "pxs.octavemix2_0", "a copy two octaves up" },
        { "pxs.tonalvsnoisebw_0", "how picky it is about what counts as a note" }, { "pxs.tonalvsnoisepreserve_0", "right = keep the notes, left = keep the noise" },
        { "pxs.filter_low_0", "cuts everything below this" }, { "pxs.filter_high_0", "cuts everything above this" },
        { "pxs.playrange_start0", "where in your sound the render starts" }, { "pxs.playrange_end0", "where in your sound the render stops" },
        { "pxs.bypass_stretch0", "plays the sound unstretched, at Dry playrate" }, { "pxs.dryplayrate0", "how fast the unstretched sound plays" },
        { "pxs.loopxfadelen0", "nothing to do here: the lab renders once, it does not loop" },
        { "pxs.passthrough0", "nothing to do here: the lab has no live input" },
        { "pxs.maxcapturelen_0", "nothing to do here: the lab does not record" },
        { "pxs.octavemix_extra0_0", "a seventh copy, at Ratio 7" }, { "pxs.octavemix_extra1_0", "an eighth copy, at Ratio 8" },
        { "pxs.ratiomix_ratio_0_0", "how far copy 1 is moved (0.5 = octave down, 2 = octave up)" }, { "pxs.ratiomix_ratio_1_0", "how far copy 2 is moved" },
        { "pxs.ratiomix_ratio_2_0", "how far copy 3 is moved" }, { "pxs.ratiomix_ratio_3_0", "how far copy 4 is moved" },
        { "pxs.ratiomix_ratio_4_0", "how far copy 5 is moved" }, { "pxs.ratiomix_ratio_5_0", "how far copy 6 is moved" },
        { "pxs.ratiomix_ratio_6_0", "how far copy 7 is moved" }, { "pxs.ratiomix_ratio_7_0", "how far copy 8 is moved" },
        { "pxs.freefilter_shiftx_0", "slides your curve left or right" }, { "pxs.freefilter_shifty_0", "slides your curve up or down" },
        { "pxs.freefilter_scaley_0", "makes your curve's hills taller or flatter" }, { "pxs.freefilter_tilty_0", "tips your curve: more lows or more highs" },
        { "pxs.freefilter_randomybands0", "how many bands wobble at random" }, { "pxs.freefilter_randomyrate0", "how fast they wobble" },
        { "pxs.freefilter_randomyamount0", "how far they wobble" },
        { "pxs.binauralbeats", "a slow pulse between your ears" }, { "pxs.binauralbeatsmono", "how strong the pulse is" },
        { "pxs.binauralbeatsfreq", "how fast the pulse is" }, { "pxs.binauralbeatsmode", "which ear leads" },
        { "ddsp.InputPitch", "slides the box sideways: the tune the model hears" }, { "ddsp.InputGain", "slides the box up and down: how loud the model hears you" },
        { "ddsp.PitchShift", "moves the tune it plays" }, { "ddsp.HarmonicGain", "the instrument's notes" }, { "ddsp.NoiseGain", "its breath, bow and key noise" },
        { "ddsp.OutputGain", "how loud it comes out" }, { "ddsp.ReverbSize", "how big its room is" }, { "ddsp.ReverbDamping", "how dark its room is" },
        { "ddsp.ReverbWet", "how much room you hear" },
        { "panner.x", "left / right" }, { "panner.y", "in front / behind" }, { "panner.z", "above / below" }, { "panner.diffusion", "how blurry the echoes are" },
        { "panner.er_level", "the first echoes off the walls" }, { "panner.direct_level", "the sound itself" }, { "panner.absorption", "soft walls soak up echoes" },
        { "panner.air_absorption", "far sounds lose their sparkle" }, { "panner.distance", "how far away it is" },
        { "fdn.room_size", "how big the room is" }, { "fdn.decay", "how long it rings" }, { "fdn.damping", "the ring gets darker" }, { "fdn.diffusion", "smooth or grainy ring" },
        { "fdn.pre_delay", "a gap before the ring" }, { "fdn.low_cut", "keeps rumble out of the ring" }, { "fdn.width", "how wide the ring is" }, { "fdn.mix", "how much ring vs sound" } };
    if (key.startsWith ("pxs.order")) return "which effect sits at this place in the chain";
    auto it = w.find (key);
    return it != w.end() ? it->second : juce::String();
}

static std::vector<Ctl> buildControls (Room r)
{
    std::vector<Ctl> out;
    const char* engines[2] = { r == Room::Stretch ? "pxs" : r == Room::Reborn ? "ddsp" : r == Room::Space ? "panner" : "", r == Room::Space ? "fdn" : "" };
    static const char* skip[] = { "enab_specmodule" };   // (the nine module switches are the groups' own switches and the chain)
    for (int i = 0; i < matter::hood::count(); ++i)
    {
        const auto& d = matter::hood::def (i);
        if (std::strcmp (d.engine, engines[0]) != 0 && std::strcmp (d.engine, engines[1]) != 0) continue;
        bool skipped = false;
        for (auto* sk : skip) if (juce::String (d.id).startsWith (sk)) skipped = true;
        if (skipped) continue;
        Ctl c;
        c.key = juce::String (d.engine) + "." + d.id; c.name = d.upstream; c.group = d.group; c.unit = d.unit; c.kind = d.kind;
        c.lo = d.lo; c.hi = d.hi; c.def = d.upDef;
        for (auto& ch : matter::hood::choicesOf (d)) c.choices.add (ch);
        c.plain = plainWords (c.key);
        if (r == Room::Stretch)
        {
            static const std::map<juce::String, int> mod { { "Harmonics", 0 }, { "Tonal vs noise", 1 }, { "Frequency shift", 2 }, { "Pitch shift", 3 },
                                                           { "Octave mixer (ratios)", 4 }, { "Spread", 5 }, { "Filter", 6 }, { "Free filter", 7 }, { "Compressor", 8 } };
            if (auto it = mod.find (c.group); it != mod.end()) c.module = it->second;
            // (the page opens at 8x so RENDER does something you can hear; the original opens at 2x) and stops at 64x, a minute of sound
            if (c.key == "pxs.stretchamount0") { c.lo = 1.0f; c.hi = 64.0f; c.def = 8.0f; }
        }
        out.push_back (c);
    }
    return out;
}

const std::vector<Ctl>& controls (Room r)
{
    static const std::vector<Ctl> none, st = buildControls (Room::Stretch), rb = buildControls (Room::Reborn), sp = buildControls (Room::Space);
    return r == Room::Stretch ? st : r == Room::Reborn ? rb : r == Room::Space ? sp : none;
}

const Ctl* control (Room r, const juce::String& key) { for (auto& c : controls (r)) if (c.key == key) return &c; return nullptr; }

float val (const Step& s, const juce::String& key)
{
    if (auto it = s.values.find (key); it != s.values.end()) return it->second;
    if (key.startsWith ("pxs.enab_specmodule")) return key.endsWithChar ('2') || key.endsWithChar ('3') ? 1.0f : 0.0f;   // the original: frequency shift and pitch shift on
    if (auto* c = control (roomOf (s.op), key)) return c->def;
    return 0.0f;
}

void spacePins (const Step& s, float* v)
{
    int i = 0;
    for (auto& c : controls (Room::Space)) if (i < kSpaceControls) v[i++] = val (s, c.key);
}

// ------------------------------------------------------------------------------------------------ helpers
static juce::AudioBuffer<float> stereoAt (const Audio& a, double rate)
{
    const Audio r = std::abs (a.sampleRate - rate) > 0.5 ? resampled (a, rate) : a;
    juce::AudioBuffer<float> b (2, r.length());
    for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, r.buf, juce::jmin (c, r.channels() - 1), 0, r.length());
    return b;
}

static std::shared_ptr<Audio> asAudio (const juce::AudioBuffer<float>& b, double rate, double wantRate)
{
    auto a = std::make_shared<Audio>();
    a->buf.makeCopyOf (b);
    a->sampleRate = rate;
    if (std::abs (wantRate - rate) > 0.5) *a = resampled (*a, wantRate);
    sanitize (*a);
    return a;
}

static matter::MakeSettings makeSettings (const Step& s)
{
    matter::MakeSettings m;
    const int src = juce::jlimit (0, 2, juce::roundToInt (s.get ("source")));
    m.source = (matter::Source) src;
    m.body = juce::jlimit (0, growBodies (src).size() - 1, juce::roundToInt (s.get ("body")));
    m.loud = s.get ("loud"); m.bright = s.get ("bright"); m.rough = s.get ("rough"); m.shape = s.get ("shape");
    m.seconds = juce::jlimit (0.5f, (float) matter::kMaxSourceSeconds, s.get ("seconds"));
    m.note = juce::jlimit (24, 96, juce::roundToInt (s.get ("note")));
    return m;
}

static matter::StretchSettings stretchSettings (const Step& s)
{
    matter::StretchSettings t;
    t.amount = juce::jlimit (1.0f, 64.0f, val (s, "pxs.stretchamount0"));
    t.size = val (s, "pxs.fftsize0"); t.spread = val (s, "pxs.spread0"); t.up = val (s, "pxs.octavemix1_0"); t.down = val (s, "pxs.octavemixm1_0");
    t.pitch = val (s, "pxs.pitchshift0"); t.volume = val (s, "pxs.mainvolume0");
    // every other control of the original that the page shows, and its nine module switches, exactly as the page has them
    for (auto& c : controls (Room::Stretch)) t.pins[c.key.toStdString()] = val (s, c.key);
    for (int m = 0; m < 9; ++m) t.pins[moduleKey (m).toStdString()] = val (s, moduleKey (m));
    // (Sound end never sits before Sound start)
    if (t.pins["pxs.playrange_end0"] < t.pins["pxs.playrange_start0"] + 0.01f) t.pins["pxs.playrange_end0"] = juce::jmin (1.0f, t.pins["pxs.playrange_start0"] + 0.01f);
    // the Free filter curve you drew, in the plug-in's own saved form (the page keeps it in the step's data)
    if (s.data.isNotEmpty()) if (auto xml = juce::parseXML (s.data)) t.freeFilter = juce::ValueTree::fromXml (*xml);
    // (binaural beats: the glue only switches the plug-in's binaural stage in when one of its four is set; off = left alone)
    if (val (s, "pxs.binauralbeats") < 0.5f)
        for (auto* k : { "pxs.binauralbeats", "pxs.binauralbeatsmono", "pxs.binauralbeatsfreq", "pxs.binauralbeatsmode" }) t.pins.erase (k);
    return t;
}

juce::String describe (const Step& s)
{
    switch (s.op)
    {
        case Op::Grow:
        {
            const int src = juce::jlimit (0, 2, juce::roundToInt (s.get ("source")));
            return "GROW " + growBodies (src)[juce::jlimit (0, growBodies (src).size() - 1, juce::roundToInt (s.get ("body")))];
        }
        case Op::XStretch: return "STRETCH " + juce::String (val (s, "pxs.stretchamount0"), 1) + "x";   // (the history's label: tests read it)
        case Op::Reborn:
        {
            const auto names = rebornInstruments();
            const int i = juce::roundToInt (s.get ("instrument"));
            return "REBORN as " + (i >= 0 && i < names.size() ? names[i].toUpperCase() : juce::String ("?"));
        }
        case Op::Space:
        {
            const float x = val (s, "panner.x"), d = val (s, "panner.distance");
            return juce::String ("SPACE ") + (x < -0.2f ? "left" : x > 0.2f ? "right" : "middle") + (d > 0.7f ? ", far" : d < 0.3f ? ", close" : "");
        }
        case Op::Spectre:
        {
            juce::StringArray bits;
            const float tr = s.get ("transpose"), sh = s.get ("shift"), x = s.get ("phase"), y = s.get ("cepstrum");
            if (std::abs (tr) >= 0.05f) bits.add ((tr > 0 ? "+" : "") + juce::String (tr, 1) + " st");
            if (std::abs (sh) >= 1.0f) bits.add ((sh > 0 ? "+" : "") + juce::String (juce::roundToInt (sh)) + " Hz");
            if (x < -0.25f) bits.add ("robot"); else if (x > 0.25f) bits.add ("whisper");
            if (y > 0.25f) bits.add ("body"); else if (y < -0.25f) bits.add ("raw");
            if (s.get ("inf") > 0.6f) bits.add ("frozen"); else if (s.get ("cloud") > 0.4f) bits.add ("cloud");
            return "SPECTRE " + (bits.isEmpty() ? juce::String ("rebuilt") : bits.joinIntoString (", "));   // (always "SPECTRE ...": printing again replaces the last print)
        }
        default: break;
    }
    return {};
}

// ------------------------------------------------------------------------------------------------ the renders
std::shared_ptr<Audio> grow (const Step& s, juce::String& error)
{
    const auto b = matter::makeSound (makeSettings (s));
    if (b.getNumSamples() < 64) { error = "Nothing grew. Try another body."; return nullptr; }
    auto a = asAudio (b, matter::kRate, matter::kRate);
    const float level = rms (*a);
    if (level < 1e-5f) { error = "That one grew silent. Turn LOUD up or try another body."; return nullptr; }
    finish (*a, level);   // (its own level: only the edges, the DC and the ceiling are touched)
    return a;
}

static RenderResult stretch (const Step& s, const Audio& in, const std::atomic<bool>* cancel)
{
    RenderResult res;
    const auto t = stretchSettings (s);
    const auto src = stereoAt (in, matter::kRate);
    const auto out = matter::stretchSound (src, t, cancel);
    if (cancel != nullptr && cancel->load()) { res.error = "Stopped."; return res; }
    if (out.getNumSamples() < 64) { res.error = "The stretch came back empty. Try a smaller amount."; return res; }
    res.audio = asAudio (out, matter::kRate, in.sampleRate);
    res.commands.add ("stretch amount " + juce::String (t.amount, 2) + "  fft size " + juce::String (matter::stretchFftSize (t.size)) + " samples  spread "
                      + juce::String (t.spread, 2) + "  octave up " + juce::String (t.up, 2) + "  octave down " + juce::String (t.down, 2) + "  pitch "
                      + juce::String (t.pitch, 0) + " st");
    return res;
}

static RenderResult reborn (const Step& s, const Audio& in, const std::atomic<bool>* cancel)
{
    RenderResult res;
    if (! rebornAvailable()) { res.error = "This build has no neural instruments in it."; return res; }
    const auto names = rebornInstruments();
    const int idx = juce::roundToInt (s.get ("instrument"));
    if (idx < 0 || idx >= names.size()) { res.error = "Pick an instrument first."; return res; }
    matter::MorphSettings m;
    m.ddspModel = names[idx];
    m.neural = 1.0f;
    m.wobble = s.get ("chaos"); m.hop = s.get ("hop"); m.refeed = s.get ("refeed");
    // its own controls, exactly as the page has them (one that sits at its own default is left alone)
    const int shift = juce::roundToInt (val (s, "ddsp.PitchShift"));
    for (auto& c : controls (Room::Reborn))
        if (std::abs (val (s, c.key) - c.def) > 1e-6f) m.pins[c.key.toStdString()] = val (s, c.key);
    const auto src = stereoAt (in, matter::kRate);
    juce::AudioBuffer<float> wet;
    juce::String why;
    if (! matter::neural::render (src, m, wet, why, cancel)) { res.error = why.isNotEmpty() ? why : juce::String ("The instrument could not play that."); return res; }
    if (cancel != nullptr && cancel->load()) { res.error = "Stopped."; return res; }
    // MIX: how much of the instrument, how much of your own sound (equal length, lined up by the engine)
    const float mix = juce::jlimit (0.0f, 1.0f, s.get ("mix"));
    const int n = juce::jmin (wet.getNumSamples(), src.getNumSamples());
    juce::AudioBuffer<float> out (2, n);
    for (int c = 0; c < 2; ++c)
    {
        out.copyFrom (c, 0, wet, juce::jmin (c, wet.getNumChannels() - 1), 0, n);
        out.applyGain (c, 0, n, mix);
        out.addFrom (c, 0, src, c, 0, n, 1.0f - mix);
    }
    res.audio = asAudio (out, matter::kRate, in.sampleRate);
    res.commands.add ("instrument " + names[idx] + "  pitch shift " + juce::String (shift) + " st  mix " + juce::String (mix, 2) + "  chaos "
                      + juce::String (m.wobble, 2) + "  hop " + juce::String (m.hop, 2) + "  refeed " + juce::String (m.refeed, 2));
    return res;
}

matter::hood::LivePins livePinsFor (const float* v)
{
    matter::hood::LivePins pins;
    const int pb = matter::hood::liveBase ("panner"), fb = matter::hood::liveBase ("fdn");
    if (pb < 0 || fb < 0) return pins;
    for (int i = 0; i < 9; ++i) { pins.v[pb + i] = v[i]; pins.on[pb + i] = true; }
    for (int i = 0; i < 8; ++i) { pins.v[fb + i] = v[9 + i]; pins.on[fb + i] = true; }
    pins.any = true;
    return pins;
}

static RenderResult space (const Step& s, const Audio& in)
{
    RenderResult res;
    float v[kSpaceControls]; spacePins (s, v);
    const auto pins = livePinsFor (v);
    matter::SpaceParams p; p.tail = 0.0f; p.width = 0.0f;   // (every control of both modules is set from the page, exactly)
    const int block = 256;
    matter::Space sp;
    sp.prepare (in.sampleRate, block);
    // room for the room to ring out (the reverb's Decay and Room Size)
    const int ring = (int) (in.sampleRate * (0.4 + 6.0 * (double) v[10] * (double) v[16] + 0.6 * (double) v[9]));
    const int n = in.length() + ring;
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, in.buf, juce::jmin (c, in.channels() - 1), 0, in.length());
    for (int i = 0; i < n; i += block)
        sp.process (out.getWritePointer (0) + i, out.getWritePointer (1) + i, juce::jmin (block, n - i), p, &pins);
    int end = n;
    const float floor = juce::jmax (1e-5f, out.getMagnitude (0, n) * 0.001f);
    while (end > in.length() && juce::jmax (std::abs (out.getSample (0, end - 1)), std::abs (out.getSample (1, end - 1))) < floor) --end;
    out.setSize (2, juce::jmin (n, end + (int) (0.05 * in.sampleRate)), true);
    res.audio = asAudio (out, in.sampleRate, in.sampleRate);
    juce::String line;
    for (auto& c : controls (Room::Space)) line << c.key << " " << juce::String (val (s, c.key), 2) << "  ";
    res.commands.add (line);
    return res;
}

static RenderResult spectreRender (const Step& s, const Audio& in, const std::atomic<bool>* cancel)
{
    RenderResult res;
    spectre::Params p;
    for (int i = 0; i < spectre::kParams; ++i) p.v[(size_t) i] = s.get (spectre::param (i).key);
    auto out = spectre::renderOffline (in.buf, in.sampleRate, p, spectre::tableFromText (s.data), cancel);   // (Step.data: the WAVETABLE, if any)
    if (out.getNumSamples() < 2) { res.error = "Stopped."; return res; }
    res.audio = asAudio (out, in.sampleRate, in.sampleRate);
    juce::String line;
    for (int i = 0; i < spectre::kParams; ++i) line << spectre::param (i).key << " " << juce::String (p[i], 2) << "  ";
    res.commands.add (line);
    return res;
}

RenderResult render (const Step& s, const Audio& in, const std::atomic<bool>* cancel)
{
    RenderResult res;
    switch (s.op)
    {
        case Op::XStretch: res = stretch (s, in, cancel); break;
        case Op::Reborn:   res = reborn (s, in, cancel); break;
        case Op::Space:    res = space (s, in); break;
        case Op::Spectre:  res = spectreRender (s, in, cancel); break;
        default: res.error = "That is not a room."; return res;
    }
    if (res.audio == nullptr) return res;
    auto out = std::const_pointer_cast<Audio> (res.audio);
    if (out->length() < 16 || rms (*out) < 1e-6f) { res.audio = nullptr; res.error = "That came back silent. Try a gentler setting."; return res; }
    if ((double) out->length() / out->sampleRate > kMaxOutputSeconds) out->buf.setSize (out->channels(), (int) (kMaxOutputSeconds * out->sampleRate), true);
    finish (*out, rms (in));
    return res;
}

// ------------------------------------------------------------------------------------------------ the pictures
juce::Image spectrogram (const Audio& a, int cols, int rows, juce::Colour low, juce::Colour high)
{
    juce::Image im (juce::Image::ARGB, juce::jmax (1, cols), juce::jmax (1, rows), true);
    const int n = a.length();
    if (n < 64 || a.channels() < 1) return im;
    const int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) size, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> buf ((size_t) size * 2), mags ((size_t) cols * (size_t) rows, 0.0f);
    const float fLo = 40.0f, fHi = (float) juce::jmin (16000.0, a.sampleRate * 0.48);
    float top = 1e-9f;
    for (int x = 0; x < cols; ++x)
    {
        const int centre = (int) ((double) (x + 0.5) / cols * n);
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i)
        {
            const int k = centre - size / 2 + i;
            if (k < 0 || k >= n) continue;
            float v = 0; for (int c = 0; c < a.channels(); ++c) v += a.buf.getSample (c, k);
            buf[(size_t) i] = v / (float) a.channels();
        }
        win.multiplyWithWindowingTable (buf.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int y = 0; y < rows; ++y)
        {
            // each row: the strongest bin in its slice of the (log) frequency axis
            const float f0 = fLo * std::pow (fHi / fLo, (float) y / (float) rows), f1 = fLo * std::pow (fHi / fLo, (float) (y + 1) / (float) rows);
            const int b0 = juce::jlimit (1, size / 2 - 1, (int) (f0 / a.sampleRate * size)), b1 = juce::jlimit (b0 + 1, size / 2, (int) std::ceil (f1 / a.sampleRate * size));
            float m = 0; for (int b = b0; b < b1; ++b) m = juce::jmax (m, buf[(size_t) b]);
            mags[(size_t) x * (size_t) rows + (size_t) y] = m;
            top = juce::jmax (top, m);
        }
    }
    juce::Image::BitmapData px (im, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < cols; ++x)
        for (int y = 0; y < rows; ++y)
        {
            const float db = juce::Decibels::gainToDecibels (mags[(size_t) x * (size_t) rows + (size_t) y] / top, -100.0f);
            const float v = std::pow (juce::jlimit (0.0f, 1.0f, (db + 58.0f) / 58.0f), 1.4f);
            const float t = (float) y / (float) juce::jmax (1, rows - 1);            // low notes one colour, high notes the other
            const auto c = low.interpolatedWith (high, t).interpolatedWith (juce::Colours::white, juce::jmax (0.0f, v - 0.75f) * 2.2f);
            px.setPixelColour (x, rows - 1 - y, c.withAlpha (juce::jmin (1.0f, v * 1.25f)));
        }
    return im;
}

Melody melody (const Audio& a, int columns)
{
    Melody m;
    m.note.assign ((size_t) columns, 0.0f); m.loud.assign ((size_t) columns, 0.0f);
    const int n = a.length();
    if (n < 2048 || columns < 1) return m;
    // work at about 11 kHz: plenty for 50 .. 1200 Hz, and quick
    const int step = juce::jmax (1, (int) std::floor (a.sampleRate / 11025.0));
    const double sr = a.sampleRate / step;
    const int win = 1024, minLag = juce::jmax (2, (int) (sr / 1200.0)), maxLag = juce::jmin (win - 1, (int) (sr / 50.0));
    std::vector<float> x ((size_t) win + (size_t) maxLag), d ((size_t) maxLag + 1);
    float peakLoud = 1e-9f;
    std::vector<float> voicedNotes;
    for (int col = 0; col < columns; ++col)
    {
        const int centre = (int) ((double) (col + 0.5) / columns * n);
        const int start = centre - (win + maxLag) * step / 2;
        double e = 0;
        for (int i = 0; i < win + maxLag; ++i)
        {
            const int k = start + i * step;
            float v = 0;
            if (k >= 0 && k < n) for (int c = 0; c < a.channels(); ++c) v += a.buf.getSample (c, k);
            x[(size_t) i] = v / (float) juce::jmax (1, a.channels());
            if (i < win) e += (double) x[(size_t) i] * x[(size_t) i];
        }
        const float loud = (float) std::sqrt (e / win);
        m.loud[(size_t) col] = loud; peakLoud = juce::jmax (peakLoud, loud);
        if (loud < 1e-4f) continue;
        // the YIN difference function, normalised by its running mean (de Cheveigne & Kawahara 2002: the method, our code)
        double running = 0; int best = -1; float bestV = 1.0f;
        d[0] = 1.0f;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            double sum = 0;
            for (int i = 0; i < win; ++i) { const double q = (double) x[(size_t) i] - x[(size_t) (i + lag)]; sum += q * q; }
            running += sum;
            d[(size_t) lag] = running > 0 ? (float) (sum * lag / running) : 1.0f;
        }
        for (int lag = minLag; lag < maxLag; ++lag)
        {
            if (d[(size_t) lag] < 0.12f) { while (lag + 1 < maxLag && d[(size_t) lag + 1] < d[(size_t) lag]) ++lag; best = lag; bestV = d[(size_t) lag]; break; }
            if (d[(size_t) lag] < bestV) { bestV = d[(size_t) lag]; best = lag; }
        }
        if (best > 0 && bestV < 0.25f)
        {
            const float note = 69.0f + 12.0f * std::log2 ((float) (sr / best) / 440.0f);
            m.note[(size_t) col] = note; voicedNotes.push_back (note);
        }
    }
    // a single column that jumps far from both its neighbours is a tracking slip (an octave error where two notes meet), not a note
    for (int i = 0; i < columns; ++i)
    {
        const float v = m.note[(size_t) i];
        if (v <= 0.0f) continue;
        const float a = i > 0 ? m.note[(size_t) i - 1] : 0.0f, b = i + 1 < columns ? m.note[(size_t) i + 1] : 0.0f;
        const bool farA = a <= 0.0f || std::abs (v - a) > 5.0f, farB = b <= 0.0f || std::abs (v - b) > 5.0f;
        if (farA && farB) m.note[(size_t) i] = 0.0f;
    }
    for (auto& l : m.loud) l /= peakLoud;
    if (! voicedNotes.empty())
    {
        std::nth_element (voicedNotes.begin(), voicedNotes.begin() + (long) voicedNotes.size() / 2, voicedNotes.end());
        m.median = voicedNotes[voicedNotes.size() / 2];
    }
    m.voiced = (float) voicedNotes.size() / (float) columns;
    return m;
}

int autoShift (const Melody& m, juce::Range<float> range)
{
    if (m.median <= 0.0f) return 0;
    const float mid = (range.getStart() + range.getEnd()) * 0.5f;
    return juce::jlimit (-24, 24, 12 * juce::roundToInt ((mid - m.median) / 12.0f));
}
}
