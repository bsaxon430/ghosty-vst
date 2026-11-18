/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the world behind the machine.
 *
 * Back to front (JUCE-NOTES.md, section 3): the sky, two drifting cloud decks, the sun shafts, the near deck, the
 * storm's dark fill, the lightning (never dimmed by the storm), the hills, the wind-blown meadow. It is kept as one
 * opaque frame; a tick re-makes only the cells where a cloud wisp, the meadow or the light really changed.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
static const juce::Colour kStormFill(8, 12, 26);

void World::build(Assets &as, const juce::String &dir, const juce::var &list)
{
    S = as.S;
    sky = as.bitmap(dir + "/world/sky.png", 2.0);
    skyLit = Bitmap(sky.w, sky.h);
    rays = as.bitmap(dir + "/world/rays.png", (double)S); // kept at one pixel per design pixel: it is soft
    flash = as.bitmap(dir + "/world/flash.png", (double)S);
    land = Sprite::fromBitmap(as.bitmap(dir + "/world/land.png", 2.0));
    for (int i = 0; i < 4; ++i)
        grass[i] = Sprite::fromBitmap(as.bitmap(dir + "/world/grass-" + juce::String(i) + ".png", 2.0));
    int d = 0, b = 0;
    if (auto *arr = list.getArray())
        for (auto &it : *arr)
        {
            const auto type = it["anim"]["type"].toString();
            if (type == "deck" && d < 3)
            {
                auto &k = deck[d++];
                k.spr = Sprite::fromBitmap(as.bitmap(Assets::resolvePath(dir, it["file"].toString()), numOf(it["png_scale"], 1)));
                k.speed = numOf(it["anim"]["speed_px_per_s"], 4);
                k.wrap = numOf(it["anim"]["wrap_px"], 2080);
                k.offset = numOf(it["anim"]["still_offset"], 0);
                k.drawn = -1e9f;
                k.occ.clear();
                for (auto c : cellsOf(k.spr, 0, 0, S)) // (the deck is two canvases wide: its own cell columns)
                    k.occ.push_back({(int16_t)(c % COLS), (int16_t)(c / COLS)});
                // cellsOf clips to one canvas: take the second half too
                for (auto c : cellsOf(k.spr, -(float)CW, 0, S))
                    k.occ.push_back({(int16_t)(c % COLS + COLS), (int16_t)(c / COLS)});
            }
            else if (type == "bolt" && b < 5)
            {
                auto &k = bolt[b++];
                k.spr = Sprite::fromBitmap(as.bitmap(Assets::resolvePath(dir, it["file"].toString()), numOf(it["png_scale"], 2)));
                k.x = numOf(it["x"]);
                k.y = numOf(it["y"]);
                k.cells = cellsOf(k.spr, k.x, k.y, S);
            }
            else if (type == "pulse")
            {
                raysMin = numOf(it["anim"]["min"], 0.6f);
                raysMax = numOf(it["anim"]["max"], 1.0f);
                raysPeriod = numOf(it["anim"]["period"], 9.5f);
            }
            else if (type == "crossfade-loop")
                grassPeriod = numOf(it["anim"]["period"], 3.6f);
        }
    // where the soft full-canvas lights have anything to show
    auto softCells = [&](const Bitmap &bm) {
        CellSet s;
        for (int y = 0; y < bm.h; ++y)
        {
            const Px *r = bm.row(y);
            for (int x = 0; x < bm.w; ++x)
                if (r[x] & 0x00ffffffu)
                {
                    s.set(juce::jmin(ROWS - 1, y / CELL) * COLS + juce::jmin(COLS - 1, x / CELL));
                    x = (x / CELL + 1) * CELL - 1;
                }
        }
        std::vector<uint16_t> out;
        for (int i = 0; i < NCELLS; ++i)
            if (s.test(i))
                out.push_back((uint16_t)i);
        return out;
    };
    raysCells = softCells(rays);
    flashCells = softCells(flash);
    landCells = cellsOf(land, 0, 0, S);
    CellSet g;
    for (auto &f : grass)
        g.mark(cellsOf(f, 0, 596, S));
    grassCells.clear();
    for (int i = 0; i < NCELLS; ++i)
        if (g.test(i))
            grassCells.push_back((uint16_t)i);
    reset();
}

