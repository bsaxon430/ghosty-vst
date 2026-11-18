#pragma once
// License keys for the paid Ghosty plugins, checked with Gumroad's license API
// (POST https://api.gumroad.com/v2/licenses/verify with product_id + license_key).
//
// - A key from the plugin's own product or from the bundle product unlocks it.
// - The first unlock needs internet (and counts one "use" on Gumroad per COMPUTER and key, max kMaxActivations computers; a bundle key
//   unlocks every plugin on that computer for that single use, and a refused attempt never counts).
//   After that it works offline. It re-checks quietly every 14 days and only locks again if Gumroad
//   says the purchase was refunded or charged back (never because the internet is down).
// - Locked (demo) mode: everything works, but the sound drops out for a moment every 20 seconds, a reopened project starts at the default sound, and
//   exporting (drag-out WAV / stems / MIDI) is off.
// Product IDs come from CMake (GHOSTY_GUMROAD_PRODUCT / GHOSTY_GUMROAD_BUNDLE). If they're empty the
// plugin builds as fully unlocked, so a build without a store set up is never crippled.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>

#ifndef GHOSTY_GUMROAD_PRODUCT
 #define GHOSTY_GUMROAD_PRODUCT ""
#endif
#ifndef GHOSTY_GUMROAD_BUNDLE
 #define GHOSTY_GUMROAD_BUNDLE ""
#endif
#ifndef GHOSTY_STORE_URL
 #define GHOSTY_STORE_URL "https://bsaxon430.github.io/ghosty-vst/store/"
#endif

namespace ghosty {

class License : private juce::Timer {
public:
    static constexpr int kMaxActivations = 5;

    explicit License(const juce::String& productName) : product(productName) {
        if (! required()) { unlocked.store(true); return; }
        load();
        // quiet re-check every 14 days (in the background, a while after startup)
        if (unlocked.load() && juce::Time::currentTimeMillis() - lastCheckMs > (juce::int64) 14 * 24 * 3600 * 1000) startTimer(20000);
    }
    ~License() override { stopTimer(); alive->store(false); }

    // a plugin whose own product ID is still empty (not on the store yet) is unlocked by the bundle key alone
    static bool required() { return juce::String(GHOSTY_GUMROAD_PRODUCT).isNotEmpty() || juce::String(GHOSTY_GUMROAD_BUNDLE).isNotEmpty(); }
    // where the per-plugin unlock files live (one <plugin>.license per plugin)
    static juce::File licenseDir() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("GhostBlock"); }
    // the fingerprint an unlock file carries: plugin + key + this computer
    static juce::String sigFor(const juce::String& productName, const juce::String& k) {
        return juce::String::toHexString(juce::String("ghostblock|" + productName + "|" + k + "|" + machine()).hashCode64()); }
    // has any GhostBlock plugin on THIS computer already unlocked with this key? Then this computer already paid its one "use"
    static bool machineAlreadyUsedKey(const juce::File& dir, const juce::String& k) {
        for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.license")) {
            auto v = juce::JSON::parse(f.loadFileAsString()); if (! v.isObject()) continue;
            if (v["key"].toString() != k) continue;
            if (v["sig"].toString() == sigFor(f.getFileNameWithoutExtension(), k)) return true;   // file name = plugin name
        }
        return false;
    }
    bool isUnlocked() const {
       #if GHOSTY_TEST_HOOKS   // test builds only (never in a plugin): force LOCKED (0) / UNLOCKED (1) for every License in the process
        if (const int t = testForce().load(); t >= 0) return t == 1;
       #endif
        return unlocked.load(); }
   #if GHOSTY_TEST_HOOKS
    static std::atomic<int>& testForce() { static std::atomic<int> v { -1 }; return v; }   // -1 = the real licence
   #endif
    juce::String keyHint() const { return key.length() > 8 ? key.substring(0, 4) + "..." + key.getLastCharacters(4) : key; }
    static juce::String storeUrl() { return GHOSTY_STORE_URL; }
    // Exporting (drag-out WAV / stems / MIDI, saving audio or MIDI files) is part of the full version: every export path
    // asks ghosty::exportAllowed() in ExportGate.h (the one gate; it shows the UNLOCK hint when refused).

    // message thread: try a key; calls done(ok, message) on the message thread
    void activate(const juce::String& rawKey, std::function<void(bool, juce::String)> done) {
        const auto k = rawKey.trim();
        if (k.length() < 8) { done(false, "That doesn't look like a license key. Copy it from your Gumroad receipt email."); return; }
        auto flag = alive; auto prods = productIds();
        juce::Thread::launch([this, k, prods, flag, done] {
            juce::String msg; bool ok = false; bool reached = false;
            const bool known = machineAlreadyUsedKey(licenseDir(), k);   // another GhostBlock plugin here already unlocked with this key
            for (auto& pid : prods) {
                auto peek = verify(pid, k, false);   // look first without counting, so a refused attempt never burns a use
                if (! peek.isObject()) continue;
                reached = true;
                if (! (bool) peek["success"]) continue;
                if (revoked(peek)) { msg = "This purchase was refunded or charged back, so the key no longer works."; break; }
                if (! known) {   // a new computer for this key: it costs one use (and then every plugin here unlocks with it)
                    const int usedBefore = (int) peek["uses"];
                    if (usedBefore >= kMaxActivations) { msg = "This key is already used on " + juce::String(usedBefore) + " computers (limit " + juce::String(kMaxActivations) + "). Email support to reset it."; break; }
                    auto counted = verify(pid, k, true);
                    if (! counted.isObject() || ! (bool) counted["success"]) { msg = "Couldn't reach Gumroad. Check your internet connection and try again."; break; }
                }
                ok = true; break;
            }
            if (! ok && msg.isEmpty()) msg = reached ? "Gumroad didn't recognise that key for " + product + ". Check for typos, or that it's the right product."
                                                     : "Couldn't reach Gumroad. Check your internet connection and try again.";
            juce::MessageManager::callAsync([this, ok, msg, k, flag, done] {
                if (! flag->load()) return;
                if (ok) { key = k; unlocked.store(true); lastCheckMs = juce::Time::currentTimeMillis(); save(); }
                done(ok, ok ? juce::String("Unlocked. Thanks for supporting GhostBlock!") : msg);
            });
        });
    }

