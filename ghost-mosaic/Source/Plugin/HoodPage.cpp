// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "HoodPage.h"
#include "Look.h"
#include "Live.h"

namespace mosaic
{
namespace
{
constexpr int kRowH = 35;

// The original window's own control names (gui/samplebrain.ui), spelling included.
juce::String originalName (const juce::String& id)
{
    static const std::map<juce::String, juce::String> m = {
        { "ratio", "fft / mfcc" }, { "n_ratio", "freq & dynamics / freq only" }, { "fft_start", "Start" }, { "fft_end", "End" },
        { "novelty", "novelty" }, { "boredom", "boredom" }, { "stickyness", "stickyness" }, { "stretch", "search stretch" },
        { "algo", "algorithm" }, { "synapses", "num synpases" }, { "slide_error", "synaptic slide error" },
        { "tgt_block", "block size" }, { "tgt_overlap", "block overlap" }, { "tgt_window", "window shape" },
        { "autotune", "autotune" }, { "n_mix", "normalised" }, { "target_mix", "brain / target" }, { "stereo", "stereo mode" },
        { "src_block", "block size" }, { "src_overlap", "block overlap" }, { "src_window", "window shape" }, { "live_input", "use mic input" } };
    auto it = m.find (id);
    return it == m.end() ? juce::String() : it->second;
}

// the original's own names for the menu items (same order, same count as ours)
const char* originalChoices (const juce::String& id)
{
    if (id == "tgt_window" || id == "src_window") return "dodgy|bartlett|blackman|flat top|gaussian|hamming|hann|rectangle";
    if (id == "algo") return "basic|reversed|synaptic|synaptic slide";
    return nullptr;
}

bool isNumberBox (const juce::String& id)
{
    return id == "tgt_block" || id == "tgt_overlap" || id == "src_block" || id == "src_overlap";
}
}

// ------------------------------------------------------------------ one setting
HoodPage::Row::Row (Processor& p, const ParamInfo& i, Style s, juce::String m, juce::String sub, juce::String pre)
    : info (i), style (s), mainText (std::move (m)), subText (std::move (sub)), prefixText (std::move (pre))
{
    auto& vts = p.params();
    liveIdx = Engine::liveIndexOf (i.id);
    setTooltip (juce::String (i.help) + "  (original: " + shownOriginal (i) + ")");
    if (style == Style::Combo || (style == Style::Added && i.choices != nullptr))
    {
        combo = std::make_unique<juce::ComboBox>();
        const char* oc = originalChoices (i.id);
        combo->addItemList (juce::StringArray::fromTokens (oc != nullptr ? oc : i.choices, "|", ""), 1);
        addAndMakeVisible (*combo);
        ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (vts, i.id, *combo);
        combo->setTooltip (getTooltip());
    }
    else if (style == Style::Check || (style == Style::Added && i.min == 0 && i.max == 1 && i.step == 1))
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
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        slider->setColour (juce::Slider::textBoxTextColourId, look::ink);
        addAndMakeVisible (*slider);
        sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (vts, i.id, *slider);
        if (i.max - i.min > 2000) slider->setSkewFactorFromMidPoint (juce::String (i.id) == "slide_error" ? 1000.0 : 2000.0);
        slider->setNumDecimalPlacesToDisplay (i.step < 1 ? 2 : 0);
        slider->setTooltip (getTooltip());
        slider->onDragStart = [&p] { p.undo().beginNewTransaction(); };
    }
}

void HoodPage::Row::syncLive (const Engine& e)
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

