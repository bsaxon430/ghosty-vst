// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "ParamRow.h"
#include "Look.h"
#include "Live.h"

namespace mosaic
{
// The original window's own control names (gui/samplebrain.ui), spelling included.
juce::String ParamRow::originalName (const juce::String& id)
{
    static const std::map<juce::String, juce::String> m = {
        { "ratio", "fft / mfcc" }, { "n_ratio", "freq & dynamics / freq only" }, { "fft_start", "Start" }, { "fft_end", "End" },
        { "novelty", "novelty" }, { "boredom", "boredom" }, { "stickyness", "stickyness" }, { "stretch", "search stretch" },
        { "algo", "algorithm" }, { "synapses", "num synpases" }, { "slide_error", "synaptic slide error" },
        { "tgt_block", "block size" }, { "tgt_overlap", "block overlap" }, { "tgt_window", "window shape" },
        { "autotune", "autotune" }, { "n_mix", "normalised" }, { "target_mix", "brain / target" }, { "stereo", "stereo mode" },
        { "src_block", "block size" }, { "src_overlap", "block overlap" }, { "src_window", "window shape" }, { "live_input", "use mic input" },
        { "volume", "volume" } };
    auto it = m.find (id);
    return it == m.end() ? juce::String() : it->second;
}

// The plain-words line. Where our own name for a control only repeated the original's (boredom / BOREDOM), the line
// says what the control does instead. ("novelty" keeps NOVELTY: a UI test holds that wording.)
juce::String ParamRow::plainWords (const juce::String& id)
{
    if (id == "boredom") return "HOW LONG PIECES STAY USED";
    if (id == "autotune") return "FOLLOW THE TARGET'S PITCH";
    if (id == "stereo") return "LEFT AND RIGHT APART";
    if (id == "volume") return "HOW LOUD IT PLAYS";
    return paramInfo (id).label;
}

// the original's own names for the menu items (same order, same count as ours)
const char* ParamRow::originalChoices (const juce::String& id)
{
    if (id == "tgt_window" || id == "src_window") return "dodgy|bartlett|blackman|flat top|gaussian|hamming|hann|rectangle";
    if (id == "algo") return "basic|reversed|synaptic|synaptic slide";
    return nullptr;
}

static bool isNumberBox (const juce::String& id)
{
    return id == "tgt_block" || id == "tgt_overlap" || id == "src_block" || id == "src_overlap";
}

// an on / off setting: 0 or 1, in one step
static bool isSwitch (const ParamInfo& i)
{
    return juce::exactlyEqual (i.min, 0.0f) && juce::exactlyEqual (i.max, 1.0f) && juce::exactlyEqual (i.step, 1.0f);
}

ParamRow::Style ParamRow::styleFor (const juce::String& id)
{
    const auto& i = paramInfo (id);
    if (id == "live_input") return Style::Check;
    if (i.group == Group::Ours) return Style::Added;
    if (id == "fft_start" || id == "fft_end") return Style::NumberInline;
    if (isNumberBox (id)) return Style::Number;
    if (i.choices != nullptr) return Style::Combo;
    if (isSwitch (i)) return Style::Check;
    return Style::Slider;
}

std::unique_ptr<ParamRow> ParamRow::make (Processor& p, const juce::String& id)
{
    if (p.params().getParameter (id) == nullptr) return nullptr;
    const auto& i = paramInfo (id);
    const auto st = styleFor (id);
    std::unique_ptr<ParamRow> r;
    if (st == Style::Added) r = std::make_unique<ParamRow> (p, i, st, i.label, juce::String());
    // (UNDER THE HOOD prints "fft subsection" once for its two boxes; a row on its own carries the whole name)
    else if (st == Style::NumberInline) r = std::make_unique<ParamRow> (p, i, Style::Number, "fft subsection " + originalName (id), plainWords (id));
    else r = std::make_unique<ParamRow> (p, i, st, originalName (id), plainWords (id));
    r->setComponentID ("param:" + id);
    return r;
}

// ------------------------------------------------------------------ one setting
ParamRow::ParamRow (Processor& p, const ParamInfo& i, Style s, juce::String m, juce::String sub, juce::String pre)
    : proc (p), info (i), style (s), mainText (std::move (m)), subText (std::move (sub)), prefixText (std::move (pre)), veil (look::panel)
{
    auto& vts = p.params();
    liveIdx = Engine::liveIndexOf (i.id);
    setTooltip (helpText());
    if (style == Style::Combo || (style == Style::Added && i.choices != nullptr))
    {
        combo = std::make_unique<juce::ComboBox>();
        const char* oc = originalChoices (i.id);
        combo->addItemList (juce::StringArray::fromTokens (oc != nullptr ? oc : i.choices, "|", ""), 1);
        addAndMakeVisible (*combo);
        ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (vts, i.id, *combo);
        combo->setTooltip (getTooltip());
    }
    else if (style == Style::Check || (style == Style::Added && isSwitch (i)))
    {
        toggle = std::make_unique<juce::ToggleButton>();
        addAndMakeVisible (*toggle);
        ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (vts, i.id, *toggle);
        toggle->setTooltip (getTooltip());
    }
    else if (style == Style::Number || style == Style::NumberInline)
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
        slider->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
        slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, style == Style::NumberInline ? 40 : 64, 24);
        slider->setColour (juce::Slider::textBoxTextColourId, look::ink);
        slider->setColour (juce::Slider::textBoxBackgroundColourId, look::ground);
        slider->setColour (juce::Slider::textBoxOutlineColourId, look::line);
        addAndMakeVisible (*slider);
        sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (vts, i.id, *slider);
        slider->setNumDecimalPlacesToDisplay (i.step < 1 ? 2 : 0);
        slider->setTooltip (getTooltip());
        slider->onDragStart = [&p] { p.undo().beginNewTransaction(); };
    }
    else
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, style == Style::Added ? 46 : 62, 20);
        slider->setColour (juce::Slider::textBoxTextColourId, look::ink);
        addAndMakeVisible (*slider);
        sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (vts, i.id, *slider);
        if (i.max - i.min > 2000) slider->setSkewFactorFromMidPoint (juce::String (i.id) == "slide_error" ? 1000.0 : 2000.0);
        slider->setNumDecimalPlacesToDisplay (i.step < 1 ? 2 : 0);
        slider->setTooltip (getTooltip());
        slider->onDragStart = [&p] { p.undo().beginNewTransaction(); };
    }
}

