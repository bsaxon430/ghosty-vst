#pragma once
// DemoGateHarness: the shared part of every plugin's `demogate` test (GHOSTY_TEST_HOOKS=1 builds only).
//
//   ghosty::demogate::Sandbox box;            // FIRST thing in the test: HOME / TMPDIR / XDG_* -> a fresh folder
//   std::vector<ghosty::demogate::Path> paths { { "arp.lane.midi-drag", Drag, [&] { ...trigger it through the real UI... } }, ... };
//   int fails = ghosty::demogate::run ("ArpEZator", paths, kInventoryCount);
//
// For every path: LOCKED -> the gate refused that id, the hint showed, no external drag, no save dialog, no new audio /
// MIDI file anywhere in the sandbox, the clipboard unchanged. UNLOCKED -> the gate allowed that id and the path really
// exported (a drag of existing files, a saved file, or the clipboard changed - its `Kind`).
// The count of paths must equal the inventory (docs/DEMO-EXPORT-GATE.md) and every path must have been asked by its id.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../ExportGate.h"
#include <cstdlib>
#include <map>
#include <set>

#if ! GHOSTY_TEST_HOOKS
 #error "DemoGateHarness needs GHOSTY_TEST_HOOKS=1 (test targets only)"
#endif

namespace ghosty::demogate
{
enum Kind { Drag, File, Clipboard, Auto };   // Auto = a background write (no user action, so no hint): locked = nothing written
struct Path { const char* id; Kind kind; std::function<void()> trigger; };

inline void pump (int ms, std::function<bool()> done = {})
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (done && done()) return;
    }
}