void HoodPage::Row::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool added = style == Style::Added;
    if (style == Style::NumberInline)
    {
        g.setColour (look::ink);
        g.setFont (look::font (12.0f, 1));
        g.drawText (prefixText, r.removeFromLeft (34).withTrimmedBottom (10), juce::Justification::centredLeft, false);
    }
    else if (style == Style::Check)
    {
        auto left = r.withTrimmedLeft (42);
        g.setColour (look::ink);
        g.setFont (look::font (12.5f, 1));
        g.drawFittedText (mainText, left.removeFromTop (kRowH / 2 + 2).withTrimmedTop (2), juce::Justification::bottomLeft, 1, 0.75f);
        g.setColour (look::inkFaint);
        g.setFont (look::font (9.5f, 0));
        g.drawText (subText, left, juce::Justification::topLeft, true);
    }
    else
    {
        auto left = r.removeFromLeft (juce::jmin (added ? 170 : 186, r.getWidth() / 2));
        g.setColour (look::ink);
        if (added) g.setFont (look::spaced (12.0f, 1, 0.1f)); else g.setFont (look::font (12.5f, 1));
        auto top = left.removeFromTop (kRowH / 2 + 2).withTrimmedTop (2);
        g.drawFittedText (mainText, top, juce::Justification::bottomLeft, 1, 0.72f);
        if (subText.isNotEmpty())
        {
            g.setColour (look::inkFaint);
            g.setFont (look::spaced (9.0f, 0, 0.08f));
            g.drawText (subText, left, juce::Justification::topLeft, true);
        }
    }
    if (liveOn && slider)
    {
        g.setColour (look::live);
        g.setFont (look::spaced (10.5f, 1, 0.06f));
        if (style == Style::Slider || style == Style::Added)
            g.drawText ("NOW " + liveText, slider->getBounds().withY (0).withHeight (13).withTrimmedRight (70), juce::Justification::centredRight, true);
        else
            g.drawText ("NOW " + liveText, juce::Rectangle<int> (slider->getX(), slider->getBottom() - 1, slider->getWidth(), 11), juce::Justification::centredRight, true);
    }
}

void HoodPage::Row::resized()
{
    auto r = getLocalBounds();
    const bool added = style == Style::Added;
    if (style == Style::Check) { if (toggle) toggle->setBounds (r.removeFromLeft (38).reduced (0, 5)); return; }
    if (style == Style::NumberInline)
    {
        r.removeFromLeft (34);
        if (slider) slider->setBounds (r.removeFromLeft (juce::jmin (84, r.getWidth())).withHeight (24).translated (0, 1));
        return;
    }
    r.removeFromLeft (juce::jmin (added ? 170 : 186, r.getWidth() / 2));
    if (style == Style::Number) { if (slider) slider->setBounds (r.removeFromRight (juce::jmin (130, r.getWidth())).withHeight (24).translated (0, 1)); return; }
    if (slider) slider->setBounds (r.reduced (0, 4));
    if (combo) combo->setBounds (r.reduced (0, 5));
    if (toggle) toggle->setBounds (r.removeFromLeft (60).reduced (0, 5));
}

