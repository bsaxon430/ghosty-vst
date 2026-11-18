#include "ProEditor.h"
#include "ProBank.h"
#include "ProBrowser.h"
#include "BendLaneEditor.h"
#include "Bypass.h"
#include "ProTheme.h"
#include "FxTheme.h"
#include "ProWavetableEditor.h"
#include "../Shared/GhostyProLogo.h"   // GHOSTY PRO logo (crowned ghost)
#include "ProGhostArt.h"        // GHOST ART (round 3)
#include "ProKeysExtras.h"   // KEYS (round 3): FLOW strip labels, HOLD, ghost cursor
#include "ProModUI.h"        // MOD (round 3)
#include "ProGlass.h"      // GLASS (round 3)
#include "ProLayout2.h"      // LAYOUT 2 (round 3)
#include <set>

using namespace ghosty;
using namespace gpro;
using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
static inline const pt::Palette& C() { return pt::pal(); }     // the current theme (read at paint time)
static constexpr float kGreyed = 0.78f;   // a control that has no effect right now: dimmed, but its text still reads (>= 3:1)

// ================================================================== look and feel
ProLnF::ProLnF (bool alwaysDark) : forceDark (alwaysDark) { applyTheme(); }

void ProLnF::applyTheme()
{
    pt::ScopedDark sd (forceDark);
    const auto& c = C();
    // one opaque palette (BLACK / PURPLE / LIGHT, Shared/GhostThemes.h) for every JUCE-drawn widget: labels, value boxes, menus,
    // tooltips, alerts, lists, scroll bars
    setColourScheme ({ c.bg, c.card, c.card, c.faint, c.white, c.ecto, c.onAccent, c.ecto, c.white });
    setColour (juce::ResizableWindow::backgroundColourId, c.bg);
    setColour (juce::TextButton::buttonColourId, c.card2);
    setColour (juce::TextButton::buttonOnColourId, c.slime);
    setColour (juce::TextButton::textColourOffId, c.white);
    setColour (juce::TextButton::textColourOnId, c.onAccent);
    setColour (juce::ComboBox::backgroundColourId, c.deep);
    setColour (juce::ComboBox::outlineColourId, c.faint);
    setColour (juce::ComboBox::textColourId, c.white);
    setColour (juce::ComboBox::arrowColourId, c.slime);
    setColour (juce::PopupMenu::backgroundColourId, c.card);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c.ecto);
    setColour (juce::PopupMenu::highlightedTextColourId, c.onAccent);
    setColour (juce::PopupMenu::textColourId, c.white);
    setColour (juce::PopupMenu::headerTextColourId, c.dim);
    setColour (juce::Slider::textBoxTextColourId, c.white);          // outlined value boxes
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, c.faint);
    setColour (juce::Slider::textBoxHighlightColourId, c.ecto.withAlpha (0.35f));
    setColour (juce::Label::textColourId, c.dim);
    setColour (juce::AlertWindow::backgroundColourId, c.card);
    setColour (juce::AlertWindow::textColourId, c.white);
    setColour (juce::AlertWindow::outlineColourId, c.faint);
    setColour (juce::TextEditor::backgroundColourId, c.deep);
    setColour (juce::TextEditor::textColourId, c.white);
    setColour (juce::TextEditor::highlightColourId, c.ecto.withAlpha (0.5f));
    setColour (juce::TextEditor::outlineColourId, c.slime.withAlpha (0.35f));
    setColour (juce::TextEditor::focusedOutlineColourId, c.slime);
    setColour (juce::CaretComponent::caretColourId, c.slime);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, c.ecto.withAlpha (0.5f));
    setColour (juce::TooltipWindow::backgroundColourId, c.card2);
    setColour (juce::TooltipWindow::textColourId, c.white);
    setColour (juce::TooltipWindow::outlineColourId, c.ecto);
}

void ProLnF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    pt::ScopedDark sd (forceDark);
    const auto f = getTextButtonFont (b, b.getHeight());
    g.setFont (f);
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.8f));
    const int yIndent = juce::jmin (4, b.proportionOfHeight (0.3f)), cornerSize = juce::jmin (b.getHeight(), b.getWidth()) / 2;
    const int fontHeight = juce::roundToInt (f.getHeight() * 0.6f);
    const int leftIndent = juce::jmin (fontHeight, 2 + cornerSize / (b.isConnectedOnLeft() ? 4 : 2)), rightIndent = juce::jmin (fontHeight, 2 + cornerSize / (b.isConnectedOnRight() ? 4 : 2));
    const int textWidth = b.getWidth() - leftIndent - rightIndent;
    if (textWidth > 0) g.drawFittedText (b.getButtonText(), leftIndent, yIndent, textWidth, b.getHeight() - yIndent * 2, juce::Justification::centred, 2);
}

void ProLnF::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (pghost::on() && ! forceDark && pghost::valueBoxLabel (g, label)) return;     // GHOST LOOK: a slider's value box = recessed glass
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    const float alpha = label.isEnabled() ? 1.0f : 0.8f;
    if (! label.isBeingEdited())
    {
        const juce::Font f (getLabelFont (label));
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (f);
        auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), area, label.getJustificationType(), juce::jmax (1, (int) ((float) area.getHeight() / f.getHeight())), label.getMinimumHorizontalScale());
        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    }
    else if (label.isEnabled()) g.setColour (label.findColour (juce::Label::outlineColourId));
    g.drawRect (label.getLocalBounds());
}

// GhostLnF's knob and button, drawn from the palette (DARK = the same pixels as before)
void ProLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    pt::ScopedDark sd (forceDark);
    const auto& c = C();
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
    if (pghost::on() && ! forceDark) { pghost::knob (g, cx, cy, rad, a0, a1, a0 + pos * (a1 - a0), s.isMouseOverOrDragging()); return; }    // GHOST LOOK: glass knob, violet -> teal arc
    juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
    g.setColour (c.dark ? juce::Colours::white.withAlpha (0.08f) : c.track); g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
    juce::ColourGradient gr (c.ecto, cx - rad, cy, c.slime, cx + rad, cy, false);
    g.setGradientFill (gr); g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (progl::on (c.theme)) progl::knobFace (g, cx, cy, rad * 0.62f);    // GLASS (round 3): a glassy cap (rim highlight, soft reflection)
    else
    {
    g.setColour (c.knobFace); g.fillEllipse (cx - rad * 0.62f, cy - rad * 0.62f, rad * 1.24f, rad * 1.24f);
    if (! c.dark) { g.setColour (c.faint); g.drawEllipse (cx - rad * 0.62f, cy - rad * 0.62f, rad * 1.24f, rad * 1.24f, 1.0f); }
    }
    juce::Path ptr; ptr.startNewSubPath (cx, cy); ptr.lineTo (cx + std::sin (ang) * rad * 0.55f, cy - std::cos (ang) * rad * 0.55f);
    g.setColour (s.isMouseOverOrDragging() ? c.slime : c.white); g.strokePath (ptr, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ProLnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    pt::ScopedDark sd (forceDark);
    const auto& c = C();
    auto r = b.getLocalBounds().toFloat().reduced (1);
    const bool on = b.getToggleState();
    if (pghost::on() && ! forceDark) { pghost::button (g, r, on, over, down, b.isEnabled(), (int) b.getProperties().getWithDefault ("accentRole", 0), c.pink, c.ice); return; }   // GHOST LOOK: pill glass
    if (const int role = (int) b.getProperties().getWithDefault ("accentRole", 0); role != 0 && on)
    {   // a button lit in its own colour (OSC 1 pink / OSC 2 blue)
        const auto acc = role == 1 ? c.pink : c.ice;
        if (progl::on (c.theme)) { progl::pill (g, r, 8, true, acc, over || down); return; }     // GLASS (round 3)
        juce::ColourGradient gr (acc.brighter (c.dark ? 0.1f : 0.0f), 0, r.getY(), acc.darker (0.2f), 0, r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 8);
        g.setColour (acc.contrasting (0.3f)); g.drawRoundedRectangle (r, 8, 1.0f);
        return;
    }
    if (c.dark)
    {
        if (on) { g.setColour (c.slime.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (2), 10); }
        juce::ColourGradient gr (on ? c.slime : c.btnTop, 0, r.getY(), on ? c.slime.darker (0.25f) : c.btnBot, 0, r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 8);
        g.setColour ((on ? c.slime : c.ecto).withAlpha (over || down ? 0.7f : 0.3f)); g.drawRoundedRectangle (r, 8, 1.0f);
        return;
    }
    if (progl::on (c.theme)) { progl::pill (g, r, 8, on, c.slime, over || down); return; }     // GLASS (round 3): a glass pill, lit in the accent
    juce::ColourGradient gr (on ? c.slime.brighter (0.15f) : c.btnTop, 0, r.getY(), on ? c.slime : c.btnBot, 0, r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r, 8);
    g.setColour (on ? c.slime.darker (0.3f) : (over || down ? c.ecto : c.faint)); g.drawRoundedRectangle (r, 8, 1.0f);
}

void ProLnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    pt::ScopedDark sd (forceDark);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2, 0);
    if (pghost::on() && ! forceDark) { pghost::linearSlider (g, r, pos, s.getMinimum() < 0 ? (float) s.valueToProportionOfLength (0.0) : 0.0f, s.isEnabled()); return; }   // GHOST LOOK
    const float cy = r.getCentreY();
    auto track = juce::Rectangle<float> (r.getX(), cy - 3, r.getWidth(), 6);
    g.setColour (C().dark ? juce::Colours::white.withAlpha (0.07f) : C().track); g.fillRoundedRectangle (track, 3);
    const bool bipolar = s.getMinimum() < 0;
    const float zero = bipolar ? r.getX() + r.getWidth() * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : r.getX();
    auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zero, pos), cy - 3, juce::jmax (zero, pos), cy + 3);
    const auto c = pos >= zero ? C().slime : C().pink;
    g.setColour (c.withAlpha (0.25f)); g.fillRoundedRectangle (fill.expanded (0, 3), 4);
    g.setColour (c); g.fillRoundedRectangle (fill, 3);
    if (bipolar) { g.setColour (C().white.withAlpha (0.35f)); g.fillRect (zero - 0.5f, cy - 7, 1.0f, 14.0f); }
    g.setColour (s.isMouseOverOrDragging() ? C().white : c.brighter (0.4f));
    g.fillEllipse (pos - 6, cy - 6, 12, 12);
}

void ProLnF::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    pt::ScopedDark sd (forceDark);
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    if (progl::on (C().theme)) progl::well (g, r, 6, C().deep, box.isMouseOver (true) ? C().slime : C().faint);    // GLASS (round 3): a frosted well
    else
    {
    g.setColour (C().deep); g.fillRoundedRectangle (r, 6);
    g.setColour ((box.isMouseOver (true) ? C().slime : (C().dark ? C().slime.withAlpha (0.3f) : C().faint))); g.drawRoundedRectangle (r, 6, 1.0f);
    }
    juce::Path arrow; const float ax = (float) w - 13, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (C().slime); g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// GLASS (round 3): a pop-down (call-out) in the glass LIGHT look: soft shadow, frosted fill, bright top edge; other looks = JUCE's own
void ProLnF::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage)
{
    if (forceDark || ! progl::on (C().theme)) { LookAndFeel_V4::drawCallOutBoxBackground (box, g, path, cachedImage); return; }
    if (cachedImage.isNull())
    {
        cachedImage = juce::Image (juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
        juce::Graphics g2 (cachedImage);
        juce::DropShadow (progl::tones().innerShade.withAlpha (0.22f), 12, { 0, 4 }).drawForPath (g2, path);
    }
    g.drawImageAt (cachedImage, 0, 0);
    const auto b = path.getBounds();
    g.setGradientFill (juce::ColourGradient (progl::tones().bodyTop, 0, b.getY(), progl::tones().bodyBottom, 0, b.getBottom(), false));
    g.fillPath (path);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.95f), 0, b.getY(), juce::Colours::white.withAlpha (0.0f), 0, b.getY() + 20, false));
    g.strokePath (path, juce::PathStrokeType (2.0f));
    g.setColour (progl::tones().rim); g.strokePath (path, juce::PathStrokeType (1.0f));
}

void ProLnF::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    pt::ScopedDark sd (forceDark);
    if (progl::on (C().theme)) { progl::well (g, { 0, 0, (float) w, (float) h }, 6, C().deep, juce::Colours::transparentBlack); return; }    // GLASS (round 3)
    g.setColour (C().deep); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
}
void ProLnF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& t)
{
    pt::ScopedDark sd (forceDark);
    g.setColour (t.hasKeyboardFocus (true) ? C().slime : C().slime.withAlpha (0.3f));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6, 1.0f);
}

// ================================================================== small shared bits
namespace
{

struct Card { juce::Rectangle<int> r; juce::String title; juce::Colour c; };

void paintCard (juce::Graphics& g, const Card& c)
{
    auto r = c.r.toFloat();
    const auto cc = pt::themed (c.c);
    if (progl::on (C().theme))
    {   // GLASS (round 3): a raised frosted card (cached shadow + body), the title in the card's colour
        progl::card (g, r, 12, cc);
        if (pghost::on() && pghost::hideTextForTests()) return;     // (GHOST LOOK tests: the pixels under the title)
        g.setColour (cc); g.setFont (font (10.5f));
        g.drawText (c.title, r.reduced (12, 0).removeFromTop (22).translated (0, 2), juce::Justification::centredLeft);
        return;
    }
    g.setColour (C().card); g.fillRoundedRectangle (r, 12);
    juce::ColourGradient top (cc.withAlpha (0.10f), 0, r.getY(), cc.withAlpha (0.0f), 0, r.getY() + 34, false);
    g.setGradientFill (top); g.fillRoundedRectangle (r, 12);
    g.setColour (cc.withAlpha (0.25f)); g.drawRoundedRectangle (r, 12, 1.0f);
    g.setColour (C().dark ? cc.withAlpha (0.9f) : cc); g.setFont (font (10.5f));
    g.drawText (c.title, r.reduced (12, 0).removeFromTop (22).translated (0, 2), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- MORE pop-down (SOUND ROUND 2, 2026-10-24)
// New controls never move an existing one: every card that grew extra settings gets a small MORE button in its free header space,
// which opens this panel in a call-out. Items: a knob, a choice box or an on/off button per parameter, laid out in a grid.
struct MorePanel : public juce::Component
{
    struct Item { juce::String id, caption; enum Kind { knob, combo, toggle, gap } kind; };   // gap = an empty cell (keeps rows readable)
    juce::String title;
    juce::OwnedArray<GhostKnob> knobs; juce::OwnedArray<juce::ComboBox> combos; juce::OwnedArray<juce::TextButton> toggles; juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<CA>> cAtt; std::vector<std::unique_ptr<BA>> bAtt;
    std::vector<juce::Component*> order; std::vector<juce::Label*> captions;
    MorePanel (juce::AudioProcessorValueTreeState& s, const juce::String& t, const std::vector<Item>& items, juce::LookAndFeel* lf) : title (t)
    {
        if (lf != nullptr) setLookAndFeel (lf);
        for (auto& it : items)
        {
            if (it.kind == Item::gap) { auto* lab = labels.add (new juce::Label()); addAndMakeVisible (lab); order.push_back (lab); continue; }
            auto* prm = s.getParameter (it.id);
            if (prm == nullptr) continue;
            if (it.kind == Item::knob)
            {
                auto* k = knobs.add (new GhostKnob (s, it.id, it.caption)); addAndMakeVisible (k);
                k->slider.setTooltip (prm->getName (60)); k->slider.getProperties().set ("paramId", it.id);
                order.push_back (k);
            }
            else if (it.kind == Item::combo)
            {
                auto* lab = labels.add (new juce::Label ({}, it.caption)); lab->setJustificationType (juce::Justification::centred); lab->setFont (font (10.0f)); addAndMakeVisible (lab); captions.push_back (lab);
                auto* c = combos.add (new juce::ComboBox()); addAndMakeVisible (c);
                if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm)) c->addItemList (ch->choices, 1);
                c->setTooltip (prm->getName (60));
                cAtt.push_back (std::make_unique<CA> (s, it.id, *c));
                order.push_back (c);
            }
            else
            {
                auto* b = toggles.add (new juce::TextButton (it.caption)); addAndMakeVisible (b); b->setClickingTogglesState (true);
                b->setTooltip (prm->getName (60));
                bAtt.push_back (std::make_unique<BA> (s, it.id, *b));
                order.push_back (b);
            }
        }
        const int n = (int) order.size(), cols = juce::jmin (4, juce::jmax (1, n)), rows = (n + cols - 1) / cols;
        setSize (16 + cols * 84, 30 + rows * 84);
        if (progl::on (C().theme)) sendLookAndFeelChange();    // GLASS (round 3): the knobs' value boxes take the theme's ink (they were built before joining the panel: white on the light glass)
    }
    ~MorePanel() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (C().card);
        if (progl::on (C().theme)) progl::card (g, getLocalBounds().toFloat(), 0, juce::Colours::transparentBlack, false);    // GLASS (round 3): frosted pop-down
        g.setColour (C().ecto); g.setFont (font (10.5f));
        g.drawText (title, getLocalBounds().removeFromTop (24).reduced (10, 0), juce::Justification::centredLeft);
    }
    void resized() override
    {
        const int cols = juce::jmin (4, juce::jmax (1, (int) order.size()));
        int i = 0, combo = 0;
        for (auto* c : order)
        {
            const juce::Rectangle<int> cell (8 + (i % cols) * 84, 26 + (i / cols) * 84, 84, 84);
            if (dynamic_cast<GhostKnob*> (c) != nullptr) c->setBounds (cell.reduced (2));
            else if (dynamic_cast<juce::ComboBox*> (c) != nullptr)
            {   // caption above, box below
                c->setBounds (cell.reduced (3).withTrimmedTop (34).withHeight (24));
                if (combo < captions.size()) captions[combo]->setBounds (cell.reduced (3).withTrimmedTop (12).withHeight (18));
                ++combo;
            }
            else if (dynamic_cast<juce::Label*> (c) != nullptr) c->setBounds (cell);                 // (a gap)
            else c->setBounds (cell.reduced (5).withTrimmedTop (30).withHeight (24));
            ++i;
        }
    }
    static void show (juce::Component* from, juce::AudioProcessorValueTreeState& s, const juce::String& title, const std::vector<Item>& items)
    {
        auto panel = std::make_unique<MorePanel> (s, title, items, &from->getLookAndFeel());
        if (const char* shot = std::getenv ("GHOSTY_MORESHOT"))
        {   // tests (no desktop under Xvfb): snapshot the panel to a PNG instead of opening the call-out
            panel->setSize (panel->getWidth(), panel->getHeight()); panel->resized();
            auto img = panel->createComponentSnapshot (panel->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f (shot); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
            return;
        }
        juce::CallOutBox::launchAsynchronously (std::move (panel), from->getScreenBounds(), nullptr);
    }
};

// a page = a component that owns its knobs / combos / toggles and draws its cards
class Page : public juce::Component
{
public:
    explicit Page (ProProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override { for (auto& c : cards) paintCard (g, c); }
    std::vector<juce::Rectangle<int>> cardRects() const { std::vector<juce::Rectangle<int>> v; for (auto& c : cards) v.push_back (c.r); return v; }

protected:
    ProProcessor& proc;
    juce::OwnedArray<GhostKnob> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::TextButton> toggles;
    std::vector<std::unique_ptr<CA>> cAtt;
    std::vector<std::unique_ptr<BA>> bAtt;
    std::vector<Card> cards;

    GhostKnob* knob (const juce::String& id, const juce::String& caption)
    {
        auto* k = knobs.add (new GhostKnob (proc.apvts, id, caption));
        addAndMakeVisible (k);
        if (auto* prm = proc.apvts.getParameter (id)) k->slider.setTooltip (prm->getName (60));
        k->slider.getProperties().set ("paramId", id);          // (MORPH: the knob shows the board's live value)
        return k;
    }
    juce::ComboBox* combo (const juce::String& id, juce::StringArray items = {})
    {
        auto* c = combos.add (new juce::ComboBox());
        addAndMakeVisible (c);
        if (items.isEmpty())
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) items = ch->choices;
        c->addItemList (items, 1);
        cAtt.push_back (std::make_unique<CA> (proc.apvts, id, *c));
        return c;
    }
    juce::TextButton* toggle (const juce::String& id, const juce::String& text)
    {
        auto* b = toggles.add (new juce::TextButton (text));
        addAndMakeVisible (b); b->setClickingTogglesState (true);
        bAtt.push_back (std::make_unique<BA> (proc.apvts, id, *b));
        return b;
    }
    juce::Rectangle<int> card (int x, int y, int w, int h, const juce::String& title, juce::Colour c)
    {
        cards.push_back ({ { x, y, w, h }, title, c });
        return { x, y, w, h };
    }
    static juce::Rectangle<int> knobArea (juce::Rectangle<int> r) { return r.withTrimmedTop (24).withTrimmedBottom (4).reduced (6, 0); }
    static void placeRow (const std::vector<GhostKnob*>& ks, juce::Rectangle<int> area)
    {
        const int n = (int) ks.size(); if (n == 0) return;
        const float w = (float) area.getWidth() / (float) n;
        for (int i = 0; i < n; ++i) ks[(size_t) i]->setBounds (area.getX() + juce::roundToInt (i * w), area.getY(), juce::roundToInt (w), area.getHeight());
    }
};

float paramValue (ProProcessor& p, const juce::String& id) { auto* v = p.apvts.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; }
} // namespace

#include "ProWavePalette.inc"      // LAYOUT 2 (round 3): the WAVE PALETTE (STUDIO under each display; CLASSIC in the OSC MORE pop-down)

// ================================================================== wave screen (3D wavetable + drawing + WAV drop)
class WaveScreen : public juce::Component, private juce::Timer, public juce::FileDragAndDropTarget, public juce::TooltipClient
{
public:
    explicit WaveScreen (ProProcessor& p) : proc (p) { startTimerHz (30); setWantsKeyboardFocus (true); setMouseClickGrabsKeyboardFocus (true); }
    int osc = 0; bool editB = false;
    std::function<void (const juce::String&)> onMessage;

    juce::String tblId() const { return osc == 0 ? "o1_tbl" : "o2_tbl"; }
    juce::String posId() const { return osc == 0 ? "o1_pos" : "o2_pos"; }
    bool ghostMode() const { return (int) paramValue (proc, tblId()) == tGhost; }

    // GHOST A/B (the drawable table): a flat, editable single-cycle curve with a grid; any other table: the 3D stack
    void paint (juce::Graphics& g) override { if (ghostMode()) paint2D (g); else paint3D (g); }

