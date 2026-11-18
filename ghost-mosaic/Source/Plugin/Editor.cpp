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

void SourcePanel::paint (juce::Graphics& g)
{
    look::card (g, getLocalBounds().toFloat(), m_hot);
    auto top = getLocalBounds().reduced (16, 0).withHeight (42).translated (0, 4);
    look::title (g, "SOURCE SOUNDS", top, look::ink, 13.0f);
    double secs = 0; int on = 0;
    for (auto& s : m_sounds) { secs += s.seconds; on += s.enabled ? 1 : 0; }
    g.setColour (look::inkDim);
    g.setFont (look::font (12.0f, 0));
    if (! m_sounds.empty())
        g.drawText (juce::String (on) + " of " + juce::String ((int) m_sounds.size()) + "  -  " + juce::String (secs, 1) + " s", top, juce::Justification::centredRight);
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
    auto r = getLocalBounds().reduced (14, 0);
    r.removeFromTop (46);
    auto row = r.removeFromTop (30);
    const int w3 = (row.getWidth() - 12) / 3;
    m_add.setBounds (row.removeFromLeft (w3)); row.removeFromLeft (6);
    m_folder.setBounds (row.removeFromLeft (w3)); row.removeFromLeft (6);
    m_kits.setBounds (row);
    auto bottom = r.removeFromBottom (44).withTrimmedTop (8).withTrimmedBottom (6);
    const int b3 = (bottom.getWidth() - 12) / 3;
    m_all.setBounds (bottom.removeFromLeft (b3)); bottom.removeFromLeft (6);
    m_none.setBounds (bottom.removeFromLeft (b3)); bottom.removeFromLeft (6);
    m_clear.setBounds (bottom);
    m_list.setBounds (r.withTrimmedTop (8));
}

void SourcePanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) m_sounds.size()) return;
    const auto& s = m_sounds[(size_t) row];
    auto r = juce::Rectangle<int> (w, h).reduced (0, 3);
    g.setColour (look::ground.withAlpha (0.6f));
    g.fillRoundedRectangle (r.toFloat(), 8);
    const auto tint = look::sourceTint (row);
    // TERRAIN keeps some sounds out of play (your own switch is untouched): they dim and their switch goes hollow; the ones in play glow
    const bool masked = m_ed.proc().engine().soundsMasked();
    const bool benched = s.enabled && masked && (row < (int) m_benched.size() ? m_benched[(size_t) row] != 0 : false);
    const bool glow = s.enabled && masked && ! benched;
    g.setColour (s.enabled ? (benched ? tint.withAlpha (0.3f) : tint) : look::inkFaint);
    g.fillEllipse ((float) r.getX() + 10, (float) r.getCentreY() - 4.5f, 9, 9);
    if (glow) { g.setColour (look::live.withAlpha (0.8f)); g.drawEllipse ((float) r.getX() + 7.5f, (float) r.getCentreY() - 7.0f, 14, 14, 1.5f); }
    // the switch and the x
    auto right = r.removeFromRight (30);
    g.setColour (look::inkDim);
    g.drawLine ((float) right.getCentreX() - 4, (float) right.getCentreY() - 4, (float) right.getCentreX() + 4, (float) right.getCentreY() + 4, 1.5f);
    g.drawLine ((float) right.getCentreX() + 4, (float) right.getCentreY() - 4, (float) right.getCentreX() - 4, (float) right.getCentreY() + 4, 1.5f);
    auto sw = r.removeFromRight (40).withSizeKeepingCentre (30, 16).toFloat();
    if (glow)
        for (int k = 3; k >= 1; --k) { g.setColour (look::live.withAlpha (0.10f * (float) (4 - k))); g.fillRoundedRectangle (sw.expanded ((float) k * 1.6f), 8.0f + (float) k * 1.6f); }
    g.setColour (benched ? look::green.withAlpha (0.18f) : s.enabled ? look::green.withAlpha (0.9f) : look::line);
    g.fillRoundedRectangle (sw, 8);
    if (benched) { g.setColour (look::green.withAlpha (0.4f)); g.drawRoundedRectangle (sw.reduced (0.5f), 8, 1.0f); }
    if (glow) { g.setColour (look::live); g.drawRoundedRectangle (sw.reduced (0.5f), 8, 1.5f); }
    g.setColour (benched ? look::inkDim : s.enabled ? look::ground : look::inkDim);
    g.fillEllipse (s.enabled ? sw.getRight() - 14 : sw.getX() + 2, sw.getY() + 2, 12, 12);
    r.removeFromLeft (28);
    g.setColour (look::inkFaint);
    g.setFont (look::font (11.5f, 0));
    g.drawText (juce::String (s.seconds, 1) + " s", r.removeFromRight (48), juce::Justification::centredRight);
    g.setColour (benched ? look::inkFaint : s.enabled ? look::ink : look::inkDim);
    g.setFont (look::font (13.0f, 0));
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

