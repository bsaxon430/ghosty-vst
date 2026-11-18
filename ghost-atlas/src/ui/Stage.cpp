// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Stage.h"

namespace atlas::ui
{
// ============================================================================================ preparing a picture (background thread)
class Stage::Prepare final : public juce::ThreadPoolJob
{
public:
    Prepare (Stage& s, int w, bool second, juce::Rectangle<float> openArea, float pixelScale, int serialNumber)
        : juce::ThreadPoolJob ("Ghost Atlas picture"), stage (&s), world (w), secondView (second), open (openArea), scale (pixelScale), serial (serialNumber) {}

    JobStatus runJob() override
    {
        const auto& def = atlas::world (world);
        auto view = std::make_shared<View>();
        view->world = world; view->second = secondView;
        const juce::Image picture = loadPicture (secondView ? def.art2 : def.art);
        const float W = (float) kBaseW, H = (float) kBaseH;
        const float lx = secondView ? def.focus2X : def.focusX, ly = secondView ? def.focus2Y : def.focusY;

        // the frame: the picture fills the window; if the point it is framed round would hide behind the glass it is
        // enlarged a little and moved until that point stands in the open part
        float s = 1.0f, ox = 0.0f, oy = 0.0f;
        const auto safe = open.reduced (juce::jmin (70.0f, open.getWidth() * 0.2f), juce::jmin (56.0f, open.getHeight() * 0.2f));
        if (! safe.isEmpty() && ! safe.contains (lx * W, ly * H))
        {
            const float wantX = juce::jlimit (safe.getX(), safe.getRight(), lx * W), wantY = juce::jlimit (safe.getY(), safe.getBottom(), ly * H);
            for (s = 1.0f; s <= 1.3001f; s += 0.01f)
            {
                ox = juce::jlimit (W - s * W, 0.0f, wantX - lx * s * W);
                oy = juce::jlimit (H - s * H, 0.0f, wantY - ly * s * H);
                if (safe.expanded (1.0f).contains (ox + lx * s * W, oy + ly * s * H)) break;
            }
            s = juce::jmin (s, 1.3f);
            ox = juce::jlimit (W - s * W, 0.0f, wantX - lx * s * W);
            oy = juce::jlimit (H - s * H, 0.0f, wantY - ly * s * H);
        }
        view->s = s; view->ox = ox; view->oy = oy; view->scale = scale;

        const int pw = juce::jmax (64, (int) std::lround (W * scale)), ph = juce::jmax (36, (int) std::lround (H * scale));
        juce::Image art (juce::Image::RGB, pw, ph, true, juce::SoftwareImageType());
        if (picture.isValid())
        {
            juce::Graphics g (art);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (picture, juce::Rectangle<float> (ox * scale, oy * scale, s * W * scale, s * H * scale));
        }
        if (shouldExit()) return jobHasFinished;

        // how bright the picture is where lettering will sit over it (a snow field needs more shade than a night sky)
        {
            juce::Image::BitmapData bd (art, juce::Image::BitmapData::readOnly);
            auto light = [&bd, this, pw, ph] (juce::Rectangle<float> r) {
                const auto px = (r * scale).toNearestInt().getIntersection ({ 0, 0, pw, ph });
                double sum = 0; int n = 0;
                for (int y = px.getY(); y < px.getBottom(); y += 6)
                    for (int x = px.getX(); x < px.getRight(); x += 6) { const auto c = bd.getPixelColour (x, y); sum += 0.3 * c.getFloatRed() + 0.59 * c.getFloatGreen() + 0.11 * c.getFloatBlue(); ++n; }
                return n > 0 ? (float) (sum / n) : 0.0f;
            };
            view->topLight = light ({ 0.0f, 0.0f, W, 70.0f });
            view->titleLight = open.isEmpty() ? 0.0f : light ({ open.getX(), open.getY(), 560.0f, 110.0f });
        }

        // the glass: the same picture, soft and darkened towards the world's own shadow colour
        juce::Image soft = blurred (art, 8, juce::jmax (2, (int) std::lround (3.2f * scale)), 3);
        {
            juce::Graphics g (soft);
            const auto deep = juce::Colour (def.deep);
            float light = 0.0f;
            {
                juce::Image::BitmapData bd (soft, juce::Image::BitmapData::readOnly);
                double sum = 0; int n = 0;
                for (int y = 0; y < ph; y += 24) for (int x = 0; x < pw; x += 24) { const auto c = bd.getPixelColour (x, y); sum += 0.3 * c.getFloatRed() + 0.59 * c.getFloatGreen() + 0.11 * c.getFloatBlue(); ++n; }
                light = n > 0 ? (float) (sum / n) : 0.0f;
            }
            g.setColour (deep.darker (0.5f).withAlpha (juce::jlimit (0.56f, 0.86f, 0.5f + 0.62f * light)));      // deeper over a bright picture
            g.fillAll();
        }
        view->art = art; view->soft = soft;
        if (shouldExit()) return jobHasFinished;

        juce::MessageManager::callAsync ([safeStage = stage, view, n = serial] { if (auto* st = safeStage.getComponent()) st->arrived (view, n); });
        return jobHasFinished;
    }

private:
    juce::Component::SafePointer<Stage> stage;
    int world; bool secondView; juce::Rectangle<float> open; float scale; int serial;
};

// ============================================================================================ life
Stage::Stage()
{
    setOpaque (true); setInterceptsMouseClicks (false, false);
    anyFilm = Film::folder().getNumberOfChildFiles (juce::File::findFiles, "*.gafilm") > 0;
    film.onFrame = [this] { repaint(); };
    startTimerHz (30);
}
Stage::~Stage() { stopTimer(); pool.removeAllJobs (true, 4000); }

float Stage::pixelScale() const
{
    float s = juce::Component::getApproximateScaleFactorForComponent (this);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) s *= (float) d->scale;
    return juce::jlimit (0.5f, 2.0f, s);
}

