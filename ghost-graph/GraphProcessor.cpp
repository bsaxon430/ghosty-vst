#include "GraphProcessor.h"
#include "GraphEditor.h"

namespace
{
    constexpr int kChunk = 32;
    constexpr double kBaseHz = 220.0;     // y = 0 is A3, measured from the videos' sound
    constexpr float kVoiceLevel = 0.31f;  // one square voice comes out at the videos' level
    constexpr double kRoundLevel = 1.7;   // ROUND is louder in its videos (fundamental ~0.7 of full scale)   // the equations are read every 32 samples; pitch glides smoothly in between

    // single-cycle tables, one per harmonic limit (1, 2, 4 ... 512 harmonics), so high notes never alias
    struct WaveTables
    {
        static constexpr int kSize = 2048, kLevels = 10;
        std::vector<float> t[2][kLevels];   // [0] SOFT SAW, [1] ROUND
        WaveTables()
        {
            // ROUND: measured from the white and second amber videos (odd overtones falling fast, a little even colour)
            const double round[] = { 1, .115, .38, .07, .117, .043, .039, .013, .011, .009, .004, .004 };
            for (int lv = 0; lv < kLevels; ++lv)
            {
                const int limit = 1 << lv;
                for (int w = 0; w < 2; ++w)
                {
                    auto& tb = t[w][lv];
                    tb.assign (kSize + 1, 0.f);
                    double peak = 0;
                    std::vector<double> acc (kSize, 0.0);
                    for (int h = 1; h <= limit && h <= 512; ++h)
                    {
                        double a, ph = 0;
                        if (w == 0) a = 1.0 / std::pow ((double) h, 1.5);   // SOFT SAW: the first video's darker saw
                        else { if (h > 12) break; a = round[h - 1]; ph = (h % 2 == 0) ? juce::MathConstants<double>::halfPi : 0.0; }
                        for (int i = 0; i < kSize; ++i) acc[(size_t) i] += a * std::sin (juce::MathConstants<double>::twoPi * h * i / kSize + ph);
                    }
                    for (auto v : acc) peak = std::max (peak, std::abs (v));
                    for (int i = 0; i < kSize; ++i) tb[(size_t) i] = (float) (acc[(size_t) i] / (peak > 0 ? peak : 1));
                    tb[kSize] = tb[0];
                }
            }
        }
        float read (int w, double phase, double inc) const
        {
            const double maxH = inc > 0 ? 0.45 / inc : 512.0;
            int lv = 0;
            while (lv + 1 < kLevels && (1 << (lv + 1)) <= maxH) ++lv;
            const auto& tb = t[w][lv];
            const double pos = phase * kSize;
            const int i = (int) pos;
            const double fr = pos - i;
            return (float) (tb[(size_t) i] + (tb[(size_t) i + 1] - tb[(size_t) i]) * fr);
        }
    };
    const WaveTables& tables() { static const WaveTables w; return w; }

    double polyBlep (double t, double dt)
    {
        if (dt <= 0) return 0;
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0;
    }
}

GraphProcessor::GraphProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTGRAPH", makeLayout())
{
    pSync   = apvts.getRawParameterValue ("sync");
    pPlay   = apvts.getRawParameterValue ("play");
    pTempo  = apvts.getRawParameterValue ("tempo");
    pRate   = apvts.getRawParameterValue ("rate");
    pChain  = apvts.getRawParameterValue ("chain");
    pScene  = apvts.getRawParameterValue ("scene");
    pWave   = apvts.getRawParameterValue ("wave");
    pTune   = apvts.getRawParameterValue ("tune");
    pGlide  = apvts.getRawParameterValue ("glide");
    pTone   = apvts.getRawParameterValue ("tone");
    pSpace  = apvts.getRawParameterValue ("space");
    pWidth  = apvts.getRawParameterValue ("width");
    pVolume = apvts.getRawParameterValue ("volume");
    pKeys   = apvts.getRawParameterValue ("keys");
    pKit    = apvts.getRawParameterValue ("kit");
    tables();   // built once, never on the audio thread
    resetScenes();
}

