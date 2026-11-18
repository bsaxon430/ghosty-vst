// Tests Shared/SyncTransport.h: DAW mode passthrough, own clock, independence from the DAW, edges, relocation, mode switches, no-DAW fallback.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SyncTransport.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (c) std::printf("  ok    %s\n", #c); else { std::printf("  FAIL  %s\n", #c); ++fails; } } while (0)
using ghosty::SyncTransport; using Pos = juce::AudioPlayHead::PositionInfo;

static juce::Optional<Pos> host(bool playing, double bpm, double ppq, bool withBpm = true) {
    Pos p; p.setIsPlaying(playing); if (withBpm) p.setBpm(bpm); p.setPpqPosition(ppq);
    p.setTimeSignature(juce::AudioPlayHead::TimeSignature { 7, 8 }); return p;
}
static bool near(double a, double b, double e = 1e-9) { return std::abs(a - b) <= e; }

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0; const int N = 480;   // 10 ms blocks
    const juce::Optional<Pos> noHost;

    std::printf("DAW mode follows the DAW\n");
    { SyncTransport s; s.prepare(sr);
      auto t = s.resolve(host(true, 133.0, 8.25), N);
      CHECK(t.fromDaw && t.playing && near(t.bpm, 133.0) && near(t.ppq, 8.25)); CHECK(t.timeSigNum == 7 && t.timeSigDen == 8); CHECK(t.justStarted);
      t = s.resolve(host(true, 133.0, 8.25 + N * 133.0 / 60.0 / sr), N); CHECK(! t.justStarted && ! t.relocated && t.playing);
      t = s.resolve(host(true, 133.0, 0.0), N); CHECK(t.relocated);                          // loop wrap / locate
      t = s.resolve(host(false, 133.0, 0.0), N); CHECK(t.justStopped && ! t.playing);
      t = s.resolve(host(true, 0.0, 1.0, false), N); CHECK(near(t.bpm, 120.0));              // DAW gave no tempo: own tempo fills in
    }

    std::printf("OWN mode ignores the DAW completely and runs its own clock + tempo\n");
    { SyncTransport s; s.prepare(sr); s.setMode(SyncTransport::Mode::Own); s.setOwnBpm(90.0);
      auto t = s.resolve(host(true, 140.0, 33.0), N);                                        // DAW is playing: the plugin must not
      CHECK(! t.fromDaw && ! t.playing && near(t.bpm, 90.0));
      s.startOwn(2.0); t = s.resolve(host(false, 140.0, 0.0), N);                            // DAW stopped: own PLAY still plays
      CHECK(t.playing && t.justStarted && near(t.ppq, 2.0) && near(t.bpm, 90.0) && ! t.fromDaw);
      t = s.resolve(host(false, 140.0, 0.0), N); CHECK(near(t.ppq, 2.0 + N * 90.0 / 60.0 / sr, 1e-9) && ! t.justStarted);
      double p0 = t.ppq; for (int i = 0; i < 99; ++i) t = s.resolve(host(true, 200.0, 7.0), N);   // 100 blocks = 1 s, DAW tempo/position irrelevant
      CHECK(near(t.ppq - p0, 99.0 * N * 90.0 / 60.0 / sr, 1e-6)); CHECK(! t.relocated);
      s.setOwnBpm(120.0); t = s.resolve(noHost, N); CHECK(near(t.bpm, 120.0));               // tempo change takes effect on the next block
      s.stopOwn(); t = s.resolve(host(true, 140.0, 5.0), N); CHECK(! t.playing && t.justStopped);
      s.toggleOwn(0.0); t = s.resolve(noHost, N); CHECK(t.playing && near(t.ppq, 0.0)); s.toggleOwn(); t = s.resolve(noHost, N); CHECK(! t.playing); }

    std::printf("no DAW (standalone): DAW mode falls back to the own clock\n");
    { SyncTransport s; s.prepare(sr); s.setOwnBpm(100.0);
      auto t = s.resolve(noHost, N); CHECK(! t.fromDaw && ! t.playing && near(t.bpm, 100.0));
      s.startOwn(); t = s.resolve(noHost, N); CHECK(t.playing && t.justStarted); }

    std::printf("switching the mode mid-song\n");
    { SyncTransport s; s.prepare(sr);
      auto t = s.resolve(host(true, 128.0, 16.0), N); CHECK(t.playing);
      s.setMode(SyncTransport::Mode::Own); t = s.resolve(host(true, 128.0, 16.1), N); CHECK(! t.playing && t.justStopped);   // DAW keeps playing, plugin stops
      s.setMode(SyncTransport::Mode::Daw); t = s.resolve(host(true, 128.0, 20.0), N); CHECK(t.playing && t.justStarted && t.fromDaw);   // back in sync
      s.setMode(SyncTransport::Mode::Own); (void) s.resolve(noHost, N); s.startOwn(); t = s.resolve(noHost, N); CHECK(t.playing);
      s.setMode(SyncTransport::Mode::Daw); t = s.resolve(host(false, 128.0, 0.0), N); CHECK(! t.playing && t.justStopped); }

    std::printf("robustness\n");
    { SyncTransport s; s.prepare(sr); s.setOwnBpm(NAN); CHECK(near(s.getOwnBpm(), 120.0)); s.setOwnBpm(5.0); CHECK(near(s.getOwnBpm(), 20.0)); s.setOwnBpm(5000.0); CHECK(near(s.getOwnBpm(), 999.0));
      s.startOwn(NAN); s.setMode(SyncTransport::Mode::Own); auto t = s.resolve(noHost, 0); CHECK(std::isfinite(t.ppq) && t.playing);
      Pos p; p.setIsPlaying(true); p.setBpm(NAN); p.setPpqPosition(INFINITY); p.setTimeInSamples(48000);
      s.setMode(SyncTransport::Mode::Daw); t = s.resolve(juce::Optional<Pos>(p), N); CHECK(std::isfinite(t.bpm) && std::isfinite(t.ppq)); }

    std::printf("button toggles the mode and notifies\n");
    { SyncTransport s; ghosty::SyncButton b(s); b.setBounds(0, 0, 96, 26); int calls = 0; b.onModeChanged = [&] { ++calls; };
      b.triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
      CHECK(s.getMode() == SyncTransport::Mode::Own && calls == 1);
      b.triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
      CHECK(s.getMode() == SyncTransport::Mode::Daw && calls == 2);
      juce::Image img(juce::Image::ARGB, 96, 26, true); juce::Graphics g(img); b.paintEntireComponent(g, true); b.setDark(false); CHECK(true); }

    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
