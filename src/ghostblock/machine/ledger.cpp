/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the preset ledger and the brass dialogs.
 *
 * The ledger is the plugin's preset menu as a painted board hung under the nameplate (JUCE-NOTES.md, section 11):
 * folder tabs down the left, one brass slat per sound, thirteen to a page. It lists exactly what the first editor's
 * menus listed: every factory folder, YOUR SOUNDS, INIT; for a MORPH corner also THIS SOUND, A COPY OF THIS SOUND and
 * EMPTY. SAVE's name box and "type a value" are the same JUCE AlertWindows as before, dressed as brass plates.
 */
#include "ghostblock/machine/machine-impl.h"
#include "ghostblock/machine/sizing.h"

namespace ghostblock::p2::machine
{
static const int kRows = 13, kTabRows = 14;
static const float kRowH = 34.0f;

// ============================================================================================== what it lists
static bool earFolder(const std::string &cat) { return cat != "MPE" && cat != "MTS-ESP-MPE" && cat != "Templates"; }

void MachineEditor::Impl::ledgerItems()
{
    ledger.items.clear();
    if (ledger.tabs.isEmpty())
        return;
    const auto tab = ledger.tabs[juce::jlimit(0, ledger.tabs.size() - 1, ledger.tab)];
    if (ledger.mode == Ledger::FxPick)
    { // the FX page: the effects of this folder (Spectral Corruptor's own names and folders)
        for (auto &t : spectral::types())
            if (t.index != 0 && juce::String(juce::CharPointer_UTF8(t.category.c_str())) == tab)
            {
                LedgerItem it;
                it.kind = LedgerItem::Fx;
                it.fxIndex = t.index;
                it.text = juce::String(juce::CharPointer_UTF8(t.name.c_str()));
                ledger.items.push_back(it);
            }
        return;
    }
    if (ledger.mode == Ledger::CornerPick)
    {
        LedgerItem a, b;
        a.kind = LedgerItem::This;
        a.text = "THIS SOUND";
        a.hint = "the one you shape on GHOSTS";
        b.kind = LedgerItem::Copy;
        b.text = "A COPY OF THIS SOUND";
        b.hint = "keeps it as it is now";
        ledger.items.push_back(a);
        ledger.items.push_back(b);
    }
    auto &mgr = ledger.mode == Ledger::CornerPick ? *cornerPm : *pm;
    if (tab == "YOUR SOUNDS")
    {
        mgr.rescanUserPresets();
        for (auto &up : mgr.userPatches)
        {
            LedgerItem it;
            it.kind = LedgerItem::User;
            it.user = up;
            it.text = juce::String(juce::CharPointer_UTF8(ss::presets::PresetManager::stripPresetExtension(up.filename().u8string()).c_str()));
            ledger.items.push_back(it);
        }
        if (mgr.userPatches.empty())
        {
            LedgerItem it;
            it.kind = LedgerItem::None;
            it.text = ledger.mode == Ledger::CornerPick ? "No saved sounds yet (SAVE one first)" : "No saved sounds yet (use SAVE)";
            ledger.items.push_back(it);
        }
        return;
    }
    const auto cat = tab.toStdString();
    auto f = mgr.factoryPatchNames.find(cat);
    if (f == mgr.factoryPatchNames.end())
        for (auto &kv : mgr.factoryPatchNames) // (the tabs are upper case)
            if (juce::String(kv.first).toUpperCase() == tab.toUpperCase())
                f = mgr.factoryPatchNames.find(kv.first);
    if (f != mgr.factoryPatchNames.end())
        for (auto &n : f->second)
        {
            LedgerItem it;
            it.kind = LedgerItem::Factory;
            it.cat = f->first;
            it.file = n;
            it.text = juce::String(juce::CharPointer_UTF8(ss::presets::PresetManager::stripPresetExtension(n).c_str()));
            ledger.items.push_back(it);
        }
}

int MachineEditor::Impl::ledgerPages() const { return juce::jmax(1, ((int)ledger.items.size() + kRows - 1) / kRows); }

static bool isCurrent(MachineEditor::Impl &I, const LedgerItem &it)
{
    if (it.kind == LedgerItem::None)
        return false;
    if (I.ledger.mode == Ledger::FxPick)
        return I.ed.spectral && it.kind == LedgerItem::Fx && I.ed.spectral->slotType(I.ledger.corner) == it.fxIndex;
    if (I.ledger.mode == Ledger::CornerPick)
    {
        if (!I.ed.morph)
            return false;
        const int k = I.ed.morph->kind[(size_t)I.ledger.corner].load();
        if (it.kind == LedgerItem::This)
            return k == Morph::THIS_SOUND;
        if (it.kind == LedgerItem::Copy)
            return false;
        return k == Morph::OWN && it.text == juce::String(juce::CharPointer_UTF8(I.ed.morph->names[(size_t)I.ledger.corner].c_str()));
    }
    return it.text == juce::String(juce::CharPointer_UTF8(I.P.name));
}

FRect MachineEditor::Impl::ledgerArea() const
{ // the board with its roller and chains (the sprite is larger than the ledger's own rect)
    return FRect(ledger.x - 14.0f, ledger.y - 80.0f, 452.0f, 596.0f);
}

void MachineEditor::Impl::touchLedger() { sceneDirty.mark(ledgerArea().expanded(8)); }

void MachineEditor::Impl::openLedger(Ledger::Mode m, int corner)
{
    subToggle(false); // (one board at a time)
    const auto &Lg = as->layout["ledger"];
    const auto rc = rectOf(Lg["rect"]);
    if (ledger.mode != Ledger::Closed)
        touchLedger();
    ledger.mode = m;
    ledger.corner = corner;
    ledger.pageIx = 0;
    ledger.tabScroll = 0;
    ledger.hoverTab = ledger.hoverSlat = ledger.hoverFoot = -1;
    ledger.tabs.clear();
    if (m == Ledger::Yours)
        ledger.tabs.add("YOUR SOUNDS");
    else if (m == Ledger::FxPick)
    {
        for (auto &cat : spectral::categories())
            ledger.tabs.add(juce::String(juce::CharPointer_UTF8(cat.c_str())));
    }
    else
    {
        // the folders you browse by ear first (the first editor's chips), then your own, then the technical ones
        for (auto &[cat, names] : pm->factoryPatchNames)
            if (!names.empty() && earFolder(cat))
                ledger.tabs.add(juce::String(cat));
        ledger.tabs.add("YOUR SOUNDS");
        for (auto &[cat, names] : pm->factoryPatchNames)
            if (!names.empty() && !earFolder(cat))
                ledger.tabs.add(juce::String(cat));
    }
    int want = 0;
    if (m == Ledger::Sounds)
    {
        const auto folder = currentFolder();
        want = juce::jmax(0, ledger.tabs.indexOf(folder));
    }
    else if (m == Ledger::CornerPick)
        want = juce::jlimit(0, ledger.tabs.size() - 1, ledger.tab);
    else if (m == Ledger::FxPick && ed.spectral)
    { // open on the folder of the slot's effect
        const auto &t = spectral::types()[(size_t)ed.spectral->slotType(corner)];
        want = juce::jmax(0, ledger.tabs.indexOf(juce::String(juce::CharPointer_UTF8(t.category.c_str()))));
    }
    ledger.tab = want;
    ledger.x = rc.getX();
    ledger.y = rc.getY();
    if (m == Ledger::CornerPick) // hang it clear of the corner it belongs to
        ledger.x = numOf(Lg["corner_x"][corner % 2], rc.getX());
    ledgerItems();
    // open on the page that holds the loaded sound
    for (int i = 0; i < (int)ledger.items.size(); ++i)
        if (isCurrent(*this, ledger.items[(size_t)i]))
        {
            ledger.pageIx = i / kRows;
            break;
        }
    const int n = ledger.tabs.size();
    if (n > kTabRows && ledger.tab >= kTabRows - 1 && ledger.tabs[ledger.tab] != "YOUR SOUNDS")
        ledger.tabScroll = juce::jmin(n - kTabRows, ledger.tab - (kTabRows - 2));
    ledger.openedAt = clock;
    ledger.reveal = 0;
    ledger.revealDrawn = -1;
    env.bump(0.2f);
    touchLedger();
}

void MachineEditor::Impl::closeLedger()
{
    if (ledger.mode == Ledger::Closed)
        return;
    touchLedger();
    ledger.mode = Ledger::Closed;
}

void MachineEditor::Impl::ledgerPick(const LedgerItem &it)
{
    if (it.kind == LedgerItem::None && it.text.isNotEmpty() && it.text != "EMPTY" && it.text != "EMPTY SLOT" && it.text != "INIT (plain)" && it.text != "CLOSE")
        return;
    const auto mode = ledger.mode;
    const int corner = ledger.corner;
    closeLedger();
    if (mode == Ledger::FxPick)
    {
        fxSetSlotType(corner, it.kind == LedgerItem::Fx ? it.fxIndex : 0); // (a None item here is the EMPTY SLOT foot)
        return;
    }
    if (mode == Ledger::CornerPick)
    {
        setCornerSound(corner, it); // (a None item here is the EMPTY foot)
        return;
    }
    if (it.kind == LedgerItem::Factory)
        pm->loadFactoryPreset(P, ed.mainToAudio, it.cat, it.file);
    else if (it.kind == LedgerItem::User)
        pm->loadUserPresetDirect(P, ed.mainToAudio, pm->userPatchesPath / it.user);
    else if (it.text == "INIT (plain)")
    {
        pm->loadInit(P, ed.mainToAudio);
        ed.status("INIT: one ghost awake, nothing plugged in. Drag a hose from an OUT socket to a ghost below it.");
    }
    ed.uiDirty = true;
}

// which tab sits in a row of the gutter (-1 none, -2 / -3 the "more above / below" ends)
static int tabInRow(const Ledger &L, int row)
{
    const int n = L.tabs.size();
    if (n <= kTabRows)
        return row < n ? row : -1;
    // YOUR SOUNDS keeps the last row; the folders scroll in the rows above it
    const int yours = L.tabs.indexOf("YOUR SOUNDS");
    if (row == kTabRows - 1)
        return yours;
    int k = L.tabScroll + row;
    if (k >= yours)
        ++k; // (skip it in the scrolling part)
    return k < n ? k : -1;
}
static int maxTabScroll(const Ledger &L) { return juce::jmax(0, L.tabs.size() - 1 - (kTabRows - 1)); }

bool MachineEditor::Impl::ledgerDown(FPoint p)
{
    const auto &Lg = as->layout["ledger"];
    const auto rc = rectOf(Lg["rect"]);
    const float ox = ledger.x - rc.getX(), oy = ledger.y - rc.getY();
    if (!ledgerArea().contains(p))
    {
        closeLedger();
        return false;
    }
    const auto tabs = rectOf(Lg["tabs"]).translated(ox, oy), slats = rectOf(Lg["slats"]).translated(ox, oy), foot = rectOf(Lg["foot"]).translated(ox, oy);
    if (tabs.contains(p))
    {
        const int row = juce::jlimit(0, kTabRows - 1, (int)((p.y - tabs.getY()) / kRowH));
        const bool scrolls = ledger.tabs.size() > kTabRows;
        if (scrolls && p.x > tabs.getRight() - 24 && row == 0 && ledger.tabScroll > 0)
            ledger.tabScroll = juce::jmax(0, ledger.tabScroll - 3);
        else if (scrolls && p.x > tabs.getRight() - 24 && row == kTabRows - 2 && ledger.tabScroll < maxTabScroll(ledger))
            ledger.tabScroll = juce::jmin(maxTabScroll(ledger), ledger.tabScroll + 3);
        else
        {
            const int t = tabInRow(ledger, row);
            if (t >= 0)
            {
                ledger.tab = t;
                ledger.pageIx = 0;
                ledgerItems();
            }
        }
        touchLedger();
    }
    else if (slats.contains(p))
    {
        const int row = (int)((p.y - slats.getY()) / kRowH), ix = ledger.pageIx * kRows + row;
        if (row >= 0 && row < kRows && ix < (int)ledger.items.size())
        {
            const auto it = ledger.items[(size_t)ix];
            ledgerPick(it);
        }
    }
    else if (foot.contains(p))
    {
        const float fx = p.x - foot.getX();
        if (fx < 41)
        {
            if (ledger.pageIx > 0)
                --ledger.pageIx;
        }
        else if (fx > foot.getWidth() - 41)
        {
            if (ledger.pageIx < ledgerPages() - 1)
                ++ledger.pageIx;
        }
        else
        {
            LedgerItem it;
            it.text = ledger.mode == Ledger::FxPick ? "EMPTY SLOT" : ledger.mode == Ledger::CornerPick ? "EMPTY" : ledger.mode == Ledger::Sounds ? "INIT (plain)" : "CLOSE";
            ledgerPick(it);
            return true;
        }
        touchLedger();
    }
    return true;
}

void MachineEditor::Impl::ledgerHover(FPoint p)
{
    if (ledger.mode == Ledger::Closed)
        return;
    const auto &Lg = as->layout["ledger"];
    const auto rc = rectOf(Lg["rect"]);
    const float ox = ledger.x - rc.getX(), oy = ledger.y - rc.getY();
    const auto tabs = rectOf(Lg["tabs"]).translated(ox, oy), slats = rectOf(Lg["slats"]).translated(ox, oy), foot = rectOf(Lg["foot"]).translated(ox, oy);
    int ht = -1, hs = -1, hf = -1;
    if (tabs.contains(p))
        ht = (int)((p.y - tabs.getY()) / kRowH);
    else if (slats.contains(p))
        hs = (int)((p.y - slats.getY()) / kRowH);
    else if (foot.contains(p))
        hf = p.x - foot.getX() < 41 ? 0 : p.x - foot.getX() > foot.getWidth() - 41 ? 2 : 1;
    if (ht != ledger.hoverTab || hs != ledger.hoverSlat || hf != ledger.hoverFoot)
    {
        ledger.hoverTab = ht;
        ledger.hoverSlat = hs;
        ledger.hoverFoot = hf;
        touchLedger();
    }
}

void MachineEditor::Impl::ledgerWheel(FPoint p, float dy)
{
    const auto &Lg = as->layout["ledger"];
    const auto rc = rectOf(Lg["rect"]);
    const auto tabs = rectOf(Lg["tabs"]).translated(ledger.x - rc.getX(), ledger.y - rc.getY());
    wheelAcc += dy;
    if (std::abs(wheelAcc) < 0.2f)
        return;
    const int d = wheelAcc > 0 ? -1 : 1;
    wheelAcc = 0;
    if (tabs.contains(p))
        ledger.tabScroll = juce::jlimit(0, maxTabScroll(ledger), ledger.tabScroll + d);
    else
        ledger.pageIx = juce::jlimit(0, ledgerPages() - 1, ledger.pageIx + d);
    touchLedger();
}

void MachineEditor::Impl::drawLedger(const Ctx &c)
{
    if (ledger.mode == Ledger::Closed || !c.touches(ledgerArea()))
        return;
    const auto &Lg = as->layout["ledger"];
    const auto rc = rectOf(Lg["rect"]);
    const float ox = ledger.x - rc.getX(), oy = ledger.y - rc.getY();
    // it unrolls from the roller: clipped from the top
    const auto area = ledgerArea();
    const float shown = area.getHeight() * ledger.revealDrawn + 22.0f;
    std::vector<IRect> cl;
    const IRect clipR((int)std::floor(area.getX() * S), (int)std::floor(area.getY() * S), (int)std::ceil(area.getWidth() * S), (int)std::ceil(juce::jmin(area.getHeight(), shown) * S));
    for (auto &q : *c.clips)
    {
        const auto i = q.getIntersection(clipR);
        if (!i.isEmpty())
            cl.push_back(i);
    }
    if (cl.empty())
        return;
    Ctx d = c;
    d.clips = &cl;
    auto kitSprite = [&](const juce::String &name, bool mirror = false, float gain = 1.0f) -> const Sprite & {
        const juce::String key = "led-" + name + (mirror ? "-m" : "") + juce::String(juce::roundToInt(gain * 100));
        auto it = as->made.find(key);
        if (it != as->made.end())
            return it->second;
        Bitmap b = as->kit(name).bmp;
        if (mirror)
            for (int y = 0; y < b.h; ++y)
                std::reverse(b.row(y), b.row(y) + b.w);
        if (gain != 1.0f)
            b.brighten(gain);
        return as->made.emplace(key, Sprite::fromBitmap(b)).first->second;
    };
    auto plain = [&](FRect r, const juce::String &t, float size, bool semi, float track, juce::Colour col, bool left, float pad, bool shadow, juce::Colour emboss = {}) {
        if (t.isEmpty() || !d.touches(r))
            return;
        TextStyle st;
        st.size = size;
        st.semi = semi;
        st.track = track;
        st.col = col;
        st.shadow = shadow;
        const float mx = r.getWidth() - (left ? pad + 6 : 6);
        const float w = as->textWidth(t, st), q = w > mx && w > 0 ? mx / w : 1.0f;
        const float base = r.getY() + (r.getHeight() - size) * 0.5f + 0.85f * size;
        const auto &ts = as->text(t, st, q);
        const float x = left ? r.getX() + pad : r.getCentreX() - ts.w * 0.5f;
        if (!emboss.isTransparent())
        {
            TextStyle e2 = st;
            e2.col = emboss;
            const auto &te = as->text(t, e2, q);
            d.sprite(te.spr, x - te.pad, base + 0.8f - 1.05f * size - te.pad, emboss.getFloatAlpha());
        }
        d.sprite(ts.spr, x - ts.pad, base - 1.05f * size - ts.pad);
    };
    d.sprite(kitSprite("ledger-back"), area.getX(), area.getY());
    // the head plate: its title, the page count
    const auto head = rectOf(Lg["head"]).translated(ox, oy);
    static const char *L4[Morph::N] = {"A", "B", "C", "D"};
    const juce::String title = ledger.mode == Ledger::FxPick ? "SLOT " + juce::String(ledger.corner + 1) + " - PICK ITS EFFECT" : ledger.mode == Ledger::Sounds ? "SOUNDS" : ledger.mode == Ledger::Yours ? "YOUR SOUNDS" : juce::String("CORNER ") + L4[juce::jlimit(0, 3, ledger.corner)] + " - PICK ITS SOUND";
    plain(head, title, 8.0f, true, 0.2f, juce::Colour(0xffffd98a), false, 0, true);
    const int pages = ledgerPages();
    if (pages > 1)
    {
        const juce::String pg = "PAGE " + juce::String(ledger.pageIx + 1) + " / " + juce::String(pages);
        TextStyle st;
        st.size = 7.0f;
        st.semi = false;
        st.track = 0.1f;
        st.col = juce::Colour(0xffcdb98a);
        const auto &ts = as->text(pg, st, 1.0f);
        d.sprite(ts.spr, head.getRight() - 6 - ts.w - ts.pad, head.getY() + (head.getHeight() - 7.0f) * 0.5f + 0.85f * 7.0f - 1.05f * 7.0f - ts.pad);
    }
    // the folder tabs
    const auto tabs = rectOf(Lg["tabs"]).translated(ox, oy);
    const bool scrolls = ledger.tabs.size() > kTabRows;
    for (int row = 0; row < kTabRows; ++row)
    {
        const int t = tabInRow(ledger, row);
        if (t < 0)
            continue;
        const bool on = t == ledger.tab, hov = row == ledger.hoverTab;
        const FRect r(tabs.getX(), tabs.getY() + row * kRowH, tabs.getWidth(), kRowH);
        if (on)
        { // the open folder: lit brass with an amber halo
            const juce::String key = "led-tabhalo";
            auto it = as->made.find(key);
            if (it == as->made.end())
                it = as->made.emplace(key, Sprite::fromBitmap(blurredAlpha(as->kit("ledger-tab-lit").bmp, 2.0f * S, juce::Colour(255, 178, 62), 1.0f, 6 * S))).first;
            d.sprite(it->second, r.getX() - 6, r.getY() - 6, 0.75f);
        }
        d.sprite(kitSprite(on ? "ledger-tab-lit" : "ledger-tab", false, hov ? 1.14f : 1.0f), r.getX(), r.getY());
        const FRect tr(r.getX(), r.getY(), r.getWidth() - (scrolls ? 20.0f : 6.0f), kRowH - 3.0f);
        if (on)
            plain(tr, ledger.tabs[t].toUpperCase(), 7.8f, true, 0.08f, juce::Colour(0xff1a1208), true, 13, false, juce::Colour(0x80fff4c8));
        else
            plain(tr, ledger.tabs[t].toUpperCase(), 7.8f, false, 0.08f, juce::Colour(0xffe2d2a6), true, 13, true);
        // more folders above / below: a small arrow at the end of the first / last scrolling tab
        if (scrolls && d.g != nullptr && ((row == 0 && ledger.tabScroll > 0) || (row == kTabRows - 2 && ledger.tabScroll < maxTabScroll(ledger))))
        {
            const bool up = row == 0;
            const float ax = r.getRight() - 15, ay = r.getY() + 15.5f;
            juce::Path a;
            a.addTriangle(ax - 4.5f, ay + (up ? 2.5f : -2.5f), ax + 4.5f, ay + (up ? 2.5f : -2.5f), ax, ay + (up ? -3.5f : 3.5f));
            d.g->saveState();
            d.g->reduceClipRegion(juce::Rectangle<float>(area.getX(), area.getY(), area.getWidth(), juce::jmin(area.getHeight(), shown)).toNearestInt());
            d.g->setColour(juce::Colour(0xff17110e));
            d.g->strokePath(a, juce::PathStrokeType(2.0f));
            d.g->setColour(on ? juce::Colour(0xff1a1208) : juce::Colour(0xffffd98a));
            d.g->fillPath(a);
            d.g->restoreState();
        }
    }
    // the sounds: one brass slat each, thirteen to a page; the loaded one is lit
    const auto slats = rectOf(Lg["slats"]).translated(ox, oy);
    for (int row = 0; row < kRows; ++row)
    {
        const int ix = ledger.pageIx * kRows + row;
        if (ix >= (int)ledger.items.size())
            break;
        const auto &it = ledger.items[(size_t)ix];
        const bool cur = isCurrent(*this, it), hov = row == ledger.hoverSlat && it.kind != LedgerItem::None;
        const FRect r(slats.getX(), slats.getY() + row * kRowH, slats.getWidth(), kRowH);
        if (cur)
        {
            const juce::String key = "led-slathalo";
            auto h = as->made.find(key);
            if (h == as->made.end())
                h = as->made.emplace(key, Sprite::fromBitmap(blurredAlpha(as->kit("ledger-slat-lit").bmp, 2.5f * S, juce::Colour(255, 178, 62), 1.0f, 7 * S))).first;
            d.sprite(h->second, r.getX() - 7, r.getY() - 7, 0.85f);
        }
        d.sprite(kitSprite(cur ? "ledger-slat-lit" : "ledger-slat" + juce::String((row + ledger.pageIx) % 3), false, hov ? (cur ? 1.1f : 1.14f) : 1.0f), r.getX(), r.getY(), it.kind == LedgerItem::None ? 0.7f : 1.0f);
        const FRect tr(r.getX(), r.getY(), r.getWidth() - 20.0f, kRowH - 3.0f);
        TextStyle st;
        st.size = 10.5f;
        st.semi = true;
        st.track = 0.0f;
        const float tw = as->textWidth(it.text, st);
        const float hintW = it.hint.isNotEmpty() ? 120.0f : 0.0f;
        plain(tr.withTrimmedRight(hintW), it.text, 10.5f, true, 0.0f, juce::Colour(0xff1a1208), true, 20, false, juce::Colour(0x73fff4c8));
        if (it.hint.isNotEmpty())
            plain(FRect(juce::jmin(tr.getRight() - hintW, tr.getX() + 20 + tw + 8), tr.getY() + 1, hintW, tr.getHeight()), it.hint, 7.6f, false, 0.0f, juce::Colour(0xc01a1208), true, 0, false);
    }
    // the foot: page arrows either side of INIT / EMPTY / CLOSE
    const auto foot = rectOf(Lg["foot"]).translated(ox, oy);
    d.sprite(kitSprite("ledger-arrow", false, ledger.hoverFoot == 0 && ledger.pageIx > 0 ? 1.14f : 1.0f), foot.getX(), foot.getY(), ledger.pageIx > 0 ? 1.0f : 0.35f);
    d.sprite(kitSprite("ledger-arrow", true, ledger.hoverFoot == 2 && ledger.pageIx < pages - 1 ? 1.14f : 1.0f), foot.getRight() - 40, foot.getY(), ledger.pageIx < pages - 1 ? 1.0f : 0.35f);
    d.sprite(kitSprite("ledger-foot", false, ledger.hoverFoot == 1 ? 1.14f : 1.0f), foot.getX() + 42, foot.getY());
    plain(FRect(foot.getX() + 42, foot.getY(), 212, kRowH - 3.0f), ledger.mode == Ledger::FxPick ? "EMPTY SLOT" : ledger.mode == Ledger::CornerPick ? "EMPTY" : ledger.mode == Ledger::Sounds ? "INIT (plain)" : "CLOSE", 10.5f, true, 0.0f, juce::Colour(0xff0c1a16), true, 20, false,
          juce::Colour(0x59dcfff0));
}

// ============================================================================================== brass dialogs and menus
namespace
{
struct BrassLnF : juce::LookAndFeel_V4
{
    juce::Typeface::Ptr medium, semi;
    BrassLnF(juce::Typeface::Ptr m, juce::Typeface::Ptr s) : medium(m), semi(s)
    {
        const juce::Colour enamel(0xff14100c), brass(0xffb8a366), amber(0xffffb23e), label(0xffe9dcb8);
        setColour(juce::AlertWindow::backgroundColourId, enamel);
        setColour(juce::AlertWindow::textColourId, label);
        setColour(juce::AlertWindow::outlineColourId, brass);
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a0806));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xfffff0cc));
        setColour(juce::TextEditor::highlightColourId, amber.withAlpha(0.45f));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour(juce::TextEditor::outlineColourId, brass);
        setColour(juce::TextEditor::focusedOutlineColourId, amber);
        setColour(juce::CaretComponent::caretColourId, amber);
        setColour(juce::Label::textColourId, label);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2f1c));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffd98a));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1a1208));
        setColour(juce::PopupMenu::backgroundColourId, enamel);
        setColour(juce::PopupMenu::textColourId, label);
        setColour(juce::PopupMenu::headerTextColourId, juce::Colour(0xffa89c7c));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xffc9b06a));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff1a1208));
    }
    juce::Font face(float h, bool bold) const
    {
        auto o = juce::FontOptions().withHeight(h);
        if (auto tf = bold ? semi : medium)
            o = o.withTypeface(tf);
        return juce::Font(o);
    }
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override { return f.isBold() ? (semi != nullptr ? semi : medium) : (medium != nullptr ? medium : juce::LookAndFeel_V4::getTypefaceForFont(f)); }
    juce::Font getAlertWindowTitleFont() override { return face(19.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return face(15.0f, false); }
    juce::Font getAlertWindowFont() override { return face(14.0f, false); }
    juce::Font getPopupMenuFont() override { return face(15.0f, false); }
    juce::Font getTextButtonFont(juce::TextButton &, int h) override { return face(juce::jmin(15.0f, (float)h * 0.55f), true); }
    static void plate(juce::Graphics &g, juce::Rectangle<float> r, bool rivets)
    { // a dark enamel plate in a brass frame
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1711), 0, r.getY(), juce::Colour(0xff0e0b08), 0, r.getBottom(), false));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colour(0xff17110e));
        g.drawRoundedRectangle(r.reduced(0.75f), 6.0f, 1.5f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8c487), 0, r.getY(), juce::Colour(0xff8a7440), 0, r.getBottom(), false));
        g.drawRoundedRectangle(r.reduced(2.5f), 5.0f, 2.0f);
        g.setColour(juce::Colour(0x40ffb23e));
        g.drawRoundedRectangle(r.reduced(4.5f), 4.0f, 1.0f);
        if (rivets)
            for (auto p : {r.reduced(9).getTopLeft(), r.reduced(9).getTopRight(), r.reduced(9).getBottomLeft(), r.reduced(9).getBottomRight()})
            {
                g.setColour(juce::Colour(0xff17110e));
                g.fillEllipse(p.x - 3.2f, p.y - 3.2f, 6.4f, 6.4f);
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe9d9a0), p.x - 1, p.y - 1.5f, juce::Colour(0xff7a6536), p.x + 2, p.y + 2.5f, true));
                g.fillEllipse(p.x - 2.4f, p.y - 2.4f, 4.8f, 4.8f);
            }
    }
    void drawAlertBox(juce::Graphics &g, juce::AlertWindow &w, const juce::Rectangle<int> &textArea, juce::TextLayout &layout) override
    {
        plate(g, w.getLocalBounds().toFloat(), true);
        g.setColour(findColour(juce::AlertWindow::textColourId));
        layout.draw(g, textArea.toFloat());
    }
    int getAlertWindowButtonHeight() override { return 34; }
    void drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(juce::Colour(0xff17110e));
        g.fillRoundedRectangle(r, 5.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(down ? 0xff6f5d33u : over ? 0xff54452au : 0xff3d321fu), 0, r.getY(), juce::Colour(down ? 0xff4b3e22u : 0xff241d12u), 0, r.getBottom(), false));
        g.fillRoundedRectangle(r.reduced(1.5f), 4.0f);
        g.setColour(juce::Colour(over ? 0xffffd98au : 0xffb8a366u));
        g.drawRoundedRectangle(r.reduced(1.5f), 4.0f, 1.2f);
    }
    void fillTextEditorBackground(juce::Graphics &g, int w, int h, juce::TextEditor &) override
    {
        g.setColour(juce::Colour(0xff0a0806));
        g.fillRoundedRectangle(0, 0, (float)w, (float)h, 4.0f);
    }
    void drawTextEditorOutline(juce::Graphics &g, int w, int h, juce::TextEditor &t) override
    {
        g.setColour(t.hasKeyboardFocus(true) ? juce::Colour(0xffffb23e) : juce::Colour(0xffb8a366));
        g.drawRoundedRectangle(0.75f, 0.75f, (float)w - 1.5f, (float)h - 1.5f, 4.0f, 1.5f);
    }
    void drawPopupMenuBackground(juce::Graphics &g, int w, int h) override
    {
        g.fillAll(juce::Colour(0xff14100c));
        g.setColour(juce::Colour(0xffb8a366));
        g.drawRect(0, 0, w, h, 1);
    }
};
} // namespace

