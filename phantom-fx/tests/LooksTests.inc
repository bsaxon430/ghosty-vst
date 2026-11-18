// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX looks: the painted looks (DAWN, CEL, NEON and any further asset folder). Since 0.1.10 they are the only looks offered
// (Brandon, 2026-11-10): BLACK / PURPLE / LIGHT / GHOST keep their code but cannot be shown; the checks unlock BLACK only to use
// its layout as the yardstick the painted looks' controls are measured against.
//   looksChecks()      the checks added to the table: every page of every look lays out with nothing overlapping and every control
//                      reachable (1200 x 760 and resized), hit areas no smaller than on the drawn looks, the look button's round
//                      trip, the look never touches the sound or the saved state, fresh start, the animation's cost
//   "lookshots <dir>"  every page in every painted look as PNG
//   "uicpu"            what the 12 fps animation costs, per look
#include <sys/resource.h>
#include "../src/ui/UnitVisual.h"
#include "../src/ui/pv/Units.h"
#include "../src/ui/PaintedLook.h"
#include <thread>
#include <unistd.h>
namespace lookstest
{
// a hard time limit around anything that draws: if one step takes longer than its limit the test program says which step and
// stops at once (exit 3), so a machine that draws slowly fails fast instead of hanging
struct DrawLimit
{
    static std::atomic<juce::int64>& until() { static std::atomic<juce::int64> u { 0 }; return u; }
    static std::atomic<const char*>& what() { static std::atomic<const char*> w { "" }; return w; }
    DrawLimit (const char* step, int seconds)
    {
        static std::once_flag once;
        std::call_once (once, [] { std::thread ([] { for (;;) { std::this_thread::sleep_for (std::chrono::milliseconds (200)); const auto u = until().load(); if (u != 0 && juce::Time::currentTimeMillis() > u) { std::fprintf (stdout, "\nFAIL looks: TIME LIMIT: \"%s\" did not finish in time; stopping the test program\n", what().load()); std::fflush (stdout); _exit (3); } } }).detach(); });
        what() = step; until() = juce::Time::currentTimeMillis() + (juce::int64) seconds * 1000;
    }
    ~DrawLimit() { until() = 0; }
};
using phantom::PhantomEditor;
// (checks only) lets the old drawn looks be shown while this object lives
struct UnlockOldLooks { bool was = fxtheme::oldLooksUnlocked().exchange (true); ~UnlockOldLooks() { fxtheme::oldLooksUnlocked() = was; } };
static juce::File prefsFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock").getChildFile ("Phantom FX").getChildFile ("look.settings"); }
static void pump (int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static const int kPages = kNumUnits + 4;
static juce::String pageName (int pg) { static const char* n[] { "lanes", "mods", "perform", "spatial" }; return pg < kNumUnits ? juce::String ("rack-") + unitDef (pg).key : juce::String (n[pg - kNumUnits]); }
static void showPage (PhantomEditor& e, int pg) { if (pg < kNumUnits) e.showUnit (pg); else e.showTab (pg - kNumUnits + 1); }

// the controls a person can use on the page showing: every visible slider, button, menu and drawn editor part (leaf components)
struct Ctl { juce::Component* c; juce::Rectangle<int> r; juce::String id; bool dial; };
static void collect (juce::Component& root, juce::Component& c, std::vector<Ctl>& out)
{
    for (auto* k : c.getChildren())
    {
        if (! k->isVisible() || k->getWidth() <= 0 || k->getHeight() <= 0) continue;
        const bool control = dynamic_cast<juce::Slider*> (k) != nullptr || dynamic_cast<juce::Button*> (k) != nullptr || dynamic_cast<juce::ComboBox*> (k) != nullptr || dynamic_cast<juce::TextEditor*> (k) != nullptr;
        if (control)
        {
            if (dynamic_cast<juce::Slider*> (k->getParentComponent()) != nullptr || dynamic_cast<juce::ComboBox*> (k->getParentComponent()) != nullptr) continue;   // a slider's own inc / dec buttons, a menu's label
            juce::String id = k->getProperties()["gpfxParam"].toString(); if (id.isEmpty()) if (auto* b = dynamic_cast<juce::Button*> (k)) id = b->getTooltip().startsWith ("Look:") ? juce::String ("btn:look") : "btn:" + b->getButtonText() + "@" + juce::String (typeid (*k->getParentComponent()).name()).getLastCharacters (9); if (id.isEmpty()) id = k->getName();
            auto* sl = dynamic_cast<juce::Slider*> (k); out.push_back ({ k, root.getLocalArea (k, k->getLocalBounds()), id, sl != nullptr && sl->isRotary() }); continue;
        }
        collect (root, *k, out);
    }
}
struct Size { int w = 0, h = 0; bool dial = false; bool operator== (const Size& o) const { return w == o.w && h == o.h; } };
struct PageResult { int controls = 0; juce::StringArray bad; std::map<juce::String, Size> area; };
static PageResult checkPage (PhantomEditor& e)
{
    PageResult pr; std::vector<Ctl> cs; collect (e, e, cs); pr.controls = (int) cs.size();
    for (size_t i = 0; i < cs.size(); ++i)
    {
        const auto& a = cs[i]; pr.area[a.id] = { a.r.getWidth(), a.r.getHeight(), a.dial };
        if (! e.getLocalBounds().contains (a.r)) pr.bad.add (a.id + " outside the window");
        if (a.r.getWidth() < 14 || a.r.getHeight() < 11) pr.bad.add (a.id + " too small " + a.r.toString());
        // reachable: a click in its middle lands on it (or on a part of it), nothing lies on top
        auto* hit = e.getComponentAt (a.r.getCentre()); bool mine = false; for (auto* h = hit; h != nullptr; h = h->getParentComponent()) if (h == a.c) { mine = true; break; }
        if (! mine) pr.bad.add (a.id + " covered by " + (hit != nullptr ? juce::String (typeid (*hit).name()) : juce::String ("nothing")));
        for (size_t j = i + 1; j < cs.size(); ++j) { const auto x = a.r.getIntersection (cs[j].r); if (x.getWidth() > 1 && x.getHeight() > 1) pr.bad.add (a.id + " overlaps " + cs[j].id); }
    }
    return pr;
}
static std::unique_ptr<PhantomProcessor> demoProcessor()
{
    auto p = std::make_unique<PhantomProcessor>(); p->prepareToPlay (48000.0, 256);
    for (int u : { uFatten, uCrackle, uEq, uHalls }) p->setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f);
    p->storeScene (0); p->storeScene (3); return p;
}

// ---- painted unit visuals (src/ui/pv): the designer's stand-in beat (visuals/src/sim.js) so a still can be compared with the painter's
struct SimF { float level, hit, low, mid, high; };
static SimF simFeed (double t, double loud)
{
    if (loud <= 0) return { 0, 0, 0, 0, 0 };
    const double bpm = 120, b = t * bpm / 60, bar = std::fmod (std::fmod (b, 4.0) + 4.0, 4.0), spb = 60 / bpm;
    auto since = [&] (std::initializer_list<double> l) { double m = 1e9; for (double x : l) { double d = bar - x; if (d < 0) d += 4; m = juce::jmin (m, d); } return m * spb; };
    const double tk = since ({ 0, 1.5, 2 }), ts = since ({ 1, 3 }), th = since ({ 0, .5, 1, 1.5, 2, 2.5, 3, 3.5 });
    const double low = std::exp (-tk / .16), mid = .85 * std::exp (-ts / .11), high = juce::jmax (.3 * std::exp (-th / .035), .7 * std::exp (-ts / .06));
    const double env = juce::jmax (std::exp (-tk / .3), .85 * std::exp (-ts / .24), .22 * std::exp (-th / .08)), hit = juce::jmax (tk < .25 ? 1 - tk / .25 : 0.0, ts < .25 ? 1 - ts / .25 : 0.0);
    return { (float) juce::jlimit (0.0, 1.0, env * loud), (float) hit, (float) (low * loud), (float) (mid * loud), (float) (high * loud) };
}
static painted::Look* lookById (const juce::String& id) { if (auto* r = painted::Registry::instance()) for (auto& l : r->looks) if (l->id == id) return l.get(); return nullptr; }
// one picture of a unit's painted scene at the painter's still size (2x the look's stage scale): vals = "a=1&b=.5", replayed hits from 2.5 s before
static constexpr int kPvUnits = 24;   // the units with a painted visual so far
static bool pvWideUnit (int u) { return u == uGate || u == uGrid || u == uHaunt || u == uWarp || u == uCrackle || u == uRetro || u == uEq; }
// (DAWN and a unit with its own DAWN wide-page picture: that picture, 1 su = 1 design px, at scale; wide: on the look's wide stage)
static juce::Image pvRenderV (painted::Look& L, pv::Scene& sc, const std::vector<float>& vals, double time, double loud, bool on, std::map<std::string, juce::Image>& cache, int* paths = nullptr, double scale = 2.0, bool replay = false, bool wide = false, std::function<void (pv::Feed&)> extra = {})
{
    const bool dawnPic = pv::lookFor (L.str ("visual.style")) == pv::Look::dawn && sc.hasDawn(); const auto dr = sc.dawnRect();
    auto draw = [&] (pv::G& G, const pv::Feed& F, pv::State& st, const pv::Clock& T) { if (dawnPic) sc.dawnScene (G, F, st, T, dr.getWidth(), dr.getHeight()); else sc.scene (G, F, st, T); };
    auto names = sc.controls(); pv::Feed f; f.names = &names; for (size_t i = 0; i < names.size() && i < vals.size(); ++i) f.vals[i] = vals[i];
    auto at = [&] (double tt) { auto x = f; const auto sf = simFeed (tt, loud); x.level = sf.level; x.hit = sf.hit; x.low = sf.low; x.mid = sf.mid; x.high = sf.high; x.on = on; x.t = tt; if (extra) extra (x); return x; };
    pv::State st; juce::Image tiny (juce::Image::ARGB, 8, 8, true, juce::SoftwareImageType()); juce::Graphics tg (tiny);
    for (double ft = juce::jmax (0.0, time - 2.5); ft < time - 1e-6; ft += 1.0 / 24)
    {   // (replay: the scene is also drawn, small, for the last second, so what it smooths from frame to frame is where it would be)
        const auto Fp = at (ft); st.step (Fp, pv::Clock::at (ft));
        if (replay && ft > time - 1.0) { pv::G Gt (tg, pv::lookFor (L.str ("visual.style")), &L.visualBank(), 0.03f, 0, 0, 1.0f, &cache); draw (Gt, Fp, st, pv::Clock::at (ft)); }
    }
    const auto F = at (time); st.step (F, pv::Clock::at (time));
    if (dawnPic)
    {
        juce::Image im (juce::Image::ARGB, juce::roundToInt (dr.getWidth() * scale), juce::roundToInt (dr.getHeight() * scale), true, juce::SoftwareImageType());
        juce::Graphics g (im); pv::G G (g, pv::Look::dawn, &L.visualBank(), (float) scale, 0, 0, 1.0f, &cache); draw (G, F, st, pv::Clock::at (time)); if (paths) *paths = G.nFill + G.nStroke; return im;
    }
    const auto sr = wide && L.has ("visual.wide.stage") ? L.rect ("visual.wide.stage") : L.rect ("visual.stage"); const double k = sr.getWidth() / 280.0 * scale;
    juce::Image im (juce::Image::ARGB, juce::roundToInt (280 * k), juce::roundToInt (300 * k), true, juce::SoftwareImageType());
    {
        juce::Graphics g (im);
        if ((bool) L.at ("visual.glass") && scale > 1.5) { const auto bn = L.str ("glass.blur").replace ("{page}", "rack"); const auto& b = L.img (bn); if (b.ok()) g.drawImage (b.im, 0, 0, im.getWidth(), im.getHeight(), juce::roundToInt (sr.getX() * b.scale), juce::roundToInt (sr.getY() * b.scale), juce::roundToInt (sr.getWidth() * b.scale), juce::roundToInt (sr.getHeight() * b.scale)); }
        pv::G G (g, pv::lookFor (L.str ("visual.style")), &L.visualBank(), (float) k, 0, 0, 1.0f, &cache); sc.scene (G, F, st, pv::Clock::at (time));
        if (paths) *paths = G.nFill + G.nStroke;
    }
    return im;
}
static juce::Image pvRender (painted::Look& L, pv::Scene& sc, const juce::StringPairArray& vals, double time, double loud, bool on, std::map<std::string, juce::Image>& cache, int* paths = nullptr, double scale = 2.0)
{
    auto names = sc.controls(); auto v = sc.defaults(); for (size_t i = 0; i < names.size(); ++i) if (vals.containsKey (names[i])) v[i] = vals[names[i]].getFloatValue();
    return pvRenderV (L, sc, v, time, loud, on, cache, paths, scale);
}
static void looksChecks()
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { report (true, "looks", "no display: skipped"); return; }
    const auto savedPrefs = prefsFile().existsAsFile() ? prefsFile().loadFileAsString() : juce::String();
    auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->setVisible (true);   // (not on the desktop: laid out and hit-tested off screen)
    // the looks offered are f0 .. n - 1 (the painted ones); the four drawn looks keep their numbers 0 .. 3 and are not offered
    const int n = e->numLooks(), f0 = e->firstLook(); juce::StringArray names, offered; for (int l = 0; l < n; ++l) { names.add (e->lookTitle (l)); if (l >= f0) offered.add (e->lookTitle (l)); }
    report (f0 == fxtheme::kFirstPainted && offered.joinIntoString (", ") == "DAWN, CEL, NEON" && e->currentLook() >= f0 && names[0] == "BLACK" && names[3] == "GHOST", "looks: the list",
            juce::String (offered.size()) + " looks offered: " + offered.joinIntoString (", ") + " (a new window opened in " + e->lookTitle (e->currentLook()) + "); BLACK, PURPLE, LIGHT and GHOST are kept in the code and not offered");