    void paint3D (juce::Graphics& g)
    {
        pt::ScopedDark screen;     // a display: dark in both themes
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 10); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3): light frosted screen
        const float pad = 10, dX = r.getWidth() * 0.12f, dY = r.getHeight() * 0.25f;
        front = { pad, pad + dY, r.getWidth() - 2 * pad - dX, r.getHeight() - 2 * pad - dY };
        g.setColour (C().ecto.withAlpha (0.08f));
        for (int i = 0; i <= 8; ++i) { const float x = front.getX() + i / 8.0f * front.getWidth(); g.drawLine (x, front.getY(), x, front.getBottom()); }
        for (int i = 0; i <= 4; ++i) { const float y = front.getY() + i / 4.0f * front.getHeight(); g.drawLine (front.getX(), y, front.getRight(), y); }

        const float pos = paramValue (proc, posId());
        const float lvl = proc.level.load();
        std::vector<const Frame*> frames;
        if (ghostMode()) { frames = { editB ? &proc.frameB : &proc.frameA, editB ? &proc.frameA : &proc.frameB }; }
        else if (auto t = proc.uiTable (osc)) { held = t; for (auto& f : t->preview) frames.push_back (&f); }
        if (frames.empty()) return;
        const int K = (int) frames.size();
        auto valueAt = [&] (float t, int i)
        {
            const float x = juce::jlimit (0.0f, 1.0f, t) * (float) (K - 1);
            const int a = juce::jmin (K - 2, (int) x); if (K == 1) return (*frames[0])[(size_t) i];
            const float f = x - (float) a;
            return (*frames[(size_t) a])[(size_t) i] * (1 - f) + (*frames[(size_t) a + 1])[(size_t) i] * f;
        };
        // WARP (round 3): the display shows the warped frame - the read position of each point goes through the oscillator's two slots
        const juce::String wp = osc == 0 ? "o1_" : "o2_";
        const int wm1 = (int) paramValue (proc, wp + "w1"), wm2 = (int) paramValue (proc, wp + "w2");
        const float wa1 = paramValue (proc, wp + "w1a"), wa2 = paramValue (proc, wp + "w2a");
        const bool warped = (wm1 != gpro::wOff && wa1 > 0.0f && gpro::warpIsPhase (wm1)) || (wm2 != gpro::wOff && wa2 > 0.0f && gpro::warpIsPhase (wm2));
        auto valueAtPhase = [&] (float t, double ph)
        {   // the frame read at a fractional position (linear between the preview's points)
            ph -= std::floor (ph); const double x = ph * FRAME; const int i0 = (int) x; const float f = (float) (x - i0);
            return valueAt (t, i0 % FRAME) * (1.0f - f) + valueAt (t, (i0 + 1) % FRAME) * f;
        };
        auto pathFor = [&] (float t, float depth)
        {
            juce::Path p;
            for (int i = 0; i <= FRAME; i += 2)
            {
                float v;
                if (warped)
                {
                    double ph = (double) (i % FRAME) / FRAME;
                    if (wm1 != gpro::wOff && wa1 > 0.0f && gpro::warpIsPhase (wm1)) ph = gpro::warpPhase (wm1, ph, wa1);
                    if (wm2 != gpro::wOff && wa2 > 0.0f && gpro::warpIsPhase (wm2)) ph = gpro::warpPhase (wm2, ph, wa2);
                    v = valueAtPhase (t, ph);
                }
                else v = valueAt (t, i % FRAME);
                const float x = front.getX() + depth * dX + (float) i / FRAME * front.getWidth(), y = front.getCentreY() - depth * dY - v * front.getHeight() * 0.44f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        const float shownPos = ghostMode() && editB ? 1.0f - pos : pos;
        const int layers = juce::jlimit (8, 24, K * 2);
        for (int k = layers; k >= 0; --k)
        {
            const float t = (float) k / (float) layers;
            const float near = 1.0f - juce::jmin (1.0f, std::abs (t - shownPos) * 3.0f);
            g.setColour (C().slime.interpolatedWith (C().ecto, t).withAlpha (0.08f + (1 - t) * 0.12f + near * 0.25f));
            g.strokePath (pathFor (t, t), juce::PathStrokeType (1.0f));
        }
        auto mp = pathFor (shownPos, shownPos);
        const auto oscAcc = osc == 0 ? C().pink : C().ice;          // OSC 1 pink, OSC 2 blue
        for (int k = 3; k >= 1; --k) { g.setColour (oscAcc.withAlpha (0.07f)); g.strokePath (mp, juce::PathStrokeType (2.6f + k * 4.0f)); }
        g.setColour (oscAcc); g.strokePath (mp, juce::PathStrokeType (2.6f + lvl * 3.0f));
        // front frame filled
        auto fp = pathFor (0, 0);
        auto fill = fp; fill.lineTo (front.getRight(), front.getCentreY()); fill.lineTo (front.getX(), front.getCentreY()); fill.closeSubPath();
        juce::ColourGradient fg (C().slime.withAlpha (0.22f), 0, front.getY(), C().ecto.withAlpha (0.26f), 0, front.getBottom(), false);
        g.setGradientFill (fg); g.fillPath (fill);
        if (shownPos > 0.02f) { g.setColour (C().white.withAlpha (0.8f)); g.strokePath (fp, juce::PathStrokeType (1.4f)); }

        g.setFont (font (10.5f)); g.setColour (oscAcc);
        const auto name = gpro::eng::tableChoiceNames()[(int) paramValue (proc, tblId())];   // (ENGINES round 3: the engines' names too)
        juce::String label = "OSC " + juce::String (osc + 1) + "  /  " + name.toUpperCase();
        if ((int) paramValue (proc, tblId()) == tSample && proc.sampleName().isNotEmpty()) label << "  (" << proc.sampleName() << ")";
        if ((int) paramValue (proc, tblId()) == tUser) label << "  (" << (proc.userTableName().isNotEmpty() ? proc.userTableName() : juce::String ("empty: Ghost A/B")) << ")";
        g.drawText (label, juce::Rectangle<float> (12, 6, r.getWidth() - 24, 14), juce::Justification::centredLeft);
        g.setColour (C().dim);
        g.drawText ("press DRAW A / DRAW B to draw your own wave  -  drop a WAV: sample -> wavetable",
                    juce::Rectangle<float> (12, r.getHeight() - 18, r.getWidth() - 24, 14), juce::Justification::centredRight);
        g.drawText ("POS " + juce::String (juce::roundToInt (pos * 100)) + "%", juce::Rectangle<float> (12, r.getHeight() - 18, 120, 14), juce::Justification::centredLeft);
        if (glitch > 0.05f) { g.setColour (C().slime.withAlpha (glitch * 0.15f)); g.fillRect (r.withHeight (6 + glitch * 20).withY (juce::Random::getSystemRandom().nextFloat() * r.getHeight())); }
        if (dragOver) { g.setColour (C().slime.withAlpha (0.15f)); g.fillRoundedRectangle (r, 10); g.setColour (C().slime); g.drawRoundedRectangle (r.reduced (1), 10, 2.0f);
                        g.setFont (font (16.0f)); g.drawText ("DROP TO MAKE A WAVETABLE", r, juce::Justification::centred); }
    }

    // ---- drawing (GHOST A/B): drag = freehand (filled between mouse samples), SHIFT = a straight line from where the stroke began,
    //      right-click = menu (undo / redo, smooth, stepped, mirror, reset, normalise). DRAW A / DRAW B on another table switches this oscillator
    //      to GHOST A/B, starting from the wave it was playing; a plain click on the display of another table never draws.
    bool smoothOn = true, mirrorOn = false; int steps = 0;     // steps: 0 = free, else the cycle is drawn in that many flat steps
    void mouseDown (const juce::MouseEvent& e) override
    {
        // drawing is deliberate: on any other table (CLASSIC etc.) a click on the display does nothing (it only shows the wave),
        // so POS / the table can be moved around without ever starting to draw. DRAW A / DRAW B (or the A/B shape boxes) switch to GHOST A/B.
        if (! ghostMode()) return;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        pushUndo();
        strokeBase = editFrame(); downPos = e.position; last = -1;
        paintAt (e.position, e.mods.isShiftDown());
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu() && ghostMode()) paintAt (e.position, e.mods.isShiftDown()); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! ghostMode() || last < 0) return;
        finishStroke(); last = -1; repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {   // cmd / ctrl + Z = undo, + SHIFT (or + Y) = redo
        if (! k.getModifiers().isCommandDown()) return false;
        if (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z') { if (k.getModifiers().isShiftDown()) redo(); else undo(); return true; }
        if (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y') { redo(); return true; }
        return false;
    }
    juce::String getTooltip() override
    {
        return ghostMode() ? "Drag to draw the wave. SHIFT = straight line, right-click = undo / smooth / stepped / mirror / reset"
                           : "This shows the wave (clicking here never draws). To draw your own wave press DRAW A / DRAW B below. Drop a WAV to make a wavetable.";
    }

    ghosty::Frame& editFrame() { return editB ? proc.frameB : proc.frameA; }
    void ensureGhost()
    {
        if (ghostMode()) return;
        // seed A and B with the wave this oscillator plays now (unless the other oscillator already uses GHOST A/B: keep its frames)
        const int other = (int) paramValue (proc, osc == 0 ? "o2_tbl" : "o1_tbl");
        if (other != tGhost)
            if (auto t = proc.uiTable (osc); t != nullptr && ! t->preview.empty())
            {
                const float pos = paramValue (proc, posId());
                const int K = (int) t->preview.size();
                const float x = juce::jlimit (0.0f, 1.0f, pos) * (float) (K - 1);
                const int a = juce::jmin (K - 1, (int) x), b2 = juce::jmin (K - 1, a + 1); const float fr = x - (float) a;
                ghosty::Frame f;
                for (int i = 0; i < FRAME; ++i) f[(size_t) i] = t->preview[(size_t) a][(size_t) i] * (1 - fr) + t->preview[(size_t) b2][(size_t) i] * fr;
                normaliseFrame (f);
                proc.frameA = f; proc.frameB = f;
            }
        proc.setParam (tblId(), (float) tGhost);
        proc.rebuildWaves();
        if (onMessage) onMessage ("OSC " + juce::String (osc + 1) + " now plays GHOST A/B: draw away");
    }
    void undo()
    {
        if (undoStack.empty()) return;
        redoStack.push_back ({ proc.frameA, proc.frameB });
        proc.frameA = undoStack.back().first; proc.frameB = undoStack.back().second; undoStack.pop_back();
        proc.rebuildWaves(); repaint();
    }
    void redo()
    {
        if (redoStack.empty()) return;
        undoStack.push_back ({ proc.frameA, proc.frameB });
        proc.frameA = redoStack.back().first; proc.frameB = redoStack.back().second; redoStack.pop_back();
        proc.rebuildWaves(); repaint();
    }
    void resetFrame()
    {
        ensureGhost(); pushUndo();
        editFrame() = ghosty::makeShape ((int) paramValue (proc, editB ? "waveB" : "waveA"));
        proc.rebuildWaves(); repaint();
    }
    bool canUndo() const { return ! undoStack.empty(); }
    juce::Rectangle<float> drawArea() const { return getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (24.0f); }

    void randomise()
    {
        ensureGhost(); pushUndo();
        auto& r = juce::Random::getSystemRandom();
        float amps[24]; double ph[24];
        for (int k = 0; k < 24; ++k) { amps[k] = std::pow (r.nextFloat(), 2.2f) / std::pow ((float) (k + 1), 0.55f) * (k == 0 ? 2.0f : 1.0f); ph[k] = r.nextDouble() * juce::MathConstants<double>::twoPi; }
        auto& f = editB ? proc.frameB : proc.frameA;
        for (int i = 0; i < FRAME; ++i)
        {
            double v = 0; for (int k = 0; k < 24; ++k) v += amps[k] * std::sin (juce::MathConstants<double>::twoPi * (k + 1) * i / FRAME + ph[k]);
            f[(size_t) i] = (float) v;
        }
        normaliseFrame (f); proc.rebuildWaves(); glitch = 1.0f; repaint();
    }

    void importFile (const juce::File& file)
    {
        juce::String msg;
        if (proc.importSample (file, msg)) { proc.setParam (tblId(), (float) tSample); glitch = 1.0f; }
        if (onMessage) onMessage (msg);
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg")) { importFile (juce::File (f)); break; }
    }

private:
    ProProcessor& proc;
    std::shared_ptr<const Table> held;
    int last = -1; float lastV = 0; bool dirty = false, dragOver = false; int sinceRebuild = 0; float glitch = 0;
    juce::Rectangle<float> front;

    ghosty::Frame strokeBase {};
    juce::Point<float> downPos;
    std::vector<std::pair<ghosty::Frame, ghosty::Frame>> undoStack, redoStack;
    void pushUndo()
    {
        undoStack.push_back ({ proc.frameA, proc.frameB }); redoStack.clear();
        if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    }
    int indexAt (float x) const { const auto a = drawArea(); return juce::jlimit (0, FRAME - 1, (int) std::round ((x - a.getX()) / a.getWidth() * (FRAME - 1))); }
    float valueAt (float y) const { const auto a = drawArea(); return juce::jlimit (-1.0f, 1.0f, -(y - a.getCentreY()) / (a.getHeight() * 0.5f)); }
    void setPoint (ghosty::Frame& f, int i, float v)
    {
        if (steps > 0)
        {   // stepped: the whole step this point falls in takes the value
            const int w = FRAME / steps, s0 = (i / w) * w;
            for (int k = s0; k < juce::jmin (FRAME, s0 + w); ++k) { f[(size_t) k] = v; if (mirrorOn) f[(size_t) ((k + FRAME / 2) % FRAME)] = -v; }
            return;
        }
        f[(size_t) i] = v;
        if (mirrorOn) f[(size_t) ((i + FRAME / 2) % FRAME)] = -v;     // half-wave mirror: odd harmonics only (square / triangle family)
    }
    void paintAt (juce::Point<float> p, bool line)
    {
        auto& f = editFrame();
        const int i = indexAt (p.x); const float v = valueAt (p.y);
        if (line)
        {   // SHIFT: a straight line from where the stroke began, over the frame as it was before the stroke
            f = strokeBase;
            const int i0 = indexAt (downPos.x); const float v0 = valueAt (downPos.y);
            const int a = juce::jmin (i0, i), b = juce::jmax (i0, i);
            for (int k = a; k <= b; ++k) setPoint (f, k, b == a ? v : (i0 < i ? v0 + (v - v0) * (float) (k - a) / (float) (b - a) : v + (v0 - v) * (float) (k - a) / (float) (b - a)));
        }
        else if (last < 0) setPoint (f, i, v);
        else
        {   // fill every index between the last mouse sample and this one (a fast stroke leaves no gaps)
            const int a = juce::jmin (last, i), b = juce::jmax (last, i);
            const float va = last < i ? lastV : v, vb = last < i ? v : lastV;
            for (int k = a; k <= b; ++k) setPoint (f, k, b == a ? v : va + (vb - va) * (float) (k - a) / (float) (b - a));
        }
        last = i; lastV = v; dirty = true; glitch = 0.4f; repaint();
    }
    // end of a stroke: optional smoothing, DC removed, peak normalised (a silent frame stays silent), the table rebuilt
    void finishStroke()
    {
        auto& f = editFrame();
        if (smoothOn && steps == 0)
            for (int pass = 0; pass < 2; ++pass)
            {
                auto c = f;
                for (int i = 0; i < FRAME; ++i) f[(size_t) i] = (c[(size_t) ((i + FRAME - 1) % FRAME)] + 2 * c[(size_t) i] + c[(size_t) ((i + 1) % FRAME)]) * 0.25f;
            }
        float mean = 0; for (auto v : f) mean += v; mean /= (float) FRAME;
        float mx = 0; for (auto& v : f) { v -= mean; mx = std::max (mx, std::abs (v)); }
        if (mx > 1.0e-3f) for (auto& v : f) v /= mx; else f.fill (0.0f);
        for (auto& v : f) if (! std::isfinite (v)) v = 0.0f;
        proc.rebuildWaves(); dirty = false;
    }
    void showMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (juce::String ("DRAW ") + (editB ? "B" : "A") + "  -  OSC " + juce::String (osc + 1));
        m.addItem (1, "Undo", canUndo()); m.addItem (2, "Redo", ! redoStack.empty());
        m.addSeparator();
        m.addItem (3, "Smooth strokes", true, smoothOn);
        m.addItem (4, "Free drawing", true, steps == 0); m.addItem (5, "Stepped: 16 steps", true, steps == 16); m.addItem (6, "Stepped: 32 steps", true, steps == 32);
        m.addItem (7, "Mirror (half-wave symmetry, odd harmonics)", true, mirrorOn);
        m.addSeparator();
        m.addItem (8, juce::String ("Reset ") + (editB ? "B" : "A") + " to its shape");
        m.addItem (9, juce::String ("Copy ") + (editB ? "B to A" : "A to B"));
        juce::Component::SafePointer<WaveScreen> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& w = *safe;
            switch (r)
            {
                case 1: w.undo(); break;
                case 2: w.redo(); break;
                case 3: w.smoothOn = ! w.smoothOn; break;
                case 4: w.steps = 0; break;
                case 5: w.steps = 16; break;
                case 6: w.steps = 32; break;
                case 7: w.mirrorOn = ! w.mirrorOn; break;
                case 8: w.resetFrame(); break;
                case 9: w.ensureGhost(); w.pushUndo(); if (w.editB) w.proc.frameA = w.proc.frameB; else w.proc.frameB = w.proc.frameA; w.proc.rebuildWaves(); break;
                default: break;
            }
            w.repaint();
        });
    }
    void paint2D (juce::Graphics& g)
    {
        pt::ScopedDark screen;     // a display: dark in both themes
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 10); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3): light frosted screen
        const auto a = drawArea();
        g.setColour (C().white.withAlpha (0.07f));
        const int cols = steps > 0 ? steps : 16;
        for (int i = 0; i <= cols; ++i) { const float x = a.getX() + (float) i / (float) cols * a.getWidth(); g.drawLine (x, a.getY(), x, a.getBottom()); }
        for (int i = 0; i <= 8; ++i) { const float y = a.getY() + (float) i / 8.0f * a.getHeight(); g.drawLine (a.getX(), y, a.getRight(), y); }
        g.setColour (C().white.withAlpha (0.25f)); g.drawLine (a.getX(), a.getCentreY(), a.getRight(), a.getCentreY(), 1.0f);
        auto pathOf = [&] (const ghosty::Frame& f)
        {
            juce::Path p;
            for (int i = 0; i < FRAME; ++i) { const float x = a.getX() + (float) i / (FRAME - 1) * a.getWidth(), y = a.getCentreY() - f[(size_t) i] * a.getHeight() * 0.5f; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            return p;
        };
        const auto& other = editB ? proc.frameA : proc.frameB;
        const auto& mine = editB ? proc.frameB : proc.frameA;
        g.setColour ((editB ? C().slime : C().pink).withAlpha (0.35f)); g.strokePath (pathOf (other), juce::PathStrokeType (1.4f));
        auto mp = pathOf (mine);
        auto fill = mp; fill.lineTo (a.getRight(), a.getCentreY()); fill.lineTo (a.getX(), a.getCentreY()); fill.closeSubPath();
        const auto ac = editB ? C().pink : C().slime;
        g.setColour (ac.withAlpha (0.16f)); g.fillPath (fill);
        g.setColour (ac.withAlpha (0.25f)); g.strokePath (mp, juce::PathStrokeType (6.0f));
        g.setColour (ac); g.strokePath (mp, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (font (10.5f)); g.setColour (osc == 0 ? C().pink : C().ice);
        g.drawText ("OSC " + juce::String (osc + 1) + "  /  GHOST A/B  -  drawing " + (editB ? "B" : "A") + "  (the faint line is " + (editB ? "A" : "B") + ";  POS morphs A -> B)",
                    juce::Rectangle<float> (12, 5, r.getWidth() - 24, 14), juce::Justification::centredLeft);
        g.setColour (ac);
        juce::String opts; if (steps > 0) opts << "  -  stepped " << steps; if (mirrorOn) opts << "  -  mirror"; if (! smoothOn) opts << "  -  raw";
        g.drawText (juce::String ("DRAW ") + (editB ? "B" : "A") + " is on: drag here to draw the wave;  SHIFT = straight line;  right-click = menu" + opts,
                    juce::Rectangle<float> (12, r.getHeight() - 19, r.getWidth() - 24, 14), juce::Justification::centredLeft);
        if (dragOver) { g.setColour (C().slime.withAlpha (0.15f)); g.fillRoundedRectangle (r, 10); g.setColour (C().slime); g.drawRoundedRectangle (r.reduced (1), 10, 2.0f);
                        g.setFont (font (16.0f)); g.drawText ("DROP TO MAKE A WAVETABLE", r, juce::Justification::centred); }
    }

    void timerCallback() override
    {
        if (dirty && ++sinceRebuild > 3) { proc.rebuildWaves(); dirty = false; sinceRebuild = 0; }
        glitch *= 0.88f;
        repaint();
    }
};

// ================================================================== flow strip (slide across the scale)
class FlowStrip : public juce::Component, private juce::Timer
{
public:
    explicit FlowStrip (ProProcessor& p) : latch (p.keyboardState), proc (p) { startTimerHz (40); setName ("FLOW"); cursor.attach (*this); }   // KEYS (round 3): named for the tests; the ghost cursor overlay
    // KEYS (round 3): HOLD (latch) and OCT - / + for this strip; session controls, never saved
    keysx::NoteLatch latch;
    keysx::StripOctave octave;
    keysx::GhostCursor cursor;                 // KEYS (round 3): never takes a mouse event
    void mouseDown (const juce::MouseEvent& e) override { down = true; play (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { play (e.position); }
    void mouseUp (const juce::MouseEvent&) override { down = false; release(); }

    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;     // a display: dark in both themes
        buildNotes();
        auto b = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, b, 10); else { g.setColour (juce::Colour (0xff050608)); g.fillRoundedRectangle (b, 10); }    // GLASS (round 3)
        if (count == 0) return;
        const float cw = b.getWidth() / (float) count;
        const int key = (int) proc.keyP->load();
        for (int i = 0; i < count; ++i)
        {
            const int n = notes[i];
            const bool on = proc.keyboardState.isNoteOnForChannels (0xffff, n) || proc.isNoteSounding (n);
            if (on) flash[n] = juce::jmax (flash[n], 0.8f);
            const auto c = colourFor (n);
            juce::ColourGradient gr (c.withAlpha (0.05f + flash[n] * 0.55f), 0, 0, c.withAlpha (0.14f + flash[n] * 0.35f + proc.level.load() * 0.15f), 0, b.getHeight(), false);
            g.setGradientFill (gr); g.fillRect (i * cw + 1, 0.0f, cw - 2, b.getHeight());
            if (((n - key) % 12 + 12) % 12 == 0) { g.setColour (c.withAlpha (0.9f)); g.fillRect (i * cw + 1, 0.0f, cw - 2, 3.0f); }
            // KEYS (round 3): the label takes a fixed readable ink (white / black) against the key colour behind it
            {
                const float ly = keysx::labelYFrac (b.getHeight());
                const auto base = progl::on (C().theme) ? progl::tones().screenTop.interpolatedWith (progl::tones().screenBottom, ly) : juce::Colour (keysx::stripBase);   // GLASS (round 3): the light screen behind
                g.setColour (keysx::labelInk (keysx::keyBehind (c, flash[n], proc.level.load(), ly, base))); g.setFont (font (9.0f));
            }
            g.drawText (keyNames()[n % 12], juce::Rectangle<float> (i * cw, b.getHeight() - 16, cw, 14), juce::Justification::centred);
        }
        // the STACK's KEY SPLIT, when a layer uses it
        bool keySplit = false;
        for (int k = 0; k < NUM_LAYERS; ++k) { const int pm = (int) proc.ly[k].play->load(); keySplit |= pm == pmKeyLow || pm == pmKeyHigh; }
        if (keySplit)
        {
            const int split = (int) proc.stkKSplit->load(); int at = -1;
            for (int i = 0; i < count; ++i) if (notes[i] >= split) { at = i; break; }
            if (at >= 0)
            {
                g.setColour (C().ecto.withAlpha (0.9f)); g.fillRect (at * cw - 1, 0.0f, 2.0f, b.getHeight());
                g.setFont (font (8.5f)); g.drawText ("KEY SPLIT " + juce::MidiMessage::getMidiNoteName (split, true, true, 4), juce::Rectangle<float> (at * cw + 4, b.getHeight() - 30, 90, 12), juce::Justification::centredLeft);
            }
        }
        for (auto& r : ripples) { g.setColour (r.c.withAlpha (r.a)); g.drawEllipse (r.x - r.r, r.y - r.r, r.r * 2, r.r * 2, 2.0f); }
        if (down && current >= 0)
        {
            const auto c = colourFor (current);
            juce::ColourGradient glow (c.withAlpha (0.8f), finger.x, finger.y, c.withAlpha (0.0f), finger.x + 60, finger.y, true);
            g.setGradientFill (glow); g.fillEllipse (finger.x - 60, finger.y - 60, 120, 120);
            const bool flipG = juce::Random::getSystemRandom().nextBool();
            drawGhost (g, lookFor ((current * 7) % 35), finger.x, finger.y - 4, 22, 0.4f + proc.level.load(), flipG);
            if (pghost::on()) pghost::outlineGhost (g, finger.x, finger.y - 4, 22, 0.85f, flipG);     // GHOST LOOK: legible on the glass screen
        }
        g.setColour (C().dim); g.setFont (font (9.0f));
        g.drawText ("FLOW  - slide to play  (higher = louder)", b.reduced (8, 4).removeFromTop (12), juce::Justification::centredLeft);
    }

private:
    ProProcessor& proc;
    int current = -1; bool down = false, currentOn = false; juce::Point<float> finger;   // KEYS (round 3): currentOn = the latch let it sound
    float flash[128] {};
    struct Ripple { float x, y, r, a; juce::Colour c; };
    std::vector<Ripple> ripples;
    int notes[64] {}; int count = 0;

    void buildNotes()
    {
        const int key = (int) proc.keyP->load(), scale = (int) proc.scaleP->load(), mask = scaleMask (scale);
        const int start = juce::jlimit (0, 127 - 36, 48 + key + 12 * octave.value), end = start + 36;   // KEYS (round 3): OCT - / +
        count = 0;
        for (int n = start; n <= end && count < 64; ++n) if (inScale (n, key, mask)) notes[count++] = n;
    }
    juce::Colour colourFor (int note) const
    {
        return keysx::keyColour (note - (int) proc.keyP->load());   // KEYS (round 3): the same colours, one formula shared with the label test
    }
    void play (juce::Point<float> p)
    {
        buildNotes();
        if (count == 0) return;
        finger = p;
        const int idx = juce::jlimit (0, count - 1, (int) (p.x / (float) getWidth() * (float) count));
        const int note = notes[idx];
        if (note == current) return;
        release();
        const float vel = juce::jlimit (0.2f, 1.0f, 1.0f - p.y / (float) getHeight() + 0.2f);
        currentOn = latch.press (note, vel);      // KEYS (round 3): through HOLD (a held note played again lets go)
        current = note; if (currentOn) flash[note] = 1.0f;
        ripples.push_back ({ p.x, p.y, 6.0f, 1.0f, colourFor (note) });
    }
    void release() { if (current >= 0) latch.release (current, currentOn); current = -1; currentOn = false; }   // KEYS (round 3): HOLD keeps it
    void timerCallback() override
    {
        for (auto& f : flash) f *= 0.9f;
        for (auto& r : ripples) { r.r += 3.0f; r.a *= 0.9f; }
        ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.a < 0.03f; }), ripples.end());
        repaint();
    }
};

// ================================================================== SYNTH page
class SynthPage : public Page, private juce::Timer
{
public:
    std::function<void (const juce::String&)> onMessage;