juce::String ParamRow::helpText() const { return juce::String (info.help) + "  (original: " + shownOriginal (info) + ")"; }

void ParamRow::applyTooltip()
{
    const auto tip = idleWhy.isNotEmpty() ? idleWhy + "  " + helpText() : helpText();
    setTooltip (tip);
    if (slider) slider->setTooltip (tip);
    if (combo) combo->setTooltip (tip);
    if (toggle) toggle->setTooltip (tip);
}

void ParamRow::setIdle (const juce::String& why)
{
    if (why == idleWhy) return;
    idleWhy = why;
    const bool on = why.isEmpty();
    if (slider) slider->setEnabled (on);
    if (combo) combo->setEnabled (on);
    if (toggle) toggle->setEnabled (on);
    applyTooltip();
    repaint();
}

void ParamRow::sync (const Engine& e)
{
    syncLive (e);
    const auto line = idleShort (proc, info.id, true);
    if (line != idleLine) { idleLine = line; repaint(); }
    setIdle (idleReason (proc, info.id, true));
}

void ParamRow::syncLive (const Engine& e)
{
    if (liveIdx < 0) return;
    const auto v = e.liveValue (liveIdx);
    const bool on = v.moving;
    float p = 0;
    juce::String txt;
    if (on) { txt = live::text (liveIdx, v.now); if (slider) p = (float) slider->valueToProportionOfLength (v.now); }
    if (on == liveOn && std::abs (p - liveP) < 0.002f && txt == liveText) return;
    liveOn = on; liveP = p; liveText = txt;
    if (slider) { slider->getProperties().set ("liveOn", on); slider->getProperties().set ("live", (double) p); slider->repaint(); }
    repaint();
    if (combo) { combo->getProperties().set ("liveOn", on); combo->getProperties().set ("liveText", txt); combo->resized(); combo->repaint(); }
}

int ParamRow::labelW() const
{
    const bool added = style == Style::Added;
    return juce::jmin (juce::roundToInt ((added ? 160.0f : 186.0f) * juce::jmax (1.0f, k)), getWidth() / 2);
}
int ParamRow::prefixW() const { return juce::roundToInt (38.0f * juce::jmax (0.9f, k)); }
int ParamRow::toggleW() const { return juce::roundToInt (36.0f * juce::jmax (0.85f, k)); }

void ParamRow::setScale (float s)
{
    s = juce::jlimit (0.7f, 2.2f, s);
    if (std::abs (s - k) < 0.01f) return;
    k = s;
    for (juce::Component* c : std::initializer_list<juce::Component*> { slider.get(), combo.get(), toggle.get() })
        if (c != nullptr) look::setScale (*c, k);
    if (slider && style != Style::Number && style != Style::NumberInline)
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, juce::roundToInt ((style == Style::Added ? 46.0f : 62.0f) * juce::jmax (0.85f, k)), juce::roundToInt (20.0f * juce::jmax (0.9f, k)));
    if (slider) slider->sendLookAndFeelChange();   // (the number's type follows the scale)
    resized();
    if (combo) combo->resized();
    repaint();
}