    // every page of every look at 1200 x 760: nothing overlaps, everything inside the window, every control reachable
    // (BLACK is laid out too, unlocked for that moment and not reported: it is the yardstick for the hit-area check below)
    std::vector<std::map<juce::String, Size>> areas ((size_t) (n * kPages));
    for (int l = 0; l < n; ++l)
    {
        if (l > 0 && l < f0) continue;
        std::unique_ptr<UnlockOldLooks> yard; if (l < f0) yard = std::make_unique<UnlockOldLooks>();
        e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); juce::StringArray bad; int controls = 0;
        for (int pg = 0; pg < kPages; ++pg) { showPage (*e, pg); pump (5); auto r = checkPage (*e); controls += r.controls; for (auto& b : r.bad) bad.add (pageName (pg) + ": " + b); areas[(size_t) (l * kPages + pg)] = r.area; }
        if (const auto dump = juce::SystemStats::getEnvironmentVariable ("PHANTOM_DUMP", ""); dump.isNotEmpty())
        {   // (every control's size on every page, for comparing two builds)
            juce::String txt; for (int pg = 0; pg < kPages; ++pg) for (auto& [id, a] : areas[(size_t) (l * kPages + pg)]) txt << names[l] << " " << pageName (pg) << " " << id.replaceCharacter (' ', '_') << " " << a.w << " " << a.h << " " << (a.dial ? "dial" : "ctl") << "\n";
            juce::File (dump).appendText (txt);
        }
        if (yard != nullptr) { yard.reset(); e->setLook (f0); continue; }
        report (bad.isEmpty(), "looks: " + names[l] + " layout at 1200 x 760", juce::String (kPages) + " pages, " + juce::String (controls) + " controls; overlapping / outside / covered: " + (bad.isEmpty() ? juce::String ("none") : juce::String (bad.size()) + ": " + bad.joinIntoString ("; ").substring (0, juce::SystemStats::getEnvironmentVariable ("PHANTOM_LONG", "").isNotEmpty() ? 200000 : 600)));
    }
    // the painted looks keep every control of the old drawn looks, none with a smaller target (compared with BLACK at 1200 x 760, page by
    // page): a dial's clickable square is the same or bigger; a button, menu or bar is as high as before, and as wide wherever the
    // look's page is as wide (CEL keeps the right third for the machine: there a menu may be narrower, never under 60 %)
    const double kWidthFloor = 0.85;   // a button, menu or bar on a painted look is never under 85 % of its width on BLACK
    for (int l = fxtheme::kFirstPainted; l < n; ++l)
    {
        juce::StringArray missing, smaller; int compared = 0, bigger = 0, narrower = 0; double worstW = 1.0e9;
        for (int pg = 0; pg < kPages; ++pg) for (auto& [id, a0] : areas[(size_t) pg])
        {
            auto& mine = areas[(size_t) (l * kPages + pg)]; auto it = mine.find (id);
            if (it == mine.end()) { missing.add (pageName (pg) + "/" + id); continue; }
            const auto a1 = it->second; ++compared; if (a1.w * a1.h >= a0.w * a0.h) ++bigger;
            if (a0.dial) { if (juce::jmin (a1.w, a1.h) < juce::jmin (a0.w, a0.h)) smaller.add (pageName (pg) + "/" + id + " dial " + juce::String (juce::jmin (a1.w, a1.h)) + " < " + juce::String (juce::jmin (a0.w, a0.h))); continue; }
            if (a1.h < a0.h - 1) smaller.add (pageName (pg) + "/" + id + " height " + juce::String (a1.h) + " < " + juce::String (a0.h));
            const double rw = (double) a1.w / (double) juce::jmax (1, a0.w); if (rw < 0.995) { ++narrower; worstW = juce::jmin (worstW, rw); }
            if (rw < kWidthFloor) smaller.add (pageName (pg) + "/" + id + " width " + juce::String (a1.w) + " < " + juce::String (a0.w));
        }
        report (missing.isEmpty(), "looks: " + names[l] + " keeps every control", juce::String (compared) + " controls compared with BLACK, missing: " + (missing.isEmpty() ? juce::String ("none") : missing.joinIntoString (", ").substring (0, 400)));
        report (smaller.isEmpty(), "looks: " + names[l] + " hit areas", juce::String (bigger) + " of " + juce::String (compared) + " controls have the same or a bigger area than on BLACK; no dial is smaller, no control lower and none under 85 % of its width there; " + juce::String (narrower) + " are narrower, the narrowest " + (narrower > 0 ? juce::String (juce::roundToInt (worstW * 100.0)) + " %" : juce::String ("-")) + "; too small: " + (smaller.isEmpty() ? juce::String ("none") : juce::String (smaller.size()) + ": " + smaller.joinIntoString (", ").substring (0, juce::SystemStats::getEnvironmentVariable ("PHANTOM_LONG", "").isNotEmpty() ? 200000 : 500)));
    }
    // the painted unit visuals (src/ui/pv: the designer's scenes, in DAWN, CEL and NEON). For every unit that has one, in every
    // look: each control it reads exists; the same feed gives the same picture; EVERY control it reads changes the picture from one
    // end to the other with no sound playing; the sound moves it; a picture paints in under 4 ms on average here (software drawing,
    // 1x); its stage sits inside the window and neither the stage nor the frame drawn round it meets a control; the switch hides it
    // and is remembered. (The four old drawn looks and their code-only pictures are not offered and not checked.)
    {
        juce::StringArray bad, lines; int units = 0; std::map<std::string, juce::Image> cache;
        auto differ = [] (const juce::Image& a, const juce::Image& b) { int d = 0; const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) { const auto ca = da.getPixelColour (x, y), cb = db.getPixelColour (x, y); if (std::abs ((int) ca.getRed() - (int) cb.getRed()) + std::abs ((int) ca.getGreen() - (int) cb.getGreen()) + std::abs ((int) ca.getBlue() - (int) cb.getBlue()) > 6) ++d; }
            return 100.0 * d / (a.getWidth() * a.getHeight()); };
        struct Row { int nIds = 0; double least = 1.0e9, snd = 1.0e9, ms = 0.0; juce::String weakest; juce::StringArray moving, withSound, small, editorShows; juce::String sndNeeds; }; std::map<int, Row> rows;
        for (int l = f0; l < n; ++l)
        {
            e->setLook (l); auto* L = painted::active(); if (L == nullptr) { bad.add (names[l] + ": not a painted look"); continue; }
            for (int u = 0; u < kNumUnits; ++u)
            {
                auto sc = pv::makeScene (u); if (sc == nullptr) continue; auto& row = rows[u]; const auto ids = sc->controls(); const auto defs = sc->defaults(); row.nIds = (int) ids.size(); const juce::String who = names[l] + " " + unitDef (u).shortName;
                if ((int) ids.size() > 24 || defs.size() != ids.size()) bad.add (who + ": controls / defaults do not match");
                std::vector<juce::RangedAudioParameter*> ps; for (auto& id : ids) { ps.push_back (p->apvts.getParameter (PhantomProcessor::pid (u, id))); if (ps.back() == nullptr) bad.add (who + ": no control " + id); }
                // the two ends of a control in the scene's own units (a menu's first and last entry, a switch off and on, a count 1..n, a knob 0..1)
                auto ends = [&] (size_t i) -> std::pair<float, float>
                {
                    auto* rp = ps[i]; if (rp == nullptr) return { 0.0f, 1.0f };
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp)) return { 0.0f, (float) (c->choices.size() - 1) };
                    if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) return { 0.0f, 1.0f };
                    if (auto* ip = dynamic_cast<juce::AudioParameterInt*> (rp)) return { (float) ip->getRange().getStart(), (float) ip->getRange().getEnd() };
                    return { 0.0f, sc->scaleOf (ids[i]) };
                };
                const bool wd = pvWideUnit (u); auto still = [&] (std::vector<float> v, double loud) { return pvRenderV (*L, *sc, v, 2.27, loud, true, cache, nullptr, 1.0, false, wd); };
                if (differ (still (defs, 0), still (defs, 0)) != 0.0) bad.add (who + ": not the same picture twice");
                // other settings for controls that only show when another one is engaged (an LFO's rate with its amount up and the
                // movement menu on LFO, a delay time with SYNC off ...): every knob engaged with the switches on, then off; and each menu at
                // its second and last entry
                std::vector<std::vector<float>> bases { defs };
                for (int sw = 1; sw >= 0; --sw) { auto en = defs; for (size_t i = 0; i < ids.size(); ++i) { if (ps[i] == nullptr || dynamic_cast<juce::AudioParameterChoice*> (ps[i]) != nullptr) continue; const auto e2 = ends (i); en[i] = dynamic_cast<juce::AudioParameterBool*> (ps[i]) != nullptr ? (float) sw : e2.first + (e2.second - e2.first) * 0.85f; } bases.push_back (en); }
                for (size_t j = 0; j < ids.size(); ++j) if (ps[j] != nullptr && dynamic_cast<juce::AudioParameterChoice*> (ps[j]) == nullptr) { auto v = defs; const auto e2 = ends (j); v[j] = dynamic_cast<juce::AudioParameterBool*> (ps[j]) != nullptr ? 1.0f - (defs[j] > 0.5f ? 1.0f : 0.0f) : e2.first + (e2.second - e2.first) * 0.85f; bases.push_back (v); }   // each one engaged alone
                const size_t nPlain = bases.size();
                for (size_t b0 = 0; b0 < nPlain; ++b0) for (size_t j = 0; j < ids.size(); ++j) if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (ps[j])) for (int idx = 1; idx < c->choices.size(); ++idx) if (c->choices.size() <= 6 || idx == 1 || idx == c->choices.size() - 1) { auto v = bases[b0]; v[j] = (float) idx; bases.push_back (v); }
                auto clip = [&] (const std::vector<float>& v, double loud) { std::vector<juce::Image> out; for (double t : { 2.27, 2.6, 2.95 }) out.push_back (pvRenderV (*L, *sc, v, t, loud, true, cache, nullptr, 1.0, loud > 0, wd)); return out; };
                auto differClip = [&] (const std::vector<juce::Image>& a, const std::vector<juce::Image>& b) { double d = 0; for (size_t j = 0; j < a.size(); ++j) d = juce::jmax (d, differ (a[j], b[j])); return d; };
                // each control: from its first setting to each other one (a knob 0 -> 1, a menu entry 0 -> every entry), in silence on one
                // picture; else in silence over a second of motion; else with the sound playing
                const bool dawnPic = pv::lookFor (L->str ("visual.style")) == pv::Look::dawn && sc->hasDawn(); const auto dIds = sc->dawnControls();
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    if (dawnPic && std::find (dIds.begin(), dIds.end(), ids[i]) == dIds.end()) { row.editorShows.addIfNotAlreadyThere (ids[i]); continue; }   // (on DAWN's wide page: shown by the editor in front)
                    const auto e2 = ends (i); std::vector<float> others { e2.second };
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (ps[i])) { others.clear(); for (int idx = 1; idx < c->choices.size(); ++idx) others.push_back ((float) idx); }
                    double d = 0.0, best = 0.0; int how = 0;   // 1 still in silence, 2 moving in silence, 3 only with sound
                    for (int stage = 1; stage <= 3 && how == 0; ++stage)
                        for (auto& base : bases)
                        {
                            if (how != 0) break;
                            if (&base != &bases[0] && base[i] != defs[i] && dynamic_cast<juce::AudioParameterChoice*> (ps[i]) != nullptr) continue;
                            for (float hiV : others)
                            {
                                auto lo = base, hi = base; lo[i] = e2.first; hi[i] = hiV;
                                d = stage == 1 ? differ (still (lo, 0), still (hi, 0)) : differClip (clip (lo, stage == 3 ? 1 : 0), clip (hi, stage == 3 ? 1 : 0)); best = juce::jmax (best, d);
                                if (d >= 0.25) { how = stage; break; }
                                if (stage == 3 && d >= 0.03) { how = 4; break; }   // (a small change: a few streaks, a pointer)
                            }
                        }
                    if (how == 1 && best < row.least) { row.least = best; row.weakest = juce::String (ids[i]) + " in " + names[l]; }
                    if (how == 2) row.moving.addIfNotAlreadyThere (ids[i]); if (how == 3) row.withSound.addIfNotAlreadyThere (ids[i]); if (how == 4) row.small.addIfNotAlreadyThere (juce::String (ids[i]) + " " + juce::String (best, 2) + " %");
                    if (how == 0) bad.add (who + ": " + ids[i] + " changes only " + juce::String (best, 2) + " % of the picture, even with sound");
                }
                double snd = differ (still (defs, 0), still (defs, 1)); juce::String sndNeeds;
                // (a unit that only plays while something is set, HAUNT with a pad held: the sound with that set)
                for (size_t b0 = 1; b0 < bases.size() && snd < 0.25; ++b0) { const double d2 = differ (still (bases[b0], 0), still (bases[b0], 1)); if (d2 > snd) { snd = d2; sndNeeds.clear(); for (size_t j = 0; j < ids.size(); ++j) if (bases[b0][j] != defs[j]) sndNeeds << (sndNeeds.isEmpty() ? "" : " ") << ids[j]; } }
                row.snd = juce::jmin (row.snd, snd); if (sndNeeds.isNotEmpty()) row.sndNeeds = sndNeeds; if (snd < 0.25) bad.add (who + ": the sound does not move it");
                DrawLimit limit ("a unit visual's paint time", 60); for (int k = 0; k < 3; ++k) pvRenderV (*L, *sc, defs, 1.0 + k / 24.0, 1, true, cache, nullptr, 1.0, false, wd);   // (caches warm)
                double sum = 0.0; for (int k = 0; k < 24; ++k) { const double t0 = juce::Time::getMillisecondCounterHiRes(); auto im = pvRenderV (*L, *sc, defs, 2.0 + k / 24.0, 1, true, cache, nullptr, 1.0, false, wd); juce::ignoreUnused (im); sum += juce::Time::getMillisecondCounterHiRes() - t0; }
                const double ms = sum / 24.0; row.ms = juce::jmax (row.ms, ms); if (ms > 4.0) bad.add (who + ": " + juce::String (ms, 2) + " ms a picture");
            }
        }
        for (auto& [u, row] : rows) { ++units; lines.add (juce::String (unitDef (u).shortName) + ": " + juce::String (row.nIds) + " controls (the least visible on a still, " + row.weakest + ", changes " + juce::String (row.least, 1) + " % of the picture" + (row.moving.isEmpty() ? juce::String() : "; shown by motion: " + row.moving.joinIntoString (" ")) + (row.withSound.isEmpty() ? juce::String() : "; shown only with sound: " + row.withSound.joinIntoString (" ")) + (row.small.isEmpty() ? juce::String() : "; small changes: " + row.small.joinIntoString (", ")) + (row.editorShows.isEmpty() ? juce::String() : "; on DAWN's page shown by the editor in front: " + row.editorShows.joinIntoString (" ")) + "), sound " + juce::String (row.snd, 1) + " %" + (row.sndNeeds.isEmpty() ? juce::String() : " (with " + row.sndNeeds + " set)") + ", " + juce::String (row.ms, 2) + " ms"); }
        report (bad.isEmpty() && units == kPvUnits, "unit visuals: every control changes the painted picture", juce::String (units) + " units painted, each checked in " + offered.joinIntoString (", ") + ". " + lines.joinIntoString ("; ") + " (limits: 0.25 % of the picture per control and for the sound, 4 ms a picture on average)" + (bad.isEmpty() ? juce::String() : "; wrong: " + bad.joinIntoString ("; ").substring (0, 900)));
        // on the page
        juce::StringArray bad2; juce::String modes; int panes = 0; const bool was = e->unitVisuals(); juce::String paneAt;
        std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* { if (c->getName() == "UNIT VISUAL" && c->isVisible()) return c; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = find (k)) return r; return nullptr; };
        e->setUnitVisuals (true);
        for (int l = f0; l < n; ++l) for (int u = 0; u < kNumUnits; ++u)
        {
            if (pv::makeScene (u) == nullptr) continue; e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (u); pump (5);
            auto* pn = find (e); if (pn == nullptr) { bad2.add (names[l] + " " + unitDef (u).shortName + ": no picture"); continue; } ++panes;
            // where it stands (0 the stage, 1 the wide stage, 2 DAWN behind the editor: light only, takes no clicks, 3 DAWN's own small pane)
            const int md = (int) pn->getProperties().getWithDefault ("pvMode", 0); const juce::String who = names[l] + " " + unitDef (u).shortName; modes << who << " " << juce::StringArray { "stage", "wide stage", "under the editor", "own pane" }[juce::jlimit (0, 3, md)] << "; ";
            const auto r = e->getLocalArea (pn, pn->getLocalBounds()); if (! e->getLocalBounds().contains (r) || (md < 2 && (r.getWidth() < 200 || r.getHeight() < 200)) || r.getWidth() < 40) bad2.add (who + ": stage at " + r.toString());
            if (pvWideUnit (u) != (md >= 1) && ! (u == uCrackle && names[l] == "DAWN" && md == 0)) bad2.add (who + (pvWideUnit (u) ? ": a wide page, but the picture stands on the narrow stage" : ": not a wide page, but the picture stands elsewhere"));
            if (md == 2 && (pn->isOpaque() || [&] { bool a = false, b = false; pn->getInterceptsMouseClicks (a, b); return a; }())) bad2.add (who + ": the picture behind the editor is opaque or takes clicks");
            const char* fk = md == 1 ? "visual.wide.frame" : "visual.frame";
            juce::Rectangle<int> fr; if (auto* L = painted::active(); L != nullptr && md < 2 && L->has (juce::String (fk) + ".at")) fr = painted::Map (*e).r (L->el (juce::String (fk) + ".at", 0), L->el (juce::String (fk) + ".at", 1), L->el (juce::String (fk) + ".size", 0), L->el (juce::String (fk) + ".size", 1)).reduced (2);
            std::vector<Ctl> cs; collect (*e, *e, cs); for (auto& c : cs) { if (md != 2 && c.r.intersects (r)) bad2.add (who + ": stage meets " + c.id); if (! fr.isEmpty() && c.r.intersects (fr)) bad2.add (who + ": frame meets " + c.id); }
            if (md == 2) { const auto pr = checkPage (*e); for (auto& b : pr.bad) if (b.contains ("covered")) bad2.add (who + ": " + b); }   // (every control over it is still reached)
            auto img = e->createComponentSnapshot (r, false, 1.0f); juce::ignoreUnused (img);
            if (u == uFatten) paneAt << names[l] << " " << r.toString() << "  ";
        }
        e->setLook (f0); e->showUnit (uFatten); pump (5); e->setUnitVisuals (false); pump (5); const bool hidden = find (e) == nullptr; bool saved = false; { juce::PropertiesFile props (prefsFile(), {}); saved = ! props.getBoolValue ("unitVisuals", true); }
        e->setUnitVisuals (true); pump (5); const bool back = find (e) != nullptr; e->setUnitVisuals (was);
        report (bad2.isEmpty() && panes == kPvUnits * (n - f0) && hidden && saved && back, "unit visuals: on the page", juce::String (panes) + " unit pages in " + offered.joinIntoString (", ") + ": the picture stands on its stage inside the window, and neither the stage nor its frame meets a control" + (bad2.isEmpty() ? juce::String() : " EXCEPT " + bad2.joinIntoString ("; ").substring (0, 500))
                + " (" + paneAt + "; wide pages: " + modes + ")" + "; switched off it is gone (" + (hidden ? "yes" : "NO") + ") and that is remembered (" + (saved ? "yes" : "NO") + "); switched on it is back (" + (back ? "yes" : "NO") + ")");
    }
    // a unit visual shows ONLY on the RACK tab's unit pages (Brandon, 0.1.11: FATTEN's picture stayed over SPATIAL's side panel).
    // Every look; from every unit page to every other tab and back; tab to tab; and, while another tab shows: a look change, a resize,
    // a scene recall and the host reopening the editor there. Off the unit pages no picture (nor its frame, glass or what it
    // replaces) may show; back on a unit page it must be there again, on its stage, meeting no control.
    {
        juce::StringArray bad; int moves = 0; const bool was = e->unitVisuals(); e->setUnitVisuals (true);
        std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* { if (c->getName() == "UNIT VISUAL" && c->isVisible()) return c; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = find (k)) return r; return nullptr; };
        static const char* tabN[5] { "RACK", "LANES", "MODS", "PERFORM", "SPATIAL" };
        auto offPage = [&] (PhantomEditor& ed, const juce::String& where) { ++moves; if (auto* pn = find (&ed)) bad.add (where + ": a unit visual shows at " + ed.getLocalArea (pn, pn->getLocalBounds()).toString()); };
        auto onPage = [&] (PhantomEditor& ed, int u, const juce::String& where)
        {
            ++moves; auto* pn = find (&ed); if (pn == nullptr) { bad.add (where + ": no picture back on " + unitDef (u).shortName); return; }
            const auto r = ed.getLocalArea (pn, pn->getLocalBounds()); const int md = (int) pn->getProperties().getWithDefault ("pvMode", 0); if (md == 2) return;   // (DAWN light under / over an editor: checked above)
            std::vector<Ctl> cs; collect (ed, ed, cs); for (auto& c : cs) if (c.r.intersects (r)) bad.add (where + ": " + unitDef (u).shortName + "'s picture meets " + c.id);
        };
        for (int l = f0; l < n; ++l)
        {
            e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);
            for (int u = 0; u < kNumUnits; ++u)
            {
                if (pv::makeScene (u) == nullptr) continue;
                for (int t = 1; t <= 4; ++t)
                {
                    e->showUnit (u); pump (2); onPage (*e, u, names[l] + " " + unitDef (u).shortName);
                    e->showTab (t); pump (2); offPage (*e, names[l] + " " + unitDef (u).shortName + " -> " + tabN[t]);
                    for (int t2 = 1; t2 <= 4; ++t2) if (t2 != t && u % 6 == t % 6) { e->showTab (t2); pump (1); offPage (*e, names[l] + " " + tabN[t] + " -> " + tabN[t2]); e->showTab (t); pump (1); }
                    e->showTab (0); pump (2); onPage (*e, u, names[l] + " " + tabN[t] + " -> RACK (" + unitDef (u).shortName + ")");
                }
            }
            // while another tab shows: a look change, a resize, a scene recall
            for (int t = 1; t <= 4; ++t)
            {
                e->showUnit (uFatten); pump (2); e->showTab (t); pump (2);
                e->setLook (f0 + ((l - f0 + 1) % (n - f0))); pump (2); offPage (*e, names[l] + " " + tabN[t] + ", then a look change");
                e->setLook (l); pump (2); e->setSize (PhantomProcessor::kDefaultWidth + 240, PhantomProcessor::kDefaultHeight + 152); pump (2); offPage (*e, names[l] + " " + tabN[t] + ", then a resize");
                e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); p->recallScene (3); pump (4); offPage (*e, names[l] + " " + tabN[t] + ", then a scene recall");
            }
        }
        // the host closes the editor on another tab and opens it again
        for (int t = 1; t <= 4; ++t)
        {
            e->setLook (f0); e->showUnit (uFatten); pump (2); e->showTab (t); pump (2);
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (p->createEditor()); auto* e2 = dynamic_cast<PhantomEditor*> (ed2.get());
            if (e2 != nullptr) { e2->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e2->setVisible (true); pump (3); e2->showTab (t); pump (2); offPage (*e2, juce::String ("reopened on ") + tabN[t]); }
        }
        e->setLook (f0); e->showTab (0); e->showUnit (uFatten); pump (2); e->setUnitVisuals (was);
        report (bad.isEmpty() && moves > 900, "unit visuals: only on the unit pages", juce::String (moves) + " page changes in " + offered.joinIntoString (", ") + " (every painted unit's page to LANES, MODS, PERFORM, SPATIAL and back, tab to tab, a look change, a resize and a scene recall on each tab, the editor reopened on each tab): off the unit pages no picture shows, back on a unit page it is there again and meets no control" + (bad.isEmpty() ? juce::String() : "; wrong: " + bad.joinIntoString ("; ").substring (0, 900)));
    }
    // GHOST LIMITER's gain-reduction meter shows on its page in every look, inside the window, under no control and on none
    {
        juce::StringArray bad; int seen = 0;
        std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* { if (c->getName() == "GR METER") return c; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = find (k)) return r; return nullptr; };
        for (int l = f0; l < n; ++l)
        {
            e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (uLim); pump (5);
            auto* m = find (e); if (m == nullptr) { bad.add (names[l] + ": no meter"); continue; } ++seen;
            const auto mr = e->getLocalArea (m, m->getLocalBounds()); if (! e->getLocalBounds().contains (mr) || mr.getWidth() < 300 || mr.getHeight() < 16) bad.add (names[l] + ": meter at " + mr.toString());
            std::vector<Ctl> cs; collect (*e, *e, cs); for (auto& c : cs) if (c.r.intersects (mr)) bad.add (names[l] + ": meter meets " + c.id);
            auto img = e->createComponentSnapshot (mr, false, 1.0f); if (! img.isValid()) bad.add (names[l] + ": not drawn");
            e->showUnit (uFatten); pump (2); if (find (e) != nullptr) bad.add (names[l] + ": a meter on FATTEN");
        }
        report (bad.isEmpty() && seen == n - f0, "looks: the limiter's gain-reduction meter", "on the GHOST LIMITER page in " + juce::String (seen) + " of " + juce::String (n - f0) + " looks (" + offered.joinIntoString (", ") + "), inside the window, clear of every control, on no other unit's page" + (bad.isEmpty() ? juce::String() : "; wrong: " + bad.joinIntoString ("; ").substring (0, 400)));
    }
    // SPIRIT EQ's spectrum display: its switch is on the EQ page in every look; switched on it takes the dials' place, reads the
    // unit's output (a 1 kHz tone shows as a peak at 1 kHz) and switches the audio thread's copy on; switched off, the dials are
    // back and the copy stops
    {
        juce::StringArray bad; int seen = 0;
        std::function<juce::Component* (juce::Component*, const juce::String&)> find = [&] (juce::Component* c, const juce::String& nm) -> juce::Component* { if (c->getName() == nm) return c; for (auto* k : c->getChildren()) if (auto* r = find (k, nm)) return r; return nullptr; };
        std::function<juce::TextButton* (juce::Component*)> findBtn = [&] (juce::Component* c) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText() == "SPECTRUM" && b->isVisible()) return b; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = findBtn (k)) return r; return nullptr; };
        auto* tap = p->rack.slot (uEq).fx().analyserTap(); const float eqOn = p->getParamNormalised (PhantomProcessor::pid (uEq, "on"));
        p->setParamNormalised (PhantomProcessor::pid (uEq, "on"), 1.0f);
        for (int l = f0; l < n; ++l)
        {
            e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (uEq); pump (5);
            auto* btn = findBtn (e); if (btn == nullptr || tap == nullptr) { bad.add (names[l] + ": no SPECTRUM switch"); continue; }
            const int dialsBefore = [&] { std::vector<Ctl> cs; collect (*e, *e, cs); int d = 0; for (auto& c : cs) if (c.dial) ++d; return d; }();
            btn->triggerClick(); pump (30); auto* sv = find (e, "SPECTRUM VIEW");
            if (sv == nullptr || ! sv->isVisible()) { bad.add (names[l] + ": no display"); continue; } ++seen;
            const auto r = e->getLocalArea (sv, sv->getLocalBounds()); if (! e->getLocalBounds().contains (r) || r.getWidth() < 300 || r.getHeight() < 60) bad.add (names[l] + ": display at " + r.toString());
            std::vector<Ctl> cs; collect (*e, *e, cs); int dials = 0; for (auto& c : cs) { if (c.dial && c.r.intersects (r)) ++dials; else if (! c.dial && c.r.intersects (r)) bad.add (names[l] + ": display meets " + c.id); }
            if (dials != 0) bad.add (names[l] + ": dials still under the display");
            if (! tap->wanted.load()) bad.add (names[l] + ": the copy is not switched on");
            juce::MidiBuffer m; auto tone = sine (48000.0, 40 * 256, 1000.0, 0.5f);
            for (int i = 0; i < 40; ++i) { juce::AudioBuffer<float> part (tone.getArrayOfWritePointers(), 2, i * 256, 256); p->processBlock (part, m); pump (12); }
            const double hz = (double) sv->getProperties()["peakHz"], db = (double) sv->getProperties()["peakDb"];
            if (hz < 800.0 || hz > 1250.0 || db < -40.0) bad.add (names[l] + ": a 1 kHz tone read as " + juce::String (hz, 0) + " Hz at " + juce::String (db, 1) + " dB");
            auto img = e->createComponentSnapshot (r, false, 1.0f); juce::ignoreUnused (img);
            btn->triggerClick(); pump (30); const int dialsAfter = [&] { std::vector<Ctl> c2; collect (*e, *e, c2); int d = 0; for (auto& c : c2) if (c.dial) ++d; return d; }();
            if (dialsAfter != dialsBefore || sv->isVisible() || tap->wanted.load()) bad.add (names[l] + ": not back to the dials");
        }
        p->setParamNormalised (PhantomProcessor::pid (uEq, "on"), eqOn);
        report (bad.isEmpty() && seen == n - f0, "looks: SPIRIT EQ's spectrum display", "its switch is on the page in " + juce::String (seen) + " of " + juce::String (n - f0) + " looks (" + offered.joinIntoString (", ") + "); on: the display takes the dials' place inside the window, a 1 kHz tone reads as a peak near 1 kHz; off: the dials are back and nothing is copied on the audio thread" + (bad.isEmpty() ? juce::String() : "; wrong: " + bad.joinIntoString ("; ").substring (0, 500)));
    }
    // the blurb line never runs under a scene ghost: on every page of every painted look, at three window sizes, the blurb's
    // rectangle and every scene ghost's rectangle stay apart
    {
        juce::StringArray bad; int pages = 0, withBoth = 0;
        for (int l = fxtheme::kFirstPainted; l < n; ++l) for (int w : { 1100, 1200, 1500 })
        {
            e->setLook (l); e->setSize (w, juce::roundToInt ((float) w * 760.0f / 1200.0f));
            for (int pg = 0; pg < kPages; ++pg)
            {
                showPage (*e, pg); pump (2); ++pages; const auto b = e->blurbAreaForTest(); const auto gs = e->sceneGhostAreasForTest(); if (b.isEmpty() || gs.empty()) continue; ++withBoth;
                for (auto& gr : gs) if (b.intersects (gr)) bad.add (names[l] + " " + pageName (pg) + " at " + juce::String (w) + ": blurb " + b.toString() + " meets ghost " + gr.toString());
                if (b.getWidth() < juce::roundToInt (140.0f * (float) w / 1200.0f)) bad.add (names[l] + " " + pageName (pg) + " at " + juce::String (w) + ": blurb only " + juce::String (b.getWidth()) + " px wide");
            }
        }
        e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);
        report (bad.isEmpty(), "looks: the blurb stays clear of the scene ghosts", juce::String (pages) + " pages over the painted looks at 1100 / 1200 / 1500 wide, " + juce::String (withBoth) + " with a blurb and a scene ghost; touching or too narrow to read: " + (bad.isEmpty() ? juce::String ("none") : juce::String (bad.size()) + ": " + bad.joinIntoString ("; ").substring (0, 600)));
    }
    // drawing time (a budget, with a hard stop): opening a painted look may take a moment once (its pictures are read and scaled
    // once); after that a whole-window repaint and a small animation repaint must be quick. Software drawing, no screen involved.
    for (int l = fxtheme::kFirstPainted; l < n; ++l)
    {
        DrawLimit limit ("the paint-time budget of a painted look", 45);
        e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (uFatten); pump (5);
        auto ms = [&] (juce::Rectangle<int> r) { const double t0 = juce::Time::getMillisecondCounterHiRes(); auto img = e->createComponentSnapshot (r, false, 1.0f); juce::ignoreUnused (img); return juce::Time::getMillisecondCounterHiRes() - t0; };
        const double first = ms (e->getLocalBounds()); double again = 1.0e9, small = 1.0e9, wide = 0.0;
        for (int k = 0; k < 3; ++k) { again = juce::jmin (again, ms (e->getLocalBounds())); small = juce::jmin (small, ms ({ 100, 400, 160, 160 })); }
        e->showUnit (uGate); pump (5); wide = ms (e->getLocalBounds()); double wideAgain = 1.0e9; for (int k = 0; k < 2; ++k) wideAgain = juce::jmin (wideAgain, ms (e->getLocalBounds()));
        report (first < 5000.0 && wide < 5000.0 && again < 250.0 && wideAgain < 250.0 && small < 60.0, "looks: " + names[l] + " paint-time budget", "first paint of the look " + juce::String (juce::roundToInt (first)) + " ms and of a wide page " + juce::String (juce::roundToInt (wide)) + " ms (limit 5000 each); a whole-window repaint after that " + juce::String (again, 1) + " / " + juce::String (wideAgain, 1) + " ms (limit 250); a 160 x 160 repaint " + juce::String (small, 1) + " ms (limit 60)");
    }
    // resized: the smallest and a large window, every look, every page
    for (int big = 0; big < 2; ++big)
    {
        juce::StringArray bad; int pages = 0; juce::Point<int> size (1500, 950);
        for (int l = f0; l < n; ++l) { e->setLook (l); if (big == 0) size = { e->getConstrainer()->getMinimumWidth(), e->getConstrainer()->getMinimumHeight() };
            e->setSize (size.x, l >= fxtheme::kFirstPainted ? juce::roundToInt ((float) size.x * 760.0f / 1200.0f) : size.y);
            for (int pg = 0; pg < kPages; ++pg) { showPage (*e, pg); pump (2); ++pages; for (auto& b : checkPage (*e).bad) bad.add (names[l] + " " + pageName (pg) + ": " + b); } }
        report (bad.isEmpty(), juce::String (big == 0 ? "looks: resized to the smallest window" : "looks: resized to 1500 wide"), juce::String (pages) + " pages over " + juce::String (n - f0) + " looks; overlapping / outside / covered: " + (bad.isEmpty() ? juce::String ("none") : juce::String (bad.size()) + ": " + bad.joinIntoString ("; ").substring (0, juce::SystemStats::getEnvironmentVariable ("PHANTOM_LONG", "").isNotEmpty() ? 200000 : 600)));
    }
    // the look button: one press per look goes round the whole list and comes back; the layout is the same as before the trip;
    // the sound and the saved state never change with the look
    {
        e->setLook (f0); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (uFatten); pump (5);
        juce::MemoryBlock before; p->getStateInformation (before); const auto area0 = checkPage (*e).area;
        std::function<juce::Button* (juce::Component*)> findLook = [&] (juce::Component* c) -> juce::Button* { if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getTooltip().startsWith ("Look:")) return b; for (auto* k : c->getChildren()) if (auto* r = findLook (k)) return r; return nullptr; };
        auto* btn = findLook (e); juce::StringArray seen; bool ok = btn != nullptr, statesSame = true;
        for (int i = 0; ok && i < n - f0; ++i)
        {
            btn = findLook (e); if (btn == nullptr) { ok = false; break; }
            btn->triggerClick(); pump (40); seen.add (e->lookTitle (e->currentLook()));
            juce::MemoryBlock now; p->getStateInformation (now); if (now != before) statesSame = false;
            juce::PropertiesFile props (prefsFile(), {}); if (props.getIntValue ("look", -1) != e->currentLook()) ok = false;
        }
        bool oldSeen = false; for (int l = 0; l < f0; ++l) if (seen.contains (names[l])) oldSeen = true;
        const bool back = e->currentLook() == f0, allSeen = [&] { juce::StringArray u (seen); u.removeDuplicates (false); return u.size() == n - f0; }() && ! oldSeen;
        const bool sameLayout = checkPage (*e).area == area0;
        report (ok && back && allSeen && sameLayout && statesSame, "looks: the look button's round trip", "pressed " + juce::String (n - f0) + " times from " + names[f0] + ": " + seen.joinIntoString (" -> ") + "; every look offered was shown and no other: " + (allSeen ? "yes" : "NO") + "; back on " + names[f0] + ": " + (back ? "yes" : "NO") + "; layout as before: " + (sameLayout ? "yes" : "NO") + "; saved state unchanged by any look: " + (statesSame ? "yes" : "NO") + "; preference saved each time: " + (ok ? "yes" : "NO"));
    }
    // a painted look is remembered like the others (the look is the one thing a new window may remember), and a new instance is
    // still brand new: factory defaults, first page, whatever the look
    {
        const int cel = names.indexOf ("CEL"); e->setLook (cel);
        PhantomProcessor q; std::unique_ptr<juce::AudioProcessorEditor> e2 (q.createEditor()); auto* pe2 = dynamic_cast<PhantomEditor*> (e2.get());
        bool defaults = true; for (auto* prm : q.getParameters()) if (std::abs (prm->getValue() - prm->getDefaultValue()) > 1.0e-6f) defaults = false;
        const bool remembered = pe2->currentLook() == cel, size = e2->getWidth() == PhantomProcessor::kDefaultWidth && e2->getHeight() == PhantomProcessor::kDefaultHeight;
        report (defaults && remembered && size, "looks: fresh start in a painted look", juce::String ("new instance opens in the last chosen look (") + pe2->lookTitle (pe2->currentLook()) + "), at " + juce::String (e2->getWidth()) + " x " + juce::String (e2->getHeight()) + ", every parameter at its default: " + (defaults ? "yes" : "NO"));
        e2.reset(); e->setLook (f0);
    }
    // a look that is no longer offered: a preference saved by an older version naming BLACK, PURPLE, LIGHT or GHOST, a painted
    // look that is gone, a number out of range, or no preference at all: the window opens in DAWN with no error and draws.
    // Asking for an old look in code gives DAWN too. (The look was never part of a project or a preset: only this preference.)
    {
        juce::StringArray got; bool ok = true; const auto keep = prefsFile().existsAsFile() ? prefsFile().loadFileAsString() : juce::String();
        auto openWith = [&] (int look, const juce::String& name, bool noFile)
        {
            prefsFile().deleteFile();
            if (! noFile) { juce::PropertiesFile props (prefsFile(), {}); props.setValue ("look", look); if (name.isNotEmpty()) props.setValue ("lookName", name); props.saveIfNeeded(); }
            PhantomProcessor q; std::unique_ptr<juce::AudioProcessorEditor> e3 (q.createEditor()); auto* pe3 = dynamic_cast<PhantomEditor*> (e3.get()); pump (5);
            DrawLimit limit ("drawing a window opened from an old look preference", 40); auto img = pe3->createComponentSnapshot ({ 0, 0, 300, 200 }, false, 1.0f);
            return img.isValid() && pe3->currentLook() >= pe3->firstLook() ? pe3->lookTitle (pe3->currentLook()) : juce::String ("NOT SHOWN");
        };
        struct Case { const char* what; int look; const char* name; bool noFile; };
        for (auto c : { Case { "no preference yet", 0, "", true }, Case { "BLACK", 0, "", false }, Case { "PURPLE", 1, "", false }, Case { "LIGHT", 2, "", false }, Case { "GHOST", 3, "", false },
                        Case { "a painted look that is gone", fxtheme::kFirstPainted + 1, "sepia", false }, Case { "a number out of range", 99, "", false } })
        { const auto t = openWith (c.look, c.name, c.noFile); got.add (juce::String (c.what) + " -> " + t); if (t != "DAWN") ok = false; }
        const auto kept = openWith (fxtheme::kFirstPainted, "neon", false); if (kept != "NEON") ok = false;   // (and a painted look is still remembered by its name)
        juce::StringArray direct; for (int l = 0; l < fxtheme::kFirstPainted; ++l) { e->setLook (l); direct.add (e->lookTitle (e->currentLook())); if (e->currentLook() < f0) ok = false; }
        if (keep.isNotEmpty()) prefsFile().replaceWithText (keep); else prefsFile().deleteFile();
        e->setLook (f0);
        report (ok, "looks: a look no longer offered opens as DAWN", "a new window with the saved preference: " + got.joinIntoString ("; ") + "; a preference naming NEON -> " + kept + "; asking for looks 0 to 3 in code shows " + direct.joinIntoString (", "));
    }
    // the sound: the same input gives the same output samples with a painted look showing and animating as with no window at
    // all: EXACTLY, to the last bit. The per-unit level meter the ghosts dance to only reads the finished block (a peak search,
    // one atomic store); it changes no sample, no block size, no floating-point mode and no timing the engines can see.
    // If a machine ever shows a difference, the runs below say where it comes from: the same processor run twice without a
    // window (the engines' own repeatability), with the sound buffer at another memory address (code whose rounding depends on
    // where its data sits), with the first look's window standing still, with the last look's window standing still (meter off) and animating.
    {
        auto run = [] (int mode, int shift = 0)   // 0 no window, 1 the first look's window (DAWN), not animating, 2 the last look's window (NEON), animating, 3 the last look's window, not animating
        {
            PhantomProcessor q; q.prepareToPlay (48000.0, 256); for (int u : { uFatten, uEcho, uHalls }) q.setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> ed2; PhantomEditor* pe = nullptr;
            if (mode > 0) { ed2.reset (q.createEditor()); pe = dynamic_cast<PhantomEditor*> (ed2.get()); pe->setLook (mode >= 2 ? pe->numLooks() - 1 : pe->firstLook()); pe->forceAnim = mode == 2; }
            const auto src = noise (48000, 0.3f, 77); juce::AudioBuffer<float> buf (2, 48000 + 16); juce::MidiBuffer m;   // (the block the processor is handed starts "shift" samples into its own memory)
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::copy (buf.getWritePointer (c) + shift, src.getReadPointer (c), 48000);
            for (int i = 0; i < 48000 / 256; ++i) { float* ch[2] { buf.getWritePointer (0) + shift + i * 256, buf.getWritePointer (1) + shift + i * 256 }; juce::AudioBuffer<float> part (ch, 2, 256); q.processBlock (part, m); if (pe != nullptr && mode == 2 && i % 10 == 0) pe->animStepForTest(); }
            juce::AudioBuffer<float> out (2, 48000); for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, buf, c, shift, 48000);
            return out;
        };
        auto diff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) { float w = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) w = juce::jmax (w, std::abs (a.getSample (c, i) - b.getSample (c, i))); return w; };
        const auto a = run (0), a2 = run (0), sh = run (0, 3), d = run (1), still = run (3), b = run (2);
        const float own = diff (a, a2), moved = diff (a, sh), drawn = diff (a, d), stillD = diff (a, still), paintedD = diff (a, b);
        const float engines = juce::jmax (own, moved);   // what this machine's engines do on their own, no window anywhere
        juce::String why = "exact";
        if (paintedD > 0.0f || drawn > 0.0f || stillD > 0.0f)
            why = own > 0.0f ? "the engines are not bit-repeatable on this machine even with no window (two identical runs differ): not the looks"
                : moved > 0.0f ? "the engines' rounding depends on where their sound buffer sits in memory on this machine (the same run at another address differs, no window): not the looks"
                : drawn > 0.0f || stillD > 0.0f ? "an open window changes it even standing still with its level meter off"
                : "only the animating look changes it: its level meter";
        // exact, unless the machine's engines themselves are shown not to be (then the windows must add nothing beyond that)
        const bool ok = (paintedD == 0.0f && drawn == 0.0f && stillD == 0.0f) || (engines > 0.0f && paintedD <= engines && drawn <= engines && stillD <= engines);
        report (ok, "looks: the sound is untouched", "1 s through FATTEN + ECHO + HALLS, biggest sample difference from the no-window run: no window again " + juce::String (own, 9) + "; no window, buffer at another address " + juce::String (moved, 9) + "; DAWN's window standing still " + juce::String (drawn, 9) + "; NEON's window standing still " + juce::String (stillD, 9) + "; NEON's window animating " + juce::String (paintedD, 9) + " (" + why + ")");
    }
    ed.reset();
    if (savedPrefs.isNotEmpty()) prefsFile().replaceWithText (savedPrefs); else prefsFile().deleteFile();
}

