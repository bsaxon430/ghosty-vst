// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "HoodPage.h"
#include "Look.h"
#include "Live.h"

namespace mosaic
{
// ------------------------------------------------------------------ the loaded sounds ("brain contents" list)
class HoodPage::SoundList : public juce::Component, public juce::FileDragAndDropTarget, private juce::ListBoxModel
{
public:
    explicit SoundList (Processor& p) : m_p (p)
    {
        setComponentID ("hoodSounds");
        m_list.setRowHeight (28);
        m_list.setColour (juce::ListBox::backgroundColourId, look::well);
        m_list.setColour (juce::ListBox::outlineColourId, look::line);
        m_list.setOutlineThickness (1);
        addAndMakeVisible (m_list);
    }
    void refresh() { m_sounds = m_p.engine().sounds(); m_list.updateContent(); m_list.repaint(); repaint(); }
    // GHOST POSTER: the list grows with the page: whole rows only, type and switches at the page's scale
    void setScale (float k)
    {
        m_k = k;
        const int want = juce::jmax (22, juce::roundToInt (28.0f * k));
        const int rows = juce::jmax (1, juce::roundToInt ((float) (getHeight() - 2) / (float) want));
        const int rowH = juce::jmax (20, (getHeight() - 2) / rows);
        m_list.setRowHeight (rowH);
        m_list.setBounds (0, 0, getWidth(), juce::jmin (getHeight(), rowH * rows + 2));   // (whole rows only)
        m_list.repaint();
    }
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
        const float k = m_k;
        auto r = juce::Rectangle<int> (w, h).reduced (2, 1);
        if (row % 2 == 1) { g.setColour (look::ground); g.fillRect (r); }
        const auto tint = look::sourceTint (row);
        const bool masked = m_p.engine().soundsMasked();
        const bool benched = s.enabled && masked && (row < (int) m_benched.size() ? m_benched[(size_t) row] != 0 : false);
        const bool glow = s.enabled && masked && ! benched;
        const float d = juce::jmax (8.0f, 9.0f * k);
        g.setColour (s.enabled ? (benched ? tint.withAlpha (0.3f) : tint) : look::inkFaint);
        g.fillRect ((float) r.getX() + 6, (float) r.getCentreY() - d * 0.5f, d, d);
        if (glow) { g.setColour (look::live); g.drawRect ((float) r.getX() + 4, (float) r.getCentreY() - d * 0.5f - 2, d + 4, d + 4, 1.4f); }
        auto right = r.removeFromRight (juce::roundToInt (26.0f * k));
        const float u = 3.5f * k;
        g.setColour (look::inkDim);
        g.drawLine ((float) right.getCentreX() - u, (float) right.getCentreY() - u, (float) right.getCentreX() + u, (float) right.getCentreY() + u, 1.4f * k);
        g.drawLine ((float) right.getCentreX() + u, (float) right.getCentreY() - u, (float) right.getCentreX() - u, (float) right.getCentreY() + u, 1.4f * k);
        const float sh = std::floor (juce::jmin (15.0f * k, (float) h - 6));
        auto sw = r.removeFromRight (juce::roundToInt (40.0f * k)).toFloat().withSizeKeepingCentre (std::floor (sh * 1.9f), sh);
        g.setColour (benched ? look::green.withAlpha (0.18f) : s.enabled ? look::green : look::line);
        g.fillPath (look::cutRect (sw, sh * 0.34f));
        if (glow) { g.setColour (look::live); g.strokePath (look::cutRect (sw, sh * 0.34f), juce::PathStrokeType (1.4f)); }
        g.setColour (benched ? look::inkDim : s.enabled ? look::ground : look::inkDim);
        g.fillRect (s.enabled ? sw.getRight() - (sh - 6) - 3 : sw.getX() + 3, sw.getY() + 3, sh - 6, sh - 6);
        r.removeFromLeft (juce::roundToInt (d + 14.0f));
        g.setColour (look::inkDim);
        g.setFont (look::font (juce::jmax (11.0f, 10.5f * k), 0));
        g.drawText (juce::String (s.seconds, 1) + " s", r.removeFromRight (juce::roundToInt (44.0f * k)), juce::Justification::centredRight);
        g.setColour (benched ? look::inkFaint : s.enabled ? look::ink : look::inkDim);
        g.setFont (look::font (juce::jmax (12.0f, 12.5f * k), 0));
        g.drawText (s.name, r.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) m_sounds.size()) return;
        const int w = m_list.getVisibleRowWidth();
        if (e.x > w - juce::roundToInt (28.0f * m_k)) m_p.engine().deleteSound (row);
        else if (e.x > w - juce::roundToInt (68.0f * m_k)) m_p.engine().setSoundEnabled (row, ! m_sounds[(size_t) row].enabled);
        refresh();
    }
    float m_k = 1.0f;
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
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::Colour c = m_kind == Play ? look::green : m_kind == Pause ? look::purple : m_kind == Record ? look::danger : look::band;
        if (down) c = c.darker (0.25f); else if (over) c = c.brighter (0.15f);
        const auto shape = look::cutRect (r, juce::jmin (12.0f, r.getHeight() * 0.3f));
        g.setColour (c);
        g.fillPath (shape);
        if (m_kind == Stop) { g.setColour (look::inkFaint); g.strokePath (shape, juce::PathStrokeType (1.0f)); }
        if (m_lit) { g.setColour (look::ink); g.strokePath (shape, juce::PathStrokeType (2.0f)); }   // (what it is doing now: a bright edge)
        const auto ctr = r.getCentre();
        const float u = juce::jmin (r.getWidth(), r.getHeight()) * 0.2f;
        g.setColour (m_kind == Stop || m_kind == Record ? look::ink : look::ground);
        if (m_kind == Play)
        {
            juce::Path t; t.addTriangle (ctr.x - u * 0.8f, ctr.y - u * 1.2f, ctr.x - u * 0.8f, ctr.y + u * 1.2f, ctr.x + u * 1.3f, ctr.y);
            g.fillPath (t);
        }
        else if (m_kind == Pause) { g.fillRect (ctr.x - u * 1.1f, ctr.y - u * 1.2f, u * 0.8f, u * 2.4f); g.fillRect (ctr.x + u * 0.3f, ctr.y - u * 1.2f, u * 0.8f, u * 2.4f); }
        else if (m_kind == Record) g.fillEllipse (ctr.x - u * 1.1f, ctr.y - u * 1.1f, u * 2.2f, u * 2.2f);
        else g.fillRect (ctr.x - u, ctr.y - u, u * 2, u * 2);
        if (! isEnabled()) { g.setColour (look::ground.withAlpha (0.55f)); g.fillPath (shape); }
    }