    explicit SynthPage (ProProcessor& p) : Page (p), wave (p)
    {
        addAndMakeVisible (wave); addAndMakeVisible (pad);
        wave.onMessage = [this] (const juce::String& m) { if (onMessage) onMessage (m); };
        for (auto* b : { &view1, &view2, &drawA, &drawB, &dice, &importBtn, &editBtn }) addAndMakeVisible (b);
        // WAVETABLE EDITOR (round 3 item 2b): its own window, opened from the free space of the wave toolbar
        editBtn.setTooltip ("WAVETABLE EDITOR: frames, drawing, harmonic + phase bars, morph between key frames, generate, a sample cut at a chosen cycle length, save to the user bank. Opens in its own window");
        editBtn.onClick = [this] { openEditor(); };
        view1.setToggleState (true, juce::dontSendNotification);
        view1.getProperties().set ("accentRole", 1); view2.getProperties().set ("accentRole", 2);     // OSC 1 pink, OSC 2 blue (lit when shown)
        view1.setTooltip ("Show OSC 1 (pink) in the display - or click anywhere in the OSC 1 panel"); view2.setTooltip ("Show OSC 2 (blue) in the display - or click anywhere in the OSC 2 panel");
        view1.onClick = [this] { setView (0); };
        view2.onClick = [this] { setView (1); };
        drawA.setToggleState (true, juce::dontSendNotification);
        // DRAW A / DRAW B: pick the frame to draw; on another table this switches the oscillator to GHOST A/B (seeded with what it plays)
        drawA.onClick = [this] { wave.editB = false; wave.ensureGhost(); drawA.setToggleState (true, juce::dontSendNotification); drawB.setToggleState (false, juce::dontSendNotification); wave.repaint(); };
        drawB.onClick = [this] { wave.editB = true; wave.ensureGhost(); drawB.setToggleState (true, juce::dontSendNotification); drawA.setToggleState (false, juce::dontSendNotification); wave.repaint(); };
        drawA.setTooltip ("Draw frame A of GHOST A/B (POS at 0 %) - drag on the display above; right-click it for undo / smooth / stepped / mirror");
        drawB.setTooltip ("Draw frame B of GHOST A/B (POS at 100 %) - drag on the display above; right-click it for undo / smooth / stepped / mirror");
        dice.onClick = [this] { wave.randomise(); };
        importBtn.onClick = [this] { chooseFile(); };
        dice.setTooltip ("Random wave into the GHOST A/B frame you are drawing");
        importBtn.setTooltip ("Turn a sample into a wavetable (or drop a WAV on the screen)");

        tbl1 = combo ("o1_tbl"); tbl2 = combo ("o2_tbl");
        waveA = combo ("waveA"); waveB = combo ("waveB");
        waveA->setTooltip ("GHOST A/B: the shape of frame A (POS 0 %). Picking one switches this oscillator to GHOST A/B: POS then morphs A -> B");
        waveB->setTooltip ("GHOST A/B: the shape of frame B (POS 100 %). Picking one switches this oscillator to GHOST A/B: POS then morphs A -> B");
        for (auto* c : { waveA, waveB }) c->addMouseListener (this, false);   // a click on an A/B shape box switches to GHOST A/B (mouseDown)

        pad.addGhost ("TONE",  C().ice,  proc.apvts.getParameter ("cutoff"), proc.apvts.getParameter ("res"), 4);
        pad.addGhost ("MORPH", C().pink, proc.apvts.getParameter ("o1_pos"), proc.apvts.getParameter ("o1_det"), 9);
        pad.addGhost ("SPACE", C().ecto, proc.apvts.getParameter ("echo"),   proc.apvts.getParameter ("verb"), 17);
        pad.addGhost ("HEAT",  C().gold, proc.apvts.getParameter ("drive"),  proc.apvts.getParameter ("sub_lvl"), 22);
        pad.levelSource = [this] { return proc.level.load(); };
        pad.ghostPainter = proghost::padPainter(); pad.painterDrawsNames = true;   // GHOST ART (round 3): HEAT / SPACE / MORPH / TONE characters, name on the body (GhostyPro/ProGhostArt.h)
        progl::themePad (pad);      // GLASS (round 3): the pad backdrop follows the theme (BLACK = the original black pad)

        for (int o = 0; o < 2; ++o)
        {
            const juce::String pfx = o == 0 ? "o1_" : "o2_";
            auto& row = oscRow[o];
            for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "pos", "POS" }, { "lvl", "LEVEL" }, { "oct", "OCT" }, { "semi", "SEMI" }, { "fine", "FINE" },
                                                                                                 { "uni", "UNISON" }, { "det", "DETUNE" }, { "spr", "SPREAD" }, { "bln", "BLEND" } })
                row.push_back (knob (pfx + id, cap));
            onBtn[o] = toggle (pfx + "on", "ON");
            addAndMakeVisible (oscMore[o]);
            oscMore[o].setTooltip ("More unison settings: DETUNE MODE (Linear / Super / Exp / Inv / Random), STACK (octaves / fifths / centre down) and the UNISON LAW (Classic / V2: side voices lose their sub, slow drift)");
            oscMore[o].setTooltip ("More for this oscillator: UNISON (detune mode, stack, law), two WARP slots in series (mode + amount: Sync, Bend, Squeeze, Asym, Mirror, Quantize, Formant, Pulse Width, FM / PD / AM / RM from the other osc, the sub or the noise, Tube, Zero-Square), a SPECTRAL mode (reshapes the table's spectrum), the ENGINE row and the WAVE PALETTE (every table as a picture: click to play it)");
            oscMore[o].onClick = [this, pfx]
            {   // (the items + title: oscMoreItems / oscMoreTitle, ProWavePalette.inc - the same pop-down in STUDIO)
                const int oi = pfx == "o1_" ? 0 : 1;
                MoreWithPalette::show (&oscMore[oi], proc, oi, oscMoreTitle (proc, pfx), oscMoreItems (proc, pfx));
            };
            // touching an oscillator's knob shows THAT oscillator's wavetable above (OSC 2 POS moves the OSC 2 display)
            for (auto* k : row) k->slider.onDragStart = [this, o] { if (wave.osc != o) setView (o); };
            for (auto* k : row) { k->label.setInterceptsMouseClicks (false, false); k->addMouseListener (this, false); }   // the caption / empty knob area: the panel
        }
        subK = knob ("sub_lvl", "SUB"); nzK = knob ("nz_lvl", "NOISE"); nzColK = knob ("nz_col", "COLOUR"); xamtK = knob ("xamt", "AMOUNT");
        subOct = combo ("sub_oct"); subShp = combo ("sub_shp"); xmodBox = combo ("xmod");
        addAndMakeVisible (nzMore);
        nzMore.setTooltip ("More noise settings: TYPE (Classic, White, Pink, Brown, Blue, Violet, Geiger clicks, Breath that follows the note) and STEREO (mono .. fully wide). With a type other than Classic the COLOUR knob is a FILTER: left = low pass, right = high pass");
        nzMore.onClick = [this] { MorePanel::show (&nzMore, proc.apvts, "NOISE", { { "nz_type", "TYPE", MorePanel::Item::combo }, { "nz_stereo", "STEREO", MorePanel::Item::knob } }); };
        xmodBox->setTooltip ("How OSC 1 bends OSC 2: FM, hard Sync or Ring");
        fType = combo ("f_type");
        addAndMakeVisible (filtMore);
        filtMore.setTooltip ("More filter settings: DRIVE law (Classic / Inside the filter) and CLEAN MODE (-24 dB in, +24 dB out: the filter curve without the saturation)");
        filtMore.onClick = [this] { MorePanel::show (&filtMore, proc.apvts, "FILTER DRIVE", { { "f_dlaw", "DRIVE LAW", MorePanel::Item::combo }, { "f_clean", "CLEAN MODE", MorePanel::Item::toggle } }); };
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "cutoff", "CUTOFF" }, { "res", "RES" }, { "f_drive", "DRIVE" }, { "fenv", "ENV" }, { "f_key", "KEY" },
                                                                                             { "vel_f", "VEL" }, { "f_a", "F.ATK" }, { "f_d", "F.DEC" }, { "f_s", "F.SUS" }, { "f_r", "F.REL" } })
            filtRow.push_back (knob (id, cap));
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "attack", "ATTACK" }, { "decay", "DECAY" }, { "sustain", "SUSTAIN" }, { "release", "RELEASE" },
                                                                                             { "curve", "CURVE" }, { "vel_amp", "VEL" }, { "glide", "GLIDE" }, { "poly", "VOICES" }, { "bend", "BEND" } })
            ampRow.push_back (knob (id, cap));
        monoBtn = toggle ("mono", "MONO"); legatoBtn = toggle ("legato", "LEGATO");
        addAndMakeVisible (ampMore);
        ampMore.setTooltip ("QUALITY: Draft = 1x, High = 2x, Ultra = 4x engine oversampling (oscillators, filter, drive); ULTRA WHEN RENDERING lifts an offline render to Ultra; LOCK keeps the quality when a sound loads. Older sounds load Draft (exactly as saved)."
                           "\nVOICING: 1 PER NOTE = a key played again takes over its own voice instead of stacking a second one; STEAL = which voice a new note takes when all VOICES are busy "
                           "(Oldest = as before, Newest, Highest note, Lowest note, Quietest envelope; released voices always go first)");
        ampMore.onClick = [this] { MorePanel::show (&ampMore, proc.apvts, "QUALITY  /  VOICING", { { "quality", "QUALITY", MorePanel::Item::combo }, { "q_render", "ULTRA ON RENDER", MorePanel::Item::toggle }, { "q_lock", "LOCK", MorePanel::Item::toggle }, { "", "", MorePanel::Item::gap },
                                                                                               { "v_same1", "1 PER NOTE", MorePanel::Item::toggle }, { "v_steal", "STEAL", MorePanel::Item::combo } }); };
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "drive", "DRIVE" }, { "crush", "CRUSH" }, { "chorus", "CHORUS" }, { "phaser", "PHASER" },
                                                                                             { "echo", "ECHO" }, { "verb", "REVERB" }, { "haunt", "HAUNT" }, { "gain", "VOLUME" } })
            fxRow.push_back (knob (id, cap));
        addAndMakeVisible (fxMore);
        fxMore.setTooltip ("More reverb settings: TYPE (Classic = the old reverb, Ghost = the new one with pre-delay and wet filters), SIZE, DECAY, PRE-DELAY, LO CUT, HI CUT, DUCK (the sound pushes the tail down while it plays), WIDTH");
        fxMore.onClick = [this] { MorePanel::show (&fxMore, proc.apvts, "REVERB", { { "verb_type", "TYPE", MorePanel::Item::combo }, { "verb_size", "SIZE", MorePanel::Item::knob }, { "verb_decay", "DECAY", MorePanel::Item::knob }, { "verb_pre", "PRE-DELAY", MorePanel::Item::knob },
                                                                                   { "verb_lo", "LO CUT", MorePanel::Item::knob }, { "verb_hi", "HI CUT", MorePanel::Item::knob }, { "verb_duck", "DUCK", MorePanel::Item::knob }, { "verb_width", "WIDTH", MorePanel::Item::knob } }); };
        setView (0);
        startTimerHz (10);
    }

    void resized() override
    {
        cards.clear();
        const int LW = 552, RX = LW + 14, RW = getWidth() - RX;
        // wave toolbar
        view1.setBounds (0, 0, 62, 26); view2.setBounds (66, 0, 62, 26);
        tbl1->setBounds (136, 0, 150, 26); tbl2->setBounds (136, 0, 150, 26);
        importBtn.setBounds (292, 0, 100, 26);
        editBtn.setBounds (398, 0, 88, 26);                                       // (free toolbar space between IMPORT WAV and DICE)
        dice.setBounds (LW - 60, 0, 60, 26);
        wave.setBounds (0, 32, LW, 196);
        waveA->setBounds (0, 234, 128, 24); waveB->setBounds (134, 234, 128, 24);
        drawA.setBounds (272, 234, 84, 24); drawB.setBounds (360, 234, 84, 24);
        // oscillators
        int y = 266;
        for (int o = 0; o < 2; ++o)
        {
            auto r = card (0, y, LW, 98, o == 0 ? "OSC 1" : "OSC 2", o == 0 ? C().pink : C().ice);
            oscCard[o] = r;
            placeRow (oscRow[o], knobArea (r));
            onBtn[o]->setBounds (r.getRight() - 12 - 46, y + 4, 46, 18);
            oscMore[o].setBounds (onBtn[o]->getX() - 6 - 54, y + 4, 54, 18);        // (free header space left of ON)
            y += 106;
        }
        {
            auto r = card (0, y, LW, getHeight() - y, "SUB  /  NOISE  /  OSC MOD", C().gold);
            auto a = knobArea (r);
            const int kw = 66;
            subK->setBounds (a.getX(), a.getY(), kw, a.getHeight());
            subOct->setBounds (a.getX() + kw + 4, a.getY() + 8, 92, 24); subShp->setBounds (a.getX() + kw + 4, a.getY() + 38, 92, 24);
            nzK->setBounds (a.getX() + kw + 104, a.getY(), kw, a.getHeight());
            nzColK->setBounds (a.getX() + 2 * kw + 104, a.getY(), kw, a.getHeight());
            xmodBox->setBounds (a.getX() + 3 * kw + 124, a.getY() + 22, 110, 26);
            xamtK->setBounds (a.getX() + 3 * kw + 240, a.getY(), kw, a.getHeight());
            nzMore.setBounds (r.getRight() - 12 - 54, y + 4, 54, 18);                 // (free header space at the right)
        }
        // right column
        pad.setBounds (RX, 0, RW, 258);
        auto f = card (RX, 266, RW, 98, "FILTER", C().ice);
        placeRow (filtRow, knobArea (f));
        fType->setBounds (f.getRight() - 12 - 108, 266 + 3, 108, 20);
        filtMore.setBounds (fType->getX() - 6 - 54, 266 + 3, 54, 20);         // (free header space left of the type box)
        auto a = card (RX, 372, RW, 98, "AMP  /  VOICE", C().slime);
        placeRow (ampRow, knobArea (a));
        legatoBtn->setBounds (a.getRight() - 12 - 64, 372 + 4, 64, 18); monoBtn->setBounds (legatoBtn->getX() - 6 - 54, 372 + 4, 54, 18);
        ampMore.setBounds (monoBtn->getX() - 6 - 54, 372 + 4, 54, 18);          // (free header space left of MONO)
        auto x = card (RX, 478, RW, getHeight() - 478, "GHOST FX  /  OUTPUT", C().ecto);
        placeRow (fxRow, knobArea (x));
        fxMore.setBounds (x.getRight() - 12 - 54, 478 + 4, 54, 18);                 // (free header space at the right)
    }

    // clicking an A/B shape box on another table: this oscillator plays GHOST A/B with frame A = shape A, frame B = shape B
    // (the free Ghosty's morph: POS moves A -> B). The box's own menu then picks the shape as usual.
    void mouseDown (const juce::MouseEvent& e) override
    {
        // a click anywhere in the OSC 1 / OSC 2 panel (empty space, the title, a knob's caption) shows that oscillator in the display
        // above; knob drags select it too (onDragStart) and still turn the knob
        for (int o = 0; o < 2; ++o)
        {
            const bool onPanel = e.eventComponent == this && oscCard[o].contains (e.getPosition());
            bool onKnob = false; for (auto* k : oscRow[o]) onKnob |= e.eventComponent == k;
            if (onPanel || onKnob) { if (wave.osc != o) setView (o); return; }
        }
        if ((e.eventComponent != waveA && e.eventComponent != waveB) || wave.ghostMode()) return;
        proc.frameA = ghosty::makeShape ((int) paramValue (proc, "waveA")); proc.frameB = ghosty::makeShape ((int) paramValue (proc, "waveB"));
        ghosty::alignFundamental (proc.frameB, proc.frameA);       // in phase: the morph grows from A into B (no cancelling half-way)
        proc.setParam (wave.tblId(), (float) tGhost); proc.rebuildWaves(); wave.repaint();
        if (onMessage) onMessage ("OSC " + juce::String (wave.osc + 1) + " now plays GHOST A/B: " + waveA->getText() + " -> " + waveB->getText() + "  (POS morphs A -> B)");
    }
    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        g.setColour (C().dim); g.setFont (font (9.5f));
        g.drawText (wave.ghostMode() ? "POS morphs A -> B" : "A/B = GHOST A/B", juce::Rectangle<int> (446, 234, 106, 24), juce::Justification::centredRight);
        // the oscillator shown above: its panel outlined in its colour (OSC 1 pink, OSC 2 blue); the other says how to get there
        for (int o = 0; o < 2; ++o)
        {
            const auto acc = o == 0 ? C().pink : C().ice;
            if (o == wave.osc) { g.setColour (acc); g.drawRoundedRectangle (oscCard[o].toFloat().reduced (1.0f), 12.0f, 2.2f); }
            else { g.setColour (C().dim); g.setFont (font (9.0f, false)); g.drawText ("click this panel to show OSC " + juce::String (o + 1) + " above", oscCard[o].withTrimmedLeft (70).removeFromTop (22).translated (0, 2), juce::Justification::centredLeft); }
        }
    }

private:
    WaveScreen wave;
    XYGhostPad pad;
    juce::TextButton view1 { "OSC 1" }, view2 { "OSC 2" }, drawA { "DRAW A" }, drawB { "DRAW B" }, dice { "DICE" }, importBtn { "IMPORT WAV" };
    juce::TextButton editBtn { "EDITOR" };      // WAVETABLE EDITOR window (round 3 item 2b)
    struct WtWindow : public juce::DocumentWindow { using DocumentWindow::DocumentWindow; void closeButtonPressed() override { setVisible (false); } };
    std::unique_ptr<WtWindow> wtWin;
public:
    ~SynthPage() override { if (wtWin != nullptr) wtWin->setLookAndFeel (nullptr); }
    juce::DocumentWindow* editorWindow() const { return wtWin.get(); }
    void openEditor()
    {
        if (const char* shot = std::getenv ("GHOSTY_WTSHOT"))
        {   // tests (no window manager under Xvfb: a desktop window dies with an X error): snapshot the editor to a PNG instead
            WavetableEditor ed (proc, wave.osc); ed.setLookAndFeel (&getLookAndFeel()); ed.setSize (WavetableEditor::W, WavetableEditor::H);
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f (shot); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
            ed.setLookAndFeel (nullptr);
            return;
        }
        if (wtWin == nullptr)
        {
            wtWin = std::make_unique<WtWindow> ("Ghosty Pro  -  WAVETABLE EDITOR", C().bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton);
            wtWin->setLookAndFeel (&getLookAndFeel());
            wtWin->setUsingNativeTitleBar (false); wtWin->setResizable (false, false); wtWin->setTitleBarHeight (26);
            wtWin->setContentOwned (new WavetableEditor (proc, wave.osc), true);
            if (auto* top = getTopLevelComponent()) wtWin->centreAroundComponent (top, wtWin->getWidth(), wtWin->getHeight());
        }
        else if (auto* ed = dynamic_cast<WavetableEditor*> (wtWin->getContentComponent()); ed != nullptr && ed->oscIndex() != wave.osc)
            wtWin->setContentOwned (new WavetableEditor (proc, wave.osc), true);
        wtWin->setBackgroundColour (C().bg);
        wtWin->setVisible (true); wtWin->toFront (true);
    }
private:
    juce::TextButton filtMore { "MORE" };       // SOUND ROUND 2: the FILTER card's pop-down (drive law, CLEAN MODE)
    juce::TextButton oscMore[2] { juce::TextButton ("MORE"), juce::TextButton ("MORE") };   // the OSC cards' pop-downs (detune mode, stack, unison law)
    juce::TextButton nzMore { "MORE" };         // the SUB / NOISE card's pop-down (noise type, stereo)
    juce::TextButton fxMore { "MORE" };         // the GHOST FX card's pop-down (reverb type + settings)
    juce::TextButton ampMore { "MORE" };        // the AMP / VOICE card's pop-down (QUALITY, ultra on render, lock)
    juce::ComboBox* tbl1 = nullptr, *tbl2 = nullptr, *waveA = nullptr, *waveB = nullptr, *subOct = nullptr, *subShp = nullptr, *xmodBox = nullptr, *fType = nullptr;
    std::vector<GhostKnob*> oscRow[2], filtRow, ampRow, fxRow;
    GhostKnob* subK = nullptr, *nzK = nullptr, *nzColK = nullptr, *xamtK = nullptr;
    juce::TextButton* onBtn[2] {}, *monoBtn = nullptr, *legatoBtn = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> oscCard[2];
    void setView (int o)
    {
        wave.osc = o;
        view1.setToggleState (o == 0, juce::dontSendNotification); view2.setToggleState (o == 1, juce::dontSendNotification);
        tbl1->setVisible (o == 0); tbl2->setVisible (o == 1);
        repaint();
    }
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Sample -> wavetable", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        juce::Component::SafePointer<SynthPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f.existsAsFile()) safe->wave.importFile (f);
        });
    }
    void timerCallback() override
    {
        // A/B shapes and DRAW A/B always work: on another table they switch the oscillator to GHOST A/B (they used to be disabled
        // there, which read as "dead" controls)
        const bool mono = paramValue (proc, "mono") > 0.5f;
        legatoBtn->setEnabled (mono); legatoBtn->setAlpha (mono ? 1.0f : kGreyed);
        for (int o = 0; o < 2; ++o)
        {
            const bool on = paramValue (proc, o == 0 ? "o1_on" : "o2_on") > 0.5f;
            for (auto* k : oscRow[o]) k->setAlpha (on ? 1.0f : kGreyed);
            const bool uni = paramValue (proc, o == 0 ? "o1_uni" : "o2_uni") > 1.5f;
            for (int i = 6; i < 9; ++i) oscRow[o][(size_t) i]->setAlpha (on && uni ? 1.0f : kGreyed);
        }
        const bool xm = paramValue (proc, "xmod") > 0.5f;
        xamtK->setAlpha (xm ? 1.0f : kGreyed);
        repaint();
    }
};

// ================================================================== MOD page
class Scope : public juce::Component
{
public:
    std::function<float()> source; juce::Colour colour { C().slime }; bool bipolar = true; int look = 3;
    ProProcessor* proc = nullptr; int lfoIndex = -1;      // an LFO: its shape is drawn under the trace, with a live dot per voice at its phase
    // where an LFO's dot sits: x = phase across one drawn cycle (0..1), y = the LFO's value there
    static juce::Point<float> lfoDot (int shape, float phase, juce::uint32 seed) noexcept { return { phase, ProProcessor::lfoValue (shape, phase, seed) }; }
    std::array<float, 120> hist {}; int head = 0;
    void push() { hist[(size_t) head] = source ? source() : 0.0f; head = (head + 1) % (int) hist.size(); repaint(); }
    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;     // a display: dark in both themes
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 8); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 8); }    // GLASS (round 3)
        const auto ink = progl::on (C().theme) ? ghosty::themes::readableOn (pt::themed (colour), pt::glassScreenBottom(), 3.0) : colour;    // GLASS (round 3): deepened on the light screen
        g.setColour (ink.withAlpha (0.08f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4.0f), r.getX() + 4, r.getRight() - 4);
        const int N = (int) hist.size();
        auto yOf = [&] (float v) { const float t = bipolar ? (v * 0.5f + 0.5f) : v; return r.getBottom() - 6 - juce::jlimit (0.0f, 1.0f, t) * (r.getHeight() - 12); };
        juce::Path p;
        for (int i = 0; i < N; ++i)
        {
            const float v = hist[(size_t) ((head + i) % N)];
            const float x = r.getX() + 4 + (float) i / (N - 1) * (r.getWidth() - 30);
            if (i == 0) p.startNewSubPath (x, yOf (v)); else p.lineTo (x, yOf (v));
        }
        if (proc != nullptr && lfoIndex >= 0)
        {   // one cycle of the LFO's shape, faint, and a dot per sounding voice at its phase (one global dot when nothing plays)
            const int shape = (int) paramValue (*proc, "l" + juce::String (lfoIndex + 1) + "_shape");
            const juce::uint32 seed = (juce::uint32) lfoIndex * 977u;
            const float x0 = r.getX() + 4, w = r.getWidth() - 30;
            juce::Path sp;
            for (int i = 0; i <= 64; ++i) { const float ph = (float) i / 64.0f, v = ProProcessor::lfoValue (shape, ph, seed); const float x = x0 + ph * w, y = yOf (v); if (i == 0) sp.startNewSubPath (x, y); else sp.lineTo (x, y); }
            g.setColour (ink.withAlpha (0.28f)); g.strokePath (sp, juce::PathStrokeType (1.2f));
            ProProcessor::TraceSnap snap;
            if (proc->readTrace (snap))
            {
                auto dot = [&] (float ph, float alpha)
                {
                    const auto d = lfoDot (shape, ph, seed); const float x = x0 + d.x * w, y = yOf (d.y);
                    g.setColour (ink.withAlpha (0.3f * alpha)); g.fillEllipse (x - 7, y - 7, 14, 14);
                    g.setColour (C().white.withAlpha (alpha)); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
                };
                if (snap.count == 0) dot (snap.lfoGlobal[lfoIndex], 0.9f);
                for (int i = snap.count - 1; i >= 0; --i) dot (snap.v[i].lfo[lfoIndex], 1.0f - 0.11f * (float) i);
            }
        }
        g.setColour (ink.withAlpha (0.18f)); g.strokePath (p, juce::PathStrokeType (6.0f));
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.8f));
        const float last = hist[(size_t) ((head + N - 1) % N)];
        drawGhost (g, lookFor (look), r.getRight() - 16, yOf (last), 18, std::abs (last) > 0.9f ? 0.5f : 0.0f);
    }
};

class EnvView : public juce::Component
{
public:
    EnvView (ProProcessor& p, juce::String pre, juce::Colour c, int src, int envIndex) : proc (p), prefix (std::move (pre)), colour (c), source (src), env (envIndex)
    { ids = { prefix + "a", prefix + "d", prefix + "s", prefix + "r" }; }
    // the AMP envelope (ENV 1): its parameters have their own names
    EnvView (ProProcessor& p, juce::StringArray adsr, juce::Colour c, int src, int envIndex) : proc (p), colour (c), source (src), env (envIndex), ids (std::move (adsr)) {}
    // where a voice's envelope sits on the drawn curve: x = progress through attack / decay / the sustain plateau / release (the same
    // time axis as the curve), y = its actual level
    static juce::Point<float> dotTime (int stage, float lin, float out, float a, float d, float rl) noexcept
    {
        switch (stage)
        {
            case 1: return { lin * a, out };                              // attack: lin runs 0 -> 1
            case 2: return { a + (1.0f - lin) * d, out };                 // decay: lin runs 1 -> 0
            case 3: return { a + d + 0.25f, out };                        // sustain: waiting on the plateau
            case 4: return { a + d + 0.5f + (1.0f - lin) * rl, out };     // release: lin runs 1 -> 0
            default: return { -1.0f, 0.0f };
        }
    }
    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;     // a display: dark in both themes
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 8); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 8); }    // GLASS (round 3)
        const auto ink = progl::on (C().theme) ? ghosty::themes::readableOn (pt::themed (colour), pt::glassScreenBottom(), 3.0) : colour;    // GLASS (round 3): deepened on the light screen
        const float a = paramValue (proc, ids[0]), d = paramValue (proc, ids[1]), s = paramValue (proc, ids[2]), rl = paramValue (proc, ids[3]);
        const float total = a + d + 0.5f + rl, w = r.getWidth() - 12, h = r.getHeight() - 14;
        auto X = [&] (float t) { return r.getX() + 6 + t / total * w; };
        auto Y = [&] (float v) { return r.getBottom() - 7 - v * h; };
        const float c = 1.0f + paramValue (proc, "curve") * 3.0f;
        juce::Path p; p.startNewSubPath (X (0), Y (0)); p.lineTo (X (a), Y (1));
        for (int i = 1; i <= 16; ++i) { const float t = (float) i / 16; p.lineTo (X (a + d * t), Y (s + (1 - s) * std::pow (1 - t, c))); }
        p.lineTo (X (a + d + 0.5f), Y (s));
        for (int i = 1; i <= 16; ++i) { const float t = (float) i / 16; p.lineTo (X (a + d + 0.5f + rl * t), Y (s * std::pow (1 - t, c))); }
        auto fill = p; fill.lineTo (X (total), Y (0)); fill.closeSubPath();
        g.setColour (ink.withAlpha (0.12f)); g.fillPath (fill);
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.8f));
        const float live = proc.srcLive[source].load();
        g.setColour (ink.withAlpha (0.25f + live * 0.6f)); g.fillRect (r.getRight() - 8, Y (live), 4.0f, r.getBottom() - 7 - Y (live));
        // the live dots: one per sounding voice (newest brightest, older ones fade), riding the curve
        ProProcessor::TraceSnap snap;
        if (proc.readTrace (snap))
            for (int i = snap.count - 1; i >= 0; --i)
            {
                const auto& v = snap.v[i];
                const auto pt = dotTime (v.stage[env], v.lin[env], v.out[env], a, d, rl);
                if (pt.x < 0) continue;
                const float x = X (juce::jlimit (0.0f, total, pt.x)), y = Y (juce::jlimit (0.0f, 1.0f, pt.y));
                const float alpha = 1.0f - 0.11f * (float) i;
                g.setColour (ink.withAlpha (0.30f * alpha)); g.fillEllipse (x - 8, y - 8, 16, 16);
                g.setColour (C().white.withAlpha (alpha)); g.fillEllipse (x - 4, y - 4, 8, 8);
                g.setColour (ink.withAlpha (alpha)); g.drawEllipse (x - 4, y - 4, 8, 8, 1.5f);
            }
    }
private:
    ProProcessor& proc; juce::String prefix; juce::Colour colour; int source, env; juce::StringArray ids;
};

