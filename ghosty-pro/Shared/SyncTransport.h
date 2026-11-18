#pragma once
// SYNC TO DAW, shared by every plugin that runs on a clock (Mr. Mickens, Ghosty Beats, ArpEZator; also usable by Spectrum's players).
//
//   Mode::Daw  (default, how the plugins have always worked): tempo, play/stop and song position come from the DAW. Press play in the DAW and
//              the whole song plays, this plugin included, and you can keep editing while it runs. SPACE is NOT taken by the plugin, so the
//              DAW gets it. (No DAW at all, e.g. the standalone app: it falls back to its own clock and tempo automatically.)
//   Mode::Own  (independent): the plugin ignores the DAW's transport and tempo completely. It has its OWN clock, its OWN tempo (editable in
//              the plugin) and its own PLAY / SPACE (SPACE while the plugin window has focus plays/stops only this plugin; the DAW's other
//              tracks don't start and the DAW's play button doesn't start the plugin).
//
// The mode and the own tempo are saved with the project (not in presets). Nothing here allocates or locks; the UI side only touches atomics.
//
// WIRING
//   processor:  ghosty::SyncTransport sync;   prepareToPlay(): sync.prepare(sampleRate);
//               processBlock():  const auto t = sync.resolve(getPlayHead() ? getPlayHead()->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{}, numSamples);
//                                use t.playing / t.bpm / t.ppq (position of the block's first sample, in quarter notes) / t.justStarted /
//                                t.justStopped / t.relocated (loop or locate while playing) exactly where the plugin used the host values before.
//               state:  save sync.getMode() (int) and sync.getOwnBpm(); restore with setMode / setOwnBpm (missing = Daw, 120).
//   editor:     ghosty::SyncButton in the header (about 96 x 26) next to PLAY / BPM; in Own mode show an editable BPM box (+ TAP if the plugin has one)
//               and a PLAY button that calls sync.toggleOwn(startPpq); in Daw mode show the DAW's tempo read-only (greyed, "from DAW") and hide/disable the
//               plugin's own PLAY. Editor keyPressed: in Own mode SPACE = sync.toggleOwn(); in Daw mode do not consume SPACE.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <atomic>
#include <cmath>
#include <functional>

namespace ghosty {

struct TransportInfo {
    bool playing = false;        // the clock is running
    bool fromDaw = false;        // position and tempo come from the DAW (false = the plugin's own clock)
    double bpm = 120.0;
    double ppq = 0.0;            // quarter-note position of this block's first sample
    int timeSigNum = 4, timeSigDen = 4;
    bool justStarted = false;    // playing now, wasn't in the previous block (also after switching the mode)
    bool justStopped = false;
    bool relocated = false;      // the position jumped while playing (loop wrap, locate)
    bool looping = false; double loopStartPpq = 0.0, loopEndPpq = 0.0;
};

class SyncTransport {
public:
    enum class Mode { Daw = 0, Own = 1 };

    void prepare(double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 44100.0; lastPlaying = false; lastPpq = 0.0; lastN = 0; ownPpq = 0.0; }

    // ---- UI / message thread (atomics only) ----
    void setMode(Mode m) { mode.store((int) m); }
    Mode getMode() const { return (Mode) mode.load(); }
    void setOwnBpm(double b) { ownBpm.store(std::isfinite(b) ? juce::jlimit(20.0, 999.0, b) : 120.0); }
    double getOwnBpm() const { return ownBpm.load(); }
    void startOwn(double startPpq = 0.0) { startPpqReq.store(std::isfinite(startPpq) ? juce::jmax(0.0, startPpq) : 0.0); startReq.store(true); }
    void stopOwn() { stopReq.store(true); }
    void toggleOwn(double startPpq = 0.0) { if (ownPlayingUi.load()) stopOwn(); else startOwn(startPpq); }
    bool isOwnPlaying() const { return ownPlayingUi.load(); }
    // for the playhead display in Own mode (quarter notes, updated every block)
    double getOwnPpq() const { return ownPpqUi.load(); }