// ------------------------------------------------------------------ the loaded sounds ("brain contents" list)
class HoodPage::SoundList : public juce::Component, public juce::FileDragAndDropTarget, private juce::ListBoxModel
{
public:
    explicit SoundList (Processor& p) : m_p (p)
    {
        setComponentID ("hoodSounds");
        m_list.setRowHeight (28);
        m_list.setColour (juce::ListBox::backgroundColourId, look::ground.withAlpha (0.7f));
        m_list.setColour (juce::ListBox::outlineColourId, look::line);
        m_list.setOutlineThickness (1);
        addAndMakeVisible (m_list);
    }
    void refresh() { m_sounds = m_p.engine().sounds(); m_list.updateContent(); m_list.repaint(); repaint(); }
    void syncLive()
    {
        auto& e = m_p.engine();
        std::vector<char> now (m_sounds.size(), 0);
        if (e.soundsMasked()) for (size_t k = 0; k < now.size(); ++k) now[k] = e.soundBenched ((int) k) ? 1 : 0;
        if (now != m_benched || e.soundsMasked() != m_wasMasked) { m_benched = std::move (now); m_wasMasked = e.soundsMasked(); m_list.repaint(); }
    }
    void resized() override { m_list.setBounds (getLocalBounds()); }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (m_sounds.empty())
        {
            g.setColour (look::inkFaint);
            g.setFont (look::font (12.0f, 0));
            g.drawFittedText ("no sounds loaded\n(drop sounds or a folder here)", getLocalBounds().reduced (12), juce::Justification::centred, 3);
        }
        if (m_hot) { g.setColour (look::purple.withAlpha (0.18f)); g.fillRect (getLocalBounds()); }
    }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { m_hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { m_hot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override { m_hot = false; m_p.engine().addFiles (files); repaint(); }

private:
    int getNumRows() override { return (int) m_sounds.size(); }
    juce::String getTooltipForRow (int) override { return "Click the switch to use this sound or not; x takes it out (original: the sound list)."; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) m_sounds.size()) return;
        const auto& s = m_sounds[(size_t) row];
        auto r = juce::Rectangle<int> (w, h).reduced (2, 1);
        const auto tint = look::sourceTint (row);
        const bool masked = m_p.engine().soundsMasked();
        const bool benched = s.enabled && masked && (row < (int) m_benched.size() ? m_benched[(size_t) row] != 0 : false);
        const bool glow = s.enabled && masked && ! benched;
        g.setColour (s.enabled ? (benched ? tint.withAlpha (0.3f) : tint) : look::inkFaint);
        g.fillEllipse ((float) r.getX() + 6, (float) r.getCentreY() - 4.0f, 8, 8);
        if (glow) { g.setColour (look::live.withAlpha (0.8f)); g.drawEllipse ((float) r.getX() + 4, (float) r.getCentreY() - 6.0f, 12, 12, 1.4f); }
        auto right = r.removeFromRight (26);
        g.setColour (look::inkDim);
        g.drawLine ((float) right.getCentreX() - 3.5f, (float) right.getCentreY() - 3.5f, (float) right.getCentreX() + 3.5f, (float) right.getCentreY() + 3.5f, 1.4f);
        g.drawLine ((float) right.getCentreX() + 3.5f, (float) right.getCentreY() - 3.5f, (float) right.getCentreX() - 3.5f, (float) right.getCentreY() + 3.5f, 1.4f);
        auto sw = r.removeFromRight (36).withSizeKeepingCentre (28, 15).toFloat();
        g.setColour (benched ? look::green.withAlpha (0.18f) : s.enabled ? look::green.withAlpha (0.9f) : look::line);
        g.fillRoundedRectangle (sw, 7.5f);
        if (glow) { g.setColour (look::live); g.drawRoundedRectangle (sw.reduced (0.5f), 7.5f, 1.4f); }
        g.setColour (benched ? look::inkDim : s.enabled ? look::ground : look::inkDim);
        g.fillEllipse (s.enabled ? sw.getRight() - 13 : sw.getX() + 2, sw.getY() + 2, 11, 11);
        r.removeFromLeft (22);
        g.setColour (look::inkFaint);
        g.setFont (look::font (10.5f, 0));
        g.drawText (juce::String (s.seconds, 1) + " s", r.removeFromRight (44), juce::Justification::centredRight);
        g.setColour (benched ? look::inkFaint : s.enabled ? look::ink : look::inkDim);
        g.setFont (look::font (12.0f, 0));
        g.drawText (s.name, r.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) m_sounds.size()) return;
        const int w = m_list.getVisibleRowWidth();
        if (e.x > w - 28) m_p.engine().deleteSound (row);
        else if (e.x > w - 66) m_p.engine().setSoundEnabled (row, ! m_sounds[(size_t) row].enabled);
        refresh();
    }
    Processor& m_p;
    juce::ListBox m_list { "sounds", this };
    std::vector<SoundItem> m_sounds;
    std::vector<char> m_benched; bool m_wasMasked = false, m_hot = false;
};