private:
    Kind m_kind; bool m_lit = false;
};

// ------------------------------------------------------------------ the page
void HoodPage::syncLive (const Engine& e)
{
    for (auto* r : m_rows) r->sync (e);      // (the marker of what TERRAIN moves, and greyed while the setting does nothing)
    // "fft subsection" is printed once, by the page, for its two boxes: it greys with them
    const bool fftIdle = row ("fft_start") != nullptr && row ("fft_start")->idle();
    if (fftIdle != m_fftIdle) { m_fftIdle = fftIdle; repaint (m_fftLabel); }
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

    auto sub = [] (const char* id) { return ParamRow::plainWords (id); };
    auto orig = [] (const char* id) { return ParamRow::originalName (id); };
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
        m_portLabel[i].setText ({}, juce::dontSendNotification);      // (the page prints the name and its plain-words line: paint)
        juce::ignoreUnused (names);
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
        m_dest[i].setTextToShowWhenEmpty ("osc.udp://address of another copy: every setting you change is sent there", look::inkDim.withAlpha (0.7f));
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
    const float k = m_rowK;
    const float mainH = juce::jmax (12.0f, 13.0f * k), subH = juce::jmax (11.0f, 10.5f * k);
    // a name with its plain-words line under it, as every row prints them (greyed: the name under a veil, and the
    // short reason in the plain-words line's place, readable)
    auto twoLines = [&] (juce::Rectangle<int> area, const juce::String& name, const juce::String& sub, juce::Colour c, bool greyed = false)
    {
        const float lead = area.getHeight() < 32 ? 1.0f : 1.12f;
        const int mh = juce::roundToInt (mainH * lead), sh = juce::roundToInt (subH * lead);
        auto b = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), mh + sh));
        g.setColour (greyed ? c.interpolatedWith (look::panel, 0.55f) : c);
        g.setFont (look::font (mainH, 1));
        g.drawFittedText (name, b.removeFromTop (juce::jmin (b.getHeight(), mh)), juce::Justification::centredLeft, 1, 0.72f);
        g.setColour (greyed ? look::ink.interpolatedWith (look::inkDim, 0.5f) : look::inkDim);
        g.setFont (look::spaced (subH, greyed ? 1 : 0, 0.06f));
        g.drawFittedText (sub, b, juce::Justification::centredLeft, 1, 0.72f);
    };
    // the tab rail: the two tabs stand on a purple line that runs the whole width
    g.setColour (look::purple);
    g.fillRect (0, m_tabSearch.getBottom(), getWidth(), 2);
    g.setColour (look::inkDim);
    g.setFont (look::spaced (juce::jmax (10.5f, 11.0f * m_k), 0, 0.14f));
    g.drawFittedText ("THE ORIGINAL'S OWN WINDOW: EVERY SETTING UNDER ITS OWN NAME, PLAIN WORDS BELOW IT",
                      juce::Rectangle<int> (m_tabNet.getRight() + 16, 0, getWidth() - m_tabNet.getRight() - 16, m_tabSearch.getHeight()), juce::Justification::centredRight, 1, 0.75f);
    for (auto& s : m_panels)
    {
        if (s.title.isEmpty())
        {
            // the bottom row: a band of its own tone
            const auto shape = look::cutRect (s.box.toFloat().reduced (0.5f), 14.0f);
            g.setColour (look::panel); g.fillPath (shape);
            g.setColour (look::line); g.strokePath (shape, juce::PathStrokeType (1.0f));
        }
        else look::zone (g, s.box, s.number, s.title, s.headH, false, s.lower);
    }
    if (! m_netTab)
    {
        // "mix": the second section of the middle column (the original's own heading): a band on the same grid, numbered like the others
        g.setColour (look::band);
        g.fillRect (m_mixLabel);
        g.setColour (look::purple);
        g.fillRect (m_mixLabel.withWidth (m_numW));
        g.setColour (look::ground);
        g.setFont (look::font (juce::jmin ((float) m_numW * 0.5f, (float) m_mixLabel.getHeight() * 0.7f), 2));
        g.drawText ("03", m_mixLabel.withWidth (m_numW), juce::Justification::centred, false);
        g.setColour (look::ink);
        g.setFont (look::font (juce::jlimit (12.0f, 26.0f, (float) m_mixLabel.getHeight() * 0.62f), 2));
        g.drawText ("mix", m_mixLabel.withTrimmedLeft (m_numW + juce::roundToInt (12.0f * m_k)), juce::Justification::centredLeft, false);
        // "fft subsection": its name, then Start and End, on ONE row, as the original has it
        twoLines (m_fftLabel, "fft subsection", m_fftIdle ? idleShort (m_proc, "fft_start", true) : juce::String ("LOWEST / HIGHEST BAND"), look::ink, m_fftIdle);
        if (m_micOff.isVisible()) twoLines (m_micLabel, "use mic input", "ONLY IN THE EFFECT VERSION (GHOST MOSAIC FX)", look::ink, true);
    }
    twoLines (m_volumeLabel, "volume", ParamRow::plainWords ("volume"), look::ink);
    if (m_netTab)
    {
        // the three ports under the original settings' own names, plain words below, like every row of the search tab
        static const char* const portName[3] = { "gui OSC port", "audio OSC port", "process OSC port" };
        static const char* const portPlain[3] = { "STATUS MESSAGES GO OUT HERE", "PLAY, STOP AND SETTINGS COME IN", "SOUNDS AND BUILDING COME IN" };
        for (int i = 0; i < 3; ++i) twoLines (m_portLabel[i].getBounds(), portName[i], portPlain[i], look::ink);
    }
}