void World::reset()
{
    bakedRays = bakedStorm = bakedDeckAlpha = -1;
    bakedGrass = 0;
    bakedStrike = 0;
    sinceSky = sinceGrass = 1e3;
    litValid.clear();
    for (auto &k : deck)
        k.drawn = -1e9f;
    for (int i = 0; i < 256; ++i)
        landLut[i] = (uint8_t)i;
}

static float keyed(const float (*k)[2], int n, float x)
{
    if (x <= k[0][0])
        return k[0][1];
    for (int i = 1; i < n; ++i)
        if (x <= k[i][0])
            return k[i - 1][1] + (k[i][1] - k[i - 1][1]) * (x - k[i - 1][0]) / juce::jmax(1e-6f, k[i][0] - k[i - 1][0]);
    return k[n - 1][1];
}

bool World::strike(double now, juce::Random &rng)
{
    if (now - lastStrike < 1.5)
        return false; // never more than one sequence in 1.5 s
    lastStrike = now;
    boltIx = rng.nextInt(5);
    return true;
}

float World::strikeLevel(double now) const
{
    static const float k[6][2] = {{0, 0}, {19, 1}, {81, 0.12f}, {149, 0.8f}, {223, 0.45f}, {620, 0}};
    const float ms = (float)((now - lastStrike) * 1000.0);
    return ms < 0 || ms > 620 ? 0.f : keyed(k, 6, ms);
}

void World::tick(const Env &env, float dt, CellSet &dirty)
{
    const float a = env.act, s = env.storm;
    calm = env.calm;
    // ---- the decks drift with parallax and race in a storm
    const float cs = calm ? 0.3f : 1.0f + (a - Env::IDLE) * 1.5f + s * 6.0f;
    for (auto &k : deck)
    {
        k.offset += dt * k.speed * cs;
        while (k.offset >= k.wrap)
            k.offset -= k.wrap;
    }
    sinceSky += dt;
    const double skyEvery = calm ? 0.25 : (s > 0.25f ? 1.0 / 15.0 : 0.125);
    auto markDeck = [&](const Deck &k, float off) {
        for (auto &c : k.occ)
        {
            const float x = (float)(c.first * CELL) + off;
            for (float xx : {x - k.wrap, x})
            {
                if (xx <= -(float)CELL || xx >= (float)CW)
                    continue;
                const int c0 = (int)std::floor(xx / CELL), c1 = (int)std::floor((xx + CELL - 0.01f) / CELL);
                for (int cx = juce::jmax(0, c0); cx <= juce::jmin(COLS - 1, c1); ++cx)
                    dirty.set(c.second * COLS + cx);
            }
        }
    };
    if (sinceSky >= skyEvery || deck[0].drawn < -1e8f)
    {
        sinceSky = 0;
        for (auto &k : deck)
        {
            const float snapped = std::round(k.offset * (float)S) / (float)S;
            if (std::abs(snapped - k.drawn) >= 1.0f)
            {
                if (k.drawn > -1e8f)
                    markDeck(k, k.drawn);
                markDeck(k, snapped);
                k.drawn = snapped;
            }
        }
    }
    // ---- the sun shafts breathe (and fade in a storm)
    raysNow = (raysMin + (raysMax - raysMin) * (calm ? 0.5f : 0.5f + 0.5f * (float)std::sin(env.t * juce::MathConstants<double>::twoPi / raysPeriod))) * (1.0f - 0.8f * s);
    if (std::abs(raysNow - bakedRays) > 0.085f || bakedRays < 0)
    {
        bakedRays = raysNow;
        litValid.clear();
        dirty.mark(raysCells);
    }
    // ---- the storm darkens the sky and the land
    if (std::abs(s - bakedStorm) > 0.015f || (s == 0.f && bakedStorm != 0.f))
    {
        bakedStorm = s;
        const float k = 1.0f - 0.5f * s;
        for (int i = 0; i < 256; ++i)
            landLut[i] = (uint8_t)juce::jlimit(0, 255, (int)std::lround((float)i * k));
        dirty.fill();
    }
    deckAlphaNow = env.theme == 2 ? 0.55f : 1.0f; // CLEAR thins the decks
    if (deckAlphaNow != bakedDeckAlpha)
    {
        bakedDeckAlpha = deckAlphaNow;
        dirty.fill();
    }
    // ---- the meadow: four wind phases, cross-faded, so the gust runs through the grass
    grassPh += dt / grassPeriod * (calm ? 0.35 : 1.0 + 1.8 * s + 0.6 * (a - Env::IDLE));
    grassPh -= std::floor(grassPh);
    sinceGrass += dt;
    if (sinceGrass >= (calm ? 0.25 : 0.125) && std::abs((float)grassPh - bakedGrass) > 0.004f)
    {
        sinceGrass = 0;
        bakedGrass = (float)grassPh;
        dirty.mark(grassCells);
    }
    // ---- lightning: the cold light in the clouds and one bolt
    static const float bk[6][2] = {{0, 0}, {0.03f, 1}, {0.13f, 0.25f}, {0.24f, 0.9f}, {0.5f, 0}, {1, 0}};
    const float lv = strikeLevel(env.t);
    const float frac = (float)((env.t - lastStrike) / 0.62);
    const float bl = (frac < 0 || frac > 1) ? 0.f : keyed(bk, 6, frac);
    if (std::abs(lv - bakedStrike) > 0.02f || std::abs(bl - boltNow) > 0.02f || (lv == 0.f && bakedStrike != 0.f))
    {
        bakedStrike = lv;
        boltNow = bl;
        dirty.mark(flashCells);
        if (boltIx >= 0)
            dirty.mark(bolt[boltIx].cells);
    }
}

