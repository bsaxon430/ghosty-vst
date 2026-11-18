// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pages.h"

namespace atlas::ui
{
// layer < 0: whichever layer is in hand when the control is used
void bindLayer (Control& c, Shell& sh, int layer, int param)
{
    const auto& d = layerParam (param);
    c.label = worded (d.name);
    c.lo = d.min; c.hi = d.max; c.def = d.def; c.log = d.log; c.whole = d.kind != ParamDef::Float;
    c.bipolar = param == lpPan || param == lpCoarse || param == lpFine || param == lpRelease || param == lpVelCurve;
    c.choices.clear();
    for (const char* ch : d.choices) c.choices.add (worded (ch));
    if (c.choices.isEmpty()) c.text = [param] (float v) { return formatParam (param, v); };
    auto which = [&sh, layer] { return layer >= 0 ? layer : sh.layer; };
    if (param == lpGain)
    {
        c.get = [&sh, which] { return sh.proc.getHost (Processor::hpLevel1 + which()); };
        c.set = [&sh, which] (float v) { sh.proc.setHost (Processor::hpLevel1 + which(), v); };
        c.begin = [&sh, which] { sh.proc.beginGesture (Processor::hpLevel1 + which()); };
        c.end = [&sh, which] { sh.proc.endGesture (Processor::hpLevel1 + which()); };
    }
    else
    {
        c.get = [&sh, which, param] { return sh.proc.engine().getLayerParam (which(), param); };
        c.set = [&sh, which, param] (float v) { sh.proc.engine().setLayerParam (which(), param, v); };
    }
}

juce::String shortName (const juce::String& id, Processor& proc)
{
    if (id.isEmpty()) return {};
    Instrument i;
    if (proc.library().findLoose (id, i)) return i.name;
    return id.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false);
}

// a held chord of any instrument should arrive at about the same loudness, so a new layer neither vanishes nor takes over
float matchedLevel (const Instrument& i)
{
    if (! i.f.valid) return 0.0f;
    return juce::jlimit (-18.0f, 9.0f, std::round ((-19.0f - i.f.loudnessDb) * 2.0f) * 0.5f);
}

void headingAt (juce::Graphics& g, const juce::String& text, float x, float y, float w)
{
    g.setColour (look().accent.withAlpha (0.92f));
    g.setFont (fellCaps (18.0f));
    g.drawText (text, juce::Rectangle<float> (x, y, w, 22.0f), juce::Justification::centredLeft, true);
}
} // namespace atlas::ui