void HoodPage::resized()
{
    m_panels.clear();
    const int W = getWidth(), H = getHeight();
    if (W < 400 || H < 300) return;
    // ---- GHOST POSTER: one grid for the whole page. Everything is on screen at every window size (nothing scrolls):
    // the page's scale comes from the room there is, the three columns share eleven equal lines, and what is left over
    // goes to the header bands, so the last card ends exactly on the body's bottom edge.
    const float k = juce::jlimit (0.8f, 1.75f, juce::jmin ((float) H / 556.0f, (float) W / 1152.0f));
    m_k = k;
    auto px = [k] (float v) { return juce::roundToInt (v * k); };
    const int g = look::gutterOf (H + 204);
    const int pad = juce::jlimit (12, 26, px (16));
    const int tabsH = juce::jlimit (22, 46, px (26));
    int headH = juce::jlimit (30, 64, px (38));
    const int inPad = juce::jlimit (3, 12, px (4));          // air above the first and under the last line of a column
    const int barH = juce::jlimit (48, 100, px (58));        // (room for "load session" above "save session")
    std::vector<Row*> added; for (auto* r : m_rows) if (r->style == Style::Added) added.push_back (r);
    const int items = (int) added.size() + 1;                // + BACK TO START
    const int addLines = items > 8 ? 3 : 2;                  // (the effect has one more setting: three lines of three)
    const int addCols = (items + addLines - 1) / addLines;
    const int addHead = juce::jlimit (18, 36, px (20)), addRow = juce::jlimit (22, 52, px (26));
    const int addH = addHead + addLines * addRow + 2 * inPad;
    constexpr int kSlots = 11;
    const int fixed = tabsH + g + headH + 2 * inPad + g + barH + g + addH;
    const int slot = juce::jmax (22, (H - fixed) / kSlots);
    headH += juce::jmax (0, H - fixed - slot * kSlots);
    const float rowK = juce::jlimit (0.8f, 1.75f, juce::jmin ((float) slot / 32.0f, (float) W / 1152.0f));
    m_rowK = rowK;
    m_numW = (int) std::floor ((float) headH * 1.05f);
    for (auto* r : m_rows) if (r->style != Style::Added) r->setScale (rowK);
    const int btnH = juce::jmin (slot - 2, juce::roundToInt (30.0f * rowK));
    const int btnInset = juce::jmax (1, (slot - btnH) / 2);

    m_tabSearch.setBounds (0, 0, px (100), tabsH);
    m_tabNet.setBounds (px (100) + 4, 0, px (80), tabsH);
    look::setScale (m_tabSearch, k); look::setScale (m_tabNet, k);
    const int top = tabsH + g;
    const int colH = headH + 2 * inPad + slot * kSlots;
    auto place = [] (Row* r, juce::Rectangle<int> b) { if (r) r->setBounds (b); };
    auto btnAt = [&] (const char* id, juce::Rectangle<int> b) { if (auto* x = button (id)) { x->setBounds (b); look::setScale (*x, rowK); } };
    auto take = [&] (juce::Rectangle<int>& c, int n = 1) { return c.removeFromTop (slot * n); };
    auto btn = [&] (juce::Rectangle<int> cell) { return cell.reduced (0, btnInset); };
    // n equal buttons on one line, the last one ending exactly on the right edge
    auto inLine = [&] (juce::Rectangle<int> line, std::initializer_list<const char*> ids)
    {
        const int n = (int) ids.size(), gap = juce::jmax (6, px (8)), w = (line.getWidth() - (n - 1) * gap) / n;
        int i = 0;
        for (auto* id : ids) { btnAt (id, i == n - 1 ? line.withLeft (line.getRight() - w) : line.withX (line.getX() + i * (w + gap)).withWidth (w)); ++i; }
    };

    if (! m_netTab)
    {
        const int colW = (W - 2 * g) / 3;
        juce::Rectangle<int> box[3] = { { 0, top, colW, colH }, { colW + g, top, W - 2 * colW - 2 * g, colH }, { W - colW, top, colW, colH } };
        m_panels.push_back ({ "brain tweaks", box[0], "01", headH });
        m_panels.push_back ({ "target sound", box[1], "02", headH });
        m_panels.push_back ({ "brain contents", box[2], "04", headH });
        auto lines = [&] (int i) { return box[i].reduced (pad, 0).withTrimmedTop (headH + inPad).withHeight (slot * kSlots); };
        // 01 brain tweaks: the original's ten rows share the column's height evenly
        {
            const auto c = lines (0);
            constexpr int kRows1 = 10;
            int at = 0;
            auto next = [&] { const int y0 = c.getY() + c.getHeight() * at / kRows1, y1 = c.getY() + c.getHeight() * (at + 1) / kRows1; ++at; return juce::Rectangle<int> (c.getX(), y0, c.getWidth(), y1 - y0); };
            place (row ("ratio"), next()); place (row ("n_ratio"), next());
            // "fft subsection": its name, Start and End on ONE row, as the original has it
            auto f = next();
            m_fftLabel = f.removeFromLeft (juce::jmin (juce::roundToInt (186.0f * juce::jmax (1.0f, rowK)), f.getWidth() * 34 / 100)).withTrimmedRight (6);
            const int fg = juce::jmax (6, px (8)), half = (f.getWidth() - fg) / 2;
            place (row ("fft_start"), f.withWidth (half)); place (row ("fft_end"), f.withLeft (f.getRight() - half));
            for (auto* id : { "novelty", "boredom", "stickyness", "stretch", "algo", "synapses", "slide_error" }) place (row (id), next());
        }
        // 02 target sound, 03 mix
        {
            auto c = lines (1);
            btnAt ("loadTarget", btn (take (c)));
            for (auto* id : { "tgt_block", "tgt_overlap", "tgt_window" }) place (row (id), take (c));
            btnAt ("generateTarget", btn (take (c)));
            auto mic = take (c);
            if (auto* r = row ("live_input")) r->setBounds (mic);
            else
            {
                const int tw = juce::roundToInt (36.0f * juce::jmax (0.85f, rowK));
                look::setScale (m_micOff, rowK);
                m_micOff.setBounds (mic.removeFromLeft (tw).withSizeKeepingCentre (tw, juce::jmin (slot, juce::roundToInt (22.0f * juce::jmax (0.9f, rowK)))));
                m_micLabel = mic.withTrimmedLeft (juce::roundToInt (8.0f * rowK));
            }
            m_mixLabel = take (c).withX (box[1].getX() + 1).withWidth (box[1].getWidth() - 2).reduced (0, juce::jmax (1, slot / 12));
            for (auto* id : { "autotune", "n_mix", "target_mix", "stereo" }) place (row (id), take (c));
        }
        // 04 brain contents
        {
            auto head = box[2].withHeight (headH).withTrimmedRight (pad);
            const int hb = juce::jmin (headH - 10, juce::roundToInt (26.0f * k));
            btnAt ("allOff", head.removeFromRight (px (60)).withSizeKeepingCentre (px (60), hb)); head.removeFromRight (6);
            btnAt ("allOn", head.removeFromRight (px (46)).withSizeKeepingCentre (px (46), hb));
            auto c = lines (2);
            m_list->setBounds (take (c, 5).reduced (0, btnInset));
            m_list->setScale (rowK);
            inLine (btn (take (c)), { "addSounds", "addFolder", "clearLibrary" });
            for (auto* id : { "src_block", "src_overlap", "src_window" }) place (row (id), take (c));
            btnAt ("generateLibrary", btn (take (c)));
            inLine (btn (take (c)), { "openLibrary", "saveLibrary" });
        }
    }
    else
    {
        // the original's net tab, the whole width of the page: the switch and the status, the three ports, the ten addresses
        juce::Rectangle<int> box (0, top, W, colH);
        m_panels.push_back ({ "net", box, "01", headH });
        auto inner = box.reduced (pad, 0).withTrimmedTop (headH + inPad).withHeight (slot * kSlots);
        const int lh = inner.getHeight() / (2 + OscBridge::kDestinations);
        const int eh = juce::jmin (lh - 2, juce::roundToInt (26.0f * juce::jmax (0.85f, rowK)));
        const auto typeFont = look::font (juce::jmax (12.0f, 13.0f * rowK), 0);
        auto t = inner.removeFromTop (lh);
        look::setScale (m_netOn, rowK);
        m_netOn.setBounds (t.removeFromLeft (px (190)).withSizeKeepingCentre (px (190), juce::jmin (lh, juce::roundToInt (24.0f * rowK))));
        m_netStatus.setFont (typeFont);
        m_netStatus.setBounds (t.withTrimmedLeft (pad));
        auto ports = inner.removeFromTop (lh);
        const int pw = (ports.getWidth() - 2 * pad) / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto c = i == 2 ? ports.withLeft (ports.getRight() - pw) : ports.withX (ports.getX() + i * (pw + pad)).withWidth (pw);
            m_portLabel[i].setFont (look::spaced (juce::jmax (11.0f, 11.0f * rowK), 1, 0.1f));
            m_portLabel[i].setBounds (c.removeFromLeft (juce::jmin (px (214), c.getWidth() * 62 / 100)));
            m_port[i].applyFontToAllText (typeFont);
            m_port[i].setBounds (c.withSizeKeepingCentre (c.getWidth(), eh));
        }
        for (int i = 0; i < OscBridge::kDestinations; ++i)
        {
            auto l = inner.removeFromTop (lh);
            look::setScale (m_destOn[i], rowK);
            m_destOn[i].setBounds (l.removeFromRight (px (44)).withSizeKeepingCentre (px (44), juce::jmin (lh, juce::roundToInt (22.0f * juce::jmax (0.9f, rowK)))));
            m_dest[i].applyFontToAllText (typeFont);
            m_dest[i].setBounds (l.withTrimmedRight (pad).withSizeKeepingCentre (l.getWidth() - pad, eh));
        }
    }

    // the bottom row on one line, the whole width: play, pause, record, stop | the volume dial | load session, save session
    auto bar = juce::Rectangle<int> (0, top + colH + g, W, barH);
    m_panels.push_back ({ "", bar, "", 0 });
    {
        auto c = bar.reduced (pad, juce::jlimit (4, 12, px (7)));
        const int gg = juce::jmax (6, px (8));
        const float unit = (float) (c.getWidth() - 6 * gg) / 11.6f;
        const int tw = (int) (unit * 1.2f), vw = (int) (unit * 2.0f);
        for (auto* t : m_transport) { t->setBounds (c.removeFromLeft (tw)); c.removeFromLeft (gg); }
        auto v = c.removeFromLeft (vw); c.removeFromLeft (gg);
        m_volume.setBounds (v.removeFromLeft (v.getHeight()));
        m_volumeLabel = v.withTrimmedLeft (px (8));
        // "load session" above "save session", right of the volume dial, as the original has them
        const int sg = juce::jmax (2, px (4)), sh = (c.getHeight() - sg) / 2;
        btnAt ("openSession", c.withHeight (sh));
        btnAt ("saveSession", c.withTop (c.getBottom() - sh));
    }
    m_originalBottom = bar.getBottom();

    // our additions, in their own strip below: a slim numbered band, then two lines of settings (three in the effect)
    {
        juce::Rectangle<int> box (0, bar.getBottom() + g, W, H - bar.getBottom() - g);
        m_panels.push_back ({ "GHOST MOSAIC (added)", box, "05", addHead, false });
        auto inner = box.withTrimmedTop (addHead + inPad).reduced (pad, 0).withHeight (addLines * addRow);
        const int cg = juce::jmax (pad, px (22)), cw = (inner.getWidth() - (addCols - 1) * cg) / addCols;
        const float addK = juce::jlimit (0.8f, 1.75f, juce::jmin ((float) addRow / 26.0f, (float) W / 1152.0f));
        for (int i = 0; i < items; ++i)
        {
            const int col = i / addLines, line = i % addLines;
            juce::Rectangle<int> cell (inner.getX() + col * (cw + cg), inner.getY() + line * addRow, cw, addRow);
            if (col == addCols - 1) cell = cell.withLeft (inner.getRight() - cw);
            if (i < (int) added.size()) { added[(size_t) i]->setScale (addK); added[(size_t) i]->setBounds (cell); }
            else
            {
                cell.setBottom (inner.getBottom());                     // (the button takes what is left of its column)
                if (cell.getHeight() > addRow) cell = cell.withSizeKeepingCentre (cell.getWidth(), addRow);
                btnAt ("restart", cell.reduced (0, juce::jmax (1, (addRow - juce::jmin (addRow - 2, juce::roundToInt (26.0f * addK))) / 2)));
            }
        }
    }
}
}
