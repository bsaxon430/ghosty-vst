// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Live.h"

namespace matter
{
void NoteQueue::push (int note, float velocity) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) ev[(size_t) s1] = { note, velocity };
    else if (n2 > 0) ev[(size_t) s2] = { note, velocity };
    fifo.finishedWrite (n1 + n2);
}

void LiveEngine::prepare (double sampleRate, int maxBlock)
{
    (void) hood::liveBase ("pm");   // builds the UNDER THE HOOD index now, not on the audio thread
    sr = sampleRate;
    scratch.setSize (2, juce::jmax (64, maxBlock));
    space.prepare (sr, maxBlock);
    pattern.prepare (sr);
    limiter.prepare (sr, -1.5f);
    bounce.prepare (sr);
    xfadeLen = juce::jmax (1, (int) (0.04 * sr));
    reset();
}

void LiveEngine::reset()
{
    for (auto& v : voices) v = Voice {};
    held.fill (false); heldCount = 0;
    space.reset(); limiter.reset();
    pattern.set (ctl.steps, ctl.hits, ctl.rotate);
    lastStepIndex = -1; clockBeats = 0;
    gainNow = juce::Decibels::decibelsToGain (ctl.levelDb);
}

void LiveEngine::setMatter (const Matter* m) noexcept
{
    if (m == cur) return;
    prev = cur;
    cur = m;
    xfade = prev != nullptr ? xfadeLen : 0;
}

float LiveEngine::readAt (const Matter& m, double pos, int ch, bool loop) noexcept
{
    const int n = m.length();
    if (n < 4) return 0.0f;
    const float* x = m.audio.getReadPointer (ch);
    const int fade = loop ? m.loopFrom : 0;
    auto sample = [&] (double p) -> float
    {
        // cubic (Hermite) between the four nearest samples
        if (loop && fade > 0) { const double span = (double) (n - fade); while (p >= n) p -= span; }
        if (p < 0 || p >= n - 1) return 0.0f;
        const int i = (int) p; const float t = (float) (p - i);
        const float xm1 = x[juce::jmax (0, i - 1)], x0 = x[i], x1 = x[juce::jmin (n - 1, i + 1)], x2 = x[juce::jmin (n - 1, i + 2)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    };
    if (loop && fade > 0)
    {
        // the last `fade` samples cross-fade (equal power) into the start: going round never clicks
        const double span = (double) (n - fade);
        double p = pos;
        if (p >= n) p = std::fmod (p - fade, span) + fade;
        if (p >= n - fade)
        {
            const float t = (float) ((p - (n - fade)) / fade);
            return sample (p) * std::cos (t * 1.5707963f) + sample (p - span) * std::sin (t * 1.5707963f);
        }
        return sample (p);
    }
    return sample (pos);
}

void LiveEngine::start (int note, float vel, bool fromPattern, float pan, int gateSamples) noexcept
{
    int sound = 0;
    for (auto& v : voices) if (v.active && v.kill == 0) ++sound;
    if (sound >= kMaxSounding)
    {
        // the oldest sounding voice fades out in 5 ms (no click) and makes room
        Voice* oldest = nullptr;
        for (auto& v : voices) if (v.active && v.kill == 0 && (oldest == nullptr || v.age < oldest->age)) oldest = &v;
        if (oldest != nullptr) oldest->kill = juce::jmax (1, (int) (0.005 * sr));
    }
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        for (auto& v : voices) if (slot == nullptr || v.env < slot->env) slot = &v;
    }
    auto& v = *slot;
    v = Voice {};
    v.active = true; v.note = note; v.vel = vel; v.fromPattern = fromPattern; v.age = ++ageCounter;
    v.inc = std::pow (2.0, (note - kRootNote) / 12.0) * kRate / sr;
    const int len = cur != nullptr ? cur->length() : 0;
    v.pos = ctl.start * juce::jmax (0, len - 1);
    v.gateLeft = gateSamples;
    v.pan = juce::jlimit (-1.0f, 1.0f, pan);
    v.panL = std::sqrt (0.5f * (1.0f - v.pan)) * 1.41421356f; v.panR = std::sqrt (0.5f * (1.0f + v.pan)) * 1.41421356f;
}

