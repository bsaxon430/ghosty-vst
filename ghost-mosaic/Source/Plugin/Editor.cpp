// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Editor.h"
#include "Live.h"
#include "DemoKits.h"

namespace mosaic
{
static const juce::String kSoundTypes = "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF;*.flac;*.FLAC;*.mp3;*.MP3;*.ogg;*.OGG";

static void pill (juce::Button& b, const juce::String& id, const juce::String& tip)
{
    b.setComponentID (id);
    b.setTooltip (tip);
}

// ================================================================== SOURCE SOUNDS
SourcePanel::SourcePanel (Editor& e) : m_ed (e)
{
    pill (m_add, "srcAdd", "Add sound files to the library. Or drop them anywhere on this card.");
    pill (m_folder, "srcFolder", "Add every sound in a folder (and the folders inside it).");
    pill (m_kits, "srcKits", "Add a set of built-in sounds (made by Ghost Mosaic itself, free to use).");
    pill (m_all, "srcAll", "Use every sound.");
    pill (m_none, "srcNone", "Switch every sound off (they stay in the list).");
    pill (m_clear, "srcClear", "Take every sound out of the library.");
    m_add.onClick = [this] { m_ed.chooseSounds (false); };
    m_folder.onClick = [this] { m_ed.chooseSounds (true); };
    m_kits.onClick = [this] { m_ed.showKitMenu (m_kits); };
    m_all.onClick = [this] { m_ed.proc().engine().setAllEnabled (true); };
    m_none.onClick = [this] { m_ed.proc().engine().setAllEnabled (false); };
    m_clear.onClick = [this] { m_ed.proc().engine().clearSounds(); };
    for (auto* b : { &m_add, &m_folder, &m_kits, &m_all, &m_none, &m_clear }) addAndMakeVisible (b);
    m_list.setRowHeight (34);
    m_list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    m_list.setComponentID ("srcList");
    addAndMakeVisible (m_list);
}

void SourcePanel::refresh()
{
    m_sounds = m_ed.proc().engine().sounds();
    m_list.updateContent();
    m_list.repaint();
    repaint();
}

void SourcePanel::filesDropped (const juce::StringArray& files, int, int)
{
    m_hot = false;
    m_ed.proc().engine().addFiles (files);
    repaint();
}

// GHOST POSTER: the PLAY page's own scale (a tall window gets bigger rows, type and buttons)
static float playScale (int windowH) { return juce::jlimit (1.0f, 1.5f, (float) windowH / 760.0f); }
// ...and the frame's (page buttons, status, bottom bar): only a window that is big both ways gets bigger type
static float frameScale (int w, int h) { return juce::jlimit (1.0f, 1.5f, juce::jmin ((float) h / 760.0f, (float) w / 1200.0f)); }

void SourcePanel::paint (juce::Graphics& g)
{
    const int hh = look::headOf (m_ed.getHeight());
    look::zone (g, getLocalBounds(), "01", "SOURCE SOUNDS", hh, m_hot);
    double secs = 0; int on = 0;
    for (auto& s : m_sounds) { secs += s.seconds; on += s.enabled ? 1 : 0; }
    g.setColour (look::inkDim);
    g.setFont (look::font (12.0f * playScale (m_ed.getHeight()), 0));
    if (! m_sounds.empty())
        g.drawText (juce::String (on) + " of " + juce::String ((int) m_sounds.size()) + "  -  " + juce::String (secs, 1) + " s",
                    getLocalBounds().withHeight (hh).withTrimmedRight (hh / 2 + 6).withTrimmedLeft (getWidth() / 2), juce::Justification::centredRight);
}

void SourcePanel::paintOverChildren (juce::Graphics& g)
{
    if (m_sounds.empty())
    {
        auto r = m_list.getBounds().reduced (10);
        g.setColour (look::line);
        const float dash[] = { 6, 5 };
        juce::Path p; p.addRoundedRectangle (r.toFloat(), 10);
        juce::Path d; juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);
        g.setColour (look::inkDim);
        g.setFont (look::spaced (13.0f, 1, 0.1f));
        g.drawFittedText ("DROP SOUNDS HERE", r.withTrimmedBottom (r.getHeight() / 2), juce::Justification::centredBottom, 1);
        g.setColour (look::inkFaint);
        g.setFont (look::font (12.0f, 0));
        g.drawFittedText ("WAV, AIFF, FLAC, MP3, OGG or a whole folder.\nThe target gets rebuilt out of little pieces of these.",
                          r.withTrimmedTop (r.getHeight() / 2 + 6).reduced (14, 0), juce::Justification::centredTop, 3);
    }
    if (m_hot)
    {
        g.setColour (look::purple.withAlpha (0.16f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12);
        g.setColour (look::ink);
        g.setFont (look::spaced (15.0f, 2, 0.16f));
        g.drawText ("DROP TO ADD", getLocalBounds(), juce::Justification::centred);
    }
}

void SourcePanel::resized()
{
    const int hh = look::headOf (m_ed.getHeight());
    const float k = playScale (m_ed.getHeight());
    const int bh = juce::roundToInt (30.0f * k), gap = 8, pad = 14;
    auto r = getLocalBounds().reduced (pad, 0).withTrimmedTop (hh + 10).withTrimmedBottom (12);
    auto three = [&] (juce::Rectangle<int> row, juce::Button& a, juce::Button& b, juce::Button& c)
    {
        const int w3 = (row.getWidth() - 2 * gap) / 3;
        a.setBounds (row.withWidth (w3)); b.setBounds (row.withX (row.getX() + w3 + gap).withWidth (row.getWidth() - 2 * w3 - 2 * gap)); c.setBounds (row.withLeft (row.getRight() - w3));
        for (auto* x : { &a, &b, &c }) look::setScale (*x, k);
    };
    three (r.removeFromTop (bh), m_add, m_folder, m_kits);
    three (r.removeFromBottom (bh), m_all, m_none, m_clear);
    auto list = r.reduced (0, gap);
    // whole rows only: the list never ends on half a sound
    const int want = juce::roundToInt (34.0f * k), rows = juce::jmax (1, juce::roundToInt ((float) list.getHeight() / (float) want));
    const int rowH = juce::jmax (24, list.getHeight() / rows);
    m_list.setRowHeight (rowH);
    m_list.setBounds (list.withSizeKeepingCentre (list.getWidth(), juce::jmin (list.getHeight(), rowH * rows)));
}

void SourcePanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) m_sounds.size()) return;
    const auto& s = m_sounds[(size_t) row];
    auto r = juce::Rectangle<int> (w, h).reduced (0, 2);
    const float k = playScale (m_ed.getHeight());
    g.setColour (look::well);
    g.fillRect (r);
    const auto tint = look::sourceTint (row);
    g.setColour (s.enabled ? tint : look::inkFaint);      // the sound's own colour: a bar down the row's left edge
    g.fillRect (r.withWidth (4));
    // TERRAIN keeps some sounds out of play (your own switch is untouched): they dim and their switch goes hollow; the ones in play glow
    const bool masked = m_ed.proc().engine().soundsMasked();
    const bool benched = s.enabled && masked && (row < (int) m_benched.size() ? m_benched[(size_t) row] != 0 : false);
    const bool glow = s.enabled && masked && ! benched;
    g.setColour (s.enabled ? (benched ? tint.withAlpha (0.3f) : tint) : look::inkFaint);
    g.fillRect ((float) r.getX() + 12, (float) r.getCentreY() - 4.5f, 9.0f, 9.0f);
    if (glow) { g.setColour (look::live); g.drawRect ((float) r.getX() + 9.5f, (float) r.getCentreY() - 7.0f, 14.0f, 14.0f, 1.5f); }
    // the switch and the x
    auto right = r.removeFromRight (30);
    g.setColour (look::inkDim);
    g.drawLine ((float) right.getCentreX() - 4, (float) right.getCentreY() - 4, (float) right.getCentreX() + 4, (float) right.getCentreY() + 4, 1.5f);
    g.drawLine ((float) right.getCentreX() + 4, (float) right.getCentreY() - 4, (float) right.getCentreX() - 4, (float) right.getCentreY() + 4, 1.5f);
    auto sw = r.removeFromRight (40).withSizeKeepingCentre (32, 16).toFloat();
    const auto swShape = look::cutRect (sw, 5.5f);
    g.setColour (benched ? look::green.withAlpha (0.18f) : s.enabled ? look::green : look::line);
    g.fillPath (swShape);
    if (benched) { g.setColour (look::green.withAlpha (0.4f)); g.strokePath (swShape, juce::PathStrokeType (1.0f)); }
    if (glow) { g.setColour (look::live); g.strokePath (swShape, juce::PathStrokeType (1.5f)); }
    g.setColour (benched ? look::inkDim : s.enabled ? look::ground : look::inkDim);
    g.fillRect (s.enabled ? sw.getRight() - 13 : sw.getX() + 3, sw.getY() + 3, 10.0f, 10.0f);
    r.removeFromLeft (30);
    g.setColour (look::inkDim);
    g.setFont (look::font (11.5f * k, 0));
    g.drawText (juce::String (s.seconds, 1) + " s", r.removeFromRight (juce::roundToInt (48.0f * k)), juce::Justification::centredRight);
    g.setColour (benched ? look::inkFaint : s.enabled ? look::ink : look::inkDim);
    g.setFont (look::font (13.0f * k, 1));
    g.drawText (s.name, r.withTrimmedRight (8), juce::Justification::centredLeft, true);
}