void Stage::setScene (int world, bool secondView, std::vector<Pane> newPanes, juce::Rectangle<float> openArea, bool showTitle)
{
    world = juce::jlimit (0, kWorlds, world);
    const bool different = world != wantWorld || secondView != wantSecond || openArea != open || ! asked;
    wantWorld = world; wantSecond = secondView; panes = std::move (newPanes); open = openArea; title = showTitle;
    if (weatherKind != atlas::world (world).weather) { weatherKind = atlas::world (world).weather; motes.clear(); }
    if (different) request();
    repaint();
}

void Stage::request()
{
    if (getWidth() <= 0) return;
    preparedScale = pixelScale();
    ++serial;
    asked = true; waiting = true;
    pool.addJob (new Prepare (*this, wantWorld, wantSecond, open, preparedScale, serial), true);
}

void Stage::arrived (std::shared_ptr<View> v, int n)
{
    if (n != serial || v == nullptr) return;          // a newer picture has been asked for since
   #if JUCE_MAC
    v->art = juce::NativeImageType().convert (v->art);      // one copy now, so that every repaint afterwards is a plain blit
    v->soft = juce::NativeImageType().convert (v->soft);
   #endif
    waiting = false;
    incoming = std::move (v);
    fade = 0.0f; fading = true;
    repaint();
}

void Stage::scaleChanged() { resizeAt = juce::Time::getMillisecondCounter(); }
void Stage::resized() { if (! asked && getWidth() > 0) request(); }
void Stage::setVeil (float amount) { veil = juce::jlimit (0.0f, 1.0f, amount); }
void Stage::setMotion (int mode)
{
    motionMode = juce::jlimit ((int) motionStill, (int) motionFilm, mode);
    moving = motionMode != motionStill;
    if (! moving) { motes.clear(); haze.clear(); }
    repaint();
}

void Stage::waitUntilSettled (int timeoutMs)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    while ((! settled() || pool.getNumJobs() > 0) && (int) (juce::Time::getMillisecondCounter() - t0) < timeoutMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        advance (true);          // (a picture drawn off screen has no window to tell it that it is showing)
    }
    for (int i = 0; i < 12; ++i) advance (true);
}

