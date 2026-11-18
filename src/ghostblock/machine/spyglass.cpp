/*
 * Poltergeist 2 (GhostBlock) - the SPYGLASS window (see spyglass.h). Upstream's SpectrumAnalyzerComponent works as
 * it is (its only edits are colour-only Look hooks); everything in this file is the frame round it and its colours.
 */
#include "ghostblock/machine/spyglass.h"
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
using LC = ss::ui::SixSinesSkin::LogicalColor;

// a 2x sprite stretched over `d` in nine pieces; insets in design (= logical) px
static void nine(juce::Graphics &g, const juce::Image &img, int l, int t, int r, int b, juce::Rectangle<int> d)
{
    if (!img.isValid())
        return;
    const int W = img.getWidth(), H = img.getHeight();
    const int sx[4] = {0, l * 2, W - r * 2, W}, sy[4] = {0, t * 2, H - b * 2, H};
    const int dx[4] = {d.getX(), d.getX() + l, d.getRight() - r, d.getRight()}, dy[4] = {d.getY(), d.getY() + t, d.getBottom() - b, d.getBottom()};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            if (dx[i + 1] > dx[i] && dy[j + 1] > dy[j] && sx[i + 1] > sx[i] && sy[j + 1] > sy[j])
                g.drawImage(img, dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j], sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]);
}

// ---------------------------------------------------------------------------------------------- the brass close cap
struct SpyglassWindow::Cap : juce::Button
{
    juce::Image img, lit;
    Cap() : juce::Button("Close the SPYGLASS")
    {
        img = Assets::image("common/spyglass/spy-close.png");
        lit = Assets::image("common/spyglass/spy-close-lit.png");
        setTooltip("Close the SPYGLASS");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paintButton(juce::Graphics &g, bool over, bool down) override
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        auto r = getLocalBounds().toFloat();
        if (down)
            r = r.reduced(1.0f).translated(0, 0.6f);
        g.drawImage(over || down ? lit : img, r);
    }
};

// the lee-bottom gusset: a larger grab area for resizing (its ridges are painted in the frame)
struct SpyglassWindow::Corner : juce::ResizableCornerComponent
{
    using juce::ResizableCornerComponent::ResizableCornerComponent;
    void paint(juce::Graphics &) override {}
};
struct Border : juce::ResizableBorderComponent
{
    using juce::ResizableBorderComponent::ResizableBorderComponent;
    void paint(juce::Graphics &) override {}
};

// ---------------------------------------------------------------------------------------------- the machine's palette
ss::ui::SixSinesSkin SpyglassWindow::machineSkin()
{
    auto s = ss::ui::SixSinesSkin::darkDefault();
    s.set(LC::Background, juce::Colour(0xff0c1114));               // smoky glass
    s.set(LC::WindowBackground, juce::Colour(0xff0c1015));         // the kit's enamel
    s.set(LC::Border, juce::Colour(0xff6a5a34));                   // muted brass: plot frames; the grid is mixed from it and the glass
    s.set(LC::Label, juce::Colour(0xffb9a878));                    // brass lettering
    s.set(LC::Value, juce::Colour(0xffffb23e));                    // amber
    s.set(LC::Gutter, juce::Colour(0xff07090b));
    s.set(LC::Handle, juce::Colour(0xffb8a366));
    s.set(LC::MainMenuFill, juce::Colour(0xff15110c));
    s.set(LC::ButtonFill, juce::Colour(0xff1b1914));
    s.set(LC::KnobBody, juce::Colour(0xff3d3420));
    s.set(LC::PanelOutline, juce::Colour(0xff8a7444));
    s.set(LC::PatchSelectorBackground, juce::Colour(0xff0c1015));
    s.set(LC::PatchSelectorText, juce::Colour(0xffe9dcb8));
    s.set(LC::AnalyzerCurve, juce::Colour(0xffffc455));            // the spectrum line: amber gold (the scope trace: machineLook)
    s.set(LC::AnalyzerStop0, juce::Colour(0xff080b0d));            // the waterfall: smoky glass ...
    s.set(LC::AnalyzerStop1, juce::Colour(0xff0d2629));            // ... deep teal ...
    s.set(LC::AnalyzerStop2, juce::Colour(0xff1d6a5c));            // ... teal-green ...
    s.set(LC::AnalyzerStop3, juce::Colour(0xffc98a2c));            // ... amber ...
    s.set(LC::AnalyzerStop4, juce::Colour(0xffffe6a0));            // ... the kit's amber_hi
    return s;
}