// "lookscost": the animation: 12 frames a second on the UI timer, only the changed areas repainted; the cost of one second of it, per painted look
static void costChecks()
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { report (true, "looks: animation cost", "no display: skipped"); return; }
    const auto savedPrefs = prefsFile().existsAsFile() ? prefsFile().loadFileAsString() : juce::String();
    auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->setVisible (true);
    const int n = e->numLooks(); juce::StringArray names; for (int l = 0; l < n; ++l) names.add (e->lookTitle (l));
    for (int l = fxtheme::kFirstPainted; l < n; ++l)
    {
        std::printf ("  (measuring %s)\n", names[l].toRawUTF8()); std::fflush (stdout);
        DrawLimit limit ("the animation cost of a painted look", 90);
        e->setLook (l); e->setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->showUnit (uFatten); e->forceAnim = true; pump (20);
        { auto warm = e->createComponentSnapshot (e->getLocalBounds(), false, 1.0f); juce::ignoreUnused (warm); }
        e->dirtyLog.clear(); const int t0 = e->animTicks(); double paintMs = 0; juce::int64 px = 0; const int secs = 4;
        const double began = juce::Time::getMillisecondCounterHiRes(); int done = 0;
        for (int i = 0; i < 12 * secs; ++i)
        {
            if (juce::Time::getMillisecondCounterHiRes() - began > 60000.0) break;   // (never more than a minute per look)
            ++done;
            e->animStepForTest(); juce::RectangleList<int> rl; for (auto& r : e->dirtyLog) rl.add (r.getIntersection (e->getLocalBounds())); e->dirtyLog.clear();
            const double a = juce::Time::getMillisecondCounterHiRes();
            for (auto& r : rl) { if (r.isEmpty()) continue; auto img = e->createComponentSnapshot (r, false, 1.0f); px += (juce::int64) r.getWidth() * r.getHeight(); }
            paintMs += juce::Time::getMillisecondCounterHiRes() - a;
        }
        e->forceAnim = false; const double perSec = paintMs / secs, pct = perSec / 10.0;
        const double full = (double) px / (12.0 * secs) / ((double) e->getWidth() * e->getHeight()) * 100.0;
        report (done == 12 * secs && e->animTicks() - t0 == 12 * secs && pct < 25.0, "looks: " + names[l] + " animation cost", juce::String (12) + " ticks a second; repainting what changed takes " + juce::String (perSec, 1) + " ms of each second (" + juce::String (pct, 1) + " % of one core, software drawing), on average " + juce::String (full, 1) + " % of the window per tick; nothing runs on the audio thread");
    }
    ed.reset();
    if (savedPrefs.isNotEmpty()) prefsFile().replaceWithText (savedPrefs); else prefsFile().deleteFile();
}