// ============================================================================================ time
void Stage::advance (bool evenIfHidden)
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = lastTick == 0 ? 0.033f : juce::jlimit (0.005f, 0.1f, (float) (now - lastTick) * 0.001f);
    lastTick = now;
    const bool visible = evenIfHidden || offscreen || isShowing();

    // the film: asked for while its world's painting is in full view, let go the moment it is not
    {
        const bool wanted = filmWanted (visible);
        FilmPlayer::Ask a;
        if (wanted)
        {
            // (the film is made at the size of the window's own pixels and laid down one for one: letting the drawing
            //  enlarge a smaller picture was tried and cost more than making every pixel here)
            a.world = atlas::world (current->world).key;
            a.pixelW = current->art.getWidth(); a.pixelH = current->art.getHeight();
            a.left = current->ox * current->scale; a.top = current->oy * current->scale;
            a.width = current->s * (float) kBaseW * current->scale; a.height = current->s * (float) kBaseH * current->scale;
            a.running = true;
            filmW = a.pixelW; filmH = a.pixelH;
        }
        film.ask (a);
        const float before = filmShow;
        filmShow = juce::jlimit (0.0f, 1.0f, filmShow + (wanted ? 1.0f : -1.0f) * dt * 2.6f);
        if (std::abs (filmShow - before) > 0.0005f) repaint();
    }
    if (! visible) return;

    if (resizeAt != 0 && now - resizeAt > 260u)
    {
        resizeAt = 0;
        if (std::abs (pixelScale() - preparedScale) > 0.02f * preparedScale) request();
    }
    if (fading)
    {
        fade += dt / 0.6f;
        if (fade >= 1.0f) { current = std::move (incoming); incoming.reset(); fading = false; fade = 0.0f; }
        repaint();
    }
    if (std::abs (veil - veilNow) > 0.002f) { veilNow += (veil - veilNow) * juce::jmin (1.0f, dt * 9.0f); repaint(); }
    else veilNow = veil;

    if (current != nullptr && ! open.isEmpty() && veilNow < 0.98f)
    {
        if (moving) clock += dt;
        if (moving && filmShow < 0.99f) { stepWeather (dt); repaint (open.toNearestInt()); }                 // the weather (it thins out as the film comes in)
    }
}

bool Stage::filmWanted (bool visible) const
{
    if (! visible || motionMode != motionFilm || ! anyFilm || current == nullptr || current->second || current->world < 0 || fading || waiting || open.isEmpty() || veilNow > 0.5f) return false;
    auto& there = filmThere[(size_t) juce::jlimit (0, kWorlds, current->world)];
    if (there == 0) there = Film::exists (atlas::world (current->world).key) ? 1 : 2;
    return there == 1;
}