// A far cloud under the sun shafts: screen(over(sky, cloud), shafts) is the same picture as over(screen(sky, shafts), cloud')
// with cloud' = cloud + shafts x (alpha - cloud), so the shafts can be baked into the sky once and each cloud pixel lit as
// it is put down (exact, and only the wisps cost anything).
static void blitLitDeck(const Surface &dst, IRect clip, const Sprite &s, int x, int y, uint32_t alpha256, const Bitmap &rays, uint32_t rays256, int S)
{
    if (s.empty() || alpha256 == 0)
        return;
    clip = clip.getIntersection(dst.bounds()).getIntersection(s.box.translated(x, y));
    if (clip.isEmpty())
        return;
    const int cx0 = clip.getX() - x, cx1 = clip.getRight() - x;
    for (int dy = clip.getY(); dy < clip.getBottom(); ++dy)
    {
        const size_t r = (size_t)(dy - y - s.box.getY());
        const Sprite::Span *sp = s.spans.data() + s.rowStart[r], *end = s.spans.data() + s.rowStart[r + 1];
        Px *drow = dst.row(dy);
        const Px *rrow = rays.row(juce::jmin(rays.h - 1, dy / S));
        for (; sp != end; ++sp)
        {
            if (sp->x >= cx1)
                break;
            int x0 = sp->x, x1 = sp->x + sp->n;
            if (x1 <= cx0)
                continue;
            x0 = juce::jmax(x0, cx0);
            x1 = juce::jmin(x1, cx1);
            const Px *src = s.pix.data() + sp->off + (size_t)(x0 - sp->x);
            for (int i = 0, n = x1 - x0; i < n; ++i)
            {
                const int dx = x + x0 + i;
                Px p = src[i];
                if (alpha256 < 256u)
                    p = ((((p & 0x00ff00ffu) * alpha256) >> 8) & 0x00ff00ffu) | ((((p >> 8) & 0x00ff00ffu) * alpha256) & 0xff00ff00u);
                const uint32_t a = p >> 24;
                if (a == 0)
                    continue;
                const Px rp = rrow[juce::jmin(rays.w - 1, dx / S)];
                uint32_t out = a << 24;
                for (int sh = 16; sh >= 0; sh -= 8)
                {
                    const uint32_t d = (p >> sh) & 0xffu, rl = juce::jmin(255u, (((rp >> sh) & 0xffu) * rays256) >> 8);
                    out |= (d + ((rl * (a - juce::jmin(a, d)) * 257u + 32768u) >> 16)) << sh;
                }
                const uint32_t ia = 256u - a - (a >> 7);
                const Px q = drow[dx];
                drow[dx] = (out + ((((q & 0x00ff00ffu) * ia) >> 8) & 0x00ff00ffu) + ((((q >> 8) & 0x00ff00ffu) * ia) & 0xff00ff00u)) | 0xff000000u;
            }
        }
    }
}

