#include "Player.h"
#include <cmath>

namespace glab
{
void Player::set (int slot, AudioPtr a)
{
    if (owners[slot] == a) return;
    if (owners[slot] != nullptr) retired.push_back (owners[slot]);
    owners[slot] = a;
    current[slot].store (a.get());
    collect();
}

void Player::collect()
{
    // seq_cst: if the audio thread re-checked `current` after publishing `inUse` and still saw the old sound, our read
    // of `inUse` below happens after that publish, so we see it and keep the sound alive
    for (size_t i = 0; i < retired.size();)
    {
        const Audio* p = retired[i].get();
        bool used = false;
        for (int s = 0; s < kSlots; ++s) used = used || p == inUse[s].load() || p == current[s].load();
        if (! used) retired.erase (retired.begin() + (long) i);
        else ++i;
    }
}

void Player::play (int slot, double fromFraction) { cmdFrom.store (fromFraction); cmdSlot.store (slot); }
void Player::stop() { cmdSlot.store (-1); }

const Audio* Player::acquire (int slot)
{
    for (int tries = 0; tries < 8; ++tries)
    {
        auto* p = current[slot].load();
        inUse[slot].store (p);
        if (current[slot].load() == p) return p;
    }
    inUse[slot].store (nullptr);
    return nullptr;
}

void Player::noteOn (int note, float velocity)
{
    juce::ignoreUnused (velocity);
    heldNote = note;
    speed = std::pow (2.0, (note - 60) / 12.0);
    pendingSlot = midiSlot.load(); pendingFrom = 0.0;
    gainTarget = 0.0f;   // (fades out whatever plays, then starts from the top)
}

void Player::noteOff (int note)
{
    if (note == heldNote) { gainTarget = 0.0f; heldNote = -1; }
}

void Player::render (juce::AudioBuffer<float>& out, int start, int num, float gain)
{
    const Audio* bufs[kSlots] = { acquire (0), acquire (1), acquire (2), acquire (3), acquire (4) };
    const int cmd = cmdSlot.exchange (-2);
    if (cmd == -1) { gainTarget = 0.0f; pendingSlot = -1; }
    else if (cmd >= 0)
    {
        pendingSlot = cmd; pendingFrom = cmdFrom.load();
        if (slotNow < 0 || gainNow <= 0.0f) gainNow = 0.0f;
        gainTarget = 0.0f;   // fade out (4 ms), then jump: no click when switching A/B
    }
    const float ramp = (float) (1.0 / (0.004 * rate));
    for (int i = 0; i < num; ++i)
    {
        if (gainNow <= 0.0f && gainTarget <= 0.0f)
        {
            if (pendingSlot >= 0)
            {
                const Audio* next = bufs[pendingSlot];
                double frac = pendingFrom;
                if (frac < 0.0)   // A/B switch: the same place in the other sound
                    frac = (slotNow >= 0 && bufs[slotNow] != nullptr && bufs[slotNow]->length() > 0) ? pos / bufs[slotNow]->length() : 0.0;
                if (heldNote < 0) speed = 1.0;
                slotNow = pendingSlot; pendingSlot = -1;
                pos = next != nullptr ? juce::jlimit (0.0, (double) next->length(), frac * next->length()) : 0.0;
                gainTarget = 1.0f;
            }
            else { slotNow = -1; break; }
        }
        const Audio* a = slotNow >= 0 ? bufs[slotNow] : nullptr;
        if (a == nullptr || a->length() < 2) { slotNow = -1; break; }
        if (gainNow < gainTarget) gainNow = juce::jmin (gainTarget, gainNow + ramp);
        else if (gainNow > gainTarget) gainNow = juce::jmax (gainTarget, gainNow - ramp);
        const int n = a->length(), chans = a->channels();
        int i0 = (int) pos;
        if (i0 >= n - 1)
        {
            if (loopSlot.load() == slotNow) { pos = 0.0; i0 = 0; }   // round again
            else { slotNow = -1; gainNow = 0.0f; gainTarget = 0.0f; break; }   // the sound's own fade-out ends it
        }
        const float fr = (float) (pos - i0);
        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            const float* s = a->buf.getReadPointer (juce::jmin (c, chans - 1));
            out.addSample (c, start + i, (s[i0] + fr * (s[i0 + 1] - s[i0])) * gainNow * gain);
        }
        pos += speed * a->sampleRate / rate;
    }
    playingNow.store (slotNow);
    const Audio* a = slotNow >= 0 ? bufs[slotNow] : nullptr;
    headFrac.store (a != nullptr && a->length() > 0 ? (float) (pos / a->length()) : 0.0f);
}
}
