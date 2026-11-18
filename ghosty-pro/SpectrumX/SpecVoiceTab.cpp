#include "SpecVoiceTab.h"
#if GHOSTY_VOICE
#include "SpecTheme.h"

static constexpr int kStripH = 40;
SpecVoiceTab::SpecVoiceTab(SpecProcessor& p) : proc(p) {
    setOpaque(true);
    voicePage = std::make_unique<VoicePage>(proc.voice); voicePage->setComponentID("voicePage"); voicePage->getProperties().set("foreignTheme", true); voicePage->setOwnsTooltips(false); voicePage->allowExport = [this](juce::Component* ui, const char* id) { return proc.allowExport(ui, id); };   // the editor owns the tooltip window; demo gate
    voicePage->setThemeButtonVisible(false);   // Spectrum's DARK / LIGHT is the one toggle
    addAndMakeVisible(*voicePage);
    voicePage->setLook(sp::pal().look);
    arrangeBtn.setComponentID("voiceToArrange"); arrangeBtn.getProperties().set("tint", (juce::int64) sp::pal().blue.getARGB());
    arrangeBtn.setTooltip("Put the VOICE result on the ARRANGE timeline as a clip (ARRANGE can re-render it later from these settings)");
    arrangeBtn.onClick = [this] { sendToArrange(); }; addAndMakeVisible(arrangeBtn);
    startTimerHz(4);
}
SpecVoiceTab::~SpecVoiceTab() { stopTimer(); }
void SpecVoiceTab::themeChanged() { voicePage->setLook(sp::pal().look); repaint(); }
void SpecVoiceTab::sendToArrange() {
    juce::String err; auto r = proc.voice.result();
    if (!r || proc.voice.resultStale()) { if (proc.voice.renderNow(err)) r = proc.voice.result(); }
    if (!r || r->audio.L.empty()) { status = err.isNotEmpty() ? err : juce::String("Nothing to send yet: drop a voice into slot A first."); repaint(); return; }
    auto a = proc.voice.source(0); const juce::String name = (a ? a->name() : juce::String("Voice")) + " (voice)";
    juce::MemoryBlock st; { juce::MemoryOutputStream os(st, false); proc.voice.toTree().writeToStream(os); }
    const auto id = proc.arrange.receive(SpecProcessor::voicePayload(*r, name, std::move(st)));
    status = id != 0 ? "\"" + name + "\" is on the ARRANGE timeline." : juce::String("Couldn't send it (the result is empty)."); repaint();
}
void SpecVoiceTab::timerCallback() {
    if (proc.bypassParam->get() && proc.voice.playing.load() != voice::PlayNone) proc.voice.stop();   // BYPASS: the preview stops too
}
void SpecVoiceTab::paint(juce::Graphics& g) {
    const auto& P = sp::pal(); auto strip = getLocalBounds().removeFromBottom(kStripH);
    g.setColour(P.panel); g.fillRect(strip); g.setColour(P.line); g.drawHorizontalLine(strip.getY(), 0, (float) getWidth());
    g.setColour(P.grey); g.setFont(juce::FontOptions(11.0f)); g.drawText("The VOICE result can go to ARRANGE as a clip; drag it out from the page itself.", 196, strip.getY(), 520, kStripH, juce::Justification::centredLeft, true);
    g.setColour(P.green); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(status, 720, strip.getY(), getWidth() - 736, kStripH, juce::Justification::centredRight, true);
}
void SpecVoiceTab::resized() {
    auto r = getLocalBounds(); auto strip = r.removeFromBottom(kStripH);
    voicePage->setBounds(r);
    arrangeBtn.setBounds(16, strip.getY() + 6, 168, kStripH - 12);
}
#endif