// ------------------------------------------------------------------ the bottom row: play, pause, record, stop
class HoodPage::Transport : public juce::Button
{
public:
    enum Kind { Play, Pause, Record, Stop };
    Transport (Kind k, const juce::String& id, const juce::String& tip) : juce::Button (id), m_kind (k)
    {
        setComponentID (id);
        setTooltip (tip);
    }
    void setLit (bool b) { if (b != m_lit) { m_lit = b; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (d, d);
        juce::Colour c = m_kind == Play ? look::green : m_kind == Pause ? look::purple : m_kind == Record ? look::danger : look::panelHi;
        if (down) c = c.darker (0.25f); else if (over) c = c.brighter (0.15f);
        if (m_lit)
            for (int k = 3; k >= 1; --k) { g.setColour (c.withAlpha (0.10f * (float) (4 - k))); g.fillEllipse (r.expanded ((float) k * 2.0f)); }
        g.setColour (c);
        g.fillEllipse (r);
        if (m_kind == Stop) { g.setColour (look::ink.withAlpha (0.55f)); g.drawEllipse (r.reduced (0.5f), 1.5f); }
        const auto ctr = r.getCentre();
        const float u = d * 0.17f;
        g.setColour (m_kind == Stop || m_kind == Record ? look::ink : look::ground);
        if (m_kind == Play)
        {
            juce::Path t; t.addTriangle (ctr.x - u * 0.8f, ctr.y - u * 1.2f, ctr.x - u * 0.8f, ctr.y + u * 1.2f, ctr.x + u * 1.3f, ctr.y);
            g.fillPath (t);
        }
        else if (m_kind == Pause) { g.fillRoundedRectangle (ctr.x - u * 1.1f, ctr.y - u * 1.2f, u * 0.8f, u * 2.4f, 1.5f); g.fillRoundedRectangle (ctr.x + u * 0.3f, ctr.y - u * 1.2f, u * 0.8f, u * 2.4f, 1.5f); }
        else if (m_kind == Record) g.fillEllipse (ctr.x - u * 1.1f, ctr.y - u * 1.1f, u * 2.2f, u * 2.2f);
        else g.fillRoundedRectangle (ctr.x - u, ctr.y - u, u * 2, u * 2, 2.0f);
        if (! isEnabled()) { g.setColour (look::ground.withAlpha (0.55f)); g.fillEllipse (r); }
    }
private:
    Kind m_kind; bool m_lit = false;
};

// ------------------------------------------------------------------ the page
void HoodPage::syncLive (const Engine& e)
{
    for (auto* r : m_rows) r->syncLive (e);
    if (m_list) m_list->syncLive();
}

HoodPage::Row* HoodPage::addRow (const char* id, Style s, const juce::String& mainLabel, const juce::String& sub, const juce::String& prefix)
{
    auto* r = m_rows.add (new Row (m_proc, paramInfo (id), s, mainLabel, sub, prefix));
    r->setComponentID ("param:" + juce::String (id));
    addAndMakeVisible (r);
    return r;
}

HoodPage::Row* HoodPage::row (const char* id) const
{
    for (auto* r : m_rows) if (juce::String (id) == r->info.id) return r;
    return nullptr;
}

HoodPage::HoodPage (Processor& p, Actions a) : m_proc (p), m_actions (std::move (a))
{
    for (auto* t : { &m_tabSearch, &m_tabNet })
    {
        t->setClickingTogglesState (false);
        t->onClick = [this, t] { showTab (t == &m_tabNet); };
        addAndMakeVisible (t);
    }
    m_tabSearch.setComponentID ("hoodTabSearch");
    m_tabNet.setComponentID ("hoodTabNet");
    m_tabSearch.setTooltip ("The search settings, the target sound and the loaded sounds (the original's search tab).");
    m_tabNet.setTooltip ("Talk to other copies over the network (the original's net tab).");

    auto sub = [] (const char* id) { return juce::String (paramInfo (id).label); };
    auto orig = [] (const char* id) { return originalName (id); };
    // ---- brain tweaks
    addRow ("ratio", Style::Slider, orig ("ratio"), sub ("ratio"));
    addRow ("n_ratio", Style::Slider, orig ("n_ratio"), sub ("n_ratio"));
    addRow ("fft_start", Style::NumberInline, {}, {}, orig ("fft_start"));
    addRow ("fft_end", Style::NumberInline, {}, {}, orig ("fft_end"));
    for (auto* id : { "novelty", "boredom", "stickyness", "stretch" }) addRow (id, Style::Slider, orig (id), sub (id));
    addRow ("algo", Style::Combo, orig ("algo"), sub ("algo"));
    addRow ("synapses", Style::Slider, orig ("synapses"), sub ("synapses"));
    addRow ("slide_error", Style::Slider, orig ("slide_error"), sub ("slide_error"));
    // ---- target sound
    addRow ("tgt_block", Style::Number, orig ("tgt_block"), sub ("tgt_block"));
    addRow ("tgt_overlap", Style::Number, orig ("tgt_overlap"), sub ("tgt_overlap"));
    addRow ("tgt_window", Style::Combo, orig ("tgt_window"), sub ("tgt_window"));
    if (p.isEffect()) addRow ("live_input", Style::Check, orig ("live_input"), sub ("live_input"));
    else
    {
        m_micOff.setEnabled (false);
        m_micOff.setTooltip ("The effect version (Ghost Mosaic FX) uses the track's audio as the target, live (original: use mic input). This instrument has no audio input.");
        addAndMakeVisible (m_micOff);
    }
    // ---- mix
    for (auto* id : { "autotune", "n_mix", "target_mix" }) addRow (id, Style::Slider, orig (id), sub (id));
    addRow ("stereo", Style::Check, orig ("stereo"), sub ("stereo"));
    // ---- brain contents
    addRow ("src_block", Style::Number, orig ("src_block"), sub ("src_block"));
    addRow ("src_overlap", Style::Number, orig ("src_overlap"), sub ("src_overlap"));
    addRow ("src_window", Style::Combo, orig ("src_window"), sub ("src_window"));
    // ---- the extras we added
    for (auto& info : paramTable())
    {
        if (info.group != Group::Ours) continue;
        const juce::String id (info.id);
        if (id == "live_input" || (id == "dry" && ! p.isEffect())) continue;
        addRow (info.id, Style::Added, info.label, {});
    }

    m_list = std::make_unique<SoundList> (p);
    addAndMakeVisible (*m_list);
    auto& A = m_actions;
    addButton ("loadTarget", "load target", A.loadTarget, "Choose the sound to rebuild (original: load target).");
    addButton ("generateTarget", "(re)generate blocks", A.generateTarget, "Cut the target into pieces with the sizes above (original: (re)generate blocks).");
    addButton ("allOn", "all", A.allOn, "Use every sound (original: all).");
    addButton ("allOff", "none", A.allOff, "Switch every sound off (original: none).");
    addButton ("addSounds", "load sound", A.addSounds, "Add sound files to the library (original: load sound).");
    addButton ("addFolder", "directory", A.addFolder, "Add every sound in a folder and the folders inside it (original: directory).");
    addButton ("clearLibrary", "clear", A.clearLibrary, "Take every sound out of the library (original: clear).");
    addButton ("generateLibrary", "(re)generate brain", A.generateLibrary, "Cut the sounds into pieces and analyse them, with the sizes above (original: (re)generate library).");
    addButton ("openLibrary", "load brain", A.openLibrary, "Open a library file saved by Ghost Mosaic or the original app (original: load library).");
    addButton ("saveLibrary", "save brain", A.saveLibrary, "Save the library: sounds, pieces and network, as a file the original app opens too (original: save library).");
    addButton ("openSession", "load session", A.openSession, "Open a whole session: library, target and settings (original: load session).");
    addButton ("saveSession", "save session", A.saveSession, "Save everything as a session file, which the original app opens too (original: save session).");
    addButton ("restart", "BACK TO START", A.restart, "Play the target from the top (original: restart audio).");
    for (auto* id : { "allOn", "allOff" }) button (id)->getProperties().set ("small", true);

    auto tb = [this] (Transport::Kind k, const juce::String& id, const juce::String& tip, std::function<void()> fn)
    {
        auto* t = m_transport.add (new Transport (k, id, tip));
        t->onClick = [fn] { if (fn) fn(); };
        addAndMakeVisible (t);
    };
    tb (Transport::Play, "play", "Play (original: play).", A.play);
    tb (Transport::Pause, "pause", "Pause (original: pause).", A.pause);
    tb (Transport::Record, "record", "Record what you hear to a WAV in Documents/GhostBlock/Ghost Mosaic/Recordings (original: record).", A.record);
    tb (Transport::Stop, "stopRecord", "Stop: ends the recording (kept) and pauses (original: stop).", A.stopRecord);
    m_volume.setComponentID ("hoodVolume");
    m_volume.setTooltip ("Output level: the same control as VOLUME on the main screen (original: the volume dial).");
    m_volume.setDoubleClickReturnValue (true, paramInfo ("volume").def);
    addAndMakeVisible (m_volume);
    m_volumeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params(), "volume", m_volume);
    m_volume.onDragStart = [&p] { p.undo().beginNewTransaction(); };