juce::AudioProcessorValueTreeState::ParameterLayout GraphProcessor::makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo", NormalisableRange<float> (40.f, 220.f, 1.f), 75.f,
                                                   AudioParameterFloatAttributes().withLabel ("BPM")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate", StringArray { "1/2x", "1x", "2x" }, 1));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "chain", 1 }, "Chain", true));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "scene", 1 }, "Scene", 1, kMaxScenes, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "wave", 1 }, "Wave", StringArray { "SINE", "TRIANGLE", "SAW", "SQUARE", "SOFT SAW", "ROUND" }, 4));   // Track 1 (the first video): SOFT SAW on D3
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tune", 1 }, "Tune", NormalisableRange<float> (-24.f, 24.f, 1.f), -7.f,
                                                   AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", NormalisableRange<float> (0.f, 300.f, 1.f, 0.5f), 0.f,
                                                   AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone", NormalisableRange<float> (200.f, 20000.f, 1.f, 0.3f), 20000.f,
                                                   AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Space", NormalisableRange<float> (0.f, 1.f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.f, 1.f), 0.2f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (-36.f, 6.f, 0.1f), 0.f,
                                                   AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "keys", 1 }, "Keys", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "kit", 1 }, "Kit", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "look", 1 }, "Look", StringArray { "PAPER", "CHROMA", "INK", "WINE", "TI 84", "SILVER" }, 4));   // opens on the TI 84; LOOK then cycles SILVER, PAPER, CHROMA, INK, WINE
    return l;
}

bool GraphProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// ------------------------------------------------------------------------------------------------ scenes
std::string GraphProcessor::sceneName (int i)
{
    static const char* n[] = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve" };
    return n[juce::jlimit (0, kMaxScenes - 1, i)];
}

void GraphProcessor::rebuildAll()
{
    std::array<std::unique_ptr<gg::Program>, kMaxScenes> fresh;
    std::array<double, kMaxScenes> sp {}, pt {};
    for (size_t i = 0; i < scenes.size() && i < (size_t) kMaxScenes; ++i) sp[i] = juce::jlimit (0.1, 10.0, scenes[i].speed);
    for (size_t i = 0; i < (size_t) kMaxScenes; ++i) pt[i] = i < scenes.size() ? juce::jlimit (0.05, 4.0, scenes[i].pitch) : 1.0;
    for (size_t i = 0; i < scenes.size() && i < (size_t) kMaxScenes; ++i)
        fresh[i] = gg::Program::compile (scenes[i].text);
    {
        const juce::SpinLock::ScopedLockType sl (progLock);
        std::swap (fresh, programs);
        speeds = sp;
        pitches = pt;
        numPrograms = (int) std::min (scenes.size(), (size_t) kMaxScenes);
    }
    ++scenesVersion;
}   // the old programs are freed here, outside the lock

void GraphProcessor::setSceneText (int i, const std::string& text)
{
    if (i < 0 || i >= (int) scenes.size()) return;
    scenes[(size_t) i].text = text;
    auto p = gg::Program::compile (text);
    const juce::SpinLock::ScopedLockType sl (progLock);
    std::swap (p, programs[(size_t) i]);
}

int GraphProcessor::addScene()
{
    if ((int) scenes.size() >= kMaxScenes) return -1;
    scenes.push_back ({ sceneName ((int) scenes.size()),
                        "y = [kick, -1, 7/12 - w/12]\n"
                        "h = frac(x/2)\n"
                        "w = frac(x/8)\n"
                        "kick = {h < 0.3: 1.5e^(-14h) - 2.1}\n" });
    rebuildAll();
    return (int) scenes.size() - 1;
}

