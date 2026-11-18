#include "WaveView.h"
#include "Chords.h"

namespace gslice::ui
{
// ---------------------------------------------------------------- tiles
juce::Image WaveTileCache::get (const Key& k, const std::function<juce::Image()>& make)
{
    auto it = map.find (k);
    if (it != map.end()) { lru.splice (lru.begin(), lru, it->second); ++hits; return it->second->second; }
    ++misses;
    auto img = make();
    lru.emplace_front (k, img); map[k] = lru.begin();
    while (lru.size() > capacity) { map.erase (lru.back().first); lru.pop_back(); }
    return img;
}

static uint64 regionKey (const Project& p, const Region& r, double hostRate)
{
    Hasher h; h.add ("tile").add (p.source.fingerprint).add (p.sampleRate).add (hostRate).add (r.reverse); r.map().hashInto (h);
    return h.key();
}

juce::Image renderWaveTile (const SourceAudio& src, const Project& p, const Region& r, double hostRate, int64 col0, double hfPerPhys,
                            int H, const Palette& P, bool focus)
{
    const int W = WaveTileCache::kTilePx;
    juce::Image img (juce::Image::ARGB, W, juce::jmax (1, H), true);
    juce::Graphics g (img);
    const auto m = r.map();
    const double len = r.length(), pfPerHf = p.sampleRate / hostRate;
    const float mid = (float) H * 0.5f, hh = (float) H * 0.46f;
    const auto wave = focus ? P.wave : P.waveOther, core = focus ? P.waveCore : P.waveOther.brighter (0.2f);
    // GHOST: the waveform as a violet -> teal gradient under the glass (the core a deeper shade of the same)
    const juce::ColourGradient gWave (juce::Colour (0xff7c5ce6).withAlpha (focus ? 0.62f : 0.32f), 0, mid - hh * 0.8f, juce::Colour (0xff22c7b0).withAlpha (focus ? 0.62f : 0.32f), 0, mid + hh * 0.8f, false);
    const juce::ColourGradient gCore (juce::Colour (0xff6a46e0).withAlpha (focus ? 1.0f : 0.5f), 0, mid - hh * 0.35f, juce::Colour (0xff12a892).withAlpha (focus ? 1.0f : 0.5f), 0, mid + hh * 0.35f, false);
    juce::Path line; bool lineStarted = false; bool sampleMode = false;
    for (int c = 0; c < W; ++c)
    {
        const double u0 = (double) (col0 + c) * hfPerPhys * pfPerHf, u1 = u0 + hfPerPhys * pfPerHf;
        if (u0 >= len) break;
        const double a = r.reverse ? len - std::min (u1, len) : u0, b = r.reverse ? len - u0 : std::min (u1, len);
        const double s0 = m.srcAt (a), s1 = m.srcAt (b);
        if (std::abs (s1 - s0) < 1.5)
        {   // close zoom: the samples themselves as a line
            sampleMode = true;
            const double s = m.srcAt (r.reverse ? len - (u0 + u1) * 0.5 : (u0 + u1) * 0.5);
            const int64 i0 = (int64) std::floor (s); const double fr = s - (double) i0;
            float v = 0;
            for (int ch = 0; ch < src.channels; ++ch)
            {
                auto at = [&] (int64 i) { return i >= 0 && i < src.frames ? src.audio.getSample (ch, (int) i) : 0.0f; };
                v += (float) ((1.0 - fr) * at (i0) + fr * at (i0 + 1));
            }
            v /= (float) juce::jmax (1, src.channels);
            const float y = mid - juce::jlimit (-1.0f, 1.0f, v) * hh;
            if (! lineStarted) { line.startNewSubPath ((float) c + 0.5f, y); lineStarted = true; } else line.lineTo ((float) c + 0.5f, y);
            continue;
        }
        const int64 ia = (int64) std::floor (std::min (s0, s1)), ib = (int64) std::ceil (std::max (s0, s1));
        float mn = 0, mx = 0, ms = 0;
        for (int ch = 0; ch < src.channels; ++ch)
        {
            const auto q = src.peaks.query (src.audio, ch, ia, ib);
            mn = ch == 0 ? q.mn : std::min (mn, q.mn); mx = ch == 0 ? q.mx : std::max (mx, q.mx); ms = std::max (ms, q.ms);
        }
        const float y0 = mid - juce::jlimit (-1.0f, 1.0f, mx) * hh, y1 = mid - juce::jlimit (-1.0f, 1.0f, mn) * hh;
        if (P.glass) g.setGradientFill (gWave); else g.setColour (wave);
        g.fillRect ((float) c, y0, 1.0f, juce::jmax (1.0f, y1 - y0));
        const float rms = std::sqrt (ms) * hh;
        if (P.glass) g.setGradientFill (gCore); else g.setColour (core);
        g.fillRect ((float) c, mid - rms, 1.0f, juce::jmax (0.0f, 2.0f * rms));
    }
    if (sampleMode && lineStarted)
    {
        if (P.glass) g.setGradientFill (gWave); else g.setColour (wave);
        g.strokePath (line, juce::PathStrokeType (1.4f));
    }
    g.setColour (P.ink.withAlpha (0.10f)); g.fillRect (0.0f, mid, (float) W, 1.0f);
    return img;
}

// ---------------------------------------------------------------- WaveView
WaveView::WaveView (SliceProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (false);
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}
WaveView::~WaveView() { stopTimer(); }

void WaveView::timerCallback()
{
    const auto ph = proc.playhead();
    if (proc.isPlaying() && ph != lastPlayhead)
    {
        const auto w = waveArea();
        const float x = xOf ((double) ph);
        if (x > (float) w.getRight() - 20 && drag == Drag::None) viewStart = (double) ph - 20.0 * hfPerPx;   // follow (page)
        repaint();
    }
    else if (! proc.isPlaying() && lastPlayhead != -2) repaint();
    lastPlayhead = proc.isPlaying() ? ph : -2;
}

void WaveView::zoomAround (double factor, float x)
{
    const double at = hfAt (x);
    const double total = juce::jmax (1.0, (double) proc.currentPlan().length);
    const double maxZ = juce::jmax (4.0, total / juce::jmax (100, waveArea().getWidth()) * 1.5);
    hfPerPx = juce::jlimit (1.0 / 16.0, maxZ, hfPerPx * factor);
    viewStart = at - ((double) x - waveArea().getX()) * hfPerPx;
    if (viewStart < -hfPerPx * 40) viewStart = -hfPerPx * 40;
    repaint();
}
void WaveView::zoomToFit()
{
    const double total = (double) proc.currentPlan().length;
    hfPerPx = total > 0 ? juce::jmax (1.0 / 16.0, total * 1.04 / juce::jmax (100, waveArea().getWidth())) : 256.0;
    viewStart = -total * 0.01; repaint();
}
void WaveView::zoomToSelection()
{
    double a = 0, b = 0;
    if (sel.hasRange()) { a = (double) sel.a; b = (double) sel.b; }
    else if (auto* r = selectedRegion()) { a = proc.pfToHf (r->dstStart); b = proc.pfToHf (r->dstEnd()); }
    else { zoomToFit(); return; }
    hfPerPx = juce::jmax (1.0 / 16.0, (b - a) * 1.1 / juce::jmax (100, waveArea().getWidth()));
    viewStart = a - (b - a) * 0.05; repaint();
}
void WaveView::scrollBy (double px) { viewStart += px * hfPerPx; const double lim = -hfPerPx * 40; if (viewStart < lim) viewStart = lim; repaint(); }

juce::Rectangle<float> WaveView::regionRect (const Region& r) const
{
    const auto w = waveArea().toFloat();
    const float x0 = xOf (proc.pfToHf (r.dstStart)), x1 = xOf (proc.pfToHf (r.dstEnd()));
    return { x0, w.getY() + 4.0f, juce::jmax (1.0f, x1 - x0), w.getHeight() - 8.0f };
}
const Region* WaveView::regionAt (juce::Point<float> p) const
{
    const auto& pr = proc.project();
    for (auto it = pr.regions.rbegin(); it != pr.regions.rend(); ++it) if (regionRect (*it).expanded (2.0f, 0).contains (p)) return &*it;
    return nullptr;
}
juce::Rectangle<float> WaveView::fadeHandle (const Region& r, bool in) const
{
    const auto R = regionRect (r);
    const double ms = in ? r.fadeInMs : r.fadeOutMs, pf = ms * 0.001 * proc.project().sampleRate;
    const float dx = (float) (proc.pfToHf (pf) / hfPerPx);
    const float x = in ? R.getX() + dx : R.getRight() - dx;
    return { juce::jlimit (R.getX(), R.getRight() - 9.0f, x - 4.5f), R.getY() + (float) kBar + 1.0f, 9.0f, 9.0f };
}
juce::Rectangle<float> WaveView::stretchGrip (const Region& r) const { const auto R = regionRect (r); return { R.getRight() - 16.0f, R.getBottom() - 16.0f, 15.0f, 15.0f }; }

double WaveView::markerAbsHf (const Region& r, const WarpMarker& w) const { return proc.pfToHf (r.dstStart + (r.reverse ? r.length() - w.t : w.t)); }

WaveView::MarkerHit WaveView::markerAt (juce::Point<float> p) const
{
    MarkerHit best; float bd = 6.0f;
    const auto& pr = proc.project();
    for (auto it = pr.regions.rbegin(); it != pr.regions.rend(); ++it)
    {
        const auto R = regionRect (*it); if (! R.expanded (6, 0).contains (p)) continue;
        for (auto& w : it->warp)
        {
            const float d = std::abs (xOf (markerAbsHf (*it, w)) - p.x);
            if (d < bd) { bd = d; best = { it->id, w.id, w.anchor }; }
        }
        if (best.region != 0) return best;
    }
    return best;
}

double WaveView::snapHf (double hf, bool full, bool none) const
{
    const auto& p = proc.project();
    if (none || p.gridDiv <= 0) return hf;
    juce::ignoreUnused (full);
    return proc.pfToHf (p.snap (proc.hfToPf (hf), 1.0));
}

double WaveView::nearOnsetHf (const Region& r, double hf, float px) const
{
    auto src = proc.getSource(); if (src == nullptr) return hf;
    const auto m = r.map(); double best = hf; float bd = px;
    for (auto& e : proc.alignEvents())
    {
        const int64 s = e.pos;
        if (s <= r.srcIn() || s >= r.srcOut()) continue;
        const double t = m.outAt ((double) s);
        const double ab = proc.pfToHf (r.dstStart + (r.reverse ? r.length() - t : t));
        const float d = std::abs (xOf (ab) - xOf (hf));
        if (d < bd) { bd = d; best = ab; }
    }
    return best;
}

// ---------------------------------------------------------------- painting
void WaveView::paintGrid (juce::Graphics& g, const Project& p)
{
    const auto w = waveArea().toFloat();
    const double beatPf = p.secondsPerBeat() * p.sampleRate;
    double step = p.gridDiv > 0 ? p.gridStep() : beatPf;
    while (proc.pfToHf (step) / hfPerPx < 6.0) step *= 2.0;
    const double a = proc.hfToPf (viewStart), b = proc.hfToPf (hfAt (w.getRight()));
    const double n0 = std::floor ((a - p.beatOffset) / step);
    for (double n = n0; ; n += 1.0)
    {
        const double pf = p.beatOffset + n * step; if (pf > b) break;
        if (pf < 0) continue;
        const double beats = (pf - p.beatOffset) / beatPf;
        const bool bar = std::abs (beats / 4.0 - std::round (beats / 4.0)) < 1e-6;
        g.setColour (bar ? P.gridBar : P.grid);
        g.fillRect (xOf (proc.pfToHf (pf)), w.getY(), bar ? 1.4f : 1.0f, w.getHeight());
    }
}

void WaveView::paintRuler (juce::Graphics& g, const Project& p)
{
    const auto r = ruler().toFloat();
    if (P.glass) { g.setColour (P.line.withAlpha (0.6f)); g.fillRect (r.getX() + 8, r.getBottom() - 1, r.getWidth() - 16, 1.0f); }
    else
    {
        g.setColour (P.header); g.fillRect (r);
        g.setColour (P.line); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1.0f);
    }
    const double sr = proc.hostRate();
    // seconds labels at a readable spacing
    const double pxPerSec = sr / hfPerPx;
    double stepS = 0.001; for (double c : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 }) { stepS = c; if (c * pxPerSec >= 70) break; }
    g.setFont (LnF::uiFont (10.5f, false));
    const double s0 = std::floor (viewStart / sr / stepS) * stepS;
    for (double s = std::max (0.0, s0); ; s += stepS)
    {
        const float x = xOf (s * sr); if (x > r.getRight()) break;
        g.setColour (P.dim); g.fillRect (x, r.getBottom() - 6, 1.0f, 5.0f);
        const auto txt = stepS < 0.01 ? juce::String (s, 3) : stepS < 1.0 ? juce::String (s, 2) : juce::String (s, 0) + " s";
        g.setColour (P.dim); g.drawText (txt, juce::Rectangle<float> (x + 3, r.getY() + 2, 70, 12), juce::Justification::centredLeft, false);
    }
    // bar numbers
    const double barPf = p.secondsPerBeat() * p.sampleRate * 4.0;
    if (proc.pfToHf (barPf) / hfPerPx > 26)
        for (double bar = std::max (0.0, std::floor ((proc.hfToPf (viewStart) - p.beatOffset) / barPf)); ; bar += 1.0)
        {
            const double pf = p.beatOffset + bar * barPf; const float x = xOf (proc.pfToHf (pf)); if (x > r.getRight()) break;
            g.setColour (P.marker.withAlpha (0.9f)); g.drawText (juce::String ((int) bar + 1), juce::Rectangle<float> (x + 2, r.getBottom() - 13, 30, 11), juce::Justification::centredLeft, false);
        }
    // loop
    const auto [la, lb] = proc.loopRange();
    if (lb > la)
    {
        const float x0 = xOf ((double) la), x1 = xOf ((double) lb);
        g.setColour (P.marker.withAlpha (proc.looping() ? 0.85f : 0.3f)); g.fillRoundedRectangle (x0, r.getBottom() - 5, x1 - x0, 4.0f, 2.0f);
    }
    // cursor flag
    const float cx = xOf ((double) proc.cursor());
    juce::Path tri; tri.addTriangle (cx - 5, r.getY() + 1, cx + 5, r.getY() + 1, cx, r.getY() + 8);
    g.setColour (P.cursor); g.fillPath (tri);
}

