/*
 * Poltergeist 2 (GhostBlock) - SUB: the Bombaz voice.
 *
 * Bombaz by M.I. Hollemans - https://github.com/hollance/bombaz (commit 434d8b6eaf228338b0b2d2f56ad470c1f8929ddd),
 * "Monophonic bass synth based on Window Function synthesis". Copyright (C) 2022-2026 M.I. Hollemans, GPL-3.0-or-later.
 *
 * The synthesis itself is upstream's own code, compiled as it is: Oscillator.h (the Blackman-Harris pulse, its width,
 * the two drive waveshapers, the loudness compensation, changes only at the start of a cycle), DCBlockFilter.h and
 * DSP.h, from libs/ghostblock-sub/bombaz/src (unmodified; they sit in a namespace here so their global names stay ours).
 *
 * This file is the part of Bombaz that needs JUCE upstream and so cannot be compiled as it is: Voice.h (the glide),
 * Synth.cpp (the DC killer after the voice) and Parameters.cpp (what WIDTH / DRIVE / GLIDE / TUNE mean). It is an
 * ADAPTATION of those three, GPL-3.0-or-later like them; the lines taken from upstream are marked [Bombaz], what is
 * ours is marked [P2]:
 *   [P2] two oscillators that are fed the same pitch and width at the same moments, so they stay locked cycle for
 *        cycle: CLEAN (never driven) and DIRTY (with Bombaz's DRIVE);
 *   [P2] the pitch comes in Hz from the Six Sines voice we follow (not from a MIDI note number), the glide is
 *        Bombaz's own;
 *   [P2] WIDTH below the note where Bombaz's widest pulse (15 ms) would leave a gap: the pulse grows with the period
 *        (pulse = max(Bombaz's width, (1 - WIDTH) * period)), so WIDTH 0 is the same pure window wave on every note.
 *        Bombaz's loudness compensation is held where it is at 15 ms for those longer pulses (through setAmplitude).
 *        Above that note the voice is Bombaz bit for bit (p2-sub-tests "bombaz" compares it with upstream's Synth).
 */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace ghostblock::p2::sub
{
namespace bombaz
{
// upstream's files, unmodified (their own #includes are the three standard headers above, already in)
#include "DSP.h"
#include "DCBlockFilter.h"
#include "Oscillator.h"
} // namespace bombaz

class BombazVoice
{
  public:
    void prepare(float sampleRate_) noexcept // [Bombaz] Synth::prepareToPlay + Voice::prepareToPlay
    {
        sampleRate = sampleRate_;
        clean.prepareToPlay(sampleRate);
        dirty.prepareToPlay(sampleRate);
        dcClean.setCutoff(sampleRate, 27.5f);
        dcDirty.setCutoff(sampleRate, 27.5f);
        reset();
    }

    void reset() noexcept // [Bombaz] Voice::reset + Synth::reset
    {
        clean.reset();
        dirty.reset();
        clean.setAmplitude(1.0f);
        dirty.setAmplitude(1.0f);
        dcClean.reset();
        dcDirty.reset();
        pitch = 0.0f;
        target = 0.0f;
        glide = 1.0f;
        glideDir = 0.0f;
        amplitude = 1.0f;
        shPhase = 0.0f;
        shPeriod = 0.0f;
        cycleStart = false;
        cycHz = 0.0f;
        pushParameters();
    }

    // ---- [Bombaz] Parameters::update(): what the controls mean
    void setWidth(float percent) noexcept { widthPercent = std::clamp(percent, 0.0f, 100.0f); }
    void setDrive(float percent) noexcept { drive = std::clamp(percent, 0.0f, 100.0f) * 0.01f; }
    void setGlideMs(float ms) noexcept { glideTime = ms < 10.0f ? 0.0f : sampleRate * ms * 0.001f; } // (under 10 ms = no glide)
    void setModulation(float ratio) noexcept { modulation = ratio; } // pitch bend, tuning, octave: Parameters::pitchModulation
    void setPureExtension(bool on) noexcept { pureExtension = on; } // [P2] off = Bombaz's width law on every note (the tests)

    // [Bombaz] Voice::updateParameters, once per control step. [P2] the pulse time and the amplitude follow the note.
    void pushParameters() noexcept
    {
        float pulseTime = 0.015f - widthPercent * 0.00012f; // [Bombaz] 15 - 3 msec
        float amp = 1.0f;
        const float f = pitch * modulation; // where the note is now (it may be gliding)
        if (pureExtension && f > 0.0f)
        { // [P2]
            const float fill = (1.0f - widthPercent * 0.01f) / f;
            if (fill > pulseTime)
            {
                pulseTime = fill;
                amp = bombaz::decibelsToGain(-(pulseTime - 0.015f) * 250.0f); // (Bombaz's +3 dB at 15 ms, no more)
            }
        }
        amplitude = amp;
        for (auto *o : {&clean, &dirty})
        {
            o->setPitchModulation(modulation);
            o->setPulseTime(pulseTime);
            o->setAmplitude(amp);
        }
        clean.setDrive(0.0f);
        dirty.setDrive(drive);
        pulseTimeNow = pulseTime;
        modulationPushed = modulation;
    }

    // ---- [Bombaz] Voice::startOscillator: a note begins (legato and gliding if one is already playing)
    void start(float hz) noexcept
    {
        updatePitchTarget(hz, target != 0.0f);
        if (glide == 1.0f)
        { // not playing legato
            const bool wasActive = clean.isActive();
            pushParameters();
            clean.setPitch(pitch);
            dirty.setPitch(pitch);
            clean.startPlaying();
            dirty.startPlaying();
            if (!wasActive)
                firstCycle();
        }
    }
    // [Bombaz] Voice::restartOscillator: the note changes while it plays
    void legato(float hz) noexcept { updatePitchTarget(hz, true); }
    // [P2] the note is moved by something else (the engine's own glide, a retuning): no glide of ours on top
    void follow(float hz) noexcept
    {
        if (glide != 1.0f)
        { // our glide is still running: it now ends where the note is
            target = hz * glideDir;
            return;
        }
        target = hz;
        pitch = hz;
    }
    // [Bombaz] Voice::stopOscillator: the cycle that is playing ends, then silence
    void stop() noexcept
    {
        pitch = 0.0f;
        target = 0.0f;
        glide = 1.0f;
        glideDir = 0.0f;
    }
    // [P2] PHASE = restart: both oscillators begin a new pulse now (the caller has faded the old one out)
    void hardRestart(float hz) noexcept
    {
        clean.reset();
        dirty.reset();
        dcClean.reset();
        dcDirty.reset();
        target = 0.0f;
        glide = 1.0f;
        glideDir = 0.0f;
        updatePitchTarget(hz, false);
        pushParameters();
        clean.setPitch(pitch);
        dirty.setPitch(pitch);
        clean.startPlaying();
        dirty.startPlaying();
        firstCycle();
    }

    bool active() const noexcept { return clean.isActive(); }
    bool gliding() const noexcept { return glide != 1.0f; }
    float frequency() const noexcept { return pitch * modulation; } // Hz, as it will be at the next cycle
    float noteHz() const noexcept { return pitch; }
    // [P2] the cycle that is playing: whether the last render() began it, what its pulse is multiplied by, the part of
    // the cycle the pulse takes (1 = no dead time), how far into the cycle we are (0 .. 1)
    bool cycleBegan() const noexcept { return cycleStart; }
    float cycleLoudness() const noexcept { return cycLoud; }
    float cycleDuty() const noexcept { return cycDuty; }
    float cyclePhase() const noexcept { return shPeriod > 0.0f ? std::min(1.0f, shPhase / shPeriod) : 0.0f; }
    // what the oscillator multiplies its pulse by: [Bombaz] Oscillator::updateLoudnessCompensation (it goes by the
    // note's own pitch, before bend and tuning), times ours
    float loudness() const noexcept
    {
        const float f = std::clamp(pitch, 27.5f, 440.0f);
        return (1.0f - 0.2f * std::log2(f / 27.5f)) * bombaz::decibelsToGain((pulseTimeNow - 0.003f) * 250.0f) * amplitude;
    }

    // [Bombaz] Voice::render + Synth::render (the DC killer), one sample, for both oscillators
    // [P2] `dirtyPulse` = the DIRTY oscillator before the DC killer (0 .. loudness(), exactly 0 between two pulses)
    void render(float &cleanOut, float &dirtyOut, float &dirtyPulse, bool wantDirty) noexcept
    {
        dirtyPulse = 0.0f;
        cycleStart = false;
        if (!clean.isActive())
        {
            cleanOut = dcClean.processSample(0.0f); // (the DC killer rings out)
            dirtyOut = wantDirty ? dcDirty.processSample(0.0f) : 0.0f;
            return;
        }
        if (glide != 1.0f)
        {
            pitch *= glide;
            if (pitch * glideDir >= target)
            {
                pitch = target * glideDir;
                glide = 1.0f;
            }
        }
        clean.setPitch(pitch);
        cleanOut = dcClean.processSample(clean.nextSample());
        if (wantDirty)
        {
            dirty.setPitch(pitch);
            dirtyPulse = dirty.nextSample();
            dirtyOut = dcDirty.processSample(dirtyPulse);
        }
        else
            dirtyOut = 0.0f;
        // [P2] the oscillator's own cycle counter (Oscillator::nextSample: "Done with this cycle?"), kept here as well -
        // the same numbers, the same order - so the caller knows where a cycle begins and what its pulse is
        shPhase += 1.0f;
        if (shPhase >= shPeriod)
        {
            shPhase -= shPeriod;
            latchCycle();
        }
    }
    // [P2] the DIRTY oscillator was not run for a while (nothing asked for it): it takes the CLEAN one's place in the
    // cycle and is locked to it again
    void relockDirty() noexcept
    {
        dirty = clean;
        dirty.setDrive(drive);
        dcDirty = dcClean;
    }

  private:
    // [P2] what Oscillator::applyChanges takes over at the start of a cycle
    void latchCycle() noexcept
    {
        cycHz = pitch * modulationPushed;
        shPeriod = cycHz > 0.0f ? sampleRate / cycHz : 0.0f;
        cycLoud = loudness();
        cycDuty = std::min(1.0f, pulseTimeNow * cycHz);
        cycleStart = true;
    }
    void firstCycle() noexcept
    {
        shPhase = 0.0f;
        latchCycle();
    }
    // [Bombaz] Voice::updatePitchTarget, with the note's pitch in Hz instead of a MIDI note number
    void updatePitchTarget(float hz, bool isPlayingLegato) noexcept
    {
        target = hz;
        if (isPlayingLegato && glideTime > 0.0f && pitch > 0.0f)
        {
            const float semitones = bombaz::semitonesBetweenFrequencies(pitch, target);
            if (semitones != 0.0f)
            {
                glide = std::exp2(semitones / (12.0f * glideTime));
                glideDir = (glide > 1.0f) ? 1.0f : -1.0f;
                target *= glideDir;
                return;
            }
        }
        glide = 1.0f;
        glideDir = 0.0f;
        pitch = target;
    }

    bombaz::Oscillator clean, dirty;
    bombaz::DCBlockFilter dcClean, dcDirty;
    float sampleRate{48000.0f};
    float widthPercent{0.0f}, drive{0.0f}, glideTime{0.0f}, modulation{1.0f};
    float pitch{0.0f}, target{0.0f}, glide{1.0f}, glideDir{0.0f};
    float amplitude{1.0f}, pulseTimeNow{0.015f}, modulationPushed{1.0f};
    float shPhase{0.0f}, shPeriod{0.0f}, cycHz{0.0f}, cycLoud{1.0f}, cycDuty{1.0f};
    bool cycleStart{false};
    bool pureExtension{true};
};
} // namespace ghostblock::p2::sub
