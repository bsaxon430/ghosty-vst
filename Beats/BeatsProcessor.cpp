#include "BeatsProcessor.h"
#include "BeatsEditor.h"

using namespace ghosty;

namespace
{
// Sound indexes: 0 808 Kick, 1 Punch, 2 Boom, 3 Snare, 4 Clap, 5 Hat, 6 Open, 7 Tom, 8 Rim, 9 Cowbell, 10 Shaker, 11 Zap, 12 Blip, 13 Glitch, 14 Crash, 15 Swell
const int kits[3][8] = {
    { 0, 3, 4, 5, 6, 2, 9, 14 },     // Ghost 808
    { 1, 3, 4, 5, 6, 7, 8, 14 },     // Punchy
    { 1, 13, 4, 10, 6, 11, 12, 15 }, // Glitch
};
struct Pattern { const char* name; int bpm; int kit; float swing; float glitch; const char* t[8]; };
const Pattern patterns[] = {
    { "Ghost Trap",    140, 0, 0.0f,  0.0f, { "x.....x...x.....", "........x.......", "........x.......", "x.x.x.xxx.x.x.xx", "..............x.", "..........x...x.", ".....x.......x..", "x..............." } },
    { "Boom Bap",       90, 1, 0.35f, 0.0f, { "x......x..x.....", "....x.......x...", "............x...", "x.x.x.x.x.x.x.x.", "..............x.", "................", "...x......x.....", "x..............." } },
    { "Lofi Haunt",     80, 1, 0.45f, 0.0f, { "x.....x...x.....", "....x.......x...", "................", "x.xxx.x.x.xxx.x.", "................", "..........x.....", "...x.......x....", "................" } },
    { "House Party",   124, 0, 0.1f,  0.0f, { "x...x...x...x...", "................", "....x.......x...", "x.xxx.xxx.xxx.xx", "..x...x...x...x.", "................", "...x..x....x..x.", "x..............." } },
    { "Drill Ghost",   142, 0, 0.0f,  0.0f, { "x.....x...x..x..", "........x....x..", "................", "x..x..x.x..x..x.", "................", "..x.....x.......", ".....x.........x", "x..............." } },
    { "Dembow",         95, 0, 0.0f,  0.0f, { "x...x...x...x...", "...x..x....x..x.", "................", "x.x.x.x.x.x.x.x.", "................", ".......x.......x", "x...x...x...x...", "x..............." } },
    { "Techno Haunt",  130, 0, 0.0f,  0.0f, { "x...x...x...x...", "................", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", "................", "x..x..x..x..x...", "x..............." } },
    { "Jungle Breaks", 172, 1, 0.0f,  0.0f, { "x.x.......xx....", "....x..x.x..x..x", "................", "x.x.x.x.x.x.x.x.", "........x.......", "..............xx", "..x.....x.......", "x..............." } },
    { "Afro Float",    105, 1, 0.2f,  0.0f, { "x.....x...x.....", "................", "....x.......x...", "xxxxxxxxxxxxxxxx", ".......x........", "..x..x....x..x..", "x..x..x...x.x...", "................" } },
    { "Glitch Ghost",  150, 2, 0.0f,  0.6f, { "x..x....x.x.....", "....x.......x..x", "........x.......", "x.xx.xx.x.xx.xxx", ".......x........", "...x......x.x...", ".x....x.x....x..", "x..............." } },
    { "Empty",         120, 0, 0.0f,  0.0f, { "................", "................", "................", "................", "................", "................", "................", "................" } },
};
constexpr int numPatterns = (int) (sizeof (patterns) / sizeof (patterns[0]));
const char* trackNames[8] { "KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "TOM", "PERC", "CRASH" };
}

juce::AudioProcessorValueTreeState::ParameterLayout BeatsProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; }));
    };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo (standalone)", NormalisableRange<float> (60.0f, 200.0f, 1.0f), 140.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "speed", 1 }, "Speed", StringArray { "Half", "Normal", "Double" }, 1));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse", false));
    l.add (pct ("swing", "Swing", 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "filter", 1 }, "Filter", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float f, int) { return f < 0.47f ? "LP " + String (roundToInt ((1 - f / 0.47f) * 100)) + "%" : f > 0.53f ? "HP " + String (roundToInt ((f - 0.53f) / 0.47f * 100)) + "%" : String ("Off"); })));
    l.add (pct ("res", "Resonance", 0.2f));
    l.add (pct ("echo", "Echo", 0.1f));
    l.add (pct ("verb", "Reverb", 0.2f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "warp", 1 }, "Warp Pitch", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const int s = roundToInt ((v - 0.5f) * 24); return (s > 0 ? "+" : "") + String (s) + " st"; })));
    l.add (pct ("crush", "Crush", 0.0f));
    l.add (pct ("drive", "Drive", 0.1f));
    l.add (pct ("glitch", "Glitch", 0.0f));
    l.add (pct ("human", "Human", 0.0f));
    l.add (pct ("gain", "Volume", 0.8f));
    for (int t = 0; t < TRACKS; ++t)
    {
        const String tn = trackNames[t];
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { trackId ("snd", t), 1 }, tn + " Sound", DrumBank::names(), kits[0][t]));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { trackId ("pitch", t), 1 }, tn + " Pitch", -12, 12, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { trackId ("vol", t), 1 }, tn + " Volume", NormalisableRange<float> (0.0f, 1.5f), 1.0f));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { trackId ("rev", t), 1 }, tn + " Reverse", false));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { trackId ("mute", t), 1 }, tn + " Mute", false));
    }
    return l;
}

