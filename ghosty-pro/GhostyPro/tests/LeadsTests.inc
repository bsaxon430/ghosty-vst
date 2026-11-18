// Ghosty Pro LEADS pack tests (ported from the leads helper's harness /root/work/proleads/harness/LeadsTests.cpp), included by
// TrapTestsNew.inc:  ProTests leads <check|levels <trims.inc>|report [csv] [from] [count]|size|state|threads|demo <dir>|shots <dir>|zip|clicks|edge|alias|tail|list>
#include <thread>
#include <functional>
namespace
{
const char* leadCats[] { "Leads Hypnotic", "Leads Euphoric", "Leads Aggressive" };
bool isLead (const gpro::FactoryPreset& f) { return f.cat.startsWith ("Leads "); }
std::vector<int> leadPrograms()
{
    std::vector<int> v; const auto& b = gpro::factoryBank();
    for (int i = 0; i < (int) b.size(); ++i) if (isLead (b[(size_t) i])) v.push_back (i);
    return v;
}

// render with a per-block hook (host automation at block rate)
juce::AudioBuffer<float> renderAuto (ProProcessor& p, const std::vector<Ev>& evs, double seconds, const std::function<void (double)>& hook, int bs = BS, bool reseed = true)
{
    if (! reseed) seedSystemRandom (4242);
    const int total = (int) (seconds * SR);
    juce::AudioBuffer<float> out (2, total); out.clear();
    juce::AudioBuffer<float> blk (2, bs);
    int blockIdx = 0;
    for (int k = 0; k < (int) std::ceil (0.1 * SR / bs); ++k) { juce::MidiBuffer mb; blk.setSize (2, bs, false, false, true); if (reseed) seedSystemRandom (0x5eed + blockIdx++); p.processBlock (blk, mb); }
    for (int pos = 0; pos < total; pos += bs)
    {
        const int n = juce::jmin (bs, total - pos);
        if (hook) hook (pos / SR);
        juce::MidiBuffer mb;
        for (auto& e : evs) { const int s = (int) (e.t * SR); if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
        blk.setSize (2, n, false, false, true);
        if (reseed) seedSystemRandom (0x5eed + blockIdx++);
        p.processBlock (blk, mb);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    return out;
}

// zipper: the same knob move rendered with 512-sample host blocks and with 32-sample blocks (the knob value then moves 16x
// more finely: the reference); the 512 render's extra error above 3 kHz, dB re the signal. Run once with the knob parked too:
// what block size alone changes (control-rate details, FX) is the baseline, the moving knob's zipper is the excess over it.
double blockError (int program, int macro, const std::function<float (double)>& curve, const std::vector<Ev>& evs, double t0, double t1)
{
    auto run = [&] (int bs)
    {
        auto p = makeProc (program); p->setParam ("arp", 0.0f);
        return renderAuto (*p, evs, t1 + 0.2, [&] (double t) { p->setParam ("mac" + juce::String (macro + 1), curve (t)); }, bs, false);
    };
    auto a = run (512), b = run (32);
    juce::dsp::IIR::Filter<float> hp[2];
    for (auto& h : hp) h.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, 3000.0, 0.7);
    double ed = 0, es = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const float d = hp[ch].processSample (a.getSample (ch, i) - b.getSample (ch, i));
            if (i >= (int) (t0 * SR) && i < (int) (t1 * SR)) { ed += (double) d * d; es += (double) a.getSample (ch, i) * a.getSample (ch, i); }
        }
    return 10.0 * std::log10 ((ed + 1e-20) / (es + 1e-20));
}

// a lead line in the lead register (A minor, 128 BPM): 8th / quarter notes, each note starts 128 samples after a block edge
std::vector<Ev> leadLine (double secs, double bpm = 128.0)
{
    const double beat = 60.0 / bpm; std::vector<Ev> e;
    const int mel[] { 69, 72, 76, 74, 72, 71, 69, 76, 77, 76, 79, 76, 74, 71, 67, 69 };
    double t = 0.05; int i = 0;
    while (t + 0.5 * beat < secs - 1.6)
    {
        const double len = (i % 4 == 3) ? beat : 0.5 * beat;
        const double t0 = (std::floor (t * SR / BS) * BS + 128) / SR;
        e = e + note (mel[i % 16], t0, len * 0.9, 0.85f);
        t += len; ++i;
    }
    return e;
}

// ---- measurements
struct Feat { double lufs = -100, centroid = 0, width = -100, tail = -100, body = -100, peakDb = -100, corr = 1; };
Feat features (const juce::AudioBuffer<float>& b, double bodyEnd, double tailStart, double tailEnd)
{
    Feat f;
    const int be = juce::jmin (b.getNumSamples(), (int) (bodyEnd * SR));
    f.lufs = lufs (b, 0, be); f.peakDb = juce::Decibels::gainToDecibels (peakOf (b), -120.0f);
    f.body = rmsDb (b, 0, be); f.tail = rmsDb (b, (int) (tailStart * SR), juce::jmin (b.getNumSamples(), (int) (tailEnd * SR)));
    double m = 0, s = 0, lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < be; ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); m += (l + r) * (l + r); s += (l - r) * (l - r); lr += l * r; ll += l * l; rr += r * r; }
    f.width = 10.0 * std::log10 ((s + 1e-12) / (m + 1e-12));
    f.corr = lr / std::sqrt (ll * rr + 1e-20);                 // L/R correlation: 1 = mono
    constexpr int ORDER = 12, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2);
    double num = 0, den = 0;
    for (int st = 0; st + N <= be; st += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (b.getSample (0, st + i) + b.getSample (1, st + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) { const double pw = (double) buf[(size_t) k] * buf[(size_t) k], hz = k * SR / N; num += pw * hz; den += pw; }
    }
    f.centroid = den > 0 ? num / den : 0;
    return f;
}
// movement: how much the brightness moves over time (std of the 46 ms centroid in octaves) over the body
double movement (const juce::AudioBuffer<float>& b, double bodyEnd)
{
    constexpr int ORDER = 11, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2);
    std::vector<double> c; const int be = juce::jmin (b.getNumSamples(), (int) (bodyEnd * SR));
    for (int st = 0; st + N <= be; st += N)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (b.getSample (0, st + i) + b.getSample (1, st + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double num = 0, den = 0; for (int k = 1; k < N / 2; ++k) { const double pw = (double) buf[(size_t) k] * buf[(size_t) k]; num += pw * k * SR / N; den += pw; }
        if (den > 1e-9) c.push_back (std::log2 (num / den));
    }
    if (c.size() < 4) return 0;
    // detrended: difference between consecutive frames (pure note changes are the same in both renders)
    double mean = 0; for (auto v : c) mean += v; mean /= (double) c.size();
    double var = 0; for (auto v : c) var += (v - mean) * (v - mean);
    return std::sqrt (var / (double) c.size());
}
// zipper: roughness (2nd difference energy) right at the 512-sample block edges vs half-way between them, outside +-10 ms of any note event
double edgeRoughness (const juce::AudioBuffer<float>& b, const std::vector<Ev>& evs, double t0, double t1)
{
    std::vector<char> mask ((size_t) b.getNumSamples(), 1);
    for (auto& e : evs) { const int s = (int) (e.t * SR); for (int i = juce::jmax (0, s - 480); i < juce::jmin (b.getNumSamples(), s + 480); ++i) mask[(size_t) i] = 0; }
    double edge = 0, mid = 0; int ne = 0, nm = 0;
    const int s0 = juce::jmax (4, (int) (t0 * SR)), s1 = juce::jmin (b.getNumSamples() - 4, (int) (t1 * SR));
    for (int blk = (s0 / BS + 1) * BS; blk + BS / 2 + 3 < s1; blk += BS)
        for (int o = -1; o <= 2; ++o)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto d2 = [&] (int i) { const double v = b.getSample (ch, i) - 2.0 * b.getSample (ch, i - 1) + b.getSample (ch, i - 2); return v * v; };
                if (mask[(size_t) (blk + o)]) { edge += d2 (blk + o); ++ne; }
                if (mask[(size_t) (blk + BS / 2 + o)]) { mid += d2 (blk + BS / 2 + o); ++nm; }
            }
    if (ne == 0 || nm == 0 || mid < 1e-14) return 1.0;
    return (edge / ne) / (mid / nm);
}
// zipper LEVEL: the click energy the block edges carry, in dB re the whole signal. The signal is high-passed at 5 kHz (clicks are
// broadband, the music mostly isn't up there), the energy in the 5 samples around every 512-sample edge is compared with the 5
// samples half-way between edges (same music, no edge), the excess summed over the window and divided by the total energy.
double edgeClickDb (const juce::AudioBuffer<float>& b, const std::vector<Ev>& evs, double t0, double t1)
{
    std::vector<char> mask ((size_t) b.getNumSamples(), 1);
    for (auto& e : evs) { const int s = (int) (e.t * SR); for (int i = juce::jmax (0, s - 480); i < juce::jmin (b.getNumSamples(), s + 480); ++i) mask[(size_t) i] = 0; }
    double edge = 0, mid = 0, tot = 0; int ne = 0, nm = 0;
    const int s0 = juce::jmax (4, (int) (t0 * SR)), s1 = juce::jmin (b.getNumSamples() - 4, (int) (t1 * SR));
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::dsp::IIR::Filter<float> hp; hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, 5000.0, 0.7);
        std::vector<float> h ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) h[(size_t) i] = hp.processSample (b.getSample (ch, i));
        for (int i = s0; i < s1; ++i) tot += (double) b.getSample (ch, i) * b.getSample (ch, i);
        for (int blk = (s0 / BS + 1) * BS; blk + BS / 2 + 3 < s1; blk += BS)
            for (int o = -2; o <= 2; ++o)
            {
                if (mask[(size_t) (blk + o)]) { edge += (double) h[(size_t) (blk + o)] * h[(size_t) (blk + o)]; ++ne; }
                if (mask[(size_t) (blk + BS / 2 + o)]) { mid += (double) h[(size_t) (blk + BS / 2 + o)] * h[(size_t) (blk + BS / 2 + o)]; ++nm; }
            }
    }
    if (ne == 0 || nm == 0 || tot <= 0) return -200;
    const double excess = std::max (0.0, edge / ne - mid / nm) * ne;     // the extra energy the edges carry
    return 10.0 * std::log10 (std::max (1e-30, excess / tot));
}