struct Sandbox
{
    juce::File root;
    Sandbox()
    {
        root = juce::File ("/tmp").getChildFile ("ghosty-demogate-" + juce::String (juce::Time::currentTimeMillis()) + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        root.createDirectory();
        for (auto* d : { "home", "home/Documents", "home/Music", "home/.config", "tmp", "picked" }) root.getChildFile (d).createDirectory();
        setenv ("HOME", root.getChildFile ("home").getFullPathName().toRawUTF8(), 1);
        setenv ("TMPDIR", root.getChildFile ("tmp").getFullPathName().toRawUTF8(), 1);
        setenv ("XDG_CONFIG_HOME", root.getChildFile ("home/.config").getFullPathName().toRawUTF8(), 1);
        setenv ("XDG_DOCUMENTS_DIR", root.getChildFile ("home/Documents").getFullPathName().toRawUTF8(), 1);
        setenv ("XDG_MUSIC_DIR", root.getChildFile ("home/Music").getFullPathName().toRawUTF8(), 1);
        exportgate::log().saveDir = root.getChildFile ("picked");
    }
    ~Sandbox() { if (std::getenv ("GHOSTY_KEEP_SANDBOX") == nullptr) root.deleteRecursively(); }

    static bool isExportType (const juce::File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        for (auto* x : { ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a", ".caf", ".mid", ".midi", ".xm", ".it", ".mod", ".s3m", ".ghostywt" })   // .ghostywt = base64 float32 PCM (Ghosty Pro gates it)
            if (e == x) return true;
        return false;
    }
    std::map<juce::String, juce::int64> snapshot() const
    {
        std::map<juce::String, juce::int64> m;
        for (const auto& e : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
            m[e.getFile().getFullPathName()] = e.getFile().getSize() * 1000003 + e.getModificationTime().toMilliseconds();
        return m;
    }
    // audio / MIDI files that are new or changed since `before` (anything in picked/ counts, whatever its type)
    juce::StringArray exportsSince (const std::map<juce::String, juce::int64>& before) const
    {
        juce::StringArray out; const auto picked = root.getChildFile ("picked");
        for (const auto& [p, v] : snapshot())
        {
            const juce::File f (p);
            if (! (isExportType (f) || f.isAChildOf (picked))) continue;
            auto it = before.find (p);
            if (it == before.end() || it->second != v) out.add (p);
        }
        return out;
    }
};

inline int run (const char* plugin, const std::vector<Path>& paths, int inventoryCount, const Sandbox& box)
{
    int fails = 0;
    auto bad = [&] (const juce::String& m) { ++fails; std::printf ("  FAIL %s\n", m.toRawUTF8()); };
    std::printf ("demogate %s: %d export paths (inventory %d)\n", plugin, (int) paths.size(), inventoryCount);
    if ((int) paths.size() != inventoryCount) bad ("path count " + juce::String ((int) paths.size()) + " != inventory " + juce::String (inventoryCount));
    std::set<std::string> ids; for (auto& p : paths) if (! ids.insert (p.id).second) bad (juce::String ("duplicate id ") + p.id);
    auto& L = exportgate::log();
    for (const auto& p : paths)
    {
        // ---- LOCKED
        License::testForce().store (0);
        const juce::String clip0 = "demogate-clipboard-sentinel-" + juce::String (juce::Random::getSystemRandom().nextInt());
        juce::SystemClipboard::copyTextToClipboard (clip0);
        auto before = box.snapshot(); L.clear();
        p.trigger(); pump (60);
        juce::StringArray why;
        if (! exportgate::Log::has (L.refused, p.id)) why.add ("gate not asked");
        if (p.kind != Auto && ! exportgate::Log::has (L.hinted, p.id)) why.add ("no hint");
        if (! L.drags.empty()) why.add ("drag started");
        if (! L.saves.empty()) why.add ("save dialog opened");
        if (exportgate::Log::has (L.allowed, p.id)) why.add ("gate said yes");
        const auto leaked = box.exportsSince (before);
        if (! leaked.isEmpty()) why.add ("file written: " + leaked.joinIntoString (", "));
        if (juce::SystemClipboard::getTextFromClipboard() != clip0) why.add ("clipboard changed");
        const bool lockedOk = why.isEmpty();
        if (! lockedOk) bad (juce::String (p.id) + " LOCKED: " + why.joinIntoString ("; "));
        // ---- UNLOCKED
        License::testForce().store (1);
        juce::SystemClipboard::copyTextToClipboard (clip0);
        before = box.snapshot(); L.clear();
        p.trigger(); pump (60);
        juce::StringArray why2;
        if (! exportgate::Log::has (L.allowed, p.id)) why2.add ("gate not asked / refused");
        if (p.kind == Drag)
        {
            bool real = ! L.drags.empty();
            for (auto& d : L.drags) for (auto& f : d) real = real && juce::File (f).exists();   // a file, or a folder of them (a render set)
            if (! real) why2.add ("no drag of real files");
        }
        else if (p.kind == File || p.kind == Auto) { if (box.exportsSince (before).isEmpty()) why2.add ("no file saved"); }
        else if (juce::SystemClipboard::getTextFromClipboard() == clip0) why2.add ("clipboard not set");
        const bool unlockedOk = why2.isEmpty();
        if (! unlockedOk) bad (juce::String (p.id) + " UNLOCKED: " + why2.joinIntoString ("; "));
        std::printf ("  %-34s locked=%s unlocked=%s\n", p.id, lockedOk ? "refused" : "LEAK", unlockedOk ? "exports" : "BROKEN");
    }
    License::testForce().store (-1);
    std::printf ("demogate %s: %s (%d failure%s)\n", plugin, fails == 0 ? "PASS" : "FAIL", fails, fails == 1 ? "" : "s");
    return fails;
}
// The demo drop-out (License.h DemoGate: 2 s of silence every 20 s while locked). Renders 22 s at 48 kHz with `feed` filling
// each block's input / MIDI (an effect gets a tone, a synth gets notes) and reports where the output went silent.
struct Dropout { bool signal = false; double gapStart = -1, gapLen = 0; };
inline Dropout measureDropout (juce::AudioProcessor& p, std::function<void (juce::AudioBuffer<float>&, juce::MidiBuffer&, juce::int64 pos)> feed, double seconds = 22.0)
{
    const double sr = 48000; const int blk = 480;   // 10 ms blocks
    p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
    const int ch = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    std::vector<float> rms; Dropout d;
    for (juce::int64 pos = 0; pos < (juce::int64) (seconds * sr); pos += blk)
    {
        juce::AudioBuffer<float> b (ch, blk); b.clear(); juce::MidiBuffer m;
        if (feed) feed (b, m, pos);
        p.processBlock (b, m);
        float r = 0; for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c) r = juce::jmax (r, b.getRMSLevel (c, 0, blk));
        rms.push_back (r);
    }
    p.releaseResources();
    std::vector<float> sorted (rms); std::sort (sorted.begin(), sorted.end()); const float med = sorted[sorted.size() / 2];
    d.signal = med > 1.0e-4f;
    if (! d.signal) return d;
    int run = 0, best = 0, bestEnd = -1;   // the longest run of blocks below -60 dB of the median, after the first second
    for (int i = 100; i < (int) rms.size(); ++i) { if (rms[(size_t) i] < med * 0.001f) { if (++run > best) { best = run; bestEnd = i; } } else run = 0; }
    if (best >= 50) { d.gapLen = best * 0.01; d.gapStart = (bestEnd - best + 1) * 0.01; }
    return d;
}
// locked: one ~2 s gap starting at 18 s; unlocked: none. Returns the failures (printed).
inline int checkDropout (const char* plugin, std::function<std::unique_ptr<juce::AudioProcessor>()> make,
                         std::function<void (juce::AudioBuffer<float>&, juce::MidiBuffer&, juce::int64)> feed)
{
    License::testForce().store (0); auto pl = make(); const auto L = measureDropout (*pl, feed); pl.reset();
    License::testForce().store (1); auto pu = make(); const auto U = measureDropout (*pu, feed); pu.reset();
    License::testForce().store (-1);
    std::printf ("dropout %s: locked gap %.2f s at %.2f s, unlocked gap %.2f s (signal %s)\n", plugin, L.gapLen, L.gapStart, U.gapLen, L.signal && U.signal ? "yes" : "NO");
    const bool ok = L.signal && U.signal && L.gapLen > 1.7 && L.gapLen < 2.3 && std::abs (L.gapStart - 18.0) < 0.3 && U.gapLen == 0;
    std::printf ("dropout %s: %s\n", plugin, ok ? "PASS (2 s every 20 s while locked, none when unlocked)" : "FAIL");
    return ok ? 0 : 1;
}
} // namespace ghosty::demogate