class ModPage : public Page, private juce::Timer
{
public:
    ~ModPage() override { proc.traceOn.store (false); }
    explicit ModPage (ProProcessor& p) : Page (p), env2View (p, "e2_", C().pink, srcEnv2, 2), env3View (p, "e3_", C().ice, srcEnv3, 3),
                                         ampView (p, juce::StringArray { "attack", "decay", "sustain", "release" }, C().slime, srcAmpEnv, 0)
    {
        for (int k = 0; k < 2; ++k)
        {
            const juce::String pfx = "l" + juce::String (k + 1) + "_";
            lfo[k].rate = knob (pfx + "rate", "RATE"); lfo[k].fade = knob (pfx + "fade", "FADE IN");
            lfo[k].shape = combo (pfx + "shape"); lfo[k].div = combo (pfx + "div");
            lfo[k].sync = toggle (pfx + "sync", "SYNC"); lfo[k].trig = toggle (pfx + "trig", "RETRIG");
            lfo[k].trig->setTooltip ("On: restarts with every note. Off: one free-running LFO for all voices");
            addAndMakeVisible (scope[k]);
            scope[k].colour = k == 0 ? C().slime : C().gold; scope[k].look = k == 0 ? 5 : 12;
            const int src = k == 0 ? srcLfo1 : srcLfo2;
            scope[k].source = [this, src] { return proc.srcLive[src].load(); };
            scope[k].proc = &proc; scope[k].lfoIndex = k;
        }
        addAndMakeVisible (env2View); addAndMakeVisible (env3View); addAndMakeVisible (ampView);
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "e2_a", "ATTACK" }, { "e2_d", "DECAY" }, { "e2_s", "SUSTAIN" }, { "e2_r", "RELEASE" } }) env2Row.push_back (knob (id, cap));
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "e3_a", "ATTACK" }, { "e3_d", "DECAY" }, { "e3_s", "SUSTAIN" }, { "e3_r", "RELEASE" } }) env3Row.push_back (knob (id, cap));
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const juce::String pfx = "m" + juce::String (s + 1) + "_";
            slot[s].src = combo (pfx + "src"); slot[s].dst = combo (pfx + "dst");
            slot[s].amt.setSliderStyle (juce::Slider::LinearHorizontal);
            slot[s].amt.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
            slot[s].amt.setColour (juce::Slider::textBoxTextColourId, C().white);
            addAndMakeVisible (slot[s].amt);
            slot[s].att = std::make_unique<SA> (proc.apvts, pfx + "amt", slot[s].amt);
            slot[s].amt.setDoubleClickReturnValue (true, 0.0);
        }
        for (int m = 0; m < 4; ++m) macroRow.push_back (knob ("mac" + juce::String (m + 1), "MACRO " + juce::String (m + 1)));
        startTimerHz (30);
    }

    void resized() override
    {
        cards.clear();
        const int LW = 552, RX = LW + 14, RW = getWidth() - RX;
        int y = 0;
        for (int k = 0; k < 2; ++k)
        {
            auto r = card (0, y, LW, 136, k == 0 ? "LFO 1" : "LFO 2", k == 0 ? C().slime : C().gold);
            scope[k].setBounds (r.getX() + 10, y + 26, 190, r.getHeight() - 36);
            const int cx = r.getX() + 212;
            lfo[k].shape->setBounds (cx, y + 30, 112, 26);
            lfo[k].div->setBounds (cx, y + 64, 112, 26);
            lfo[k].sync->setBounds (cx, y + 98, 54, 24); lfo[k].trig->setBounds (cx + 58, y + 98, 66, 24);
            lfo[k].rate->setBounds (cx + 126, y + 26, 100, 100);
            lfo[k].fade->setBounds (cx + 226, y + 26, 100, 100);
            y += 144;
        }
        for (int e = 0; e < 2; ++e)
        {
            auto r = card (0, y, LW, e == 0 ? 136 : getHeight() - y, e == 0 ? "ENV 2" : "ENV 3", e == 0 ? C().pink : C().ice);
            (e == 0 ? env2View : env3View).setBounds (r.getX() + 10, y + 26, 190, r.getHeight() - 36);
            placeRow (e == 0 ? env2Row : env3Row, juce::Rectangle<int> (r.getX() + 212, y + 26, LW - 222, r.getHeight() - 32));
            y += 144;
        }
        auto m = card (RX, 0, RW, 32 + NUM_SLOTS * rowH + 8, "MOD MATRIX   source  >  target  /  amount", C().ecto);
        matrixArea = m;
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const int ry = m.getY() + 32 + s * rowH - 4;
            slot[s].src->setBounds (m.getX() + 40, ry + 8, 128, 26);
            slot[s].dst->setBounds (m.getX() + 198, ry + 8, 140, 26);
            slot[s].amt.setBounds (m.getX() + 346, ry + 8, m.getWidth() - 346 - 12, 26);
        }
        // ENV 1 = the AMP envelope (its knobs are on the SYNTH page): the curve + the live dots, with its settings as text
        const int ay = m.getBottom() + 8;
        ampCard = card (RX, ay, RW, 128, "ENV 1  /  AMP  (edit it on the SYNTH page)", C().slime);
        ampView.setBounds (ampCard.getX() + 10, ay + 26, 300, ampCard.getHeight() - 36);
        auto mc = card (RX, ampCard.getBottom() + 8, RW, getHeight() - ampCard.getBottom() - 8, "MACROS  (named by the sound; also sources in the matrix)", C().pink);
        placeRow (macroRow, knobArea (mc).reduced (20, 0));
    }

    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        auto m = matrixArea;
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const int ry = m.getY() + 32 + s * rowH - 4;
            const float live = proc.modLive[s].load();
            const bool used = paramValue (proc, "m" + juce::String (s + 1) + "_src") > 0.5f && paramValue (proc, "m" + juce::String (s + 1) + "_dst") > 0.5f;
            auto row = juce::Rectangle<float> ((float) m.getX() + 8, (float) ry + 6, (float) m.getWidth() - 16, (float) rowH - 4);
            g.setColour ((used ? C().ecto : C().white).withAlpha (used ? 0.08f : 0.025f)); g.fillRoundedRectangle (row, 8);
            g.setColour (used ? C().slime : C().dim); g.setFont (font (11.0f));
            g.drawText (juce::String (s + 1), juce::Rectangle<float> (row.getX() + 4, row.getY(), 24, row.getHeight()), juce::Justification::centred);
            // the little ghost carries the signal from source to target
            const float gx = (float) m.getX() + 183 + live * 6.0f;
            drawGhost (g, lookFor (s * 5 + 2), gx, row.getCentreY(), used ? 16.0f + std::abs (live) * 6.0f : 12.0f, used ? std::abs (live) * 0.6f : 0.0f, false, used ? 1.0f : 0.25f);
            if (used)
            {
                auto bar = juce::Rectangle<float> (row.getX() + 32, row.getBottom() - 4, row.getWidth() - 40, 2);
                const float cx = bar.getCentreX(), w = juce::jlimit (-1.0f, 1.0f, live) * bar.getWidth() * 0.5f;
                g.setColour ((live >= 0 ? C().slime : C().pink).withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, cx + w), bar.getY(), juce::jmax (cx, cx + w), bar.getBottom()));
            }
        }
        {   // ENV 1 settings as text (the knobs live on the SYNTH page)
            auto t = ampCard.withTrimmedLeft (322).withTrimmedTop (30).reduced (6, 0);
            const char* names[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" }; const char* ids[] { "attack", "decay", "sustain", "release" };
            for (int i = 0; i < 4; ++i)
            {
                auto row = t.removeFromTop (22);
                g.setColour (C().dim); g.setFont (font (10.0f)); g.drawText (names[i], row.removeFromLeft (80), juce::Justification::centredLeft);
                g.setColour (C().white); g.setFont (font (12.0f));
                if (auto* prm = proc.apvts.getParameter (ids[i])) g.drawText (prm->getCurrentValueAsText(), row, juce::Justification::centredLeft);
            }
        }
    }

private:
    struct Lfo { GhostKnob* rate = nullptr; GhostKnob* fade = nullptr; juce::ComboBox* shape = nullptr; juce::ComboBox* div = nullptr; juce::TextButton* sync = nullptr; juce::TextButton* trig = nullptr; } lfo[2];
    Scope scope[2];
    EnvView env2View, env3View, ampView;
    juce::Rectangle<int> ampCard;
    static constexpr int rowH = 36;
    std::vector<GhostKnob*> env2Row, env3Row, macroRow;
    struct Slot { juce::ComboBox* src = nullptr; juce::ComboBox* dst = nullptr; juce::Slider amt; std::unique_ptr<SA> att; } slot[NUM_SLOTS];
    juce::Rectangle<int> matrixArea;

    void timerCallback() override
    {
        for (auto& s : scope) s.push();
        proc.traceOn.store (isShowing() || isVisible() || gpro::layout2::traceWanted (&proc));     // the envelope dots: published by the audio thread only while this page (or the STUDIO page) shows
        for (int m = 0; m < (int) macroRow.size(); ++m)
        {
            auto nm = proc.macros.name (m); if (nm.isEmpty()) nm = "MACRO " + juce::String (m + 1);
            if (macroRow[(size_t) m]->label.getText() != nm) macroRow[(size_t) m]->label.setText (nm, juce::dontSendNotification);
            macroRow[(size_t) m]->slider.setDoubleClickReturnValue (true, proc.macros.bipolar (m) ? 0.5 : 0.0);
        }
        env2View.repaint(); env3View.repaint(); ampView.repaint();
        for (int k = 0; k < 2; ++k)
        {
            const bool sync = paramValue (proc, "l" + juce::String (k + 1) + "_sync") > 0.5f;
            lfo[k].rate->setAlpha (sync ? kGreyed : 1.0f); lfo[k].rate->setEnabled (! sync);
            lfo[k].div->setAlpha (sync ? 1.0f : kGreyed); lfo[k].div->setEnabled (sync);
        }
        repaint (matrixArea);
    }
};


// ================================================================== BEND page (bend lane: drawn tempo-synced pitch curve)
class BendPage : public Page, private juce::Timer
{
public:
    explicit BendPage (ProProcessor& p) : Page (p), lane (p.bendShape)
    {
        addAndMakeVisible (lane);
        lane.accent = C().ice; lane.ghostLook = 9;
        lane.rangeSemis = [this] { return bendlane::rangeOf ((int) paramValue (proc, "bl_rng")); };
        lane.bars = [this] { return bendlane::barsOf ((int) paramValue (proc, "bl_len")); };
        lane.amount = [this] { return paramValue (proc, "bl_amt"); };
        lane.playPos = [this] { return proc.laneLivePos.load(); };
        onBtn = toggle ("bl_on", "ON");
        onBtn->setTooltip ("Bend lane on/off (it bends every voice's pitch)");
        amt = knob ("bl_amt", "AMOUNT");
        len = combo ("bl_len"); rng = combo ("bl_rng"); mode = combo ("bl_mode");
        len->setTooltip ("Lane length (follows the song's bars; free-runs at the song tempo when stopped)");
        rng->setTooltip ("How far the top / bottom of the lane bends");
        mode->setTooltip ("SYNC: locked to the bar.  RETRIGGER: the lane starts over on every new note");
        for (auto* c : { &snapBox, &gridBox }) { addAndMakeVisible (c); }
        snapBox.addItemList (bendlane::snapNames(), 1); gridBox.addItemList (bendlane::gridNames(), 1);
        snapBox.setTooltip ("What new / moved points snap to (hold ALT to move freely)");
        gridBox.setTooltip ("Time grid for the points");
        snapBox.onChange = [this] { if (snapBox.getSelectedItemIndex() >= 0 && snapBox.getSelectedItemIndex() != lane.snap()) lane.setSnap (snapBox.getSelectedItemIndex()); };
        gridBox.onChange = [this] { if (gridBox.getSelectedItemIndex() >= 0 && gridBox.getSelectedItemIndex() != lane.grid()) lane.setGrid (gridBox.getSelectedItemIndex()); };
        addAndMakeVisible (shapesBtn);
        shapesBtn.setTooltip ("Factory bend shapes");
        shapesBtn.onClick = [this] { showShapes(); };
        syncBoxes();
        startTimerHz (30);
    }
    void pickShape (int i)     // also used by the test harness
    {
        const auto& f = bendlane::factory();
        if (! juce::isPositiveAndBelow (i, (int) f.size())) return;
        lane.load (f[(size_t) i].shape);
        proc.setParam ("bl_len", (float) f[(size_t) i].lenIndex);
        proc.setParam ("bl_rng", (float) f[(size_t) i].rangeIndex);
        proc.setParam ("bl_on", 1.0f);
        shapeName = f[(size_t) i].name;
        syncBoxes();
    }
    void resized() override
    {
        cards.clear();
        const int W = getWidth(), H = getHeight();
        auto top = card (0, 0, W, 404, "BEND LANE   draw a line - up an octave, down an octave or anywhere between - it repeats in time with the beat", C().ice);
        lane.setBounds (top.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (10));
        auto ctlCard = card (0, 412, 770, H - 412, "LANE", C().ice);
        const int x0 = ctlCard.getX() + 14, y1 = ctlCard.getY() + 32, y2 = ctlCard.getY() + 74;
        onBtn->setBounds (x0, y1, 60, 28);
        shapesBtn.setBounds (x0 + 70, y1, 150, 28);
        len->setBounds (x0 + 230, y1, 110, 28); rng->setBounds (x0 + 350, y1, 100, 28); mode->setBounds (x0 + 460, y1, 124, 28);
        snapBox.setBounds (x0 + 70, y2, 150, 28); gridBox.setBounds (x0 + 230, y2, 110, 28);
        amt->setBounds (ctlCard.getRight() - 150, ctlCard.getY() + 22, 136, ctlCard.getHeight() - 28);
        helpArea = { x0, y2 + 38, 600, ctlCard.getBottom() - (y2 + 38) - 6 };
        ghostCard = card (778, 412, W - 778, H - 412, "PITCH NOW", C().ice);
    }
    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        g.setColour (C().dim); g.setFont (font (10.0f));
        g.drawText ("SNAP", juce::Rectangle<int> (helpArea.getX(), snapBox.getY(), 64, 28), juce::Justification::centredLeft);
        g.drawFittedText ("click = add a point   drag = move   right-click / double-click = delete\n"
                          "drag a diamond = curve the line   double-click a diamond = step (jump)   hold ALT = no snap",
                          helpArea, juce::Justification::topLeft, 3, 0.9f);
        if (shapeName.isNotEmpty()) { g.setColour (C().ice.withAlpha (0.8f)); g.drawText (shapeName, juce::Rectangle<int> (mode->getRight() + 10, mode->getY(), 140, 28), juce::Justification::centredLeft); }
        // the ghost floats up and down with the bend
        auto r = ghostCard.toFloat().reduced (12).withTrimmedTop (18);
        const bool on = paramValue (proc, "bl_on") > 0.5f;
        const float range = bendlane::rangeOf ((int) paramValue (proc, "bl_rng"));
        const float st = on ? proc.laneLiveSemis.load() : 0.0f;
        const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, st / juce::jmax (1.0f, range)) * r.getHeight() * 0.38f;
        g.setColour (C().ice.withAlpha (0.12f)); g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
        auto L = lookFor (9); L.slime = C().ice;
        drawGhost (g, L, r.getX() + r.getWidth() * 0.32f, y, 52, std::abs (st) > range * 0.9f ? 0.3f : 0.0f, false, on ? 1.0f : 0.45f);
        g.setColour (on ? C().white : C().dim); g.setFont (font (22.0f));
        g.drawText ((st > 0.05f ? "+" : "") + juce::String (st, 1) + " st", r.withTrimmedLeft (r.getWidth() * 0.55f), juce::Justification::centredLeft);
    }

private:
    bendlane::Editor lane;
    juce::TextButton* onBtn = nullptr; GhostKnob* amt = nullptr; juce::ComboBox* len = nullptr, *rng = nullptr, *mode = nullptr;
    juce::ComboBox snapBox, gridBox; juce::TextButton shapesBtn { "SHAPES..." };
    juce::Rectangle<int> helpArea, ghostCard; juce::String shapeName;

    void syncBoxes()
    {
        snapBox.setSelectedItemIndex (lane.snap(), juce::dontSendNotification);
        gridBox.setSelectedItemIndex (lane.grid(), juce::dontSendNotification);
    }
    void showShapes()
    {
        juce::PopupMenu m; m.addSectionHeader ("BEND SHAPES");
        const auto names = bendlane::factoryNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        juce::Component::SafePointer<BendPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapesBtn), [safe] (int r) { if (safe != nullptr && r > 0) safe->pickShape (r - 1); });
    }
    void timerCallback() override
    {
        syncBoxes();
        repaint (ghostCard);
    }
};

// ================================================================== BROWSE page
// PRESET BROWSER (round 3 item 6): the list shows NAME / CATEGORY / TAGS columns; type to search (name, tag, category, author);
// TAGS opens the tag chips (derived from what each sound does + the category words); SORT; < > walk inside the filter; RANDOM picks
// inside the filter. Favourites + recently used live in favourites.json / recent.json (gpro::browse, ProBrowser.h).
// With no filter set it shows and walks the same list as before.
class TagPanel : public juce::Component
{
public:
    std::function<void (const juce::String&, bool)> onToggle;
    std::function<void()> onClear, onClose;
    std::function<void (bool)> onLegacy;           // SHOW LEGACY (SOUND ENGINE V2): the hidden LEGACY factory sounds join the list
    juce::TextButton legacyBtn { "SHOW LEGACY" };
    void setTags (const std::vector<std::pair<juce::String, int>>& tc, const std::set<juce::String>& on)
    {
        chips.clear();
        for (auto& [t, n] : tc)
        {
            auto* b = chips.add (new juce::TextButton (t + "  " + juce::String (n)));
            b->setClickingTogglesState (true); b->setToggleState (on.count (t) > 0, juce::dontSendNotification);
            b->getProperties().set ("tag", t);
            b->setTooltip ("Show only sounds tagged '" + t + "' (" + juce::String (n) + "); several chips = sounds with all of them");
            const juce::String tag = t;
            b->onClick = [this, b, tag] { if (onToggle) onToggle (tag, b->getToggleState()); };
            addAndMakeVisible (b);
        }
        for (auto* b : { &clearBtn, &closeBtn, &legacyBtn }) addAndMakeVisible (b);
        legacyBtn.setVisible (gpro::factoryPresetsBuilt());    // FACTORY PRESETS OFF: no LEGACY factory sounds to show
        legacyBtn.setClickingTogglesState (true);
        legacyBtn.setTooltip ("SHOW LEGACY: also list the older factory sounds (category Legacy). They are hidden by default; older projects still load them.");
        legacyBtn.onClick = [this] { if (onLegacy) onLegacy (legacyBtn.getToggleState()); };
        clearBtn.onClick = [this] { if (onClear) onClear(); };
        closeBtn.onClick = [this] { if (onClose) onClose(); };
        resized();
    }
    void sync (const std::set<juce::String>& on) { for (auto* b : chips) b->setToggleState (on.count (b->getProperties()["tag"].toString()) > 0, juce::dontSendNotification); }
    int heightFor (int w) const { const int cols = juce::jmax (1, (w - 16) / chipW); return 30 + ((chips.size() + cols - 1) / cols) * 26 + 36; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (C().card2); g.fillRoundedRectangle (r, 10);
        g.setColour (C().ecto.withAlpha (0.45f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1.0f);
        g.setColour (C().ecto); g.setFont (font (10.5f));
        g.drawText ("TAGS  -  pick one or more", getLocalBounds().removeFromTop (24).reduced (12, 0), juce::Justification::centredLeft);
    }
    void resized() override
    {
        const int cols = juce::jmax (1, (getWidth() - 16) / chipW), cw = (getWidth() - 16) / cols;
        for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds (8 + (i % cols) * cw, 28 + (i / cols) * 26, cw - 4, 22);
        clearBtn.setBounds (getWidth() - 8 - 70 - 6 - 70, getHeight() - 30, 70, 22);
        closeBtn.setBounds (getWidth() - 8 - 70, getHeight() - 30, 70, 22);
        legacyBtn.setBounds (8, getHeight() - 30, 110, 22);
    }
private:
    static constexpr int chipW = 104;
    juce::OwnedArray<juce::TextButton> chips;
    juce::TextButton clearBtn { "CLEAR" }, closeBtn { "DONE" };
};

class BrowsePage : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    std::function<void()> onLoaded;
    std::function<void()> onSaveRequest;

    explicit BrowsePage (ProProcessor& p) : proc (p)
    {
        addAndMakeVisible (search);
        search.setTextToShowWhenEmpty (searchHint(), C().dim);
        search.setFont (font (15.0f, false));
        search.setIndents (12, 8);
        search.setJustification (juce::Justification::centredLeft);
        search.setTooltip (gpro::factoryPresetsBuilt() ? "Type to search: name, tag (unison, fm, mono, reverb...), category or author (factory / you); several words = all of them"
                                                       : "Type to search your sounds and snapshots: name or tag (unison, fm, mono, reverb...); several words = all of them");
        search.onTextChange = [this] { refilter(); };
        search.onReturnKey = [this] { if (! m.shown.empty()) loadRow (0); };
        addAndMakeVisible (list);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setWantsKeyboardFocus (true);
        for (auto* b : { &saveBtn, &deleteBtn, &folderBtn, &favBtn }) addAndMakeVisible (b);
        saveBtn.onClick = [this] { if (onSaveRequest) onSaveRequest(); };
        folderBtn.onClick = [] { ProProcessor::userPresetDir().startAsProcess(); };
        deleteBtn.onClick = [this] { deleteSelected(); };
        favBtn.onClick = [this] { if (auto* e = selectedEntry()) toggleFav (*e); };
        // the list card's header: SORT, TAGS, < > inside the filter, RANDOM inside the filter
        addAndMakeVisible (sortBox); sortBox.addItemList (gpro::browse::sortNames(), 1); sortBox.setSelectedItemIndex (0, juce::dontSendNotification);
        sortBox.setTooltip ("Sort the list: bank order (as the sounds come), name, category, or recently used first");
        sortBox.onChange = [this] { m.sort = juce::jmax (0, sortBox.getSelectedItemIndex()); refilter(); };
        for (auto* b : { &tagsBtn, &prevIn, &nextIn, &randBtn }) addAndMakeVisible (b);
        tagsBtn.setTooltip ("Tag chips: show only the sounds with these tags (unison, fm, warp, clean, arp, mono, reverb... and the category words)");
        prevIn.setTooltip ("Previous sound in this list (the filter and sort you see; also the left arrow key)");
        nextIn.setTooltip ("Next sound in this list (the filter and sort you see; also the right arrow key)");
        randBtn.setTooltip ("A random sound from this list (never the one playing)");
        tagsBtn.onClick = [this] { showTags (! tagPanel.isVisible()); };
        prevIn.onClick = [this] { stepInFilter (-1); };
        nextIn.onClick = [this] { stepInFilter (1); };
        randBtn.onClick = [this] { randomInFilter(); };
        // FACTORY PRESETS OFF: the empty list says why, with a way to start
        addChildComponent (emptyInitBtn);
        emptyInitBtn.setTooltip ("Start a new sound from INIT (the clean default patch); SAVE keeps it in your sounds");
        emptyInitBtn.onClick = [this] { proc.initPatch(); glitch = 1.0f; if (onLoaded) onLoaded(); repaint(); };
        addChildComponent (tagPanel);
        tagPanel.onToggle = [this] (const juce::String& t, bool on) { if (on) m.tags.insert (t); else m.tags.erase (t); refilter(); repaint(); };
        tagPanel.onClear = [this] { m.tags.clear(); tagPanel.sync (m.tags); refilter(); repaint(); };
        tagPanel.onClose = [this] { showTags (false); };
        tagPanel.onLegacy = [this] (bool on) { setShowLegacy (on); };
        const auto dir = ProProcessor::userPresetDir();
        m.favs.open (dir.getChildFile ("favourites.json"), dir.getChildFile ("favourites.txt"));
        m.recent.open (dir.getChildFile ("recent.json"));
        rebuild();
        startTimerHz (24);
    }
    void lookAndFeelChanged() override
    {
        search.setTextToShowWhenEmpty (searchHint(), C().dim);
        search.applyColourToAllText (search.findColour (juce::TextEditor::textColourId), true);
        repaint();
    }
    std::vector<juce::Rectangle<int>> cardRects() const { return { catArea.withTrimmedTop (42), listArea, infoArea }; }

    void rebuild()
    {
        if (! gpro::factoryPresetsBuilt()) proc.refreshUserPrograms();   // FACTORY PRESETS OFF: your sounds are the host programs
        m.entries.clear();
        gpro::browse::addFactory (m.entries);
        for (auto& f : proc.listUserPresets())
        {
            gpro::browse::Entry e; e.name = f.getFileNameWithoutExtension(); e.cat = "Your Sounds"; e.author = "You"; e.program = -1; e.file = f;
            e.tags = gpro::userPresetTags (f);
            m.entries.push_back (e);
        }
        if (proc.snapshots != nullptr)   // the SNAPSHOTS bank (its own id range: program -2, the snapshot file)
            for (auto& sn : proc.snapshots->list()) { gpro::browse::Entry e; e.name = sn.name; e.cat = "Snapshots"; e.author = "You"; e.program = -2; e.file = sn.file; m.entries.push_back (e); }
        tagPanel.setTags (m.tagCounts(), m.tags);
        if (! gpro::factoryPresetsBuilt()) tagsBtn.setVisible (! m.tagCounts().empty() || ! m.tags.empty());   // no chips to pick: no TAGS
        refilter();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        catArea = r.removeFromLeft (230);
        r.removeFromLeft (14);
        infoArea = r.removeFromRight (290);
        r.removeFromRight (14);
        listArea = r;
        search.setBounds (catArea.getX(), 0, catArea.getWidth(), 34);
        list.setBounds (listArea.reduced (8, 0).withTrimmedTop (32).withTrimmedBottom (28));
        const int bx = infoArea.getX() + 16, bw = infoArea.getWidth() - 32;
        favBtn.setBounds (bx, infoArea.getBottom() - 170, bw, 32);
        saveBtn.setBounds (bx, infoArea.getBottom() - 130, bw, 32);
        deleteBtn.setBounds (bx, infoArea.getBottom() - 90, bw, 32);
        folderBtn.setBounds (bx, infoArea.getBottom() - 50, bw, 32);
        // (the free right end of the list card's title row)
        int x = listArea.getRight() - 12;
        auto place = [&x, this] (juce::Component& c, int w) { x -= w; c.setBounds (x, listArea.getY() + 5, w, 19); x -= 4; };
        place (randBtn, 62); place (nextIn, 24); place (prevIn, 24); place (tagsBtn, 50); place (sortBox, 104);
        emptyInitBtn.setBounds (listArea.getCentreX() - 90, listArea.getCentreY() + 34, 180, 32);
        const int pw = listArea.getWidth() - 16;
        tagPanel.setBounds (listArea.getX() + 8, listArea.getY() + 28, pw, juce::jmin (listArea.getHeight() - 60, tagPanel.heightFor (pw)));
    }