void GraphProcessor::removeScene (int i)
{
    if (scenes.size() <= 1 || i < 0 || i >= (int) scenes.size()) return;
    scenes.erase (scenes.begin() + i);
    for (size_t k = 0; k < scenes.size(); ++k) scenes[k].name = sceneName ((int) k);
    rebuildAll();
}

void GraphProcessor::loadTrack (int i)
{
    const auto tracks = gg::factoryTracks();
    if (i < 0 || i >= (int) tracks.size()) return;
    std::vector<gg::Scene> sc;
    for (size_t k = 0; k < tracks[(size_t) i].scenes.size() && k < (size_t) kMaxScenes; ++k)
        sc.push_back ({ sceneName ((int) k), tracks[(size_t) i].scenes[k],
                        k < tracks[(size_t) i].speeds.size() ? tracks[(size_t) i].speeds[k] : 1.0,
                        k < tracks[(size_t) i].pitches.size() ? tracks[(size_t) i].pitches[k] : 1.0 });
    scenes = sc;
    rebuildAll();
    auto set = [this] (const char* id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("tempo", (float) tracks[(size_t) i].bpm);
    set ("wave", (float) tracks[(size_t) i].wave);
    set ("tune", (float) tracks[(size_t) i].tune);
    set ("tone", (float) tracks[(size_t) i].tone);
    if (tracks[(size_t) i].look >= 0) set ("look", (float) tracks[(size_t) i].look);
    set ("chain", 1.0f);
    set ("scene", 1.0f);
    displayScene = 0;
    restartClock = true;
}

void GraphProcessor::generateTrack (uint32_t seed, int style)
{
    beforeGenerate = scenes;
    tempoBeforeGenerate = pTempo->load();
    gen = std::make_unique<gg::Generator> (seed, style);
    std::vector<gg::Scene> sc;
    const auto t = gen->track();
    for (size_t k = 0; k < t.size(); ++k) sc.push_back ({ sceneName ((int) k), t[k] });
    scenes = sc;
    rebuildAll();
    auto set = [this] (const char* id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("tempo", (float) gen->bpm());
    set ("wave", gen->wave());   // the sound of the video whose style it follows
    set ("tune", gen->tune());
    set ("tone", gen->tone());
    set ("chain", 1.0f);
    set ("scene", 1.0f);
    displayScene = 0;
    restartClock = true;
}

void GraphProcessor::generateSceneHere (int i)
{
    if (i < 0 || i >= (int) scenes.size()) return;
    beforeGenerate = scenes;
    tempoBeforeGenerate = pTempo->load();
    if (gen == nullptr) gen = std::make_unique<gg::Generator> ((uint32_t) juce::Random::getSystemRandom().nextInt());
    setSceneText (i, gen->sceneAgain (i));
    ++scenesVersion;
}

void GraphProcessor::undoGenerate()
{
    if (beforeGenerate.empty()) return;
    scenes = beforeGenerate;
    beforeGenerate.clear();
    rebuildAll();
    if (auto* p = apvts.getParameter ("tempo")) p->setValueNotifyingHost (p->convertTo0to1 (tempoBeforeGenerate));
}

void GraphProcessor::resetScenes()
{
    scenes = gg::factoryScenes();
    rebuildAll();
}

// ------------------------------------------------------------------------------------------------ audio
void GraphProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    lowpass.prepare (spec);
    lowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpass.setResonance (0.70710678f);
    lowpass.reset();
    reverb.setSampleRate (sr);
    reverb.reset();
    volSmooth.reset (sr, 0.05);
    toneSmooth.reset (sr, 0.05);
    volSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
    toneSmooth.setCurrentAndTargetValue (pTone->load());
    for (auto& v : voices) v = Voice();
}

void GraphProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        bool have = false;
        for (int i = 0; i < numHeld; ++i) if (held[(size_t) i] == n) have = true;
        if (! have && numHeld < (int) held.size()) held[(size_t) numHeld++] = n;
        lastNote = n;
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i] == n)
            {
                for (int k = i; k + 1 < numHeld; ++k) held[(size_t) k] = held[(size_t) k + 1];
                --numHeld;
                break;
            }
        if (numHeld > 0) lastNote = held[(size_t) numHeld - 1];
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        numHeld = 0;
}

void GraphProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    if (restartClock.exchange (false)) freeBeat = 0.0;
    const bool hadKeys = numHeld > 0;
    for (const auto meta : midi) handleMidi (meta.getMessage());
    midi.clear();

    const bool keys = pKeys->load() > 0.5f;
    if (keys && ! hadKeys && numHeld > 0) keyBeat = 0.0;   // a new phrase starts at x = -8

    // ---- the clock
    bool hostPlaying = false, hostHasPos = false;
    double ppq = 0.0, hostBpm = 0.0;
    if (fakeHost.use) { hostPlaying = fakeHost.playing; hostHasPos = true; ppq = fakeHost.ppq; hostBpm = fakeHost.bpm; }
    else if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hostHasPos = true; }
        }

    const bool syncOn = pSync->load() > 0.5f && ! isStandalone();
    displayHostSync = syncOn;
    double bpm = pTempo->load();
    double beat0 = 0.0;
    bool running = false, useKeyClock = false;
    if (syncOn)
    {
        if (hostBpm > 0) bpm = hostBpm;
        running = hostPlaying && hostHasPos;
        beat0 = ppq;
    }
    else
    {
        running = pPlay->load() > 0.5f;
        beat0 = freeBeat;
    }
    if (keys && ! running && numHeld > 0) { running = true; useKeyClock = true; beat0 = keyBeat; }

    const double bps = bpm / 60.0 / sr;
    if (! syncOn && pPlay->load() > 0.5f) freeBeat += n * bps;
    if (useKeyClock) keyBeat += n * bps;
    if (freeBeat > 1.0e6) freeBeat = std::fmod (freeBeat, (double) kPage * 64);

    const bool gateAll = running && (! keys || numHeld > 0);
    const double rate = pRate->load() < 0.5f ? 0.5 : (pRate->load() < 1.5f ? 1.0 : 2.0);
    const bool chain = pChain->load() > 0.5f;
    const int wave = (int) pWave->load();
    const bool kit = pKit->load() > 0.5f;
    const double transpose = pTune->load() / 12.0 + (keys ? (lastNote - 57) / 12.0 : 0.0);
    const double glideMs = pGlide->load();
    const double fadeStep = 1.0 / (0.004 * sr);

    const juce::SpinLock::ScopedTryLockType tl (progLock);
    if (! tl.isLocked()) return;   // a scene is being swapped this instant: one silent block

    const int count = juce::jmax (1, numPrograms.load());
    int sceneIdx = juce::jlimit (0, count - 1, chain ? displayScene.load() : (int) pScene->load() - 1);
    double xNow = displayX.load();

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const float width = pWidth->load();

    for (int s0 = 0; s0 < n; s0 += kChunk)
    {
        const int len = juce::jmin (kChunk, n - s0);
        int nv = 0;
        gg::Program* prog = nullptr;
        if (running)
        {
            const double xb = (beat0 + (s0 + len) * bps) * rate;
            if (chain)
            {
                // each scene lasts 16 / speed beats: find where we are in the whole chain
                double total = 0;
                for (int k = 0; k < count; ++k) total += kPage / speeds[(size_t) k];
                double pos = std::fmod (xb, total);
                if (pos < 0) pos += total;
                int k = 0;
                while (k < count - 1 && pos >= kPage / speeds[(size_t) k]) { pos -= kPage / speeds[(size_t) k]; ++k; }
                sceneIdx = k;
                xNow = pos * speeds[(size_t) k] - 8.0;
            }
            else
            {
                const double xs = xb * speeds[(size_t) sceneIdx];
                xNow = xs - std::floor (xs / kPage) * kPage - 8.0;
            }
        }
        prog = programs[(size_t) sceneIdx].get();
        if (prog != nullptr && prog->ok())
        {
            nv = prog->numVoices();
            prog->setX (xNow);
        }
        if (sceneIdx != lastSceneIdx) lastSceneIdx = sceneIdx;

        const float voiceGain = kVoiceLevel;   // like the videos: every voice at the same level, no matter how many
        for (int v = 0; v < gg::Program::kMaxVoices; ++v)
        {
            Voice& vc = voices[(size_t) v];
            double y = (gateAll && v < nv) ? prog->voice (v) * pitches[(size_t) sceneIdx] : std::numeric_limits<double>::quiet_NaN();
            bool want = std::isfinite (y);
            if (want)
            {
                const double f = kBaseHz * std::exp2 (y + transpose);
                if (f < 8.0 || f > 0.45 * sr) want = false;
            }
            if (! want && ! vc.on && vc.gain <= 0.0) continue;

            vc.yFrom = vc.y;
            if (want)
            {
                if (! vc.on && vc.gain <= 0.0) { vc.yFrom = y; vc.phase = 0.0; }   // a fresh note starts on its pitch
                double target = y;
                if (glideMs > 0.5 && vc.on)
                    target = vc.yFrom + (y - vc.yFrom) * (1.0 - std::exp (-len / (glideMs * 0.001 * sr)));
                vc.yTo = target;
            }
            else vc.yTo = vc.yFrom;
            vc.on = want;

            const double incA = kBaseHz * std::exp2 (vc.yFrom + transpose) / sr;
            const double incB = kBaseHz * std::exp2 (vc.yTo + transpose) / sr;
            const float pan = nv > 1 ? (float) ((double) v / (nv - 1) - 0.5) * width : 0.0f;
            const float gl = voiceGain * std::cos ((pan + 0.5f) * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
            const float gr = voiceGain * std::sin ((pan + 0.5f) * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
            const double gTarget = want ? 1.0 : 0.0;
            const int kind = (kit && prog != nullptr && v < nv) ? prog->voiceKind (v) : 0;
            const auto& wt = tables();

            for (int i = 0; i < len; ++i)
            {
                const double inc = incA + (incB - incA) * (i + 1) / (double) len;
                const double p = vc.phase;
                double s;
                switch (wave)
                {
                    case 1:  s = 1.0 - 4.0 * std::abs (p - 0.5); break;
                    case 2:  s = 2.0 * p - 1.0 - polyBlep (p, inc); break;
                    case 3:  { double q = p + 0.5; if (q >= 1.0) q -= 1.0; s = (p < 0.5 ? 1.0 : -1.0) + polyBlep (p, inc) - polyBlep (q, inc); break; }
                    case 4:  s = wt.read (0, p, inc); break;
                    case 5:  s = kRoundLevel * wt.read (1, p, inc); break;
                    default: s = std::sin (juce::MathConstants<double>::twoPi * p); break;
                }
                if (kind == 1)
                {
                    // hi-hat: six square oscillators at fixed metallic ratios (the 808 recipe), high-passed; the line only gates it
                    static const double hz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                    double m = 0;
                    for (int o = 0; o < 6; ++o)
                    {
                        vc.hatPh[o] += hz[o] * 1.7 / sr;
                        if (vc.hatPh[o] >= 1.0) vc.hatPh[o] -= 1.0;
                        m += vc.hatPh[o] < 0.5 ? 1.0 : -1.0;
                    }
                    const double a = std::exp (-juce::MathConstants<double>::twoPi * 2600.0 / sr);
                    vc.hp1 = a * (vc.hp1 + m - vc.hpx1); vc.hpx1 = m;
                    vc.hp2 = a * (vc.hp2 + vc.hp1 - vc.hpx2); vc.hpx2 = vc.hp1;
                    const double b = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 8000.0 / sr);   // nothing above ~8 kHz, like the videos
                    vc.lp1 += b * (vc.hp2 - vc.lp1); vc.lp2 += b * (vc.lp1 - vc.lp2);
                    s = vc.lp2 * 0.62;
                }
                else if (kind == 2)
                {
                    // snare / clap: noise, with a little of the line's own tone
                    vc.noise ^= vc.noise << 13; vc.noise ^= vc.noise >> 17; vc.noise ^= vc.noise << 5;
                    const double nz = (double) (int32_t) vc.noise / 2147483648.0;
                    const double a = std::exp (-juce::MathConstants<double>::twoPi * 450.0 / sr);
                    vc.hp1 = a * (vc.hp1 + nz - vc.hpx1); vc.hpx1 = nz;
                    const double b = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 6500.0 / sr);
                    vc.lp1 += b * (vc.hp1 - vc.lp1);
                    s = 0.75 * vc.lp1 + 0.3 * s;
                }
                vc.phase += inc;
                if (vc.phase >= 1.0) vc.phase -= std::floor (vc.phase);
                if (vc.gain < gTarget) vc.gain = juce::jmin (gTarget, vc.gain + fadeStep);
                else if (vc.gain > gTarget) vc.gain = juce::jmax (gTarget, vc.gain - fadeStep);
                const float out = (float) (s * vc.gain);
                L[s0 + i] += out * gl;
                if (R != nullptr) R[s0 + i] += out * gr;
            }
            vc.y = vc.yTo;
        }
    }

    // ---- tone, space, volume
    toneSmooth.setTargetValue (pTone->load());
    const float cutoff = toneSmooth.skip (n);
    if (cutoff < 19000.0f)
    {
        lowpass.setCutoffFrequency (juce::jmin (cutoff, (float) (0.45 * sr)));
        juce::dsp::AudioBlock<float> block (buffer);
        lowpass.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    const float space = pSpace->load();
    if (space > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.4f * space;
        rp.damping = 0.45f;
        rp.wetLevel = 0.55f * space;
        rp.dryLevel = 1.0f - 0.35f * space;
        rp.width = 1.0f;
        reverb.setParameters (rp);
        if (R != nullptr) reverb.processStereo (L, R, n);
        else reverb.processMono (L, n);
    }
    volSmooth.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = volSmooth.getNextValue();
        L[i] = std::tanh (L[i] * g);
        if (R != nullptr) R[i] = std::tanh (R[i] * g);
    }

    displayX = xNow;
    displayScene = sceneIdx;
    displayRunning = gateAll;
}