// aliasing: one high note, detune / unison spread / chorus / noise / LFOs / FX rack off; energy away from the harmonic grid (f0/4)
double aliasingDb (int program, int midi, std::vector<std::pair<juce::String, float>> extra = {})
{
    auto p = makeProc (program);
    for (auto& [id, v] : extra) p->setParam (id, v);
    for (auto* id : { "o1_det", "o2_det", "o2_semi", "o2_fine", "o1_fine", "chorus", "phaser", "crush", "nz_lvl", "br_growl", "glide", "echo", "verb", "haunt", "tp_on", "fenv" }) p->setParam (id, 0.0f);
    p->setParam ("sustain", 1.0f);      // a steady tone: the envelope / filter envelope would smear the spectrum
    for (int s = 1; s <= gpro::NUM_SLOTS; ++s) p->setParam ("m" + juce::String (s) + "_amt", 0.0f);
    for (int k = 2; k <= gpro::NUM_LAYERS; ++k) p->setParam ("ly" + juce::String (k) + "_src", 0.0f);
    p->setParam ("arp", 0.0f);
    p->rack.applyScene (p->apvts, "dry");
    settle (*p);
    auto b = render (*p, note (midi, 0.0, 1.6, 0.9f), 1.6);
    constexpr int ORDER = 14, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int st = (int) (0.4 * SR);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = (b.getSample (0, st + i) + b.getSample (1, st + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0), grid = f0 / 4.0, binHz = SR / N;
    double tot = 0, off = 0;
    for (int k = (int) (50 / binHz); k < (int) (20000 / binHz); ++k)
    {
        const double hz = k * binHz, pw = (double) buf[(size_t) k] * buf[(size_t) k];
        tot += pw;
        const double dist = std::abs (hz - std::round (hz / grid) * grid);
        if (dist > 6 * binHz) off += pw;
    }
    return 10.0 * std::log10 ((off + 1e-20) / (tot + 1e-20));
}

// ------------------------------------------------------------------ check
int leadCheck()
{
    printf ("== spec + macro check\n");
    const auto& bank = gpro::factoryBank();
    const auto progs = leadPrograms();
    int perCat[3] {};
    for (int i : progs) for (int c = 0; c < 3; ++c) if (bank[(size_t) i].cat == leadCats[c]) ++perCat[c];
    CHECK (progs.size() >= 36, "%d LEADS presets: Hypnotic %d, Euphoric %d, Aggressive %d", (int) progs.size(), perCat[0], perCat[1], perCat[2]);
    // the LEADS come after every older sound (program numbers of the older sounds unchanged)
    int firstLead = progs.empty() ? -1 : progs.front();
    // (only the SOUND ENGINE V2 SHOWCASE, added 2026-10-25, comes after them)
    const int endLead = firstLead + (int) progs.size();
    bool after = true; for (int i = firstLead; i < endLead; ++i) after &= isLead (bank[(size_t) i]);
    for (int i = endLead; i < (int) bank.size(); ++i) after &= bank[(size_t) i].cat == "V2 Showcase";
    CHECK (after && firstLead == 325, "LEADS are programs %d..%d, after all %d older sounds (then %d V2 SHOWCASE)", firstLead, endLead - 1, firstLead, (int) bank.size() - endLead);
    std::set<juce::String> names; int dup = 0; for (auto& f : bank) { if (names.count (f.name)) { ++dup; printf ("  duplicate name %s\n", f.name.toRawUTF8()); } names.insert (f.name); }
    CHECK (dup == 0, "no duplicate names in the whole bank (%d sounds)", (int) bank.size());
    static const char* banned[] { "serum", "sylenth", "massive", "spire", "vital", "nexus", "omnisphere", "avicii", "tiesto", "armin", "skrillex", "deadmau", "hardwell", "garrix", "guetta", "roland", "moog", "korg", "yamaha", "access", "virus", "jp-8", "jupiter", "juno", "303", "808", "dx7", "prophet", "oberheim", "minimoog" };
    int bannedHits = 0;
    for (int i : progs) { const auto n = bank[(size_t) i].name.toLowerCase(); for (auto* w : banned) if (n.contains (w)) { ++bannedHits; printf ("  name %s contains %s\n", n.toRawUTF8(), w); } }
    CHECK (bannedHits == 0, "no artist / product / brand names in the LEADS names");
    int bad = 0;
    for (int i : progs)
    {
        auto p = makeProc (i);
        const auto m = p->macros.getMap();
        const char* want[] { "BRIGHTNESS", "WIDTH", "MOVEMENT", "SIZE" };
        juce::String why;
        for (int k = 0; k < 4; ++k)
        {
            const auto& d = m.macro[(size_t) k];
            if (d.name != want[k]) why << " macro" << (k + 1) << " named '" << d.name << "'";
            if (d.targets.size() < 3) why << " macro" << (k + 1) << " has " << (int) d.targets.size() << " targets";
            int nonZero = 0;
            for (auto& t : d.targets)
            {
                auto* prm = dynamic_cast<juce::AudioParameterFloat*> (p->apvts.getParameter (t.param));
                if (prm == nullptr) why << " target '" << t.param << "' is not a float parameter";
                if (std::abs (t.amount) > 1e-6f) ++nonZero;
            }
            if (nonZero < 3) why << " macro" << (k + 1) << " only " << nonZero << " live targets";
        }
        // MOVEMENT rides slots 1 and 2: they must be LFO 2 > cutoff and LFO 2 > osc 1 position
        if ((int) p->apvts.getRawParameterValue ("m1_src")->load() != gpro::srcLfo2 || (int) p->apvts.getRawParameterValue ("m1_dst")->load() != gpro::dCutoff) why << " slot 1 is not lfo2>cut";
        if ((int) p->apvts.getRawParameterValue ("m2_src")->load() != gpro::srcLfo2 || (int) p->apvts.getRawParameterValue ("m2_dst")->load() != gpro::dPos1) why << " slot 2 is not lfo2>pos1";
        for (int k = 1; k < gpro::NUM_LAYERS; ++k)
        {
            const auto ln = p->layerSound (k);
            if ((int) p->ly[k].src->load() == gpro::lsSound && ln.isEmpty()) why << " layer " << (k + 1) << " is a sound but has none";
        }
        if (why.isNotEmpty()) { ++bad; printf ("  %s:%s\n", bank[(size_t) i].name.toRawUTF8(), why.toRawUTF8()); }
    }
    CHECK (bad == 0, "every lead: macros BRIGHTNESS WIDTH MOVEMENT SIZE, >=3 live targets each, every target a real float parameter, slots 1/2 = MOVEMENT routes");
    int stacks = 0; for (int i : progs) { auto& f = bank[(size_t) i]; if (f.spec.contains ("ly2=")) ++stacks; }
    CHECK (stacks >= 4, "%d EUPHORIC STACK presets with layers", stacks);
    return 0;
}

// ------------------------------------------------------------------ levels (same procedure as TrapTests levels: test phrase, -16 LUFS, peak <= -0.5 dB)
int leadLevels (const juce::String& out, double target)
{
    juce::StringArray lines;
    lines.add ("// generated by LeadsTests levels: measured trims (dB) that bring every LEADS preset to the bank's loudness target (-16 LUFS, test phrase)");
    for (int i : leadPrograms())
    {
        auto p = makeProc (i);
        p->trimGain.store (1.0f);                    // measure untrimmed
        auto buf = render (*p, testPhrase(), 5.0);
        int nans = 0; const float pk = peakOf (buf, &nans);
        const double L = lufs (buf, 0, (int) (4.0 * SR)), pkDb = juce::Decibels::gainToDecibels (pk, -120.0f);
        const double trim = juce::jlimit (-18.0, 14.0, std::min (target - L, -0.5 - pkDb));
        // the output soft clip changes level when it's hit: verify the trim, nudge once
        p = makeProc (i); p->trimGain.store (juce::Decibels::decibelsToGain ((float) trim));
        auto b2 = render (*p, testPhrase(), 5.0);
        const double L2 = lufs (b2, 0, (int) (4.0 * SR)), pk2 = juce::Decibels::gainToDecibels (peakOf (b2), -120.0f);
        const double trim2 = juce::jlimit (-18.0, 14.0, trim + std::min (target - L2, -0.5 - pk2));
        printf ("%-18s %-26s LUFS %6.1f peak %6.1f -> trim %6.2f (verify %6.1f LUFS, peak %6.1f)%s\n", gpro::factoryBank()[(size_t) i].cat.toRawUTF8(), gpro::factoryBank()[(size_t) i].name.toRawUTF8(), L, pkDb, trim2, L2, pk2, nans ? " NAN" : "");
        lines.add ("{ \"" + gpro::factoryBank()[(size_t) i].name + "\", " + juce::String (trim2, 2) + "f },");
        fflush (stdout);
    }
    if (out.isNotEmpty()) juce::File (out).replaceWithText (lines.joinIntoString ("\n") + "\n");
    return 0;
}

// levels on BOTH yardsticks: the bank's test phrase (chord + a bass line down to C2) and the 8-bar lead line. Each lead's trim puts
// it half-way between the two targets (-16 LUFS on the test phrase, the bank's median lead-line offset on the line), so the two
// spreads are as small as the sounds allow; peaks stay <= -0.5 dBFS on both.
int leadLevels2 (const juce::String& out)
{
    struct M { int prog; double trim, L = 0, mel = 0, pk = 0; };
    std::vector<M> ms;
    for (int i : leadPrograms()) ms.push_back ({ i, gpro::presetTrimDb (gpro::factoryBank()[(size_t) i].name) });
    const auto line = leadLine (8.0);
    auto measure = [&] (M& m)
    {
        auto p = makeProc (m.prog); p->trimGain.store (juce::Decibels::decibelsToGain ((float) m.trim));
        auto a = render (*p, testPhrase(), 5.0);
        auto q = makeProc (m.prog); q->trimGain.store (juce::Decibels::decibelsToGain ((float) m.trim));
        auto b = render (*q, line, 8.0);
        m.L = lufs (a, 0, (int) (4.0 * SR)); m.mel = lufs (b, 0, (int) (6.4 * SR)); m.pk = juce::Decibels::gainToDecibels (std::max (peakOf (a), peakOf (b)), -120.0f);
    };
    for (int pass = 0; pass < 3; ++pass)          // the output soft clip makes level non-linear near 0 dBFS: measure at the trim, correct, repeat
    {
        for (auto& m : ms) measure (m);
        std::vector<double> d; for (auto& m : ms) d.push_back (m.mel - m.L);
        auto sorted = d; std::sort (sorted.begin(), sorted.end()); const double med = sorted[sorted.size() / 2];
        double lo = 1e9, hi = -1e9, mlo = 1e9, mhi = -1e9, pk = -1e9;
        for (auto& m : ms) { lo = std::min (lo, m.L); hi = std::max (hi, m.L); mlo = std::min (mlo, m.mel); mhi = std::max (mhi, m.mel); pk = std::max (pk, m.pk); }
        printf ("pass %d: test phrase %.1f .. %.1f (%.1f LU), lead line %.1f .. %.1f (%.1f LU), worst peak %.1f dBFS\n", pass, lo, hi, hi - lo, mlo, mhi, mhi - mlo, pk);
        if (pass == 2) break;
        for (auto& m : ms)
        {
            const double delta = std::min (-((m.L + 16.0) + (m.mel + 16.0)) / 2.0, -0.6 - m.pk);   juce::ignoreUnused (med);
            m.trim = juce::jlimit (-18.0, 14.0, m.trim + delta);
        }
        fflush (stdout);
    }
    juce::StringArray lines;
    lines.add ("// generated by ProTests leads levels2: each lead's trim (dB) centres its two levels (the bank's test phrase and the 8-bar lead line) on -16 LUFS, peaks <= -0.6 dBFS");
    for (auto& m : ms)
    {
        lines.add ("{ \"" + gpro::factoryBank()[(size_t) m.prog].name + "\", " + juce::String (m.trim, 2) + "f },");
        printf ("%-26s test %6.1f  line %6.1f  peak %6.1f  trim %+6.2f\n", gpro::factoryBank()[(size_t) m.prog].name.toRawUTF8(), m.L, m.mel, m.pk, m.trim);
    }
    if (out.isNotEmpty()) juce::File (out).replaceWithText (lines.joinIntoString ("\n") + "\n");
    return 0;
}

// ------------------------------------------------------------------ report
struct MacroRes { double zipRamp = 0, zipJump = 0, zipBase = -200, dCorr = 0, dCent = 0, dWidth = 0, dLufs = 0, dTail = 0, dMove = 0, specDist = 0, rampR = 0, jumpR = 0, staticR = 0; };
double specDistance (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double e = 0, s = 0; sameSound (a, b, e, s, 99, 99); return s;   // level-aligned 1/3-octave spectrum difference (max band, dB)
}

int leadReport (const juce::String& csvPath, int from = 0, int count = 1000)
{
    const auto& bank = gpro::factoryBank();
    juce::StringArray csv;
    csv.add ("cat,name,lufs,peak_db,melody_lufs,alias_db,alias_c7_db,cpu_chord_pct,cpu_size1_pct,tail_db,voices_left,nans,"
             "bright_dcent_oct,width_d_db,width_dcorr,move_ratio,size_dcent_oct,size_dwidth_db,size_dlufs,size_dtail_db,"
             "ramp_edge_max,jump_edge_max,static_edge");
    std::vector<double> allL, allMel; double worstPk = -100, worstAlias = -200, worstCpu = 0, worstTail = -200; int totalFails = 0;
    auto ph = testPhrase();
    const auto all = leadPrograms();
    for (int li = from; li < juce::jmin ((int) all.size(), from + count); ++li)
    {
        const int i = all[(size_t) li];
        const auto& f = bank[(size_t) i];
        juce::String fails;
        // ---- level / peak / NaN / tail / stuck voices (test phrase, 12 s)
        auto p = makeProc (i);
        auto buf = render (*p, ph, 20.0);
        int nans = 0; const float pk = peakOf (buf, &nans);
        const double L = lufs (buf, 0, (int) (4.0 * SR)), pkDb = juce::Decibels::gainToDecibels (pk, -120.0f);
        const double tail = rmsDb (buf, (int) (19.0 * SR), (int) (20.0 * SR));      // 15.6 s after the last note-off
        int left = p->voicesLive.load(); for (int k = 1; k < gpro::NUM_LAYERS; ++k) left += p->layerVoices[k].load();
        // ---- the lead line (what the pack is for), loudness only
        auto pm = makeProc (i); const auto line = leadLine (8.0);
        auto mel = render (*pm, line, 8.0); const double melL = lufs (mel, 0, (int) (6.4 * SR));
        // ---- aliasing (C7, oscillators + filter + drive, FX off)
        const double al = aliasingDb (i, 84), al7 = aliasingDb (i, 96);
        // ---- CPU: 6-note chord 4 s, at SIZE 0 and SIZE 1 (wider, layers up)
        auto cpu = [&] (float size)
        {
            auto q = makeProc (i); q->setParam ("mac4", size); q->setParam ("arp", 0.0f);
            std::vector<Ev> evs; for (int k = 0; k < 6; ++k) evs = evs + note (55 + (k * 5) % 24, 0.05 + k * 0.03, 3.0);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto b = render (*q, evs, 4.0);
            return (juce::Time::getMillisecondCounterHiRes() - t0) / 4000.0 * 100.0;
        };
        const double cpu0 = cpu (0.0f), cpu1 = cpu (1.0f);
        // ---- macros: static renders of the lead line at knob 0 / 1 (BRIGHTNESS / WIDTH bipolar: 0 vs 1 = darkest vs brightest)
        auto at = [&] (int k, float v) { auto q = makeProc (i); q->setParam ("mac" + juce::String (k + 1), v); settle (*q); return render (*q, line, 8.0); };
        const double bodyEnd = 6.4, tailS = 6.5, tailE = 7.5;
        MacroRes mr[4];
        for (int k = 0; k < 4; ++k)
        {
            auto a = at (k, 0.0f), b = at (k, 1.0f);
            const auto fa = features (a, bodyEnd, tailS, tailE), fb = features (b, bodyEnd, tailS, tailE);
            mr[k].dCent = std::log2 (std::max (1.0, fb.centroid) / std::max (1.0, fa.centroid));
            mr[k].dWidth = fb.width - fa.width; mr[k].dCorr = fa.corr - fb.corr; mr[k].dLufs = fb.lufs - fa.lufs; mr[k].dTail = (fb.tail - fb.body) - (fa.tail - fa.body);
            mr[k].dMove = movement (b, bodyEnd) / std::max (1e-3, movement (a, bodyEnd));
            mr[k].specDist = specDistance (a, b);
            // smoothness: the knob automated 0 -> 1 over 5 s at block rate, and a jump 0 -> 1 at 3 s; block-edge roughness vs a static render
            const float home = (k < 2) ? 0.5f : 0.0f;
            // (rendered like a host plays: the system random seeded once, not re-seeded at every block - that re-seeding alone makes
            //  the block edges rough and was mistaken for a zipper)
            // block-edge roughness (2nd-difference energy right at the 512-sample edges vs half-way between them) of the knob
            // automated 0 -> 1 over 5 s and jumped 0 -> 1 at 3 s, divided by the rougher of the two parked renders (knob at its
            // home and at 1): modulation the knob turns on is allowed its own roughness, the zipper is what the MOVING knob adds
            const juce::String id = "mac" + juce::String (k + 1);
            auto qs = makeProc (i); qs->setParam ("arp", 0.0f); qs->setParam (id, home); auto st = renderAuto (*qs, line, 7.0, {}, BS, false);
            auto q1 = makeProc (i); q1->setParam ("arp", 0.0f); q1->setParam (id, 1.0f); auto s1 = renderAuto (*q1, line, 7.0, {}, BS, false);
            auto qr = makeProc (i); qr->setParam ("arp", 0.0f); auto rp = renderAuto (*qr, line, 7.0, [&] (double t) { qr->setParam (id, (float) juce::jlimit (0.0, 1.0, (t - 0.5) / 5.0)); }, BS, false);
            // jumps: the knob slammed 0 <-> 1 every 0.4 s (14 jumps: enough edges for a stable number)
            auto qj = makeProc (i); qj->setParam ("arp", 0.0f); auto jp = renderAuto (*qj, line, 7.0, [&] (double t) { qj->setParam (id, ((int) std::floor (t / 0.4)) % 2 ? 1.0f : 0.0f); }, BS, false);
            const double parked = std::max (edgeRoughness (st, line, 0.5, 6.0), edgeRoughness (s1, line, 0.5, 6.0));
            mr[k].zipRamp = edgeRoughness (rp, line, 0.5, 5.5) / parked; mr[k].zipJump = edgeRoughness (jp, line, 0.5, 6.0) / parked; mr[k].zipBase = parked;
            // zipper = click energy at the 512-sample host-block edges (> 5 kHz, excess over the same music half-way between edges),
            // dB re the signal, for the ramp and the 14 jumps (the roughness ratios above are printed for reference: with saw unison
            // their value depends on the random unison phases)
            mr[k].rampR = edgeClickDb (rp, line, 0.5, 5.5); mr[k].jumpR = edgeClickDb (jp, line, 0.5, 6.0);
            mr[k].staticR = std::max (edgeClickDb (st, line, 0.5, 6.0), edgeClickDb (s1, line, 0.5, 6.0));   // what the sound has with the knob parked
            int n1 = 0, n2 = 0; peakOf (rp, &n1); peakOf (jp, &n2); nans += n1 + n2;
        }
        // ---- pass / fail
        if (nans) fails << " NaN";
        if (std::abs (L - (-16.0)) > 1.5) fails << " level " << juce::String (L, 1);              // both yardsticks within 1.5 LU of -16 LUFS
        if (std::abs (melL - (-16.0)) > 1.5) fails << " lead-line level " << juce::String (melL, 1);
        if (pkDb > -0.3) fails << " peak " << juce::String (pkDb, 1);
        if (tail > -80) fails << " tail " << juce::String (tail, 1) << " dB";
        if (left) fails << " " << left << " voices left";
        if (al > -40) fails << " aliasing(C6) " << juce::String (al, 1);
        if (cpu1 > 30) fails << " cpu " << juce::String (cpu1, 1);
        if (mr[0].dCent < 0.5) fails << " BRIGHTNESS only " << juce::String (mr[0].dCent, 2) << " oct";
        // SIZE reverb: a two-note phrase stopped, tail (0.3 .. 1.3 s after) vs body, at SIZE 0 and 1
        auto tailAt = [&] (float sz) { auto q = makeProc (i); q->setParam ("arp", 0.0f); q->setParam ("mac4", sz); auto r = render (*q, note (69, 0.05, 1.0) + note (72, 1.05, 1.0), 4.0); return rmsDb (r, (int) (2.35 * SR), (int) (3.35 * SR)) - rmsDb (r, 0, (int) (2.0 * SR)); };
        mr[3].dTail = tailAt (1.0f) - tailAt (0.0f);
        if (mr[1].dWidth < 3.0 && mr[1].dCorr < 0.15) fails << " WIDTH only " << juce::String (mr[1].dWidth, 1) << " dB";
        if (mr[2].dMove < 1.3 && mr[2].specDist < 2.0) fails << " MOVEMENT x" << juce::String (mr[2].dMove, 2);
        if (mr[3].dCent < 0.15 || (mr[3].dWidth < 1.5 && mr[3].dCorr < 0.08) || mr[3].dTail < 2.0) fails << " SIZE weak (" << juce::String (mr[3].dCent, 2) << " oct, " << juce::String (mr[3].dWidth, 1) << " dB width, " << juce::String (mr[3].dTail, 1) << " dB tail)";
        if (mr[3].dLufs > 4.5 || mr[3].dLufs < -1.0) fails << " SIZE level " << juce::String (mr[3].dLufs, 1) << " dB";
        double rampMax = 0, jumpMax = 0, statMax = 0; int rampK = 0, jumpK = 0;
        rampMax = -300; jumpMax = -300; statMax = -300;
        for (int k = 0; k < 4; ++k) { auto& m = mr[k]; if (m.zipRamp > rampMax) { rampMax = m.zipRamp; rampK = k + 1; } if (m.zipJump > jumpMax) { jumpMax = m.zipJump; jumpK = k + 1; } statMax = std::max (statMax, m.zipBase); }
        // a zipper = the MOVING knob's block-edge clicks above -40 dB re the signal AND 3 dB above what the parked sound already has
        double blkMax = -300; int blkK = 0, zipK = 0; double zipOver = -300;
        for (int k = 0; k < 4; ++k)
        {
            const double mv = std::max (mr[k].rampR, mr[k].jumpR);
            if (mv > blkMax) { blkMax = mv; blkK = k + 1; }
            if (mv > -40.0 && mv - std::max (-40.0, mr[k].staticR) > 3.0 && mv - mr[k].staticR > zipOver) { zipOver = mv - mr[k].staticR; zipK = k + 1; }
        }
        juce::ignoreUnused (blkK);
        if (zipK > 0) fails << " zipper: block-edge clicks +" << juce::String (zipOver, 1) << " dB over the parked sound (macro " << zipK << ")";
        juce::ignoreUnused (rampK, jumpK);
        if (fails.isNotEmpty()) ++totalFails;
        printf ("%s %-17s %-22s L%6.1f pk%5.1f mel%6.1f al%6.1f/%6.1f cpu%5.1f/%5.1f tail%6.0f | BR %+.2foct WI %+.1fdB/%+.2fc MV x%.2f | SIZE %+.2foct %+.1fdB-w %+.1fLU %+.1fdB-tail | zip ramp x%.2f jumps x%.2f (parked %.2f) clicks %.0fdB%s%s\n",
                fails.isEmpty() ? "  ok " : "FAIL ", f.cat.toRawUTF8(), f.name.toRawUTF8(), L, pkDb, melL, al, al7, cpu0, cpu1, tail,
                mr[0].dCent, mr[1].dWidth, mr[1].dCorr, mr[2].dMove, mr[3].dCent, mr[3].dWidth, mr[3].dLufs, mr[3].dTail, rampMax, jumpMax, statMax, blkMax, fails.isEmpty() ? "" : " <-", fails.toRawUTF8());
        fflush (stdout);
        allL.push_back (L); allMel.push_back (melL); worstPk = std::max (worstPk, pkDb); worstAlias = std::max (worstAlias, al); worstCpu = std::max (worstCpu, cpu1); worstTail = std::max (worstTail, tail);
        juce::StringArray row { "\"" + f.cat + "\"", "\"" + f.name + "\"" };
        for (double v : { L, pkDb, melL, al, al7, cpu0, cpu1, tail, (double) left, (double) nans, mr[0].dCent, mr[1].dWidth, mr[1].dCorr, mr[2].dMove, mr[3].dCent, mr[3].dWidth, mr[3].dLufs, mr[3].dTail, rampMax, jumpMax, statMax })
            row.add (juce::String (v, 3));
        csv.add (row.joinIntoString (","));
    }
    auto spread = [] (const std::vector<double>& v) { double lo = 1e9, hi = -1e9; for (auto x : v) { lo = std::min (lo, x); hi = std::max (hi, x); } return std::make_pair (lo, hi); };
    const auto [lo, hi] = spread (allL); const auto [mlo, mhi] = spread (allMel);
    printf ("\nLEADS: %d presets, %d with a FAIL | test-phrase LUFS %.1f .. %.1f (spread %.1f) | lead-line LUFS %.1f .. %.1f (spread %.1f) | worst peak %.1f dBFS | worst aliasing %.1f dB | worst CPU (6 notes, SIZE 1) %.1f%% | loudest tail 15.6 s after the last note-off %.0f dBFS\n",
            (int) allL.size(), totalFails, lo, hi, hi - lo, mlo, mhi, mhi - mlo, worstPk, worstAlias, worstCpu, worstTail);
    if (csvPath.isNotEmpty()) juce::File (csvPath).replaceWithText (csv.joinIntoString ("\n") + "\n");
    failures += totalFails;
    return 0;
}

// SIZE build-up in detail for every lead: the knob automated 0 -> 1 over 8 s; brightness / width / level / reverb per 2 s quarter
int sizeReport()
{
    const auto& bank = gpro::factoryBank();
    printf ("== SIZE build-up (knob 0 -> 1 over 8 s on the lead line; each 2 s quarter vs the SAME quarter played at SIZE 0, so the melody cancels:\n"
            "   centroid ratio, side/mid change dB, LUFS change; reverb = tail after a stop at SIZE 0 and 1)\n");
    int bad = 0;
    for (int i : leadPrograms())
    {
        const auto line = leadLine (10.0);
        auto p = makeProc (i);
        auto b = renderAuto (*p, line, 10.0, [&] (double t) { p->setParam ("mac4", (float) juce::jlimit (0.0, 1.0, t / 8.0)); }, BS, false);
        auto r = makeProc (i);
        auto ref = renderAuto (*r, line, 10.0, {}, BS, false);
        double c[4], w[4], l[4];
        for (int q = 0; q < 4; ++q)
        {
            juce::AudioBuffer<float> seg (2, (int) (2.0 * SR)), segR (2, (int) (2.0 * SR));
            for (int ch = 0; ch < 2; ++ch) { seg.copyFrom (ch, 0, b, ch, (int) (q * 2.0 * SR), (int) (2.0 * SR)); segR.copyFrom (ch, 0, ref, ch, (int) (q * 2.0 * SR), (int) (2.0 * SR)); }
            auto fq = features (seg, 2.0, 1.9, 2.0), fr = features (segR, 2.0, 1.9, 2.0);
            c[q] = fq.centroid / std::max (1.0, fr.centroid); w[q] = fq.width - fr.width; l[q] = fq.lufs - fr.lufs;
        }
        auto tailAt = [&] (float s) { auto q = makeProc (i); q->setParam ("mac4", s); auto rr = render (*q, note (69, 0.05, 1.0) + note (72, 1.05, 1.0), 4.0); return rmsDb (rr, (int) (2.3 * SR), (int) (3.3 * SR)) - rmsDb (rr, 0, (int) (2.0 * SR)); };
        const double t0 = tailAt (0.0f), t1 = tailAt (1.0f);
        const bool ok = c[3] >= 1.08 && w[3] >= 1.0 && t1 >= t0 + 2.0 && l[3] <= 5.0 && l[3] >= -1.0 && c[3] >= c[0];
        if (! ok) ++bad;
        printf ("%s %-24s centroid x%.2f x%.2f x%.2f x%.2f | width %+5.1f %+5.1f %+5.1f %+5.1f dB | LUFS %+5.1f %+5.1f %+5.1f %+5.1f | reverb tail %5.1f -> %5.1f dB\n",
                ok ? "  ok " : "FAIL ", bank[(size_t) i].name.toRawUTF8(), c[0], c[1], c[2], c[3], w[0], w[1], w[2], w[3], l[0], l[1], l[2], l[3], t0, t1);
        fflush (stdout);
    }
    CHECK (bad == 0, "SIZE is a build-up on every lead: by the last quarter brighter (>= x1.08 centroid), wider (>= +1 dB side), level -1 .. +5 LU, more reverb (>= +2 dB tail)");
    return 0;
}

// ------------------------------------------------------------------ state / automation
int leadState()
{
    printf ("== state\n");
    const int lead = progOf ("Festival Saw Stack"), old = progOf ("Hyper Saw Anthem");
    {
        auto p = makeProc (lead);
        p->setParam ("mac4", 0.7f);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (old);
        CHECK (! q->macros.hasMap(), "an older preset has no macro map");
        q->setStateInformation (mb.getData(), (int) mb.getSize()); pump();
        const auto a = p->macros.getMap().toTree(), b = q->macros.getMap().toTree();
        CHECK (a.isEquivalentTo (b) && q->macros.name (3) == "SIZE", "MACROS survive the saved state (names, targets), macro 4 = %s", q->macros.name (3).toRawUTF8());
        CHECK (std::abs (q->apvts.getRawParameterValue ("mac4")->load() - 0.7f) < 1e-4f, "the knob value is in the state too (%.2f)", q->apvts.getRawParameterValue ("mac4")->load());
        // same sound after the round trip
        seedSystemRandom (99); auto r1 = render (*p, testPhrase(), 3.5); seedSystemRandom (99); auto r2 = render (*q, testPhrase(), 3.5);
        double e = 0, s = 0; const bool same = sameSound (r1, r2, e, s);
        CHECK (same, "state round trip plays the same: envelope %.2f dB, spectrum %.2f dB", e, s);
        // an old state (no MACROS child) loaded over a lead: no map left behind
        auto o = makeProc (old); juce::MemoryBlock ob; o->getStateInformation (ob);
        q->setStateInformation (ob.getData(), (int) ob.getSize()); pump();
        CHECK (! q->macros.hasMap(), "an old state loaded over a lead clears the macros");
        juce::String xml; if (auto x = Peek::getXmlFromBinary (ob.getData(), (int) ob.getSize())) xml = x->toString();
        CHECK (! xml.contains ("MACROS"), "a sound without macros saves no MACROS child (old states unchanged)");
        // program change lead -> old: map cleared, every float shadow back to its raw value after a block
        q->setCurrentProgram (lead); q->setCurrentProgram (old); settle (*q); render (*q, {}, 0.1);
        int diff = 0; for (auto* prm : q->getParameters()) if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm)) { const auto id = fp->getParameterID(); if (std::abs (q->macros.effective (id) - q->apvts.getRawParameterValue (id)->load()) > 1e-7f && q->macros.effective (id) != 0.0f) ++diff; }
        CHECK (diff == 0, "after switching to an older preset every parameter plays its own value (%d differ)", diff);
    }
    {
        // user preset save / load keeps the macros
        auto p = makeProc (progOf ("Screaming Sync Lead"));
        const juce::String nm = "zz LeadsTests tmp";
        CHECK (p->saveUserPreset (nm), "user preset saved");
        auto q = makeProc (old);
        q->loadUserPreset (ProProcessor::userPresetDir().getChildFile (nm + ".ghostypro"));
        CHECK (q->macros.name (0) == "BRIGHTNESS" && q->macros.getMap().toTree().isEquivalentTo (p->macros.getMap().toTree()), "user preset keeps the 4 macros");
        ProProcessor::userPresetDir().getChildFile (nm + ".ghostypro").deleteFile();
    }
    {
        auto p = makeProc (lead);
        int automatable = 0;
        for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) if (rp->getParameterID().startsWith ("mac") && rp->isAutomatable()) ++automatable;
        CHECK (automatable == 4, "the 4 macro knobs are host-automatable parameters (%d)", automatable);
        // SIZE moved: its targets move with it (after the 20 ms smoothing)
        const float c0 = p->macros.effective ("verb");
        p->setParam ("mac4", 1.0f); render (*p, {}, 0.3);
        const float c1 = p->macros.effective ("verb");
        CHECK (c1 > c0 + 0.2f, "SIZE 1 raises the reverb send %.2f -> %.2f", c0, c1);
        // a moving macro renders in 32-sample fine steps; once it has settled, and on sounds without macros, never
        const int f0 = p->fineBlocks.load(); p->setParam ("mac1", 0.9f); render (*p, {}, 0.05); const int f1 = p->fineBlocks.load();
        render (*p, {}, 2.0); const int f2 = p->fineBlocks.load(); render (*p, {}, 0.3); const int f3 = p->fineBlocks.load();
        auto o = makeProc (old); o->setParam ("mac1", 0.9f); render (*o, {}, 0.5);
        CHECK (f1 > f0 && f3 == f2 && o->fineBlocks.load() == 0, "fine steps while BRIGHTNESS glides (%d blocks), none once settled (%d), none on an older sound (%d)", f1 - f0, f3 - f2, o->fineBlocks.load());
        // fine steps are transparent: the lead line with fine steps forced (SIZE wobbling by 1e-4) vs plain
        {
            const auto line = leadLine (4.0);
            auto a = makeProc (lead); auto ra = renderAuto (*a, line, 4.0, {}, BS, false);
            auto b = makeProc (lead); int k = 0; auto rb = renderAuto (*b, line, 4.0, [&] (double) { b->setParam ("mac4", (k++ % 2) ? 1.0e-4f : 0.0f); }, BS, false);
            CHECK (b->fineBlocks.load() > 100 && maxDiff (ra, rb) < 0.003f, "fine steps render the same sound: max difference %.2g over %d fine-stepped blocks", maxDiff (ra, rb), b->fineBlocks.load());
        }
        // STACK: the layers follow the parent's knobs
        float mv[4]; p->macros.macroValues (mv);
        CHECK (std::abs (mv[3] - 1.0f) < 1e-6f, "parent macro values handed to the layers (SIZE %.2f)", mv[3]);
    }
    return 0;
}

