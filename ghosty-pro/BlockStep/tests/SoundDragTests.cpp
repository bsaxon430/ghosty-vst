// ⇣ sound drag-out tests (console, local harness like EngineTests): Shared/SoundDrag.h + SampleStore::exportSound.
// The real drag (macOS NSDraggingSession from the native grab area) can't run here: these check everything up to it.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SoundDrag.h"
#include ENGINE_HEADER
#include <cstdio>

using namespace ENGINE_NS;
namespace sd = ghosty::sounddrag;
static int fails = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok   %s\n", juce::String(msg).toRawUTF8()); else { std::printf("  FAIL %s\n", juce::String(msg).toRawUTF8()); ++fails; } } while (0)

static juce::File dataDir;
static std::vector<std::unique_ptr<juce::MemoryBlock>> keep;
static const void* lookup(const juce::String& f, int& size) {
    juce::File file = dataDir.getChildFile("sounds").getChildFile(f);
    auto mb = std::make_unique<juce::MemoryBlock>(); if (! file.loadFileAsData(*mb)) { size = 0; return nullptr; }
    keep.push_back(std::move(mb)); size = (int) keep.back()->getSize(); return keep.back()->getData();
}
struct Read { int ch = 0, bits = 0; double sr = 0; juce::AudioBuffer<float> b; };
static Read readBack(const juce::File& f) {
    Read r; juce::WavAudioFormat fmt; std::unique_ptr<juce::AudioFormatReader> rd(fmt.createReaderFor(f.createInputStream().release(), true));
    if (rd == nullptr) return r; r.ch = (int) rd->numChannels; r.bits = (int) rd->bitsPerSample; r.sr = rd->sampleRate;
    r.b.setSize(r.ch, (int) rd->lengthInSamples); rd->read(&r.b, 0, (int) rd->lengthInSamples, 0, true, true); return r;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    dataDir = juce::File(argc > 1 ? argv[1] : "");
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sounddrag-test-" + juce::String(juce::Time::currentTimeMillis()));
    tmp.createDirectory();

    std::printf("safe names\n");
    CHECK(sd::safeName("OLIVER KICK ONE SHOT CLEAN PUNCHY 01") == "OLIVER KICK ONE SHOT CLEAN PUNCHY 01", "a normal name stays as it is");
    CHECK(sd::safeName("kick/snare: \"big\" <1>?*|\\") == "kick snare big 1", "/ \\ : * ? \" < > | become spaces: " + sd::safeName("kick/snare: \"big\" <1>?*|\\"));
    CHECK(sd::safeName("..hidden..") == "hidden", "no leading / trailing dots");
    CHECK(sd::safeName("") == "sound" && sd::safeName(" / ") == "sound", "an empty name = \"sound\"");
    CHECK(sd::safeName(juce::String::repeatedString("A", 300)).length() == 80, "very long names are cut to 80");
    CHECK(sd::safeName(juce::String::fromUTF8("caf\xc3\xa9 808 \xe2\x99\xab")) == juce::String::fromUTF8("caf\xc3\xa9 808 \xe2\x99\xab"), "accents and symbols are kept");

    std::printf("a fresh file per drag, named after the sound\n");
    auto f1 = sd::newFile("Block Step", "kick 7", tmp); f1.replaceWithText("x");
    auto f2 = sd::newFile("Block Step", "kick 7", tmp);
    CHECK(f1.getFileName() == "kick 7.wav" && f1.getParentDirectory().getFileName() == "Sounds", "first: Sounds/kick 7.wav");
    CHECK(f2 != f1 && f2.getFileName().startsWith("kick 7") && f2.hasFileExtension(".wav"), "second drag of the same sound: a new file (" + f2.getFileName() + ")");

    std::printf("24-bit WAV at the sound's own rate\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        juce::AudioBuffer<float> b(2, 3000); for (int i = 0; i < 3000; ++i) { b.setSample(0, i, 0.8f * std::sin(i * 0.05f)); b.setSample(1, i, 0.5f * std::cos(i * 0.031f)); }
        auto f = sd::newFile("Block Step", "stereo test", tmp); CHECK(sd::writeWav24(f, b, sr), "writes " + juce::String(sr, 0));
        auto r = readBack(f); float err = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < 3000; ++i) err = juce::jmax(err, std::abs(r.b.getSample(c, i) - b.getSample(c, i)));
        CHECK(r.bits == 24 && r.sr == sr && r.ch == 2 && r.b.getNumSamples() == 3000 && err < 2.0e-7f, "  reads back 24-bit, " + juce::String(r.sr, 0) + " Hz, 2 ch, every sample (max err " + juce::String(err, 9) + ")");
    }
    { juce::AudioBuffer<float> b(2, 500); for (int i = 0; i < 500; ++i) { const float x = 0.3f * std::sin(i * 0.2f); b.setSample(0, i, x); b.setSample(1, i, x); }
      auto f = sd::newFile("Ghost House", "mono hit", tmp); sd::writeWav24(f, b, 44100); auto r = readBack(f); CHECK(r.ch == 1 && r.b.getNumSamples() == 500, "the same left and right = a mono WAV"); }
    { juce::AudioBuffer<float> b(2, 0); CHECK(! sd::writeWav24(sd::newFile("Block Step", "empty", tmp), b, 44100), "an empty sound writes nothing"); }

    std::printf("the page's rectangle -> the plugin window, at any zoom\n");
    const juce::Rectangle<int> web(6, 30, 1268, 764);   // EdgeResizer frame (6) + top strip (30)
    for (double z : { 0.45, 0.6, 0.75, 1.0, 1.37, 2.0 }) {
        const juce::Rectangle<int> web(6, 30, (int) (1268 * z), (int) (764 * z));   // the page is the same picture at every zoom
        const auto r = sd::pageToEditor(web, z, 664.3, 409.0, 20.0, 20.0, 2);
        const int x = (int) std::floor(6 + 664.3 * z - 2), y = (int) std::floor(30 + 409.0 * z - 2), x2 = (int) std::ceil(6 + (664.3 + 20) * z + 2), y2 = (int) std::ceil(30 + (409 + 20) * z + 2);
        const auto want = juce::Rectangle<int>(x, y, x2 - x, y2 - y).getIntersection(web);
        CHECK(r == want, "zoom " + juce::String(z) + ": " + r.toString() + " (the grip at 664,409 20x20 in page px)");
    }
    CHECK(sd::pageToEditor(web, -1, 10, 10, 20, 20, 0) == juce::Rectangle<int>(16, 40, 20, 20), "no zoom yet (older macOS: pageZoom missing) = 1:1, offset by the inset");
    CHECK(sd::pageToEditor(web, 1.0, 1260, 750, 30, 30, 0).getRight() <= web.getRight() && sd::pageToEditor(web, 1.0, 1260, 750, 30, 30, 0).getBottom() <= web.getBottom(), "never past the page (the edge grips stay free)");
    CHECK(sd::pageToEditor(web, 1.0, 5000, 5000, 20, 20).isEmpty(), "off the page = nothing");

    std::printf("the sounds themselves (SampleStore::exportSound)\n");
    SampleStore store; store.init(dataDir.getChildFile("allman.json").loadFileAsString(), lookup);
    { juce::AudioBuffer<float> b; double sr = 0; const bool ok = store.exportSound(0, 0, b, sr);
      CHECK(ok && b.getNumSamples() > 1000 && sr > 0, "built-in kick 1: " + juce::String(b.getNumSamples()) + " samples at " + juce::String(sr, 0) + " Hz");
      auto f = sd::newFile("Block Step", "kick 1", tmp); auto r = (sd::writeWav24(f, b, sr), readBack(f)); CHECK(r.bits == 24 && r.sr == sr && r.b.getNumSamples() == b.getNumSamples(), "  as a 24-bit WAV at that rate"); }
    {   // a MY BANKS sound: the user's own file, whole, at its own rate (48 kHz mono here), with its leading silence kept
        auto src = tmp.getChildFile("my hit.wav"); juce::AudioBuffer<float> b(1, 48000); b.clear(); for (int i = 4800; i < 48000; ++i) b.setSample(0, i, 0.5f * std::sin(i * 0.01f) * std::exp(-(i - 4800) / 9000.0f));
        { std::unique_ptr<juce::OutputStream> os = src.createOutputStream(); juce::WavAudioFormat fmt; std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.release(), 48000, 1, 16, {}, 0)); w->writeFromAudioSampleBuffer(b, 0, 48000); }
        store.setUser(7, src, "MY HIT");
        juce::AudioBuffer<float> o; double sr = 0; const bool ok = store.exportSound(0, ghosty::userbanks::kBase + 7, o, sr);
        CHECK(ok && sr == 48000 && o.getNumChannels() == 1 && o.getNumSamples() == 48000, "a sound from your banks: the whole file, 48000 Hz, mono, " + juce::String(o.getNumSamples()) + " samples");
        CHECK(std::abs(o.getSample(0, 20000) - b.getSample(0, 20000)) < 1.0e-4f && o.getSample(0, 100) == 0.0f, "  the same audio, its start not trimmed"); }
    { juce::AudioBuffer<float> o; double sr = 0; CHECK(! store.exportSound(0, 99999, o, sr) && ! store.exportSound(42, 0, o, sr), "an unknown sound exports nothing (no crash)"); }
    { SynthKit k; auto f = sd::newFile("Block Step", "BUILT-IN SNARE", tmp); CHECK(sd::writeWav24(f, k.buf[(size_t) kInstSynth[1]], k.sr) && readBack(f).sr == 44100, "a built-in synth drum (BANK 3) writes at 44100"); }

    tmp.deleteRecursively();
    std::printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails); return fails ? 1 : 0;
}