void SourcePanel::syncLive()
{
    auto& e = m_ed.proc().engine();
    std::vector<char> now (m_sounds.size(), 0);
    if (e.soundsMasked()) for (size_t k = 0; k < now.size(); ++k) now[k] = e.soundBenched ((int) k) ? 1 : 0;
    if (now != m_benched || e.soundsMasked() != m_wasMasked) { m_benched = std::move(now); m_wasMasked = e.soundsMasked(); m_list.repaint(); }
}

void SourcePanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) m_sounds.size()) return;
    const int w = m_list.getVisibleRowWidth();
    if (e.x > w - 30) m_ed.proc().engine().deleteSound (row);
    else if (e.x > w - 70) m_ed.proc().engine().setSoundEnabled (row, ! m_sounds[(size_t) row].enabled);
    refresh();
}

// ================================================================== TARGET
TargetPanel::TargetPanel (Editor& e) : m_ed (e)
{
    pill (m_load, "tgtLoad", "Choose the sound to rebuild. Or drop it on this card.");
    pill (m_demo, "tgtDemo", "Use a built-in target (made by Ghost Mosaic itself).");
    pill (m_play, "tgtPlay", "Play or stop (space bar in the standalone app).");
    pill (m_rec, "tgtRec", "Record what you hear to a WAV; press again to stop. Then DRAG WAV drags the recording.");
    m_load.onClick = [this] { m_ed.chooseTarget(); };
    m_demo.onClick = [this] { m_ed.showDemoTargetMenu (m_demo); };
    m_play.onClick = [this] { m_ed.togglePlay(); };
    m_rec.onClick = [this] { m_ed.toggleRecord(); };
    for (auto* b : { &m_load, &m_demo, &m_play, &m_rec }) addAndMakeVisible (b);
    if (m_ed.proc().isEffect())
    {
        m_live.setComponentID ("tgtLive");
        m_live.setTooltip ("On: your track's audio is the target, live. Off: rebuild a sound you load here instead.");
        m_liveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (m_ed.proc().params(), "live_input", m_live);
        addAndMakeVisible (m_live);
    }
}

void TargetPanel::refresh() { m_pic = m_ed.proc().engine().targetPicture(); repaint(); }

void TargetPanel::tick()
{
    const int c = m_ed.proc().engine().currentTargetBlock();
    if (c != m_cur) { m_cur = c; repaint (waveArea()); }
    m_play.setButtonText (m_ed.proc().engine().isPlaying() ? "STOP" : "PLAY");
    m_rec.setToggleState (m_ed.proc().engine().isRecording(), juce::dontSendNotification);
    m_rec.setButtonText (m_ed.proc().engine().isRecording() ? "STOP REC" : "RECORD");
}

juce::Rectangle<int> TargetPanel::waveArea() const
{
    const int bh = juce::roundToInt (30.0f * playScale (m_ed.getHeight()));
    return getLocalBounds().reduced (14, 0).withTrimmedTop (look::headOf (m_ed.getHeight()) + 10).withTrimmedBottom (bh + 12 + 8);
}

void TargetPanel::filesDropped (const juce::StringArray& files, int, int)
{
    m_hot = false;
    if (! files.isEmpty()) m_ed.proc().engine().setTargetFile (juce::File (files[0]));
    if (m_ed.proc().isEffect()) m_ed.proc().setParam ("live_input", 0);
    repaint();
}

void TargetPanel::mouseDown (const juce::MouseEvent& e)
{
    if (waveArea().contains (e.getPosition())) m_ed.proc().engine().restart();
}

void TargetPanel::paint (juce::Graphics& g)
{
    const int hh = look::headOf (m_ed.getHeight());
    look::zone (g, getLocalBounds(), "02", "TARGET", hh, m_hot);
    auto top = getLocalBounds().withHeight (hh).withTrimmedRight (hh / 2 + 6).withTrimmedLeft (getWidth() * 2 / 5);
    const bool live = m_ed.proc().isEffect() && m_ed.proc().getParam ("live_input") > 0.5f;
    auto wave = waveArea();
    g.setColour (look::well);
    g.fillRect (wave);
    if (live)
    {
        g.setColour (look::green);
        g.setFont (look::spaced (13.0f, 1, 0.12f));
        g.drawText ("YOUR TRACK, LIVE", wave.withTrimmedBottom (wave.getHeight() / 2), juce::Justification::centredBottom);
        g.setColour (look::inkDim);
        g.setFont (look::font (12.0f, 0));
        const double ms = 1000.0 * m_ed.proc().engine().latencySamples() / juce::jmax (1.0, m_ed.proc().engine().sampleRate());
        g.drawFittedText ("Whatever plays on this track is rebuilt from your sounds.\nDelay " + juce::String (juce::roundToInt (ms)) + " ms (your DAW lines it up).",
                          wave.withTrimmedTop (wave.getHeight() / 2 + 4).reduced (10, 0), juce::Justification::centredTop, 2);
        return;
    }
    if (m_pic == nullptr || m_pic->numBlocks == 0)
    {
        g.setColour (look::inkDim);
        g.setFont (look::spaced (12.5f, 1, 0.1f));
        g.drawFittedText ("DROP THE SOUND TO REBUILD", wave.reduced (10), juce::Justification::centred, 2);
        return;
    }
    g.setColour (look::inkDim);
    g.setFont (look::font (12.0f * playScale (m_ed.getHeight()), 0));
    if (! m_live.isVisible()) g.drawText (m_pic->name + "  -  " + juce::String (m_pic->seconds, 1) + " s", top, juce::Justification::centredRight, true);
    else g.drawText (m_pic->name + "  -  " + juce::String (m_pic->seconds, 1) + " s", wave.reduced (8, 4), juce::Justification::topRight, true);
    const int n = m_pic->numBlocks;
    auto r = wave.reduced (8, 10).toFloat();
    const float bw = r.getWidth() / (float) n;
    float mx = 0.0001f;
    for (float p : m_pic->peaks) mx = juce::jmax (mx, p);
    const int cur = m_cur;
    for (int i = 0; i < n; ++i)
    {
        const float h = juce::jmax (1.0f, r.getHeight() * 0.5f * m_pic->peaks[(size_t) i] / mx);
        auto c = look::purple.interpolatedWith (look::green, m_pic->bright[(size_t) i]);
        const bool done = cur >= 0 && i <= cur;
        g.setColour (done ? c : c.withAlpha (0.35f));
        g.fillRect (r.getX() + bw * (float) i, r.getCentreY() - h, juce::jmax (1.0f, bw - (bw > 3 ? 1.0f : 0.0f)), h * 2);
    }
    if (cur >= 0 && cur < n)
    {
        g.setColour (look::ink);
        g.fillRect (r.getX() + bw * (float) cur, r.getY() - 4, 2.0f, r.getHeight() + 8);
    }
}