    void paint (juce::Graphics& g) override
    {
        // categories
        Card cc { catArea.withTrimmedTop (42), "CATEGORIES", C().slime };
        paintCard (g, cc);
        cats = categoryList();
        for (int i = 0; i < cats.size(); ++i)
        {
            auto row = catRow (i).toFloat();
            const bool sel = cats[i] == m.category;
            if (sel) { g.setColour (C().slime.withAlpha (0.16f)); g.fillRoundedRectangle (row, 7); g.setColour (C().slime); g.fillRoundedRectangle (row.withWidth (3), 1.5f); }
            g.setColour (sel ? C().white : C().txt (0.75f)); g.setFont (font (12.5f, sel));
            g.drawText (cats[i], row.reduced (12, 0), juce::Justification::centredLeft);
            g.setColour (C().dim); g.setFont (font (10.5f, false));
            g.drawText (juce::String (countFor (cats[i])), row.reduced (10, 0), juce::Justification::centredRight);
        }
        // list
        Card lc { listArea, m.category.toUpperCase() + "   " + juce::String ((int) m.shown.size()) + " sounds", C().ecto };
        paintCard (g, lc);
        g.setColour (C().dim); g.setFont (font (9.5f, false));
        if (! showsEmptyState()) g.drawText ("click to play it - arrow keys walk the list - star to keep it", listArea.reduced (14, 0).removeFromBottom (24), juce::Justification::centredRight);
        if (showsEmptyState())
        {   // FACTORY PRESETS OFF: nothing to list yet
            const auto [ink, sub] = emptyStateColours();
            auto er = emptyStateArea();
            g.setColour (ink); g.setFont (font (17.0f));
            g.drawFittedText (emptyStateTitle(), er.removeFromTop (28), juce::Justification::centred, 1);
            g.setColour (sub); g.setFont (font (13.0f, false));
            g.drawFittedText (emptyStateText(), er.removeFromTop (44), juce::Justification::centred, 2);
        }
        // the tag chips that are on (click one to drop it)
        chipRects.clear();
        {
            int x = listArea.getX() + 14; const int y = listArea.getBottom() - 21;
            g.setFont (font (9.5f));
            for (auto& t : m.tags)
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t + "  x") + 18;
                if (x + w > listArea.getX() + 230) { g.setColour (C().dim); g.drawText ("+" + juce::String ((int) m.tags.size() - (int) chipRects.size()), x, y, 30, 18, juce::Justification::centredLeft); break; }
                juce::Rectangle<int> chip (x, y, w, 18);
                g.setColour (C().ecto.withAlpha (0.22f)); g.fillRoundedRectangle (chip.toFloat(), 9);
                g.setColour (C().white); g.drawText (t + "  x", chip.reduced (7, 0), juce::Justification::centredLeft);
                chipRects.push_back ({ chip, t });
                x += w + 4;
            }
        }
        // info
        Card ic { infoArea, "NOW LOADED", C().pink };
        paintCard (g, ic);
        auto in = infoArea.reduced (16).withTrimmedTop (22);
        const auto name = currentName();
        g.setColour (C().white); g.setFont (font (22.0f));
        g.drawFittedText (name, in.removeFromTop (56), juce::Justification::centredLeft, 2);
        g.setColour (C().slime); g.setFont (font (12.0f));
        g.drawText (currentCat().toUpperCase(), in.removeFromTop (18), juce::Justification::centredLeft);
        const auto sc = proc.currentScene();
        g.setColour (C().dim); g.setFont (font (11.0f, false));
        // scene ids are internal keys: show their plain names (the multiband squeeze scene's key is a common abbreviation, never shown)
        auto label = [] (const juce::String& id) { if (id == "ott") return juce::String ("squeeze"); if (id == "cleanlimit") return juce::String ("clean limit");
                                                   if (id == "gate16") return juce::String ("gate 1/16"); if (id == "gate8") return juce::String ("gate 1/8"); return id; };
        g.drawText (sc.isNotEmpty() ? "Ghost FX scene: " + label (sc) : "Ghost FX: dry", in.removeFromTop (18), juce::Justification::centredLeft);
        // a ghost that reacts to the sound
        const float lvl = proc.level.load();
        const float gy = (float) in.getY() + 90 + std::sin (phase * 2.0f) * 8.0f;
        juce::ColourGradient glow (C().ecto.withAlpha (0.35f + lvl * 0.4f), (float) in.getCentreX(), gy, C().ecto.withAlpha (0.0f), (float) in.getCentreX() + 90, gy, true);
        g.setGradientFill (glow); g.fillEllipse ((float) in.getCentreX() - 90, gy - 90, 180, 180);
        drawGhost (g, lookFor ((int) (std::abs (name.hashCode()) % 35)), (float) in.getCentreX(), gy, 70 + lvl * 20, glitch, false);
        // its tags (free space above STAR / UNSTAR)
        const auto tg = currentTagsText();
        if (tg.isNotEmpty())
        {
            g.setColour (C().dim); g.setFont (font (10.0f, false));
            g.drawFittedText ("tags: " + tg, juce::Rectangle<int> (infoArea.getX() + 16, infoArea.getBottom() - 170 - 34, infoArea.getWidth() - 32, 30), juce::Justification::bottomLeft, 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& [r, t] : chipRects)
            if (r.contains (e.getPosition())) { m.tags.erase (t); tagPanel.sync (m.tags); refilter(); repaint(); return; }
        for (int i = 0; i < cats.size(); ++i)
            if (catRow (i).contains (e.getPosition())) { m.category = cats[i]; refilter(); repaint(); return; }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {   // (the list keeps up / down; left / right walk the filtered list from anywhere on the page but the search box)
        if (k.getKeyCode() == juce::KeyPress::leftKey) { stepInFilter (-1); return true; }
        if (k.getKeyCode() == juce::KeyPress::rightKey) { stepInFilter (1); return true; }
        return false;
    }

    void selectCurrent()
    {
        for (int i = 0; i < (int) m.shown.size(); ++i)
            if (isCurrent (m.entries[(size_t) m.shown[(size_t) i]])) { list.selectRow (i, false, true); list.scrollToEnsureRowIsOnscreen (i); return; }
    }
    void focusList() { list.grabKeyboardFocus(); }
    bool isFav (const juce::String& key) const { return m.favs.is (key); }
    void toggleFavCurrent()
    {
        for (auto& e : m.entries) if (isCurrent (e)) { toggleFav (e); return; }
    }
    bool currentIsFav() const { for (auto& e : m.entries) if (isCurrent (e)) return isFav (e.key()); return false; }
    bool filterActive() const { return m.filterActive(); }
    int currentEntry() const { for (int i = 0; i < (int) m.entries.size(); ++i) if (isCurrent (m.entries[(size_t) i])) return i; return -1; }
    // < > inside the filter (also the header's < > while a filter is set)
    void stepInFilter (int d) { loadEntry (m.step (currentEntry(), d)); }
    void randomInFilter() { loadEntry (m.random (currentEntry(), rng)); }
    juce::String currentTagsText() const
    {
        const int i = currentEntry();
        if (i < 0) return {};
        juce::StringArray t; for (auto& s : m.entries[(size_t) i].tags) if (gpro::derivedTagNames().contains (s)) t.add (s);
        return t.joinIntoString (", ");
    }
    // FACTORY PRESETS OFF: the empty state (shown when the list has no row)
    bool showsEmptyState() const { return ! gpro::factoryPresetsBuilt() && m.shown.empty(); }
    bool emptyIsFiltered() const { return m.query.trim().isNotEmpty() || ! m.tags.empty(); }
    juce::String emptyStateTitle() const { return emptyIsFiltered() ? juce::String ("Nothing matches") : juce::String ("No factory sounds yet"); }
    juce::String emptyStateText() const
    {
        if (emptyIsFiltered()) return "No sound matches this search. Clear the search or the tags.";
        if (m.category == "Snapshots") return "No snapshots yet. Take one with the SNAPSHOT bar.";
        if (m.category == "Favourites") return "No favourites yet. Star a sound to keep it here.";
        return "A new hand-made sound set is coming.\nStart from INIT, or load your own presets.";
    }
    std::pair<juce::Colour, juce::Colour> emptyStateColours() const { return { C().white, C().txt (0.85f) }; }
    juce::Rectangle<int> emptyStateArea() const { return listArea.withSizeKeepingCentre (juce::jmin (460, listArea.getWidth() - 40), 72).translated (0, -24); }
    // test hooks (ProTests browse / screenshots)
    gpro::browse::Model& model() { return m; }
    void testSetFilter (const juce::String& text, const std::set<juce::String>& tags, bool showPanel)
    {
        m.tags = tags; tagPanel.sync (m.tags);
        search.setText (text, juce::dontSendNotification);
        refilter(); showTags (showPanel); repaint();
    }

private:
    using Entry = gpro::browse::Entry;
    ProProcessor& proc;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton saveBtn { "SAVE AS..." }, deleteBtn { "DELETE (your sound)" }, folderBtn { "OPEN SOUNDS FOLDER" }, favBtn { "STAR / UNSTAR" };
    juce::ComboBox sortBox;
    juce::TextButton tagsBtn { "TAGS" }, prevIn { "<" }, nextIn { ">" }, randBtn { "RANDOM" };
    juce::TextButton emptyInitBtn { "START FROM INIT" };
    TagPanel tagPanel;
    gpro::browse::Model m;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> chipRects;
    juce::StringArray cats;
    juce::Rectangle<int> catArea, listArea, infoArea;
    float phase = 0, glitch = 0;
    juce::Random rng;
    juce::String lastCurrentKey; int tick = 0;

    void setShowLegacy (bool on)
    {
        m.showLegacy = on; tagPanel.legacyBtn.setToggleState (on, juce::dontSendNotification);
        if (! on && m.category == gpro::legacyCategory()) m.category = "All";
        m.tags.erase ("legacy");
        tagPanel.setTags (m.tagCounts(), m.tags);
        refilter(); repaint();
    }
    void showTags (bool show)
    {
        tagPanel.setVisible (show); tagsBtn.setToggleState (show, juce::dontSendNotification);
        if (show) { tagPanel.sync (m.tags); tagPanel.toFront (false); }
    }
    void toggleFav (const Entry& e)
    {
        m.favs.toggle (e.key());
        if (m.category == "Favourites") refilter();
        list.repaint(); repaint();
    }

    juce::StringArray categoryList() const
    {
        juce::StringArray c { "All", "Snapshots", "Favourites", "Your Sounds" };
        juce::StringArray classics, trap;
        for (auto& b : bankCategories())
        {
            bool any = false; for (auto& e : m.entries) any |= e.program >= 0 && e.cat == b;   // (a category whose sounds are all LEGACY is not listed)
            if (! any) continue;
            if (b.startsWith ("Trap ")) trap.add (b); else if (b == "Ghosty Classics") classics.add (b); else c.add (b);
        }
        c.addArray (trap); c.addArray (classics);     // the TRAP bank sits before the classics (its program numbers come last so old sounds keep theirs)
        if (m.showLegacy) c.add (gpro::legacyCategory());
        return c;
    }
    int catStep() const { return juce::jlimit (18, 31, (catArea.getHeight() - 42 - 28 - 6) / juce::jmax (1, categoryList().size())); }   // 24 categories (with the LEADS folders) still fit
    juce::Rectangle<int> catRow (int i) const { const int st = catStep(); return { catArea.getX() + 8, catArea.getY() + 42 + 28 + i * st, catArea.getWidth() - 16, st - 3 }; }
    int countFor (const juce::String& c) const
    {
        int n = 0;
        for (auto& e : m.entries) if (m.visible (e) && (c == "All" || e.cat == c || (c == "Favourites" && m.isFav (e)))) ++n;
        return n;
    }
    void refilter()
    {
        m.query = search.getText();
        m.refilter();
        list.updateContent(); list.repaint();
        emptyInitBtn.setVisible (showsEmptyState() && ! emptyIsFiltered() && m.category != "Snapshots" && m.category != "Favourites");
        if (! gpro::factoryPresetsBuilt())    // FACTORY PRESETS OFF: nothing to sort / walk / pick from while there is no sound at all
            for (juce::Component* c : { (juce::Component*) &sortBox, (juce::Component*) &prevIn, (juce::Component*) &nextIn, (juce::Component*) &randBtn }) c->setVisible (! m.entries.empty());
        list.setVisible (! showsEmptyState());
        selectCurrent();
        repaint (listArea);
    }
    static juce::String searchHint() { return gpro::factoryPresetsBuilt() ? "search sounds..." : "search your sounds..."; }
    bool isCurrent (const Entry& e) const
    {
        if (e.program >= 0) return ! proc.isUserPatch() && proc.patchName().isEmpty() && proc.getCurrentProgram() == e.program;
        if (e.program == -2) return proc.snapshots != nullptr && juce::isPositiveAndBelow (proc.snapshots->current(), proc.snapshots->size()) && proc.snapshots->info (proc.snapshots->current()).file == e.file;
        return proc.isUserPatch() && proc.patchName() == e.name;
    }
    juce::String currentName() const { const auto n = proc.patchName(); return n.isNotEmpty() ? n : const_cast<ProProcessor&> (proc).getProgramName (proc.getCurrentProgram()); }
    juce::String currentCat() const
    {
        if (proc.isUserPatch()) return "Your Sounds";
        if (proc.patchName().isNotEmpty()) return "Init";
        const auto& b = factoryBank(); const int i = proc.getCurrentProgram();
        return juce::isPositiveAndBelow (i, (int) b.size()) ? b[(size_t) i].cat : juce::String();
    }
    Entry* selectedEntry() { const int r = list.getSelectedRow(); return juce::isPositiveAndBelow (r, (int) m.shown.size()) ? &m.entries[(size_t) m.shown[(size_t) r]] : nullptr; }
    void loadEntry (int idx)
    {   // the one sound-load path of the page (the processor's own loaders: same as a click always did)
        if (! juce::isPositiveAndBelow (idx, (int) m.entries.size())) return;
        const auto& e = m.entries[(size_t) idx];
        if (e.program >= 0) proc.setCurrentProgram (e.program);
        else if (e.program == -2) { if (proc.snapshots != nullptr) proc.snapshots->load (proc.snapshots->indexOfFile (e.file)); }
        else proc.loadUserPreset (e.file);
        noteRecent();
        glitch = 1.0f;
        if (onLoaded) onLoaded();
        selectCurrent();
        list.repaint(); repaint();
    }
    void loadRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) m.shown.size())) return;
        loadEntry (m.shown[(size_t) row]);
    }
    void noteRecent()
    {   // RECENTLY USED: whatever loaded it (this page, the header, LOAD, the host)
        const int i = currentEntry();
        const auto k = i >= 0 ? m.entries[(size_t) i].key() : juce::String();
        if (k.isEmpty() || k == lastCurrentKey) return;
        lastCurrentKey = k; m.recent.touch (k);
        if (m.sort == gpro::browse::sortRecent && isVisible()) { m.refilter(); list.updateContent(); list.repaint(); selectCurrent(); }
    }
    void deleteSelected()
    {
        auto* e = selectedEntry();
        if (e == nullptr || e->program >= 0) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Ghosty Pro", "Pick one of your own sounds to delete (factory sounds stay)."); return; }
        const auto file = e->file;
        juce::Component::SafePointer<BrowsePage> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete?", "Delete \"" + file.getFileNameWithoutExtension() + "\"?", "Delete", "Cancel", nullptr,
                                            juce::ModalCallbackFunction::create ([safe, file] (int r) { if (r == 1 && safe != nullptr) { file.deleteFile(); safe->rebuild(); } }));
    }

    // ListBoxModel
    int getNumRows() override { return (int) m.shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) m.shown.size())) return;
        const auto& e = m.entries[(size_t) m.shown[(size_t) row]];
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2, 1);
        const bool cur = isCurrent (e);
        if (cur || selected) { g.setColour ((cur ? C().slime : C().ecto).withAlpha (cur ? 0.16f : 0.12f)); g.fillRoundedRectangle (r, 7); }
        if (cur) { g.setColour (C().slime); g.fillRoundedRectangle (r.withWidth (3), 1.5f); }
        // star
        const bool fav = m.isFav (e);
        juce::Path star; star.addStar ({ r.getX() + 18, r.getCentreY() }, 5, 3.5f, 8.0f, 0.0f);
        g.setColour (fav ? C().gold : C().white.withAlpha (0.18f));
        if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.0f));
        // columns: NAME | CATEGORY | TAGS
        auto cols = r.withTrimmedLeft (36).withTrimmedRight (10);
        const float cw = cols.getWidth();
        auto nameCol = cols.removeFromLeft (cw * 0.46f), catCol = cols.removeFromLeft (cw * 0.25f);
        g.setColour (cur ? C().white : C().txt (0.85f)); g.setFont (font (13.5f, cur));
        g.drawText (e.name, nameCol.withTrimmedRight (6), juce::Justification::centredLeft);
        g.setColour (C().slime.withAlpha (0.8f)); g.setFont (font (10.0f, false));
        g.drawText (e.cat, catCol.withTrimmedRight (6), juce::Justification::centredLeft);
        juce::StringArray tg; for (auto& t : e.tags) if (gpro::derivedTagNames().contains (t)) tg.add (t);
        g.setColour (C().dim);
        g.drawText (tg.joinIntoString (" . "), cols, juce::Justification::centredRight);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.x < 34 && juce::isPositiveAndBelow (row, (int) m.shown.size())) { toggleFav (m.entries[(size_t) m.shown[(size_t) row]]); return; }
        loadRow (row);
    }
    void selectedRowsChanged (int row) override
    {
        // arrow keys: load as you walk the list
        if (list.hasKeyboardFocus (true) && juce::isPositiveAndBelow (row, (int) m.shown.size()) && ! isCurrent (m.entries[(size_t) m.shown[(size_t) row]])) loadRow (row);
    }
    void timerCallback() override
    {
        phase += 1.0f / 24.0f; glitch *= 0.85f;
        if (++tick % 6 == 0) noteRecent();
        repaint (infoArea);
    }
};