// ---------------------------------------------------------------------------------------------- the analyzer's menu look
namespace
{
// the right-click menu (resolution / waveform scale): smoky glass in a brass rim, brass lettering, an amber highlight, Poppins
struct SpyMenuLnF : juce::LookAndFeel_V4
{
    juce::Typeface::Ptr medium, semi;
    SpyMenuLnF() : medium(Assets::typeface(false)), semi(Assets::typeface(true))
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0c1114));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe9dcb8));
        setColour(juce::PopupMenu::headerTextColourId, juce::Colour(0xffb9a878));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xffffb23e));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff1a1208));
    }
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override
    {
        auto tf = f.isBold() ? (semi != nullptr ? semi : medium) : medium;
        return tf != nullptr ? tf : juce::LookAndFeel_V4::getTypefaceForFont(f);
    }
    juce::Font getPopupMenuFont() override
    {
        auto o = juce::FontOptions().withHeight(15.0f);
        if (medium != nullptr)
            o = o.withTypeface(medium);
        return juce::Font(o);
    }
    void drawPopupMenuBackground(juce::Graphics &g, int w, int h) override
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff121a1e), 0, 0, juce::Colour(0xff080b0d), 0, (float)h, false));
        g.fillAll();
        g.setColour(juce::Colour(0x14dcfff0)); // a faint sheen on the glass
        g.fillRect(1, 1, w - 2, juce::jmax(1, h / 3));
        g.setColour(juce::Colour(0xff6a5a34)); // muted brass rim, a lit brass line inside it
        g.drawRect(0, 0, w, h, 1);
        g.setColour(juce::Colour(0x66b8a366));
        g.drawRect(1, 1, w - 2, h - 2, 1);
    }
};
} // namespace

ss::ui::SpectrumAnalyzerComponent::Look SpyglassWindow::machineLook()
{
    ss::ui::SpectrumAnalyzerComponent::Look l;
    l.scopeTrace = juce::Colour(0xff5fc7b0); // the scope: teal-green, apart from the amber spectrum line
    l.lineGlowWidth = 6.0f;                  // a soft amber glow under the 1.5 px spectrum line
    l.lineGlowAlpha = 0.16f;
    l.labelTypeface = Assets::typeface(false); // Poppins Medium, the machine's live-label face
    l.menuLookAndFeel = menuLnf.get();
    return l;
}

// ---------------------------------------------------------------------------------------------- the window
SpyglassWindow::SpyglassWindow(ss::Synth::audioOutputQueue_t &ring, float sr, ss::ui::defaultsProvder_t *defaults)
{
    setName("SPYGLASS");
    setTitle("Poltergeist 2 SPYGLASS");
    setOpaque(false); // the frame's corners are round
    frame = Assets::image("common/spyglass/spy-frame.png");
    plate = Assets::image("common/spyglass/spy-title.png");
    titleFont = juce::Font(juce::FontOptions().withTypeface(Assets::typeface(true)).withHeight(13.0f));
    titleFont.setExtraKerningFactor(0.14f);
    subFont = juce::Font(juce::FontOptions().withTypeface(Assets::typeface(false)).withHeight(10.5f));
    subFont.setExtraKerningFactor(0.05f);

    // upstream's widgets read a stylesheet: the same initialisation its own editor does, then our palette on it
    sst::jucegui::style::StyleSheet::initializeStyleSheets([]() {});
    const auto skin = machineSkin();
    skin.applyToStylesheet(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(sst::jucegui::style::StyleSheet::DARK));

    cons.setMinimumSize(kMinW, kMinH);
    cons.setMinimumOnscreenAmounts(kT, 80, 40, 80);
    border = std::make_unique<Border>(this, &cons);
    border->setBorderThickness(juce::BorderSize<int>(5, kL - 4, kB - 4, kR - 4));
    addAndMakeVisible(*border);

    menuLnf = std::make_unique<SpyMenuLnF>();
    content = std::make_unique<ss::ui::SpectrumAnalyzerComponent>(ring, sr, defaults, skin);
    content->setSkin(skin); // re-propagates the (re-skinned) stylesheet to its pin toggle
    content->setLook(machineLook());
    content->onAlwaysOnTopChanged = [this](bool b) { setAlwaysOnTop(b); };
    addAndMakeVisible(*content);

    corner = std::make_unique<Corner>(this, &cons);
    addAndMakeVisible(*corner);

    cap = std::make_unique<Cap>();
    cap->onClick = [sp = juce::Component::SafePointer<SpyglassWindow>(this)]() {
        // deferred, as upstream's window does: the owner deletes the window, never inside its own callback
        juce::MessageManager::callAsync([sp]() {
            if (sp && sp->onCloseRequested)
                sp->onCloseRequested();
        });
    };
    addAndMakeVisible(*cap);
    setSize(kDefW, kDefH);
}