void TargetPanel::resized()
{
    const int hh = look::headOf (m_ed.getHeight());
    const float k = playScale (m_ed.getHeight());
    const int bh = juce::roundToInt (30.0f * k), gap = 8;
    auto r = getLocalBounds().reduced (14, 0);
    if (m_live.isVisible()) m_live.setBounds (r.withHeight (hh).withTrimmedRight (hh / 2).removeFromRight (136).withSizeKeepingCentre (136, juce::jmin (hh - 8, 26)));
    auto row = r.removeFromBottom (bh + 12).withTrimmedBottom (12);
    // four equal buttons, the last one ending exactly on the card's inner edge
    juce::Button* bs[] = { &m_load, &m_demo, &m_play, &m_rec };
    const int w4 = (row.getWidth() - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i)
    {
        bs[i]->setBounds (i == 3 ? row.withLeft (row.getRight() - w4) : row.withX (row.getX() + i * (w4 + gap)).withWidth (w4));
        look::setScale (*bs[i], k);
    }
}

// ================================================================== macros
Macro::Macro (Processor& p, const juce::String& l, const char* id, const juce::String& left, const juce::String& right)
    : label (l), m_proc (p), m_ids { id }, m_left (left), m_right (right)
{
    addAndMakeVisible (knob);
    knob.setComponentID (juce::String ("macro:") + id);
    m_att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params(), id, knob);
    knob.setTooltip (paramInfo (id).help);
    knob.onDragStart = [&p] { p.undo().beginNewTransaction(); };
    knob.setDoubleClickReturnValue (true, paramInfo (id).def);
    knob.setMouseDragSensitivity (220);
    knob.onValueChange = [this] { repaint(); };
    m_liveIdx = Engine::liveIndexOf (id);
}

Macro::Macro (Processor& p, const juce::String& l, std::vector<const char*> ids, double lo, double hi, double mid)
    : label (l), m_proc (p), m_ids (std::move (ids))
{
    addAndMakeVisible (knob);
    knob.setComponentID ("macro:" + l);
    knob.setRange (lo, hi, 1);
    knob.setSkewFactorFromMidPoint (mid);
    knob.setValue (p.getParam (m_ids[0]), juce::dontSendNotification);
    knob.setTooltip ("How long the pieces are: sets the library's PIECE SIZE and the TARGET PIECE SIZE together (UNDER THE HOOD sets them apart). Builds again when you let go.");
    knob.setDoubleClickReturnValue (true, 1000);
    knob.setMouseDragSensitivity (220);
    knob.onDragStart = [this]
    {
        m_proc.undo().beginNewTransaction ("Piece size");
        for (auto* id : m_ids) if (auto* prm = m_proc.params().getParameter (id)) prm->beginChangeGesture();
    };
    knob.onValueChange = [this]
    {
        for (auto* id : m_ids) if (auto* prm = m_proc.params().getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) knob.getValue()));
        repaint();
    };
    knob.onDragEnd = [this] { for (auto* id : m_ids) if (auto* prm = m_proc.params().getParameter (id)) prm->endChangeGesture(); };
}

void Macro::sync()
{
    if (m_att != nullptr || knob.isMouseButtonDown()) return;
    const double v = m_proc.getParam (m_ids[0]);
    if (std::abs (v - knob.getValue()) > 0.5) { knob.setValue (v, juce::dontSendNotification); repaint(); }
}

void Macro::syncLive (const Engine& e)
{
    if (m_att != nullptr)
    {
        // rule 3: a knob whose setting does nothing right now (SMOOTH while SEARCH STYLE is Most different) is greyed and says why
        const auto why = idleReason (m_proc, m_ids[0], false);
        if (why != m_idleWhy)
        {
            m_idleWhy = why;
            knob.setEnabled (why.isEmpty());
            knob.setTooltip (why.isEmpty() ? juce::String (paramInfo (m_ids[0]).help) : why + "  " + paramInfo (m_ids[0]).help);
            repaint();
        }
    }
    if (m_liveIdx < 0) return;
    const auto v = e.liveValue (m_liveIdx);
    const bool on = v.moving;
    const float p = on ? (float) knob.valueToProportionOfLength (v.now) : 0.0f;
    const auto txt = on ? live::text (m_liveIdx, v.now) : juce::String();
    if (on == m_liveOn && std::abs (p - m_liveP) < 0.002f && txt == m_liveText) return;
    m_liveOn = on; m_liveP = p; m_liveText = txt;
    knob.getProperties().set ("liveOn", on);
    knob.getProperties().set ("live", (double) p);
    knob.repaint();
    repaint();
}

juce::String Macro::valueText() const
{
    if (m_att == nullptr)
    {
        const double sr = juce::jmax (8000.0, m_proc.engine().sampleRate());
        const bool same = std::abs (m_proc.getParam ("src_block") - m_proc.getParam ("tgt_block")) < 0.5f;
        const double ms = 1000.0 * knob.getValue() / sr;
        return (ms < 10 ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms" + (same ? "" : " *");
    }
    if (auto* prm = m_proc.params().getParameter (m_ids[0])) return prm->getCurrentValueAsText();
    return {};
}

void Macro::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (juce::roundToInt (38.0f * playScale (getParentHeight())));
    knob.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void Macro::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const float k = playScale (getParentHeight());
    auto bottom = r.removeFromBottom (juce::roundToInt (38.0f * k));
    g.setColour (look::ink);
    g.setFont (look::spaced (12.5f * k, 2, 0.12f));
    g.drawFittedText (label, bottom.removeFromTop (bottom.getHeight() / 2), juce::Justification::centred, 1, 0.7f);
    g.setColour (look::inkDim);
    g.setFont (look::font (11.5f * k, 0));
    if (m_left.isNotEmpty())
    {
        g.drawText (m_left, bottom.removeFromLeft (bottom.getWidth() / 2).withTrimmedLeft (4), juce::Justification::centredLeft);
        g.drawText (m_right, bottom.withTrimmedRight (4), juce::Justification::centredRight);
    }
    else if (m_liveOn)
    {
        // TERRAIN is moving it: the value you set, then the one in use
        g.setColour (look::live);
        g.drawText (valueText() + "  >  " + m_liveText, bottom, juce::Justification::centred);
    }
    else if (m_idleWhy.isNotEmpty())
    {
        // rule 3 on screen: greyed knobs say why where their value would be ("NOT USED BY MOST DIFFERENT")
        g.setFont (look::font (10.5f * k, 1));
        g.drawFittedText (idleShort (m_proc, m_ids[0], false), bottom, juce::Justification::centred, 1, 0.9f);
    }
    else g.drawText (valueText(), bottom, juce::Justification::centred);
}

