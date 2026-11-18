#pragma once
// MY BANKS: the page <-> library bridge for the web-page drum plugins (Block Step, Ghost House). See UserBanks.h.
// The page sends:  bs_banks_get · bs_bank_pick {into, folders} (the Mac file chooser) · bs_bank_drop {gid, folder, into, name, b64}
//                  (sounds dropped on the page, one file per event) · bs_bank_new {name} · bs_bank_rename {id, name} · bs_bank_del {id}
//                  · bs_snd_type {u, c} · bs_snd_del {u} · bs_bank_warm {u: [uid...]} (decode a bank's sounds now)
// and gets back:   bs_banks {lib, dir, msg, add: {sounds, banks, skipped, bank}}  whenever the library changes (here or in another plugin).
#include <juce_gui_extra/juce_gui_extra.h>
#include "UserBanks.h"
#include <map>

namespace ghosty::userbanks {

template <class Store>
class Bridge {
public:
    using Emit = std::function<void(const juce::String&, juce::var)>;
    using Builtin = std::function<bool(const juce::String& fileName, juce::MemoryBlock&)>;   // a built-in sound's file (for COPY)
    Bridge(Store& s, Emit e, Builtin b) : store(s), emit(std::move(e)), builtin(std::move(b)) { fm.registerBasicFormats(); }