    buildNetwork();
    showTab (false);
}

HoodPage::~HoodPage() = default;

juce::TextButton& HoodPage::addButton (const juce::String& id, const juce::String& text, std::function<void()> fn, const juce::String& tip)
{
    auto* b = m_buttons.add (new juce::TextButton (text));
    b->setComponentID (id);
    b->setTooltip (tip);
    b->onClick = [fn] { if (fn) fn(); };
    addAndMakeVisible (b);
    return *b;
}

juce::Button* HoodPage::button (const juce::String& id) const
{
    for (auto* b : m_buttons) if (b->getComponentID() == id) return b;
    for (auto* b : m_transport) if (b->getComponentID() == id) return b;
    return nullptr;
}

bool HoodPage::hasControlFor (const juce::String& id) const
{
    if (id == "volume") return m_volume.isShowing() || m_volume.getParentComponent() == this;
    for (auto* r : m_rows) if (id == r->info.id && (r->slider || r->combo || r->toggle)) return true;
    return false;
}

juce::String HoodPage::originalLabel (const juce::String& id) const
{
    auto* r = row (id.toRawUTF8());
    if (r == nullptr) return {};
    return r->style == Style::NumberInline ? r->prefixText : r->mainText;
}

juce::Component* HoodPage::control (const juce::String& id) const
{
    if (id == "volume") return const_cast<juce::Slider*> (&m_volume);
    if (auto* r = row (id.toRawUTF8()))
    {
        if (r->slider) return r->slider.get();
        if (r->combo) return r->combo.get();
        if (r->toggle) return r->toggle.get();
    }
    return nullptr;
}

