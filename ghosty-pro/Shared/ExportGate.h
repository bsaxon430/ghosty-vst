#pragma once
// ExportGate: the ONE demo-mode export gate of every GhostBlock plugin (docs/DEMO-EXPORT-GATE.md has the inventory).
//
// Demo mode (no licence key) never lets audio or MIDI leave a plugin: no drag-out of WAV / stems / loops / slices /
// renders / MIDI, no saved or exported audio / MIDI file, no MIDI on the clipboard. Every such path calls
//
//     if (! ghosty::exportAllowed (license, uiComponent, "plugin.path-id")) return;
//
// BEFORE it writes a file, opens a save dialog, starts a drag or sets the clipboard. Refused: the UNLOCK button blinks and
// a small hint bubble says why (non-modal, it fades by itself); nothing is written and no drag starts.
// Silent checks (paint, timers, worker threads, processor back-stops) use ghosty::exportUnlocked (license).
// The plugin's own formats (presets, scenes, boards, snapshots, project state) are not exports and stay allowed.
//
// The only raw external drag and the only export save dialog of the collection live here (exportgate::dragFiles,
// exportgate::saveDialog): the source lint (Shared/tests/ExportGateLint.cpp) fails the build's tests when any other file
// calls performExternalDragDropOfFiles / opens a saveMode FileChooser / writes WAV-MIDI without a gate annotation.
//
// Tests (GHOSTY_TEST_HOOKS=1, set on the test targets only, never on a plugin): License::testForce() forces locked /
// unlocked, exportgate::log() records every gate answer, hint, drag and save, and drags / save dialogs never touch the OS.
#include "License.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace ghosty
{
inline bool exportUnlocked (const License& lic) { return lic.isUnlocked(); }

namespace exportgate
{
#if GHOSTY_TEST_HOOKS
struct Log
{
    std::vector<std::string> refused, allowed, hinted;   // gate answers (ids) and the hints actually shown
    std::vector<juce::StringArray> drags;                // external drags that would have started
    std::vector<juce::File> saves;                       // save dialogs that would have opened (the test picks the file)
    std::vector<std::string> stripped;                   // locked saves that left plugin-made audio out (ids)
    juce::File saveDir;                                  // where a test "picks" the save dialog's file (empty = cancel)
    std::function<void (const juce::StringArray&)> onDrag;
    void clear() { refused.clear(); allowed.clear(); hinted.clear(); drags.clear(); saves.clear(); stripped.clear(); }
    static bool has (const std::vector<std::string>& v, const char* id) { return std::find (v.begin(), v.end(), std::string (id)) != v.end(); }
};
inline Log& log() { static Log l; return l; }
#endif

// The hint: a small rounded note near the refused control, top of the editor, gone after a few seconds (or a click).
struct Hint : juce::Component, private juce::Timer
{
    juce::String text; float alpha = 1.0f; int ticks = 0;
    explicit Hint (juce::String t) : text (std::move (t)) { setInterceptsMouseClicks (true, false); setAlwaysOnTop (true); startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xf0231b30)); g.fillRoundedRectangle (r, 7.0f);
        g.setColour (juce::Colour (0xffffc94d)); g.drawRoundedRectangle (r, 7.0f, 1.5f);
        g.setColour (juce::Colour (0xffeee9f8)); g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (text, getLocalBounds().reduced (10, 4), juce::Justification::centred, 3, 0.8f);
    }
    void mouseDown (const juce::MouseEvent&) override { ticks = 1000; }
    // the editor closes (or drops its children): the hint goes with it at once, so no timer outlives the plugin
    void parentHierarchyChanged() override { if (getParentComponent() == nullptr && placed) { stopTimer(); delete this; } else if (getParentComponent() != nullptr) placed = true; }
    bool placed = false;
    void timerCallback() override
    {
        if (++ticks > 120) { alpha -= 0.08f; setAlpha (juce::jmax (0.0f, alpha)); }
        if (alpha <= 0.0f) { stopTimer(); auto* self = this; juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Hint> (self)] { delete sp.getComponent(); }); }
    }
};

inline UnlockButton* findUnlock (juce::Component* c)
{
    if (c == nullptr) return nullptr;
    if (auto* u = dynamic_cast<UnlockButton*> (c)) return u;
    for (auto* ch : c->getChildren()) if (auto* u = findUnlock (ch)) return u;
    return nullptr;
}

inline juce::String hintText() { return "Export is part of the full version.\nClick UNLOCK and enter your license key (or get one at " + License::storeUrl() + ")."; }