// ================================================================== TRAP page (synth brass engine, CHARACTER, TAPE / SLOWED, STACK, SAMPLE layer)
class SampleView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit SampleView (ProProcessor& p) : proc (p) { setTooltip ("Drag the two markers to set START / END; click the waveform to hear the sample at its root"); }
    std::function<void (const juce::File&)> onFile;
    std::shared_ptr<const SampleData> data; float start = 0, end = 1; bool hover = false, reverse = false;
    static bool isAudio (const juce::String& f) { return juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"); }
    float fracAt (int x) const { return juce::jlimit (0.0f, 1.0f, (float) (x - 4) / (float) juce::jmax (1, getWidth() - 8)); }
    int xOf (float f) const { return 4 + juce::roundToInt (f * (float) juce::jmax (1, getWidth() - 8)); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (data == nullptr) return;
        drag = 0;
        if (std::abs (e.x - xOf (start)) < 9) drag = 1; else if (std::abs (e.x - xOf (end)) < 9) drag = 2;
        if (drag == 0) { audition = data->root; proc.keyboardState.noteOn (1, audition, 0.8f); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == 1) proc.setParam ("smp_start", juce::jmin (fracAt (e.x), end - 0.002f));
        else if (drag == 2) proc.setParam ("smp_end", juce::jmax (fracAt (e.x), start + 0.002f));
    }
    void mouseUp (const juce::MouseEvent&) override { if (audition >= 0) { proc.keyboardState.noteOff (1, audition, 0.0f); audition = -1; } drag = 0; }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (data != nullptr && (std::abs (e.x - xOf (start)) < 9 || std::abs (e.x - xOf (end)) < 9) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (C().deep); g.fillRoundedRectangle (r, 8);
        g.setColour (hover ? C().gold : C().gold.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 8, 1.0f);
        if (data == nullptr || data->length() == 0)
        {
            g.setColour (C().dim); g.setFont (font (11.0f));
            g.drawFittedText ("drop a sample here  (WAV / AIFF / FLAC / MP3)\none shot, pitched across the keys, formants kept", getLocalBounds().reduced (10), juce::Justification::centred, 3);
            return;
        }
        const int w = juce::jmax (1, getWidth() - 8), n = data->length();
        const float midY = r.getCentreY() + 6, hh = r.getHeight() * 0.36f;
        g.setColour (C().gold.withAlpha (0.10f)); g.fillRect (r.getX() + 4 + start * (float) w, r.getY() + 20, (end - start) * (float) w, r.getHeight() - 24);
        juce::Path p;
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
            float mx = 0; for (int i = a; i < b && i < n; ++i) for (int c = 0; c < data->chans; ++c) mx = std::max (mx, std::abs (data->fwd[c][(size_t) i]));
            p.addRectangle ((float) x + 4, midY - mx * hh, 1.0f, juce::jmax (1.0f, 2.0f * mx * hh));
        }
        g.setColour (C().gold.withAlpha (0.85f)); g.fillPath (p);
        for (int h = 0; h < 2; ++h)      // START / END markers
        {
            const float x = (float) xOf (h == 0 ? start : end);
            g.setColour (h == 0 ? C().slime : C().pink); g.fillRect (x - 1, r.getY() + 20, 2.0f, r.getHeight() - 24);
            juce::Path tri; tri.addTriangle (x - 6, r.getBottom() - 2, x + 6, r.getBottom() - 2, x, r.getBottom() - 10); g.fillPath (tri);
        }
        g.setColour (C().white); g.setFont (font (10.5f));
        g.drawText (data->name + (data->chans > 1 ? "  stereo" : "") + (reverse ? "   (reversed)" : ""), r.reduced (8, 2).removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (C().dim);
        g.drawText (juce::String (data->length() / data->rate, 2) + " s   root " + juce::MidiMessage::getMidiNoteName (data->root, true, true, 4), r.reduced (8, 2).removeFromTop (18), juce::Justification::centredRight);
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { for (auto& s : f) if (isAudio (s)) return true; return false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { hover = false; repaint(); for (auto& s : f) if (isAudio (s)) { if (onFile) onFile (juce::File (s)); break; } }
private:
    ProProcessor& proc; int drag = 0, audition = -1;
};

class TrapPage : public Page, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    std::function<void (const juce::String&)> onMessage;
    // a sample dropped anywhere on the page lands in the SAMPLE layer
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return sampleView.isInterestedInFileDrag (f); }
    void fileDragEnter (const juce::StringArray&, int, int) override { sampleView.hover = true; sampleView.repaint(); }
    void fileDragExit (const juce::StringArray&) override { sampleView.hover = false; sampleView.repaint(); }
    void filesDropped (const juce::StringArray& f, int x, int y) override { sampleView.filesDropped (f, x, y); }

    explicit TrapPage (ProProcessor& p) : Page (p)
    {
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "br_scoop", "SCOOP" }, { "br_scoopt", "SCOOP TIME" }, { "br_growl", "GROWL" },
                                                                                             { "br_form", "FORMANT" }, { "br_formf", "FORM FREQ" }, { "br_fall", "FALL" } })
            brassRow.push_back (knob (id, cap));
        brassRow[0]->slider.setTooltip ("Pitch scoops up into every note (the brass attack)");
        brassRow[1]->slider.setTooltip ("How long the scoop takes");
        brassRow[2]->slider.setTooltip ("Fast pitch + filter growl (velocity makes it angrier; also a mod target: Growl)");
        brassRow[3]->slider.setTooltip ("Resonant body peak (the horn bell)");
        brassRow[4]->slider.setTooltip ("Where the body peak sits: 400 Hz .. 4 kHz");
        brassRow[5]->slider.setTooltip ("Pitch falls away after the key is let go (the \"doit\"), over the release");
        addAndMakeVisible (brassBtn);
        brassBtn.setTooltip ("BRASS ON / OFF. Lit = the trap brass is on (scoop, growl, horn body, fall). Click to turn it on (your oscillators stay); click again to turn it off (the brass knobs go back to zero)");
        brassBtn.setClickingTogglesState (false);
        brassBtn.onClick = [this] { if (brassActive()) brassOff(); else brassIt(); brassBtn.setToggleState (brassActive(), juce::dontSendNotification); };
        charK = knob ("ch_amt", "CHARACTER");
        charK->slider.setTooltip ("Saturation that turns into clipping, with the low mids cleaned up and the level held steady");
        tapeBtn = toggle ("tp_on", "TAPE");
        tapeBtn->setTooltip ("TAPE / SLOWED: wow + flutter, tape saturation, high-end roll-off and a pitch-down on everything");
        tapeK = knob ("tp_amt", "AMOUNT");
        tapeK->slider.setTooltip ("0 = a warm tape, 1 = a worn one: wow, flutter, saturation, 3 kHz roll-off");
        slowK = knob ("tp_slow", "SLOW");
        slowK->slider.setTooltip ("The slowed edit's pitch-down, in semitones (0 .. -12), on every layer");

        for (int k = 0; k < NUM_LAYERS; ++k)
        {
            const juce::String pfx = "ly" + juce::String (k + 1) + "_";
            auto& L = layer[k];
            if (k > 0) { L.src = combo (pfx + "src"); L.src->setTooltip ("What this layer plays: another factory sound or the SAMPLE"); }
            L.sound = std::make_unique<juce::TextButton> ("pick a sound...");
            addAndMakeVisible (*L.sound);
            L.sound->setTooltip (k == 0 ? "Layer 1 is the sound on the SYNTH / MOD pages" : "Choose the factory sound this layer plays");
            L.sound->onClick = [this, k] { pickSound (k); };
            for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "oct", "OCT" }, { "det", "DETUNE" }, { "lvl", "LEVEL" }, { "pan", "PAN" }, { "dly", "DELAY" } })
                L.knobs.push_back (knob (pfx + id, cap));
            L.knobs[4]->slider.setTooltip ("Start delay: this layer comes in late (0 .. 1 s)");
            L.rev = toggle (pfx + "rev", "REV");
            L.rev->setTooltip ("REVERSE: a sample plays backwards; a sound swells in over its decay and is cut off");
            L.play = combo (pfx + "play");
            L.play->setTooltip ("Which notes this layer plays: everything, or one side of the velocity / key split");
            if (k > 0) { L.fx = toggle (pfx + "fx", "FX"); L.fx->setTooltip ("LAYER FX: this layer keeps its own Ghost FX and CHARACTER. Off = the layer is dry and the FX page shapes the sum"); }
            L.solo = std::make_unique<juce::TextButton> ("S"); L.mute = std::make_unique<juce::TextButton> ("M");
            for (auto* b : { L.solo.get(), L.mute.get() }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); }
            L.solo->setColour (juce::TextButton::buttonOnColourId, C().gold); L.mute->setColour (juce::TextButton::buttonOnColourId, C().pink);
            L.solo->setTooltip ("Solo this layer (not saved with the sound)"); L.mute->setTooltip ("Mute this layer (not saved with the sound)");
            L.solo->onClick = [this, k] { proc.layerSolo[k].store (layer[k].solo->getToggleState()); };
            L.mute->onClick = [this, k] { proc.layerMute[k].store (layer[k].mute->getToggleState()); };
        }
        vsplitK = knob ("stk_vsplit", "VEL SPLIT"); ksplitK = knob ("stk_ksplit", "KEY SPLIT");
        vsplitK->slider.setTooltip ("Velocity below this is VEL LOW, from here up VEL HIGH"); ksplitK->slider.setTooltip ("Keys below this are KEY LOW, from here up KEY HIGH");

        addAndMakeVisible (sampleView);
        sampleView.onFile = [this] (const juce::File& f) { importFile (f); };
        for (auto* b : { &importBtn, &clearBtn }) addAndMakeVisible (b);
        importBtn.onClick = [this] { chooseFile(); };
        clearBtn.onClick = [this] { proc.clearTrapSample(); if (onMessage) onMessage ("Sample cleared"); };
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "smp_root", "ROOT" }, { "smp_fine", "FINE" }, { "smp_start", "START" }, { "smp_end", "END" },
                                                                                             { "smp_a", "ATTACK" }, { "smp_d", "DECAY" }, { "smp_s", "SUSTAIN" }, { "smp_r", "RELEASE" }, { "smp_revvel", "REV VEL" } })
            smpRow.push_back (knob (id, cap));
        smpRow[0]->slider.setTooltip ("The note the sample is at (found automatically on import; set it by ear if it had no clear pitch)");
        smpRow[8]->slider.setTooltip ("Notes played at or above this velocity play the sample backwards (0 = off); a Random -> Sample Start route in the matrix chops per note");
        formantBtn = toggle ("smp_formant", "KEEP FORMANTS"); loopBtn = toggle ("smp_loop", "LOOP");
        formantBtn->setTooltip ("Pitch the sample without the chipmunk / giant effect: its character stays where it is");
        loopBtn->setTooltip ("Loop the START .. END region while the key is held");
        startTimerHz (12);
    }

    void resized() override
    {
        cards.clear();
        const int W = getWidth(), H = getHeight();
        auto b = card (0, 0, 560, 120, "SYNTH BRASS   scoop into the note, growl, a horn body and the fall", C().gold);
        placeRow (brassRow, knobArea (b));
        brassBtn.setBounds (b.getRight() - 12 - 86, b.getY() + 4, 86, 18);
        auto c = card (570, 0, 170, 120, "CHARACTER", C().pink);
        charK->setBounds (knobArea (c).reduced (20, 0));
        auto t = card (750, 0, W - 750, 120, "TAPE  /  SLOWED", C().ice);
        tapeBtn->setBounds (t.getRight() - 12 - 56, t.getY() + 4, 56, 18);
        tapeK->setBounds (t.getX() + 6, t.getY() + 24, 78, t.getHeight() - 28);
        slowK->setBounds (t.getX() + 86, t.getY() + 24, 78, t.getHeight() - 28);
        tapeArea = { t.getX() + 172, t.getY() + 26, t.getWidth() - 184, t.getHeight() - 32 };

        auto s = card (0, 128, W, 312, "STACK   three layers: this sound, another factory sound or the sample - each with its own octave, level, pan, delay, reverse and split", C().ecto);
        const int rowH = 68, x0 = s.getX() + 12;
        for (int k = 0; k < NUM_LAYERS; ++k)
        {
            auto& L = layer[k];
            const int y = s.getY() + 26 + k * rowH;
            L.row = { x0, y, s.getWidth() - 24, rowH };
            const int cy = y + rowH / 2;
            if (L.src) L.src->setBounds (x0 + 36, cy - 13, 92, 26);
            L.sound->setBounds (x0 + 136, cy - 13, 200, 26);
            placeRow (L.knobs, { x0 + 344, y + 2, 5 * 72, rowH - 4 });
            L.rev->setBounds (x0 + 712, cy - 11, 48, 22);
            L.play->setBounds (x0 + 766, cy - 13, 100, 26);
            L.solo->setBounds (x0 + 874, cy - 11, 24, 22); L.mute->setBounds (x0 + 900, cy - 11, 24, 22);
            if (L.fx) L.fx->setBounds (x0 + 932, cy - 11, 40, 22);
            L.readout = { x0 + 982, y, L.row.getRight() - (x0 + 982), rowH };
        }
        const int sy = s.getY() + 26 + 3 * rowH;
        vsplitK->setBounds (x0 + 344, sy, 72, s.getBottom() - sy - 4); ksplitK->setBounds (x0 + 416, sy, 72, s.getBottom() - sy - 4);
        splitHelp = { x0 + 500, sy, s.getRight() - 12 - (x0 + 500), s.getBottom() - sy - 4 };

        auto m = card (0, 448, W, H - 448, "SAMPLE   one shot across the keys, pitched with its formants kept", C().gold);
        const int my = m.getY() + 26, mh = m.getHeight() - 32;
        sampleView.setBounds (m.getX() + 12, my, 262, mh);
        importBtn.setBounds (m.getX() + 282, my + 2, 76, 26); clearBtn.setBounds (m.getX() + 282, my + mh - 28, 76, 26);
        placeRow (smpRow, { m.getX() + 366, my, 9 * 66, mh });
        formantBtn->setBounds (m.getRight() - 12 - 116, my + 8, 116, 26); loopBtn->setBounds (m.getRight() - 12 - 116, my + mh - 34, 116, 26);
    }

    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        g.setFont (font (10.0f));
        // tape readout
        {
            const bool on = paramValue (proc, "tp_on") > 0.5f; const float amt = paramValue (proc, "tp_amt");
            const float semis = Tape::semisFor (on, paramValue (proc, "tp_slow"));
            g.setColour (on ? C().ice : C().dim); g.setFont (font (22.0f));
            g.drawText (on ? juce::String (semis, 1) + " st" : "off", tapeArea.removeFromTop (34), juce::Justification::centredLeft);
            g.setColour (C().dim); g.setFont (font (10.0f));
            g.drawFittedText (on ? "wow + flutter, tape saturation, roll-off " + juce::String ((int) (18000.0f * std::pow (3200.0f / 18000.0f, amt)) / 100 * 100 / 1000.0f, 1) + " kHz\nthe whole sound, every layer, about " + juce::String ((int) std::round ((1.0f - std::exp2 (semis / 12.0f)) * 100.0f)) + "% slower"
                                 : "TAPE on = tape wear by AMOUNT,\nSLOW = the slowed edit's pitch-down", tapeArea, juce::Justification::topLeft, 3);
        }
        // stack rows
        for (int k = 0; k < NUM_LAYERS; ++k)
        {
            auto& L = layer[k];
            const bool on = k == 0 || (int) paramValue (proc, "ly" + juce::String (k + 1) + "_src") != lsOff;
            g.setColour (C().ecto.withAlpha (0.08f)); if (k % 2 == 1) g.fillRoundedRectangle (L.row.toFloat(), 8);
            g.setColour (on ? C().ecto : C().dim); g.setFont (font (16.0f));
            g.drawText (juce::String (k + 1), L.row.withWidth (30), juce::Justification::centred);
            g.setFont (font (10.0f)); g.setColour (C().dim);
            if (k == 0) g.drawText ("MAIN", juce::Rectangle<int> (L.row.getX() + 36, L.row.getCentreY() - 13, 92, 26), juce::Justification::centredLeft);
            const int v = proc.layerVoices[k].load(); const float cpu = proc.cpuLive[k].load();
            g.setColour (on ? C().txt (0.8f) : C().dim);
            g.drawFittedText (on ? "voices " + juce::String (v) + "\ncpu " + juce::String (cpu, 1) + "%" : "off", L.readout.reduced (4, 10), juce::Justification::centredLeft, 2);
            // little activity bar
            auto bar = juce::Rectangle<float> ((float) L.readout.getX() + 4, (float) L.readout.getBottom() - 12, (float) L.readout.getWidth() - 12, 4);
            g.setColour (C().white.withAlpha (0.08f)); g.fillRoundedRectangle (bar, 2);
            g.setColour (C().ecto); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (float) v / 8.0f)), 2);
        }
        g.setColour (C().dim); g.setFont (font (10.0f));
        g.drawFittedText ("PLAY MODE splits the keyboard: VEL LOW / VEL HIGH around VEL SPLIT, KEY LOW / KEY HIGH around KEY SPLIT.\n"
                          "DELAY staggers a layer; REV plays a sample backwards or swells a sound in. The FX page and TAPE run on the sum of all layers.",
                          splitHelp, juce::Justification::centredLeft, 3);
    }

    void importFile (const juce::File& f)
    {
        juce::String msg;
        proc.importTrapSample (f, msg);
        if (onMessage) onMessage (msg);
    }
    void pickSound (int k)
    {
        if (k == 0) return;
        juce::PopupMenu m; m.addSectionHeader ("LAYER " + juce::String (k + 1) + " SOUND");
        m.addItem (1, "(none)");
        const auto& bank = factoryBank();
        for (auto& cat : bankCategories())
        {
            juce::PopupMenu sub;
            for (int i = 0; i < (int) bank.size(); ++i) if (bank[(size_t) i].cat == cat) sub.addItem (i + 2, bank[(size_t) i].name, true, bank[(size_t) i].name == proc.layerSound (k));
            if (sub.getNumItems() > 0) m.addSubMenu (cat, sub);
        }
        if (bank.empty())
        {   // FACTORY PRESETS OFF: layer sounds come with the new sound set (an older project's layer still plays, shown ticked)
            if (proc.layerSound (k).isNotEmpty()) m.addItem (-1, proc.layerSound (k), false, true);
            m.addItem (-2, "No factory sounds yet: layer sounds come with the new set", false);
        }
        juce::Component::SafePointer<TrapPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (layer[k].sound.get()), [safe, k] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1) { safe->proc.setLayerSound (k, {}); safe->proc.setParam ("ly" + juce::String (k + 1) + "_src", (float) lsOff); return; }
            const auto& b = factoryBank();
            if (juce::isPositiveAndBelow (r - 2, (int) b.size())) safe->proc.setLayerSound (k, b[(size_t) (r - 2)].name);
        });
    }
    bool brassActive() const { return paramValue (proc, "br_scoop") > 0.02f || paramValue (proc, "br_growl") > 0.02f || paramValue (proc, "br_form") > 0.02f || paramValue (proc, "br_fall") > 0.02f; }
    void brassOff()
    {   // the brass knobs back to zero (attack, filter and character settings stay as you shaped them)
        for (const char* id : { "br_scoop", "br_growl", "br_form", "br_fall" }) proc.setParam (id, 0.0f);
        if (onMessage) onMessage ("Brass off: scoop, growl, body and fall are back to zero");
    }
    void brassIt()
    {
        proc.setParam ("br_scoop", 0.6f); proc.setParam ("br_scoopt", 0.08f); proc.setParam ("br_growl", 0.15f);
        proc.setParam ("br_form", 0.5f); proc.setParam ("br_formf", 0.5f);
        if (paramValue (proc, "attack") < 0.02f) proc.setParam ("attack", 0.03f);
        if (paramValue (proc, "f_a") < 0.03f) proc.setParam ("f_a", 0.04f);
        if (paramValue (proc, "ch_amt") < 0.2f) proc.setParam ("ch_amt", 0.3f);
        if (onMessage) onMessage ("Brass: scoop, growl and body added - shape them above");
    }

private:
    std::vector<GhostKnob*> brassRow, smpRow;
    GhostKnob* charK = nullptr, *tapeK = nullptr, *slowK = nullptr, *vsplitK = nullptr, *ksplitK = nullptr;
    juce::TextButton* tapeBtn = nullptr, *formantBtn = nullptr, *loopBtn = nullptr;
    juce::TextButton brassBtn { "BRASS OFF" }, importBtn { "IMPORT" }, clearBtn { "CLEAR" };
    struct Layer { juce::ComboBox* src = nullptr, *play = nullptr; std::unique_ptr<juce::TextButton> sound, solo, mute; std::vector<GhostKnob*> knobs; juce::TextButton* rev = nullptr, *fx = nullptr; juce::Rectangle<int> row, readout; };
    Layer layer[NUM_LAYERS];
    SampleView sampleView { proc };
    juce::Rectangle<int> tapeArea, splitHelp;
    std::unique_ptr<juce::FileChooser> chooser;

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Sample for the SAMPLE layer", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<TrapPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f.existsAsFile()) safe->importFile (f);
        });
    }
    void timerCallback() override
    {
        { const bool b = brassActive(); if (brassBtn.getToggleState() != b) brassBtn.setToggleState (b, juce::dontSendNotification); brassBtn.setButtonText (b ? "BRASS ON" : "BRASS OFF"); }
        const bool tapeOn = paramValue (proc, "tp_on") > 0.5f;
        tapeK->setAlpha (tapeOn ? 1.0f : kGreyed);
        for (int k = 0; k < NUM_LAYERS; ++k)
        {
            auto& L = layer[k];
            const int src = k == 0 ? lsSound : (int) paramValue (proc, "ly" + juce::String (k + 1) + "_src");
            juce::String name;
            if (k == 0) { name = proc.patchName(); if (name.isEmpty()) name = proc.getProgramName (proc.getCurrentProgram()); }
            else if (src == lsSample) { auto d = proc.trapSample(); name = d != nullptr ? "SAMPLE: " + d->name : juce::String ("SAMPLE: (drop one below)"); }
            else { name = proc.layerSound (k); if (name.isEmpty()) name = "pick a sound..."; }
            if (L.sound->getButtonText() != name) L.sound->setButtonText (name);
            L.sound->setEnabled (k > 0 && src != lsSample);
            const float a = src == lsOff ? kGreyed : 1.0f;
            for (auto* kn : L.knobs) kn->setAlpha (a);
            L.rev->setAlpha (a); L.play->setAlpha (a); L.sound->setAlpha (src == lsSample ? 0.7f : a); if (L.fx) L.fx->setAlpha (src == lsSound ? 1.0f : kGreyed);
            L.solo->setToggleState (proc.layerSolo[k].load(), juce::dontSendNotification); L.mute->setToggleState (proc.layerMute[k].load(), juce::dontSendNotification);
        }
        auto d = proc.trapSample();
        if (d != sampleView.data) { sampleView.data = d; sampleView.repaint(); }
        const float st = paramValue (proc, "smp_start"), en = paramValue (proc, "smp_end");
        bool rev = false; for (int k = 1; k < NUM_LAYERS; ++k) if ((int) paramValue (proc, "ly" + juce::String (k + 1) + "_src") == lsSample && paramValue (proc, "ly" + juce::String (k + 1) + "_rev") > 0.5f) rev = true;
        if (st != sampleView.start || en != sampleView.end || rev != sampleView.reverse) { sampleView.start = st; sampleView.end = en; sampleView.reverse = rev; sampleView.repaint(); }
        const bool haveSample = d != nullptr;
        clearBtn.setEnabled (haveSample); for (auto* kn : smpRow) kn->setAlpha (haveSample ? 1.0f : kGreyed);
        repaint();
    }
};

// ================================================================== tabs + preset display
// ================================================================== MORPH BOARD page (Shared/SpiritBoard.h + ProMorph.h)
class MorphPage : public juce::Component, private juce::Timer
{
public:
    std::function<void (const juce::String&)> onMessage;
    explicit MorphPage (ProProcessor& p) : proc (p), view (p.morphBoard)
    {
        addAndMakeVisible (view);
        view.palette = [] () -> const ghosty::themes::Palette& { return ghosty::themes::get (pt::pal().theme); };
        view.hint = "click = a ghost with the CURRENT SOUND   -   right-click a ghost: set it from a snapshot / preset   -   drag the puck to morph   -   double-click a ghost: hear it alone";
        view.onAddGhost = [this] (float x, float y) { const int i = proc.morphAddGhost (x, y, proc.captureSound(), proc.patchName().isNotEmpty() ? proc.patchName() : proc.getProgramName (proc.getCurrentProgram()));
                                                      if (i >= 0) { if (! proc.morph.enabled.load()) setOn (true); say ("Ghost " + proc.morphBoard.ghosts[(size_t) i].label + " holds the current sound"); } };
        view.requestScene = [this] (int g) { contentMenu (g); };
        view.onRemoveGhost = [this] (int g) { proc.morphRemove (g); onBtn.setToggleState (proc.morph.enabled.load(), juce::dontSendNotification); };
        view.onGhostMoved = [this] (int, float, float) { proc.morphChanged (false); };
        view.onRenamed = [this] (int) { proc.morphChanged (false); };
        view.onPuckMoved = [this] (float x, float y) { proc.setParam ("morph_x", x); proc.setParam ("morph_y", y); };
        view.livePuck = [this] { return proc.morph.enabled.load() ? juce::Point<float> { proc.morph.board.livePx.load(), proc.morph.board.livePy.load() }
                                                                    : juce::Point<float> { paramValue (proc, "morph_x"), paramValue (proc, "morph_y") }; };
        view.liveWeights = [this] { std::vector<float> w; if (! proc.morph.enabled.load()) return w; const int n = proc.morph.board.liveN.load(); for (int k = 0; k < n; ++k) w.push_back (proc.morph.board.liveW[(size_t) k].load()); return w; };
        for (auto* b : { &onBtn, &randomBtn, &circleBtn, &syncBtn, &saveBtn, &loadBtn }) addAndMakeVisible (b);
        onBtn.setClickingTogglesState (true); syncBtn.setClickingTogglesState (true);
        onBtn.setTooltip ("MORPH ON: the board plays (the puck blends the ghosts' sounds). Off: the sound stays as it is and the knobs are yours again");
        onBtn.onClick = [this] { setOn (onBtn.getToggleState()); };
        randomBtn.setTooltip ("Scatter the ghosts over the board"); circleBtn.setTooltip ("Put the ghosts on a circle around the middle");
        randomBtn.onClick = [this] { proc.morphBoard.randomise ((uint32_t) juce::Random::getSystemRandom().nextInt (1 << 20)); proc.morphChanged (false); view.refresh(); };
        circleBtn.onClick = [this] { proc.morphBoard.arrangeInCircle(); proc.morphChanged (false); view.refresh(); };
        saveBtn.setTooltip ("Save this board (ghosts + their sounds) as a board file"); loadBtn.setTooltip ("Load a saved board");
        saveBtn.onClick = [this] { askSave(); }; loadBtn.onClick = [this] { loadMenu(); };
        addAndMakeVisible (orbitBox); orbitBox.addItemList ({ "ORBIT off", "Circle", "Figure 8", "Random walk" }, 1);
        orbitBox.setTooltip ("ORBIT: the puck circles around where you put it (tempo-synced with SYNC on)");
        orbitBox.onChange = [this] { proc.morphBoard.orbit.mode = (ghosty::spirit::Orbit) (orbitBox.getSelectedId() - 1); proc.morphChanged (false); };
        syncBtn.setTooltip ("SYNC: the orbit rate in cycles per beat (off: per second)");
        syncBtn.onClick = [this] { proc.morphBoard.orbit.sync = syncBtn.getToggleState(); proc.morphChanged (false); };
        for (auto* s : { &rateS, &depthS }) { addAndMakeVisible (s); s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20); }
        for (auto* sl : { &rateS, &depthS }) sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 20);
        rateS.textFromValueFunction = [] (double v) { return "RATE " + juce::String (v, 2); }; rateS.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue(); };
        depthS.textFromValueFunction = [] (double v) { return "DEPTH " + juce::String (v, 2); }; depthS.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue(); };
        rateS.setRange (0.03, 4.0, 0.01); rateS.setSkewFactorFromMidPoint (0.5); rateS.setTooltip ("ORBIT RATE (cycles per beat with SYNC, per second without)");
        depthS.setRange (0.0, 0.5, 0.01); depthS.setTooltip ("ORBIT DEPTH (how far from the puck)");
        rateS.onValueChange = [this] { proc.morphBoard.orbit.rate = (float) rateS.getValue(); proc.morphChanged (false); };
        depthS.onValueChange = [this] { proc.morphBoard.orbit.depth = (float) depthS.getValue(); proc.morphChanged (false); };
        sync();
        startTimerHz (30);
    }
    void sync()
    {
        onBtn.setToggleState (proc.morph.enabled.load(), juce::dontSendNotification);
        orbitBox.setSelectedId ((int) proc.morphBoard.orbit.mode + 1, juce::dontSendNotification);
        syncBtn.setToggleState (proc.morphBoard.orbit.sync, juce::dontSendNotification);
        rateS.setValue (proc.morphBoard.orbit.rate, juce::dontSendNotification); depthS.setValue (proc.morphBoard.orbit.depth, juce::dontSendNotification);
        view.refresh();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30);
        onBtn.setBounds (top.removeFromLeft (110)); top.removeFromLeft (8);
        randomBtn.setBounds (top.removeFromLeft (130)); top.removeFromLeft (6);
        circleBtn.setBounds (top.removeFromLeft (160)); top.removeFromLeft (14);
        orbitBox.setBounds (top.removeFromLeft (128)); top.removeFromLeft (6);
        syncBtn.setBounds (top.removeFromLeft (60)); top.removeFromLeft (6);
        rateS.setBounds (top.removeFromLeft (165)); top.removeFromLeft (6);
        depthS.setBounds (top.removeFromLeft (165));
        loadBtn.setBounds (top.removeFromRight (70)); top.removeFromRight (6); saveBtn.setBounds (top.removeFromRight (70));
        r.removeFromTop (8);
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override { if (! pghost::backdropUnder (g, *this)) g.fillAll (C().bg); }     // (GHOST LOOK: the aurora shows through)
    void lookAndFeelChanged() override { view.repaint(); }

private:
    ProProcessor& proc;
    ghosty::spirit::BoardView view;
    juce::TextButton onBtn { "MORPH ON" }, randomBtn { "RANDOM BOARD" }, circleBtn { "ARRANGE IN CIRCLE" }, syncBtn { "SYNC" }, saveBtn { "SAVE" }, loadBtn { "LOAD" };
    juce::ComboBox orbitBox; juce::Slider rateS, depthS;
    std::unique_ptr<juce::FileChooser> chooser;
    void say (const juce::String& m) { if (onMessage) onMessage (m); }
    void setOn (bool on)
    {
        on = on && ! proc.morphBoard.ghosts.empty();
        if (! on && proc.morph.enabled.load()) proc.morphCommit();      // the blend you hear becomes the knobs' values
        if (on && ! proc.morph.enabled.load()) proc.morphWriteBackEdits();   // the sound you changed while MORPH was off belongs to the ghost you were on
        proc.morph.enabled.store (on); proc.morph.lastLeader = -1; proc.morphChanged (false);
        onBtn.setToggleState (on, juce::dontSendNotification);
        say (on ? "MORPH ON: the puck blends the ghosts" : (proc.morphBoard.ghosts.empty() ? "Click the board to place a ghost first" : "MORPH off: the sound stays, the knobs are yours"));
    }
    void contentMenu (int g)
    {
        juce::PopupMenu m; m.addSectionHeader ("GHOST " + proc.morphBoard.ghosts[(size_t) g].label + " HOLDS ...");
        m.addItem (1, "CURRENT SOUND");
        juce::PopupMenu snaps;
        if (proc.snapshots != nullptr) for (int i = 0; i < proc.snapshots->size(); ++i) snaps.addItem (1000 + i, proc.snapshots->info (i).name);
        m.addSubMenu ("Snapshots", snaps, proc.snapshots != nullptr && ! proc.snapshots->empty());
        juce::PopupMenu presets; std::map<juce::String, juce::PopupMenu> byCat; juce::StringArray cats;
        const auto& bank = factoryBank();
        for (int i = 0; i < (int) bank.size(); ++i) { if (! cats.contains (bank[(size_t) i].cat)) cats.add (bank[(size_t) i].cat); byCat[bank[(size_t) i].cat].addItem (100000 + i, bank[(size_t) i].name); }
        for (auto& c : cats) presets.addSubMenu (c, byCat[c]);
        if (! bank.empty()) m.addSubMenu ("Factory sounds", presets);   // (FACTORY PRESETS OFF: none)
        juce::Component::SafePointer<MorphPage> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&view).withMousePosition(), [sp, g] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            auto& pp = sp->proc;
            if (r == 1) pp.morphSetSound (g, pp.captureSound(), pp.patchName());
            else if (r >= 100000) pp.morphSetSound (g, ProProcessor::factorySound (r - 100000), factoryBank()[(size_t) (r - 100000)].name);
            else if (r >= 1000 && pp.snapshots != nullptr) pp.morphSetSound (g, pp.snapshots->stateOf (r - 1000), pp.snapshots->info (r - 1000).name);
            sp->view.refresh();
        });
    }
    void askSave()
    {
        auto* w = new juce::AlertWindow ("Save board", "Name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("n", "My board"); w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        juce::Component::SafePointer<MorphPage> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int res) { if (sp != nullptr && res == 1) sp->say (sp->proc.saveBoard (w->getTextEditorContents ("n")) ? "Board saved" : "Couldn't save the board"); }), true);
    }
    void loadMenu()
    {
        auto files = ProProcessor::boardDir().findChildFiles (juce::File::findFiles, false, "*.ghostyboard");
        juce::PopupMenu m; m.addSectionHeader ("BOARDS");
        if (files.isEmpty()) m.addItem (-1, "(none saved yet)", false);
        for (int i = 0; i < files.size(); ++i) m.addItem (i + 1, files[i].getFileNameWithoutExtension());
        juce::Component::SafePointer<MorphPage> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn), [sp, files] (int r)
        { if (sp != nullptr && r >= 1 && r <= files.size()) { sp->say (sp->proc.loadBoard (files[r - 1]) ? "Board loaded" : "That isn't a board file"); sp->sync(); } });
    }
    void timerCallback() override { if (isShowing() || isVisible()) view.repaint(); }
};

