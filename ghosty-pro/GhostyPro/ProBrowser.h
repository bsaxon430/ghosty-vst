#pragma once
// PRESET BROWSER (round 3 item 6): the BROWSE page's model, apart from any drawing so ProTests can drive it.
// Entries (factory, your sounds, snapshots) -> one filter (category, typed search, tag chips) -> one sort -> the rows shown.
// FAVOURITES and RECENTLY USED belong to the person, never to a sound: favourites.json / recent.json next to the saved sounds.
// With no filter set (category All, empty search, no chip) and the default sort the rows are the entries in their bank order:
// exactly the list the page showed before.
#include <juce_core/juce_core.h>
#include <set>
#include <vector>
#include "ProBank.h"

namespace gpro::browse
{
struct Entry
{
    juce::String name, cat, scene, author;
    juce::StringArray tags;              // derived + category tags (ProBank.h)
    int program = -1;                    // >= 0 factory, -1 your sound (file), -2 snapshot (file)
    bool legacy = false;                 // SOUND ENGINE V2: a LEGACY factory sound (category "Legacy", hidden unless showLegacy)
    juce::File file;
    juce::String key() const { return (program >= 0 ? "f:" : program == -2 ? "s:" + file.getFileName() + ":" : "u:") + name; }
};

// a list of keys in a JSON file: { "version": 1, "<field>": [ ... ] }
struct KeyStore
{
    juce::File file; juce::String field; std::vector<juce::String> keys;
    void load()
    {
        keys.clear();
        const auto v = juce::JSON::parse (file.loadFileAsString());
        if (auto* a = v.getProperty (field, juce::var()).getArray()) for (auto& k : *a) if (k.toString().isNotEmpty()) keys.push_back (k.toString());
    }
    bool save() const
    {
        juce::Array<juce::var> a; for (auto& k : keys) a.add (k);
        auto* o = new juce::DynamicObject(); o->setProperty ("version", 1); o->setProperty (field, a);
        file.getParentDirectory().createDirectory();
        return file.replaceWithText (juce::JSON::toString (juce::var (o)));
    }
    bool contains (const juce::String& k) const { return std::find (keys.begin(), keys.end(), k) != keys.end(); }
    int rank (const juce::String& k) const { auto it = std::find (keys.begin(), keys.end(), k); return it == keys.end() ? -1 : (int) (it - keys.begin()); }
};

struct Favourites : KeyStore
{
    // the first build kept them in favourites.txt (one key per line): read once when there is no JSON yet
    void open (const juce::File& json, const juce::File& legacyTxt = {})
    {
        file = json; field = "favourites"; load();
        if (! json.existsAsFile() && legacyTxt.existsAsFile())
        {
            juce::StringArray l; l.addLines (legacyTxt.loadFileAsString());
            for (auto& s : l) if (s.trim().isNotEmpty() && ! contains (s.trim())) keys.push_back (s.trim());
            if (! keys.empty()) save();
        }
    }
    bool is (const juce::String& k) const { return contains (k); }
    void toggle (const juce::String& k) { auto it = std::find (keys.begin(), keys.end(), k); if (it != keys.end()) keys.erase (it); else keys.push_back (k); save(); }
};

struct Recents : KeyStore
{
    static constexpr int maxKeep = 64;
    void open (const juce::File& json) { file = json; field = "recent"; load(); }
    void touch (const juce::String& k)       // newest first
    {
        auto it = std::find (keys.begin(), keys.end(), k); if (it != keys.end()) keys.erase (it);
        keys.insert (keys.begin(), k);
        if ((int) keys.size() > maxKeep) keys.resize ((size_t) maxKeep);
        save();
    }
};

enum Sort { sortBank = 0, sortName, sortCategory, sortRecent, numSorts };
inline const juce::StringArray& sortNames() { static const juce::StringArray s { "BANK ORDER", "NAME", "CATEGORY", "RECENT" }; return s; }

struct Model
{
    std::vector<Entry> entries;
    std::vector<int> shown;              // indices into entries, in display order
    juce::String category { "All" }, query;
    std::set<juce::String> tags;         // chips that are on: a row needs every one of them
    int sort = sortBank;
    bool showLegacy = false;             // SHOW LEGACY (TAGS panel): the LEGACY factory sounds join the list
    Favourites favs; Recents recent;
    bool visible (const Entry& e) const { return showLegacy || ! e.legacy; }