BeatsProcessor::BeatsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTYBEATS", layout())
{
    for (auto& tr : steps) for (auto& s : tr) s.store (0);
    for (auto& h : hitCount) h.store (0);
    setCurrentProgram (0);
}

bool BeatsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BeatsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    bank.build (sampleRate);
    fx.prepare (sampleRate, samplesPerBlock);
    tapeBuf.assign ((size_t) juce::jmax (512, samplesPerBlock), 1.0f);
    for (auto& v : voices) v.active = false;
}

static float P (juce::AudioProcessorValueTreeState& s, const juce::String& id) { return s.getRawParameterValue (id)->load(); }

void BeatsProcessor::startVoice (int t, int offset, float vel, bool forceReverse)
{
    if (! bank.ready()) return;
    const auto& snd = bank.get ((int) P (apvts, trackId ("snd", t)));
    const bool rev = (P (apvts, trackId ("rev", t)) > 0.5f) != (P (apvts, "reverse") > 0.5f) != forceReverse;
    const float warp = std::round ((P (apvts, "warp") - 0.5f) * 24.0f);
    if (t == 3) for (auto& v : voices) if (v.active && v.track == 4) v.active = false;   // closed hat chokes open hat
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr) slot = &voices[(size_t) rng.nextInt ((int) voices.size())];
    slot->data = rev ? &snd.rev : &snd.fwd;
    slot->pos = 0; slot->rate = std::pow (2.0, (P (apvts, trackId ("pitch", t)) + warp) / 12.0);
    slot->gain = P (apvts, trackId ("vol", t)) * snd.gain * vel * 0.9f;
    slot->panL = slot->panR = 0.7071f; slot->track = t; slot->delay = offset; slot->active = true;
    hitCount[t].fetch_add (1);
}

void BeatsProcessor::triggerStep (int s, int offset)
{
    playStep.store (s);
    const float glitch = P (apvts, "glitch"), human = P (apvts, "human");
    for (int t = 0; t < TRACKS; ++t)
    {
        if (! steps[t][s].load() || P (apvts, trackId ("mute", t)) > 0.5f) continue;
        int off = offset; float vel = steps[t][s].load() == 2 ? 1.25f : 1.0f;
        if (human > 0.0f) { off += (int) (rng.nextFloat() * human * 0.018f * (float) sr); vel *= 1.0f - rng.nextFloat() * human * 0.35f; }
        if (glitch > 0.0f && rng.nextFloat() < glitch * 0.22f)
        {
            const float r = rng.nextFloat();
            if (r < 0.5f)
            {
                const int n = 2 + rng.nextInt (3), stepLen = (int) (sr * 0.12);
                for (int k = 0; k < n; ++k) startVoice (t, off + k * stepLen / n, vel * (1.0f - (float) k * 0.15f));
                continue;
            }
            if (r < 0.7f) continue;
            if (r < 0.9f) { startVoice (t, off, vel, true); continue; }
        }
        startVoice (t, off, vel);
    }
}

void BeatsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! bank.ready()) return;

    bool hp = false; double hostBpm = 0, hostPpq = 0; bool havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hp = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; havePpq = true; }
        }
    hostPlaying.store (hp);
    const bool running = hp || internalPlay.load();
    const double bpm = hostBpm > 0 ? hostBpm : (double) P (apvts, "bpm");
    const double speedMul = std::array<double, 3> { 0.5, 1.0, 2.0 }[(size_t) juce::jlimit (0, 2, (int) P (apvts, "speed"))];
    const double inc16 = bpm / 60.0 * 4.0 * speedMul / sr;
    const float swing = P (apvts, "swing");
    const bool rev = P (apvts, "reverse") > 0.5f;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getNoteNumber() >= 36 && m.getNoteNumber() < 36 + TRACKS)
            startVoice (m.getNoteNumber() - 36, meta.samplePosition, 0.4f + m.getFloatVelocity() * 0.8f);
    }
    const int pq = previewQueue.exchange (-1);
    if (pq >= 0) startVoice (pq, 0, 1.0f);

    if ((int) tapeBuf.size() < n) tapeBuf.resize ((size_t) n);
    std::fill (tapeBuf.begin(), tapeBuf.begin() + n, 1.0f);
    auto& tape = tapeBuf;
    if (running)
    {
        const bool tapeOn = tapeHeld.load();
        if (hp && havePpq && ! tapeOn && tapeRate > 0.999)
        {
            const double host16 = hostPpq * 4.0 * speedMul;
            if (! wasPlaying || std::abs (host16 - pos16) > 0.75) { pos16 = host16; nextStep = (long long) std::ceil (pos16 - 1e-9); }
        }
        else if (! wasPlaying) { pos16 = 0; nextStep = 0; }

        for (int i = 0; i < n; ++i)
        {
            tapeRate += ((tapeOn ? 0.04 : 1.0) - tapeRate) * (tapeOn ? 5.0 / sr : 20.0 / sr);
            tape[(size_t) i] = (float) tapeRate;
            const int st = stutterLen.load();
            pos16 += inc16 * tapeRate;
            if (st > 0)
            {
                if (lastStutter != st)
                {
                    lastStutter = st; stutterPos = 0; stutterStart = 0;
                    stutterBase = ((nextStep - 1) % STEPS + STEPS) % STEPS;
                }
                stutterPos += inc16 * 2.0 * tapeRate;
                while (stutterPos >= (double) stutterStart)
                {
                    const long long local = stutterStart % st;
                    if (st >= 2) { if (local % 2 == 0) triggerStep ((int) ((stutterBase + local / 2) % STEPS), i); }
                    else triggerStep ((int) stutterBase, i);
                    ++stutterStart;
                }
                nextStep = (long long) std::ceil (pos16 - 1e-9);
            }
            else
            {
                lastStutter = 0;
                for (;;)
                {
                    const double when = (double) nextStep + ((nextStep & 1) ? swing * 0.5 : 0.0);
                    if (pos16 < when) break;
                    int s = (int) (((nextStep % STEPS) + STEPS) % STEPS);
                    if (rev) s = STEPS - 1 - s;
                    triggerStep (s, i);
                    ++nextStep;
                }
            }
        }
    }
    else { tapeRate = 1.0; playStep.store (-1); }
    wasPlaying = running;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const auto& d = *v.data; const double len = (double) d.size() - 1.0;
        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            if (v.pos >= len) { v.active = false; break; }
            const int idx = (int) v.pos; const float fr = (float) (v.pos - idx);
            const float s = (d[(size_t) idx] + (d[(size_t) idx + 1] - d[(size_t) idx]) * fr) * v.gain;
            L[i] += s * v.panL; R[i] += s * v.panR;
            v.pos += v.rate * tape[(size_t) i];
        }
    }

    buffer.applyGain (P (apvts, "gain") * 1.2f);
    FxParams p;
    p.filter = P (apvts, "filter"); p.res = P (apvts, "res"); p.echo = P (apvts, "echo"); p.verb = P (apvts, "verb");
    p.crush = P (apvts, "crush"); p.drive = P (apvts, "drive"); p.bpm = bpm * speedMul;
    if (tapeRate < 0.98) p.filter = juce::jmin (p.filter, 0.47f * (float) std::pow (tapeRate, 0.4));
    fx.process (buffer, p);
    for (int ch = 0; ch < 2; ++ch) { auto* dd = buffer.getWritePointer (ch); for (int i = 0; i < n; ++i) dd[i] = std::tanh (dd[i]); }
    const float rms = buffer.getRMSLevel (0, 0, n);
    level.store (level.load() * 0.7f + juce::jmin (1.0f, rms * 3.5f) * 0.3f);
}