void HoodPage::showTab (bool net)
{
    m_netTab = net;
    m_tabSearch.setToggleState (! net, juce::dontSendNotification);
    m_tabNet.setToggleState (net, juce::dontSendNotification);
    for (auto* r : m_rows) r->setVisible (r->style == Style::Added || ! net);
    for (auto* b : m_buttons)
    {
        const auto id = b->getComponentID();
        b->setVisible (! net || id == "openSession" || id == "saveSession" || id == "restart");
    }
    m_list->setVisible (! net);
    m_micOff.setVisible (! net);
    m_netOn.setVisible (net); m_netStatus.setVisible (net);
    for (auto& l : m_portLabel) l.setVisible (net);
    for (auto& e : m_port) e.setVisible (net);
    for (auto& e : m_dest) e.setVisible (net);
    for (auto& t : m_destOn) t.setVisible (net);
    resized();
    repaint();
}

void HoodPage::buildNetwork()
{
    auto net = m_proc.osc().net();
    m_netOn.setToggleState ((bool) net["on"], juce::dontSendNotification);
    m_netOn.setTooltip ("Talk to other copies of Ghost Mosaic (or the original app) over the network (OSC), as the original's net tab. Off unless you switch it on.");
    m_netOn.onClick = [this] { applyNetwork(); };
    addAndMakeVisible (m_netOn);
    const char* names[3] = { "STATUS OUT", "SOUND IN", "BUILD IN" };
    const char* keys[3] = { "gui_port", "audio_port", "process_port" };
    const char* tips[3] = { "Where status messages go (original settings: gui OSC port).",
                            "Port for play, stop and every search setting (original settings: audio OSC port).",
                            "Port for sounds, sizes, building and files (original settings: process OSC port)." };
    for (int i = 0; i < 3; ++i)
    {
        m_portLabel[i].setText (names[i], juce::dontSendNotification);
        m_portLabel[i].setFont (look::spaced (11.0f, 1, 0.1f));
        m_portLabel[i].setColour (juce::Label::textColourId, look::inkDim);
        addAndMakeVisible (m_portLabel[i]);
        m_port[i].setText (net[keys[i]].toString(), false);
        m_port[i].setInputRestrictions (5, "0123456789");
        m_port[i].setTooltip (tips[i]);
        m_port[i].setFont (look::font (13.0f, 0));
        m_port[i].onReturnKey = [this] { applyNetwork(); };
        m_port[i].onFocusLost = [this] { applyNetwork(); };
        addAndMakeVisible (m_port[i]);
    }
    for (int i = 0; i < OscBridge::kDestinations; ++i)
    {
        m_dest[i].setText (net["dest" + juce::String (i)].toString(), false);
        m_dest[i].setTextToShowWhenEmpty ("osc.udp://address", look::inkFaint);
        m_dest[i].setFont (look::font (13.0f, 0));
        m_dest[i].setTooltip ("Another copy to send every setting you change to (the original's net tab).");
        m_dest[i].onReturnKey = [this] { applyNetwork(); };
        m_dest[i].onFocusLost = [this] { applyNetwork(); };
        addAndMakeVisible (m_dest[i]);
        m_destOn[i].setToggleState ((bool) net["dest_on" + juce::String (i)], juce::dontSendNotification);
        m_destOn[i].onClick = [this] { applyNetwork(); };
        addAndMakeVisible (m_destOn[i]);
    }
    m_netStatus.setFont (look::font (12.0f, 0));
    m_netStatus.setColour (juce::Label::textColourId, look::inkDim);
    addAndMakeVisible (m_netStatus);
}

void HoodPage::applyNetwork()
{
    auto net = m_proc.osc().net();
    net.setProperty ("on", m_netOn.getToggleState(), nullptr);
    const char* keys[3] = { "gui_port", "audio_port", "process_port" };
    for (int i = 0; i < 3; ++i) net.setProperty (keys[i], juce::jlimit (1024, 65535, m_port[i].getText().getIntValue()), nullptr);
    for (int i = 0; i < OscBridge::kDestinations; ++i)
    {
        net.setProperty ("dest" + juce::String (i), m_dest[i].getText().trim(), nullptr);
        net.setProperty ("dest_on" + juce::String (i), m_destOn[i].getToggleState(), nullptr);
    }
    m_proc.osc().apply();
    refresh();
}

void HoodPage::refreshSounds() { if (m_list) m_list->refresh(); }

void HoodPage::refresh()
{
    m_netStatus.setText ("Network: " + m_proc.osc().statusText(), juce::dontSendNotification);
    auto& e = m_proc.engine();
    for (auto* t : m_transport)
    {
        const auto id = t->getComponentID();
        t->setLit ((id == "play" && e.isPlaying() && ! e.isRecording()) || (id == "pause" && ! e.isPlaying()) || (id == "record" && e.isRecording()));
    }
    refreshSounds();
}