// "lookprobe": one traced paint of each look with its time and the process's peak memory (a quick health check of a machine's
// drawing before the long checks run on it)
static int lookprobe()
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf ("no display\n"); return 0; }
    auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->setVisible (true);
    auto rss = [] { struct rusage ru; getrusage (RUSAGE_SELF, &ru); return (double) ru.ru_maxrss; };
    int slow = 0;
    for (int l = e->firstLook(); l < e->numLooks(); ++l)
    {
        std::printf ("probe %s: set look\n", e->lookTitle (l).toRawUTF8()); std::fflush (stdout);
        DrawLimit limit ("the look probe (one look)", 30);
        double t0 = juce::Time::getMillisecondCounterHiRes(); e->setLook (l); e->showUnit (uFatten); const double tl = juce::Time::getMillisecondCounterHiRes() - t0;
        double tp[3] {};
        for (int k = 0; k < 3; ++k) { t0 = juce::Time::getMillisecondCounterHiRes(); auto img = e->createComponentSnapshot (k == 2 ? juce::Rectangle<int> (100, 400, 160, 160) : e->getLocalBounds(), false, 1.0f); tp[k] = juce::Time::getMillisecondCounterHiRes() - t0; }
        std::printf ("probe %s: look set in %.0f ms; first full paint %.0f ms, second %.0f ms, a 160 x 160 area %.0f ms; peak memory %.0f (ru_maxrss)\n", e->lookTitle (l).toRawUTF8(), tl, tp[0], tp[1], tp[2], rss()); std::fflush (stdout);
        if (tp[1] > 1500.0) ++slow;
        // the painted unit visuals on this machine's own drawing (CoreGraphics on a Mac): one picture of the stage at 1x and at 2x (Retina)
        std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* { if (c->getName() == "UNIT VISUAL" && c->isVisible()) return c; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = find (k)) return r; return nullptr; };
        juce::String vis;
        for (int u : { uFatten, uWreck, uOtt, uEcho })
        {
            e->showUnit (u); pump (5); auto* pn = find (e); if (pn == nullptr) { vis << unitDef (u).shortName << " none  "; continue; }
            double ms[2] {}; for (int z = 0; z < 2; ++z) { auto first = pn->createComponentSnapshot (pn->getLocalBounds(), true, (float) (z + 1)); juce::ignoreUnused (first); t0 = juce::Time::getMillisecondCounterHiRes(); for (int k = 0; k < 6; ++k) { auto img = pn->createComponentSnapshot (pn->getLocalBounds(), true, (float) (z + 1)); juce::ignoreUnused (img); } ms[z] = (juce::Time::getMillisecondCounterHiRes() - t0) / 6.0; }
            vis << unitDef (u).shortName << " " << juce::String (ms[0], 1) << " / " << juce::String (ms[1], 1) << " ms  ";
        }
        std::printf ("probe %s: unit visual, one picture at 1x / 2x: %s\n", e->lookTitle (l).toRawUTF8(), vis.toRawUTF8()); std::fflush (stdout);
    }
    e->setLook (e->firstLook()); std::printf ("probe done, %d slow\n", slow); return slow;
}