void Macro::paintOverChildren (juce::Graphics& g)
{
    if (m_idleWhy.isEmpty()) return;
    g.setColour (look::panel.withAlpha (0.55f));
    g.fillRect (getLocalBounds());
}

// ================================================================== DRAG WAV
DragWav::DragWav (Editor& e) : m_ed (e)
{
    setComponentID ("dragWav");
    setTooltip ("Click to make a WAV of one pass through the target, then drag it into your DAW. After a recording, drags the recording.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragWav::render()
{
    if (m_ed.proc().isEffect() && (m_ed.proc().getParam ("live_input") > 0.5f || m_ed.proc().engine().targetName().isEmpty()))
    {
        // the effect's target is the track itself: what to drag is a recording of it
        m_ed.proc().engine().setStatus ("Press RECORD while your track plays, then STOP REC: DRAG WAV then drags the recording.");
        return;
    }
    auto dir = m_ed.proc().userFolder().getChildFile ("Exports");
    dir.createDirectory();
    auto name = m_ed.proc().engine().targetName();
    if (name.isEmpty()) name = "take";
    auto f = dir.getChildFile (juce::File::createLegalFileName ("Ghost Mosaic - " + name + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S")) + ".wav");
    m_rendering = true; m_fresh = false;
    repaint();
    juce::Component::SafePointer<DragWav> safe (this);
    m_ed.proc().engine().renderToFile (f, 600, [safe, f] (bool ok)
    {
        if (safe == nullptr) return;
        safe->m_rendering = false;
        safe->m_file = ok ? f : juce::File();
        safe->m_fresh = ok;
        safe->repaint();
    });
}

void DragWav::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool ready = fresh();
    const auto shape = look::cutRect (r, juce::jmin (10.0f, r.getHeight() * 0.32f));
    g.setColour (isMouseOver() ? look::panelHi : look::panel);
    g.fillPath (shape);
    g.setColour (ready ? look::green : look::line.brighter (0.25f));
    g.strokePath (shape, juce::PathStrokeType (ready ? 1.5f : 1.0f));
    auto t = getLocalBounds().reduced (10, 4);
    const float fk = juce::jmin (1.25f, look::scaleOf (*this));
    g.setColour (look::ink);
    g.setFont (look::spaced (13.0f * fk, 1, 0.14f));
    g.drawText ("DRAG WAV", t.removeFromTop (t.getHeight() / 2 + 2), juce::Justification::centredBottom);
    g.setColour (ready ? look::green : look::inkDim);
    g.setFont (look::font (11.0f * fk, 0));
    g.drawText (m_rendering ? "making it..." : ready ? "ready: drag it out" : "click to make it", t, juce::Justification::centredTop);
}

void DragWav::mouseDown (const juce::MouseEvent&)
{
    m_dragged = false;
    if (! fresh() && ! m_rendering) render();
}

void DragWav::mouseDrag (const juce::MouseEvent& e)
{
    if (m_dragged || ! fresh() || e.getDistanceFromDragStart() < 6) return;
    m_dragged = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ m_file.getFullPathName() }, false, this);
}

// ================================================================== the editor
Editor::Editor (Processor& p)
    : juce::AudioProcessorEditor (p), m_proc (p), m_source (*this), m_target (*this), m_drag (*this)
{
    setLookAndFeel (&m_lnf);
    addAndMakeVisible (m_word);
    m_word.setComponentID ("wordmark");
    for (auto* b : { &m_tabPlay, &m_tabTerrain, &m_tabHood, &m_tabScope })
    {
        addAndMakeVisible (b);
        b->onClick = [this, b] { setPage (b == &m_tabScope ? 3 : b == &m_tabHood ? 2 : b == &m_tabTerrain ? 1 : 0); };
    }
    pill (m_tabPlay, "tabPlay", "The main screen.");
    pill (m_tabTerrain, "tabTerrain", "TERRAIN: shape a landscape that steers the matching. Across = which frequencies count; front to back = how wild it gets along the target.");
    pill (m_tabHood, "tabHood", "Every setting and button of the original, and the network page.");
    addAndMakeVisible (m_source);
    addAndMakeVisible (m_target);
    addAndMakeVisible (m_swarm);
    m_swarm.setComponentID ("swarm");
    m_swarm.setTooltip ("Every dot is a piece of your sounds, in its sound's colour. It flashes when it is played. Move over it; press it.");
    for (auto* b : { &m_viewDots, &m_viewHeat })
    {
        addAndMakeVisible (b);
        b->onClick = [this, b] { m_swarm.setHeatView (b == &m_viewHeat); m_viewDots.setToggleState (b == &m_viewDots, juce::dontSendNotification); m_viewHeat.setToggleState (b == &m_viewHeat, juce::dontSendNotification); };
    }
    pill (m_viewDots, "viewDots", "Dots: one per piece, flashing when played.");
    pill (m_viewHeat, "viewHeat", "Heat: which pieces are being used most, as a heat map.");
    m_viewDots.setToggleState (true, juce::dontSendNotification);

    m_algo.addItemList (juce::StringArray::fromTokens (kAlgoChoices, "|", ""), 1);
    m_algo.setComponentID ("algo");
    m_algo.setTooltip (paramInfo ("algo").help);
    m_algoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params(), "algo", m_algo);
    addAndMakeVisible (m_algo);

    m_macros.add (new Macro (p, "TONE / TIMBRE", "ratio", "TONE", "TIMBRE"));
    m_macros.add (new Macro (p, "NOVELTY", "novelty"));
    m_macros.add (new Macro (p, "SMOOTH", "stickyness"));
    m_macros.add (new Macro (p, "REPEAT", "stretch"));
    m_macros.add (new Macro (p, "PIECE SIZE", { "src_block", "tgt_block" }, 100, 20000, 2000));
    m_macros.add (new Macro (p, "AUTOTUNE", "autotune"));
    m_macros.add (new Macro (p, "ORIGINAL MIX", "target_mix"));
    if (p.isEffect()) m_macros.add (new Macro (p, "DRY", "dry"));
    m_macros.add (new Macro (p, "VOLUME", "volume"));
    for (auto* m : m_macros) addAndMakeVisible (m);

    pill (m_undo, "undo", "Undo the last change of a setting.");
    pill (m_redo, "redo", "Redo.");
    pill (m_ab, "ab", "Two sets of settings: switch between A and B to compare. Your sounds stay the same.");
    pill (m_generate, "generate", "Cut your sounds into pieces, analyse them, and play the target rebuilt out of them.");
    pill (m_mutate, "mutate", "New search settings around the ones you have. UNDO takes it back.");
    pill (m_session, "session", "Open or save a whole session or a library file (both open in the original app too); open your Ghost Mosaic folder.");
    m_generate.getProperties().set ("primary", true);
    m_undo.onClick = [this] { m_proc.undo().undo(); };
    m_redo.onClick = [this] { m_proc.undo().redo(); };
    m_ab.onClick = [this] { m_proc.switchAB(); m_ab.setButtonText (m_proc.abIsB() ? "B  (A)" : "A  (B)"); };
    m_ab.setButtonText ("A  (B)");
    m_generate.onClick = [this] { generate(); };
    m_mutate.onClick = [this] { mutate(); };
    m_session.onClick = [this] { showSessionMenu(); };
    for (auto* b : { &m_undo, &m_redo, &m_ab, &m_generate, &m_mutate, &m_session }) addAndMakeVisible (b);
    m_presets.setTextWhenNothingSelected ("PRESETS");
    m_presets.setComponentID ("presets");
    m_presets.setTooltip ("Starting points. The ones with built-in sounds load those too.");
    int id = 1;
    for (auto& pr : factoryPresets()) m_presets.addItem (juce::String (pr.name).toUpperCase(), id++);
    m_presets.onChange = [this]
    {
        const int i = m_presets.getSelectedItemIndex();
        if (i >= 0) m_proc.applyPreset (i);
    };
    addAndMakeVisible (m_presets);
    addAndMakeVisible (m_drag);

    HoodPage::Actions a;
    a.addSounds = [this] { chooseSounds (false); };
    a.addFolder = [this] { chooseSounds (true); };
    a.allOn = [this] { m_proc.engine().setAllEnabled (true); };
    a.allOff = [this] { m_proc.engine().setAllEnabled (false); };
    a.clearLibrary = [this] { m_proc.engine().clearSounds(); };
    a.generateLibrary = [this] { m_proc.engine().generateLibrary(); m_proc.uiShockwave.fetch_add (1); };
    a.loadTarget = [this] { chooseTarget(); };
    a.generateTarget = [this] { m_proc.engine().generateTarget(); };
    a.restart = [this] { m_proc.engine().restart(); };
    a.openLibrary = [this] { openLibrary(); };
    a.saveLibrary = [this] { saveLibrary(); };
    a.openSession = [this] { openSession(); };
    a.saveSession = [this] { saveSession(); };
    a.record = [this] { if (! m_proc.engine().isRecording()) toggleRecord(); };
    a.stopRecord = [this] { if (m_proc.engine().isRecording()) toggleRecord(); m_proc.engine().pause(); };   // the original's stop: ends the recording and pauses
    a.play = [this] { m_proc.engine().play(); };
    a.pause = [this] { m_proc.engine().pause(); };
    m_hood = std::make_unique<HoodPage> (p, a);
    m_hoodView.setViewedComponent (m_hood.get(), false);
    m_hoodView.setScrollBarsShown (true, false);
    m_hoodView.setScrollBarThickness (10);
    addChildComponent (m_hoodView);
    m_terrain = std::make_unique<TerrainPage> (p);
    addChildComponent (*m_terrain);
    // the new mode: its page hosts whichever view Scope/ActiveView.cpp makes; the view names the button
    m_scope = std::make_unique<scope::Page> (p);
    addChildComponent (*m_scope);
    m_tabScope.setButtonText (m_scope->buttonName());
    pill (m_tabScope, "tabScope", m_scope->buttonTip());

    m_proc.engine().addChangeListener (this);
    m_proc.params().state.addListener (this);
    m_shock = m_proc.uiShockwave.load();
    m_events.resize (1024);
    refreshAll();
    setPage (0);
    setResizable (true, true);
    setResizeLimits (1000, 700, 2000, 1400);
    setSize (1200, 760);   // the size that fits a 1440 x 900 laptop screen inside FL (Brandon 2026-11-06): never needs FL's fit-to-screen button
    sendLookAndFeelChange();   // (the UNDER THE HOOD page was made before it joined the editor)
    startTimerHz (60);
}

