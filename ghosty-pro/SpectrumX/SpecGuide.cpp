#include "SpecGuide.h"
#include <cmath>

namespace sp {
namespace {
GuideCard card(const char* id, const char* title, const char* role, const char* what, juce::StringArray steps, juce::StringArray gestures, const char* out, int tab) {
    GuideCard c; c.id = id; c.title = title; c.role = role; c.what = what; c.steps = std::move(steps); c.gestures = std::move(gestures); c.out = out; c.tab = tab; return c;
}
constexpr float kTitle = 24.0f, kRole = 14.0f, kHead = 11.5f, kText = 13.0f;
}

const std::vector<GuideCard>& guideCards() {
    static const std::vector<GuideCard> cards = [] {
        std::vector<GuideCard> v;
        v.push_back(card("what", "WHAT IS SPECTRUM", "A sound maker you play with the mouse: paint, draw or sing a sound, then arrange it.",
            "Every tab makes sound a different way. PAINT PADS and PAINT LOOP: paint notes on a canvas (up = higher, across = time). "
            "SPECTRAL LAB: paint the sound itself on a spectrogram (a sound or a picture can become the canvas). PROCESS: change the Lab's sound "
            "(reverse, stretch, ...). ARCS: every line you draw becomes a gliding voice. VOICE: move the pitch and vowels of a voice. "
            "ARRANGE: put what you made on a timeline as clips. Every page can drag its result straight into your DAW.",
            { "Pick a tab at the top (or press the small ? on a tab).", "Press TRY IT on that tab's card: a small example loads and you can hear it.",
              "Change it: draw, drag, turn knobs.", "SEND TO ARRANGE collects it on the timeline - or drag it out into your DAW." },
            { "Knobs and bars: right-click = type a value, double-click = back to the default.",
              "SYNC: DAW = play with your DAW. SYNC: OWN = Spectrum's own PLAY, tempo and SPACE.",
              "PLAY: ON NOTES = the players only sound while a MIDI key is held. BYPASS = silence.",
              "BLACK / PURPLE / LIGHT changes the look of every page." },
            "PAINT: DRAG PATTERN OUT (WAV), DRAG STEMS OUT.  LAB / VOICE: DRAG WAV OUT.  ARCS: DRAG MIDI, RENDER AUDIO.  PROCESS: DRAG.  ARRANGE: MIX / STEMS / MIDI.", -1));
        v.push_back(card("pads", "PAINT PADS", "Paint ringing notes: every dot keeps sounding.",
            "A canvas of ringing notes. Up = higher pitch, left = pure, right = bright and driven. Notes close together beat against each other and "
            "then settle. The left panel picks the brush (BELL, PIANO, PAD, FEEDBACK) and has four STARTING POINTS in your key.",
            { "Click a STARTING POINT on the left (e.g. Bell chord) - or click the canvas to add a note.", "Drag a note up / down / sideways while it rings.",
              "Turn BLOOM, SPACE, SHIMMER at the bottom.", "HOLD on = it always sounds; HOLD off = it plays while you hold MIDI keys." },
            { "Click = add a note, drag = move it, right-click a note = remove it.", "Patterns A - H: different pictures; COPY makes a variation.",
              "STOP / START stops or restarts the ringing; FREEZE holds the sound.", "Right-click a knob = type a value, double-click = reset." },
            "Bottom right: DRAG PATTERN OUT (WAV), DRAG STEMS OUT, SEND TO ARRANGE.", (int) TabKind::Pads));
        v.push_back(card("loop", "PAINT LOOP", "Draw a loop in time with synths, drums and your own sounds.",
            "A piano-roll you draw on: up = pitch, across = time. Pick a sound in the bank on the left (SYNTHS, DRUMS, MY SAMPLES), then draw. "
            "Each sound becomes a layer with its own VOLUME, TONE, SPACE and PAN.",
            { "Click a sound on the left (e.g. a Bass).", "Choose DRAW and drag a line on the canvas: it becomes notes.",
              "Press PLAY (or play in your DAW).", "Pick another sound and draw over it: a new layer." },
            { "DRAW = a line of notes, DROP = single notes, CONNECT = a slide between two notes, MOVE = move / lengthen (drag a note's end), ERASE.",
              "Right-click a note = delete it. QUANTIZE snaps to the grid (1/4, 1/8, 1/16).", "Drag any sound from the bank or from your computer onto the canvas.",
              "Patterns A - H; right-click a pattern = copy / clear." },
            "Bottom right: DRAG PATTERN OUT (WAV), DRAG STEMS OUT, SEND TO ARRANGE.", (int) TabKind::Loop));
        v.push_back(card("lab", "SPECTRAL LAB", "Paint the sound itself: up = pitch, across = time, bright = loud.",
            "A spectrogram you can paint on. Import a sound (or a picture) and it becomes the canvas; paint, smear, shift and stretch it, then play it. "
            "The pages under the canvas add more: IMAGE (picture to sound), GHOST (a set of variations), SPLIT (band rhythms), CHAOS, DNA (morph two sounds).",
            { "IMPORT SOUND (left) - or drag a sound or picture onto the canvas. Or just paint with PAINT.", "Press PLAY at the top (or hold the ruler to scrub).",
              "Pick a tool (SMEAR, SHIFT, STRETCH ...) and paint over the sound.", "Under the canvas: drag START / END, right-drag a LOOP, then PLAY again." },
            { "Paint tools: drag = paint, right-drag = erase. SELECT: drag a box; HEAR plays it, LOOP SEL loops it, CUT BOX erases it.",
              "The waveform strip: right-click = START here, right-drag = LOOP, drag START / END, the WAV grip drags that part out.",
              "UNDO / REDO on the left (Ctrl/Cmd + Z). Wheel = zoom, 0 = fit, SPACE = play.", "KEYS on = your MIDI keys play the sound." },
            "Left: DRAG WAV OUT, SEND TO BEATS, SEND TO ARRANGE, PNG (the picture). The waveform's WAV grip drags out the highlighted part.", (int) TabKind::Lab));
#if GHOSTY_PROCESS_LAB
        v.push_back(card("process", "PROCESS", "Change the Lab's sound: reverse, stretch, spectral moves ... then keep it.",
            "Destructive processing of the SPECTRAL LAB's sound. Select a part of the waveform, build a chain of processes, hear it with PREVIEW, "
            "then APPLY it (the Lab plays the result) or BOUNCE NEW (keeps the original in the SOURCE menu).",
            { "Make or import a sound in SPECTRAL LAB first (TRY IT does that for you).", "Drag across the waveform to select a part (or leave it: the whole sound).",
              "+ ADD a process and set it, then PREVIEW.", "APPLY - or BOUNCE NEW to keep the original." },
            { "Drag on the waveform = select a region, click = cursor, Shift-click = extend.", "SPACE = play, P = preview, Enter = apply.",
              "UNDO / REDO (Ctrl/Cmd + Z). BACK TO THE LAB returns to the canvas." },
            "DRAG drags the preview (or the selected region) into your DAW.", (int) TabKind::Process));
#endif
        v.push_back(card("arcs", "ARCS", "Draw lines that become voices: each line glides along its path.",
            "Up = pitch, across = time. Every line you draw is a voice that glides along it; the page loops like a pattern. The panels below set "
            "the sound new lines get (waveform, envelope, pan, filter, FM).",
            { "FACTORY (top right) loads a ready page - or pick DRAW and drag a line.", "Press PLAY (HOST on = it follows your DAW's play instead).",
              "Draw more lines; LINE and CURVE EXP make straight and curved glides.", "Change the waveform / envelope below, then draw again." },
            { "Drag = draw, right-click / right-drag a line = erase it. SELECT: pick lines to move or edit.",
              "Ruler: right-click = play from here, right-drag = loop, double-click = fit.", "UNDO / REDO (Ctrl/Cmd + Z), SPACE = play, wheel = zoom.",
              "Pages A - H; SAVE / LOAD keep a page as a file." },
            "Bottom left: DRAG MIDI; RENDER AUDIO, then DRAG AUDIO. SEND TO ARRANGE at the top.", (int) TabKind::Arcs));
        v.push_back(card("arrange", "ARRANGE", "Put the sounds you made on a timeline and build a piece.",
            "A timeline of tracks and clips. Anything made in Spectrum arrives here with SEND TO ARRANGE (you can also IMPORT or drop files). "
            "Clips can be moved, trimmed, pitched, stretched, faded and morphed into each other.",
            { "On another tab press SEND TO ARRANGE (TRY IT adds two clips).", "Press PLAY (or SPACE).",
              "Drag clips to move them; drag their edges to trim.", "Right-drag on the ruler to loop a part, then PLAY." },
            { "Click a clip = select (inspector below: PITCH, STRETCH, GAIN, PAN ...), Alt-drag = copy, right-click = menu.",
              "Ruler: right-click = move the playhead here, right-drag = loop, drag its ends to change it.",
              "UNDO / REDO, Ctrl/Cmd + C / V, Del, S = split, Q = quantise.", "Ctrl/Cmd + wheel = zoom; FIT shows everything." },
            "Bottom right: DRAG OUT - MIX, STEMS, MIDI. RENDER writes the mix; SEND TO BEATS shares it.", (int) TabKind::Arrange));
#if GHOSTY_VOICE
        v.push_back(card("voice", "VOICE", "Change a voice: its pitch, its formants (the vowels), its timing.",
            "Load a voice (or any sound) into slot A. It is analysed into pitch and formants; the tabs at the top change them: PITCH (move it, "
            "keep the formants), BORROW (take B's vowels), CONTOUR (draw the melody), VOWEL (A E I O U over time), TUNE.",
            { "Drop a sound on A (or LOAD). TRY IT loads a short sung line.", "PLAY RESULT.",
              "Turn PITCH or FORMANT - or draw on the pitch picture.", "DRAG WAV OUT (left) when you like it." },
            { "Ruler: drag = select a part, click = hear from there. PROCESS ONLY THIS limits the change to the selection.",
              "Lanes: draw vowels or values over time; CLEAR LANE resets it.", "UNDO / REDO at the top right." },
            "Left: DRAG WAV OUT, DRAG SET. Bottom: SEND TO ARRANGE.", (int) TabKind::Voice));
        v.push_back(card("ghostvoice", "GHOST VOICE", "Sing a take, blend your voice with another voice: a new singer sings your performance.",
            "YOUR VOICE is the performance - its pitch, timing, words and vibrato stay exactly as sung. VOICE B (and C) is the voice blended in: a recording "
            "you have the rights to, a pad or instrument, or a designed voice (Glass Angel, Midnight...). BLEND sets how much of each; VOICE DESIGN shapes "
            "the new voice; PITCH tunes or transposes it; STACK makes a ghost choir.",
            { "Press REC on YOUR VOICE and sing (COUNT-IN clicks you in), or drop a WAV. TRY IT loads a short line + Glass Angel.", "VOICE B: PICK A VOICE, record, or drop a file.",
              "Drag the blend dot (or the VOICE B slider). PLAY NEW VOICE; A/B compares with your dry voice.", "SAVE VOICE keeps the blend; DRAG WAV OUT or EXPORT WAV when you like it." },
            { "Click the take to hear from there; drag START / END to use a part (double-click = whole take).", "KEEP MY CONSONANTS: your s / t / k stay yours.", "UNDO / REDO at the top right; Space plays, Tab = A/B, R = record." },
            "Left: DRAG WAV OUT, EXPORT WAV, SEND TO ARRANGE.", (int) TabKind::GhostVoice));
#endif
        return v;
    }();
    return cards;
}
int guideCardFor(TabKind k) { const auto& c = guideCards(); for (int i = 0; i < (int) c.size(); ++i) if (c[(size_t) i].tab == (int) k) return i; return 0; }
juce::String roleOf(TabKind k) { const int i = guideCardFor(k); return i > 0 ? guideCards()[(size_t) i].role : juce::String(); }
juce::String firstHintOf(TabKind k) {
    switch (k) {
        case TabKind::Pads:    return "Click a STARTING POINT on the left, or click the canvas to paint a note that rings.";
        case TabKind::Loop:    return "Pick a sound on the left, choose DRAW, then drag a line on the canvas. PLAY to hear it.";
        case TabKind::Lab:     return "IMPORT SOUND or drag a sound / picture onto the canvas - or just paint. PLAY at the top.";
        case TabKind::Process: return "Select a part of the waveform, + ADD a process, PREVIEW, then APPLY or BOUNCE NEW.";
        case TabKind::Arcs:    return "Press FACTORY for a ready page, or DRAW a line - it becomes a gliding voice. PLAY to hear it.";
        case TabKind::Arrange: return "SEND TO ARRANGE from any tab puts a clip here. PLAY, drag clips, right-drag the ruler to loop.";
        case TabKind::Voice:   return "Drop a voice on slot A (or LOAD), then PLAY RESULT and turn PITCH / FORMANT.";
        case TabKind::GhostVoice: return "REC and sing (or drop your take), PICK A VOICE for VOICE B, drag the blend dot, PLAY NEW VOICE.";
    }
    return {};
}

// ---------------------------------------------------------------- demo material
std::vector<float> demoVoice(double sr, double seconds) {
    // a pulse train (the "vocal folds") through three formant resonators that move ah -> oh -> ee, with a little vibrato and a sung melody
    const int n = (int) (sr * seconds); std::vector<float> out((size_t) n, 0.0f);
    struct Res { double y1 = 0, y2 = 0; float process(float x, double f, double bw, double sr) { const double r = std::exp(-juce::MathConstants<double>::pi * bw / sr), c = 2 * r * std::cos(2 * juce::MathConstants<double>::pi * f / sr);
        const double y = (1 - r) * x + c * y1 - r * r * y2; y2 = y1; y1 = y; return (float) y; } } r1, r2, r3;
    const double vowels[3][3] = { { 730, 1090, 2440 }, { 570, 840, 2410 }, { 270, 2290, 3010 } };   // ah, oh, ee
    const double notes[] = { 57, 60, 62, 60 };   // A3 C4 D4 C4
    double phase = 0; float prev = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr, u = t / seconds;
        const double midi = notes[juce::jlimit(0, 3, (int) (u * 4))] + 0.25 * std::sin(2 * juce::MathConstants<double>::pi * 5.2 * t) * juce::jmin(1.0, t * 2);
        phase += 440.0 * std::exp2((midi - 69) / 12.0) / sr; float src = 0; if (phase >= 1) { phase -= 1; src = 1.0f; }
        const double v = juce::jlimit(0.0, 1.999, u * 2); const int a = (int) v; const double w = v - a;
        double f[3]; for (int k = 0; k < 3; ++k) f[k] = vowels[a][k] * (1 - w) + vowels[juce::jmin(2, a + 1)][k] * w;
        const float d = src - prev * 0.97f; prev = src;   // a softened pulse
        float y = r1.process(d, f[0], 90, sr) * 1.0f + r2.process(d, f[1], 110, sr) * 0.6f + r3.process(d, f[2], 160, sr) * 0.3f;
        const float env = (float) juce::jmin(1.0, juce::jmin(t / 0.08, (seconds - t) / 0.15));
        out[(size_t) i] = y * env;
    }
    float pk = 0; for (auto x : out) pk = std::max(pk, std::abs(x)); if (pk > 0) for (auto& x : out) x *= 0.7f / pk;
    return out;
}
juce::AudioBuffer<float> demoClip(int kind, double sr) {
    const double secs = kind == 0 ? 4.0 : 6.0; const int n = (int) (sr * secs); juce::AudioBuffer<float> b(2, n); b.clear();
    if (kind == 0) {   // a plucked figure, 8th notes at 120 bpm (two bars), decaying partials
        const int seq[] = { 0, 7, 12, 7, 3, 10, 15, 10, 0, 7, 12, 19, 15, 12, 7, 3 };
        for (int k = 0; k < 16; ++k) { const int s0 = (int) (k * 0.25 * sr); const double f = 220.0 * std::exp2(seq[k] / 12.0);
            for (int i = 0; i < (int) (0.9 * sr) && s0 + i < n; ++i) { const double t = i / sr; float v = 0;
                for (int h = 1; h <= 5; ++h) v += (float) (std::sin(2 * juce::MathConstants<double>::pi * f * h * t) * std::exp(-t * (3.0 + h * 2.0)) / h);
                const float pan = (k % 2) ? 0.35f : -0.35f; b.addSample(0, s0 + i, v * 0.22f * (1 - pan)); b.addSample(1, s0 + i, v * 0.22f * (1 + pan)); } }
    } else {           // a slow pad: detuned partials of a minor chord, swelling in and out
        const double freqs[] = { 110.0, 130.81, 164.81, 220.0 };
        for (int i = 0; i < n; ++i) { const double t = i / sr; const float env = (float) std::sin(juce::MathConstants<double>::pi * t / secs); float l = 0, r = 0;
            for (int k = 0; k < 4; ++k) { l += (float) std::sin(2 * juce::MathConstants<double>::pi * freqs[k] * 1.003 * t); r += (float) std::sin(2 * juce::MathConstants<double>::pi * freqs[k] * 0.997 * t); }
            b.setSample(0, i, l * 0.08f * env); b.setSample(1, i, r * 0.08f * env); }
    }
    return b;
}