// "visualshots <folder>": the unit visuals as stills: each unit that has one, in every look offered, with its first control
// at 0 / 50 / 100 % (the rest at their defaults), quiet and loud; the whole page in each look; a clip's frames in the first look
static int visualshots (const juce::File& dir)
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf ("no display\n"); return 0; }
    dir.createDirectory(); auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); const int saved = e->currentLook(); int n = 0;
    auto save = [&] (const juce::Image& im, const juce::String& name) { juce::PNGImageFormat png; juce::FileOutputStream os (dir.getChildFile (name + ".png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (im, os); ++n; } };
    for (int u = 0; u < kNumUnits; ++u)
    {
        auto v = visual::make (u); if (v == nullptr) continue; const auto ids = v->mapped(); const juce::String un = juce::String (unitDef (u).shortName).toLowerCase();
        visual::Feed def; for (size_t i = 0; i < ids.size(); ++i) if (auto* rp = p->apvts.getParameter (PhantomProcessor::pid (u, ids[i]))) def.v[i] = rp->getDefaultValue();
        for (int l = e->firstLook(); l < e->numLooks(); ++l)
        {
            e->setLook (l); const juce::String lk = e->lookTitle (l).toLowerCase(); const auto st = visual::styleFor (u);
            // one sheet: columns = the first control at 0 / 50 / 100 %, rows = quiet / loud
            juce::Image sheet (juce::Image::RGB, 3 * 440 + 40, 2 * 240 + 60, true, juce::SoftwareImageType()); { juce::Graphics g (sheet); g.fillAll (fxtheme::pal().window);
              for (int row = 0; row < 2; ++row) for (int c = 0; c < 3; ++c)
              {
                  auto f = def; f.v[0] = (float) c * 0.5f; f.level = row == 0 ? 0.04f : 0.85f; f.hit = row == 0 ? 0.0f : 0.8f; f.t = 2.0 + c * 0.3;
                  const juce::Rectangle<float> r (10.0f + (float) c * 440.0f, 30.0f + (float) row * 250.0f, 430.0f, 230.0f);
                  g.setColour (st.back); g.fillRoundedRectangle (r, 9.0f); { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt()); v->paint (g, r.reduced (8.0f), f, st); }
                  g.setColour (fxtheme::pal().ink); g.setFont (juce::Font (juce::FontOptions (13.0f))); g.drawText (juce::String (unitDef (u).shortName) + "  " + ids[0] + " " + juce::String (c * 50) + " %  " + (row == 0 ? "quiet" : "loud"), r.withY (r.getY() - 22.0f).withHeight (20.0f), juce::Justification::centredLeft, false);
              } }
            save (sheet, un + "-" + lk + "-sheet");
            e->setUnitVisuals (true); e->showUnit (u); pump (80); save (e->createComponentSnapshot (e->getLocalBounds(), false, 1.0f), un + "-" + lk + "-page");
        }
        // a short clip's frames (the first look): the first control swept up and back while a beat plays, 4 s at 12 pictures a second
        const auto st = (e->setLook (e->firstLook()), visual::styleFor (u));
        for (int k = 0; k < 48; ++k)
        {
            auto f = def; f.v[0] = 0.5f - 0.5f * std::cos (k / 48.0f * 6.2832f); const int beat = k % 6; f.hit = beat == 0 ? 1.0f : std::pow (0.6f, (float) beat); f.level = 0.25f + 0.6f * f.hit; f.t = k / 12.0;
            juce::Image im (juce::Image::RGB, 560, 300, true, juce::SoftwareImageType()); { juce::Graphics g (im); g.fillAll (st.back); v->paint (g, { 8.0f, 8.0f, 544.0f, 284.0f }, f, st); }
            save (im, "frames-" + un + "-" + juce::String (k).paddedLeft ('0', 3));
        }
    }
    e->setLook (saved); std::printf ("%d visual stills in %s\n", n, dir.getFullPathName().toRawUTF8()); return 0;
}

// "freerects <look>": on each wide unit page, every control's rectangle and the drawn editors' areas (for agreeing visual stages)
static int freerects (const juce::String& lookId)
{
    auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);
    for (int l = e->firstLook(); l < e->numLooks(); ++l) if (e->lookTitle (l).equalsIgnoreCase (lookId)) e->setLook (l);
    for (int u : { uGate, uGrid, uHaunt, uWarp, uCrackle, uRetro, uEq })
    {
        e->showUnit (u); pump (20); std::vector<Ctl> cs; collect (*e, *e, cs); std::printf ("UNIT %s\n", unitDef (u).shortName);
        for (auto& c : cs) std::printf ("C %d %d %d %d %s\n", c.r.getX(), c.r.getY(), c.r.getWidth(), c.r.getHeight(), c.id.toRawUTF8());
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* k : c->getChildren()) { if (! k->isVisible()) continue; const juce::String tn = typeid (*k).name(); if (tn.containsIgnoreCase ("Editor") || tn.containsIgnoreCase ("Panel") || tn.containsIgnoreCase ("Lane") || tn.containsIgnoreCase ("Pad") || tn.containsIgnoreCase ("Spectrum")) { const auto r = e->getLocalArea (k, k->getLocalBounds()); std::printf ("E %d %d %d %d %s\n", r.getX(), r.getY(), r.getWidth(), r.getHeight(), tn.toRawUTF8()); } walk (k); } }; walk (e);
    }
    return 0;
}
// "pvparams": every painted scene's controls against the unit's parameters (type, range): what the scene will be fed
static int pvparams()
{
    auto p = demoProcessor();
    for (int u = 0; u < kNumUnits; ++u)
    {
        auto sc = pv::makeScene (u); if (sc == nullptr) continue; std::printf ("%s:", unitDef (u).shortName);
        for (auto& n : sc->controls())
        {
            auto* rp = p->apvts.getParameter (PhantomProcessor::pid (u, n));
            if (rp == nullptr) std::printf (" %s=MISSING", n.c_str());
            else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp)) std::printf (" %s=menu%d", n.c_str(), c->choices.size());
            else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) std::printf (" %s=switch", n.c_str());
            else if (auto* ip = dynamic_cast<juce::AudioParameterInt*> (rp)) std::printf (" %s=int%d..%d", n.c_str(), ip->getRange().getStart(), ip->getRange().getEnd());
            else { auto* f = dynamic_cast<juce::AudioParameterFloat*> (rp); std::printf (" %s=%g..%g", n.c_str(), f ? f->range.start : 0.0f, f ? f->range.end : 1.0f); }
        }
        std::printf ("\n");
    }
    return 0;
}
// "pvstill <unit> <look> <vals> <time> <loud> <out.png>"
static int pvstill (const juce::StringArray& a)
{
    if (a.size() < 6) return 1; auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    int u = -1; for (int i = 0; i < kNumUnits; ++i) if (juce::String (unitDef (i).shortName).equalsIgnoreCase (a[0]) || juce::String (unitDef (i).key).equalsIgnoreCase (a[0])) u = i;
    auto sc = u >= 0 ? pv::makeScene (u) : nullptr; auto* L = lookById (a[1]); if (sc == nullptr || L == nullptr) { std::printf ("no scene / look\n"); return 1; }
    juce::StringPairArray vals; for (auto kv : juce::StringArray::fromTokens (a[2], "&", "")) if (kv.contains ("=")) vals.set (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false));
    std::map<std::string, juce::Image> cache; int paths = 0; const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto im = pvRender (*L, *sc, vals, a[3].getDoubleValue(), a[4].getDoubleValue(), true, cache, &paths);
    const double cold = juce::Time::getMillisecondCounterHiRes() - t0; double worst = 0, sum = 0;
    for (int i = 0; i < 24; ++i) { const double t1 = juce::Time::getMillisecondCounterHiRes(); auto im1 = pvRender (*L, *sc, vals, a[3].getDoubleValue() + i / 24.0, a[4].getDoubleValue(), true, cache, nullptr, 1.0); juce::ignoreUnused (im1); const double d = juce::Time::getMillisecondCounterHiRes() - t1; worst = juce::jmax (worst, d); sum += d; }
    std::printf ("%d paths, 1x %.2f ms avg %.2f worst (first 2x picture %.0f ms)\n", paths, sum / 24, worst, cold);
    if (pv::lookFor (L->str ("visual.style")) == pv::Look::dawn && sc->hasDawn()) { juce::Image bg (juce::Image::ARGB, im.getWidth(), im.getHeight(), true); juce::Graphics g (bg); g.fillAll (juce::Colour (0xff1c3a3c)); g.drawImageAt (im, 0, 0); im = bg; }   // (as the painter's dawnpic shows it)
    juce::PNGImageFormat png; juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (a[5]); out.deleteFile(); juce::FileOutputStream os (out); png.writeImageToStream (im, os); return 0;
}