Editor::~Editor()
{
    stopTimer();
    m_proc.params().state.removeListener (this);
    m_proc.engine().removeChangeListener (this);
    m_hoodView.setViewedComponent (nullptr, false);
    setLookAndFeel (nullptr);
}

juce::Component* Editor::findById (const juce::String& id)
{
    std::function<juce::Component* (juce::Component&)> walk = [&] (juce::Component& c) -> juce::Component*
    {
        if (c.getComponentID() == id) return &c;
        for (auto* ch : c.getChildren()) if (auto* f = walk (*ch)) return f;
        return nullptr;
    };
    if (auto* f = walk (*this)) return f;
    if (auto* f = walk (*m_terrain)) return f;
    return walk (*m_hood);
}

void Editor::setPage (int page)
{
    m_page = juce::jlimit (0, 3, page);
    m_proc.engine().setDrag (0, 0, 0);   // (a drag can't outlive its page)
    const bool hood = m_page == 2, land = m_page == 1, main = m_page == 0, inside = m_page == 3;
    m_hoodShown = hood;
    m_tabPlay.setToggleState (main, juce::dontSendNotification);
    m_tabTerrain.setToggleState (land, juce::dontSendNotification);
    m_tabHood.setToggleState (hood, juce::dontSendNotification);
    m_tabScope.setToggleState (inside, juce::dontSendNotification);
    m_hoodView.setVisible (hood);
    m_terrain->setVisible (land);
    m_scope->setVisible (inside);
    scopeOnScreen();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &m_source, &m_target, &m_swarm, &m_viewDots, &m_viewHeat, &m_algo })
        c->setVisible (main);
    for (auto* m : m_macros) m->setVisible (main);
    if (hood) m_hood->refresh();
    repaint();
}

// The new mode reads the engine's tap only while it is really on screen: its page chosen and the window up. Any other
// page, a closed or minimised window: the tap is detached and no frame runs. (No window at all, as in the tests'
// headless editor, counts as up: there the page number alone decides, as it does for TERRAIN.)
void Editor::scopeOnScreen() { m_scope->setOnScreen (m_page == 3 && (getPeer() == nullptr || isShowing())); }

void Editor::setTestClock (bool on)
{
    if (on) stopTimer(); else startTimerHz (60);
    m_scope->useTestClock (on);
    m_word.setManualClock (on);
}

void Editor::stepForTests (double dt)
{
    timerCallback();
    m_scope->stepForTests (dt);
    m_word.advance ((float) dt);
}

void Editor::refreshAll()
{
    m_source.refresh();
    m_target.refresh();
    if (m_hood) m_hood->refreshSounds();
    auto pic = m_proc.engine().libraryPicture();
    if (pic != m_libShown) { m_libShown = pic; m_swarm.setLibrary (pic); m_drag.stale(); }
}

void Editor::changeListenerCallback (juce::ChangeBroadcaster*) { refreshAll(); }