class TabBar : public juce::Component
{
public:
    std::function<void (int)> onTab;
    int current = 0;
    juce::StringArray names { "SYNTH", "MOD", "BEND", "TRAP", "MORPH", "FX", "BROWSE" };
    const int ids[7] { 0, 1, 4, 5, 6, 2, 3 };    // tab shown at position i -> page index (BEND 4, TRAP 5, MORPH 6, shown after MOD)
    static constexpr int tabW = 110, tabStep = 115;
    void paint (juce::Graphics& g) override
    {
        const float tw = (float) tabW;
        for (int i = 0; i < names.size(); ++i)
        {
            auto r = juce::Rectangle<float> ((float) (i * tabStep), 0, tw, (float) getHeight());
            const bool on = ids[i] == current, over = i == hover;
            if (pghost::on())
            {   // GHOST LOOK: glass pill tabs, the open one violet glass with white text
                pghost::pill (g, r, 10, on, over);
                if (on) drawGhost (g, lookFor (i * 9 + 1), r.getX() + 18, r.getCentreY(), 16, 0.0f);
                if (pghost::hideTextForTests()) continue;
                g.setColour (on ? C().onAccent : C().white); g.setFont (font (12.5f));
                g.drawText (names[i], r.withTrimmedLeft (on ? 22.0f : 0.0f), juce::Justification::centred);
                continue;
            }
            if (progl::on (C().theme)) progl::pill (g, r, 9, false, C().ecto, over);     // GLASS (round 3): glass tabs (the lit one tinted below)
            if (on)
            {
                juce::ColourGradient gr (C().slime.withAlpha (0.28f), 0, r.getY(), C().ecto.withAlpha (0.12f), 0, r.getBottom(), false);
                g.setGradientFill (gr); g.fillRoundedRectangle (r, 9);
                g.setColour (C().slime); g.fillRoundedRectangle (r.withTop (r.getBottom() - 3).reduced (12, 0), 1.5f);
                drawGhost (g, lookFor (i * 9 + 1), r.getX() + 18, r.getCentreY(), 16, 0.0f);
            }
            else if (! progl::on (C().theme)) { g.setColour ((over ? C().ecto : C().card2).withAlpha (over ? 0.25f : 0.8f)); g.fillRoundedRectangle (r, 9); }
            g.setColour (on ? C().white : C().txt (0.6f)); g.setFont (font (12.5f));
            g.drawText (names[i], r.withTrimmedLeft (on ? 22.0f : 0.0f), juce::Justification::centred);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.x); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { const int h = hit (e.x); if (h >= 0 && onTab) onTab (ids[h]); }
private:
    int hover = -1;
    int hit (int x) const { const int i = x / tabStep; return juce::isPositiveAndBelow (i, names.size()) && x - i * tabStep < tabW ? i : -1; }
};

class PresetDisplay : public juce::Component, public juce::SettableTooltipClient     // (tooltip: the sound's category + tags)
{
public:
    std::function<void()> onClick, onStar;
    std::function<bool()> isFav;
    juce::String name, cat;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (progl::on (C().theme)) progl::well (g, r, 8, C().deep, isMouseOver() ? C().slime : C().faint);     // GLASS (round 3)
        else
        {
        g.setColour (C().deep); g.fillRoundedRectangle (r, 8);
        g.setColour (C().slime.withAlpha (isMouseOver() ? 0.7f : 0.3f)); g.drawRoundedRectangle (r, 8, 1.0f);
        }
        juce::Path star; star.addStar ({ r.getX() + 18, r.getCentreY() }, 5, 4.0f, 9.0f, 0.0f);
        const bool fav = isFav && isFav();
        g.setColour (fav ? C().gold : C().white.withAlpha (0.3f));
        if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.2f));
        g.setColour (C().white); g.setFont (font (15.0f));
        g.drawText (name, r.withTrimmedLeft (36).withTrimmedRight (110), juce::Justification::centredLeft);
        g.setColour (C().slime.withAlpha (0.8f)); g.setFont (font (9.5f));
        g.drawText (cat.toUpperCase(), r.withTrimmedRight (12), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.x < 34) { if (onStar) onStar(); } else if (onClick) onClick(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

// a hover-only area over painted text (the VOICES readout): shows a tooltip, draws nothing
struct TipZone : public juce::Component, public juce::SettableTooltipClient
{
    TipZone() { setInterceptsMouseClicks (true, false); setWantsKeyboardFocus (false); }
};

// GHOST LOOK: the shared BYPASS button as a glass pill in GHOST (every other look: BypassButton's own painting, unchanged)
struct ProBypass : public ghosty::BypassButton
{
    using ghosty::BypassButton::BypassButton;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    { if (pghost::on()) pghost::bypassPill (g, getLocalBounds().toFloat(), getToggleState(), over, down); else ghosty::BypassButton::paintButton (g, over, down); }
};

// ================================================================== DARK / LIGHT toggle, wordmark, re-tint
class ThemeButton : public juce::Button
{
public:
    ThemeButton() : juce::Button ("THEME") { setTooltip ("The look: BLACK -> PURPLE -> LIGHT -> GHOST (the button names the next one; saved with your project, not with sounds). Right-click: LOOK + LAYOUT (CLASSIC / STUDIO)"); }
    pt::Theme theme = pt::Theme::Dark;
    bool ghost = false;                // GHOST LOOK: the 4th look (theme = LIGHT + the glass)
    int look() const { return ghost ? pghost::kLook : ghosty::themes::toInt (theme); }
    static int nextLook (int l) { return (pt::lookFromInt (l) + 1) % (pghost::kLook + 1); }   // BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK
    juce::ModifierKeys lastMods;       // LAYOUT 2 (round 3): a right-click opens the LOOK / LAYOUT menu instead of cycling
    void clicked (const juce::ModifierKeys& m) override { lastMods = m; }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& c = C();
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (pghost::on() || nextLook (look()) == pghost::kLook) { pghost::lookButton (g, r, nextLook (look()), over, down); return; }    // GHOST LOOK (and LIGHT's button naming GHOST)
        if (progl::on (c.theme)) progl::pill (g, r, r.getHeight() * 0.5f, false, c.ecto, over || down);     // GLASS (round 3)
        else
        {
        g.setColour (over ? c.card2.brighter (c.dark ? 0.15f : 0.0f) : c.card2);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (c.dim);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, down ? 2.0f : 1.2f);
        }
        // a swatch of the NEXT look (its window + accent), then its name
        const auto nextT = ghosty::themes::next (theme);
        const auto& np = ghosty::themes::get (nextT);
        const float cx = r.getX() + 13, cy = r.getCentreY();
        g.setColour (np.window); g.fillEllipse (cx - 6, cy - 6, 12, 12);
        g.setColour (np.violet); g.drawEllipse (cx - 6, cy - 6, 12, 12, 1.6f);
        g.setColour (c.white); g.setFont (font (11.0f));
        g.drawText (ghosty::themes::displayName (nextT), r.withTrimmedLeft (22), juce::Justification::centred);
    }
};

// the "ghosty" wordmark in the current theme (DARK = ghosty::drawWordmark exactly)
void wordmark (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& tag, float glitch)
{
    if (C().dark) { drawWordmark (g, r, tag, glitch); return; }
    const auto f = font (r.getHeight() * 0.8f);
    g.setFont (f);
    auto drawText = [&] (float dx, juce::Colour a, juce::Colour b)
    {
        auto rr = r.translated (dx, 0);
        g.setColour (a); g.drawText ("ghost", rr, juce::Justification::centredLeft);
        g.setColour (b); g.drawText ("y", rr.withTrimmedLeft (textWidth (f, "ghost")), juce::Justification::centredLeft);
    };
    if (glitch > 0.05f) { drawText (-3.0f * glitch, C().slime.withAlpha (0.5f), C().slime.withAlpha (0.5f)); drawText (3.0f * glitch, C().ecto.withAlpha (0.5f), C().ecto.withAlpha (0.5f)); }
    drawText (0, C().white, C().slime);
    if (tag.isNotEmpty())
    {
        auto tr = juce::Rectangle<float> (r.getX() + textWidth (f, "ghosty") + 10.0f, r.getCentreY() - 9, 70, 18);
        g.setColour (C().ecto); g.drawRoundedRectangle (tr, 5, 1.2f);
        g.setFont (font (10.0f)); g.drawText (tag, tr, juce::Justification::centred);
    }
}

// colours a component was given explicitly (setColour) are DARK-palette colours: show them in the current theme. The original is
// kept in the component's properties, so switching back and forth never drifts.
void retint (juce::Component& c, const juce::Component* skip)
{
    if (&c == skip) return;
    static const int ids[] { juce::Slider::textBoxTextColourId, juce::TextButton::buttonOnColourId, juce::TextButton::textColourOnId, juce::TextButton::textColourOffId,
                             juce::Label::textColourId, juce::ComboBox::textColourId, juce::Slider::thumbColourId, juce::Slider::trackColourId };
    for (int id : ids)
    {
        const juce::Identifier key ("ptOrig" + juce::String (id));
        if (! c.getProperties().contains (key))
        {
            if (! c.isColourSpecified (id)) continue;
            c.getProperties().set (key, (juce::int64) c.findColour (id).getARGB());
        }
        c.setColour (id, pt::themed (juce::Colour ((juce::uint32) (juce::int64) c.getProperties()[key])));
    }
    for (auto* ch : c.getChildren()) retint (*ch, skip);
}

// ================================================================== CLEAN SOUND: header strip (switch, output meter, clip LED) + the knobs card
// The strip: CLEAN on / off, an arrow that opens the card, the output peak meter and a clip LED (green fine / amber hot / red = the
// hidden ceiling caught something; click the meter or the LED to reset it). The card: POCKET WIDTH, LOW CUT, MONO BELOW, MUD, TRIM,
// MONO CHECK and what the automatic choices are right now.
class CleanStrip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit CleanStrip (ProProcessor& p) : proc (p)
    {
        addAndMakeVisible (cleanBtn); cleanBtn.setClickingTogglesState (true);
        att = std::make_unique<BA> (proc.apvts, "cl_on", cleanBtn);
        cleanBtn.setTooltip ("CLEAN: layers, unison and effects stay clear and never clip - each layer gets its own place, the bass stays mono, "
                             "drives keep their loudness. The arrow opens its knobs.");
        addAndMakeVisible (moreBtn); moreBtn.setTooltip ("CLEAN knobs: pocket width, low cut, mono below, mud, trim, mono check");
        moreBtn.onClick = [this] { if (onMore) onMore(); };
        setTooltip ("OUTPUT: the peak level (the white line is the ceiling, -0.3 dB). The LED goes red when the hidden ceiling caught a peak (nothing ever goes over -0.3 dB). Click to reset it.");
        startTimerHz (24);
    }
    std::function<void()> onMore;
    void resized() override
    {
        if (getHeight() < 30) { cleanBtn.setBounds (0, 0, 56, getHeight()); moreBtn.setBounds (58, 0, 20, getHeight()); }
        else { cleanBtn.setBounds (0, 0, 62, 20); moreBtn.setBounds (64, 0, 20, 20); }
    }
    juce::Rectangle<float> ledArea() const { return getHeight() < 30 ? juce::Rectangle<float> (84, (float) getHeight() * 0.5f - 5, 10, 10) : juce::Rectangle<float> ((float) getWidth() - 14, 5, 10, 10); }
    juce::Rectangle<float> meterArea() const { return getHeight() < 30 ? juce::Rectangle<float> (100, (float) getHeight() * 0.5f - 3, (float) getWidth() - 100, 6) : juce::Rectangle<float> (0, 27, (float) getWidth(), 7); }
    void paint (juce::Graphics& g) override
    {
        const float pkDb = juce::Decibels::gainToDecibels (peak, -60.0f);
        const bool red = latched, amber = ! red && pkDb > -6.0f;
        auto led = ledArea();
        const auto lc = red ? C().red : amber ? C().gold : C().slime;
        g.setColour (lc.withAlpha (peak > 0.0005f || red ? 1.0f : 0.35f)); g.fillEllipse (led);
        g.setColour (C().white.withAlpha (0.25f)); g.drawEllipse (led, 1.0f);
        auto m = meterArea();
        g.setColour (C().deep); g.fillRoundedRectangle (m, 3);
        auto xOf = [&m] (float db) { return m.getX() + m.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f); };
        const float x = xOf (pkDb);
        g.setColour (pkDb > -6.0f ? C().gold : C().slime); g.fillRoundedRectangle (m.withRight (x), 3);
        if (gr < -0.05f) { g.setColour (C().red); g.fillRect (juce::Rectangle<float> (xOf (gr - 0.3f), m.getY(), xOf (-0.3f) - xOf (gr - 0.3f), m.getHeight()).withRight (m.getRight() - 1)); }
        g.setColour (C().white.withAlpha (0.6f)); g.fillRect (xOf (-0.3f) - 0.5f, m.getY() - 1, 1.0f, m.getHeight() + 2);   // the ceiling
        g.setColour (C().faint); g.drawRoundedRectangle (m, 3, 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (ledArea().expanded (6).contains (e.position) || meterArea().expanded (0, 6).contains (e.position)) { latched = false; seenOvers = proc.ceiling.overs.load(); repaint(); }
    }
    bool clipLit() const noexcept { return latched; }
    juce::TextButton cleanBtn { "CLEAN" }, moreBtn { juce::String::fromUTF8 ("\xe2\x96\xbe") };
private:
    void timerCallback() override
    {
        const float pk = proc.ceiling.outPeak.load();
        peak = juce::jmax (pk, peak * 0.85f);
        gr = proc.ceiling.grDb.load();
        const int ov = proc.ceiling.overs.load();
        if (ov != seenOvers) { seenOvers = ov; latched = true; }
        repaint();
    }
    ProProcessor& proc;
    std::unique_ptr<BA> att;
    float peak = 0, gr = 0; int seenOvers = 0; bool latched = false;
};

class CleanCard : public Page, private juce::Timer
{
public:
    static constexpr int CW = 500, CH = 178;
    explicit CleanCard (ProProcessor& p) : Page (p)
    {
        card (0, 0, CW, CH, "CLEAN SOUND  -  every layer in its own place, nothing clips", C().slime);
        auto* kw = knob ("cl_width", "POCKET WIDTH"); kw->slider.setTooltip ("How far layers 2 and 3 sit out to the sides (layer 2 leans left, layer 3 right, each a bit wider). 0 = all centred.");
        auto* kl = knob ("cl_lowcut", "LOW CUT");      kl->slider.setTooltip ("Layers that don't carry the bass are cut below this, so only ONE layer owns the low end.");
        auto* km = knob ("cl_mono", "MONO BELOW");     km->slider.setTooltip ("Everything below this is mono: a solid, club-safe bass. All the way down = off.");
        auto* kd = knob ("cl_mud", "MUD");             kd->slider.setTooltip ("Gently dips 150-400 Hz, only when layers pile up there (the boxy, muddy zone).");
        auto* kt = knob ("out_trim", "TRIM");          kt->slider.setTooltip ("The sound's level trim - what a preset sets so all sounds are equally loud. VOLUME is your own level on top.");
        ks = { kw, kl, km, kd, kt };
        addAndMakeVisible (monoBtn); monoBtn.setClickingTogglesState (true);
        monoBtn.setToggleState (proc.monoCheck.load(), juce::dontSendNotification);
        monoBtn.onClick = [this] { proc.monoCheck.store (monoBtn.getToggleState()); };
        monoBtn.setTooltip ("MONO CHECK: listen in mono (not saved). With CLEAN on the width backs off by itself if the mono sum would lose more than 3 dB.");
        addAndMakeVisible (closeBtn); closeBtn.onClick = [this] { setVisible (false); };
        closeBtn.setTooltip ("Close");
        setSize (CW, CH);
        startTimerHz (12);
    }
    void resized() override
    {
        placeRow (ks, { 8, 26, 5 * 66, 92 });
        monoBtn.setBounds (CW - 150, 34, 136, 26);
        closeBtn.setBounds (CW - 30, 4, 24, 20);
    }
    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        // correlation: -1 (out of phase) .. +1 (mono)
        auto bar = juce::Rectangle<float> ((float) CW - 150, 82, 136, 8);
        g.setColour (C().deep); g.fillRoundedRectangle (bar, 3);
        const float cx = bar.getX() + bar.getWidth() * (0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, corr));
        g.setColour (corr < 0.0f ? C().red : corr < 0.3f ? C().gold : C().slime);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, bar.getCentreX()), bar.getY(), std::abs (cx - bar.getCentreX()) + 1.0f, bar.getHeight()), 3);
        g.setColour (C().white.withAlpha (0.5f)); g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 2, 1.0f, bar.getHeight() + 4);
        g.setColour (C().dim); g.setFont (font (9.0f));
        g.drawText ("CORRELATION " + juce::String (corr, 2), juce::Rectangle<float> (bar.getX(), 66, bar.getWidth(), 14), juce::Justification::centredLeft);
        g.drawText ("-1", juce::Rectangle<float> (bar.getX(), 92, 30, 12), juce::Justification::centredLeft);
        g.drawText ("+1", juce::Rectangle<float> (bar.getRight() - 30, 92, 30, 12), juce::Justification::centredRight);
        // what CLEAN is doing right now, in plain words
        g.setColour (C().txt (0.8f)); g.setFont (font (10.0f));
        const bool on = paramValue (proc, "cl_on") > 0.5f;
        juce::String line;
        if (! on) line = "CLEAN is off: the sound plays as designed (the hidden ceiling still stops overs).";
        else
        {
            line << "bass: layer " << (owner + 1) << "   |   layers " << juce::String (juce::Decibels::gainToDecibels (layerLaw), 1) << " dB, voices "
                 << juce::String (juce::Decibels::gainToDecibels (voiceLaw), 1) << " dB   |   mud dip " << juce::String (-mudDip, 1) << " dB   |   width " << juce::roundToInt (width * 100) << " %";
        }
        g.drawText (line, juce::Rectangle<int> (12, 124, CW - 24, 16), juce::Justification::centredLeft);
        g.setColour (C().dim); g.setFont (font (9.5f));
        g.drawText ("ceiling " + juce::String (juce::jmin (0.0f, grDb), 1) + " dB  -  presets only set knobs: TRIM is the level a preset brings", juce::Rectangle<int> (12, 144, CW - 24, 16), juce::Justification::centredLeft);
    }
private:
    void timerCallback() override
    {
        corr = proc.corrLive.load(); owner = proc.bassOwnerLive.load(); layerLaw = proc.layerLawLive.load(); voiceLaw = proc.voiceLawLive.load();
        mudDip = proc.sumPockets.mudDipDb.load(); width = proc.sumPockets.widthGain.load(); grDb = proc.ceiling.grDb.load();
        if (monoBtn.getToggleState() != proc.monoCheck.load()) monoBtn.setToggleState (proc.monoCheck.load(), juce::dontSendNotification);
        if (isVisible()) repaint();
    }
    std::vector<GhostKnob*> ks;
    juce::TextButton monoBtn { "MONO CHECK" }, closeBtn { "x" };
    float corr = 1, layerLaw = 1, voiceLaw = 1, mudDip = 0, width = 1, grDb = 0; int owner = 0;
};

// the window and the play card (shared by CLASSIC and STUDIO: one place to restyle them; cards / knobs: paintCard + ProLnF)
static void paintHouseWindow (juce::Graphics& g, juce::Rectangle<float> b)
{   // the house window: an opaque vertical gradient (BLACK / PURPLE), one flat colour on LIGHT; no translucent full-window layers
    if (pghost::on()) { pghost::backdrop (g, juce::roundToInt (b.getWidth()), juce::roundToInt (b.getHeight())); return; }     // GHOST LOOK: the cached aurora
    const auto& h = ghosty::themes::get (C().theme);
    juce::ColourGradient wg (h.windowTop, 0, 0, h.windowBottom, 0, b.getHeight(), false);
    g.setGradientFill (wg); g.fillRect (b);
}
static void paintPlayCard (juce::Graphics& g, juce::Rectangle<float> pc)
{
    if (progl::on (C().theme)) { progl::card (g, pc, 14, C().slime); return; }     // GLASS (round 3)
    g.setColour (C().card.withAlpha (0.93f)); g.fillRoundedRectangle (pc, 14);
    g.setColour (C().slime.withAlpha (0.25f)); g.drawRoundedRectangle (pc, 14, 1.0f);
}

// LAYOUT 2 (round 3): the STUDIO layout's panels + window (ProStudio), built from the pieces above
class ProContent;
#include "ProPanels2.inc"