// shows the hint for a refused export (message thread; from another thread it is posted)
inline void showHint (juce::Component* ui, const char* id)
{
    if (ui == nullptr) return;
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::Component::SafePointer<juce::Component> sp (ui); const std::string sid (id);
        juce::MessageManager::callAsync ([sp, sid] { if (sp != nullptr) showHint (sp.getComponent(), sid.c_str()); });
        return;
    }
    auto* top = ui->getTopLevelComponent(); if (top == nullptr) top = ui;
    auto* unlock = findUnlock (top);
    if (unlock != nullptr) unlock->nudge();
    for (auto* ch : top->getChildren()) if (auto* old = dynamic_cast<Hint*> (ch)) { old->placed = false; delete old; break; }   // one hint at a time
    auto* h = new Hint (hintText());
    const int w = juce::jmin (420, juce::jmax (200, top->getWidth() - 20)), hgt = 50;
    auto anchor = top->getLocalArea (unlock != nullptr && unlock->isVisible() ? (juce::Component*) unlock : ui,
                                     (unlock != nullptr && unlock->isVisible() ? (juce::Component*) unlock : ui)->getLocalBounds());
    int x = anchor.getCentreX() - w / 2, y = anchor.getBottom() + 6;
    if (y + hgt > top->getHeight()) y = anchor.getY() - hgt - 6;
    h->setBounds (juce::jlimit (4, juce::jmax (4, top->getWidth() - w - 4), x), juce::jlimit (4, juce::jmax (4, top->getHeight() - hgt - 4), y), w, hgt);
    top->addAndMakeVisible (h);
   #if GHOSTY_TEST_HOOKS
    log().hinted.push_back (id);
   #endif
}

// The external drag (files out to the DAW / Finder). Call it only after exportAllowed() said yes.
inline bool dragFiles (const juce::StringArray& files, juce::Component* src)
{
    if (files.isEmpty()) return false;
   #if GHOSTY_TEST_HOOKS
    log().drags.push_back (files); if (log().onDrag) log().onDrag (files); juce::ignoreUnused (src); return true;
   #else
    return juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, src);
   #endif
}
// the same with the licence as a back-stop (a path that was gated earlier, e.g. before a render, re-checks silently)
inline bool dragFiles (const License& lic, const juce::StringArray& files, juce::Component* src)
{
    return exportUnlocked (lic) && dragFiles (files, src);
}

// The save dialog for an exported audio / MIDI file. Call it only after exportAllowed() said yes.
// holder keeps the chooser alive; picked runs with the chosen file (never with an empty one).
inline void saveDialog (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title, const juce::File& initial, const juce::String& patterns,
                        std::function<void (const juce::File&)> picked, bool canSelectFiles = false)
{
   #if GHOSTY_TEST_HOOKS
    juce::ignoreUnused (holder, title, canSelectFiles);
    log().saves.push_back (initial);
    if (log().saveDir != juce::File()) { log().saveDir.createDirectory(); picked (log().saveDir.getChildFile (initial.getFileName().isNotEmpty() ? initial.getFileName() : juce::String ("export"))); }
   #else
    holder = std::make_unique<juce::FileChooser> (title, initial, patterns);
    holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | (canSelectFiles ? juce::FileBrowserComponent::canSelectFiles : 0),
                         [picked] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f != juce::File()) picked (f); });
   #endif
}
// Saves of the plugin's own formats (presets, boards, user sounds, project state) stay allowed in the demo, but while locked
// they must not carry audio the PLUGIN made (resample / process / render / generated wavetable results): only the user's
// own imported samples. Call keepRenderedAudio() where a save would embed such audio; false = leave it out. When it was
// left out and the save came from a click (ui != nullptr), the same UNLOCK hint explains why.
inline bool keepRenderedAudio (const License& lic) { return lic.isUnlocked(); }
inline void renderedAudioLeftOut (juce::Component* ui, const char* id)
{
   #if GHOSTY_TEST_HOOKS
    log().stripped.push_back (id);
   #endif
    showHint (ui, id);
}
} // namespace exportgate

// THE gate. true = unlocked (export away). false = demo: the hint shows on ui's editor, the caller must stop at once
// (no file written, no dialog, no drag, no clipboard). extraLock = a plugin's own test switch (locks too).
inline bool exportAllowed (const License& lic, juce::Component* ui, const char* id, bool extraLock = false)
{
    const bool ok = ! extraLock && lic.isUnlocked();
   #if GHOSTY_TEST_HOOKS
    (ok ? exportgate::log().allowed : exportgate::log().refused).push_back (id);
   #endif
    if (! ok) exportgate::showHint (ui, id);
    return ok;
}
} // namespace ghosty