juce::Rectangle<int> TargetPanel::waveArea() const { return getLocalBounds().reduced (14, 0).withTrimmedTop (46).withTrimmedBottom (48); }

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
    look::card (g, getLocalBounds().toFloat(), m_hot);
    auto top = getLocalBounds().reduced (16, 0).withHeight (42).translated (0, 4);
    look::title (g, "TARGET", top, look::ink, 13.0f);
    const bool live = m_ed.proc().isEffect() && m_ed.proc().getParam ("live_input") > 0.5f;
    auto wave = waveArea();
    g.setColour (look::ground.withAlpha (0.6f));
    g.fillRoundedRectangle (wave.toFloat(), 8);
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
    g.setFont (look::font (12.0f, 0));
    g.drawText (m_pic->name + "  -  " + juce::String (m_pic->seconds, 1) + " s", top, juce::Justification::centredRight, true);
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
    auto r = getLocalBounds().reduced (14, 0);
    if (m_live.isVisible()) m_live.setBounds (r.withHeight (42).translated (0, 4).removeFromRight (130).reduced (0, 9));
    auto row = r.removeFromBottom (44).withTrimmedBottom (12);
    const int w4 = (row.getWidth() - 18) / 4;
    for (auto* b : { &m_load, &m_demo, &m_play, &m_rec }) { b->setBounds (row.removeFromLeft (w4)); row.removeFromLeft (6); }
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
    r.removeFromBottom (36);
    knob.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void Macro::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (36);
    g.setColour (look::ink);
    g.setFont (look::spaced (12.0f, 1, 0.12f));
    g.drawFittedText (label, bottom.removeFromTop (18), juce::Justification::centred, 1, 0.7f);
    g.setColour (look::inkDim);
    g.setFont (look::font (11.5f, 0));
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
    else g.drawText (valueText(), bottom, juce::Justification::centred);
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
    g.setColour (isMouseOver() ? look::panelHi : look::panel);
    g.fillRoundedRectangle (r, 10);
    g.setColour (ready ? look::green : look::line);
    g.drawRoundedRectangle (r, 10, ready ? 1.5f : 1.0f);
    auto t = getLocalBounds().reduced (10, 6);
    g.setColour (look::ink);
    g.setFont (look::spaced (13.0f, 1, 0.14f));
    g.drawText ("DRAG WAV", t.removeFromTop (t.getHeight() / 2 + 2), juce::Justification::centredBottom);
    g.setColour (ready ? look::green : look::inkDim);
    g.setFont (look::font (11.0f, 0));
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
    for (auto* b : { &m_tabPlay, &m_tabTerrain, &m_tabHood })
    {
        addAndMakeVisible (b);
        b->onClick = [this, b] { setPage (b == &m_tabHood ? 2 : b == &m_tabTerrain ? 1 : 0); };
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
    m_page = juce::jlimit (0, 2, page);
    m_proc.engine().setDrag (0, 0, 0);   // (a drag can't outlive its page)
    const bool hood = m_page == 2, land = m_page == 1, main = m_page == 0;
    m_hoodShown = hood;
    m_tabPlay.setToggleState (main, juce::dontSendNotification);
    m_tabTerrain.setToggleState (land, juce::dontSendNotification);
    m_tabHood.setToggleState (hood, juce::dontSendNotification);
    m_hoodView.setVisible (hood);
    m_terrain->setVisible (land);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &m_source, &m_target, &m_swarm, &m_viewDots, &m_viewHeat, &m_algo })
        c->setVisible (main);
    for (auto* m : m_macros) m->setVisible (main);
    if (hood) m_hood->refresh();
    repaint();
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
    g.setColour (look::line);
    g.fillRect (0, 96, r.getWidth(), 1);
    g.fillRect (0, r.getHeight() - 80, r.getWidth(), 1);
    // status: a dot (green ready, purple busy) and the engine's words
    {
        auto s = m_statusArea;
        const float pulse = m_busy ? 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f) : 1.0f;
        const auto dotC = m_busy ? look::purple : look::green;
        auto dot = s.removeFromLeft (20).withSizeKeepingCentre (10, 10).toFloat();
        g.setColour (dotC.withAlpha (0.25f * pulse));
        g.fillEllipse (dot.expanded (4));
        g.setColour (dotC.withAlpha (pulse));
        g.fillEllipse (dot);
        // line 1: what you hear and why ("Building library... 40%", "Playing", "Silent: no target loaded");
        // line 2: the latest message, or a note that matters right now (a long sound); a bar while the library builds
        auto t = s.withTrimmedLeft (6);
        auto bar = t.removeFromBottom (8).toFloat();
        const bool silent = m_stateLine.startsWith ("Silent");
        g.setColour (silent ? look::purple.brighter (0.4f) : look::ink);
        g.setFont (look::font (13.0f, 1));
        g.drawFittedText (m_stateLine, t.removeFromTop (t.getHeight() * 3 / 5), juce::Justification::centredLeft, 2, 0.85f);
        g.setColour (look::inkDim);
        g.setFont (look::font (11.0f, 0));
        g.drawFittedText (m_status, t, juce::Justification::centredLeft, 1, 0.8f);
        if (m_building)
        {
            bar = bar.withTrimmedTop (2);
            g.setColour (look::line); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (look::purple); g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * m_progress)), 3.0f);
        }
    }
    if (m_page != 0) return;
    look::card (g, m_viewCard.toFloat());
    look::title (g, "THE LIVE VIEW", m_viewCard.reduced (18, 0).withHeight (44).translated (0, 2), look::ink, 13.0f);
    g.setColour (look::inkFaint);
    g.setFont (look::font (12.0f, 0));
    const auto pic = m_libShown;
    const juce::String info = pic != nullptr && pic->numBlocks > 0 ? juce::String (pic->numBlocks) + " pieces from " + juce::String ((int) pic->soundNames.size()) + " sounds" : juce::String();
    g.drawText (info, m_viewCard.reduced (150, 0).withHeight (44).translated (0, 2), juce::Justification::centredLeft, true);
    look::card (g, m_macroCard.toFloat());
    look::title (g, "MACROS", m_macroCard.reduced (18, 0).withHeight (40).translated (0, 2), look::ink, 13.0f);
    g.setColour (look::inkDim);
    g.setFont (look::spaced (11.5f, 1, 0.12f));
    g.drawText ("SEARCH STYLE", m_algo.getBounds().translated (-130, 0).withWidth (122), juce::Justification::centredRight);
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
    const int tabW = narrow ? 350 : 410;
    auto tabs = juce::Rectangle<int> (r.getRight() - pad - tabW, 26, tabW, 34);
    m_tabHood.setBounds (tabs.removeFromRight (narrow ? 156 : 180));
    tabs.removeFromRight (8);
    m_tabTerrain.setBounds (tabs.removeFromRight (narrow ? 100 : 114));
    tabs.removeFromRight (8);
    m_tabPlay.setBounds (tabs);
    m_statusArea = juce::Rectangle<int> (m_word.getRight() + 24, 14, m_tabPlay.getX() - m_word.getRight() - 48, 62);

    // bottom bar
    auto bar = juce::Rectangle<int> (pad, r.getHeight() - 66, r.getWidth() - 2 * pad, 50);
    auto bh = [] (juce::Rectangle<int> b) { return b.withSizeKeepingCentre (b.getWidth(), 42); };
    // (narrow windows: the side buttons give way so GENERATE keeps its room)
    m_undo.setBounds (bh (bar.removeFromLeft (narrow ? 72 : 84))); bar.removeFromLeft (8);
    m_redo.setBounds (bh (bar.removeFromLeft (narrow ? 72 : 84))); bar.removeFromLeft (8);
    m_ab.setBounds (bh (bar.removeFromLeft (narrow ? 84 : 96))); bar.removeFromLeft (8);
    m_presets.setBounds (bh (bar.removeFromLeft (narrow ? 150 : 200)));
    m_session.setBounds (bh (bar.removeFromRight (narrow ? 100 : 110))); bar.removeFromRight (10);
    m_drag.setBounds (bar.removeFromRight (narrow ? 124 : 150).withSizeKeepingCentre (narrow ? 124 : 150, 46)); bar.removeFromRight (10);
    m_mutate.setBounds (bh (bar.removeFromRight (narrow ? 104 : 128))); bar.removeFromRight (16);
    bar.removeFromLeft (16);
    m_generate.setBounds (bar.withSizeKeepingCentre (juce::jmin (300, bar.getWidth()), 50));

    auto body = juce::Rectangle<int> (pad, 110, r.getWidth() - 2 * pad, r.getHeight() - 110 - 94);
    m_hoodView.setBounds (body.withTrimmedRight (-12));
    m_terrain->setBounds (body);
    m_hood->setSize (body.getWidth() - 4, juce::jmax (m_hood->getHeight(), 400));
    m_hood->resized();

    auto left = body.removeFromLeft (colW);
    body.removeFromLeft (16);
    const int tgtH = 210;
    m_target.setBounds (left.removeFromBottom (tgtH));
    left.removeFromBottom (14);
    m_source.setBounds (left);

    m_macroCard = body.removeFromBottom (190);
    body.removeFromBottom (14);
    m_viewCard = body;
    auto vh = m_viewCard.reduced (14, 0).withHeight (44).translated (0, 2);
    m_viewHeat.setBounds (vh.removeFromRight (74).reduced (0, 7));
    vh.removeFromRight (6);
    m_viewDots.setBounds (vh.removeFromRight (74).reduced (0, 7));
    m_swarm.setBounds (m_viewCard.reduced (12).withTrimmedTop (40));

    auto mh = m_macroCard.reduced (14, 0).withHeight (40).translated (0, 2);
    m_algo.setBounds (mh.removeFromRight (230).reduced (0, 6));
    auto knobs = m_macroCard.reduced (12, 0).withTrimmedTop (46).withTrimmedBottom (10);
    const int kw = knobs.getWidth() / juce::jmax (1, m_macros.size());
    for (auto* m : m_macros) m->setBounds (knobs.removeFromLeft (kw).reduced (4, 0));
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
    return "Ghost Mosaic " + juce::String (GHOSTMOSAIC_VERSION) + " by GhostBlock. Free software, GNU General Public License v2 or later.\n\n"
           "Its sound engine is Samplebrain by Then Try This and contributors (GPL-2.0-or-later). Source: https://gitlab.com/then-try-this/samplebrain\n"
           "Source of this program: https://github.com/bsaxon430/ghosty-vst (folder ghost-mosaic)\n\n"
           "Also inside: FFTW (GPL-2.0-or-later, Matteo Frigo and MIT), Aquila DSP (MIT, Zbigniew Siciarz), JUCE (AGPLv3), "
           "Bencho's Particles and Heat map (MIT), Poppins font (SIL OFL 1.1).\n"
           "Full licences: the Licenses folder that came with this plug-in.";
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
