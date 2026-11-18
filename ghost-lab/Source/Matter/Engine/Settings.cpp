// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Settings.h"

namespace matter
{
const juce::StringArray& sourceNames()
{
    static const juce::StringArray n { "PHYSICAL", "VOICE", "NOISE", "WORDS", "YOUR SOUND" };
    return n;
}

const juce::StringArray& bodyNames (Source s)
{
    // PHYSICAL = Flues PM-Synth's 12 interfaces, in its order; VOICE = Flues Chatterbox with its own vowel presets (and its default, NEUTRAL); NOISE = a pick of Flues Disyn's generators
    static const juce::StringArray phys { "PLUCK", "HIT", "REED", "FLUTE", "BRASS", "BOW", "BELL", "DRUM", "CRYSTAL", "VAPOR", "QUANTUM", "PLASMA" };
    static const juce::StringArray voice { "AH", "EE", "OO", "OH", "EH", "NEUTRAL", "HUM", "WHISPER" };
    static const juce::StringArray noise { "BUZZ", "GLASS PULSE", "TWIN STARS", "FUZZ", "TORN SAW", "FORMANT", "FM", "SWARM", "FEEDBACK", "ODD METAL", "RING", "POLYGON" };
    // WORDS: Bark's own synthetic speaker presets (v2/en_speaker_0 .. 9), no real people's voices
    static const juce::StringArray words { "SPEAKER 1", "SPEAKER 2", "SPEAKER 3", "SPEAKER 4", "SPEAKER 5", "SPEAKER 6", "SPEAKER 7", "SPEAKER 8", "SPEAKER 9", "SPEAKER 10" };
    static const juce::StringArray own { "YOUR SOUND" };
    switch (s)
    {
        case Source::physical: return phys;
        case Source::voice: return voice;
        case Source::noise: return noise;
        case Source::words: return words;
        default: return own;
    }
}

const juce::StringArray& timbreNames()
{
    static const juce::StringArray n { "OFF", "VOICE", "METAL", "AIR", "GLASS", "THROAT", "WOOD", "YOUR SOUND" };
    return n;
}

bool MakeSettings::operator== (const MakeSettings& o) const
{
    return source == o.source && body == o.body && loud == o.loud && bright == o.bright && rough == o.rough && shape == o.shape
        && seconds == o.seconds && note == o.note && ownAudio == o.ownAudio
        && (source != Source::words || (words == o.words && wordsAudio == o.wordsAudio))
        && hood::samePins (pins, o.pins, { "pm", "cb", "ds" });
}

bool StretchSettings::operator== (const StretchSettings& o) const
{
    return amount == o.amount && size == o.size && spread == o.spread && up == o.up && down == o.down && pitch == o.pitch && volume == o.volume
        && hood::samePins (pins, o.pins, { "pxs" }) && freeFilter.isValid() == o.freeFilter.isValid() && (! freeFilter.isValid() || freeFilter.isEquivalentTo (o.freeFilter));
}

bool MorphSettings::operator== (const MorphSettings& o) const
{
    return timbre == o.timbre && amount == o.amount && detail == o.detail && ownAudio == o.ownAudio && ddspModel == o.ddspModel
        && neural == o.neural && wobble == o.wobble && hop == o.hop && refeed == o.refeed
        && hood::samePins (pins, o.pins, { "cdp", "ddsp" });
}


juce::ValueTree RenderSettings::toTree() const
{
    juce::ValueTree t ("RENDER");
    juce::ValueTree m ("MAKE");
    m.setProperty ("source", (int) make.source, nullptr).setProperty ("body", make.body, nullptr)
     .setProperty ("loud", make.loud, nullptr).setProperty ("bright", make.bright, nullptr).setProperty ("rough", make.rough, nullptr)
     .setProperty ("shape", make.shape, nullptr).setProperty ("seconds", make.seconds, nullptr).setProperty ("note", make.note, nullptr);
    if (make.ownAudio.getSize() > 0) m.setProperty ("ownAudio", make.ownAudio.toBase64Encoding(), nullptr).setProperty ("ownName", make.ownName, nullptr);
    m.setProperty ("words", make.words, nullptr);
    if (make.wordsAudio.getSize() > 0) m.setProperty ("wordsAudio", make.wordsAudio.toBase64Encoding(), nullptr);
    t.appendChild (m, nullptr);

    juce::ValueTree s ("STRETCH");
    s.setProperty ("amount", stretch.amount, nullptr).setProperty ("size", stretch.size, nullptr).setProperty ("spread", stretch.spread, nullptr)
     .setProperty ("up", stretch.up, nullptr).setProperty ("down", stretch.down, nullptr).setProperty ("pitch", stretch.pitch, nullptr).setProperty ("volume", stretch.volume, nullptr);
    if (stretch.freeFilter.isValid()) s.appendChild (stretch.freeFilter.createCopy(), nullptr);   // the plug-in's own "freefilter_envelope" tree
    t.appendChild (s, nullptr);

    juce::ValueTree mo ("MORPH");
    mo.setProperty ("timbre", (int) morph.timbre, nullptr).setProperty ("amount", morph.amount, nullptr).setProperty ("detail", morph.detail, nullptr)
      .setProperty ("ddspModel", morph.ddspModel, nullptr).setProperty ("neural", morph.neural, nullptr).setProperty ("wobble", morph.wobble, nullptr)
      .setProperty ("hop", morph.hop, nullptr).setProperty ("refeed", morph.refeed, nullptr);
    if (morph.ownAudio.getSize() > 0) mo.setProperty ("ownAudio", morph.ownAudio.toBase64Encoding(), nullptr).setProperty ("ownName", morph.ownName, nullptr);
    t.appendChild (mo, nullptr);
    juce::ValueTree h ("HOOD");
    for (auto* pp : { &make.pins, &stretch.pins, &morph.pins, &livePins })
        for (auto& kv : *pp) h.setProperty (juce::Identifier (kv.first), kv.second, nullptr);
    if (h.getNumProperties() > 0) t.appendChild (h, nullptr);
    return t;
}

RenderSettings RenderSettings::fromTree (const juce::ValueTree& t)
{
    RenderSettings r;
    auto f = [] (const juce::ValueTree& v, const char* id, float def, float lo, float hi)
    { return juce::jlimit (lo, hi, (float) (double) v.getProperty (id, def)); };

    if (auto m = t.getChildWithName ("MAKE"); m.isValid())
    {
        r.make.source = (Source) juce::jlimit (0, 4, (int) m.getProperty ("source", 0));
        r.make.body = juce::jlimit (0, bodyNames (r.make.source).size() - 1, (int) m.getProperty ("body", 0));
        r.make.loud = f (m, "loud", 0.5f, 0, 1); r.make.bright = f (m, "bright", 0.5f, 0, 1); r.make.rough = f (m, "rough", 0.2f, 0, 1);
        r.make.shape = f (m, "shape", 0.5f, 0, 1); r.make.seconds = f (m, "seconds", 4, 0.5f, (float) kMaxSourceSeconds);
        r.make.note = juce::jlimit (24, 96, (int) m.getProperty ("note", kRootNote));
        r.make.ownAudio.fromBase64Encoding (m.getProperty ("ownAudio").toString());
        r.make.ownName = m.getProperty ("ownName").toString();
        r.make.words = m.getProperty ("words", "ghost matter").toString().substring (0, 200);
        r.make.wordsAudio.fromBase64Encoding (m.getProperty ("wordsAudio").toString());
    }
    // (sessions from before 0.1.7 may hold a PAINT step and a RAVE model: both are gone and are ignored)
    if (auto s = t.getChildWithName ("STRETCH"); s.isValid())
    {
        r.stretch.amount = f (s, "amount", 1, 0.1f, 1024); r.stretch.size = f (s, "size", 0.7f, 0, 1); r.stretch.spread = f (s, "spread", 0, 0, 1);
        r.stretch.up = f (s, "up", 0, 0, 1); r.stretch.down = f (s, "down", 0, 0, 1); r.stretch.pitch = f (s, "pitch", 0, -24, 24);
        r.stretch.volume = f (s, "volume", -3, -24, 12);
        if (auto ff = s.getChildWithName ("freefilter_envelope"); ff.isValid() && ff.getNumChildren() > 0) r.stretch.freeFilter = ff.createCopy();
    }
    if (auto m = t.getChildWithName ("MORPH"); m.isValid())
    {
        r.morph.timbre = (Timbre) juce::jlimit (0, (int) Timbre::count - 1, (int) m.getProperty ("timbre", 0));
        r.morph.amount = f (m, "amount", 0.6f, 0, 1); r.morph.detail = f (m, "detail", 0, 0, 1);
        r.morph.ddspModel = m.getProperty ("ddspModel").toString().substring (0, 200);
        r.morph.neural = r.morph.ddspModel.isEmpty() ? 0.0f : f (m, "neural", 0, 0, 1);
        r.morph.wobble = f (m, "wobble", 0, 0, 1); r.morph.hop = f (m, "hop", 0, 0, 1); r.morph.refeed = f (m, "refeed", 0, 0, 1);
        r.morph.ownAudio.fromBase64Encoding (m.getProperty ("ownAudio").toString());
        r.morph.ownName = m.getProperty ("ownName").toString();
    }
    if (auto h = t.getChildWithName ("HOOD"); h.isValid())
        for (int i = 0; i < h.getNumProperties(); ++i)
        {
            const std::string key = h.getPropertyName (i).toString().toStdString();
            const int ti = hood::indexOf (key);
            if (ti < 0) continue;
            const auto& d = hood::def (ti);
            const float v = juce::jlimit (d.lo, d.hi, (float) (double) h.getProperty (h.getPropertyName (i)));
            const std::string e = d.engine;
            if (e == "pm" || e == "cb" || e == "ds" || e == "bark") r.make.pins[key] = v;
            else if (e == "pxs") r.stretch.pins[key] = v;
            else if (e == "cdp" || e == "ddsp") r.morph.pins[key] = v;
            else r.livePins[key] = v;
        }
    return r;
}
}