// ---------------------------------------------------------------- the guide overlay
SpecGuide::SpecGuide() {
    setWantsKeyboardFocus(true); setInterceptsMouseClicks(true, true);
    const auto& c = guideCards();
    for (int i = 0; i < (int) c.size(); ++i) {
        auto* b = list.add(new juce::TextButton(c[(size_t) i].title)); b->setComponentID("guide_" + c[(size_t) i].id); b->setClickingTogglesState(false);
        b->setTooltip(c[(size_t) i].role); b->onClick = [this, i] { showCard(i); }; addAndMakeVisible(b);
    }
    tryBtn.setComponentID("guideTry"); tryBtn.setTooltip("Load a small built-in example for this page and go there (undo / the pattern menu keep your own work)");
    tryBtn.onClick = [this] { if (onTryIt) onTryIt(cur); };
    closeBtn.setComponentID("guideClose"); closeBtn.setTooltip("Close (Esc)"); closeBtn.onClick = [this] { if (onClose) onClose(); };
    nextBtn.setComponentID("guideNext"); nextBtn.setTooltip("The next card"); nextBtn.onClick = [this] { showCard((cur + 1) % numCards()); };
    prevBtn.setComponentID("guidePrev"); prevBtn.setTooltip("The previous card"); prevBtn.onClick = [this] { showCard((cur + numCards() - 1) % numCards()); };
    for (auto* b : { &tryBtn, &closeBtn, &nextBtn, &prevBtn }) addAndMakeVisible(b);
    tryBtn.getProperties().set("tint", (juce::int64) 0xff3cff8f); closeBtn.getProperties().set("tint", (juce::int64) 0xffa855f7);
    showCard(0);
}
void SpecGuide::showCard(int i) {
    cur = juce::jlimit(0, numCards() - 1, i);
    for (int k = 0; k < list.size(); ++k) list[k]->setToggleState(k == cur, juce::dontSendNotification);
    tryBtn.setButtonText(cur == 0 ? "TRY IT (PAINT PADS)" : "TRY IT"); repaint();
}
juce::Rectangle<int> SpecGuide::listArea() const { return panelArea().reduced(18).removeFromLeft(196); }
juce::Rectangle<int> SpecGuide::bodyArea() const { auto r = panelArea().reduced(18); r.removeFromLeft(212); r.removeFromBottom(48); return r; }
void SpecGuide::resized() {
    auto l = listArea().withTrimmedTop(40); for (auto* b : list) { b->setBounds(l.removeFromTop(34).reduced(0, 3)); }
    auto p = panelArea().reduced(18); auto bottom = p.removeFromBottom(38); bottom.removeFromLeft(212);
    closeBtn.setBounds(bottom.removeFromRight(110)); bottom.removeFromRight(8); nextBtn.setBounds(bottom.removeFromRight(96)); bottom.removeFromRight(8); prevBtn.setBounds(bottom.removeFromRight(96));
    tryBtn.setBounds(bottom.removeFromLeft(220));
}
bool SpecGuide::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::downKey) { showCard((cur + 1) % numCards()); return true; }
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey) { showCard((cur + numCards() - 1) % numCards()); return true; }
    return false;
}
void SpecGuide::mouseDown(const juce::MouseEvent& e) { if (!panelArea().contains(e.getPosition()) && onClose) onClose(); }