// ============================================================================================ weather
void Stage::spawn (Mote& m, bool anywhere)
{
    auto r = [this] { return rng.nextFloat(); };
    m = Mote();
    m.phase = r() * 6.283f; m.life = 6.0f + r() * 8.0f; m.age = anywhere ? r() * m.life : 0.0f;
    switch (weatherKind)
    {
        case wMotes:    m.x = r(); m.y = anywhere ? r() : 0.55f + 0.45f * r(); m.vx = 0.006f + 0.012f * r(); m.vy = -0.008f - 0.014f * r(); m.size = 1.0f + 1.6f * r(); break;
        case wEmbers:   m.x = r(); m.y = anywhere ? r() : 1.02f; m.vx = (r() - 0.5f) * 0.02f; m.vy = -0.025f - 0.04f * r(); m.size = 1.0f + 1.5f * r(); m.life = 5.0f + 5.0f * r(); break;
        case wPetals:   m.x = anywhere ? r() : r() * 1.2f - 0.2f; m.y = anywhere ? r() : -0.04f; m.vx = 0.02f + 0.03f * r(); m.vy = 0.03f + 0.035f * r(); m.size = 3.0f + 3.0f * r(); m.spin = (r() - 0.5f) * 3.0f; m.life = 30.0f; break;
        case wLanterns: m.x = r(); m.y = anywhere ? 0.35f + 0.6f * r() : 0.95f; m.vx = (r() - 0.5f) * 0.006f; m.vy = -0.006f - 0.01f * r(); m.size = 3.0f + 4.0f * r(); m.life = 14.0f + 10.0f * r(); break;
        case wRain:     m.x = r() * 1.1f; m.y = anywhere ? r() : -0.1f; m.vx = -0.05f; m.vy = 0.9f + 0.5f * r(); m.size = 10.0f + 16.0f * r(); m.life = 30.0f; break;
        case wRings:    m.x = r(); m.y = 0.58f + 0.4f * r(); m.size = 4.0f; m.life = 3.0f + 3.0f * r(); break;
        case wSnow:     m.x = r(); m.y = anywhere ? r() : -0.03f; m.vx = (r() - 0.5f) * 0.02f; m.vy = 0.02f + 0.035f * r(); m.size = 1.2f + 2.2f * r(); m.life = 60.0f; break;
        case wStars:    m.x = r(); m.y = r() * 0.5f; m.size = 0.9f + 1.5f * r(); m.life = 3.0f + 6.0f * r(); break;
        default:        m.x = anywhere ? r() : -0.3f; m.y = 0.45f + 0.4f * r(); m.vx = 0.006f + 0.008f * r(); m.size = 120.0f + 160.0f * r(); m.life = 60.0f; break;
    }
}

// mist lying low over the water: a few long, faint bands that slide slowly sideways and come and go
void Stage::spawnHaze (Mote& m, bool anywhere)
{
    auto r = [this] { return rng.nextFloat(); };
    m = Mote();
    m.life = 16.0f + 14.0f * r(); m.age = anywhere ? r() * m.life : 0.0f; m.phase = r() * 6.283f;
    m.x = r(); m.y = 0.56f + 0.4f * r();
    m.vx = (r() < 0.5f ? -1.0f : 1.0f) * (0.004f + 0.007f * r());
    m.size = 170.0f + 170.0f * r();
}

void Stage::stepWeather (float dt)
{
    static const int counts[] = { 30, 24, 20, 10, 46, 9, 50, 28, 4 };
    const size_t want = (size_t) counts[juce::jlimit (0, 8, weatherKind)];
    if (motes.size() != want) { motes.resize (want); for (auto& m : motes) spawn (m, true); }
    if (haze.size() != 5) { haze.resize (5); for (auto& m : haze) spawnHaze (m, true); }
    for (auto& m : haze)
    {
        m.age += dt; m.phase += dt; m.x += m.vx * dt;
        if (m.age > m.life || m.x < -0.3f || m.x > 1.3f) spawnHaze (m, false);
    }
    for (auto& m : motes)
    {
        m.age += dt; m.phase += dt;
        m.x += m.vx * dt; m.y += m.vy * dt;
        if (weatherKind == wPetals || weatherKind == wSnow) m.x += std::sin (m.phase * 1.3f) * 0.004f * dt * 8.0f;
        if (weatherKind == wEmbers) m.x += std::sin (m.phase * 2.1f) * 0.003f;
        if (m.age > m.life || m.x < -0.35f || m.x > 1.25f || m.y < -0.15f || m.y > 1.1f) spawn (m, false);
    }
}

void Stage::drawHaze (juce::Graphics& g, float alpha)
{
    if (! moving || haze.empty()) return;
    const auto colour = look().accent.interpolatedWith (juce::Colours::white, 0.55f);
    for (const auto& m : haze)
    {
        const float inOut = juce::jmin (1.0f, m.age * 0.25f) * juce::jmin (1.0f, (m.life - m.age) * 0.25f);
        const float a = alpha * juce::jlimit (0.0f, 1.0f, inOut) * (0.075f + 0.03f * std::sin (m.phase * 0.9f));
        if (a <= 0.003f) continue;
        const juce::Point<float> c (open.getX() + m.x * open.getWidth(), open.getY() + m.y * open.getHeight());
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, 0.085f, c.x, c.y));        // long and low
        glow (g, c, m.size, colour, a);
    }
}