    bool filterActive() const { return category != "All" || query.trim().isNotEmpty() || ! tags.empty(); }
    bool isFav (const Entry& e) const { return favs.is (e.key()); }

    static bool hasTag (const Entry& e, const juce::String& t) { return e.tags.contains (t); }
    bool matchesQuery (const Entry& e) const
    {
        const auto q = query.trim().toLowerCase();
        if (q.isEmpty()) return true;
        auto in = [&e] (const juce::String& w)
        {
            if (e.name.toLowerCase().contains (w) || e.cat.toLowerCase().contains (w) || e.scene.toLowerCase().contains (w) || e.author.toLowerCase().contains (w)) return true;
            for (auto& t : e.tags) if (t.contains (w)) return true;
            return false;
        };
        if (in (q)) return true;                       // the whole text (what the page searched before, plus tags + author)
        juce::StringArray words; words.addTokens (q, " ", ""); words.removeEmptyStrings();
        if (words.size() < 2) return false;
        for (auto& w : words) if (! in (w)) return false;   // or every word somewhere ("fm bell", "trap mono")
        return true;
    }
    bool matches (const Entry& e) const
    {
        if (! visible (e)) return false;
        if (category == "Favourites" ? ! isFav (e) : (category != "All" && e.cat != category)) return false;
        for (auto& t : tags) if (! hasTag (e, t)) return false;
        return matchesQuery (e);
    }
    void refilter()
    {
        shown.clear();
        for (int i = 0; i < (int) entries.size(); ++i) if (matches (entries[(size_t) i])) shown.push_back (i);
        if (sort == sortBank) return;
        std::stable_sort (shown.begin(), shown.end(), [this] (int a, int b)
        {
            const auto& x = entries[(size_t) a]; const auto& y = entries[(size_t) b];
            if (sort == sortName) return x.name.compareNatural (y.name) < 0;
            if (sort == sortCategory) { const int c = x.cat.compareNatural (y.cat); return c != 0 ? c < 0 : x.name.compareNatural (y.name) < 0; }
            const int rx = recent.rank (x.key()), ry = recent.rank (y.key());       // RECENT: the latest first, the rest in bank order
            if (rx < 0 || ry < 0) return rx >= 0 && ry < 0;
            return rx < ry;
        });
    }
    int rowOf (int entry) const { for (int r = 0; r < (int) shown.size(); ++r) if (shown[(size_t) r] == entry) return r; return -1; }
    // PREV / NEXT inside what is shown (wraps); from a sound that is not shown: the first / last row
    int step (int currentEntry, int d) const
    {
        const int n = (int) shown.size(); if (n == 0) return -1;
        const int r = rowOf (currentEntry);
        if (r < 0) return shown[(size_t) (d > 0 ? 0 : n - 1)];
        return shown[(size_t) (((r + d) % n + n) % n)];
    }
    // RANDOM inside what is shown, never the one playing (unless it is the only row)
    int random (int currentEntry, juce::Random& rng) const
    {
        const int n = (int) shown.size(); if (n == 0) return -1;
        if (n == 1) return shown[0];
        const int r = rowOf (currentEntry);
        int k = rng.nextInt (r >= 0 ? n - 1 : n);
        if (r >= 0 && k >= r) ++k;
        return shown[(size_t) k];
    }
    // the chips: derived tags first, then the category words, each with how many entries carry it
    std::vector<std::pair<juce::String, int>> tagCounts() const
    {
        juce::StringArray order (derivedTagNames());
        for (auto& c : bankCategories()) for (auto& w : categoryTags (c)) order.addIfNotAlreadyThere (w);
        order.addIfNotAlreadyThere ("legacy");
        std::vector<std::pair<juce::String, int>> out;
        for (auto& t : order)
        {
            int n = 0; for (auto& e : entries) if (visible (e) && hasTag (e, t)) ++n;
            if (n > 0) out.push_back ({ t, n });
        }
        return out;
    }
};

// the factory part of the entries (the page adds your sounds + snapshots after them)
inline void addFactory (std::vector<Entry>& out)
{
    const auto& bank = factoryBank();
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        Entry e; e.name = bank[(size_t) i].name; e.cat = bank[(size_t) i].cat; e.scene = bank[(size_t) i].scene; e.author = "Factory";
        e.tags = factoryTags (i); e.program = i;
        if (isLegacy (e.name)) { e.legacy = true; e.cat = legacyCategory(); e.tags.addIfNotAlreadyThere ("legacy"); }
        out.push_back (e);
    }
}
} // namespace gpro::browse