void SpecGuide::drawFlow(juce::Graphics& g, juce::Rectangle<float> r) const {
    const auto& P = pal();
    auto box = [&](juce::Rectangle<float> b, const juce::String& t, const juce::String& sub, juce::Colour c) {
        g.setColour(P.panel2); g.fillRoundedRectangle(b, 8); g.setColour(c); g.drawRoundedRectangle(b.reduced(0.5f), 8, 1.6f);
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.setColour(P.ink); g.drawText(t, b.withTrimmedBottom(b.getHeight() * 0.45f), juce::Justification::centredBottom);
        g.setFont(juce::FontOptions(10.5f)); g.setColour(P.grey); g.drawText(sub, b.withTrimmedTop(b.getHeight() * 0.55f).reduced(4, 0), juce::Justification::centredTop, true); };
    auto arrow = [&](juce::Point<float> a, juce::Point<float> b) { juce::Path p; p.addArrow({ a, b }, 2.2f, 11.0f, 10.0f); g.setColour(P.grey); g.fillPath(p); };
    const float w = r.getWidth(), h = r.getHeight(), bw = w * 0.27f, bh = juce::jmin(46.0f, (h - 18) / 4.0f);
    const char* makers[] = { "PAINT PADS / LOOP", "SPECTRAL LAB + PROCESS", "ARCS", "VOICE" };
    const char* subs[] = { "paint notes", "paint the sound", "lines become voices", "change a voice" };
    const juce::Colour cols[] = { P.pink, P.green, P.amber, P.purple };
    for (int i = 0; i < 4; ++i) { auto b = juce::Rectangle<float>(r.getX(), r.getY() + i * (bh + 6), bw, bh); box(b, makers[i], subs[i], cols[i]);
        arrow({ b.getRight() + 4, b.getCentreY() }, { r.getX() + w * 0.37f - 6, r.getCentreY() }); }
    auto mid = juce::Rectangle<float>(r.getX() + w * 0.37f, r.getCentreY() - 34, bw, 68); box(mid, "ARRANGE", "clips on a timeline", P.blue);
    auto daw = juce::Rectangle<float>(r.getRight() - bw, r.getCentreY() - 34, bw, 68); box(daw, "YOUR DAW", "drag out WAV / MIDI", P.cyan);
    arrow({ mid.getRight() + 4, mid.getCentreY() }, { daw.getX() - 6, daw.getCentreY() });
    g.setColour(P.dim); g.setFont(juce::FontOptions(10.5f)); g.drawText("SEND TO ARRANGE", juce::Rectangle<float>(r.getX() + bw - 40, r.getBottom() - 14, w * 0.37f - bw + 80, 14), juce::Justification::centred);
    g.drawText("(every page can also drag out directly)", juce::Rectangle<float>(mid.getX(), mid.getBottom() + 4, daw.getRight() - mid.getX(), 14), juce::Justification::centred);
}
int SpecGuide::layoutCard(int i, juce::Graphics* g) const {
    const auto& c = guideCards()[(size_t) juce::jlimit(0, numCards() - 1, i)]; const auto& P = pal();
    const auto area = bodyArea(); const int W = area.getWidth(); int y = area.getY();
    auto text = [&](const juce::String& s, float size, bool bold, juce::Colour col, int indent = 0) {
        juce::AttributedString a; a.append(s, juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)), col); a.setWordWrap(juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout(a, (float) (W - indent)); const int h = (int) std::ceil(tl.getHeight());
        if (g) tl.draw(*g, juce::Rectangle<float>((float) (area.getX() + indent), (float) y, (float) (W - indent), (float) h)); y += h; };
    text(c.title, kTitle, true, P.ink); y += 2; text(c.role, kRole, true, P.cyan); y += 8;
    text(c.what, kText, false, P.ink); y += 10;
    if (c.tab < 0) { const int fh = 4 * 46 + 18 + 6 * 3; if (g) drawFlow(*g, juce::Rectangle<float>((float) area.getX(), (float) y, (float) W, (float) fh)); y += fh + 10; }
    text(c.tab < 0 ? "START HERE" : "A FIRST RESULT IN A MINUTE", kHead, true, P.grey); y += 3;
    for (int k = 0; k < c.steps.size(); ++k) { text(juce::String(k + 1) + ".  " + c.steps[k], kText, false, P.ink, 8); y += 2; }
    y += 8; text(c.tab < 0 ? "EVERYWHERE" : "GESTURES", kHead, true, P.grey); y += 3;
    for (auto& s : c.gestures) { text(juce::String::fromUTF8("\xe2\x80\xa2  ") + s, kText, false, P.ink, 8); y += 2; }
    y += 8; text("DRAG OUT / EXPORT", kHead, true, P.grey); y += 3; text(c.out, kText, false, P.ink, 8);
    return y - area.getY();
}
bool SpecGuide::textFits(int i) const { return layoutCard(i, nullptr) <= bodyArea().getHeight(); }
void SpecGuide::paint(juce::Graphics& g) {
    const auto& P = pal();
    g.fillAll(P.bg.withAlpha(0.82f));   // the page behind dims (this is the overlay's own backdrop; the panel itself is opaque)
    const auto pr = panelArea().toFloat();
    g.setColour(P.panel); g.fillRoundedRectangle(pr, 14); g.setColour(P.line); g.drawRoundedRectangle(pr.reduced(0.5f), 14, 1.2f);
    auto la = listArea(); g.setColour(P.grey); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText("HOW IT WORKS", la.removeFromTop(30), juce::Justification::centredLeft);
    g.setColour(P.line); g.drawVerticalLine(listArea().getRight() + 8, (float) listArea().getY(), (float) listArea().getBottom());
    layoutCard(cur, &g);
}