// ------------------------------------------------------------------ threads: the handoff under load
int leadThreads()
{
    printf ("== threads: preset changes + macro maps on the message thread while an audio thread plays\n");
    auto p = makeProc (progOf ("Festival Saw Stack"));
    std::atomic<bool> run { true }; std::atomic<int> blocks { 0 }, bad { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> b (2, BS); int k = 0;
        while (run.load())
        {
            juce::MidiBuffer mb; if (k % 40 == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 60 + k % 12, 0.8f), 0); if (k % 40 == 20) mb.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            b.clear(); p->processBlock (b, mb); ++k; blocks.store (k);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < BS; ++i) if (! std::isfinite (b.getSample (ch, i))) bad.fetch_add (1);
        }
    });
    const auto progs = leadPrograms(); const int old = progOf ("Hyper Saw Anthem");
    for (int r = 0; r < 300; ++r)
    {
        p->setCurrentProgram ((r % 3 == 0) ? old : progs[(size_t) r % progs.size()]);
        p->setParam ("mac4", (float) (r % 10) / 9.0f);
        if (r % 7 == 0) { gpro::MacroMap m; m.parseToken ("mc", "aggr"); p->macros.setMap (m); }
        pump (1);
    }
    run.store (false); audio.join();
    CHECK (bad.load() == 0 && blocks.load() > 50, "%d audio blocks under 300 preset / map changes, %d non-finite samples", blocks.load(), bad.load());
    return 0;
}