// ------------------------------------------------------------------------------------------------ state
void GraphProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto sc = juce::ValueTree ("SCENES");
    for (auto& s : scenes)
    {
        juce::ValueTree t ("SCENE");
        t.setProperty ("name", juce::String (s.name), nullptr);
        t.setProperty ("text", juce::String (s.text), nullptr);
        t.setProperty ("speed", s.speed, nullptr);
        if (s.pitch != 1.0) t.setProperty ("pitch", s.pitch, nullptr);
        sc.appendChild (t, nullptr);
    }
    state.removeChild (state.getChildWithName ("SCENES"), nullptr);
    state.appendChild (sc, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void GraphProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto sc = state.getChildWithName ("SCENES");
    std::vector<gg::Scene> loaded;
    for (int i = 0; i < sc.getNumChildren() && i < kMaxScenes; ++i)
    {
        auto t = sc.getChild (i);
        loaded.push_back ({ sceneName (i), t.getProperty ("text").toString().toStdString(), (double) t.getProperty ("speed", 1.0), (double) t.getProperty ("pitch", 1.0) });
    }
    state.removeChild (sc, nullptr);
    apvts.replaceState (state);
    if (! loaded.empty()) { scenes = loaded; rebuildAll(); }
}

juce::AudioProcessorEditor* GraphProcessor::createEditor() { return new GraphEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GraphProcessor(); }