void LiveEngine::noteOn (int note, float vel) noexcept
{
    note = juce::jlimit (0, 127, note);
    if (! held[(size_t) note]) { held[(size_t) note] = true; ++heldCount; }
    lastNote = note;
    if (! ctl.pattern) start (note, vel, false, 0.0f, -1);
}

void LiveEngine::noteOff (int note) noexcept
{
    note = juce::jlimit (0, 127, note);
    if (held[(size_t) note]) { held[(size_t) note] = false; --heldCount; }
    for (auto& v : voices) if (v.active && ! v.fromPattern && v.note == note) v.released = true;
}

void LiveEngine::allOff() noexcept
{
    held.fill (false); heldCount = 0;
    for (auto& v : voices) if (v.active) v.released = true;
}

void LiveEngine::patternTick (int offset, int n, const Transport& tp) noexcept
{
    static constexpr double stepBeats[4] = { 1.0, 0.5, 0.25, 0.125 };
    const double sb = stepBeats[juce::jlimit (0, 3, ctl.rate)];
    const double bpm = juce::jlimit (20.0, 400.0, tp.bpm);
    const double beatsPerSample = bpm / 60.0 / sr;
    const double b0 = tp.playing ? tp.ppq + offset * beatsPerSample : clockBeats;
    for (int i = 0; i < n; ++i)
    {
        const double beat = b0 + i * beatsPerSample;
        const long long idx = (long long) std::floor (beat / sb + 1e-9);
        if (idx != lastStepIndex)
        {
            lastStepIndex = idx;
            int off = 0;
            const bool hit = pattern.tick (off);
            lastStep = pattern.step();
            if (heldCount > 0 && hit)
            {
                const float pan = ((++hitCount % 3) - 1) * 0.6f;   // ours: the hits step left / centre / right
                const int gate = juce::jmax (64, (int) (ctl.gate * sb / beatsPerSample));
                int k = 0;
                for (int note = 0; note < 128 && k < 4; ++note)
                    if (held[(size_t) note]) { start (juce::jmin (127, note + off), 0.85f, true, pan * (k % 2 == 0 ? 1.0f : -1.0f), gate); ++k; }
            }
        }
        (void) i;
    }
    if (! tp.playing) clockBeats += n * beatsPerSample;
}

void LiveEngine::renderVoices (float* L, float* R, int n) noexcept
{
    const float att = 1.0f / juce::jmax (1.0f, ctl.attack * (float) sr);
    const float rel = std::exp (std::log (1e-4f) / juce::jmax (1.0f, ctl.release * (float) sr));
    const bool loop = ctl.loop;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        for (int i = 0; i < n; ++i)
        {
            if (v.gateLeft == 0) v.released = true;
            if (v.gateLeft > 0) --v.gateLeft;
            if (v.kill > 0)
            {
                v.env *= 1.0f - 1.0f / (float) v.kill;
                if (--v.kill == 0) { v.active = false; break; }
            }
            else if (! v.released) v.env = juce::jmin (1.0f, v.env + att);
            else { v.env *= rel; if (v.env < 1e-4f) { v.active = false; break; } }
            float l = 0, r = 0;
            if (cur != nullptr)
            {
                l = readAt (*cur, v.pos, 0, loop); r = readAt (*cur, v.pos, 1, loop);
                if (xfade > 0 && prev != nullptr)
                {
                    const float t = 1.0f - (float) (xfade - i) / (float) xfadeLen;   // per sample through the block
                    const float tt = juce::jlimit (0.0f, 1.0f, t);
                    l = l * tt + readAt (*prev, v.pos, 0, loop) * (1.0f - tt);
                    r = r * tt + readAt (*prev, v.pos, 1, loop) * (1.0f - tt);
                }
            }
            const float g = v.env * v.vel;
            L[i] += l * g * v.panL; R[i] += r * g * v.panR;
            v.pos += v.inc;
            if (! loop && cur != nullptr && v.pos >= cur->length()) { v.active = false; break; }
        }
    }
}

void LiveEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const LiveControls& c, const Transport& tp, bool keepInput) noexcept
{
    const bool patternWas = ctl.pattern;
    ctl = c;
    {
        // Pertylizer's Euclidean and Turing Machine: what the PATTERN page sends, or the controls you moved UNDER THE HOOD
        const auto& pn = ctl.pins;
        const int eb = hood::liveBase ("euclid"), tb = hood::liveBase ("turing");
        pattern.set ((int) std::lround (pn.pick (eb, (float) ctl.steps)), (int) std::lround (pn.pick (eb + 1, (float) ctl.hits)),
                     (int) std::lround (pn.pick (eb + 2, (float) ctl.rotate)));
        pattern.setTuring (pn.pick (tb, ctl.mutate), pn.pick (tb + 1, juce::jlimit (0.0f, 1.0f, ctl.range / 2.0f)));
        pattern.applyPins (pn);
    }
    if (patternWas && ! ctl.pattern) for (auto& v : voices) if (v.fromPattern) v.released = true;
    if (! patternWas && ctl.pattern) for (auto& v : voices) if (! v.fromPattern) v.released = true;

    const int n = out.getNumSamples();
    const int chans = out.getNumChannels();
    if (n > scratch.getNumSamples()) return;   // cannot happen after prepare(maxBlock); never allocate here
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);
    if (keepInput && chans > 0)
    {
        std::copy (out.getReadPointer (0), out.getReadPointer (0) + n, L);
        std::copy (out.getReadPointer (juce::jmin (1, chans - 1)), out.getReadPointer (juce::jmin (1, chans - 1)) + n, R);
    }
    else { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); }

    ui.drain ([this] (int note, float vel) { if (vel > 0) noteOn (note, vel); else noteOff (note); });

    // voices, split at each MIDI event
    int at = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= at) return;
        if (ctl.pattern) patternTick (at, end - at, tp);
        renderVoices (L + at, R + at, end - at);
        if (xfade > 0) { xfade -= end - at; if (xfade <= 0) { xfade = 0; } }
        at = end;
    };
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, n, meta.samplePosition);
        renderTo (pos);
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    renderTo (n);
    if (xfade <= 0) prev = nullptr;   // the caller may let it go now (it reads previousMatter())

    // BOUNCE: the balls' strikes join the voices (and go through SPACE with them)
    bounce.process (L, R, n, { ctl.bounceLevel, ctl.gravity, ctl.springy, lastNote, &ctl.pins });

    // SPACE
    space.process (L, R, n, ctl.space, &ctl.pins);

    // where the newest voice is in the matter (for the pages)
    {
        float ph = -1.0f; int newest = -1, count = 0;
        for (auto& v : voices)
            if (v.active)
            {
                ++count;
                if (v.age > newest && cur != nullptr && cur->length() > 0)
                {
                    newest = v.age;
                    const double span = ctl.loop && cur->loopFrom > 0 ? (double) (cur->length() - cur->loopFrom) : (double) cur->length();
                    double p = v.pos;
                    if (ctl.loop && p >= cur->length()) p = std::fmod (p - cur->loopFrom, span) + cur->loopFrom;
                    ph = (float) juce::jlimit (0.0, 1.0, p / cur->length());
                }
            }
        playhead = ph; sounding = count;
    }

    // level (smoothed) and the ceiling
    const float gT = juce::Decibels::decibelsToGain (ctl.levelDb);
    const float step = (gT - gainNow) / (float) juce::jmax (1, n);
    for (int i = 0; i < n; ++i) { gainNow += step; L[i] *= gainNow; R[i] *= gainNow; }
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i])) L[i] = 0.0f;
        if (! std::isfinite (R[i])) R[i] = 0.0f;
    }
    if (ctl.ceiling) limiter.process (L, R, n);   // OFF by default (Brandon's rule: nothing of ours on the sound)
    float* chp[2] = { L, R };
    tap.push (chp, 2, n);
    float pk = 0;
    for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
    outPeak = juce::jmax (pk, outPeak.load() * 0.9f);
    for (int c2 = 0; c2 < chans; ++c2) std::copy (c2 == 0 ? L : R, (c2 == 0 ? L : R) + n, out.getWritePointer (c2));
}
}