void HoodPage::paint (juce::Graphics& g)
{
    for (auto& s : m_panels)
    {
        look::card (g, s.box.toFloat());
        if (s.titleSize >= 18.0f)
        {
            g.setColour (look::ink);
            g.setFont (look::font (s.titleSize, 2));
            g.drawText (s.title, s.box.reduced (16, 0).withHeight (40).translated (0, 6), juce::Justification::centredLeft, true);
        }
        else look::title (g, s.title, s.box.reduced (16, 0).withHeight (38).translated (0, 4), look::purple.brighter (0.2f), 12.5f);
    }
    if (! m_netTab)
    {
        g.setColour (look::ink);
        g.setFont (look::font (22.0f, 2));
        g.drawText ("mix", m_mixLabel, juce::Justification::centredLeft, false);
        g.setFont (look::font (12.5f, 1));
        g.drawText ("fft subsection", m_fftLabel.withHeight (kRowH / 2 + 2).translated (0, 2), juce::Justification::bottomLeft, false);
        g.setColour (look::inkFaint);
        g.setFont (look::spaced (9.0f, 0, 0.08f));
        g.drawText ("LOWEST / HIGHEST BAND", m_fftLabel.withTrimmedTop (kRowH / 2 + 2), juce::Justification::topLeft, false);
        if (m_micOff.isVisible())
        {
            auto r = m_micOff.getBounds().withTrimmedLeft (42).withWidth (200);
            g.setColour (look::inkFaint);
            g.setFont (look::font (12.5f, 1));
            g.drawText ("use mic input", r, juce::Justification::centredLeft, false);
        }
    }
    g.setColour (look::inkDim);
    g.setFont (look::spaced (9.5f, 1, 0.12f));
    g.drawText ("volume", m_volumeLabel, juce::Justification::centredLeft, false);
}