// THE LIVE VIEW says it too, big: while the library builds (percentage, time left, the long-sound note),
// and why it is silent when it is
void Editor::paintOverChildren (juce::Graphics& g)
{
    if (m_page != 0 || m_viewCard.isEmpty()) return;
    auto area = m_viewCard.reduced (14).withTrimmedTop (40);
    if (m_building)
    {
        auto box = area.withSizeKeepingCentre (juce::jmin (440, area.getWidth() - 20), juce::jmin (200, area.getHeight() - 10)).toFloat();
        g.setColour (look::ground.withAlpha (0.92f)); g.fillRoundedRectangle (box, 14.0f);
        g.setColour (look::purple.withAlpha (0.6f)); g.drawRoundedRectangle (box, 14.0f, 1.5f);
        auto r = box.reduced (22, 16);
        g.setColour (look::inkDim); g.setFont (look::spaced (11.0f, 1, 0.25f));
        g.drawText ("BUILDING THE LIBRARY", r.removeFromTop (16), juce::Justification::centredLeft);
        auto row = r.removeFromTop (46);
        g.setColour (look::ink); g.setFont (look::font (34.0f, 2));
        g.drawText (juce::String ((int) (m_progress * 100)) + "%", row.removeFromLeft (110), juce::Justification::centredLeft);
        g.setFont (look::font (14.0f, 1));
        g.drawFittedText ((m_eta >= 0 ? Engine::timeWords (m_eta) + " left" : juce::String ("working out the time..."))
                              + (m_proc.engine().playPending() ? ", then it plays" : juce::String()),
                          row.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
        auto bar = r.removeFromTop (10).withTrimmedTop (2);
        g.setColour (look::line); g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (look::purple); g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * m_progress)), 4.0f);
        r.removeFromTop (10);
        g.setColour (look::inkDim); g.setFont (look::font (12.0f, 0));
        g.drawFittedText (m_notice.isNotEmpty() ? m_notice : juce::String ("The audio keeps running; you can keep working."), r.toNearestInt(), juce::Justification::topLeft, 4, 0.9f);
        return;
    }
    if (m_stateLine.startsWith ("Silent"))
    {
        auto strip = area.removeFromBottom (40).reduced (40, 4).toFloat();
        g.setColour (look::ground.withAlpha (0.9f)); g.fillRoundedRectangle (strip, 10.0f);
        g.setColour (look::purple.withAlpha (0.5f)); g.drawRoundedRectangle (strip, 10.0f, 1.0f);
        g.setColour (look::ink); g.setFont (look::font (13.0f, 1));
        g.drawFittedText (m_stateLine, strip.reduced (14, 2).toNearestInt(), juce::Justification::centred, 2, 0.85f);
    }
}

void Editor::syncLive()
{
    auto& e = m_proc.engine();
    if (m_page == 0)
    {
        for (auto* m : m_macros) m->syncLive (e);
        m_source.syncLive();
        const auto v = e.liveValue (Engine::kLiveAlgo);
        const auto txt = v.moving ? live::choice (v.now) : juce::String();
        if (v.moving != m_algoLive || txt != m_algoLiveText)
        {
            m_algoLive = v.moving; m_algoLiveText = txt;
            m_algo.getProperties().set ("liveOn", v.moving);
            m_algo.getProperties().set ("liveText", txt);
            m_algo.resized(); m_algo.repaint();
        }
    }
    else if (m_page == 2) m_hood->syncLive (e);
}

void Editor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { m_drag.stale(); }

void Editor::timerCallback()
{
    auto& e = m_proc.engine();
    const int n = e.popEvents (m_events.data(), (int) m_events.size());
    for (int i = 0; i < n; ++i) m_swarm.played ((int) m_events[(size_t) i].src, m_events[(size_t) i].channel);
    const int shock = m_proc.uiShockwave.load();
    if (shock != m_shock) { m_shock = shock; m_swarm.shockwave(); }
    m_swarm.tick();
    m_target.tick();
    if (m_page == 1) m_terrain->tick();
    if (m_page == 3 || m_scope->onScreen()) { scopeOnScreen(); m_scope->tick(); }
    if ((++m_liveFrame & 1) == 0) syncLive();   // (~30 fps)
    const auto tp = e.targetPicture();
    const int cur = e.currentTargetBlock();
    const float where = tp != nullptr && tp->numBlocks > 0 && cur >= 0 ? (float) cur / (float) tp->numBlocks : 0.5f;
    m_level = e.outputLevel();
    // the wordmark lights up by itself whenever music plays through the plug-in (Brandon 2026-11-13): what it
    // plays, what comes in on the track (the effect), or simply the song running in the DAW. While Ghost Mosaic
    // itself is rebuilding the target, the glow follows the target's playhead; otherwise it roams the word.
    {
        const float heard = juce::jmax (m_level, m_proc.uiInputLevel.load());
        float drive = juce::jmin (1.0f, heard * 2.0f);
        if (e.hostPlaying()) drive = juce::jmax (drive, 0.4f);
        const bool follow = e.isPlaying() && m_level > 0.002f && tp != nullptr && tp->numBlocks > 0;
        m_word.setAudioHeat (follow ? where : -1.0f, drive > 0.002f ? drive : 0.0f);
    }
    const auto notice = e.notice();
    const auto st = notice.isNotEmpty() ? notice : e.status();
    const bool busy = e.busy();
    const auto build = e.buildInfo();
    const float prog = build.phase > 0 ? build.fraction : 0.0f;
    const auto line = m_proc.stateLine();
    if (st != m_status || line != m_stateLine || busy != m_busy || std::abs (prog - m_progress) > 0.005f || busy)
    {
        const bool viewChanged = line != m_stateLine || notice != m_notice || (build.phase > 0) != m_building || std::abs (prog - m_progress) > 0.005f;
        m_status = st; m_stateLine = line; m_busy = busy; m_progress = prog; m_building = build.phase > 0; m_notice = notice; m_eta = build.eta;
        repaint (m_statusArea);
        if (viewChanged) repaint (m_viewCard);
    }
    static int frame = 0;
    if (++frame % 6 == 0)
    {
        for (auto* m : m_macros) m->sync();
        m_undo.setEnabled (m_proc.undo().canUndo());
        m_redo.setEnabled (m_proc.undo().canRedo());
        if (m_hoodShown) m_hood->refresh();
        if (e.isRecording()) m_drag.stale();
        // upstream's own note: the Closest / Most different styles check every piece for every target piece,
        // which a big library makes too slow for live playing; say so once per library
        const auto lib = m_libShown;
        const bool slow = lib != nullptr && lib->numBlocks > 4000 && m_proc.getParam ("algo") < 1.5f;
        if (slow && ! m_warnedSlow) { m_warnedSlow = true; e.setStatus ("Big library (" + juce::String (lib->numBlocks) + " pieces): pick SEARCH STYLE Network, or it may stutter."); }
        if (! slow) m_warnedSlow = false;
    }
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (look::ground);
    auto r = getLocalBounds();
    // subtitle
    g.setColour (look::inkDim);
    g.setFont (look::spaced (12.0f, 0, 0.32f));
    {
        // spread end to end under the letters of GHOST MOSAIC (same left and right edges), evenly spaced
        const juce::String sub = m_proc.isEffect() ? "YOUR TRACK, REBUILT FROM YOUR SOUNDS" : "ANY SOUND, REBUILT FROM YOUR SOUNDS";
        juce::GlyphArrangement ga;
        ga.addLineOfText (look::font (12.0f, 0), sub, 0, m_subSpan.getBottom());
        const int n = ga.getNumGlyphs();
        if (n > 1 && ! m_subSpan.isEmpty())
        {
            const auto ink = ga.getBoundingBox (0, -1, false);
            const float extra = (m_subSpan.getWidth() - ink.getWidth()) / (float) (n - 1);
            const float x0 = m_subSpan.getX() - ink.getX();
            for (int i = n; --i >= 0;) ga.moveRangeOfGlyphs (i, 1, x0 + extra * (float) i, 0);
            ga.draw (g);
        }
    }
    // the frame: two rules, and on the upper one a purple bar under the page you are on
    g.setColour (look::line);
    g.fillRect (0, 95, r.getWidth(), 2);
    g.fillRect (0, r.getHeight() - 81, r.getWidth(), 2);
    for (auto* t : { &m_tabPlay, &m_tabTerrain, &m_tabHood, &m_tabScope })
        if (t->getToggleState()) { g.setColour (look::purple); g.fillRect (t->getX(), 93, t->getWidth(), 4); }
    // status: a dot (green ready, purple busy) and the engine's words
    {
        auto s = m_statusArea;
        const float pulse = m_busy ? 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f) : 1.0f;
        const auto dotC = m_busy ? look::purple : look::green;
        auto dot = s.removeFromLeft (20).withSizeKeepingCentre (12, 12).toFloat();
        g.setColour (dotC.withAlpha (pulse));
        g.fillPath (look::cutRect (dot, 4.0f));
        // line 1: what you hear and why ("Building library... 40%", "Playing", "Silent: no target loaded");
        // line 2: the latest message, or a note that matters right now (a long sound); a bar while the library builds
        auto t = s.withTrimmedLeft (6);
        auto bar = t.removeFromBottom (8).toFloat();
        const bool silent = m_stateLine.startsWith ("Silent");
        const float fk = frameScale (getWidth(), getHeight());
        g.setColour (silent ? look::purple.brighter (0.4f) : look::ink);
        g.setFont (look::font (13.0f * fk, 1));
        g.drawFittedText (m_stateLine, t.removeFromTop (t.getHeight() * 3 / 5), juce::Justification::centredLeft, 2, 0.85f);
        g.setColour (look::inkDim);
        g.setFont (look::font (11.0f * fk, 0));
        g.drawFittedText (m_status, t, juce::Justification::centredLeft, 1, 0.8f);
        if (m_building)
        {
            bar = bar.withTrimmedTop (2);
            g.setColour (look::line); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (look::purple); g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * m_progress)), 3.0f);
        }
    }
    if (m_page != 0) return;
    const int hh = look::headOf (getHeight());
    const float pk = playScale (getHeight());
    look::zone (g, m_viewCard, "03", "THE LIVE VIEW", hh);
    g.setColour (look::inkDim);
    g.setFont (look::font (12.0f * pk, 0));
    const auto pic = m_libShown;
    const juce::String info = pic != nullptr && pic->numBlocks > 0 ? juce::String (pic->numBlocks) + " pieces from " + juce::String ((int) pic->soundNames.size()) + " sounds" : juce::String();
    g.drawText (info, m_viewCard.withHeight (hh).withRight (m_viewDots.getX() - 14).withTrimmedLeft (m_viewCard.getWidth() / 3), juce::Justification::centredRight, true);
    look::zone (g, m_macroCard, "04", "MACROS", hh);
    g.setColour (look::inkDim);
    g.setFont (look::spaced (11.5f * pk, 1, 0.12f));
    g.drawText ("SEARCH STYLE", m_algo.getBounds().withX (m_algo.getX() - 170).withWidth (160), juce::Justification::centredRight);
}