    juce::WebBrowserComponent::Options listen(juce::WebBrowserComponent::Options o) {
        return o.withEventListener("bs_banks_get", [this](juce::var) { send(); })
                .withEventListener("bs_bank_pick", [this](juce::var v) { pick(v["into"].toString(), (bool) v["folders"]); })
                .withEventListener("bs_bank_drop", [this](juce::var v) { drop(v); })
                .withEventListener("bs_bank_copy", [this](juce::var v) { copy(v); })
                .withEventListener("bs_bank_new", [this](juce::var v) { edit([&](juce::var& L) { last = newBank(L, v["name"].toString()); return true; }, "New bank: drop sounds on it"); })
                .withEventListener("bs_bank_rename", [this](juce::var v) { edit([&](juce::var& L) { return renameBank(L, v["id"].toString(), v["name"].toString()); }, {}); })
                .withEventListener("bs_bank_del", [this](juce::var v) { edit([&](juce::var& L) { return deleteBank(L, v["id"].toString()); }, "Bank deleted"); })
                .withEventListener("bs_snd_type", [this](juce::var v) { edit([&](juce::var& L) { return setType(L, (int) v["u"], v["c"].toString()); }, {}); })
                .withEventListener("bs_snd_del", [this](juce::var v) { edit([&](juce::var& L) { return deleteSound(L, (int) v["u"]); }, "Sound removed"); })
                .withEventListener("bs_bank_warm", [this](juce::var v) { if (auto* a = v["u"].getArray()) for (auto& u : *a) store.request(0, kBase + (int) u); });
    }
    // ~30 times a second from the editor's timer: sends after a burst of changes settles, and notices other plugins' changes
    void tick() {
        if (pending >= 0) { if (--pending < 0) { send(msg, &added); msg = {}; added = {}; } return; }
        if (++poll % 20 == 0 && stamp() != sent) send();
    }
    void send(const juce::String& m = {}, const Added* a = nullptr) {
        auto lib = load(); store.loadUserLibrary(); sent = stamp();
        auto* o = new juce::DynamicObject(); o->setProperty("lib", lib); o->setProperty("dir", dir().getFullPathName()); o->setProperty("msg", m); o->setProperty("bank", last);
        if (a != nullptr) { auto* x = new juce::DynamicObject(); x->setProperty("sounds", a->sounds); x->setProperty("banks", a->banks); x->setProperty("skipped", a->skipped); x->setProperty("why", a->why); o->setProperty("add", juce::var(x)); }
        emit("bs_banks", juce::var(o));
    }

private:
    template <class Op> void edit(Op op, const juce::String& m) { auto lib = load(); if (op(lib) && save(lib)) changed(m); else changed("Couldn't change MY BANKS (is Music/GhostBlock Banks writable?)"); }
    void changed(const juce::String& m) { if (m.isNotEmpty()) msg = m; pending = 4; }
    void merge(const Added& a) { added.sounds += a.sounds; added.banks += a.banks; added.skipped += a.skipped; if (a.why.isNotEmpty()) added.why = a.why; if (a.lastBank.isNotEmpty()) last = a.lastBank; }
    void pick(const juce::String& into, bool folders) {
        chooser = std::make_unique<juce::FileChooser>(folders ? "Pick drum folders: each folder becomes a bank" : "Pick drum sounds for your bank",
                                                      juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        const int fl = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems | (folders ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles);
        chooser->launchAsync(fl, [this, into](const juce::FileChooser& fc) {
            const auto items = fc.getResults(); if (items.isEmpty()) return;
            auto lib = load(); const auto a = addPaths(lib, items, into, fm);
            if (! save(lib)) { changed("Couldn't save MY BANKS (is Music/GhostBlock Banks writable?)"); return; }
            merge(a); changed(a.sounds ? "" : "No sounds found there (WAV, AIFF, FLAC, MP3, OGG)");
        });
    }
    // COPY: sounds (yours {u} or built-in {f, n, c}) into one of your banks, or into a new bank called 'name'
    void copy(const juce::var& v) {
        auto lib = load(); juce::String id = v["into"].toString(); Added a;
        if (id.isEmpty() || bankById(lib, id) == nullptr) { id = newBank(lib, v["name"].toString()); a.banks = 1; }
        if (auto* items = v["items"].getArray()) for (auto& it : *items) {
            if (it.hasProperty("u")) copySound(lib, id, (int) it["u"], a);
            else { juce::MemoryBlock mb; const auto f = it["f"].toString(); if (builtin && f.isNotEmpty() && builtin(f, mb)) addBytes(lib, id, it["n"].toString(), f.fromLastOccurrenceOf(".", false, false), mb, it["c"].toString(), a); else ++a.skipped; }
        }
        a.lastBank = id;
        if (! save(lib)) { changed("Couldn't save MY BANKS (is Music/GhostBlock Banks writable?)"); return; }
        merge(a); juce::String bn; if (auto* b = bankById(lib, id)) bn = (*b)["name"].toString();
        changed("Copied " + juce::String(a.sounds) + " sound" + (a.sounds == 1 ? "" : "s") + " into " + bn + (a.skipped ? " (" + juce::String(a.skipped) + " couldn't be copied)" : juce::String()));
        added = {};   // (the message says it all)
    }
    // one dropped file (the page reads it and sends the bytes): into the open bank, else a bank per folder, else one new bank per drop
    void drop(const juce::var& v) {
        juce::MemoryOutputStream mo; if (! juce::Base64::convertFromBase64(mo, v["b64"].toString())) return;
        const juce::String name = cleanName(v["name"].toString(), 120), folder = v["folder"].toString().trim().isEmpty() ? juce::String() : cleanName(v["folder"].toString(), 60);
        if (! isAudio(name)) return;
        auto tdir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostBlockDrop").getChildFile(juce::String(++dropN));
        tdir.createDirectory(); const auto f = tdir.getChildFile(name); if (! f.replaceWithData(mo.getData(), mo.getDataSize())) return;
        auto lib = load(); juce::String id = v["into"].toString();
        if (id.isEmpty() || bankById(lib, id) == nullptr) {
            const auto key = v["gid"].toString() + "\n" + folder; auto it = dropBanks.find(key);
            if (it != dropBanks.end() && bankById(lib, it->second) != nullptr) id = it->second;
            else { bool made = false; id = findOrNewBank(lib, folder.isNotEmpty() ? folder : juce::String(), made); if (folder.isEmpty() && ! made) { id = newBank(lib, {}); made = true; } dropBanks[key] = id; if (made) { Added nb; nb.banks = 1; merge(nb); } }   // a folder named like one of your banks adds to it
        }
        Added a; addFile(lib, id, f, folder, fm, a); tdir.deleteRecursively();
        if (save(lib)) { merge(a); changed({}); }
    }

    Store& store; Emit emit; Builtin builtin; juce::AudioFormatManager fm; std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, juce::String> dropBanks; juce::int64 sent = -1; int poll = 0, pending = -1, dropN = 0; juce::String msg, last; Added added;
};

} // namespace ghosty::userbanks
