#include "LabProcessPage.h"
#if GHOSTY_PROCESS_LAB
#include "SpecTheme.h"
// the shared PROCESS LAB panel in Spectrum's house look (Shared/GhostThemes.h): its own dark / light set as the base, surfaces + text from the tokens
static plab::Palette processPalette() {
    const auto& T = sp::tokens(); auto P = T.isLight ? plab::Palette::light() : plab::Palette::dark();
    auto rd = [&](juce::Colour c, juce::Colour on) { return ghosty::themes::readableOn(c, on, 3.0); };
    P.bg = T.window; P.card = T.panel; P.cell = T.cell; P.text = T.ink; P.text2 = T.dim; P.dim = T.dim;
    P.accent = rd(T.accent2, T.panel); P.accent2 = rd(T.violet, T.panel);
    P.waveBg = T.waveBg; P.wave = rd(T.accent2, T.waveBg); P.waveText = T.ink; P.waveDim = T.dim; P.waveGrid = T.line;
    P.opText = T.ink; P.opTextOff = T.dim; P.info = T.dim; P.seed = ghosty::themes::readableOn(rd(T.warn, T.panel), T.cell, 4.6); P.warn = ghosty::themes::readableOn(rd(T.bad, T.panel), T.cell, 4.6);
    P.button = T.cell; P.buttonOn = rd(T.violet, T.cell); P.buttonText = T.ink; P.buttonTextOn = sp::textOn(P.buttonOn, P.buttonOn, T);
    P.comboBg = T.cell; P.comboText = T.ink; P.comboOutline = T.line; P.sliderBg = T.cell; P.sliderTrack = rd(T.accent2, T.cell);
    P.labelBg = T.cell; P.labelText = T.ink; P.popupBg = T.card; P.popupText = T.ink; P.popupHighlight = rd(T.violet, T.card); P.popupHighlightText = sp::textOn(P.popupHighlight, P.popupHighlight, T);
    P.textEditBg = T.cell; P.textEditText = T.ink;
    return P;
}

static plab::Audio toAudio(const std::vector<float>& x) { plab::Audio a; a.rate = lab::SR; a.ch.push_back(x); return a; }
static std::vector<float> toMono(const plab::Audio& a) {   // the Lab is mono: channels are averaged (a mono result passes bit-exact)
    if (a.channels() == 1) return a.ch[0];
    std::vector<float> m((size_t) a.length(), 0.0f); for (auto& c : a.ch) for (size_t i = 0; i < m.size() && i < c.size(); ++i) m[i] += c[i] / (float) a.channels();
    return m;
}
static std::vector<float> at48k(const plab::Audio& a) { auto m = toMono(a); return std::abs(a.rate - lab::SR) < 1.0 ? m : lab::resampleTo48k(m.data(), (int) m.size(), a.rate); }

plab::Audio LabProcessPage::Host::getBuffer() { page.seenRev = page.proc.lab.revision(); return toAudio(page.proc.lab.processSource()); }
void LabProcessPage::Host::setBuffer(const plab::Audio& committed) {
    auto& L = page.proc.lab; auto name = L.sourceName.isNotEmpty() ? L.sourceName : juce::String("Lab sound");
    if (!name.endsWith(" (processed)")) name << " (processed)";
    L.setProcessed(at48k(committed), name, false); page.seenRev = L.revision(); page.status = "Processed: the Lab plays the result now (UNDO here or in the Lab brings the sound back).";
    page.repaint();
}
bool LabProcessPage::Host::bounceToNew(const plab::Audio& a) {
    auto& L = page.proc.lab; const auto base = L.sourceName.isNotEmpty() ? L.sourceName : juce::String("Lab sound");
    L.setProcessed(at48k(a), base + " (processed)", true); page.seenRev = L.revision();
    page.status = "BOUNCE NEW: the result is the Lab's sound now; \"" + base + "\" is kept in the SOURCE menu.";
    juce::MessageManager::callAsync([sp = juce::Component::SafePointer<LabProcessPage>(&page)] { if (sp) sp->reloadIfChanged(true); });
    return true;
}