// "pvpages <folder>": every painted unit visual on its page in DAWN, CEL and NEON (the whole window, JPEG), and whether the
// stage meets any control
static int pvpages (const juce::File& dir)
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf ("no display\n"); return 0; }
    dir.createDirectory(); auto p = demoProcessor(); std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->setUnitVisuals (true); int bad = 0;
    std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* { if (c->getName() == "UNIT VISUAL" && c->isVisible()) return c; for (auto* k : c->getChildren()) if (k->isVisible()) if (auto* r = find (k)) return r; return nullptr; };
    for (int l = e->firstLook(); l < e->numLooks(); ++l)
    {
        e->setLook (l); const juce::String lk = e->lookTitle (l).toLowerCase();
        for (int u = 0; u < kNumUnits; ++u)
        {
            if (pv::makeScene (u) == nullptr) continue; e->showUnit (u); pump (60);
            auto* pn = find (e); juce::String note = "no pane";
            if (pn != nullptr) { const auto r = e->getLocalArea (pn, pn->getLocalBounds()); note = r.toString(); std::vector<Ctl> cs; collect (*e, *e, cs); for (auto& c : cs) if (c.r.intersects (r)) { note << " MEETS " << c.id; ++bad; } }
            else ++bad;
            std::printf ("%s %s: %s\n", lk.toRawUTF8(), unitDef (u).shortName, note.toRawUTF8());
            if (u == uFatten || u == uWreck || u == uOtt || u == uSpace || pvWideUnit (u))
            { auto img = e->createComponentSnapshot (e->getLocalBounds(), false, 1.0f); juce::JPEGImageFormat jpg; jpg.setQuality (0.85f); juce::File f = dir.getChildFile (lk + "-" + juce::String (unitDef (u).shortName).toLowerCase() + "-page.jpg"); f.deleteFile(); juce::FileOutputStream os (f); jpg.writeImageToStream (img, os);
              if (pn != nullptr && (int) pn->getProperties().getWithDefault ("pvMode", 0) >= 2) { auto pi = pn->createComponentSnapshot (pn->getLocalBounds(), false, 1.0f); juce::PNGImageFormat png; juce::File f2 = dir.getChildFile (lk + "-" + juce::String (unitDef (u).shortName).toLowerCase() + "-picture.png"); f2.deleteFile(); juce::FileOutputStream o2 (f2); png.writeImageToStream (pi, o2); } }
        }
    }
    e->setLook (e->firstLook()); std::printf ("%d problems\n", bad); return bad;
}