// ---------------------------------------------------------------- first-run hint
FirstRunHint::FirstRunHint() {
    gotIt.setComponentID("hintGotIt"); gotIt.setTooltip("Hide this hint (it only shows the first time)"); more.setComponentID("hintMore"); more.setTooltip("Open this page's HOW IT WORKS card");
    gotIt.onClick = [this] { if (onDismiss) onDismiss(); }; more.onClick = [this] { if (onMore) onMore(); };
    addAndMakeVisible(gotIt); addAndMakeVisible(more); setAlwaysOnTop(true);
}
void FirstRunHint::showFor(TabKind k) { kind = k; setVisible(true); toFront(false); repaint(); }
void FirstRunHint::resized() { auto r = getLocalBounds().reduced(12, 10); auto b = r.removeFromRight(130); gotIt.setBounds(b.removeFromBottom(30)); b.removeFromBottom(6); more.setBounds(b.removeFromBottom(30)); }
void FirstRunHint::paint(juce::Graphics& g) {
    const auto& P = pal(); auto r = getLocalBounds().toFloat();
    g.setColour(P.panel2); g.fillRoundedRectangle(r, 12); g.setColour(P.cyan); g.drawRoundedRectangle(r.reduced(1), 12, 2.0f);
    auto t = getLocalBounds().reduced(16, 10).withTrimmedRight(140);
    const auto label = [&] { for (auto& d : tabs()) if (d.kind == kind) return juce::String(d.label); return juce::String(); }();
    g.setColour(P.ink); g.setFont(juce::FontOptions(14.0f, juce::Font::bold)); g.drawText(label + "  -  " + roleOf(kind), t.removeFromTop(22), juce::Justification::centredLeft, true);
    g.setColour(P.grey); g.setFont(juce::FontOptions(12.5f)); g.drawFittedText("First: " + firstHintOf(kind), t, juce::Justification::topLeft, 3, 1.0f);
}

} // namespace sp
