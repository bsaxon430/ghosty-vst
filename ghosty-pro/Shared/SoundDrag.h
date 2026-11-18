#pragma once
// SoundDrag (owner 2026-11-02): drag ONE sound out of a sound browser into the DAW as a WAV.
//  - newFile():   a fresh file per drag, named after the sound (FL keeps clips by path, so a reused name could bring back an
//                 older sound): ~/Music/<plugin> Exports/Sounds/<name>.wav, "<name> (2).wav", ... (the temp folder if Music
//                 can't be written). Kept there (not in /tmp) so a project that uses the sample still finds it next week.
//  - writeWav24(): 24-bit WAV at the sound's own sample rate; a sound whose two channels are the same is written mono.
//  - pageToEditor(): a rectangle the WebView page measured (CSS px, getBoundingClientRect) -> editor pixels: times the page
//                 zoom (WebZoom, WKWebView.pageZoom) plus where the WebView sits (top strip, EdgeResizer frame), cut to it.
//  - Grip:        the invisible grab area a web-page plugin puts over the page's "⇣". macOS only starts a file drag from a native
//                 mouse event, and a JUCE component can't sit above a WKWebView (the NSView is always on top), so the grip is
//                 its own little native view attached to the editor's window (attach()), above the WebView.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ghosty::sounddrag {

inline juce::String safeName(const juce::String& name) {
    juce::String s;
    for (auto c : name) { if (c < 32 || juce::String("/\\:*?\"<>|").containsChar(c)) s << ' '; else s << juce::String::charToString(c); }
    while (s.contains("  ")) s = s.replace("  ", " ");
    s = s.trim(); while (s.startsWithChar('.')) s = s.substring(1).trim(); while (s.endsWithChar('.')) s = s.dropLastCharacters(1).trim();
    if (s.length() > 80) s = s.substring(0, 80).trim();
    return s.isEmpty() ? juce::String("sound") : s;
}

inline juce::File newFile(const juce::String& plugin, const juce::String& soundName, juce::File base = {}) {
    if (base == juce::File()) base = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(plugin + " Exports");
    auto dir = base.getChildFile("Sounds");
    if (! dir.createDirectory()) { dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(plugin + " Sounds"); dir.createDirectory(); }
    return dir.getNonexistentChildFile(safeName(soundName), ".wav", false);
}

inline bool sameChannels(const juce::AudioBuffer<float>& b) {
    if (b.getNumChannels() < 2) return true;
    const float* l = b.getReadPointer(0); const float* r = b.getReadPointer(1);
    for (int i = 0; i < b.getNumSamples(); ++i) if (l[i] != r[i]) return false;
    return true;
}

inline bool writeWav24(const juce::File& f, const juce::AudioBuffer<float>& b, double sr) {
    if (b.getNumSamples() <= 0 || b.getNumChannels() <= 0 || sr <= 0) return false;
    const int ch = sameChannels(b) ? 1 : juce::jmin(2, b.getNumChannels());
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream(); if (os == nullptr) return false;
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), sr, (unsigned int) ch, 24, {}, 0)); // GATE: via-caller
    if (w == nullptr) return false;
    os.release();   // the writer owns the stream now
    const bool ok = w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); w.reset();
    return ok && f.getSize() > 44;
}

inline juce::Rectangle<int> pageToEditor(juce::Rectangle<int> web, double zoom, double x, double y, double w, double h, int pad = 2) {
    const double z = zoom > 0 ? zoom : 1.0;
    const auto r = juce::Rectangle<double>(web.getX() + x * z, web.getY() + y * z, w * z, h * z).expanded(pad).getSmallestIntegerContainer();
    return r.getIntersection(web);
}

struct Grip : juce::Component, juce::SettableTooltipClient {
    std::function<juce::File()> make;                    // writes the WAV (asks the demo gate first); a missing file = no drag
    std::function<void(const juce::File&)> drag;         // starts the external drag
    std::function<void()> tapped, wheel;                 // a click without a drag; the scroll wheel (the grip steps aside)
    Grip() { setOpaque(false); setMouseCursor(juce::MouseCursor::DraggingHandCursor); setRepaintsOnMouseActivity(true);
        setTooltip(juce::String::fromUTF8("drag \xe2\x87\xa3 into your DAW for the WAV")); setWantsKeyboardFocus(false); }
    // a native view of its own inside the editor's window, above the WebView (call when the editor has its peer)
    bool attach(juce::Component& editor) {
        if (isOnDesktop()) return true; auto* p = editor.getPeer(); if (p == nullptr) return false;
        addToDesktop(juce::ComponentPeer::windowIgnoresKeyPresses, p->getNativeHandle()); return isOnDesktop(); }
    void place(juce::Rectangle<int> r) { if (r.getWidth() < 6 || r.getHeight() < 6) { setVisible(false); return; } setBounds(r); setVisible(true); }
    void paint(juce::Graphics& g) override {   // (nearly invisible: the page draws the ⇣; never fully transparent, so it gets the mouse)
        g.fillAll(juce::Colour(0x0300ff88));
        if (isMouseOver(true)) { g.setColour(juce::Colour(0xaa00ff88)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f, 1.5f); } }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 5) return; started = true;
        const juce::File f = make ? make() : juce::File(); if (f.existsAsFile() && drag) drag(f); }
    void mouseUp(const juce::MouseEvent&) override { if (! started && tapped) tapped(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override { setVisible(false); if (wheel) wheel(); }
    bool started = false;
};

} // namespace ghosty::sounddrag