// "lookshots <folder>": every page in every painted look
static int lookshots (const juce::File& dir, bool jpeg = false)
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf ("no display: no screenshots\n"); return 0; }
    dir.createDirectory(); auto p = demoProcessor(); p->addRoute ({ 0, uWreck, 1, 0.5f, true });
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* e = dynamic_cast<PhantomEditor*> (ed.get());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight); e->forceAnim = true;
    auto snap = [&] (const juce::String& name) { DrawLimit limit ("a look screenshot", 40); pump (60); for (int i = 0; i < 3; ++i) e->animStepForTest(); std::printf ("  shot %s\n", name.toRawUTF8()); std::fflush (stdout); auto img = e->createComponentSnapshot (e->getLocalBounds(), false, 1.0f); juce::PNGImageFormat png; juce::JPEGImageFormat jpg; jpg.setQuality (0.88f); juce::FileOutputStream os (dir.getChildFile (name + (jpeg ? ".jpg" : ".png"))); if (os.openedOk()) { os.setPosition (0); os.truncate(); if (jpeg) jpg.writeImageToStream (img, os); else png.writeImageToStream (img, os); } };
    const int first = fxtheme::kFirstPainted; int shotsN = 0;
    for (int l = first; l < e->numLooks(); ++l)
    {
        e->setLook (l); const juce::String lk = e->lookTitle (l).toLowerCase().replaceCharacter (' ', '-');
        for (int u : { uEcho, uCrystal, uWreck, uChorus }) p->setParamNormalised (PhantomProcessor::pid (u, "on"), 0.0f);
        spatialtest::setP (*p, "fx_spatial_on", 0.0f);
        for (int pg = 0; pg < kPages - 1; ++pg) { showPage (*e, pg); snap (lk + "-" + juce::String (pg + 1).paddedLeft ('0', 2) + "-" + pageName (pg)); ++shotsN; }
        // SPATIAL as in Brandon's own example: reverb below, delay above, distortion on the right, crystal on the left, with recorded paths
        for (int u : { uEcho, uCrystal, uWreck, uChorus }) p->setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f);
        spatialtest::setP (*p, "fx_spatial_on", 1.0f);
        spatialtest::place (*p, uHalls, 0, -70, 6); spatialtest::place (*p, uEcho, 20, 70, 8); spatialtest::place (*p, uWreck, 90, 0, 3); spatialtest::place (*p, uCrystal, -90, 10, 4); spatialtest::place (*p, uChorus, 150, 20, 12);
        std::vector<SpPoint> path; for (int i = 0; i <= 160; ++i) { const float t = i / 160.0f; path.push_back ({ i * 0.1, -90.0f + 180.0f * t, 10.0f + 30.0f * std::sin (t * 6.28f), 4.0f + 3.0f * std::sin (t * 3.14f), i == 0 }); }
        p->spatialAuto.setPath (uCrystal, path); p->spatialAuto.armed[uCrystal] = true; p->spatialAuto.mode = spTouch;
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m; for (int i = 0; i < 8; ++i) { buf.clear(); p->processBlock (buf, m); }
        e->showSpatial (uWreck); snap (lk + "-" + juce::String (kPages).paddedLeft ('0', 2) + "-spatial"); ++shotsN;
        p->spatialAuto.clearAll(); for (int u : { uHalls, uEcho, uWreck, uCrystal, uChorus }) spatialtest::place (*p, u, 0, 0, 0);
        e->showTab (0);
    }
    e->setLook (e->firstLook());   // (leave the look preference as a fresh install has it)
    std::printf ("%d look screenshots in %s\n", shotsN, dir.getFullPathName().toRawUTF8());
    return 0;
}
} // namespace lookstest