// ------------------------------------------------------------------ demos
juce::File toMp3 (const juce::AudioBuffer<float>& b, const juce::File& dir, const juce::String& name)
{
    auto wav = dir.getChildFile (name + ".wav"); writeWav (b, wav);
    auto mp3 = dir.getChildFile (name + ".mp3"); mp3.deleteFile();
    juce::ChildProcess ff;
    if (ff.start (juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wav.getFullPathName(), "-codec:a", "libmp3lame", "-q:a", "2", mp3.getFullPathName() })) ff.waitForProcessToFinish (60000);
    wav.deleteFile();
    return mp3;
}
// 8 bars at 128 BPM, A minor (Am F C G), each folder its own phrase
std::vector<Ev> demoPhrase (int kind)
{
    const double beat = 60.0 / 128.0; std::vector<Ev> e;
    auto add = [&] (int n, double b, double len, float v = 0.85f) { e = e + note (n, 0.05 + b * beat, len * beat * 0.92, v); };
    if (kind == 0)          // HYPNOTIC: a rolling 8th motif that shifts with the chords
    {
        const int roots[] { 69, 65, 72, 67 };
        const int motif[] { 0, 7, 12, 7, 3, 7, 10, 7 };
        for (int bar = 0; bar < 8; ++bar)
            for (int s = 0; s < 8; ++s)
            {
                int n = roots[bar % 4] + motif[s]; if (bar % 4 == 1 || bar % 4 == 2) n = roots[bar % 4] + (motif[s] == 3 ? 4 : motif[s]);
                add (n - 12, bar * 4 + s * 0.5, 0.5, s % 2 ? 0.7f : 0.9f);
            }
    }
    else if (kind == 1)     // EUPHORIC: the anthem line
    {
        struct N { int n; double b, l; };
        const N a[] { { 76, 0, 1.5 }, { 74, 1.5, .5 }, { 72, 2, 1 }, { 71, 3, 1 }, { 72, 4, 1.5 }, { 74, 5.5, .5 }, { 76, 6, 2 },
                      { 79, 8, 1.5 }, { 77, 9.5, .5 }, { 76, 10, 1 }, { 74, 11, 1 }, { 74, 12, 1.5 }, { 72, 13.5, .5 }, { 71, 14, 2 } };
        for (int rep = 0; rep < 2; ++rep) for (auto& n : a) add (n.n + (rep == 1 && n.b >= 12 ? (n.n == 71 ? -2 : 0) : 0), rep * 16 + n.b, n.l, 0.9f);
        // a last long tonic
        e.back().t = e.back().t;
    }
    else                    // AGGRESSIVE: a punchy riff, lower register, 16th pickups
    {
        const double pat[] { 0, 0.75, 1.5, 2, 2.5, 3, 3.25, 3.5 };
        const int off[] { 0, 0, 3, 0, 7, 5, 3, 0 };
        const int roots[] { 57, 53, 60, 55 };
        for (int bar = 0; bar < 8; ++bar) for (int s = 0; s < 8; ++s) add (roots[bar % 4] + off[s] + (bar == 7 && s >= 6 ? 12 : 0), bar * 4 + pat[s], s == 2 ? 0.5 : 0.25, s == 0 ? 1.0f : 0.8f);
    }
    return e;
}
int leadDemo (const juce::File& dir)
{
    dir.createDirectory();
    const double beat = 60.0 / 128.0, secs = 32 * beat + 3.0;
    struct D { const char* preset; int kind; const char* file; };
    const D ds[] { { "Hypnotic Pluck", 0, "HYPNOTIC_8bars_Hypnotic_Pluck" }, { "Festival Saw Stack", 1, "EUPHORIC_8bars_Festival_Saw_Stack" }, { "Screaming Sync Lead", 2, "AGGRESSIVE_8bars_Screaming_Sync_Lead" } };
    for (auto& d : ds)
    {
        auto p = makeProc (progOf (d.preset));
        // MOVEMENT half-way on the hypnotic one: the filter walks
        if (d.kind == 0) p->setParam ("mac3", 0.5f);
        auto ev = demoPhrase (d.kind);
        auto b = renderAuto (*p, ev, secs, {});
        auto f = toMp3 (b, dir, d.file);
        int nans = 0; const float pk = peakOf (b, &nans);
        CHECK (f.getSize() > 10000 && nans == 0 && pk < 1.0f, "%s: %.1f LUFS, peak %.2f -> %s (%d kB)", d.preset, lufs (b), pk, f.getFileName().toRawUTF8(), (int) (f.getSize() / 1024));
    }
    // SIZE build-up: the euphoric phrase, SIZE 0 -> 1 over bars 1-8 (automation), on two sounds
    for (auto* nm : { "Festival Saw Stack", "Warm Saw Drift" })
    {
        auto p = makeProc (progOf (nm));
        auto ev = demoPhrase (1);
        auto b = renderAuto (*p, ev, secs, [&] (double t) { p->setParam ("mac4", (float) juce::jlimit (0.0, 1.0, (t - 0.05) / (32 * beat))); });
        auto f = toMp3 (b, dir, juce::String ("SIZE_buildup_") + safeName (nm));
        int nans = 0; const float pk = peakOf (b, &nans);
        CHECK (f.getSize() > 10000 && nans == 0 && pk < 1.0f, "SIZE build-up on %s: first 2 bars %.1f LUFS -> last 2 bars %.1f LUFS, peak %.2f -> %s", nm, lufs (b, 0, (int) (8 * beat * SR)), lufs (b, (int) (24 * beat * SR), (int) (32 * beat * SR)), pk, f.getFileName().toRawUTF8());
    }
    // all 40 in one file: each lead plays 2 bars of its folder's phrase (a quick audition reel)
    {
        juce::AudioBuffer<float> reel (2, 1); reel.clear(); int pos = 0;
        const auto& bank = gpro::factoryBank();
        for (int i : leadPrograms())
        {
            const int kind = bank[(size_t) i].cat == "Leads Hypnotic" ? 0 : bank[(size_t) i].cat == "Leads Euphoric" ? 1 : 2;
            std::vector<Ev> ev; for (auto& e : demoPhrase (kind)) if (e.t < 8 * beat) ev.push_back (e);
            for (auto& e : ev) if (! e.on && e.t > 8 * beat - 0.05) e.t = 8 * beat - 0.05;
            auto p = makeProc (i);
            auto b = render (*p, ev, 8 * beat + 0.8);
            reel.setSize (2, pos + b.getNumSamples(), true, true);
            for (int ch = 0; ch < 2; ++ch) reel.copyFrom (ch, pos, b, ch, 0, b.getNumSamples());
            pos += b.getNumSamples();
        }
        auto f = toMp3 (reel, dir, "ALL_40_LEADS_reel");
        CHECK (f.getSize() > 10000, "audition reel: %d presets x 2 bars -> %s (%d kB)", (int) leadPrograms().size(), f.getFileName().toRawUTF8(), (int) (f.getSize() / 1024));
    }
    return 0;
}