    // ---- audio thread, once per block ----
    TransportInfo resolve(const juce::Optional<juce::AudioPlayHead::PositionInfo>& host, int numSamples) {
        TransportInfo t;
        const bool wantDaw = getMode() == Mode::Daw;
        const bool haveHost = host.hasValue();

        // the own clock always listens to start/stop requests (so a request made in either mode is never lost)
        if (stopReq.exchange(false)) ownPlaying = false;
        if (startReq.exchange(false)) { ownPpq = startPpqReq.load(); ownPlaying = true; }

        if (wantDaw && haveHost) {
            t.fromDaw = true;
            t.playing = host->getIsPlaying();
            const double bpmHost = host->getBpm().hasValue() ? *host->getBpm() : 0.0;
            t.bpm = (std::isfinite(bpmHost) && bpmHost >= 20.0 && bpmHost <= 999.0) ? bpmHost : getOwnBpm();
            if (host->getPpqPosition().hasValue() && std::isfinite(*host->getPpqPosition())) t.ppq = *host->getPpqPosition();
            else if (host->getTimeInSamples().hasValue()) t.ppq = (double) *host->getTimeInSamples() / sr * t.bpm / 60.0;
            if (host->getTimeSignature().hasValue()) { t.timeSigNum = juce::jmax(1, host->getTimeSignature()->numerator); t.timeSigDen = juce::jmax(1, host->getTimeSignature()->denominator); }
            t.looping = host->getIsLooping();
            if (t.looping && host->getLoopPoints().hasValue()) { t.loopStartPpq = host->getLoopPoints()->ppqStart; t.loopEndPpq = host->getLoopPoints()->ppqEnd; }
        } else {
            // own clock: the plugin's own tempo and play state (also the fallback when there is no DAW at all)
            t.fromDaw = false;
            t.bpm = getOwnBpm();
            t.playing = ownPlaying;
            t.ppq = ownPpq;
            if (ownPlaying) ownPpq += (double) juce::jmax(0, numSamples) * t.bpm / 60.0 / sr;
        }
        ownPlayingUi.store(ownPlaying); ownPpqUi.store(ownPpq);

        t.justStarted = t.playing && ! lastPlaying;
        t.justStopped = ! t.playing && lastPlaying;
        if (t.playing && lastPlaying && lastN > 0) {
            const double expected = lastPpq + (double) lastN * lastBpm / 60.0 / sr;
            if (std::abs(t.ppq - expected) > 0.02) t.relocated = true;     // > ~20 ms of music off the expected position
        }
        if (t.justStarted) t.relocated = false;
        lastPlaying = t.playing; lastPpq = t.ppq; lastN = numSamples; lastBpm = t.bpm;
        return t;
    }

private:
    double sr = 44100.0;
    std::atomic<int> mode { (int) Mode::Daw };
    std::atomic<double> ownBpm { 120.0 };
    std::atomic<bool> startReq { false }, stopReq { false }, ownPlayingUi { false };
    std::atomic<double> startPpqReq { 0.0 }, ownPpqUi { 0.0 };
    // audio thread only
    bool ownPlaying = false, lastPlaying = false; double ownPpq = 0.0, lastPpq = 0.0, lastBpm = 120.0; int lastN = 0;
};

// the header button: SYNC: DAW  <->  SYNC: OWN  (the plugin shows/hides its own BPM + PLAY in onModeChanged)
class SyncButton : public juce::Button {
public:
    explicit SyncButton(SyncTransport& s) : juce::Button("SYNC"), sync(s) {
        setTooltip("SYNC TO DAW: follow the DAW's play button, tempo and position (default). OWN: this plugin runs by itself with its own tempo "
                   "and its own PLAY / SPACE, and ignores the DAW's transport.");
        setClickingTogglesState(false);
    }
    std::function<void()> onModeChanged;
    void setDark(bool d) { dark = d; themed = false; repaint(); }   // old API
    void setTheme(themes::Theme t) { theme = t; dark = t != themes::Theme::Light; themed = true; repaint(); }   // BLACK / PURPLE / LIGHT tokens
    void clicked() override {
        sync.setMode(sync.getMode() == SyncTransport::Mode::Daw ? SyncTransport::Mode::Own : SyncTransport::Mode::Daw);
        repaint(); if (onModeChanged) onModeChanged();
    }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool own = sync.getMode() == SyncTransport::Mode::Own;
        const auto& pal = themes::get(theme);
        const auto accent = themed ? themes::readableOn(pal.accent2, pal.window, 3.0) : juce::Colour(0xff4cd0ff);   // cyan = running by itself
        const auto idleFill = themed ? pal.cell : dark ? juce::Colour(0x33ffffff) : juce::Colour(0x22000000);
        const auto idleText = themed ? pal.ink : dark ? juce::Colour(0xffe9e4f5) : juce::Colour(0xff2c2540);
        g.setColour(own ? accent : idleFill.brighter(over ? 0.25f : 0.0f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(own ? accent.brighter(0.3f) : idleText.withAlpha(0.55f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, down ? 2.0f : 1.2f);
        g.setColour(own ? juce::Colour(0xff001a24) : idleText);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(13.0f, r.getHeight() * 0.52f), juce::Font::bold)));
        g.drawText(own ? "SYNC: OWN" : "SYNC: DAW", getLocalBounds(), juce::Justification::centred);
    }
private:
    SyncTransport& sync; bool dark = true, themed = false; themes::Theme theme = themes::Theme::Dark;
};

} // namespace ghosty