void HoodPage::resized()
{
    m_panels.clear();
    const int W = getWidth(), gap = 14, pad = 16, Hc = 440;
    m_tabSearch.setBounds (0, 0, 92, 28);
    m_tabNet.setBounds (98, 0, 72, 28);
    const int top = 34;
    auto place = [] (Row* r, juce::Rectangle<int> b) { if (r) r->setBounds (b); };
    auto rowAt = [&] (const char* id, juce::Rectangle<int>& col) { place (row (id), col.removeFromTop (kRowH)); };
    auto btnAt = [&] (const char* id, juce::Rectangle<int> b) { if (auto* x = button (id)) x->setBounds (b); };

    if (! m_netTab)
    {
        const int colW = (W - 2 * gap) / 3;
        juce::Rectangle<int> box[3];
        for (int i = 0; i < 3; ++i) box[i] = { i * (colW + gap), top, colW, Hc };
        m_panels.push_back ({ "brain tweaks", box[0] });
        m_panels.push_back ({ "target sound", box[1] });
        m_panels.push_back ({ "brain contents", box[2] });
        // brain tweaks
        {
            auto c = box[0].reduced (pad, 0).withTrimmedTop (54);
            rowAt ("ratio", c); rowAt ("n_ratio", c);
            auto f = c.removeFromTop (kRowH);
            const int lw = 112;
            m_fftLabel = f.removeFromLeft (lw);
            const int half = f.getWidth() / 2;
            place (row ("fft_start"), f.removeFromLeft (half)); place (row ("fft_end"), f);
            for (auto* id : { "novelty", "boredom", "stickyness", "stretch", "algo", "synapses", "slide_error" }) rowAt (id, c);
        }
        // target sound + mix
        {
            auto c = box[1].reduced (pad, 0).withTrimmedTop (52);
            btnAt ("loadTarget", c.removeFromTop (30)); c.removeFromTop (6);
            for (auto* id : { "tgt_block", "tgt_overlap", "tgt_window" }) rowAt (id, c);
            btnAt ("generateTarget", c.removeFromTop (30)); c.removeFromTop (6);
            auto mic = c.removeFromTop (28);
            if (auto* r = row ("live_input")) r->setBounds (mic); else m_micOff.setBounds (mic.removeFromLeft (38).reduced (0, 5));
            c.removeFromTop (8);
            m_mixLabel = c.removeFromTop (30);
            for (auto* id : { "autotune", "n_mix", "target_mix", "stereo" }) rowAt (id, c);
        }
        // brain contents
        {
            auto head = box[2].reduced (pad, 0).withHeight (46).translated (0, 6);
            btnAt ("allOff", head.removeFromRight (54).withSizeKeepingCentre (54, 24)); head.removeFromRight (6);
            btnAt ("allOn", head.removeFromRight (40).withSizeKeepingCentre (40, 24));
            auto c = box[2].reduced (pad, 0).withTrimmedTop (52);
            m_list->setBounds (c.removeFromTop (146)); c.removeFromTop (8);
            auto b3 = c.removeFromTop (30); c.removeFromTop (8);
            const int w3 = (b3.getWidth() - 12) / 3;
            btnAt ("addSounds", b3.removeFromLeft (w3)); b3.removeFromLeft (6);
            btnAt ("addFolder", b3.removeFromLeft (w3)); b3.removeFromLeft (6);
            btnAt ("clearLibrary", b3);
            for (auto* id : { "src_block", "src_overlap", "src_window" }) rowAt (id, c);
            btnAt ("generateLibrary", c.removeFromTop (30)); c.removeFromTop (6);
            auto l = c.removeFromTop (30);
            btnAt ("openLibrary", l.removeFromLeft (l.getWidth() / 2 - 3)); l.removeFromLeft (6);
            btnAt ("saveLibrary", l);
        }
    }
    else
    {
        // the original's net tab
        const int lh = 30;
        juce::Rectangle<int> box (0, top, juce::jmin (W, 680), Hc);
        m_panels.push_back ({ "net", box });
        auto inner = box.reduced (pad, 0).withTrimmedTop (54);
        auto t = inner.removeFromTop (lh);
        m_netOn.setBounds (t.removeFromLeft (170));
        inner.removeFromTop (6);
        auto ports = inner.removeFromTop (lh);
        const int pw = ports.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto c = ports.removeFromLeft (pw).reduced (2, 0);
            m_portLabel[i].setBounds (c.removeFromLeft (juce::jmin (84, c.getWidth() / 2)));
            m_port[i].setBounds (c.reduced (0, 3));
        }
        inner.removeFromTop (8);
        for (int i = 0; i < OscBridge::kDestinations; ++i)
        {
            auto l = inner.removeFromTop (lh - 2);
            m_destOn[i].setBounds (l.removeFromRight (48).reduced (0, 4));
            m_dest[i].setBounds (l.reduced (0, 2).withTrimmedRight (6));
        }
        inner.removeFromTop (8);
        m_netStatus.setBounds (inner.removeFromTop (24));
    }

    // the bottom row: play, pause, record, stop, the volume dial, load session, save session
    auto bar = juce::Rectangle<int> (0, top + Hc + 8, W, 68);
    m_panels.push_back ({ "", bar, 0.0f });
    {
        auto c = bar.reduced (pad, 8);
        for (auto* t : m_transport) { t->setBounds (c.removeFromLeft (52)); c.removeFromLeft (12); }
        c.removeFromLeft (28);
        m_volume.setBounds (c.removeFromLeft (52));
        c.removeFromLeft (6);
        m_volumeLabel = c.removeFromLeft (54).withSizeKeepingCentre (54, 16);
        c.removeFromLeft (14);
        auto s = c.removeFromLeft (160);
        btnAt ("openSession", s.removeFromTop (26)); s.removeFromTop (4);
        btnAt ("saveSession", s.removeFromTop (26));
    }
    m_originalBottom = bar.getBottom();

    // our additions, in their own strip below
    {
        std::vector<Row*> added; for (auto* r : m_rows) if (r->style == Style::Added) added.push_back (r);
        const int items = (int) added.size() + 1;           // + BACK TO START
        const int perCol = (items + 2) / 3;
        const int h = 46 + perCol * 40 + 10;
        juce::Rectangle<int> box (0, bar.getBottom() + 14, W, h);
        m_panels.push_back ({ "GHOST MOSAIC (added)", box, 12.5f });
        auto inner = box.reduced (pad, 0).withTrimmedTop (44).withTrimmedBottom (10);
        const int cw = (inner.getWidth() - 2 * 20) / 3;
        for (int k = 0; k < items; ++k)
        {
            juce::Rectangle<int> cell (inner.getX() + (k / perCol) * (cw + 20), inner.getY() + (k % perCol) * 40, cw, 40);
            if (k < (int) added.size()) added[(size_t) k]->setBounds (cell);
            else btnAt ("restart", cell.withSizeKeepingCentre (juce::jmin (cw, 180), 30).withX (cell.getX()));
        }
        const int bottom = box.getBottom() + 8;
        if (bottom != getHeight()) setSize (getWidth(), bottom);
    }
}
}