juce::Rectangle<int> ParamRow::textArea() const
{
    auto r = getLocalBounds();
    if (style == Style::Check) return r.withTrimmedLeft (toggleW() + juce::roundToInt (8.0f * k));
    return r.removeFromLeft (labelW()).withTrimmedRight (6);
}

// the name and, small under it, the plain-words line: one block of two lines, centred in the row's height
void ParamRow::twoLines (juce::Rectangle<int> area, juce::Rectangle<int>& nameAt, juce::Rectangle<int>& subAt) const
{
    const float mainH = juce::jmax (12.0f, 13.0f * k), subH = juce::jmax (11.0f, 10.5f * k);
    const float lead = getHeight() < 32 ? 1.0f : 1.12f;      // (a short row: the two lines close up, the rows stay apart)
    const int mh = juce::roundToInt (mainH * lead), sh = subText.isNotEmpty() || idleLine.isNotEmpty() ? juce::roundToInt (subH * lead) : 0;
    auto b = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), mh + sh));
    nameAt = b.removeFromTop (juce::jmin (b.getHeight(), mh));
    subAt = b;
}

void ParamRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool added = style == Style::Added;
    const float mainH = juce::jmax (12.0f, 13.0f * k), subH = juce::jmax (11.0f, 10.5f * k);
    if (style == Style::NumberInline)
    {
        g.setColour (look::ink);
        g.setFont (look::font (mainH, 1));
        g.drawText (prefixText, r.removeFromLeft (prefixW()), juce::Justification::centredLeft, false);
    }
    else
    {
        juce::Rectangle<int> nameAt, subAt;
        twoLines (textArea(), nameAt, subAt);
        g.setColour (look::ink);
        if (added) g.setFont (look::spaced (juce::jmax (11.0f, 12.0f * k), 1, 0.1f)); else g.setFont (look::font (mainH, 1));
        g.drawFittedText (mainText, nameAt, juce::Justification::centredLeft, 1, 0.72f);
        // (while the row is greyed its reason stands here instead, drawn over the veil: paintOverChildren)
        if (subText.isNotEmpty() && ! (idle() && idleLine.isNotEmpty()))
        {
            g.setColour (look::inkDim);
            g.setFont (look::spaced (subH, 0, 0.06f));
            g.drawFittedText (subText, subAt, juce::Justification::centredLeft, 1, 0.72f);
        }
    }
    if (liveOn && slider)
    {
        g.setColour (look::live);
        g.setFont (look::spaced (juce::jmax (11.0f, 10.5f * k), 1, 0.06f));
        if (style == Style::Slider || style == Style::Added)
            g.drawText ("NOW " + liveText, slider->getBounds().withY (0).withHeight (13).withTrimmedRight (70), juce::Justification::centredRight, true);
        else
            g.drawText ("NOW " + liveText, juce::Rectangle<int> (slider->getX(), slider->getBottom() - 1, slider->getWidth(), 11), juce::Justification::centredRight, true);
    }
}

// Rule 3: a setting that does nothing right now is greyed (a veil of the card's own colour over the whole row), and
// the short reason is written over the veil, in the place of the plain-words line, where it can be read
void ParamRow::paintOverChildren (juce::Graphics& g)
{
    if (idleWhy.isEmpty()) return;
    g.setColour (veil.withAlpha (0.55f));
    g.fillRect (getLocalBounds());
    if (idleLine.isEmpty() || style == Style::NumberInline) return;
    juce::Rectangle<int> nameAt, subAt;
    twoLines (textArea(), nameAt, subAt);
    g.setColour (look::ink.interpolatedWith (look::inkDim, 0.5f));
    g.setFont (look::spaced (juce::jmax (11.0f, 10.5f * k), 1, 0.06f));
    g.drawFittedText (idleLine, subAt.withRight (juce::jmax (subAt.getRight(), juce::jmin (getWidth(), subAt.getX() + juce::roundToInt (230.0f * juce::jmax (1.0f, k))))),
                      juce::Justification::centredLeft, 1, 0.72f);
}