void World::compose(const Surface &w, const CellSet &cells)
{
    std::vector<IRect> rects;
    // ---- the sky with the sun shafts baked in (made again only where the shafts' strength changed)
    {
        CellSet need = cells;
        need.remove(litValid);
        need.rects(rects, S);
        const Surface skyS{const_cast<Px *>(sky.px.data()), sky.w, sky.h, sky.w};
        const Surface lit{skyLit.px.data(), skyLit.w, skyLit.h, skyLit.w};
        for (auto &r : rects)
        {
            copyRect(lit, skyS, r);
            blitScaled(lit, r, rays, 0, 0, (float)S, (int)std::lround(bakedRays * 256.0f), Blend::Screen);
        }
        litValid.add(need);
    }
    cells.rects(rects, S);
    const int grassK = (int)std::floor(bakedGrass * 4.0f) & 3;
    const float grassF = bakedGrass * 4.0f - std::floor(bakedGrass * 4.0f);
    const uint8_t *lut = bakedStorm > 0.004f ? landLut : nullptr;
    const uint32_t deckA = (uint32_t)std::lround(bakedDeckAlpha * 256.0f), raysA = (uint32_t)std::lround(bakedRays * 256.0f);
    const Surface lit{skyLit.px.data(), skyLit.w, skyLit.h, skyLit.w};
    for (auto r : rects)
    {
        r = r.getIntersection(w.bounds());
        if (r.isEmpty())
            continue;
        copyRect(w, lit, r);
        for (int k = 0; k < 3; ++k)
        {
            const auto &dk = deck[k];
            const int x = (int)std::lround(dk.drawn * (float)S), wr = (int)std::lround(dk.wrap * (float)S);
            if (k < 2)
            { // behind the shafts
                blitLitDeck(w, r, dk.spr, x - wr, 0, deckA, rays, raysA, S);
                blitLitDeck(w, r, dk.spr, x, 0, deckA, rays, raysA, S);
            }
            else
            {
                blit(w, r, dk.spr, x - wr, 0, (int)deckA);
                blit(w, r, dk.spr, x, 0, (int)deckA);
            }
        }
        if (bakedStorm > 0.004f)
            fillOver(w, r, kStormFill, (int)std::lround(0.72f * bakedStorm * 256.0f));
        if (bakedStrike > 0.004f || boltNow > 0.004f)
        { // after the dark fill, so lightning is never dimmed; capped (visual only)
            blitScaled(w, r, flash, 0, 0, (float)S, (int)std::lround(bakedStrike * 0.7f * 0.9f * 256.0f), Blend::Screen);
            if (boltIx >= 0)
                blit(w, r, bolt[boltIx].spr, (int)std::lround(bolt[boltIx].x * S), (int)std::lround(bolt[boltIx].y * S), (int)std::lround(boltNow * 0.85f * 256.0f), Blend::Screen);
        }
        blit(w, r, land, 0, 0, 256, Blend::Over, lut);
        blit(w, r, grass[grassK], 0, 596 * S, 256, Blend::Over, lut);
        blit(w, r, grass[(grassK + 1) & 3], 0, 596 * S, (int)std::lround(grassF * 256.0f), Blend::Over, lut);
    }
}
} // namespace ghostblock::p2::machine