int leadShots (const juce::File& dir)
{
    // the browser on a LEADS sound and the MOD page with the 4 macro names, DARK and LIGHT
    dir.createDirectory();
    auto p = makeProc (progOf ("Festival Saw Stack"));
    p->setParam ("mac4", 0.6f); p->setParam ("mac3", 0.3f); render (*p, {}, 0.2);
    int okShots = 0;
    for (bool light : { false, true })
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = dynamic_cast<ProEditor*> (ed.get());
        if (pe == nullptr) { printf ("FAIL  no editor\n"); ++failures; return 1; }
        pe->setSize (ProEditor::W, ProEditor::H); pe->setLightTheme (light);
        for (int tab : { 1, 3 })
        {
            pe->showTab (tab); pump (400);
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("leads-") + (light ? "light-" : "dark-") + (tab == 1 ? "macros" : "browser") + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            okShots += f.getSize() > 10000;
            printf ("  %s\n", f.getFullPathName().toRawUTF8());
        }
        // the macro knobs carry the sound's names
        juce::StringArray labels;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* l = dynamic_cast<juce::Label*> (k)) labels.add (l->getText()); find (*k); } };
        pe->showTab (1); pump (200); find (*pe);
        CHECK (labels.contains ("BRIGHTNESS") && labels.contains ("WIDTH") && labels.contains ("MOVEMENT") && labels.contains ("SIZE"), "%s MOD page: the 4 macro knobs read BRIGHTNESS / WIDTH / MOVEMENT / SIZE", light ? "LIGHT" : "DARK ");
    }
    CHECK (okShots == 4, "4 screenshots (browser + macros, DARK + LIGHT) in %s", dir.getFullPathName().toRawUTF8());
    return 0;
}
} // namespace