LabProcessPage::LabProcessPage(SpecProcessor& p) : proc(p), host(*this) {
    setOpaque(true);
    lab = std::make_unique<plab::ProcessLabPanel>(host); lab->getProperties().set("foreignTheme", true);
    lab->setPalette(processPalette());   // the panel follows Spectrum's look (BLACK / PURPLE / LIGHT)
    addAndMakeVisible(*lab);
    if (auto st = proc.plabState(); st.isValid()) lab->setState(st);
    seenGen = proc.plabGen.load();
    sourceBox.setTooltip("SOURCE: the Lab's sound now, or a sound BOUNCE NEW kept (picking one makes it the Lab's sound; UNDO in the Lab goes back)");
    sourceBox.onChange = [this] { const int id = sourceBox.getSelectedId(); if (id >= 2) { proc.lab.useKept(id - 2); reloadIfChanged(true); } };
    addAndMakeVisible(sourceBox);
    backBtn.setTooltip("Back to the SPECTRAL LAB (it plays the processed sound)"); backBtn.onClick = [this] { if (onBack) onBack(); }; addAndMakeVisible(backBtn);
    reloadIfChanged(true);
    startTimerHz(4);
}
LabProcessPage::~LabProcessPage() { stopTimer(); proc.storePlabState(lab->getState()); proc.labAudition.stop(); lab.reset(); }
void LabProcessPage::reloadIfChanged(bool force) {
    // the Lab's sound changed behind the panel (painted, imported, undone): hand the panel the new sound
    if (force || proc.lab.revision() != seenRev) { lab->reload(); seenRev = proc.lab.revision(); }
    sourceBox.clear(juce::dontSendNotification);
    sourceBox.addItem("SOURCE: " + (proc.lab.sourceName.isNotEmpty() ? proc.lab.sourceName : juce::String("the Lab's sound")), 1);
    for (int i = 0; i < (int) proc.lab.kept.size(); ++i) sourceBox.addItem("kept: " + proc.lab.kept[(size_t) i].name, i + 2);
    sourceBox.setSelectedId(1, juce::dontSendNotification);
    repaint();
}
void LabProcessPage::themeChanged() { lab->setPalette(processPalette()); repaint(); }
void LabProcessPage::visibilityChanged() { if (isVisible()) reloadIfChanged(); else proc.labAudition.stop(); }
void LabProcessPage::timerCallback() {
    if (!isVisible()) return;
    if (proc.plabGen.load() != seenGen) { seenGen = proc.plabGen.load(); if (auto st = proc.plabState(); st.isValid()) lab->setState(st); }
    proc.storePlabState(lab->getState());   // the project always saves the current chain
    if (!lab->lab().busy() && proc.lab.revision() != seenRev) reloadIfChanged();
}
void LabProcessPage::paint(juce::Graphics& g) {
    const auto& P = sp::pal();
    g.setColour(P.bg); g.fillRect(getLocalBounds());
    g.setColour(P.ink); g.setFont(juce::FontOptions(20.0f, juce::Font::bold)); g.drawText("PROCESS", 24, 10, 160, 26, juce::Justification::centredLeft);
    g.setColour(P.grey); g.setFont(juce::FontOptions(11.0f));
    g.drawText("Destructive processes on the SPECTRAL LAB's sound (48 kHz mono). Select a region, build a chain, PREVIEW, then APPLY - or BOUNCE NEW to keep the original.",
               24, 36, getWidth() - 48, 16, juce::Justification::centredLeft, true);
    g.setColour(P.green); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(status, 520, 12, getWidth() - 520 - 24 - 330, 20, juce::Justification::centredRight, true);
}
void LabProcessPage::resized() {
    sourceBox.setBounds(getWidth() - 24 - 160 - 8 - 160, 10, 160, 26); backBtn.setBounds(getWidth() - 24 - 160, 10, 160, 26);
    lab->setBounds(16, 58, getWidth() - 32, getHeight() - 58 - 12);
}
#endif