void Editor::resized()
{
    auto r = getLocalBounds();
    const int pad = 24;
    // the left column (SOURCE SOUNDS / TARGET): the wordmark is centred over it, its letters lined up with the
    // card's buttons (equal room to both edges), and the subtitle spread under the letters (Brandon 2026-11-13)
    const int colW = juce::jlimit (300, 380, (r.getWidth() - 2 * pad) / 3 - 40);
    m_word.setInset (14.0f);
    m_word.setBounds (pad, 0, colW, 66);
    {
        const auto ink = m_word.inkBounds();
        const float subGap = 10.0f, subH = 9.0f;   // (the subtitle's capitals at 12 px)
        const float blockH = ink.getHeight() + subGap + subH;
        const float top = juce::jmax (4.0f, (96.0f - blockH) * 0.5f);
        m_word.setTopLeftPosition (pad, juce::roundToInt (top - ink.getY()));
        const auto inkAbs = ink.translated ((float) m_word.getX(), (float) m_word.getY());
        m_subSpan = { inkAbs.getX(), inkAbs.getBottom() + subGap, inkAbs.getWidth(), subH };
    }
    const bool narrow = r.getWidth() < 1150;
    {
        // the page buttons, right-aligned on one line with equal gaps: each is as wide as its words plus the same room
        // on both sides, so the new mode's button belongs with the other three whatever a design calls it. In a narrow
        // window the four share the 350 px the three had, so the status keeps all of its room.
        juce::TextButton* tabs[] = { &m_tabPlay, &m_tabTerrain, &m_tabHood, &m_tabScope };
        const float fk = frameScale (r.getWidth(), r.getHeight());
        for (auto* t : tabs) look::setScale (*t, fk);
        const int tabH = juce::roundToInt (40.0f * juce::jmin (1.3f, fk)), gap = narrow ? 6 : 8;
        const auto f = m_lnf.getTextButtonFont (m_tabPlay, tabH);
        int words[4], sum = 0;
        for (int i = 0; i < 4; ++i) { words[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, tabs[i]->getButtonText())); sum += words[i]; }
        const int side = narrow ? juce::jlimit (7, 22, (350 - 3 * gap - sum) / 8) : 22;
        int x = r.getRight() - pad;
        for (int i = 4; --i >= 0;)
        {
            const int w = words[i] + 2 * side;
            tabs[i]->setBounds (x - w, (96 - tabH) / 2, w, tabH);
            x -= w + gap;
        }
    }
    m_statusArea = juce::Rectangle<int> (m_word.getRight() + 24, 14, m_tabPlay.getX() - m_word.getRight() - 48, 62);

    // bottom bar (GHOST POSTER): eight blocks of one height on one line with equal gaps, sharing the whole width by weight;
    // GENERATE is the widest. The same room above and below (18 px).
    {
        const auto bar = juce::Rectangle<int> (pad, r.getHeight() - 80 + 18, r.getWidth() - 2 * pad, 44);
        juce::Component* items[] = { &m_undo, &m_redo, &m_ab, &m_presets, &m_generate, &m_mutate, &m_drag, &m_session };
        const float weight[] = { 1.0f, 1.0f, 1.0f, 2.2f, 3.0f, 1.4f, 1.6f, 1.2f };
        const int bg = 8;
        const float unit = (float) (bar.getWidth() - 7 * bg) / 12.4f;
        float at = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const int x0 = bar.getX() + juce::roundToInt (at) + i * bg;
            at += unit * weight[i];
            const int x1 = i == 7 ? bar.getRight() : bar.getX() + juce::roundToInt (at) + i * bg;
            items[i]->setBounds (x0, bar.getY(), x1 - x0, bar.getHeight());
            look::setScale (*items[i], frameScale (r.getWidth(), r.getHeight()));
            items[i]->repaint();
        }
    }

    // the body: one grid for every page (24 px to the sides, 14 px to both rules, one gutter between cards)
    auto body = juce::Rectangle<int> (pad, 110, r.getWidth() - 2 * pad, r.getHeight() - 110 - 94);
    const int g = look::gutterOf (r.getHeight()), hh = look::headOf (r.getHeight());
    const float pk = playScale (r.getHeight());
    m_hoodView.setBounds (body);
    m_terrain->setBounds (body);
    m_scope->setBounds (body);
    m_hood->setBounds (0, 0, body.getWidth(), body.getHeight());   // (it fits at every size: nothing scrolls)

    // PLAY: two rows of two cards. Each row's cards share their top and bottom edges (SOURCE SOUNDS | THE LIVE VIEW, TARGET | MACROS).
    auto left = body.removeFromLeft (colW);
    body.removeFromLeft (g);
    const int lowH = juce::jlimit (190, 460, juce::roundToInt ((float) body.getHeight() * 0.36f));
    m_target.setBounds (left.removeFromBottom (lowH));
    left.removeFromBottom (g);
    m_source.setBounds (left);
    m_macroCard = body.removeFromBottom (lowH);
    body.removeFromBottom (g);
    m_viewCard = body;
    {
        const int bh = juce::jmin (hh - 10, juce::roundToInt (28.0f * pk)), bw = juce::roundToInt (76.0f * pk);
        auto vh = m_viewCard.withHeight (hh).withTrimmedRight (hh / 2);
        m_viewHeat.setBounds (vh.removeFromRight (bw).withSizeKeepingCentre (bw, bh));
        vh.removeFromRight (6);
        m_viewDots.setBounds (vh.removeFromRight (bw).withSizeKeepingCentre (bw, bh));
        look::setScale (m_viewHeat, pk); look::setScale (m_viewDots, pk);
        m_swarm.setBounds (m_viewCard.withTrimmedTop (hh).reduced (10));
        auto mh = m_macroCard.withHeight (hh).withTrimmedRight (hh / 2);
        const int aw = juce::roundToInt (230.0f * pk);
        m_algo.setBounds (mh.removeFromRight (aw).withSizeKeepingCentre (aw, bh));
        look::setScale (m_algo, pk);
        auto knobs = m_macroCard.withTrimmedTop (hh + 8).reduced (12, 0).withTrimmedBottom (10);
        const int n = juce::jmax (1, m_macros.size()), kw = knobs.getWidth() / n;
        knobs = knobs.withSizeKeepingCentre (kw * n, knobs.getHeight());
        for (auto* m : m_macros) m->setBounds (knobs.removeFromLeft (kw).reduced (4, 0));
    }
}