SpyglassWindow::~SpyglassWindow()
{
    content.reset(); // stops the analysis thread before anything else goes
}

juce::Component *SpyglassWindow::closeCap() { return cap.get(); }

void SpyglassWindow::setHostSampleRate(float sr)
{
    if (content)
        content->setHostSampleRate(sr);
}

void SpyglassWindow::open(juce::Rectangle<int> where, juce::Component *nextTo)
{
    auto &displays = juce::Desktop::getInstance().getDisplays();
    if (where.isEmpty())
    {
        where = {kDefW, kDefH};
        if (nextTo != nullptr && nextTo->isShowing())
        {
            const auto ed = nextTo->getScreenBounds();
            where.setPosition(ed.getRight() - kDefW / 2, ed.getY() + 40);
        }
        else if (auto *d = displays.getPrimaryDisplay())
            where.setCentre(d->userBounds.toNearestInt().getCentre());
    }
    where.setSize(juce::jmax(kMinW, where.getWidth()), juce::jmax(kMinH, where.getHeight()));
    // keep it on a screen that is there now (a remembered place may be on a display that was unplugged)
    if (auto *d = displays.getDisplayForRect(where))
    {
        const auto ua = d->userBounds.toNearestInt();
        where.setSize(juce::jmin(where.getWidth(), ua.getWidth()), juce::jmin(where.getHeight(), ua.getHeight()));
        where = where.constrainedWithin(ua);
    }
    setBounds(where);
    addToDesktop(juce::ComponentPeer::windowHasDropShadow); // (no OS frame: the painted frame moves and resizes it)
    setAlwaysOnTop(true); // as upstream: hosts that pin the plugin window above its peers would hide it otherwise
    setVisible(true);
    toFront(false);
}

void SpyglassWindow::resized()
{
    const auto b = getLocalBounds();
    border->setBounds(b);
    corner->setBounds(b.getRight() - 24, b.getBottom() - 24, 24, 24);
    content->setBounds(b.getX() + kL, b.getY() + kT, b.getWidth() - kL - kR, b.getHeight() - kT - kB);
    cap->setBounds(b.getRight() - 32, 4, 24, 24);
    if (onBoundsChanged && isOnDesktop())
        onBoundsChanged(getScreenBounds());
}

void SpyglassWindow::moved()
{
    if (onBoundsChanged && isOnDesktop())
        onBoundsChanged(getScreenBounds());
}