uint64 repeatKey (const std::vector<const Piece*>& pieces, int64 repeatId)
{
    Hasher h; h.add ("repeat-tile").add (repeatId).add ((int) pieces.size());
    for (auto* pc : pieces)
    {
        h.add (pc->dst0).add (pc->dst1).add (pc->u0).add (pc->regionLen).add (pc->reverse).add (pc->mirror).add (pc->gain)
         .add (pc->fadeIn).add (pc->fadeOut).add (pc->edgeIn).add (pc->edgeOut).add (pc->pitchOffset);
        pc->map.hashInto (h);
    }
    return h.key();
}

juce::Image renderRepeatTile (const SourceAudio& src, const WorkingAudio& work, const std::vector<const Piece*>& pieces, double spanStartHf,
                              int64 col0, double hfPerPhys, int H, const Palette& P)
{
    const int W = WaveTileCache::kTilePx;
    juce::Image img (juce::Image::ARGB, W, juce::jmax (1, H), true);
    juce::Graphics g (img);
    const float mid = (float) H * 0.5f, hh = (float) H * 0.46f;
    const double sfPerWf = 1.0 / juce::jmax (1.0e-9, work.ratio);
    const juce::ColourGradient gWave (juce::Colour (0xff7c5ce6).withAlpha (0.62f), 0, mid - hh * 0.8f, juce::Colour (0xff22c7b0).withAlpha (0.62f), 0, mid + hh * 0.8f, false);
    const juce::ColourGradient gCore (juce::Colour (0xff6a46e0), 0, mid - hh * 0.35f, juce::Colour (0xff12a892), 0, mid + hh * 0.35f, false);
    for (int c = 0; c < W; ++c)
    {
        const double h0 = spanStartHf + (double) (col0 + c) * hfPerPhys, h1 = h0 + hfPerPhys;
        float mn = 0, mx = 0, ms = 0; bool any = false;
        for (auto* pc : pieces)
        {
            if ((double) pc->dst1 <= h0 || (double) pc->dst0 >= h1) continue;
            const double a = std::max (h0, (double) pc->dst0), b = std::min (h1, (double) pc->dst1);
            const int64 um = pc->u0 + (int64) std::llround ((a + b) * 0.5 - (double) pc->dst0);
            const float env = pc->envelope (juce::jlimit (pc->u0, pc->u0 + pc->length() - 1, um));
            if (env <= 0.0f) continue;
            // the source span (SF) this column reads
            double s0 = pc->map.srcAt ((double) pc->u0 + (a - (double) pc->dst0)), s1 = pc->map.srcAt ((double) pc->u0 + (b - (double) pc->dst0));
            if (pc->reverse) { s0 = pc->mirror - 1.0 - s0; s1 = pc->mirror - 1.0 - s1; }
            s0 *= sfPerWf; s1 *= sfPerWf;
            const int64 ia = (int64) std::floor (std::min (s0, s1)), ib = std::max (ia + 1, (int64) std::ceil (std::max (s0, s1)));
            for (int ch = 0; ch < src.channels; ++ch)
            {
                const auto q = src.peaks.query (src.audio, ch, ia, ib);
                const float qn = q.mn * env, qx = q.mx * env, qs = q.ms * env * env;
                if (! any) { mn = qn; mx = qx; ms = qs; any = true; } else { mn = std::min (mn, qn); mx = std::max (mx, qx); ms = std::max (ms, qs); }
            }
        }
        if (! any) continue;
        const float y0 = mid - juce::jlimit (-1.0f, 1.0f, mx) * hh, y1 = mid - juce::jlimit (-1.0f, 1.0f, mn) * hh;
        if (P.glass) g.setGradientFill (gWave); else g.setColour (P.wave);
        g.fillRect ((float) c, y0, 1.0f, juce::jmax (1.0f, y1 - y0));
        const float rms = std::sqrt (ms) * hh;
        if (P.glass) g.setGradientFill (gCore); else g.setColour (P.waveCore);
        g.fillRect ((float) c, mid - rms, 1.0f, juce::jmax (0.0f, 2.0f * rms));
    }
    g.setColour (P.ink.withAlpha (0.10f)); g.fillRect (0.0f, mid, (float) W, 1.0f);
    return img;
}

void WaveView::paintRepeats (juce::Graphics& g, const Project& p, float scale)
{
    repeatTilesDrawn = 0;
    auto src = proc.getSource(); auto work = proc.getWorking();
    if (p.repeats.empty() || src == nullptr || work == nullptr || src->fingerprint != p.source.fingerprint) return;
    const auto& plan = proc.currentPlan();
    const auto W = waveArea().toFloat();
    for (auto& rp : p.repeats)
    {
        std::vector<const Piece*> ps;
        for (auto& pc : plan.pieces) if (pc.repeatId == rp.id && pc.repeatIndex >= 0) ps.push_back (&pc);
        if (ps.empty()) continue;
        const double a = proc.pfToHf (rp.start), b = proc.pfToHf (rp.end());
        const float x0 = xOf (a), x1 = xOf (b);
        if (x1 < W.getX() || x0 > W.getRight()) continue;
        // the region body under the span (the region rect's waveform band), covered, then the repetitions drawn into it
        const juce::Rectangle<float> body (x0, W.getY() + 4.0f + (float) kBar + 1.0f, x1 - x0, W.getHeight() - 8.0f - (float) kBar - 1.0f);
        const auto wr = body.reduced (0, 2.0f);
        if (wr.getHeight() <= 4) continue;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body.getSmallestIntegerContainer().getIntersection (waveArea()));
        g.setColour (P.waveBg.withAlpha (1.0f)); g.fillRect (body);
        g.setColour (P.glass ? juce::Colours::white.withAlpha (0.20f) : P.region); g.fillRect (body);
        const double hfPerPhys = hfPerPx / scale;
        const int Hp = (int) std::ceil (wr.getHeight() * scale);
        const double x0phys = std::round ((double) x0 * scale);
        const double visL = std::max ((double) W.getX(), (double) x0) * scale, visR = std::min ((double) W.getRight(), (double) x1) * scale;
        const int64 t0 = (int64) std::floor ((visL - x0phys) / WaveTileCache::kTilePx), t1 = (int64) std::floor ((visR - x0phys) / WaveTileCache::kTilePx);
        WaveTileCache::Key k; k.region = repeatKey (ps, rp.id); std::memcpy (&k.zoomBits, &hfPerPhys, 8); k.h = Hp; k.scale100 = (int) std::lround (scale * 100);
        k.theme = 16 + (P.glass ? 3 : (int) P.theme); k.focus = true;
        for (int64 t = juce::jmax<int64> (0, t0); t <= t1; ++t)
        {
            k.tile = t;
            auto img = tiles.get (k, [&] { return renderRepeatTile (*src, *work, ps, a, t * WaveTileCache::kTilePx, hfPerPhys, Hp, P); });
            const float x = (float) ((x0phys + (double) (t * WaveTileCache::kTilePx)) / scale);
            g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / scale).translated (x, wr.getY()));
            ++repeatTilesDrawn;
        }
    }
}