private:
    juce::String product, key; std::atomic<bool> unlocked { false }; juce::int64 lastCheckMs = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    static juce::StringArray productIds() {
        juce::StringArray a; if (juce::String(GHOSTY_GUMROAD_PRODUCT).isNotEmpty()) a.add(GHOSTY_GUMROAD_PRODUCT); if (juce::String(GHOSTY_GUMROAD_BUNDLE).isNotEmpty()) a.add(GHOSTY_GUMROAD_BUNDLE); return a; }
    static bool revoked(const juce::var& r) { auto p = r["purchase"]; return (bool) p["refunded"] || (bool) p["chargebacked"] || (bool) p["disputed"]; }
    static juce::var verify(const juce::String& productId, const juce::String& k, bool countUse) {
        juce::URL url("https://api.gumroad.com/v2/licenses/verify");
        url = url.withParameter("product_id", productId).withParameter("license_key", k).withParameter("increment_uses_count", countUse ? "true" : "false");
        int status = 0;
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData).withConnectionTimeoutMs(8000).withStatusCode(&status);
        std::unique_ptr<juce::InputStream> in(url.createInputStream(opts));
        if (! in) return {};
        return juce::JSON::parse(in->readEntireStreamAsString());   // a wrong key comes back as 404 with success:false, which is still an answer
    }

    // the saved unlock: the key plus a fingerprint of this computer, so copying the file elsewhere doesn't carry it
    juce::File file() const { return licenseDir().getChildFile(product + ".license"); }
    static juce::String machine() {   // this computer's ID (same across launches; changes only if the OS reinstalls)
        auto ids = juce::SystemStats::getMachineIdentifiers(juce::SystemStats::MachineIdFlags::uniqueId);
        const auto basis = ids.isEmpty() ? juce::SystemStats::getComputerName() : ids.joinIntoString("|");
        return juce::String::toHexString(basis.hashCode64()); }
    juce::String sig(const juce::String& k) const { return sigFor(product, k); }
    void save() const {
        juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty("key", key); o->setProperty("sig", sig(key)); o->setProperty("checked", juce::String(lastCheckMs));
        file().getParentDirectory().createDirectory(); file().replaceWithText(juce::JSON::toString(juce::var(o.get())));
    }
    void load() {
        auto v = juce::JSON::parse(file().loadFileAsString()); if (! v.isObject()) return;
        const juce::String k = v["key"].toString();
        if (k.isNotEmpty() && v["sig"].toString() == sig(k)) { key = k; unlocked.store(true); lastCheckMs = v["checked"].toString().getLargeIntValue(); }
    }
    void timerCallback() override {
        stopTimer(); auto flag = alive; auto prods = productIds(); const auto k = key;
        juce::Thread::launch([this, prods, k, flag] {
            bool revokedNow = false, confirmed = false;
            for (auto& pid : prods) { auto r = verify(pid, k, false); if (! r.isObject()) continue;
                if ((bool) r["success"]) { if (revoked(r)) revokedNow = true; else confirmed = true; break; } }
            juce::MessageManager::callAsync([this, revokedNow, confirmed, flag] {
                if (! flag->load()) return;
                if (revokedNow) { unlocked.store(false); file().deleteFile(); }
                else if (confirmed) { lastCheckMs = juce::Time::currentTimeMillis(); save(); }
            });
        });
    }
};