// ------------------------------------------------------------------ actions
void Editor::launch (std::unique_ptr<juce::FileChooser> c, int flags, std::function<void (const juce::FileChooser&)> fn)
{
    m_chooser = std::move (c);
    m_chooser->launchAsync (flags, [fn] (const juce::FileChooser& fc) { fn (fc); });
}

void Editor::chooseSounds (bool folder)
{
    auto start = m_proc.userFolder();
    if (folder)
        launch (std::make_unique<juce::FileChooser> ("Add a folder of sounds", start), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) m_proc.engine().addFiles ({ fc.getResult().getFullPathName() }); });
    else
        launch (std::make_unique<juce::FileChooser> ("Add sounds", start, kSoundTypes), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                [this] (const juce::FileChooser& fc)
                {
                    juce::StringArray paths;
                    for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                    if (! paths.isEmpty()) m_proc.engine().addFiles (paths);
                });
}

void Editor::chooseTarget()
{
    launch (std::make_unique<juce::FileChooser> ("Choose the sound to rebuild", m_proc.userFolder(), kSoundTypes), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File()) return;
                m_proc.engine().setTargetFile (fc.getResult());
                if (m_proc.isEffect()) m_proc.setParam ("live_input", 0);
            });
}

void Editor::showKitMenu (juce::Component& from)
{
    juce::PopupMenu m;
    const auto names = demo::kitNames();
    for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, demo::kitLabel (names[i]));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from), [this, names] (int r) { if (r > 0) m_proc.loadKit (names[r - 1]); });
}

void Editor::showDemoTargetMenu (juce::Component& from)
{
    juce::PopupMenu m;
    m.addItem (1, "Synth beat");
    m.addItem (2, "Sung vowels");
    m.addItem (3, "Plucked melody");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from), [this] (int r)
    {
        if (r <= 0) return;
        m_proc.loadDemoTarget (r == 1 ? "beat" : r == 2 ? "vowels" : "piano");
        if (m_proc.isEffect()) m_proc.setParam ("live_input", 0);
    });
}

void Editor::togglePlay()
{
    auto& e = m_proc.engine();
    if (e.isPlaying()) e.pause(); else e.play();
    const int mode = (int) m_proc.getParam ("play_mode");
    if (mode == 1) e.setStatus ("PLAY BY is set to MIDI keys: hold a note to play.");
    if (mode == 2) e.setStatus ("PLAY BY is set to DAW play: press play in your DAW.");
}

void Editor::toggleRecord()
{
    auto& e = m_proc.engine();
    if (e.isRecording()) { e.stopRecording(); m_drag.setFile (e.lastRecording()); }
    else e.startRecording (m_proc.nextRecordingFile());
}

void Editor::generate() { m_proc.generate(); }

void Editor::mutate() { m_proc.mutate(); }

void Editor::showSessionMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Open session...");
    m.addItem (2, "Save session...");
    m.addSeparator();
    m.addItem (3, "Open library...");
    m.addItem (4, "Save library...");
    m.addSeparator();
    m.addItem (5, "Show my Ghost Mosaic folder");
    m.addSeparator();
    m.addItem (6, "About / Credits");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&m_session), [this] (int r)
    {
        if (r == 1) openSession();
        if (r == 2) saveSession();
        if (r == 3) openLibrary();
        if (r == 4) saveLibrary();
        if (r == 5) m_proc.userFolder().revealToUser();
        if (r == 6) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About / Credits", creditsText(), "OK", this);
    });
}

// The one place the open-source projects are named (GPL "appropriate legal notices"): who, which licence, where the source is.
juce::String creditsText()
{
    return juce::String ("Ghost Mosaic v1 beta by GhostBlock. Free software, GNU General Public License v2 or later.\n\n"
           "Its sound engine is Samplebrain by Then Try This and contributors (GPL-2.0-or-later). Source: https://gitlab.com/then-try-this/samplebrain\n"
           "Source of this program: https://github.com/bsaxon430/ghosty-vst (folder ghost-mosaic)\n\n"
           "Also inside: FFTW (GPL-2.0-or-later, Matteo Frigo and MIT), Aquila DSP (MIT, Zbigniew Siciarz), JUCE (AGPLv3), "
           "Bencho's Particles and Heat map (MIT), Poppins font (SIL OFL 1.1).\n"
           "Full licences: the Licenses folder that came with this plug-in.");
}

void Editor::openSession()
{
    launch (std::make_unique<juce::FileChooser> ("Open a session", m_proc.userFolder(), "*.samplebrain"), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) m_proc.engine().loadSessionFile (fc.getResult()); });
}

void Editor::saveSession()
{
    launch (std::make_unique<juce::FileChooser> ("Save the session", m_proc.userFolder().getChildFile ("session.samplebrain"), "*.samplebrain"),
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) m_proc.engine().saveSessionFile (fc.getResult().withFileExtension ("samplebrain")); });
}

void Editor::openLibrary()
{
    launch (std::make_unique<juce::FileChooser> ("Open a library", m_proc.userFolder(), "*.brain"), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) m_proc.engine().loadLibraryFile (fc.getResult()); });
}

void Editor::saveLibrary()
{
    launch (std::make_unique<juce::FileChooser> ("Save the library", m_proc.userFolder().getChildFile ("library.brain"), "*.brain"),
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) m_proc.engine().saveLibraryFile (fc.getResult().withFileExtension ("brain")); });
}
}