void Stage::drawWeather (juce::Graphics& g, float alpha)
{
    if (! moving || motes.empty()) return;
    const auto& l = look();
    const float ox = open.getX(), oy = open.getY(), w = open.getWidth(), h = open.getHeight();
    for (const auto& m : motes)
    {
        const float x = ox + m.x * w, y = oy + m.y * h;
        const float inOut = juce::jmin (1.0f, m.age * 1.2f) * juce::jmin (1.0f, (m.life - m.age) * 1.2f);     // each one fades in and out
        const float a = alpha * juce::jlimit (0.0f, 1.0f, inOut);
        switch (weatherKind)
        {
            case wMotes:
                g.setColour (l.lantern.withAlpha (a * (0.35f + 0.3f * std::sin (m.phase * 1.7f))));
                g.fillEllipse (x - m.size * 0.5f, y - m.size * 0.5f, m.size, m.size);
                break;
            case wEmbers:
                g.setColour (juce::Colour (0xffffb078).withAlpha (a * (0.45f + 0.35f * std::sin (m.phase * 3.0f))));
                g.fillEllipse (x - m.size * 0.5f, y - m.size * 0.5f, m.size, m.size);
                break;
            case wPetals:
            {
                juce::Path p; p.addEllipse (-m.size * 0.5f, -m.size * 0.28f, m.size, m.size * 0.56f);
                g.setColour (juce::Colour (0xfffbe6f2).withAlpha (a * 0.75f));
                g.fillPath (p, juce::AffineTransform::rotation (m.phase * m.spin).translated (x, y));
                break;
            }
            case wLanterns:
                glow (g, { x, y }, m.size * 3.2f, juce::Colour (0xffffb070), a * (0.5f + 0.2f * std::sin (m.phase)));
                break;
            case wRain:
                g.setColour (juce::Colour (0xffd8fff0).withAlpha (a * 0.22f));
                g.drawLine (x, y, x - m.size * 0.06f, y + m.size, 1.0f);
                break;
            case wRings:
            {
                const float t = m.age / m.life, rw = 6.0f + 34.0f * t;
                g.setColour (l.accent.withAlpha (a * 0.55f * (1.0f - t)));
                g.drawEllipse (x - rw, y - rw * 0.3f, rw * 2.0f, rw * 0.6f, 1.1f);
                break;
            }
            case wSnow:
                g.setColour (juce::Colours::white.withAlpha (a * 0.8f));
                g.fillEllipse (x - m.size * 0.5f, y - m.size * 0.5f, m.size, m.size);
                break;
            case wStars:
                g.setColour (juce::Colours::white.withAlpha (a * (0.4f + 0.5f * std::sin (m.phase * 2.2f))));
                g.fillEllipse (x - m.size * 0.5f, y - m.size * 0.5f, m.size, m.size);
                break;
            default:
                glow (g, { x, y }, m.size, juce::Colours::white, a * 0.05f);
                break;
        }
    }
}