void BeatsProcessor::randomise()
{
    static const float dens[8][16] = {
        { .9f,0,.1f,.15f,.3f,0,.4f,.1f,.6f,.05f,.45f,.1f,.2f,.1f,.3f,.15f },
        { 0,0,0,0,.95f,0,0,.1f,0,.15f,0,.05f,.95f,.1f,.1f,.25f },
        { 0,0,0,0,.4f,0,0,0,0,0,0,0,.5f,0,0,.1f },
        { .9f,.45f,.9f,.45f,.9f,.45f,.9f,.45f,.9f,.45f,.9f,.45f,.9f,.45f,.9f,.45f },
        { .08f,.08f,.35f,.08f,.08f,.08f,.35f,.08f,.08f,.08f,.35f,.08f,.08f,.08f,.35f,.08f },
        { .1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f },
        { .18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f,.18f },
        { .5f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } };
    for (int t = 0; t < TRACKS; ++t) for (int s = 0; s < STEPS; ++s) steps[t][s].store (rng.nextFloat() < dens[t][s] ? 1 : 0);
    patternVersion++;
}

int BeatsProcessor::getNumPrograms() { return numPatterns; }
const juce::String BeatsProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, numPatterns) ? patterns[i].name : ""; }

void BeatsProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPatterns)) return;
    currentProgram = index;
    const auto& p = patterns[index];
    auto set = [this] (const juce::String& id, float v) { if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    set ("bpm", (float) p.bpm); set ("swing", p.swing); set ("glitch", p.glitch);
    for (int t = 0; t < TRACKS; ++t)
    {
        set (trackId ("snd", t), (float) kits[p.kit][t]); set (trackId ("pitch", t), 0); set (trackId ("vol", t), 1.0f);
        set (trackId ("rev", t), 0); set (trackId ("mute", t), 0);
        for (int s = 0; s < STEPS; ++s) steps[t][s].store (p.t[t][s] == 'x' ? 1 : 0);
    }
    patternVersion++;
}

void BeatsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::String pat;
    for (int t = 0; t < TRACKS; ++t) { for (int s = 0; s < STEPS; ++s) pat << juce::String (steps[t][s].load()); pat << "|"; }
    state.setProperty ("steps", pat, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void BeatsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        apvts.replaceState (state);
        auto rows = juce::StringArray::fromTokens (state.getProperty ("steps").toString(), "|", "");
        for (int t = 0; t < TRACKS && t < rows.size(); ++t)
            for (int s = 0; s < STEPS && s < rows[t].length(); ++s) steps[t][s].store ((uint8_t) (rows[t][s] - '0'));
        currentProgram = (int) state.getProperty ("program", 0);
        patternVersion++;
    }
}

juce::AudioProcessorEditor* BeatsProcessor::createEditor() { return new BeatsEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeatsProcessor(); }