juce::LookAndFeel &brass(MachineEditor::Impl &I)
{
    if (!I.lnf)
        I.lnf = std::make_unique<BrassLnF>(I.as ? I.as->medium : juce::Typeface::Ptr(), I.as ? I.as->semibold : juce::Typeface::Ptr());
    return *I.lnf;
}

void MachineEditor::Impl::saveSound()
{
    auto *w = new juce::AlertWindow("Save this sound", "Name it:", juce::MessageBoxIconType::NoIcon, &ed);
    w->setLookAndFeel(&brass(*this));
    dialog = w;
    w->addTextEditor("name", juce::String(juce::CharPointer_UTF8(P.name)));
    w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<MachineEditor>(&ed), w](int r) {
        if (r != 1 || sp == nullptr)
            return;
        auto &I = *sp->impl;
        auto n = juce::File::createLegalFileName(w->getTextEditorContents("name").trim());
        if (n.isEmpty())
            return;
        std::error_code ec;
        fs::create_directories(I.pm->userPatchesPath, ec);
        const auto path = I.pm->userPatchesPath / (n.toStdString() + ss::presets::PresetManager::presetExtension);
        strncpy(I.P.name, n.toRawUTF8(), sizeof(I.P.name) - 1);
        I.P.name[sizeof(I.P.name) - 1] = 0;
#if USE_WCHAR_PRESET
        I.pm->saveUserPresetDirect(I.P, path.c_str());
#else
        I.pm->saveUserPresetDirect(I.P, path);
#endif
        I.P.dirty = false;
        const auto lp = I.pm->identifyUserPath(path);
        sp->dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
        sp->status("Saved \"" + n + "\". LOAD brings it back in any project.");
        sp->uiDirty = true;
    }), true);
}