// ============================================================================================ painting
void Stage::drawView (juce::Graphics& g, const View& v, float alpha, bool withLife)
{
    const auto& l = look();
    const auto b = getLocalBounds().toFloat();

    // the film lies over its painting: not at all where the run begins and ends (there it is the painting), wholly in between
    float filmed = 0.0f;
    FilmPlayer::Shown shown;
    if (filmShow > 0.001f && &v == current.get() && v.world >= 0 && film.latest (atlas::world (v.world).key, shown)
        && shown.image.getWidth() == filmW && shown.image.getHeight() == filmH)
    {
        const float t = juce::jlimit (0.0f, 1.0f, shown.position / 0.7f);
        filmed = filmShow * t * t * (3.0f - 2.0f * t);
    }
    g.setOpacity (alpha);
    if (filmed < 0.999f) g.drawImage (v.art, b);
    if (filmed > 0.001f) { g.setOpacity (alpha * filmed); g.drawImage (shown.image, b); }

    if (! open.isEmpty())
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (open.toNearestInt());
        const float weather = alpha * (1.0f - filmed);          // (the film has its own rain, petals and snow)
        if (withLife && weather > 0.01f) drawHaze (g, weather);
        if (withLife && weather > 0.01f) drawWeather (g, weather);
    }

    for (const auto& p : panes)
    {
        if (p.kind == glass)
        {
            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path shape; shape.addRoundedRectangle (p.r, p.radius);
                g.reduceClipRegion (shape);
                g.setOpacity (alpha);
                g.drawImage (v.soft, b);
            }
            g.setColour (l.text (0.11f * alpha));
            g.drawRoundedRectangle (p.r.reduced (0.5f), p.radius, 1.0f);
        }
        else if (p.kind == scrimTop || p.kind == scrimBottom)
        {
            const auto dark = l.deep.darker (0.75f);
            const bool top = p.kind == scrimTop;
            const float topAlpha = juce::jlimit (0.0f, 0.97f, 0.66f + 0.5f * v.topLight);
            juce::ColourGradient grad (dark.withAlpha ((top ? topAlpha : 0.0f) * alpha), 0.0f, p.r.getY(), dark.withAlpha ((top ? 0.0f : 0.9f) * alpha), 0.0f, p.r.getBottom(), false);
            if (top) grad.addColour (0.5, dark.withAlpha (topAlpha * 0.62f * alpha)); else grad.addColour (0.45, dark.withAlpha (0.62f * alpha));
            g.setGradientFill (grad);
            g.fillRect (p.r);
        }
    }

    if (title && ! open.isEmpty() && v.world >= 0)
    {
        const auto& def = atlas::world (v.world);
        const auto at = open.getTopLeft() + juce::Point<float> (26.0f, 14.0f);
        const float shade = juce::jlimit (0.0f, 1.0f, (v.titleLight - 0.3f) * 2.2f);
        if (shade > 0.01f)
        {   // a soft shadow lying behind the name, as wide as it is
            juce::Graphics::ScopedSaveState ss (g);
            const juce::Point<float> c (at.x + 210.0f, at.y + 48.0f);
            g.addTransform (juce::AffineTransform::scale (1.0f, 0.3f, c.x, c.y));
            glow (g, c, 430.0f, l.deep.darker (0.8f), juce::jmin (0.96f, 1.15f * shade) * alpha);
        }
        softShadowText (g, def.name, { at.x, at.y, 620.0f, 64.0f }, juce::Justification::centredLeft, fellItalic (58.0f), l.text (alpha), juce::Colours::black.withAlpha ((0.32f + 0.3f * shade) * alpha));
        softShadowText (g, def.line, { at.x + 3.0f, at.y + 62.0f, 620.0f, 22.0f }, juce::Justification::centredLeft, sans (16.5f), l.text ((0.86f + 0.12f * shade) * alpha), juce::Colours::black.withAlpha ((0.4f + 0.3f * shade) * alpha));
    }
}

void Stage::paint (juce::Graphics& g)
{
    if (onDrawBegins)
    {
        const auto clip = g.getClipBounds();
        onDrawBegins (clip.getWidth() * clip.getHeight() * 5 >= getWidth() * getHeight() * 3, g.getInternalContext().getPhysicalPixelScaleFactor());
    }
    const auto& l = look();
    g.fillAll (l.deep.darker (0.7f));
    if (current != nullptr) drawView (g, *current, 1.0f, ! fading);
    if (incoming != nullptr) { const float t = juce::jlimit (0.0f, 1.0f, fade); drawView (g, *incoming, t * t * (3.0f - 2.0f * t), true); }
    if (veilNow > 0.004f) { g.setColour (l.deep.darker (0.82f).withAlpha (0.74f * veilNow)); g.fillRect (getLocalBounds()); }
}
} // namespace atlas::ui