void WaveView::paintRegion (juce::Graphics& g, const Region& r, const Project& p, float scale, bool)
{
    const auto R = regionRect (r);
    const auto W = waveArea().toFloat();
    if (R.getRight() < W.getX() || R.getX() > W.getRight()) return;
    const bool selected = r.id == sel.regionId;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (R.getSmallestIntegerContainer().getIntersection (waveArea()));
    auto bar = R.withHeight ((float) kBar);
    if (P.glass)
    {   // GHOST: a faint frosted body, the header bar a small glass capsule (violet glass when selected)
        g.setColour (juce::Colours::white.withAlpha (selected ? 0.30f : 0.20f)); g.fillRoundedRectangle (R, 6.0f);
        ghosty::glass::body (g, bar, 6.0f, selected ? ghosty::glass::violetDeep() : juce::Colours::white, selected ? 0.80f : 0.55f);
    }
    else
    {
        g.setColour (P.region); g.fillRoundedRectangle (R, 4.0f);
        // header bar: label + state
        g.setColour (selected ? P.regionSel.withAlpha (0.9f) : P.header2); g.fillRoundedRectangle (bar, 4.0f);
    }
    const int idx = proc.project().indexOf (r.id) + 1;
    juce::String label = juce::String (idx);
    const double rate = (double) (r.srcOut() - r.srcIn()) / r.length() * p.sampleRate / juce::jmax (1.0, p.source.sampleRate);
    bool extreme = false; for (size_t i = 0; i + 1 < r.warp.size(); ++i) { const double rr = (double) (r.warp[i + 1].s - r.warp[i].s) / (r.warp[i + 1].t - r.warp[i].t) * p.sampleRate / juce::jmax (1.0, p.source.sampleRate); if (limits::extreme (rr)) extreme = true; }
    if (std::abs (rate - 1.0) > 0.001) label << "  " << juce::String (1.0 / rate, 2) << "x";
    if (r.reverse) label << "  REV";
    if (std::abs (r.totalPitchSemis()) > 1e-9) label << "  " << (r.totalPitchSemis() > 0 ? "+" : "") << juce::String (r.totalPitchSemis(), r.pitchCents != 0.0 ? 2 : 0) << " st";
    if (std::abs (r.formantSemis) > 1e-9) label << "  F" << (r.formantSemis > 0 ? "+" : "") << juce::String (r.formantSemis, 1);
    if (std::abs (r.gainDb) > 0.01) label << "  " << (r.gainDb > 0 ? "+" : "") << juce::String (r.gainDb, 1) << " dB";
    g.setFont (LnF::uiFont (10.5f));
    g.setColour (extreme ? P.warn : (selected ? P.onAccent : P.ink));
    if (extreme && selected) g.setColour (P.onAccent);
    g.drawText (label, bar.reduced (5, 0), juce::Justification::centredLeft, true);
    if (extreme) { g.setColour (P.warn); g.drawRoundedRectangle (R.reduced (0.5f), 4.0f, 1.6f); }
    // waveform tiles (physical pixels)
    auto src = proc.getSource();
    const auto wr = R.withTrimmedTop ((float) kBar + 1.0f).reduced (0, 2.0f);
    if (src != nullptr && src->fingerprint == p.source.fingerprint && wr.getHeight() > 4)
    {
        const double hfPerPhys = hfPerPx / scale;
        const int Hp = (int) std::ceil (wr.getHeight() * scale);
        const double regX0phys = std::round ((double) xOf (proc.pfToHf (r.dstStart)) * scale);
        const double visL = std::max ((double) W.getX(), (double) R.getX()) * scale, visR = std::min ((double) W.getRight(), (double) R.getRight()) * scale;
        const int64 t0 = (int64) std::floor ((visL - regX0phys) / WaveTileCache::kTilePx), t1 = (int64) std::floor ((visR - regX0phys) / WaveTileCache::kTilePx);
        WaveTileCache::Key k; k.region = regionKey (p, r, proc.hostRate()); std::memcpy (&k.zoomBits, &hfPerPhys, 8); k.h = Hp; k.scale100 = (int) std::lround (scale * 100); k.theme = P.glass ? 3 : (int) P.theme;
        k.focus = sel.regionId == 0 || selected;
        for (int64 t = juce::jmax<int64> (0, t0); t <= t1; ++t)
        {
            k.tile = t;
            auto img = tiles.get (k, [&] { return renderWaveTile (*src, p, r, proc.hostRate(), t * WaveTileCache::kTilePx, hfPerPhys, Hp, P, k.focus); });
            const float x = (float) ((regX0phys + (double) (t * WaveTileCache::kTilePx)) / scale);
            g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / scale).translated (x, wr.getY()));
        }
    }
    // fades: the curve + a handle
    const double sr = p.sampleRate;
    for (int side = 0; side < 2; ++side)
    {
        const double ms = side == 0 ? r.fadeInMs : r.fadeOutMs;
        if (ms > 0)
        {
            const float dx = (float) (proc.pfToHf (ms * 0.001 * sr) / hfPerPx);
            juce::Path c; const int steps = 24;
            for (int i = 0; i <= steps; ++i)
            {
                const float f = (float) i / steps, gain = fadeShape (f);
                const float x = side == 0 ? wr.getX() + f * dx : wr.getRight() - f * dx;
                const float y = wr.getBottom() - gain * wr.getHeight();
                if (i == 0) c.startNewSubPath (x, y); else c.lineTo (x, y);
            }
            g.setColour (P.marker.withAlpha (0.9f)); g.strokePath (c, juce::PathStrokeType (1.4f));
        }
        if (ws == Workspace::Edit && (selected || ms > 0))
        {
            auto h = fadeHandle (r, side == 0);
            g.setColour (P.marker); g.fillRect (h); g.setColour (P.onAccent); g.drawRect (h, 1.0f);
        }
    }
    if (ws == Workspace::Edit && selected)
    {   // STRETCH grip
        auto gr = stretchGrip (r);
        g.setColour (P.card); g.fillRoundedRectangle (gr, 3.0f); g.setColour (P.marker); g.drawRoundedRectangle (gr, 3.0f, 1.2f);
        juce::Path a; const float cy = gr.getCentreY();
        a.startNewSubPath (gr.getX() + 3, cy); a.lineTo (gr.getRight() - 3, cy);
        a.startNewSubPath (gr.getX() + 6, cy - 3); a.lineTo (gr.getX() + 3, cy); a.lineTo (gr.getX() + 6, cy + 3);
        a.startNewSubPath (gr.getRight() - 6, cy - 3); a.lineTo (gr.getRight() - 3, cy); a.lineTo (gr.getRight() - 6, cy + 3);
        g.strokePath (a, juce::PathStrokeType (1.3f));
    }
    g.setColour (selected ? P.regionSel : P.regionLine); g.drawRoundedRectangle (R.reduced (0.5f), 4.0f, selected ? 1.6f : 1.0f);
}