// right-click a wheel: type its value (the parameter's own words are read back, as before)
void MachineEditor::Impl::typeIn(Ctl &c)
{
    auto *p = paramOf(c);
    const juce::String id = c.id;
    juce::String title = p ? juce::String(juce::CharPointer_UTF8(p->meta.name.c_str())) : c.fx != 0 ? fxName(c) : c.name, now;
    if (p)
    {
        auto s = p->meta.valueToString(p->value);
        now = s ? juce::String(juce::CharPointer_UTF8(s->c_str())) : juce::String(p->value);
    }
    else if (c.fx != 0)
    {
        FxV v;
        if (!fxValue(c, v))
            return;
        now = fxText(v, v.val);
    }
    else
        now = c.t2;
    auto *w = new juce::AlertWindow("Type a value", title, juce::MessageBoxIconType::NoIcon, &ed);
    w->setLookAndFeel(&brass(*this));
    dialog = w;
    w->addTextEditor("v", now);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<MachineEditor>(&ed), w, id](int r) {
        if (r != 1 || sp == nullptr)
            return;
        auto &I = *sp->impl;
        auto *k = I.find(id);
        if (k == nullptr)
            return;
        const auto txt = w->getTextEditorContents("v").toStdString();
        if (k->fx != 0)
        { // the rack's own words: the parameter reads what it wrote
            FxV v;
            if (!I.fxValue(*k, v))
                return;
            float real = v.val;
            double hv = 0;
            bool ok = false;
            if (v.d != nullptr)
                ok = v.d->parse(txt, real);
            else if (sp->spectral && sp->spectral->textToValue(v.id, txt.c_str(), &hv))
            {
                real = (float)hv;
                ok = true;
            }
            if (!ok)
            {
                const auto num = juce::String(txt).retainCharacters("-0123456789.");
                ok = num.isNotEmpty();
                real = num.getFloatValue();
            }
            if (ok)
            {
                I.fxSet(*k, real, 0);
                I.fxSet(*k, real, 1);
                I.fxSet(*k, real, 2);
            }
            else
                sp->status("Couldn't read \"" + juce::String(txt) + "\"");
        }
        else if (auto *q = I.paramOf(*k))
        {
            std::string err;
            if (auto v = q->meta.valueFromString(txt, err))
            {
                const float lo = q->meta.minVal, hi = q->meta.maxVal;
                const float cl = juce::jlimit(lo, hi, *v);
                I.setCtl01(*k, 0, 0, true);
                if (cl != q->value)
                    sp->setValue(*q, cl); // (as typed: no snapping)
                I.setCtl01(*k, 0, 2, true);
            }
            else
                sp->status("Couldn't read \"" + juce::String(txt) + "\"" + (err.empty() ? juce::String() : ": " + juce::String(err)));
        }
        else
        { // MORPH's own wheels: SIZE in percent, a corner's volume in dB, its pan -100 .. 100 (or L / R)
            auto t = juce::String(txt).trim();
            double num = t.retainCharacters("-0123456789.").getDoubleValue();
            float v01 = I.getCtl01(*k);
            if (k->role == R_OSize)
                v01 = (float)(num / 100.0);
            else if (k->role == R_CornerVol)
                v01 = (float)((num + 24.0) / 30.0);
            else if (k->role == R_CornerPan)
            {
                if (t.startsWithIgnoreCase("C"))
                    num = 0;
                else if (t.startsWithIgnoreCase("L"))
                    num = -std::abs(num);
                v01 = (float)((num / 100.0 + 1.0) * 0.5);
            }
            I.setCtl01(*k, 0, 0, true);
            I.setCtl01(*k, v01, 1, true);
            I.setCtl01(*k, 0, 2, true);
        }
        sp->uiDirty = true;
    }), true);
}