static int leadsModes (const juce::StringArray& argsIn)
{
    juce::StringArray args (argsIn); args.remove (0);     // drop "leads"
    const auto mode = args[0];
    int r = 2;
    if (mode == "check") r = leadCheck();
    else if (mode == "levels") r = leadLevels (args[1], -16.0);
    else if (mode == "levels2") r = leadLevels2 (args[1]);
    else if (mode == "report") r = leadReport (args[1], args.size() > 2 ? args[2].getIntValue() : 0, args.size() > 3 ? args[3].getIntValue() : 1000);
    else if (mode == "size") r = sizeReport();
    else if (mode == "state") r = leadState();
    else if (mode == "threads") r = leadThreads();
    else if (mode == "demo") r = leadDemo (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/proleads/demos")));
    else if (mode == "shots") r = leadShots (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/proleads/demos")));
    else if (mode == "baseline")      // the same aliasing / tail numbers for older bank sounds (reference)
    {
        for (int i = 1; i < args.size(); ++i)
        {
            const int pr = progOf (args[i]); auto p = makeProc (pr); auto buf = render (*p, testPhrase(), 20.0);
            printf ("%-26s aliasing %6.1f dB  tail(19-20 s) %6.0f dBFS  LUFS %6.1f\n", args[i].toRawUTF8(), aliasingDb (pr, 96), rmsDb (buf, (int) (19 * SR), (int) (20 * SR)), lufs (buf, 0, (int) (4 * SR)));
        }
        r = 0;
    }
    else if (mode == "zip")          // zip <preset> <macro 1..4>: ramp / jump edge roughness with only one target of that macro live
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1;
        auto base = makeProc (pr); const auto full = base->macros.getMap(); const auto line = leadLine (8.0);
        for (int t = -1; t < (int) full.macro[(size_t) k].targets.size(); ++t)
        {
            gpro::MacroMap m = full;
            if (t >= 0) { m.macro[(size_t) k].targets = { full.macro[(size_t) k].targets[(size_t) t] }; }
            auto q = makeProc (pr); q->setParam ("arp", 0.0f); q->macros.setMap (m);
            auto rp = renderAuto (*q, line, 7.0, [&] (double tt) { q->setParam ("mac" + juce::String (k + 1), (float) juce::jlimit (0.0, 1.0, (tt - 0.5) / 5.0)); }, BS, false);
            auto qj = makeProc (pr); qj->setParam ("arp", 0.0f); qj->macros.setMap (m);
            auto jp = renderAuto (*qj, line, 7.0, [&] (double tt) { qj->setParam ("mac" + juce::String (k + 1), tt < 3.0 ? 0.0f : 1.0f); }, BS, false);
            printf ("%-12s ramp %.2f jump %.2f  (fine-step blocks %d / %d)\n", t < 0 ? "ALL" : full.macro[(size_t) k].targets[(size_t) t].param.toRawUTF8(), edgeRoughness (rp, line, 0.5, 5.5), edgeRoughness (jp, line, 3.0, 3.3), q->fineBlocks.load(), qj->fineBlocks.load());
        }
        r = 0;
    }
    else if (mode == "fine")         // fine <preset> <macro>: the ramp rendered with 512- and 32-sample host blocks (no reseeding): roughness of both
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1; const auto line = leadLine (8.0);
        auto go = [&] (int bsz) { auto q = makeProc (pr); q->setParam ("arp", 0.0f); return renderAuto (*q, line, 7.0, [&] (double tt) { q->setParam ("mac" + juce::String (k + 1), (float) juce::jlimit (0.0, 1.0, (tt - 0.5) / 5.0)); }, bsz, false); };
        auto a = go (512), b = go (32);
        printf ("ramp 512: roughness x%.2f   ramp 32: x%.2f   max diff %.4g (peak %.3f)\n", edgeRoughness (a, line, 0.5, 5.5), edgeRoughness (b, line, 0.5, 5.5), maxDiff (a, b), peakOf (a));
        printf ("block-size HF error: ramp %.1f dB, parked %.1f dB, jumps %.1f dB (re the signal, > 3 kHz)\n", blockError (pr, k, [] (double t) { return (float) juce::jlimit (0.0, 1.0, (t - 0.5) / 5.0); }, line, 0.5, 5.5),
                blockError (pr, k, [] (double) { return 0.5f; }, line, 0.5, 5.5), blockError (pr, k, [] (double t) { return ((int) std::floor (t / 0.4)) % 2 ? 1.0f : 0.0f; }, line, 0.5, 5.5));
        r = 0;
    }
    else if (mode == "macro")        // macro <preset> <k> [token ...]: centroid / width / LUFS change of macro k 0 -> 1 on the lead line, with extra mc tokens applied
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1; const auto line = leadLine (8.0);
        auto base = makeProc (pr); auto m = base->macros.getMap();
        for (int t = 3; t < args.size(); ++t) { const int eq = args[t].indexOfChar ('='); if (eq > 0) m.parseToken (args[t].substring (0, eq), args[t].substring (eq + 1)); }
        auto at = [&] (float v) { auto q = makeProc (pr); q->macros.setMap (m); q->setParam ("mac" + juce::String (k + 1), v); settle (*q); return render (*q, line, 8.0); };
        auto a = at (0.0f), b = at (1.0f);
        const auto fa = features (a, 6.4, 6.5, 7.5), fb = features (b, 6.4, 6.5, 7.5);
        printf ("macro %d: centroid %.0f -> %.0f Hz (%+.2f oct), width %+.1f dB, LUFS %+.1f, tail %+.1f dB\n", k + 1, fa.centroid, fb.centroid, std::log2 (fb.centroid / fa.centroid), fb.width - fa.width, fb.lufs - fa.lufs, (fb.tail - fb.body) - (fa.tail - fa.body));
        r = 0;
    }
    else if (mode == "finecheck")    // finecheck <preset>: the lead line with fine steps forced (SIZE wobbling by 1e-4) vs plain: level / difference
    {
        const int pr = progOf (args[1]); const auto line = leadLine (8.0);
        auto a = makeProc (pr); auto ra = renderAuto (*a, line, 8.0, {}, BS, false);
        auto b = makeProc (pr); int k = 0; auto rb = renderAuto (*b, line, 8.0, [&] (double) { b->setParam ("mac4", (k++ % 2) ? 1.0e-4f : 0.0f); }, BS, false);
        printf ("plain %.2f LUFS, fine steps %.2f LUFS (%d fine blocks), max diff %.4g\n", lufs (ra, 0, (int) (6.4 * SR)), lufs (rb, 0, (int) (6.4 * SR)), b->fineBlocks.load(), maxDiff (ra, rb));
        r = 0;
    }
    else if (mode == "at")           // at <preset> <k> <v...>: lead-line LUFS / centroid with macro k parked at each value
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1; const auto line = leadLine (8.0);
        for (int t = 3; t < args.size(); ++t)
        {
            auto q = makeProc (pr); q->setParam ("mac" + juce::String (k + 1), args[t].getFloatValue()); settle (*q);
            auto b = renderAuto (*q, line, 8.0, {}, BS, false); const auto f = features (b, 6.4, 6.5, 7.5);
            printf ("macro %d = %s: %.2f LUFS, centroid %.0f Hz, width %.1f dB\n", k + 1, args[t].toRawUTF8(), f.lufs, f.centroid, f.width);
        }
        r = 0;
    }
    else if (mode == "atone")        // atone <preset> <k> <v>: lead-line LUFS at macro k = v with only one target of it live at a time
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1; const float v = args[3].getFloatValue(); const auto line = leadLine (8.0);
        auto base = makeProc (pr); const auto full = base->macros.getMap();
        for (int t = -1; t < (int) full.macro[(size_t) k].targets.size(); ++t)
        {
            gpro::MacroMap m = full; if (t >= 0) m.macro[(size_t) k].targets = { full.macro[(size_t) k].targets[(size_t) t] }; else m.macro[(size_t) k].targets.clear();
            auto q = makeProc (pr); q->macros.setMap (m); q->setParam ("mac" + juce::String (k + 1), v); settle (*q);
            auto b = renderAuto (*q, line, 8.0, {}, BS, false);
            printf ("%-10s %.2f LUFS\n", t < 0 ? "none" : full.macro[(size_t) k].targets[(size_t) t].param.toRawUTF8(), lufs (b, 0, (int) (6.4 * SR)));
        }
        r = 0;
    }
    else if (mode == "clicks")       // clicks <preset> <macro>: edge click level parked at 0 / 1 / ramp / jump with one target live at a time
    {
        const int pr = progOf (args[1]), k = args[2].getIntValue() - 1;
        auto base = makeProc (pr); const auto full = base->macros.getMap(); const auto line = leadLine (8.0);
        const int bsz = args.size() > 3 ? args[3].getIntValue() : BS;
        for (int t = -1; t < (int) full.macro[(size_t) k].targets.size(); ++t)
        {
            gpro::MacroMap m = full;
            if (t >= 0) m.macro[(size_t) k].targets = { full.macro[(size_t) k].targets[(size_t) t] };
            auto go = [&] (std::function<float (double)> f) { auto q = makeProc (pr); q->setParam ("arp", 0.0f); q->macros.setMap (m); return renderAuto (*q, line, 7.0, [&] (double tt) { q->setParam ("mac" + juce::String (k + 1), f (tt)); }, bsz); };
            auto p0 = go ([] (double) { return 0.0f; }), p1 = go ([] (double) { return 1.0f; });
            auto rp = go ([] (double tt) { return (float) juce::jlimit (0.0, 1.0, (tt - 0.5) / 5.0); }), jp = go ([] (double tt) { return tt < 3.0 ? 0.0f : 1.0f; });
            printf ("%-12s parked0 %6.0f parked1 %6.0f ramp %6.0f jump %6.0f\n", t < 0 ? "ALL" : full.macro[(size_t) k].targets[(size_t) t].param.toRawUTF8(),
                    edgeClickDb (p0, line, 0.5, 6.0), edgeClickDb (p1, line, 0.5, 6.0), edgeClickDb (rp, line, 0.5, 5.5), edgeClickDb (jp, line, 3.0, 3.4));
        }
        r = 0;
    }
    else if (mode == "edge")         // edge <preset> [bs]: static edge click level of a plain render
    {
        const auto line = leadLine (8.0); const int bsz = args.size() > 2 ? args[2].getIntValue() : BS;
        auto q = makeProc (progOf (args[1])); q->setParam ("arp", 0.0f); auto b = renderAuto (*q, line, 7.0, {}, bsz);
        printf ("%s bs %d: edge clicks %.0f dB, roughness x%.2f\n", args[1].toRawUTF8(), bsz, edgeClickDb (b, line, 0.5, 6.0), edgeRoughness (b, line, 0.5, 6.0));
        r = 0;
    }
    else if (mode == "alias")        // alias <preset>: aliasing at C6 / C7 with each nonlinear stage switched off in turn
    {
        const int pr = progOf (args[1]);
        printf ("as is: C6 %.1f C7 %.1f\n", aliasingDb (pr, 84), aliasingDb (pr, 96));
        for (auto* id : { "ch_amt", "f_drive", "drive", "res", "o2_on", "xamt", "sub_lvl", "o1_uni", "o1_pos" })
            printf ("%-8s=0: C6 %.1f C7 %.1f\n", id, aliasingDb (pr, 84, { { id, 0.0f } }), aliasingDb (pr, 96, { { id, 0.0f } }));
        printf ("cutoff=1 : C6 %.1f C7 %.1f\n", aliasingDb (pr, 84, { { "cutoff", 1.0f } }), aliasingDb (pr, 96, { { "cutoff", 1.0f } }));
        r = 0;
    }
    else if (mode == "sizegain")     // sizegain <out>: measured SIZE level compensation (mc4:gain) so SIZE 0 -> 1 grows by ~+1.5 LU at most
    {
        juce::StringArray lines; const auto line = leadLine (8.0);
        for (int pr : leadPrograms())
        {
            auto base = makeProc (pr); auto m = base->macros.getMap();
            auto& tg = m.macro[3].targets; tg.erase (std::remove_if (tg.begin(), tg.end(), [] (auto& t) { return t.param == "gain"; }), tg.end());
            auto dL = [&] (float x)
            {
                auto mm = m; if (x != 0.0f) mm.setTarget (3, "gain", x, 1.0f);
                auto a = makeProc (pr); a->macros.setMap (mm); a->setParam ("mac4", 0.0f); auto ra = render (*a, line, 8.0);
                auto b = makeProc (pr); b->macros.setMap (mm); b->setParam ("mac4", 1.0f); auto rb = render (*b, line, 8.0);
                return lufs (rb, 0, (int) (6.4 * SR)) - lufs (ra, 0, (int) (6.4 * SR));
            };
            const double d0 = dL (0.0f); float x = 0.0f; double d1 = d0;
            if (d0 > 2.0)
            {
                for (int it = 0; it < 3 && std::abs (d1 - 1.5) > 0.3; ++it)
                {
                    const double g = std::pow (10.0, -(d1 - 1.5) / 20.0);
                    x = (float) juce::jlimit (-0.3, 0.0, (0.5 + x) * std::sqrt (g) - 0.5);
                    d1 = dL (x);
                }
            }
            printf ("%-26s SIZE level %+.1f LU -> mc4:gain=%.3f -> %+.1f LU\n", gpro::factoryBank()[(size_t) pr].name.toRawUTF8(), d0, x, d1); fflush (stdout);
            lines.add (gpro::factoryBank()[(size_t) pr].name + "|" + juce::String (x, 3));
        }
        juce::File (args[1]).replaceWithText (lines.joinIntoString ("\n") + "\n");
        r = 0;
    }
    else if (mode == "tail")
    {
        const int pr = progOf (args[1]);
        for (int dry = 0; dry < 2; ++dry) for (float sz : { 0.0f, 1.0f })
        {
            auto q = makeProc (pr); q->setParam ("arp", 0.0f); q->setParam ("mac4", sz); if (dry) { q->rack.applyScene (q->apvts, "dry"); settle (*q); }
            auto rr = render (*q, note (69, 0.05, 1.0) + note (72, 1.05, 1.0), 4.0);
            printf ("%s SIZE %.0f: body %.1f  tail %.1f %.1f %.1f (0.3-1.3 / 1.3-1.9 s)  verb %.2f haunt %.2f\n", dry ? "dry " : "rack", sz, rmsDb (rr, 0, (int) (2.0 * SR)), rmsDb (rr, (int) (2.35 * SR), (int) (3.35 * SR)), rmsDb (rr, (int) (3.35 * SR), (int) (3.95 * SR)), 0.0, q->macros.effective ("verb"), q->macros.effective ("haunt"));
        }
        r = 0;
    }
    else if (mode == "list") { int i = 0; for (auto& f : gpro::factoryBank()) { if (isLead (f)) printf ("%3d %s | %s | %s\n", i, f.cat.toRawUTF8(), f.name.toRawUTF8(), f.scene.toRawUTF8()); ++i; } r = 0; }
    else { printf ("leads modes: check, levels <trims.inc>, report [csv], size, state, threads, demo <dir>, shots <dir>, list\n"); return 2; }
    printf ("\n%d failures\n", failures);
    return r;
}