void SpyglassWindow::paint(juce::Graphics &g)
{
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    // the glass behind the analyzer (it is opaque itself; this only shows while it is being laid out)
    g.setColour(juce::Colour(0xff0c1114));
    g.fillRect(getLocalBounds().reduced(kL - 2, 0).withTrimmedTop(kT - 2).withTrimmedBottom(kB - 2));
    nine(g, frame, kL, kT, kR, kB, getLocalBounds());
    // the title plate: smoked glass in a brass rim, 3-sliced to the name's length, the name in live Poppins
    const juce::String name("SPYGLASS");
    juce::GlyphArrangement ga;
    ga.addLineOfText(titleFont, name, 0, 0);
    const float tw = ga.getBoundingBox(0, -1, true).getWidth();
    const int pw = juce::jmax(118, (int)std::ceil(tw) + 40), ph = 22;
    const juce::Rectangle<int> pr(kL + 6, 4, pw, ph);
    if (plate.isValid())
    {
        const int sl = 12 * 2, W = plate.getWidth(), H = plate.getHeight();
        g.drawImage(plate, pr.getX(), pr.getY(), 12, ph, 0, 0, sl, H);
        g.drawImage(plate, pr.getX() + 12, pr.getY(), pw - 24, ph, sl, 0, W - 2 * sl, H);
        g.drawImage(plate, pr.getRight() - 12, pr.getY(), 12, ph, W - sl, 0, sl, H);
    }
    const auto tr = pr.toFloat().reduced(10.0f, 3.0f).translated(0, 0.5f);
    g.setFont(titleFont);
    g.setColour(juce::Colour(0xb0000000));
    g.drawText(name, tr.translated(0.6f, 0.9f), juce::Justification::centred, false);
    g.setColour(juce::Colour(0x55ffb23e)); // a little of the lamp behind the glass
    g.drawText(name, tr.expanded(0.6f, 0.0f), juce::Justification::centred, false);
    g.setColour(juce::Colour(0xffffd27a));
    g.drawText(name, tr, juce::Justification::centred, false);
    // what it shows, small, on the iron (only when there is room before the close cap)
    const juce::Rectangle<float> sub((float)pr.getRight() + 10.0f, (float)pr.getY(), (float)(getWidth() - 40 - pr.getRight() - 10), (float)ph);
    if (sub.getWidth() > 120)
    {
        g.setFont(subFont);
        g.setColour(juce::Colour(0x99000000));
        g.drawText("SPECTRUM   +   SCOPE", sub.translated(0.5f, 0.8f), juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xffa89c7c));
        g.drawText("SPECTRUM   +   SCOPE", sub, juce::Justification::centredLeft, true);
    }
}

void SpyglassWindow::paintOverChildren(juce::Graphics &g)
{
    // the frame stands proud of the glass: a soft shadow along the inside of the frame
    const auto in = getLocalBounds().withTrimmedLeft(kL).withTrimmedTop(kT).withTrimmedRight(kR).withTrimmedBottom(kB).toFloat();
    const auto dark = juce::Colour(0x8c000000), none = juce::Colour(0x00000000);
    g.setGradientFill(juce::ColourGradient(dark, 0, in.getY(), none, 0, in.getY() + 9.0f, false));
    g.fillRect(in.withHeight(9.0f));
    g.setGradientFill(juce::ColourGradient(dark.withMultipliedAlpha(0.7f), in.getX(), 0, none, in.getX() + 6.0f, 0, false));
    g.fillRect(in.withWidth(6.0f));
    g.setGradientFill(juce::ColourGradient(dark.withMultipliedAlpha(0.5f), in.getRight(), 0, none, in.getRight() - 5.0f, 0, false));
    g.fillRect(in.withLeft(in.getRight() - 5.0f));
    g.setGradientFill(juce::ColourGradient(dark.withMultipliedAlpha(0.5f), 0, in.getBottom(), none, 0, in.getBottom() - 5.0f, false));
    g.fillRect(in.withTop(in.getBottom() - 5.0f));
}

void SpyglassWindow::mouseDown(const juce::MouseEvent &e)
{
    dragging = e.y < kT; // the title band moves the window
    if (dragging)
        dragger.startDraggingComponent(this, e);
}
void SpyglassWindow::mouseDrag(const juce::MouseEvent &e)
{
    if (dragging)
        dragger.dragComponent(this, e, &cons);
}
void SpyglassWindow::mouseUp(const juce::MouseEvent &) { dragging = false; }

juce::Image SpyglassWindow::snapshot(float scale) { return createComponentSnapshot(getLocalBounds(), true, scale); }
} // namespace ghostblock::p2::machine