// right-click the size grip: the window's sizes (the ones the screen has no room for are greyed)
void MachineEditor::sizeMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel(&brass(*impl));
    m.addSectionHeader("WINDOW SIZE");
    const double cap = isShowing() ? sizing::displayCap(this) : sizing::kMax;
    const int now = juce::roundToInt(viewScale() * 100.0);
    for (int pct : sizing::kSteps)
        m.addItem(juce::String(pct) + " %", pct <= juce::roundToInt(cap * 100.0) + 0, std::abs(pct - now) <= 1, [sp = juce::Component::SafePointer<MachineEditor>(this), pct] {
            if (sp != nullptr)
                sp->setUiScale(pct / 100.0, true);
        });
    juce::Rectangle<int> r = getLocalBounds().removeFromBottom(40).removeFromRight(40);
    if (auto *k = impl->find("size-grip"))
        r = (k->rect * (float)viewScale()).toNearestInt();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(r)).withStandardItemHeight(24));
}

// right-click a dial or a drum: every option at once (the first editor's drop-down)
void MachineEditor::Impl::popupFor(Ctl &c)
{
    if (c.role == R_Folder)
    {
        openLedger(Ledger::Sounds, 0);
        return;
    }
    const int n = selectorCount(c), cur = selectorIndex(c);
    if (n <= 1)
        return;
    juce::PopupMenu m;
    m.setLookAndFeel(&brass(*this));
    m.addSectionHeader(c.name);
    const juce::String id = c.id;
    for (int i = 0; i < n; ++i)
        m.addItem(selectorText(c, i), true, i == cur, [sp = juce::Component::SafePointer<MachineEditor>(&ed), id, i] {
            if (sp == nullptr)
                return;
            if (auto *k = sp->impl->find(id))
                sp->impl->stepSelector(*k, i);
        });
    const auto r = (c.rect * (float)ed.viewScale()).toNearestInt();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&ed).withTargetScreenArea(ed.localAreaToGlobal(r)).withStandardItemHeight(24));
}
} // namespace ghostblock::p2::machine