void ParamRow::resized()
{
    auto r = getLocalBounds();
    const int h = getHeight();
    const int boxH = juce::jmin (h, juce::roundToInt (26.0f * juce::jmax (0.85f, k)));    // number boxes and menus
    const int swH = juce::jmin (h, juce::roundToInt (22.0f * juce::jmax (0.9f, k)));      // switches
    // a number box: the - and + keep their size, the number takes the rest of the box
    auto numberBox = [&] (juce::Rectangle<int> b)
    {
        const int btn = juce::roundToInt (25.0f * juce::jmax (0.85f, k));
        slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, juce::jmax (28, b.getWidth() - 2 * btn - 4), boxH);
        slider->setBounds (b.withSizeKeepingCentre (b.getWidth(), boxH));
    };
    if (style == Style::Check) { if (toggle) toggle->setBounds (r.removeFromLeft (toggleW()).withSizeKeepingCentre (toggleW(), swH)); return; }
    if (style == Style::NumberInline)
    {
        r.removeFromLeft (prefixW());
        if (slider) numberBox (r);
        return;
    }
    r.removeFromLeft (labelW());
    if (style == Style::Number) { if (slider) numberBox (r.removeFromRight (juce::jmin (juce::roundToInt (132.0f * juce::jmax (1.0f, k)), r.getWidth()))); return; }
    if (slider) slider->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (h, juce::roundToInt (26.0f * juce::jmax (0.9f, k)))));
    if (combo) combo->setBounds (r.withSizeKeepingCentre (r.getWidth(), boxH));
    if (toggle) toggle->setBounds (r.removeFromLeft (toggleW()).withSizeKeepingCentre (toggleW(), swH));
}

// ------------------------------------------------------------------ rule 3: when a setting does nothing
// The table is the engine map's (part 2, "When a control does nothing"), read from the engine's own code:
//   num synpases              only the two synaptic algorithms look at neighbours
//   synaptic slide error      only synaptic slide waits for a close match
//   stickyness                the reversed search never asks for the next piece
//   boredom                   the "used" counters are only read when novelty is above 0
//   fft subsection Start/End  with fft / mfcc at exactly 1 (all mfcc) no band is read
juce::String idleReason (Processor& p, const juce::String& id, bool orig)
{
    const auto& e = p.engine();
    // the value in use: TERRAIN's while it moves the control, otherwise the one you set
    auto inUse = [&] (const char* pid, int live) { const auto v = e.liveValue (live); return v.moving ? v.now : p.getParam (pid); };
    const int algo = juce::roundToInt (inUse ("algo", Engine::kLiveAlgo));   // 0 basic, 1 reversed, 2 synaptic, 3 synaptic slide
    const juce::String lead = "Does nothing right now: ";
    if (id == "synapses" && algo < 2)
        return lead + (orig ? "only the synaptic and synaptic slide algorithms use it." : "only the two Network search styles use it.");
    if (id == "slide_error" && algo != 3)
        return lead + (orig ? "only the synaptic slide algorithm uses it." : "only SEARCH STYLE Network + wait uses it.");
    if (id == "stickyness" && algo == 1)
        return lead + (orig ? "the reversed algorithm never uses it." : "SEARCH STYLE Most different never uses it.");
    if (id == "boredom" && ! e.liveValue (Engine::kLiveNovelty).moving && p.getParam ("novelty") < 0.005f)
        return lead + (orig ? "it only counts when novelty is above 0." : "it only counts when NOVELTY is above 0.");
    if ((id == "fft_start" || id == "fft_end") && inUse ("ratio", Engine::kLiveRatio) > 0.995f)
        return lead + (orig ? "with fft / mfcc all the way at mfcc, no frequency band is compared." : "with TONE / TIMBRE all the way at TIMBRE, no frequency band is compared.");
    return {};
}

// The same table, as the short line the row shows on screen while it is greyed.
juce::String idleShort (Processor& p, const juce::String& id, bool orig)
{
    if (idleReason (p, id, orig).isEmpty()) return {};
    const auto& e = p.engine();
    auto inUse = [&] (const char* pid, int live) { const auto v = e.liveValue (live); return v.moving ? v.now : p.getParam (pid); };
    if (id == "boredom") return "NEEDS NOVELTY ABOVE 0";
    if (id == "fft_start" || id == "fft_end") return orig ? "NOT USED AT FULL MFCC" : "NOT USED AT FULL TIMBRE";
    // the algorithm in use, by the name this page shows for it
    const int algo = juce::jlimit (0, 3, juce::roundToInt (inUse ("algo", Engine::kLiveAlgo)));
    const char* oc = ParamRow::originalChoices ("algo");
    const auto names = juce::StringArray::fromTokens (orig && oc != nullptr ? oc : paramInfo ("algo").choices, "|", "");
    return "NOT USED BY " + (algo < names.size() ? names[algo] : juce::String ("THIS ALGORITHM")).toUpperCase();
}
}