void WaveView::paintFlex (juce::Graphics& g, const Region& r, const Project& p)
{
    const auto R = regionRect (r);
    const auto wr = R.withTrimmedTop ((float) kBar);
    if (R.getRight() < 0 || R.getX() > (float) getWidth()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (R.getSmallestIntegerContainer().getIntersection (waveArea()));
    // onsets (snap targets) as small ticks at the bottom
    if (proc.getSource() != nullptr)
    {   // the analysis' events of the material (taller = stronger); syllables in violet
        const auto m = r.map();
        for (auto& e : proc.alignEvents())
        {
            if (e.pos <= r.srcIn() || e.pos >= r.srcOut()) continue;
            const double t = m.outAt ((double) e.pos);
            const float x = xOf (proc.pfToHf (r.dstStart + (r.reverse ? r.length() - t : t)));
            const float h = 3.0f + 7.0f * juce::jlimit (0.0f, 1.0f, e.strength);
            g.setColour ((e.flags & analysis::kSyllable) != 0 ? P.pitch : P.dim.withAlpha (e.strength >= 0.45f ? 0.9f : 0.5f));
            g.fillRect (x, wr.getBottom() - 1 - h, 1.0f, h);
        }
    }
    // segment rates between markers (amber when outside 0.5x .. 2x)
    std::vector<std::pair<double, const WarpMarker*>> ms;
    for (auto& w : r.warp) ms.push_back ({ markerAbsHf (r, w), &w });
    std::sort (ms.begin(), ms.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    g.setFont (LnF::uiFont (10.0f));
    for (size_t i = 0; i + 1 < ms.size(); ++i)
    {
        const auto* a = ms[i].second; const auto* b = ms[i + 1].second;
        const double ds = std::abs ((double) (b->s - a->s)), dt = std::abs (b->t - a->t);
        const double rate = ds / dt * p.sampleRate / juce::jmax (1.0, p.source.sampleRate);
        const float x0 = xOf (ms[i].first), x1 = xOf (ms[i + 1].first);
        if (limits::extreme (rate)) { g.setColour (P.warn.withAlpha (0.16f)); g.fillRect (x0, wr.getY(), x1 - x0, wr.getHeight()); }
        if (x1 - x0 > 44 && std::abs (rate - 1.0) > 0.004)
        {
            g.setColour (limits::extreme (rate) ? P.warn : P.marker);
            g.drawText (juce::String (1.0 / rate, 2) + "x", juce::Rectangle<float> (x0, wr.getBottom() - 22, x1 - x0, 13), juce::Justification::centred, false);
        }
    }
    // GHOST ALIGN preview: amber ghosts where the events would go, an arrow from where they are; amber flags on refused ones
    if (alignPreview != nullptr && alignPreviewFor == proc.getHistory().currentPtr())
    {
        const float yb = wr.getBottom() - 30;
        for (auto& it : alignPreview->items)
        {
            if (it.regionId != r.id) continue;
            const float x0 = xOf (proc.pfToHf (it.from)), x1 = xOf (proc.pfToHf (it.to));
            if (it.moves())
            {
                juce::Path ghost; ghost.startNewSubPath (x1, wr.getY() + 10); ghost.lineTo (x1, wr.getBottom() - 8);
                juce::Path dashed; const float dash[] { 5, 3 }; juce::PathStrokeType (1.4f).createDashedStroke (dashed, ghost, dash, 2);
                g.setColour (P.warn.withAlpha (0.95f)); g.fillPath (dashed);
                g.fillEllipse (x0 - 2.5f, yb - 2.5f, 5.0f, 5.0f);
                if (std::abs (x1 - x0) > 2.0f)
                {
                    juce::Line<float> ln (x0, yb, x1, yb); juce::Path arrow; arrow.addArrow (ln, 1.4f, 6.0f, std::min (6.0f, std::abs (x1 - x0)));
                    g.fillPath (arrow);
                }
            }
            else if (it.amber())
            {
                juce::Path flag; flag.addTriangle (x0 - 5, wr.getY() + 22, x0 + 5, wr.getY() + 22, x0, wr.getY() + 12);
                g.setColour (P.warn); g.fillPath (flag); g.fillRect (x0 - 0.5f, wr.getY() + 22, 1.0f, wr.getHeight() - 30);
            }
            else { g.setColour (P.dim.withAlpha (0.7f)); g.drawEllipse (x0 - 2.5f, yb - 2.5f, 5.0f, 5.0f, 1.0f); }
        }
    }
    // markers (USER: filled; AUTO from GHOST ALIGN: hollow)
    for (auto& [x, w] : ms)
    {
        const float px = xOf (x);
        const bool s = w->id == sel.markerId;
        const bool edge = w == &r.warp.front() || w == &r.warp.back();
        g.setColour (edge ? P.markerDim : P.marker);
        g.fillRect (px - (s ? 1.0f : 0.5f), wr.getY(), s ? 2.0f : 1.0f, wr.getHeight());
        juce::Path tri; tri.addTriangle (px - 6, wr.getY(), px + 6, wr.getY(), px, wr.getY() + 9);
        g.setColour (s ? P.playhead : P.marker);
        if (w->isAuto() && ! s) { g.setColour (P.waveBg); g.fillPath (tri); g.setColour (P.marker); g.strokePath (tri, juce::PathStrokeType (1.5f)); }
        else g.fillPath (tri);
        if (w->anchor)
        {   // ANCHOR LOCK: a small lock
            auto b = juce::Rectangle<float> (px - 5, wr.getY() + 11, 10, 8);
            g.setColour (P.marker); g.fillRoundedRectangle (b, 1.5f);
            juce::Path arc; arc.addCentredArc (px, wr.getY() + 11, 3.2f, 3.6f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (1.5f));
        }
    }
}

// PITCH (phases 13 / 14): the notes as violet blobs on a piano roll over the output timeline, the f0 curve on top. Blobs sit at
// where they sound (a note 20 cents sharp sits 20 % of a row high); a moved note leaves a faint ghost at its detected pitch; the
// edited pitch (detected f0 + the compiled shift) is the deeper violet line; opacity = confidence; the selection is outlined.
std::vector<pitchedit::NoteRef> WaveView::pitchNotes() const
{
    auto a = proc.analysis();
    return pitchedit::notesOf (proc.project(), a.get());
}
std::vector<pitchedit::NoteRef> WaveView::selectedNotes() const
{
    std::vector<pitchedit::NoteRef> v; for (auto& n : pitchNotes()) if (isSelected (n)) v.push_back (n); return v;
}

WaveView::PitchRange WaveView::pitchRange() const
{
    PitchRange pr;
    const auto notes = pitchNotes();
    if (notes.empty()) return pr;
    double lo = 1e9, hi = -1e9; for (auto& n : notes) { lo = std::min ({ lo, n.centre, n.target }); hi = std::max ({ hi, n.centre, n.target }); }
    lo = std::floor (lo) - 3; hi = std::ceil (hi) + 3;
    if (hi - lo < 14) { const double c = 0.5 * (lo + hi); lo = std::floor (c - 7); hi = lo + 14; }
    pr.lo = lo; pr.hi = hi; return pr;
}

float WaveView::yOfPitch (double midi) const
{
    const auto w = waveArea().toFloat().withTrimmedTop (4).withTrimmedBottom (4);
    const auto pr = pitchRange();
    return w.getBottom() - (float) ((midi - pr.lo) / (pr.hi - pr.lo)) * w.getHeight();
}
double WaveView::midiAt (float y) const
{
    const auto w = waveArea().toFloat().withTrimmedTop (4).withTrimmedBottom (4);
    const auto pr = pitchRange();
    return pr.lo + (double) (w.getBottom() - y) / juce::jmax (1.0f, w.getHeight()) * (pr.hi - pr.lo);
}
double WaveView::outHfOfSrc (const Region& r, double src) const
{
    const double t = r.map().outAt (src); return proc.pfToHf (r.dstStart + (r.reverse ? r.length() - t : t));
}
juce::Rectangle<float> WaveView::noteRect (const pitchedit::NoteRef& n) const
{
    const auto* r = proc.project().find (n.regionId); if (r == nullptr) return {};
    const double s0 = (double) std::max (n.s0, r->srcIn()), s1 = (double) std::min (n.s1, r->srcOut());
    float x0 = xOf (outHfOfSrc (*r, s0)), x1 = xOf (outHfOfSrc (*r, s1)); if (x1 < x0) std::swap (x0, x1);
    const auto pr = pitchRange();
    const float rowH = (float) (waveArea().getHeight() - 8) / (float) (pr.hi - pr.lo);
    const float h = juce::jlimit (6.0f, 26.0f, rowH * 0.8f), yc = yOfPitch (n.target);
    return { x0, yc - h * 0.5f, juce::jmax (3.0f, x1 - x0), h };
}
std::optional<pitchedit::NoteRef> WaveView::noteAt (juce::Point<float> p) const
{
    for (auto& n : pitchNotes()) if (noteRect (n).expanded (2.0f, 3.0f).contains (p)) return n;
    return std::nullopt;
}

void WaveView::paintPitch (juce::Graphics& g, const Project& p)
{
    const auto w = waveArea().toFloat();
    if (P.glass) { g.setColour (juce::Colours::white.withAlpha (0.45f)); g.fillRect (w); }   // GHOST: frost over the waveform
    else { g.setColour (P.waveBg.withAlpha (0.72f)); g.fillRect (w); }   // the waveform stays visible, faint
    auto a = proc.analysis();
    const auto pr = pitchRange();
    const float rowH = (float) (w.getHeight() - 8) / (float) (pr.hi - pr.lo);
    // piano-roll lanes: black keys shaded, C lines + names
    g.setFont (LnF::uiFont (10.0f));
    for (int m = (int) pr.lo; m < (int) pr.hi; ++m)
    {
        const int pc = ((m % 12) + 12) % 12; const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
        if (black) { g.setColour (P.ink.withAlpha (P.light ? 0.06f : 0.045f)); g.fillRect (w.getX(), y0, w.getWidth(), y1 - y0); }
        if (pc == 0)
        {
            g.setColour (P.ink.withAlpha (0.16f)); g.fillRect (w.getX(), y1, w.getWidth(), 1.0f);
            g.setColour (P.dim); g.drawText ("C" + juce::String (m / 12 - 1), juce::Rectangle<float> (w.getX() + 4, y0, 34, y1 - y0), juce::Justification::centredLeft, false);
        }
    }
    if (a == nullptr)
    {
        g.setColour (P.ink); g.setFont (LnF::uiFont (14.0f));
        g.drawText (proc.analysing() ? "Analysing the pitch..." : "No pitch analysis yet", w, juce::Justification::centred);
        return;
    }
    const auto notes = pitchNotes();
    if (notes.empty())
    {
        g.setColour (P.ink); g.setFont (LnF::uiFont (14.0f));
        g.drawText ("No pitched notes found (unvoiced or polyphonic material)", w, juce::Justification::centred);
    }
    for (auto& r : p.regions)
    {
        if (r.muted) continue;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (regionRect (r).getSmallestIntegerContainer().getIntersection (waveArea()));
        for (auto& n : notes)
        {
            if (n.regionId != r.id) continue;
            const auto blob = noteRect (n);
            if (blob.getRight() < w.getX() || blob.getX() > w.getRight()) continue;
            const float h = blob.getHeight();
            const bool moved = std::abs (n.target - n.centre) > 0.004, s = isSelected (n);
            if (moved)
            {   // the ghost of where it was sung
                auto gh = blob.withY (yOfPitch (n.centre) - h * 0.5f);
                juce::Path gp; gp.addRoundedRectangle (gh, h * 0.45f);
                juce::Path dashed; const float dash[] { 3, 3 }; juce::PathStrokeType (1.0f).createDashedStroke (dashed, gp, dash, 2);
                g.setColour (P.pitch.withAlpha (0.55f)); g.fillPath (dashed);
                g.setColour (P.pitch.withAlpha (0.35f)); g.drawLine (gh.getCentreX(), gh.getCentreY(), blob.getCentreX(), blob.getCentreY(), 1.0f);
            }
            auto fill = P.pitch.withAlpha (0.28f + 0.5f * juce::jlimit (0.0f, 1.0f, n.confidence) + (s ? 0.12f : 0.0f));
            if (P.glass)
            {   // GHOST: a glossy glass capsule (violet; selected = teal)
                const float conf = juce::jlimit (0.0f, 1.0f, n.confidence);
                ghosty::glass::capsule (g, blob, s, conf);
                fill = (s ? ghosty::glass::teal() : ghosty::glass::violet()).withAlpha (0.40f + 0.22f * conf + 0.10f);   // its mid body (label contrast)
            }
            else
            {
                g.setColour (fill); g.fillRoundedRectangle (blob, h * 0.45f);
                g.setColour (s ? P.marker : P.pitch); g.drawRoundedRectangle (blob, h * 0.45f, s ? 2.2f : 1.3f);
            }
            if (n.editId != 0 && ! moved) { g.setColour (P.draw); g.fillEllipse (blob.getRight() - 7, blob.getY() + 2, 4, 4); }   // edited (formant / drift / ...)
            if (blob.getWidth() > 46 && h >= 12)
            {
                const int near = (int) std::lround (n.target); const int ct = (int) std::lround ((n.target - near) * 100.0);
                const auto txt = analysis::pitchClassName (near) + juce::String (near / 12 - 1) + (ct != 0 ? (ct > 0 ? " +" : " ") + juce::String (ct) : juce::String());
                const auto under = P.waveBg.overlaidWith (fill);   // the label in whichever of ink / on-accent reads better on the blob
                g.setColour (P.glass ? juce::Colour (0xff1b1740) : ghosty::themes::contrast (P.ink, under) >= ghosty::themes::contrast (P.onAccent, under) ? P.ink : P.onAccent); g.setFont (LnF::uiFont (10.0f));
                g.drawText (txt, blob.reduced (6, 0), juce::Justification::centredLeft, false);
            }
        }
        const PitchCurve* curve = nullptr;
        for (auto& pc : proc.currentPlan().pieces) if (pc.regionId == r.id && pc.curve != nullptr) { curve = pc.curve.get(); break; }
        if (showF0 || curve != nullptr)
        {   // the f0 curve (broken where it is unvoiced) and, where notes are edited, the pitch they now sound at
            const auto& t = a->pitch;
            juce::Path orig, edited; bool open = false, openE = false; float lastX = -1e9f;
            const int step = juce::jmax (1, (int) (hfPerPx * 0.5 / juce::jmax (1, t.hop)));
            for (int f = 0; f < t.frames(); f += step)
            {
                const double sp = (double) f * t.hop;
                if (sp <= (double) r.srcIn() || sp >= (double) r.srcOut() || t.f0[(size_t) f] <= 0) { open = openE = false; continue; }
                const float x = xOf (outHfOfSrc (r, sp)); if (x < w.getX() - 4 || x > w.getRight() + 4) { open = openE = false; continue; }
                const double m = 69.0 + 12.0 * std::log2 (t.f0[(size_t) f] / 440.0);
                const float y = yOfPitch (m);
                const bool jump = std::abs (x - lastX) > 12.0f;
                if (! open || jump) { orig.startNewSubPath (x, y); open = true; } else orig.lineTo (x, y);
                float c = 0, fm = 0, b = 0; if (curve != nullptr) curve->at (sp * curve->wfPerSf, c, fm, b);
                if (std::abs (c) > 0.5f)
                {
                    const float ye = yOfPitch (m + c / 100.0);
                    if (! openE || jump) { edited.startNewSubPath (x, ye); openE = true; } else edited.lineTo (x, ye);
                }
                else openE = false;
                lastX = x;
            }
            if (showF0) { g.setColour (P.f0.withAlpha (curve != nullptr ? 0.5f : 0.85f)); g.strokePath (orig, juce::PathStrokeType (P.glass ? 2.2f : 1.3f)); }
            g.setColour (P.draw); g.strokePath (edited, juce::PathStrokeType (1.8f));
        }
        for (auto& e : r.pitch)
        {   // drawn ABSOLUTE curves as the user drew them
            if (e.isNote() || ! e.absolute || e.curve.size() < 2) continue;
            juce::Path dp; bool first = true;
            for (auto& [sp, c] : e.curve) { const float x = xOf (outHfOfSrc (r, (double) sp)), y = yOfPitch (c / 100.0); if (first) dp.startNewSubPath (x, y); else dp.lineTo (x, y); first = false; }
            g.setColour (P.draw.withAlpha (0.5f)); g.strokePath (dp, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    if (drag == Drag::Band && ! band.isEmpty()) { g.setColour (P.select); g.fillRect (band); g.setColour (P.pitch); g.drawRect (band, 1.0f); }
    if (drag == Drag::Draw && drawPath.size() > 1)
    {
        juce::Path dp; dp.startNewSubPath (drawPath[0]); for (size_t i = 1; i < drawPath.size(); ++i) dp.lineTo (drawPath[i]);
        g.setColour (P.draw); g.strokePath (dp, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (pitchDraw)
    {
        g.setColour (P.draw); g.setFont (LnF::uiFont (11.0f));
        g.drawText ("DRAW: drag to draw the pitch", w.reduced (8, 6).removeFromTop (16), juce::Justification::topRight, false);
    }
}

// ---------------------------------------------------------------- PITCH editing (phase 14)
bool WaveView::editSelected (const std::function<void (PitchEdit&)>& f, const juce::String& label)
{
    const auto sel = selectedNotes();
    if (sel.empty()) { if (toast) toast ("Select notes first (click a blob, Cmd/Ctrl-click for more, or drag a band)"); return false; }
    std::optional<Project> q = proc.project();
    std::set<int64> regions; for (auto& n : sel) regions.insert (n.regionId);
    for (auto rid : regions)
    {
        std::vector<pitchedit::NoteRef> v; for (auto& n : sel) if (n.regionId == rid) v.push_back (n);
        if (auto nq = ops::editNotes (*q, rid, v, f)) q = nq;
    }
    return applyOp (q, label);
}

bool WaveView::shiftSelected (double cents, bool snap)
{
    const auto sel = selectedNotes();
    if (sel.empty()) return false;
    std::optional<Project> q = proc.project();
    std::set<int64> regions; for (auto& n : sel) regions.insert (n.regionId);
    for (auto rid : regions)
    {
        std::vector<pitchedit::NoteRef> v; for (auto& n : sel) if (n.regionId == rid) v.push_back (n);
        if (auto nq = ops::shiftNotes (*q, rid, v, cents, snap)) q = nq;
    }
    return applyOp (q, "Note pitch");
}

bool WaveView::splitSelected()
{
    const auto sel = selectedNotes();
    if (sel.empty()) { if (toast) toast ("Select a note to SPLIT (or double-click a blob where it should split)"); return false; }
    const auto& n = sel.front();
    const auto* r = proc.project().find (n.regionId); if (r == nullptr) return false;
    int64 at = (n.s0 + n.s1) / 2;
    const double c = proc.hfToPf ((double) proc.cursor()) - r->dstStart;
    if (c > 0 && c < r->length()) { const double uf = r->reverse ? r->length() - c : c; const auto s = (int64) std::llround (r->map().srcAt (uf)); if (s > n.s0 && s < n.s1) at = s; }
    auto a = proc.analysis(); juce::String why;
    const bool ok = applyOp (ops::splitNote (proc.project(), n, at, a != nullptr ? &a->pitch : nullptr, &why), "Split note", why);
    if (ok) { noteSel.clear(); for (auto& m : pitchNotes()) if (m.regionId == n.regionId && (m.s0 == n.s0 || m.s0 == at)) noteSel.insert ({ m.regionId, m.s0 }); }
    return ok;
}

bool WaveView::mergeSelected()
{
    auto a = proc.analysis(); juce::String why;
    const auto sel = selectedNotes();
    const bool ok = applyOp (ops::mergeNotes (proc.project(), sel, a != nullptr ? &a->pitch : nullptr, &why), "Merge notes", why);
    if (ok && ! sel.empty()) { const auto first = *std::min_element (sel.begin(), sel.end(), [] (auto& x, auto& y) { return x.s0 < y.s0; }); noteSel = { { first.regionId, first.s0 } }; }
    return ok;
}

bool WaveView::resetSelected()
{
    const auto sel = selectedNotes();
    if (sel.empty()) { if (toast) toast ("Select the notes to RESET"); return false; }
    const bool ok = applyOp (ops::resetNotes (proc.project(), sel), "Reset notes", "Nothing to reset: the notes are as sung");
    if (ok) { auto keep = noteSel; noteSel.clear(); for (auto& m : pitchNotes()) for (auto& n : sel) if (m.regionId == n.regionId && m.s0 < n.s1 && m.s1 > n.s0) noteSel.insert ({ m.regionId, m.s0 }); }
    return ok;
}

bool WaveView::snapSelected (const pitchedit::Scale& sc, bool all, int* count)
{
    const auto v = all ? pitchNotes() : selectedNotes();
    if (v.empty()) { if (toast) toast (all ? "No notes to snap" : "Select notes to SNAP (or SNAP ALL)"); return false; }
    int n = 0;
    const bool ok = applyOp (ops::snapNotes (proc.project(), v, sc, all ? 0.5f : 0.0f, &n), "Snap to " + pitchedit::scaleName (sc), "Already on the scale");
    if (count) *count = n;
    if (ok && toast) toast ("SNAP " + pitchedit::scaleName (sc) + ": " + juce::String (n) + (n == 1 ? " note" : " notes") + (all ? " (notes under 50 % confidence stay)" : ""));
    return ok;
}

bool WaveView::drawCurve (const std::vector<juce::Point<float>>& path)
{
    if (path.size() < 2) return false;
    const auto* r = regionAt (path.front()); if (r == nullptr) { const double hf = hfAt (path.front().x); for (auto& x : proc.project().regions) if (proc.hfToPf (hf) >= x.dstStart && proc.hfToPf (hf) < x.dstEnd()) r = &x; }
    if (r == nullptr) { if (toast) toast ("Draw over a region"); return false; }
    std::vector<std::pair<int64, double>> pts;
    const auto m = r->map();
    for (auto& pt : path)
    {
        const double lt = proc.hfToPf (hfAt (pt.x)) - r->dstStart;
        if (lt < 0 || lt > r->length()) continue;
        const double uf = r->reverse ? r->length() - lt : lt;
        pts.push_back ({ (int64) std::llround (m.srcAt (uf)), std::round (midiAt (pt.y) * 1000.0) / 10.0 });
    }
    int64 id = 0; juce::String why;
    return applyOp (ops::addDraw (proc.project(), r->id, pts, true, &id, &why), "Pitch draw", why);
}

void WaveView::pitchDown (const juce::MouseEvent& e)
{
    selBefore = noteSel;
    if (pitchDraw) { drag = Drag::Draw; drawPath = { e.position }; return; }
    if (auto n = noteAt (e.position))
    {
        const auto R = noteRect (*n);
        if (! isSelected (*n) && ! e.mods.isCommandDown()) noteSel = { { n->regionId, n->s0 } };
        else if (! isSelected (*n)) noteSel.insert ({ n->regionId, n->s0 });
        dragNote = *n; dragNotes = selectedNotes();
        if (R.getWidth() > 18 && std::abs (e.position.x - R.getX()) <= 5.0f) drag = Drag::NoteStart;
        else if (R.getWidth() > 18 && std::abs (e.position.x - R.getRight()) <= 5.0f) drag = Drag::NoteEnd;
        else drag = e.mods.isCommandDown() ? Drag::NoteTime : Drag::NotePitch;
        proc.beginGesture();
        if (selectionChanged) selectionChanged();
        repaint(); return;
    }
    drag = Drag::Band; band = {};
    if (! e.mods.isCommandDown()) noteSel.clear();
    if (selectionChanged) selectionChanged();
    repaint();
}

void WaveView::pitchDrag (const juce::MouseEvent& e)
{
    juce::String why;
    switch (drag)
    {
        case Drag::NotePitch:
        {
            const double rowH = (double) (yOfPitch (60.0) - yOfPitch (61.0));
            double semis = (double) (down.y - e.position.y) / juce::jmax (1.0, rowH);
            const bool free = e.mods.isShiftDown();
            std::optional<Project> q = before;
            std::set<int64> regions; for (auto& n : dragNotes) regions.insert (n.regionId);
            for (auto rid : regions)
            {
                std::vector<pitchedit::NoteRef> v; for (auto& n : dragNotes) if (n.regionId == rid) v.push_back (n);
                if (auto nq = ops::shiftNotes (*q, rid, v, semis * 100.0, ! free)) q = nq;
            }
            if (*q == proc.project()) break;
            if (*q == before) { proc.apply (before, "Note pitch"); break; }
            applyOp (q, "Note pitch");
            break;
        }
        case Drag::NoteTime: case Drag::NoteStart: case Drag::NoteEnd:
        {
            const double d = proc.hfToPf (hfAt (e.position.x)) - proc.hfToPf (downHf);
            const double dS = drag == Drag::NoteEnd ? 0.0 : d, dE = drag == Drag::NoteStart ? 0.0 : d;
            auto q = ops::noteTiming (before, dragNote, dS, dE, &why);
            if (q.has_value()) proc.apply (*q, "Note timing"); else if (why.isNotEmpty() && toast) toast (why);
            break;
        }
        case Drag::Band:
        {
            band = juce::Rectangle<float> (down, e.position);
            noteSel = e.mods.isCommandDown() ? selBefore : std::set<std::pair<int64, int64>>();
            for (auto& n : pitchNotes()) if (noteRect (n).intersects (band)) noteSel.insert ({ n.regionId, n.s0 });
            if (selectionChanged) selectionChanged();
            break;
        }
        case Drag::Draw: if (drawPath.empty() || e.position.x > drawPath.back().x + 1.5f || e.position.x < drawPath.back().x - 1.5f) drawPath.push_back (e.position); break;
        default: break;
    }
    repaint();
}

void WaveView::pitchUp (const juce::MouseEvent& e)
{
    const auto d = drag;
    if (d == Drag::NotePitch || d == Drag::NoteTime || d == Drag::NoteStart || d == Drag::NoteEnd) proc.endGesture();
    if (d == Drag::Draw)
    {
        auto path = drawPath; drawPath.clear();
        std::sort (path.begin(), path.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        if (moved) drawCurve (path);
    }
    if (! moved && (d == Drag::NotePitch || d == Drag::NoteTime))
    {
        if (e.mods.isCommandDown() && selBefore.count ({ dragNote.regionId, dragNote.s0 })) noteSel.erase ({ dragNote.regionId, dragNote.s0 });   // Cmd-click a selected note: off
        else if (! e.mods.isCommandDown()) noteSel = { { dragNote.regionId, dragNote.s0 } };
    }
    if (! moved && d == Drag::Band) proc.setCursor ((int64) std::llround (std::max (0.0, downHf)));
    band = {};
    if (selectionChanged) selectionChanged();
    repaint();
}

void WaveView::paint (juce::Graphics& g)
{
    const auto& p = proc.project();
    const auto w = waveArea().toFloat();
    if (P.glass)
    {   // GHOST: the editor paints the glass panel under this view (it floats); keep inside its rounded corners
        juce::Path clip; clip.addRoundedRectangle (getLocalBounds().toFloat(), 16.0f); g.reduceClipRegion (clip);
    }
    else { g.setColour (P.waveBg); g.fillRect (w); }
    paintRuler (g, p);
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! p.hasSource() || proc.getSource() == nullptr)
    {
        g.setColour (P.dim);
        const float dash[] { 6, 5 }; juce::Path box; box.addRoundedRectangle (w.reduced (24), 12); juce::Path d;
        juce::PathStrokeType (1.5f).createDashedStroke (d, box, dash, 2); g.fillPath (d);
        if (w.getHeight() > 260 && P.glass)   // GHOST: the mascot on a glass disc
            ghosty::glass::panel (g, juce::Rectangle<float> (128.0f, 128.0f).withCentre ({ w.getCentreX(), w.getCentreY() - 92.0f }), 64.0f, juce::Colours::white, 0.45f, false, 8.0f);
        if (w.getHeight() > 260) drawSliceGhost (g, mascotColours (P), w.getCentreX(), w.getCentreY() - 92.0f, 120.0f);
        g.setColour (P.ink); g.setFont (LnF::uiFont (17.0f));
        const juce::String t = proc.needsRelink() ? "The audio of this project is missing: click RELINK and pick " + p.source.fileName
                             : proc.isLoading() ? "Loading..." : "Drop a WAV / AIFF / FLAC / MP3 here, or click LOAD";
        g.drawFittedText (t, (w.getHeight() > 260 ? w.withTrimmedTop (110) : w).reduced (40).toNearestInt(), juce::Justification::centred, 2);
        if (dropHover) { g.setColour (P.marker.withAlpha (0.18f)); g.fillRect (w); }
        return;
    }
    for (auto& r : p.regions) paintRegion (g, r, p, scale, false);
    paintRepeats (g, p, scale);   // GHOST REPEAT: the spans show what they play, not the original under them
    paintGrid (g, p);   // over the regions (translucent), so the beat lines read on the waveform too
    if (ws == Workspace::Edit && ! chopPreview.empty())
    {   // GHOST CHOP: where SPLIT @ SLICES would cut (dashed marker lines)
        for (auto c : chopPreview)
        {
            const float x = xOf ((double) c); if (x < w.getX() || x > w.getRight()) continue;
            juce::Path l; l.startNewSubPath (x, w.getY() + (float) kBar); l.lineTo (x, w.getBottom());
            juce::Path d; const float dash[] { 5, 4 }; juce::PathStrokeType (1.2f).createDashedStroke (d, l, dash, 2);
            g.setColour (P.slime.withAlpha (0.9f)); g.fillPath (d);
        }
    }
    if (sel.hasRange())
    {
        const float x0 = xOf ((double) sel.a), x1 = xOf ((double) sel.b);
        g.setColour (P.select); g.fillRect (x0, w.getY(), x1 - x0, w.getHeight());
        g.setColour (P.pitch); g.fillRect (x0, w.getY(), 1.0f, w.getHeight()); g.fillRect (x1 - 1, w.getY(), 1.0f, w.getHeight());
    }
    if (ws == Workspace::Flex) for (auto& r : p.regions) paintFlex (g, r, p);
    if (ws == Workspace::Pitch) paintPitch (g, p);
    if (showChords && (ws == Workspace::Pitch || ws == Workspace::Edit)) paintChords (g, chordLabels);
    const auto [la, lb] = proc.loopRange();
    if (proc.looping() && lb > la) { g.setColour (P.marker.withAlpha (0.07f)); g.fillRect (xOf ((double) la), w.getY(), xOf ((double) lb) - xOf ((double) la), w.getHeight()); }
    // cursor + playhead
    const float cx = xOf ((double) proc.cursor());
    {
        juce::Path dl; dl.startNewSubPath (cx, w.getY()); dl.lineTo (cx, w.getBottom());
        juce::Path dd; const float dash[] { 4, 3 }; juce::PathStrokeType (1.0f).createDashedStroke (dd, dl, dash, 2);
        g.setColour (P.cursor.withAlpha (0.8f)); g.fillPath (dd);
    }
    if (proc.isPlaying()) { g.setColour (P.playhead); g.fillRect (xOf ((double) proc.playhead()) - 0.75f, (float) 0, 1.5f, w.getBottom()); }
    if (ws == Workspace::Play)
    {
        auto card = w.withSizeKeepingCentre (juce::jmin (560.0f, w.getWidth() - 40), 110);
        g.setColour (P.card.withAlpha (0.94f)); g.fillRoundedRectangle (card, 10); g.setColour (P.line); g.drawRoundedRectangle (card, 10, 1.2f);
        g.setColour (ws == Workspace::Pitch ? P.pitch : P.marker); g.setFont (LnF::uiFont (20.0f));
        g.drawText (ws == Workspace::Pitch ? "PITCH - coming" : "PLAY - coming", card.removeFromTop (48).translated (0, 8), juce::Justification::centred);
        g.setColour (P.ink); g.setFont (LnF::uiFont (13.0f, false));
        g.drawFittedText (ws == Workspace::Pitch ? "Note blobs on a piano roll: PITCH / FINE / FORMANT / DRIFT / VIBRATO / TRANSITION / TIMING.\nNeeds pitch detection first (phases 13-15)."
                                                 : "Slices on keys with GHOST PERFORMANCE: per-key pitch, direction, stretch, retrigger, envelope.\nComes with MIDI slices (phase 10).",
                          card.reduced (16, 4).toNearestInt(), juce::Justification::centredTop, 3);
    }
    if (dropHover) { g.setColour (P.marker.withAlpha (0.14f)); g.fillRect (w); }
}

// ---------------------------------------------------------------- CHORDS lane
juce::Rectangle<float> WaveView::chordLane() const
{   // the top of the view (EDIT: under the regions' top bars, which stay grabbable)
    const auto w = waveArea().toFloat();
    return { w.getX(), w.getY() + (ws == Workspace::Edit ? (float) kBar + 1.0f : 1.0f), w.getWidth(), (float) kChordLane };
}

std::vector<WaveView::ChordChip> WaveView::chordChips() const
{
    std::vector<ChordChip> out;
    auto a = proc.analysis();
    if (a == nullptr || a->chords == nullptr) return out;
    const auto& p = proc.project();
    const auto lane = chordLane();
    for (auto& r : p.regions)
    {
        if (r.muted) continue;
        const auto R = regionRect (r);
        const int tr = (int) std::lround (p.totalPitchSemis() + r.totalPitchSemis());   // labels as heard
        for (auto& c : a->chords->chords)
        {
            const double s0 = (double) std::max (c.s0, r.srcIn()), s1 = (double) std::min (c.s1, r.srcOut());
            if (s1 <= s0) continue;
            float x0 = xOf (outHfOfSrc (r, s0)), x1 = xOf (outHfOfSrc (r, s1)); if (x1 < x0) std::swap (x0, x1);
            x0 = std::max (x0, R.getX()); x1 = std::min (x1, R.getRight());
            if (x1 < lane.getX() || x0 > lane.getRight() || x1 - x0 < 1.0f) continue;
            ChordChip ch; ch.r = { x0, lane.getY(), x1 - x0, lane.getHeight() }; ch.nc = ! c.isChord();
            ch.label = c.name (tr); ch.confidence = c.confidence;
            out.push_back (ch);
        }
    }
    return out;
}

void WaveView::paintChords (juce::Graphics& g, bool labels)
{
    const auto lane = chordLane();
    auto a = proc.analysis();
    const juce::Font f = LnF::uiFont (11.5f);
    g.setFont (f);
    if (a == nullptr || a->chords == nullptr)
    {   // (the analysis runs in the background: the lane says so instead of blocking)
        const auto t = proc.analysing() ? juce::String ("CHORDS: analysing...") : juce::String ("CHORDS: no analysis yet");
        const auto box = lane.withWidth (ghosty::textWidth (f, t) + 16.0f).translated (6.0f, 0);
        g.setColour (P.chordBg); g.fillRoundedRectangle (box, 5.0f);
        if (labels) { g.setColour (P.chordNc); g.drawText (t, box, juce::Justification::centred, false); }
        return;
    }
    for (auto& ch : chordChips())
    {
        const auto r = ch.r.reduced (1.0f, 0);
        if (ch.nc)
        {   // N.C.: a quiet dashed line; the label only where it fits
            juce::Path l; l.startNewSubPath (r.getX(), r.getCentreY()); l.lineTo (r.getRight(), r.getCentreY());
            juce::Path d; const float dash[] { 3, 4 }; juce::PathStrokeType (1.0f).createDashedStroke (d, l, dash, 2);
            g.setColour (P.chordNc.withAlpha (0.6f)); g.fillPath (d);
            const float tw = ghosty::textWidth (f, ch.label) + 10.0f;
            if (r.getWidth() >= tw + 8.0f)
            {
                const auto box = juce::Rectangle<float> (tw, r.getHeight()).withCentre (r.getCentre());
                g.setColour (P.chordBg); g.fillRoundedRectangle (box, 5.0f);
                if (labels) { g.setColour (P.chordNc); g.drawText (ch.label, box, juce::Justification::centred, false); }
            }
            continue;
        }
        g.setColour (P.chordBg); g.fillRoundedRectangle (r, 5.0f);
        // confidence: the outline's strength (a weak chord reads as weaker)
        g.setColour (P.chordLine.withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, ch.confidence))); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.2f);
        if (labels && r.getWidth() > 14.0f)
        {
            g.setColour (P.chordInk);
            g.drawText (ch.label, r.reduced (4.0f, 0), r.getWidth() >= ghosty::textWidth (f, ch.label) + 10.0f ? juce::Justification::centred : juce::Justification::centredLeft, true);
        }
    }
}

juce::String WaveView::getTooltip()
{
    if (! showChords || (ws != Workspace::Pitch && ws != Workspace::Edit)) return {};
    const auto m = getMouseXYRelative().toFloat();
    if (! chordLane().contains (m)) return {};
    for (auto& ch : chordChips())
        if (ch.r.contains (m))
            return ch.nc ? juce::String ("N.C.: no chord here (silence, one pitch or noise) - CHORDS is read-only, detected from the audio")
                         : "Chord " + ch.label + " (" + juce::String (juce::roundToInt (ch.confidence * 100.0f)) + " % sure) - read-only, detected from the audio; '?' = the root only (the quality is unclear)";
    return "CHORDS: the chord track, read-only (detected from the audio in the background)";
}

// ---------------------------------------------------------------- editing helpers
bool WaveView::applyOp (const std::optional<Project>& q, const juce::String& label, const juce::String& why)
{
    if (q.has_value()) return proc.apply (*q, label);
    if (why.isNotEmpty() && toast) toast (why);
    return false;
}

bool WaveView::cutAtCursorOrSelection()
{
    const auto& p = proc.project(); juce::String why;
    if (sel.hasRange()) return applyOp (ops::splitRange (p, proc.hfToPf ((double) sel.a), proc.hfToPf ((double) sel.b), &why), "Cut", why);
    const double c = proc.hfToPf ((double) proc.cursor());
    for (auto it = p.regions.rbegin(); it != p.regions.rend(); ++it)
        if (c > it->dstStart && c < it->dstEnd())
        {
            int64 right = 0;
            if (applyOp (ops::splitAt (p, it->id, c, &why, &right), "Cut", why)) { sel.regionId = right; if (selectionChanged) selectionChanged(); return true; }
            return false;
        }
    if (toast) toast ("Put the cursor inside a region (or select a range) to cut");
    return false;
}
bool WaveView::splitAtMarkers()
{
    juce::String why; const auto* r = selectedRegion();
    if (r == nullptr) { if (toast) toast ("Select a region first"); return false; }
    return applyOp (ops::splitAtMarkers (proc.project(), r->id, &why), "Split at markers", why);
}
bool WaveView::deleteSelection()
{
    const auto& p = proc.project();
    if (ws == Workspace::Flex && sel.markerId != 0) { const bool ok = removeSelectedMarker(); return ok; }
    if (sel.hasRange())
    {
        const bool ok = applyOp (ops::deleteRange (p, proc.hfToPf ((double) sel.a), proc.hfToPf ((double) sel.b), false), "Delete range", "Nothing to delete there (or an ANCHOR LOCK is inside)");
        if (ok) { sel.a = sel.b = 0; if (selectionChanged) selectionChanged(); }
        return ok;
    }
    if (sel.regionId != 0)
    {
        const bool ok = applyOp (ops::deleteRegion (p, sel.regionId), "Delete region", "ANCHOR LOCK: this region has an anchored marker");
        if (ok) { sel.regionId = 0; if (selectionChanged) selectionChanged(); }
        return ok;
    }
    return false;
}
bool WaveView::duplicateSelection()
{
    if (sel.regionId == 0) { if (toast) toast ("Select a region first"); return false; }
    int64 nid = 0;
    if (applyOp (ops::duplicate (proc.project(), sel.regionId, &nid), "Duplicate")) { sel.regionId = nid; if (selectionChanged) selectionChanged(); return true; }
    return false;
}
bool WaveView::reverseSelection()
{
    const auto* r = selectedRegion(); if (r == nullptr) { if (toast) toast ("Select a region first"); return false; }
    return applyOp (ops::setReverse (proc.project(), r->id, ! r->reverse), r->reverse ? "Forward" : "Reverse", "ANCHOR LOCK: this region has an anchored marker");
}
bool WaveView::nudge (int dir, bool fine)
{
    const auto* r = selectedRegion(); if (r == nullptr) return false;
    const auto& p = proc.project();
    const double step = fine ? p.sampleRate * 0.001 : (p.gridDiv > 0 ? p.gridStep() : p.sampleRate * 0.010);
    juce::String why;
    return applyOp (ops::moveRegion (p, r->id, std::max (0.0, r->dstStart + dir * step), &why), "Nudge", why);
}
bool WaveView::addMarkerAtCursor()
{
    const auto& p = proc.project(); const double c = proc.hfToPf ((double) proc.cursor());
    for (auto it = p.regions.rbegin(); it != p.regions.rend(); ++it)
        if (c > it->dstStart && c < it->dstEnd())
        {
            int64 id = 0; juce::String why;
            if (applyOp (ops::addMarker (p, it->id, c - it->dstStart, &id, &why), "Add marker", why)) { sel.markerId = id; sel.markerRegion = it->id; sel.regionId = it->id; if (selectionChanged) selectionChanged(); return true; }
            return false;
        }
    if (toast) toast ("Put the cursor inside a region");
    return false;
}
bool WaveView::removeSelectedMarker()
{
    if (sel.markerId == 0) { if (toast) toast ("Click a marker first"); return false; }
    juce::String why;
    if (applyOp (ops::removeMarker (proc.project(), sel.markerRegion, sel.markerId, &why), "Remove marker", why)) { sel.markerId = 0; if (selectionChanged) selectionChanged(); return true; }
    return false;
}
bool WaveView::toggleAnchor()
{
    if (sel.markerId == 0) { if (toast) toast ("Click a marker first, then ANCHOR"); return false; }
    const auto* r = proc.project().find (sel.markerRegion); if (r == nullptr) return false;
    for (auto& w : r->warp) if (w.id == sel.markerId) return applyOp (ops::setAnchor (proc.project(), r->id, w.id, ! w.anchor), w.anchor ? "Unlock marker" : "ANCHOR LOCK");
    return false;
}
bool WaveView::bpmFromMarkers()
{
    const auto& p = proc.project();
    std::vector<double> times;   // absolute PF of all markers, sorted
    for (auto& r : p.regions) for (auto& w : r.warp) times.push_back (r.dstStart + (r.reverse ? r.length() - w.t : w.t));
    std::sort (times.begin(), times.end());
    times.erase (std::unique (times.begin(), times.end(), [] (double a, double b) { return std::abs (a - b) < 1.0; }), times.end());
    if (times.size() < 2) { if (toast) toast ("Needs two markers"); return false; }
    double a = -1;
    if (sel.markerId != 0) if (const auto* r = p.find (sel.markerRegion)) for (auto& w : r->warp) if (w.id == sel.markerId) a = r->dstStart + (r->reverse ? r->length() - w.t : w.t);
    if (a < 0) { const double c = proc.hfToPf ((double) proc.cursor()); a = times.front(); for (double t : times) if (t <= c) a = t; }
    double b = -1; for (double t : times) if (t > a + 1.0) { b = t; break; }
    if (b < 0) { if (toast) toast ("Needs a marker after the selected one"); return false; }
    int beats = 1; const double bpm = ops::bpmFromSpan ((b - a) / p.sampleRate, &beats);
    auto q = ops::setTempo (p, bpm); Project np = q ? *q : p; np.beatOffset = a;
    if (! proc.apply (np, "Tempo from markers")) return false;
    if (toast) toast ("BPM " + juce::String (bpm, 2) + " (" + juce::String (beats) + (beats == 1 ? " beat" : " beats") + " between the markers; the grid starts at the first)");
    return true;
}

// ---------------------------------------------------------------- gestures
void WaveView::mouseDown (const juce::MouseEvent& e)
{
    down = e.position; moved = false; before = proc.project(); downHf = hfAt (e.position.x); drag = Drag::None; dragRegion = dragMarker = 0;
    if (proc.getSource() == nullptr) return;
    if (ruler().toFloat().contains (e.position)) { drag = e.mods.isRightButtonDown() ? Drag::RulerLoop : Drag::RulerCursor; return; }
    if (e.mods.isRightButtonDown()) { drag = Drag::Loop; return; }   // click = cursor here, drag = a loop
    if (ws == Workspace::Pitch) { pitchDown (e); return; }
    if (ws == Workspace::Play) return;
    const auto* r = regionAt (e.position);
    if (ws == Workspace::Flex)
    {
        const auto mh = markerAt (e.position);
        if (mh.marker != 0)
        {
            sel.markerId = mh.marker; sel.markerRegion = mh.region; sel.regionId = mh.region;
            if (const auto* mr = proc.project().find (mh.region))
                for (auto& w : mr->warp) if (w.id == mh.marker) dragMarkerT0 = mr->reverse ? mr->length() - w.t : w.t;
            drag = Drag::Marker; dragRegion = mh.region; dragMarker = mh.marker; proc.beginGesture();
            if (selectionChanged) selectionChanged(); repaint(); return;
        }
        sel.markerId = 0;
        if (r != nullptr && (flexMode == FlexMode::Slip || e.mods.isCommandDown())) { drag = Drag::Slip; dragRegion = r->id; sel.regionId = r->id; proc.beginGesture(); repaint(); return; }
        drag = Drag::Range; if (r != nullptr) sel.regionId = r->id;
        repaint(); return;
    }
    if (r != nullptr)
    {
        const auto R = regionRect (*r);
        dragRegion = r->id;
        auto gesture = [&] (Drag d) { drag = d; sel.regionId = r->id; proc.beginGesture(); if (selectionChanged) selectionChanged(); repaint(); };
        if (fadeHandle (*r, true).expanded (2).contains (e.position) && (r->id == sel.regionId || r->fadeInMs > 0)) return gesture (Drag::FadeIn);
        if (fadeHandle (*r, false).expanded (2).contains (e.position) && (r->id == sel.regionId || r->fadeOutMs > 0)) return gesture (Drag::FadeOut);
        if (r->id == sel.regionId && stretchGrip (*r).contains (e.position)) return gesture (Drag::Stretch);
        if (std::abs (e.position.x - R.getRight()) <= kEdge) return gesture (e.mods.isAltDown() ? Drag::Stretch : Drag::TrimR);
        if (std::abs (e.position.x - R.getX()) <= kEdge) return gesture (Drag::TrimL);
        if (e.position.y < R.getY() + (float) kBar) { grabOffset = downHf - proc.pfToHf (r->dstStart); return gesture (Drag::Move); }
        if (e.mods.isCommandDown()) return gesture (Drag::Slip);
        drag = Drag::Range; sel.regionId = r->id;
    }
    else { drag = Drag::Range; sel.regionId = 0; }
    if (selectionChanged) selectionChanged();
    repaint();
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None) return;
    if (! moved && e.getDistanceFromDragStart() < 4) return;
    moved = true;
    const double hf = hfAt (e.position.x);
    const bool alt = e.mods.isAltDown(), cmd = e.mods.isCommandDown(), fine = e.mods.isShiftDown();
    const auto& p = before;
    juce::String why;
    if (ws == Workspace::Pitch && drag >= Drag::NotePitch) { pitchDrag (e); return; }
    switch (drag)
    {
        case Drag::Range:
        {
            const double a = snapHf (std::min (downHf, hf), false, alt || ws == Workspace::Flex), b = snapHf (std::max (downHf, hf), false, alt || ws == Workspace::Flex);
            sel.a = (int64) std::llround (std::max (0.0, a)); sel.b = (int64) std::llround (std::max (0.0, b));
            break;
        }
        case Drag::Loop: case Drag::RulerCursor: case Drag::RulerLoop:
        {
            drag = drag == Drag::RulerCursor ? Drag::RulerLoop : drag;
            const double a = snapHf (std::min (downHf, hf), false, alt), b = snapHf (std::max (downHf, hf), false, alt);
            proc.setLoop ((int64) std::llround (std::max (0.0, a)), (int64) std::llround (std::max (0.0, b)), true);
            break;
        }
        case Drag::Move:
        {
            const double nd = snapHf (hf - grabOffset, false, alt);
            applyOp (ops::moveRegion (p, dragRegion, proc.hfToPf (std::max (0.0, nd)), &why), "Move", why);
            break;
        }
        case Drag::Slip:
        {
            const double dsf = (hf - downHf) * (p.source.sampleRate / proc.hostRate()) * (fine ? 0.25 : 1.0);
            applyOp (ops::slip (p, dragRegion, -(int64) std::llround (dsf), &why), "Slip", why);
            break;
        }
        case Drag::TrimL: case Drag::TrimR:
        {
            const auto* r = p.find (dragRegion); if (r == nullptr) break;
            const double edgePf = proc.hfToPf (snapHf (hf, false, alt));
            const double localOut = edgePf - r->dstStart;
            const bool left = drag == Drag::TrimL;
            const double uf = r->reverse ? r->length() - localOut : localOut;
            const auto m = r->map();
            const int64 ns = (int64) std::llround (m.srcAt (uf));
            applyOp (ops::trim (p, dragRegion, left, ns, &why), "Trim", why);
            break;
        }
        case Drag::Stretch:
        {
            const auto* r = p.find (dragRegion); if (r == nullptr) break;
            const double endPf = proc.hfToPf (snapHf (hf, false, alt));
            applyOp (ops::stretchTo (p, dragRegion, endPf - r->dstStart, &why), "Stretch", why);
            break;
        }
        case Drag::FadeIn: case Drag::FadeOut:
        {
            const auto* r = p.find (dragRegion); if (r == nullptr) break;
            const double pf = proc.hfToPf (hf);
            const double ms = (drag == Drag::FadeIn ? pf - r->dstStart : r->dstEnd() - pf) / p.sampleRate * 1000.0;
            applyOp (drag == Drag::FadeIn ? ops::setFades (p, dragRegion, std::max (0.0, ms), r->fadeOutMs) : ops::setFades (p, dragRegion, r->fadeInMs, std::max (0.0, ms)), "Fade");
            break;
        }
        case Drag::Marker:
        {
            const auto* r = p.find (dragRegion); if (r == nullptr) break;
            // the marker keeps its offset from the grab point; Shift = fine (1/4 speed)
            double t = dragMarkerT0 + (proc.hfToPf (hf) - proc.hfToPf (downHf)) * (fine ? 0.25 : 1.0);
            if (p.gridDiv > 0 && ! alt)
            {
                const float strength = cmd ? 1.0f : snapStrength;
                const double ab = p.snap (r->dstStart + t, strength);
                // the grid pulls only near a line (within 8 px), so free placement between lines stays possible
                if (std::abs (xOf (proc.pfToHf (p.snap (r->dstStart + t, 1.0))) - e.position.x) <= 8.0f || cmd) t = ab - r->dstStart;
            }
            applyOp (ops::dragMarker (p, dragRegion, dragMarker, t, flexMode == FlexMode::Slip ? FlexMode::Stretch : flexMode, &why), "Marker", why);
            break;
        }
        default: break;
    }
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent& e)
{
    if (ws == Workspace::Pitch && drag >= Drag::NotePitch) { pitchUp (e); drag = Drag::None; repaint(); return; }
    const auto d = drag; drag = Drag::None;
    const bool gesture = d == Drag::Move || d == Drag::Slip || d == Drag::TrimL || d == Drag::TrimR || d == Drag::Stretch || d == Drag::FadeIn || d == Drag::FadeOut || d == Drag::Marker;
    if (gesture) proc.endGesture();
    if (! moved)
    {
        if (d == Drag::Range || d == Drag::Loop || d == Drag::RulerCursor)
        {
            proc.setCursor ((int64) std::llround (std::max (0.0, downHf)));
            if (d == Drag::Range) { sel.a = sel.b = 0; }
            if (d == Drag::Range && e.mods.isCtrlDown() && ! e.mods.isRightButtonDown())
            {   // Ctrl-click: the region menu (right-click stays the cursor, like the house waveform)
                juce::PopupMenu m;
                m.addItem (1, "Cut here"); m.addItem (2, "Duplicate"); m.addItem (3, "Reverse"); m.addItem (4, "Delete"); m.addItem (5, "Split at markers");
                juce::Component::SafePointer<WaveView> sp (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [sp] (int r)
                {
                    if (sp == nullptr) return;
                    if (r == 1) sp->cutAtCursorOrSelection(); if (r == 2) sp->duplicateSelection(); if (r == 3) sp->reverseSelection();
                    if (r == 4) sp->deleteSelection(); if (r == 5) sp->splitAtMarkers();
                });
            }
        }
    }
    if (selectionChanged) selectionChanged();
    repaint();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (proc.getSource() == nullptr || ruler().toFloat().contains (e.position)) return;
    if (ws == Workspace::Pitch)
    {   // double-click a blob: SPLIT it there
        if (auto n = noteAt (e.position))
        {
            const auto* nr = proc.project().find (n->regionId); if (nr == nullptr) return;
            const double lt = proc.hfToPf (hfAt (e.position.x)) - nr->dstStart, uf = nr->reverse ? nr->length() - lt : lt;
            auto a = proc.analysis(); juce::String why;
            const auto at = (int64) std::llround (nr->map().srcAt (uf));
            if (applyOp (ops::splitNote (proc.project(), *n, at, a != nullptr ? &a->pitch : nullptr, &why), "Split note", why))
                noteSel = { { n->regionId, n->s0 } };
            if (selectionChanged) selectionChanged();
            repaint();
        }
        return;
    }
    const auto* r = regionAt (e.position); if (r == nullptr) return;
    if (ws == Workspace::Flex)
    {
        if (markerAt (e.position).marker != 0) return;
        const double hf = nearOnsetHf (*r, hfAt (e.position.x), 8.0f);
        int64 id = 0; juce::String why;
        if (applyOp (ops::addMarker (proc.project(), r->id, proc.hfToPf (hf) - r->dstStart, &id, &why), "Add marker", why))
        { sel.markerId = id; sel.markerRegion = r->id; sel.regionId = r->id; if (selectionChanged) selectionChanged(); }
    }
    else if (ws == Workspace::Edit)
    {   // the region's span as the selection
        sel.regionId = r->id; sel.a = (int64) std::llround (proc.pfToHf (r->dstStart)); sel.b = (int64) std::llround (proc.pfToHf (r->dstEnd()));
        if (selectionChanged) selectionChanged();
    }
    repaint();
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    auto cur = juce::MouseCursor::NormalCursor;
    if (const auto* r = regionAt (e.position))
    {
        const auto R = regionRect (*r);
        if (ws == Workspace::Edit)
        {
            if (std::abs (e.position.x - R.getRight()) <= kEdge || std::abs (e.position.x - R.getX()) <= kEdge) cur = juce::MouseCursor::LeftRightResizeCursor;
            else if (e.position.y < R.getY() + (float) kBar) cur = juce::MouseCursor::DraggingHandCursor;
        }
        else if (ws == Workspace::Flex && markerAt (e.position).marker != 0) cur = juce::MouseCursor::LeftRightResizeCursor;
    }
    setMouseCursor (cur);
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoomAround (std::exp (-w.deltaY * 2.0), e.position.x); return; }
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    scrollBy (-d * 320.0);
}
void WaveView::mouseMagnify (const juce::MouseEvent& e, float s) { if (s > 0) zoomAround (1.0 / s, e.position.x); }

bool WaveView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) { const juce::File x (f); if (source::canLoad (x) || projectio::isProjectFile (x)) return true; }
    return false;
}
void WaveView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (auto& f : files)
    {
        const juce::File x (f);
        if (projectio::isProjectFile (x)) { juce::String err; if (! proc.openProject (x, err) && toast) toast (err == "relink" ? proc.lastMessage : err); zoomToFit(); return; }
        if (source::canLoad (x)) { proc.loadAudioAsync (x); firstLayout = true; sel = {}; return; }
    }
}