// Demo-mode audio: a short fade-out / silence every 20 s while the plugin is locked (audio thread, no allocation)
struct DemoGate {
    double sr = 44100; juce::int64 pos = 0;
    void prepare(double rate) { sr = rate; pos = 0; }
    // MIDI out (a plugin that sends notes to the DAW): true = this block offset falls in the drop-out, so a note-on there is
    // not sent (note-offs always are, nothing hangs). Same clock as process(): call it for the block BEFORE process().
    bool midiMuted(int offset, bool unlocked) const {
        if (unlocked) return false;
        const juce::int64 period = (juce::int64) (sr * 20.0), gap = (juce::int64) (sr * 2.0);
        return period > 0 && (pos + offset) % period >= period - gap;
    }
    void process(juce::AudioBuffer<float>& b, bool unlocked) {
        if (unlocked) return;
        const juce::int64 period = (juce::int64) (sr * 20.0), gap = (juce::int64) (sr * 2.0), fade = (juce::int64) (sr * 0.03);
        for (int i = 0; i < b.getNumSamples(); ++i, ++pos) {
            const juce::int64 ph = pos % period; float g = 1;
            if (ph >= period - gap) { const juce::int64 t = ph - (period - gap); g = t < fade ? 1.0f - (float) t / (float) fade : (gap - t < fade ? 1.0f - (float) (gap - t) / (float) fade : 0.0f); }
            if (g < 1) for (int c = 0; c < b.getNumChannels(); ++c) b.getWritePointer(c)[i] *= g;
        }
    }
};

// The UNLOCK button for an editor header: shows "UNLOCK" (or "LICENSED") and opens the key box
struct UnlockButton : juce::TextButton, private juce::Timer {
    License& lic; std::function<void()> onChange;
    explicit UnlockButton(License& l) : lic(l) { refresh(); onClick = [this] { open(); }; }
    ~UnlockButton() override { stopTimer(); }
    // a refused export points here: the button blinks for a few seconds (non-modal; text, size and place unchanged)
    void nudge() {
        if (lic.isUnlocked()) return;
        ++nudged; nudges = 7; setAlpha(0.35f); startTimer(260);
    }
    int nudgeCount() const { return nudged; }
    void refresh() {
        setButtonText(lic.isUnlocked() ? "LICENSED" : "UNLOCK"); setVisible(License::required());
        setTooltip(lic.isUnlocked() ? "Licensed with key " + lic.keyHint() : "Demo mode: the sound drops out for 2 s every 20 s, projects reopen at the default sound, and export is off. Enter your license key to unlock.");
    }
    void timerCallback() override {
        if (--nudges <= 0) { stopTimer(); setAlpha(1.0f); return; }
        setAlpha((nudges & 1) ? 0.35f : 1.0f);
    }
    int nudges = 0, nudged = 0;
public:
    void open() {
        if (lic.isUnlocked()) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Licensed", "This copy is unlocked (key " + lic.keyHint() + "). Thanks!"); return; }
        auto* w = new juce::AlertWindow("Unlock", "Paste the license key from your Gumroad receipt email.\nDon't have one yet? Choose \"Buy\".", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("key", "", "License key");
        w->addButton("Unlock", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Buy", 2); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<UnlockButton> safe(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int r) {
            if (! safe) return;
            if (r == 2) { juce::URL(License::storeUrl()).launchInDefaultBrowser(); return; }
            if (r != 1) return;
            const auto k = w->getTextEditorContents("key");
            safe->setButtonText("CHECKING..."); safe->setEnabled(false);
            safe->lic.activate(k, [safe](bool ok, juce::String msg) {
                if (! safe) return; safe->setEnabled(true); safe->refresh(); if (safe->onChange) safe->onChange();
                juce::AlertWindow::showMessageBoxAsync(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon, ok ? "Unlocked" : "Not unlocked", msg);
            });
        }), true);
    }
};

} // namespace ghosty