// ================================================================== the whole editor content
class ProContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = ProEditor::W, H = ProEditor::H;
    static constexpr int TX = 16, TY = 112, TW = W - 32, TH = 588;

    ProContent (ProProcessor& p, ProLnF& lf) : proc (p), synthPage (p), modPage (p), bendPage (p), trapPage (p), browsePage (p), fxPanel (p.apvts, p.rack), flow (p), lnf (lf)
    {
        setLookAndFeel (&lf);
        addAndMakeVisible (tabs); tabs.onTab = [this] (int i) { showTab (i); };
        addAndMakeVisible (bypassBtn); bypassBtn.setDark (true);
        addAndMakeVisible (snapBar);
        snapBar.onMessage = [this] (const juce::String& m) { toast = m; toastTime = 2.5f; };
        snapBar.onLoaded = [this] { wordGlitch = 1; browsePage.selectCurrent(); };
        proc.snapshots->onChanged = [this] { browsePage.rebuild(); snapBar.refresh(); };
        addAndMakeVisible (themeBtn); themeBtn.onClick = [this] { if (themeBtn.lastMods.isPopupMenu()) layout2Menu (&themeBtn); else setLook (ThemeButton::nextLook (themeBtn.look())); };   // (right-click: LAYOUT 2; GHOST LOOK: 4 looks)
        for (auto* c : std::initializer_list<juce::Component*> { &synthPage, &modPage, &fxPanel, &browsePage, &bendPage, &trapPage, &morphPage }) addChildComponent (c);
        morphPage.onMessage = [this] (const juce::String& m) { toast = m; toastTime = 3.0f; };
        trapPage.onMessage = [this] (const juce::String& m) { toast = m; toastTime = 3.5f; };
        addAndMakeVisible (display); addAndMakeVisible (flow); display.setComponentID ("presetDisplay");
        addAndMakeVisible (voicesTip);
        voicesTip.setTooltip ("VOICES n = notes sounding right now (every STACK layer).  a / b UNI = oscillator voices playing / the most there can be: "
                              "each note plays its OSC 1 + OSC 2 unison voices, so b = VOICES (the knob) x unison, over the layers that are on. "
                              "When every voice is busy a new note steals one (STEAL in the AMP card's MORE).");
        addAndMakeVisible (holdBtn); addAndMakeVisible (octCtl); flow.octave.onChange = [this] { flow.repaint(); };   // KEYS (round 3)
        display.onClick = [this] { showTab (3); };
        display.onStar = [this] { browsePage.toggleFavCurrent(); };
        display.isFav = [this] { return browsePage.currentIsFav(); };
        synthPage.onMessage = [this] (const juce::String& m) { toast = m; toastTime = 3.0f; };
        browsePage.onLoaded = [this] { wordGlitch = 1; };
        browsePage.onSaveRequest = [this] { askSave(); };

        for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &loadBtn, &initBtn }) addAndMakeVisible (b);
        prevBtn.onClick = [this] { step (-1); };
        nextBtn.onClick = [this] { step (1); };
        saveBtn.onClick = [this] { askSave(); };
        loadBtn.onClick = [this] { showLoadMenu(); };
        initBtn.onClick = [this] { proc.initPatch(); wordGlitch = 1; };
        addChildComponent (unlock); unlock.refresh();
        addAndMakeVisible (cleanStrip); addChildComponent (cleanCard);
        cleanStrip.onMore = [this] { cleanCard.setVisible (! cleanCard.isVisible()); if (cleanCard.isVisible()) cleanCard.toFront (false); };
        unlock.onChange = [this] { resized(); repaint(); };

        auto combo = [this] (juce::ComboBox& c, const juce::StringArray& items, const char* id)
        { addAndMakeVisible (c); c.addItemList (items, 1); comboAtts.push_back (std::make_unique<CA> (proc.apvts, id, c)); };
        combo (keyBox, keyNames(), "key"); combo (scaleBox, scaleNames(), "scale"); combo (chordBox, chordNames(), "chord");
        combo (rateBox, arpRateNames(), "arpRate"); combo (modeBox, arpModeNames(), "arpMode"); combo (octBox, { "1 octave", "2 octaves", "3 octaves" }, "arpOct");
        addAndMakeVisible (arpBtn); arpBtn.setClickingTogglesState (true); buttonAtts.push_back (std::make_unique<BA> (proc.apvts, "arp", arpBtn));
        gateKnob = std::make_unique<GhostKnob> (proc.apvts, "arpGate", "ARP GATE"); addAndMakeVisible (*gateKnob);

        for (int i = 0; i < 11; ++i)
        {
            auto& r = juce::Random::getSystemRandom();
            bg.push_back ({ lookFor ((i * 4 + 1) % 35), r.nextFloat() * 6.28f, 0.25f + r.nextFloat() * 0.45f, (0.04f + r.nextFloat() * 0.08f) * (r.nextBool() ? 1.0f : -1.0f), 20 + r.nextFloat() * 28, 0.10f + r.nextFloat() * 0.12f });
        }
        setSize (W, H);
        showTab (0);
        fxPanel.themeChanged();
        sliderConvention (*this);
        addMouseListener (this, true);          // MORPH ON: a knob turn explains why it springs back
        startTimerHz (24);
        // MOD (round 3): source tiles, amount rings, knob menus, the ROUTES sub-page (ProModUI.h); spring-loaded tabs while dragging
        modUi = std::make_unique<gpro::ModUi> (proc, *this, modPage, gpro::ModUi::Hooks {
            [this] (int i) { showTab (i); },
            [this] (juce::Point<int> pt) { if (! tabs.getBounds().contains (pt)) return -1; const int x = pt.x - tabs.getX(), i = x / TabBar::tabStep;
                                           return juce::isPositiveAndBelow (i, 7) && x - i * TabBar::tabStep < TabBar::tabW ? tabs.ids[i] : -1; },
            [this] { return tabs.current; },
            [this] (const juce::String& m) { toast = m; toastTime = 3.0f; repaint(); } });
    }
    bool modKnobMenu (juce::Slider& s) { return modUi != nullptr && modUi->knobRightClick (s); }   // MOD (round 3): right-click on a knob
    gpro::ModUi* mod() const { return modUi.get(); }

    // every knob / slider of the Ghosty Pro pages (the FX rack has its own): right-click = type a value (TypeIn), double-click =
    // default (the parameter attachments), hover = the value in real units, Shift = fine (velocity mode while held); linear bars
    // jump to the click position and scrub while dragged, rotary knobs keep their relative drag; one host gesture per drag.
    void sliderConvention (juce::Component& c)
    {
        if (&c == &fxPanel) return;
        if (auto* s = dynamic_cast<juce::Slider*> (&c))
        {
            s->setPopupDisplayEnabled (true, true, this, 700); s->getProperties().set ("hoverValue", true);
            s->setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            if (! s->isRotary()) s->setSliderSnapsToMousePosition (true);
        }
        for (auto* ch : c.getChildren()) sliderConvention (*ch);
    }
    ~ProContent() override { stopTimer(); proc.snapshots->onChanged = nullptr; setLookAndFeel (nullptr); }
    bool handleKey (const juce::KeyPress& k) { return snapBar.handleKey (k); }
    BrowsePage& browser() { return browsePage; }

    // LAYOUT 2 (round 3): STUDIO = a ProStudio (ProPanels2.inc / ProLayout2Studio.inc) laid over this content at its own size; it
    // places this content's header, tabs, pages and play row itself. CLASSIC never creates it: everything here stays as it was.
    friend class studio2::ProStudio;
    int layout() const { return studio != nullptr && studio->isOn() ? gpro::layout2::studio : gpro::layout2::classic; }
    void setLayout (int layout);
    void layout2Menu (juce::Component* target);     // LOOK + LAYOUT (right-click on the look button; the LAYOUT button in STUDIO)

    // ---- MORPH ON: the knobs follow the board
    std::vector<std::pair<juce::Component::SafePointer<juce::Slider>, int>> morphKnobs; bool morphKnobsBuilt = false;
    void followMorph()
    {
        if (! proc.morph.enabled.load()) return;
        if (! morphKnobsBuilt)
        {
            morphKnobsBuilt = true;
            std::map<juce::String, int> slotOf; for (int i = 0; i < proc.macros.slotCount(); ++i) slotOf[proc.macros.slotId (i)] = i;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            { for (auto* k : c.getChildren()) { if (auto* sl = dynamic_cast<juce::Slider*> (k)) { const auto id = sl->getProperties()["paramId"].toString(); if (slotOf.count (id)) morphKnobs.push_back ({ sl, slotOf[id] }); } walk (*k); } };
            walk (*this);
        }
        for (auto& [sl, slot] : morphKnobs)
        {
            float v = 0;
            if (sl != nullptr && proc.morph.blended (slot, v) && std::abs (sl->getValue() - v) > 1.0e-6) sl->setValue (v, juce::dontSendNotification);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (cleanCard.isVisible() && e.eventComponent != nullptr && e.eventComponent != &cleanCard && ! cleanCard.isParentOf (e.eventComponent)
            && e.eventComponent != &cleanStrip && ! cleanStrip.isParentOf (e.eventComponent)) cleanCard.setVisible (false);   // a click elsewhere closes the CLEAN card
        if (! proc.morph.enabled.load() || e.eventComponent == nullptr || morphPage.isParentOf (e.eventComponent)) return;
        for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
            if (dynamic_cast<juce::Slider*> (c) != nullptr) { toast = "MORPH is on: the knobs follow the board - click MORPH OFF (MORPH tab) to edit"; toastTime = 3.0f; repaint(); return; }
    }

    // DARK / LIGHT: the palette, every look-and-feel colour, explicitly set colours, the shared BYPASS button, and the FX rack
    // (its own look-and-feel, themed through fxtheme::set + Panel::themeChanged; DARK stays pixel-identical).
    void setTheme (pt::Theme t) { setLook (ghosty::themes::toInt (t)); }
    // GHOST LOOK: 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST (the LIGHT theme + the glass); stored in the project's look field as that index
    void setLook (int look)
    {
        look = pt::lookFromInt (look);
        proc.uiTheme.store (look);
        if (look == themeBtn.look()) return;
        const auto t = pt::themeOfLook (look);
        themeBtn.theme = t; themeBtn.ghost = look == pghost::kLook;
        pt::setLook (look);
        lnf.applyTheme();
        bypassBtn.setTheme (t);
        snapBar.setTheme (t);
        retint (*this, &fxPanel);
        fxtheme::set (t); fxPanel.themeChanged();
        sendLookAndFeelChange();
        repaint();
    }
    pt::Theme theme() const { return themeBtn.theme; }
    int look() const { return themeBtn.look(); }
    int seeThroughPixels()
    {
        // the visible page alone, on a transparent image: inside each card (corners trimmed) every pixel must be opaque
        juce::Component* page = nullptr;
        for (auto* c : { (juce::Component*) &synthPage, (juce::Component*) &modPage, (juce::Component*) &fxPanel, (juce::Component*) &browsePage, (juce::Component*) &bendPage, (juce::Component*) &trapPage })
            if (c->isVisible()) page = c;
        if (page == nullptr) return -1;
        juce::Image img (juce::Image::ARGB, page->getWidth(), page->getHeight(), true);
        { juce::Graphics g (img); page->paintEntireComponent (g, true); }
        std::vector<juce::Rectangle<int>> areas;
        if (auto* pg = dynamic_cast<Page*> (page)) areas = pg->cardRects();
        else if (page == &browsePage) areas = browsePage.cardRects();
        int bad = 0;
        for (auto r : areas)
            for (int y = r.getY() + 8; y < r.getBottom() - 8; ++y)
                for (int x = r.getX() + 8; x < r.getRight() - 8; ++x)
                    if (img.getPixelAt (x, y).getAlpha() != 255) ++bad;
        return areas.empty() ? -2 : bad;
    }

    void showTab (int i)
    {
        tabs.current = i; tabs.repaint();
        synthPage.setVisible (i == 0); modPage.setVisible (i == 1); fxPanel.setVisible (i == 2); browsePage.setVisible (i == 3); bendPage.setVisible (i == 4); trapPage.setVisible (i == 5); morphPage.setVisible (i == 6);
        if (i == 6) morphPage.sync();
        if (i == 3) { browsePage.rebuild(); browsePage.selectCurrent(); browsePage.focusList(); }
        wordGlitch = 0.6f;
        if (studio != nullptr) studio->tabShown (i);      // LAYOUT 2: in STUDIO the front page replaces the SYNTH page
    }

    void paint (juce::Graphics& g) override
    {
        if (layout() == gpro::layout2::studio) return;      // LAYOUT 2: ProStudio paints the window
        g.fillAll (C().bg);
        auto b = getLocalBounds().toFloat();
        paintHouseWindow (g, b);
        const float lvl = proc.level.load();
        {
            // the background parade stays off the tab bar, the header row and the play card (it used to float over the labels)
            juce::Graphics::ScopedSaveState ss (g);
            g.excludeClipRegion (juce::Rectangle<int> (0, 0, W, TY));      // header + tab row
            g.excludeClipRegion (playCard);
            for (auto& q : bg)
            {
                const float rr = q.r * juce::jmin (b.getWidth(), b.getHeight());
                const float x = b.getCentreX() + std::cos (q.a) * rr * 1.3f, y = b.getHeight() * 0.5f + std::sin (q.a) * rr * 0.8f;
                drawGhost (g, q.look, x, y, q.sz, wordGlitch * 0.4f, false, q.al + lvl * 0.25f, std::cos (q.a + 1.57f) < 0);
                if (pghost::on()) pghost::outlineGhost (g, x, y, q.sz, 0.35f + lvl * 0.25f, std::cos (q.a + 1.57f) < 0);    // GHOST LOOK: legible on the aurora
            }
        }
        // PRO tag glow, breathing with the sound
        if (C().dark)
        {
            juce::ColourGradient pg (C().ecto.withAlpha (0.18f + lvl * 0.35f), 164, 35, C().ecto.withAlpha (0.0f), 214, 35, true);
            g.setGradientFill (pg); g.fillEllipse (114, 5, 100, 60);
        }
        wordmark (g, { 20, 14, 230, 42 }, "PRO", wordGlitch);
        ghosty::prologo::drawMascot (g, { 214.0f, 5.0f, 31.0f, 58.0f }, C().bg, 0.6f);   // GHOSTY PRO logo (brand): angry ghost, additive
        g.setColour (C().dim); g.setFont (font (9.0f, false));
        g.drawText ("v" + ProEditor::versionString(), juce::Rectangle<int> (150, 50, 70, 12), juce::Justification::centred);

        if (proc.morph.enabled.load())
        {   // MORPH ON: say why the knobs move on their own
            g.setColour (C().pink); g.setFont (font (9.5f));
            g.drawText ("MORPH ON - knobs follow the board", juce::Rectangle<int> (16 + 7 * TabBar::tabStep + 6, 78, 170, 28), juce::Justification::centredLeft);
        }
        // voice / level readout next to the tabs
        g.setColour (C().dim); g.setFont (font (10.0f));
        int voicesAll = 0; for (int k = 0; k < NUM_LAYERS; ++k) voicesAll += proc.layerVoices[k].load();
        // ... and the same counting unison voices, playing / max (round 2 item 6): the main layer plus the STACK layers that are on
        int uniPlaying = 0, uniMax = 0;
        for (int k = 0; k < NUM_LAYERS; ++k) { uniPlaying += proc.uniLayerLive[k].load(); uniMax += proc.uniLayerMax[k].load(); }
        g.drawText ("VOICES " + juce::String (voicesAll) + "   " + juce::String (uniPlaying) + " / " + juce::String (uniMax) + " UNI", juce::Rectangle<int> (W - 16 - 300, 78, 220, 28), juce::Justification::centredRight);
        auto meter = juce::Rectangle<float> ((float) W - 16 - 70, 89, 70, 6);
        g.setColour (C().white.withAlpha (0.08f)); g.fillRoundedRectangle (meter, 3);
        g.setColour (C().slime.interpolatedWith (C().pink, juce::jmin (1.0f, lvl))); g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jmin (1.0f, lvl * 1.4f)), 3);

        // play card
        auto pc = playCard.toFloat();
        paintPlayCard (g, pc);      // (GLASS inside)
        g.setColour (C().dim); g.setFont (font (10.0f));
        g.drawText ("PLAY", juce::Rectangle<float> (pc.getX() + 12, (float) keyBox.getY(), 44, (float) keyBox.getHeight()), juce::Justification::centredLeft);
        auto led = juce::Rectangle<float> ((float) arpBtn.getRight() - 12, (float) arpBtn.getY() + 4, 6, 6);
        g.setColour (C().slime.withAlpha (0.15f + arpFlash * 0.85f)); g.fillEllipse (led);

        if (! proc.license.isUnlocked() && License::required())
        {
            g.setColour (C().gold.withAlpha (0.85f)); g.setFont (font (10.0f));
            g.drawText ("DEMO - sound drops out every 20 s", juce::Rectangle<int> (W - 16 - 330, 78, 200, 28), juce::Justification::centredLeft);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (toastTime <= 0) return;
        auto t = layout() == gpro::layout2::studio ? studio->toastRect() : juce::Rectangle<float> ((float) W / 2 - 220, (float) TY + TH - 44, 440, 32);   // (LAYOUT 2)
        g.setColour (C().card2.withAlpha (juce::jmin (1.0f, toastTime))); g.fillRoundedRectangle (t, 10);
        g.setColour (C().slime.withAlpha (juce::jmin (1.0f, toastTime))); g.drawRoundedRectangle (t, 10, 1.0f);
        g.setColour (C().white.withAlpha (juce::jmin (1.0f, toastTime))); g.setFont (font (12.0f)); g.drawText (toast, t, juce::Justification::centred);
    }

    void resized() override
    {
        if (layout() == gpro::layout2::studio) { studio->layoutContent(); return; }     // LAYOUT 2
        // header
        prevBtn.setBounds (244, 18, 34, 34); display.setBounds (282, 18, 206, 34); nextBtn.setBounds (492, 18, 34, 34);
        voicesTip.setBounds (W - 16 - 300 + 60, 78, 160, 28);    // over the VOICES readout (painted text, unchanged)
        saveBtn.setBounds (534, 18, 60, 34); loadBtn.setBounds (598, 18, 60, 34); initBtn.setBounds (662, 18, 54, 34);
        snapBar.setBounds (722, 20, 108, 30);                   // SNAPSHOT (Shift+Alt+S) + the list
        unlock.setBounds (836, 18, 112, 34);
        // CLEAN strip: the free header space between SNAPSHOT and the look button (one row under UNLOCK while that shows)
        if (unlock.isVisible()) cleanStrip.setBounds (722, 55, 226, 18); else cleanStrip.setBounds (838, 17, 112, 36);
        cleanCard.setBounds (W - 16 - CleanCard::CW, 58, CleanCard::CW, CleanCard::CH);
        themeBtn.setBounds (W - 16 - 84 - 8 - 76, 22, 76, 26);   // DARK / LIGHT, next to BYPASS
        bypassBtn.setBounds (W - 16 - 84, 22, 84, 26);      // top right, the same button in every GhostBlock plug-in
        tabs.setBounds (TX, 76, 7 * TabBar::tabStep, 32);
        const juce::Rectangle<int> tab (TX, TY, TW, TH);
        for (auto* c : std::initializer_list<juce::Component*> { &synthPage, &modPage, &fxPanel, &browsePage, &bendPage, &trapPage, &morphPage }) c->setBounds (tab);
        // play card
        const int y = TY + TH + 10;
        playCard = { TX, y, TW, H - 10 - y };
        const int ry = y + 10;
        keyBox.setBounds (66, ry, 62, 28); scaleBox.setBounds (134, ry, 160, 28); chordBox.setBounds (300, ry, 140, 28);
        arpBtn.setBounds (460, ry, 72, 28); rateBox.setBounds (538, ry, 86, 28); modeBox.setBounds (630, ry, 116, 28); octBox.setBounds (752, ry, 110, 28);
        gateKnob->setBounds (playCard.getRight() - 96, y + 6, 88, playCard.getHeight() - 12);
        holdBtn.setBounds (octBox.getRight() + 8, ry, 60, 28); octCtl.setBounds (holdBtn.getRight() + 6, ry, 82, 28);   // KEYS (round 3): free space before GATE
        flow.setBounds (26, ry + 36, gateKnob->getX() - 10 - 26, playCard.getBottom() - 10 - (ry + 36));
    }

private:
    ProProcessor& proc;
    TabBar tabs;
    SynthPage synthPage;
    ModPage modPage;
    BendPage bendPage;
    TrapPage trapPage;
    MorphPage morphPage { proc };
    BrowsePage browsePage;
    gpfx::Panel fxPanel;
    FlowStrip flow;
    keysx::HoldButton holdBtn { flow.latch };          // KEYS (round 3): PLAY row, right end
    keysx::OctaveControl octCtl { flow.octave };
    PresetDisplay display;
    TipZone voicesTip;
    UnlockButton unlock { proc.license };
public:
    CleanStrip cleanStrip { proc };
    CleanCard cleanCard { proc };
private:
    ProBypass bypassBtn { *proc.bypassParam };     // (GHOST LOOK: a glass pill in GHOST)
    ghosty::SnapshotBar snapBar { *proc.snapshots, true };
    ThemeButton themeBtn;
    ProLnF& lnf;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, loadBtn { "LOAD" }, initBtn { "INIT" }, arpBtn { "ARP" };
    juce::ComboBox keyBox, scaleBox, chordBox, rateBox, modeBox, octBox;
    std::unique_ptr<GhostKnob> gateKnob;
    std::vector<std::unique_ptr<CA>> comboAtts;
    std::vector<std::unique_ptr<BA>> buttonAtts;
    juce::Rectangle<int> playCard;
    struct BgGhost { GhostLook look; float a, r, sp, sz, al; };
    std::vector<BgGhost> bg;
    float wordGlitch = 0, arpFlash = 0, toastTime = 0;
    juce::String toast;
    int lastArpCount = 0;
    int arrowsState = -1;                      // FACTORY PRESETS OFF: the header < > enabled (1) / greyed (0)
    juce::String lastName;
    std::unique_ptr<gpro::ModUi> modUi;        // MOD (round 3): last, so it goes first (it adds children to the pages)

    void step (int d)
    {
        if (browsePage.filterActive()) { browsePage.stepInFilter (d); wordGlitch = 1; return; }   // a BROWSE filter is set: walk inside it
#if GHOSTY_FACTORY_PRESETS
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n);
#else
        // FACTORY PRESETS OFF: < > walk your saved sounds (programs 1..n); from INIT or any other sound: the first / the last
        proc.refreshUserPrograms();
        const int n = proc.getNumPrograms() - 1, cur = proc.isUserPatch() ? proc.userProgramOf (proc.patchName()) : 0;
        if (n <= 0) return;
        proc.setCurrentProgram (cur <= 0 ? (d > 0 ? 1 : n) : ((cur - 1 + d) % n + n) % n + 1);
#endif
        wordGlitch = 1;
        browsePage.selectCurrent();
    }

    void askSave()
    {
        auto* w = new juce::AlertWindow ("Save sound", "Saves every knob, both drawn waves, an imported sample and the FX to\n" + ProProcessor::userPresetDir().getFullPathName(), juce::MessageBoxIconType::NoIcon, this);
        const auto current = proc.isUserPatch() ? proc.patchName() : juce::String ("My Ghost");
        w->addTextEditor ("name", current, "Name");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ProContent> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;
            auto target = ProProcessor::userPresetDir().getChildFile (juce::File::createLegalFileName (name) + ".ghostypro");
            auto doSave = [safe, name]
            {
                if (safe == nullptr) return;
                if (! safe->proc.saveUserPreset (name))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Ghosty Pro", "Could not save the sound.");
                safe->browsePage.rebuild(); safe->wordGlitch = 1;
                safe->toast = "Saved \"" + name + "\""; safe->toastTime = 2.5f;
            };
            if (target.existsAsFile())
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace?", "\"" + target.getFileNameWithoutExtension() + "\" already exists. Replace it?",
                                                    "Replace", "Cancel", nullptr, juce::ModalCallbackFunction::create ([doSave] (int r) { if (r == 1) doSave(); }));
            else doSave();
        }), true);
    }

    void showLoadMenu()
    {
        auto files = proc.listUserPresets();
        juce::PopupMenu m;
        m.addSectionHeader ("YOUR SOUNDS");
        if (files.isEmpty()) m.addItem (-1, "(nothing saved yet - press SAVE)", false);
        for (int i = 0; i < files.size(); ++i) m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, proc.isUserPatch() && files[i].getFileNameWithoutExtension() == proc.patchName());
        m.addSeparator();
        m.addItem (100001, "Load a sound file...");
        m.addItem (100000, "Open sounds folder");
        juce::Component::SafePointer<ProContent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn), [safe, files] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 100000) { ProProcessor::userPresetDir().startAsProcess(); return; }
            if (r == 100001) { safe->chooseSoundFile(); return; }
            if (r >= 1 && r <= files.size()) { safe->proc.loadUserPreset (files[r - 1]); safe->wordGlitch = 1; safe->browsePage.selectCurrent(); }
        });
    }
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseSoundFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a Ghosty Pro sound", ProProcessor::userPresetDir(), "*.ghostypro");
        juce::Component::SafePointer<ProContent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f.existsAsFile() && ! safe->proc.loadUserPreset (f)) { safe->toast = "That isn't a Ghosty Pro sound"; safe->toastTime = 3.0f; }
        });
    }

    void timerCallback() override
    {
        if (pt::lookFromInt (proc.uiTheme.load()) != themeBtn.look()) setLook (proc.uiTheme.load());     // a project loaded while the window is open
        followMorph();
        for (auto& q : bg) q.a += q.sp * (1.0f + proc.level.load() * 4.0f) * 0.04f;
        wordGlitch *= 0.85f;
        toastTime = juce::jmax (0.0f, toastTime - 1.0f / 24.0f);
        const int ac = proc.performer.arpCounter.load();
        if (ac != lastArpCount) { lastArpCount = ac; arpFlash = 1.0f; } else arpFlash *= 0.75f;
#if ! GHOSTY_FACTORY_PRESETS
        {   // FACTORY PRESETS OFF: < > walk your sounds: greyed (with a tooltip that says why) while there are none
            const int any = proc.getNumPrograms() > 1 || browsePage.filterActive() ? 1 : 0;
            if (any != arrowsState)
            {
                arrowsState = any;
                for (auto* b : { &prevBtn, &nextBtn }) b->setEnabled (any == 1);
                prevBtn.setTooltip (any ? "Previous of your saved sounds" : "No saved sounds yet: press SAVE to keep one, then < > walk your sounds");
                nextBtn.setTooltip (any ? "Next of your saved sounds" : "No saved sounds yet: press SAVE to keep one, then < > walk your sounds");
            }
        }
#endif
        // preset display
        juce::String name = proc.patchName(), cat;
        if (name.isEmpty())
        {
            name = proc.getProgramName (proc.getCurrentProgram());
            const auto& b = factoryBank(); const int i = proc.getCurrentProgram();
            cat = juce::isPositiveAndBelow (i, (int) b.size()) ? b[(size_t) i].cat : juce::String();
        }
        else cat = proc.isUserPatch() ? "Your Sound" : "";
        if (name != display.name || cat != display.cat)
        {
            display.name = name; display.cat = cat; if (lastName.isNotEmpty()) wordGlitch = 1; lastName = name;
            const auto tg = browsePage.currentTagsText();
            display.setTooltip ((cat.isNotEmpty() ? cat : juce::String ("Sound")) + (tg.isNotEmpty() ? "  -  tags: " + tg : juce::String()) + "\nclick: BROWSE   star: favourite");
        }
        display.repaint();
        repaint (0, 0, W, TY);
        repaint (playCard);
        if (toastTime > 0) repaint();
    }

    std::unique_ptr<studio2::ProStudio> studio;     // LAYOUT 2 (round 3): made the first time STUDIO is chosen; declared last = destroyed first
};

#include "ProLayout2Studio.inc"      // LAYOUT 2 (round 3): ProStudio's code (it uses ProContent)

// ================================================================== editor
ProEditor::ProEditor (ProProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    {
        pt::setTheme (pt::Theme::Dark);     // every colour a page stores at construction is a BLACK-palette colour (themed at paint)
        fxtheme::set (false);
        lnf.applyTheme();
        setLookAndFeel (&lnf);
        content = std::make_unique<ProContent> (p, lnf);     // (its constructor themes the FX rack: fxtheme is DARK here)
    }
    content->setLook (p.uiTheme.load());     // (GHOST LOOK: 0 .. 3, anything else BLACK)
    typeIn.intercept = [this] (juce::Slider& s) { return content != nullptr && content->modKnobMenu (s); };   // MOD (round 3): right-click = the knob menu
    tips = std::make_unique<juce::TooltipWindow> (this, 600);
    addAndMakeVisible (*content);
    content->setBounds (0, 0, W, H);
    setResizable (true, true);
    setResizeLimits (W * 6 / 10, H * 6 / 10, W * 8 / 5, H * 8 / 5);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    if (gpro::layout2::savedLayout() == gpro::layout2::studio)
    {   // LAYOUT 2: the user's choice (STUDIO by default), opened at a zoom that fits the screen (1440 x 900: 80 %)
        setLayout (gpro::layout2::studio);
        if (std::getenv ("GHOSTY_NO_FIT") == nullptr)
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                if (const float z = fitZoomFor (d->userArea, content->getWidth(), content->getHeight()); z < 1.0f)
                    setSize (juce::roundToInt ((float) content->getWidth() * z), juce::roundToInt ((float) content->getHeight() * z));
    }
}

// LAYOUT 2 (round 3): CLASSIC (W x H) or STUDIO (ProStudio::W x H); the window keeps its zoom, the limits / aspect follow the layout
void ProEditor::setLayout (int l)
{
    if (content == nullptr || l == layout()) return;
    const float zoom = (float) getWidth() / (float) content->getWidth();
    content->setLayout (l);
    const int w = content->getWidth(), h = content->getHeight();
    setResizeLimits (w * 6 / 10, h * 6 / 10, w * 8 / 5, h * 8 / 5);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) w / h);
    setSize (juce::roundToInt ((float) w * zoom), juce::roundToInt ((float) h * zoom));
    resized();
}
int ProEditor::layout() const { return content != nullptr ? content->layout() : 0; }
float ProEditor::fitZoomFor (juce::Rectangle<int> a, int w, int h)
{   // room for the host's plug-in frame + title bar (FL Studio: ~110 px) and a margin
    const float z = juce::jmin (1.0f, (float) (a.getHeight() - 120) / (float) h, (float) (a.getWidth() - 40) / (float) w);
    return juce::jlimit (0.6f, 1.0f, std::floor (z * 20.0f) / 20.0f);
}

ProEditor::~ProEditor() { tips.reset(); content.reset(); setLookAndFeel (nullptr); }
juce::String ProEditor::versionString()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "dev";
   #endif
}
void ProEditor::paint (juce::Graphics& g)
{
    if (content) { pt::setLook (content->look()); fxtheme::set (content->theme()); }    // two Ghosty Pro windows may use different themes: the one painting now sets it
    g.fillAll (C().bg);
}
void ProEditor::setLightTheme (bool light) { setTheme (light ? 2 : 0); }
bool ProEditor::isLightTheme() const { return content != nullptr && content->theme() == pt::Theme::Light; }
void ProEditor::setTheme (int t) { if (content) content->setLook (t); }
int ProEditor::themeIndex() const { return content != nullptr ? content->look() : 0; }
int ProEditor::seeThroughPixels() { return content ? content->seeThroughPixels() : -1; }
void ProEditor::showCleanCard (bool show) { if (content) { content->cleanCard.setVisible (show); if (show) content->cleanCard.toFront (false); } }
void ProEditor::resized() { if (content) content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) content->getWidth())); }   // (content width: W in CLASSIC)
void ProEditor::showTab (int i) { if (content) content->showTab (i); }
gpro::ModUi* ProEditor::modUi() const { return content != nullptr ? content->mod() : nullptr; }   // MOD (round 3)
juce::Component* ProEditor::contentComponent() { return content.get(); }
void ProEditor::browseTestFilter (const juce::String& text, const juce::StringArray& tags, bool showTagPanel)
{
    if (content == nullptr) return;
    std::set<juce::String> t; for (auto& s : tags) t.insert (s);
    content->browser().testSetFilter (text, t, showTagPanel);
}
ProEditor::BrowseEmptyInfo ProEditor::browseEmptyInfo()
{
    BrowseEmptyInfo i;
    if (content == nullptr) return i;
    auto& b = content->browser();
    i.shown = b.showsEmptyState(); i.title = b.emptyStateTitle(); i.text = b.emptyStateText();
    i.area = getLocalArea (&b, b.emptyStateArea());
    i.rows = (int) b.model().shown.size(); i.entries = (int) b.model().entries.size();
    for (auto* c : b.getChildren()) if (auto* tb = dynamic_cast<juce::TextButton*> (c))
    {
        if (tb->getButtonText() == "START FROM INIT") i.initButton = tb->isVisible();
        if (tb->getButtonText() == "TAGS") i.tagsButton = tb->isVisible();
    }
    std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) if (tb->getButtonText() == "SHOW LEGACY") i.legacyButton = tb->isVisible(); walk (ch); } };
    walk (&b);
    return i;
}
juce::String ProEditor::presetNameTooltip() { if (auto* d = content != nullptr ? content->findChildWithID ("presetDisplay") : nullptr) if (auto* tc = dynamic_cast<juce::TooltipClient*> (d)) return tc->getTooltip(); return {}; }
bool ProEditor::keyPressed (const juce::KeyPress& k) { return content != nullptr && content->handleKey (k); }   // Shift+Alt+S = SNAPSHOT