// ---------------------------------------------------------------- lane strip
std::vector<std::pair<int64, juce::Rectangle<float>>> LaneStrip::blocks() const
{
    const auto& p = proc.project();
    std::vector<const Region*> rs; for (auto& r : p.regions) rs.push_back (&r);
    std::stable_sort (rs.begin(), rs.end(), [] (const Region* a, const Region* b) { return a->dstStart < b->dstStart; });
    const double total = juce::jmax (1.0, p.length());
    auto area = getLocalBounds().toFloat().reduced (6, 8).withTrimmedLeft (70);
    std::vector<std::pair<int64, juce::Rectangle<float>>> out;
    for (auto* r : rs)
    {
        const float x0 = area.getX() + (float) (r->dstStart / total) * area.getWidth();
        const float w = juce::jmax (14.0f, (float) (r->length() / total) * area.getWidth() - 2.0f);
        out.push_back ({ r->id, { x0, area.getY(), w, area.getHeight() } });
    }
    return out;
}
int LaneStrip::slotAt (float x) const
{
    const auto b = blocks(); int slot = 0;
    for (int i = 0; i < (int) b.size(); ++i) if (x > b[(size_t) i].second.getCentreX()) slot = i;
    if (! b.empty() && x < b.front().second.getCentreX()) slot = 0;
    return slot;
}
void LaneStrip::paint (juce::Graphics& g)
{
    if (! P.glass)   // (GHOST: the editor paints the glass panel under the strip)
    {
        g.setColour (P.panel); g.fillRect (getLocalBounds());
        g.setColour (P.line); g.fillRect (0, 0, getWidth(), 1);
    }
    g.setColour (P.dim); g.setFont (LnF::uiFont (10.5f));
    g.drawFittedText ("ARRANGE\ndrag blocks", juce::Rectangle<int> (P.glass ? 12 : 6, P.glass ? 2 : 6, 62, getHeight() - (P.glass ? 4 : 12)), juce::Justification::centredLeft, 2);
    const auto b = blocks(); const auto& p = proc.project();
    for (int i = 0; i < (int) b.size(); ++i)
    {
        const auto& [id, r] = b[(size_t) i];
        const auto* reg = p.find (id);
        const bool s = id == view.sel.regionId, dragged = i == dragFrom;
        auto rr = dragged ? r.withX (r.getX() + dragX) : r;
        if (P.glass) ghosty::glass::pill (g, rr.expanded (1.5f, 1.0f).withTrimmedBottom (-2.0f), s, false, false);
        else
        {
            g.setColour (s ? P.wave : P.cell); g.fillRoundedRectangle (rr, 5.0f);
            g.setColour (s ? P.wave.brighter (0.2f) : P.line); g.drawRoundedRectangle (rr, 5.0f, 1.0f);
        }
        g.setColour (s ? P.onAccent : P.ink); g.setFont (LnF::uiFont (12.0f));
        const int idx = p.indexOf (id) + 1;
        g.drawFittedText (juce::String (idx) + (reg != nullptr && reg->reverse ? " R" : ""), rr.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
    }
    if (dragFrom >= 0 && dragTo >= 0 && dragTo < (int) b.size())
    {
        const auto& r = b[(size_t) dragTo].second;
        const float x = dragTo > dragFrom ? r.getRight() + 1 : r.getX() - 2;
        g.setColour (P.marker); g.fillRect (x - 1.0f, r.getY() - 4, 3.0f, r.getHeight() + 8);
    }
}
void LaneStrip::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = -1; dragTo = -1; dragX = 0;
    const auto b = blocks();
    for (int i = 0; i < (int) b.size(); ++i)
        if (b[(size_t) i].second.contains (e.position))
        {
            dragFrom = i; view.sel.regionId = b[(size_t) i].first; view.sel.a = view.sel.b = 0;
            if (const auto* r = proc.project().find (view.sel.regionId)) proc.setCursor ((int64) std::llround (proc.pfToHf (r->dstStart)));
            if (view.selectionChanged) view.selectionChanged();
            break;
        }
    view.repaint(); repaint();
}
void LaneStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    dragX = (float) e.getDistanceFromDragStartX();
    dragTo = std::abs (dragX) < 4 ? -1 : slotAt (e.position.x);
    repaint();
}
void LaneStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom)
    {
        if (auto q = ops::rearrange (proc.project(), dragFrom, dragTo)) proc.apply (*q, "Rearrange");
        else if (view.toast) view.toast ("ANCHOR LOCK: an anchored region can't move");
    }
    dragFrom = dragTo = -1; dragX = 0;
    repaint(); view.repaint();
}
} // namespace gslice::ui
