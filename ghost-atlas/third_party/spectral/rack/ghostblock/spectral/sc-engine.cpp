// SPDX-License-Identifier: MPL-2.0
/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: hosting Spectral Corruptor's modules in a four-slot rack.
 *
 * This file is under the Mozilla Public License 2.0 because parts of it are adapted from
 *   Spectral Corruptor, Source/dsp/SpectralProcessor.cpp, Copyright (c) 2026 Dirty Octopus (MPL-2.0),
 *   commit 81d39c709f01ff609d4ce6435f3205f2e32f9b3a (vendored unmodified in libs/ghostblock-spectral/spectral-corruptor).
 * Adapted parts (each marked "[SC]" below): processSlot, processSliceChain, recomputeSplitWeights, configurePartial,
 * the module preparation order of syncSliceModules, and the order of work in process(). What changed, and why:
 *   - one chain of four slots instead of upstream's four racks x 64 slices, no macros / modulation / oversampling;
 *   - parameters arrive as plain numbers (see sc-shadow.h) instead of a juce::ValueTree read on the audio thread;
 *   - Partial effects run upstream's PartialProcessor::processSlice over every slice of the chain; effects around
 *     them work on the summed spectrum (upstream clones them per slice);
 *   - nothing here allocates or locks while processing.
 * The modules themselves, the STFT (FFTEngine), PartialProcessor, FrequencySplit and DryWetMixer are upstream's
 * files, compiled as they are. Modifications Copyright (c) 2026 GhostBlock.
 */
#include "ghostblock/spectral/sc-shadow.h"
#include "ghostblock/spectral/sc-engine.h"
#include <cmath>
#include <functional>

namespace ghostblock::p2::spectral::detail
{
namespace sc = scrr::dsp;

// ===================================================================================================== catalogue
namespace
{
struct Folder
{
    const char *name;
    std::vector<const char *> ids;
};
// Spectral Corruptor's eight folders (what its effectFolder() answers), in a fixed order of ours. APPEND ONLY:
// the host automates a slot's effect by its position in this list.
const std::vector<Folder> &folders()
{
    static const std::vector<Folder> f = {
        {"DESTRUCTION", {"BinShuffle", "RandomBinDeath", "SpectralDropout", "SpectralPixelation", "SpectralJPEG", "SpectralClipper"}},
        {"FREQUENCY", {"FrequencyShift", "FrequencyWarp", "FrequencyDrift", "FrequencyRemap", "SpectralMirror", "BinTeleport"}},
        {"PHASE", {"PhaseCorruption", "PhaseNoise", "ComplexRotation", "MagnitudePhaseSwap", "SpectralDispersion"}},
        {"TEXTURE / TIME", {"BinHold", "FrameHold", "TemporalSmear", "TimeDesync", "SpectralBloom"}},
        {"FILTER / UTILITY", {"SpectralFilter", "SpectralComb", "SpectralEqualizer"}},
        {"HARMONICS", {"SpectralContrast", "HarmonicSculpt", "HarmonicMatch"}},
        {"PARTIAL", {"PartialStretcher", "PartialStiffString", "PartialCentroid", "PartialPGrid", "PartialFGrid"}},
        {"GENERIC", {"Reverb", "Delay", "Compressor", "Distortion", "Utility"}},
    };
    return f;
}
ParamDesc fromSpec(const sc::ParamSpec &p)
{
    ParamDesc d;
    d.id = p.id.toStdString();
    d.name = p.label.toStdString();
    d.kind = p.type == sc::ParamSpec::Float ? Kind::Float : p.type == sc::ParamSpec::Int ? Kind::Int : p.type == sc::ParamSpec::Bool ? Kind::Bool : Kind::Choice;
    d.min = p.minVal;
    d.max = p.maxVal;
    d.def = p.defaultVal;
    d.step = p.step;
    d.unit = p.unit.toStdString();
    d.logScale = p.logScale && p.minVal > 0;
    for (auto &c : p.choices) d.choices.push_back(c.toStdString());
    return d;
}
ParamDesc ours(const char *id, const char *name, Kind k, float mn, float mx, float df, float step, const char *unit, std::vector<std::string> ch = {})
{
    ParamDesc d;
    d.id = id;
    d.name = name;
    d.kind = k;
    d.min = mn;
    d.max = mx;
    d.def = df;
    d.step = step;
    d.unit = unit;
    d.choices = std::move(ch);
    return d;
}
} // namespace

void buildCatalogue(std::vector<TypeDesc> &types, std::vector<std::string> &categories)
{
    types.clear();
    categories.clear();
    TypeDesc none;
    none.name = "(empty)";
    types.push_back(none);
    for (auto &f : folders())
    {
        categories.emplace_back(f.name);
        for (auto *id : f.ids)
        {
            const auto *spec = sc::findModuleSpec(id);
            if (!spec) continue; // (cannot happen with the pinned upstream)
            TypeDesc t;
            t.index = (int)types.size();
            t.id = id;
            t.name = spec->displayName.toStdString();
            t.category = f.name;
            t.source = "Spectral Corruptor";
            t.domain = sc::isPartialEffect(id) ? Domain::Partial : sc::isGenericEffect(id) ? Domain::Time : Domain::Spectral;
            const std::string sid = id;
            if (sid == "SpectralEqualizer")
            {
                // upstream lists a band's five parameters together; we list all gains first so the 8 host knobs are the 8 gains
                for (const char *suffix : {"Gain", "Freq", "Q", "Type", "Enabled"})
                    for (int band = 0; band < sc::spectralEqBands; ++band)
                    {
                        const auto pid = sc::spectralEqParameter(band, suffix);
                        for (auto &p : spec->params)
                            if (p.id == pid) t.params.push_back(fromSpec(p));
                    }
                t.liveParams = false; // upstream builds these parameter names as strings on every read: not for the audio thread
            }
            else
                for (auto &p : spec->params) t.params.push_back(fromSpec(p));
            if (sid == "FrequencyRemap")
            {
                // upstream draws the map as a curve of nodes (x = from, y = to, both 0..1 of 20 Hz..limit on a log axis).
                // Here: the same curve with three nodes, their coordinates as parameters (identity by default, as upstream).
                t.params.push_back(ours("curve.lowTo", "Low To", Kind::Float, 0, 100, 0, 0.1f, " %"));
                t.params.push_back(ours("curve.midFrom", "Mid From", Kind::Float, 0.1f, 99.9f, 50, 0.1f, " %"));
                t.params.push_back(ours("curve.midTo", "Mid To", Kind::Float, 0, 100, 50, 0.1f, " %"));
                t.params.push_back(ours("curve.highTo", "High To", Kind::Float, 0, 100, 100, 0.1f, " %"));
                t.params.push_back(ours("curve.bendLow", "Bend Low", Kind::Float, -100, 100, 0, 0.1f, " %"));
                t.params.push_back(ours("curve.bendHigh", "Bend High", Kind::Float, -100, 100, 0, 0.1f, " %"));
                t.params.push_back(ours("remapInvalidPolicy", "Out Of Range", Kind::Choice, 0, 1, 0, 1, "", {"Silence", "Keep Original"}));
            }
            if (t.domain == Domain::Partial)
            {
                // upstream's rack-wide "Frequency Slices" and "Crossfade", here per effect
                t.params.push_back(ours("slices", "Slices", Kind::Int, 2, 64, 16, 1, ""));
                t.params.push_back(ours("crossfade", "Slice Crossfade", Kind::Float, 0, 100, 0, 0.1f, " %"));
            }
            for (size_t i = 0; i < t.params.size(); ++i) t.params[i].hostParam = (int)i < kHostParamsPerSlot;
            jassert((int)t.params.size() <= kMaxParams);
            types.push_back(std::move(t));
        }
    }
}

// ===================================================================================================== objects
struct Core
{
    CoreCfg cfg;
    uint32_t serial{0};
    int bins{0}, hop{0};
    std::unique_ptr<sc::FFTEngine> fft;
    sc::DryWetMixer mixer;
    std::shared_ptr<sc::PartialProcessor::Workspace> partialWorkspace;
    std::vector<std::complex<float>> moduleDry, sliceBuffer, sliceSource;
    std::vector<sc::FrequencySplit::SliceWeights> individualWeights;
    std::array<int, 64> sliceFirst{}, sliceLast{};
    int weightSlices{0};
    float weightCrossfade{-1.f};
    std::function<void(std::complex<float> *, int, float)> onFrame;
    RackIo *io{nullptr};
    int channel{0};
    float work[2][kQuantum]{}, dry[2][kQuantum]{}, keep[2][kQuantum]{};
};

struct Proc
{
    int type{0};
    Domain domain{Domain::None};
    uint32_t serial{0};
    const TypeDesc *desc{nullptr};
    std::unique_ptr<sc::SpectralModule> module;
    RtTree tree;
    int treeIndex[kMaxParams]{};
    // Frequency Remap
    bool remap{false};
    scrr::params::TransferCurve curve;
    int iCurve{-1};
    // Partial
    std::unique_ptr<sc::PartialProcessor> partial;
    std::shared_ptr<sc::PartialProcessor::Workspace> workspace; // (keeps it alive as long as this proc)
    std::vector<sc::PartialProcessor::Stage> stage;
    int iMode{-1}, iShape{-1}, iAnchor{-1}, iAmount{-1}, iTarget{-1}, iPitch{-1}, iReference{-1}, iSlices{-1}, iCrossfade{-1};
    int stageMode{-1}, stageShape{-1};
    int slices{16};
    float crossfade{0.f};
};

int coreLatency(const Core *c) noexcept { return c && c->fft ? c->cfg.fftSize - 1 : 0; }
uint32_t coreSerial(const Core *c) noexcept { return c ? c->serial : 0; }
bool coreHasStft(const Core *c) noexcept { return c && c->fft; }
int procType(const Proc *p) noexcept { return p ? p->type : 0; }
uint32_t procSerial(const Proc *p) noexcept { return p ? p->serial : 0; }

static void runFrame(Core &c, std::complex<float> *spectrum, int bins) noexcept;

Core *makeCore(const CoreCfg &cfg, float chainMix01, uint32_t serial)
{
    auto *c = new Core();
    c->cfg = cfg;
    c->serial = serial;
    if (cfg.stft)
    {
        c->fft = std::make_unique<sc::FFTEngine>();
        const int n = cfg.fftSize;
        // [SC] reconfigure(): hop = round(N / overlap); upstream's exact (per-phase) synthesis normalisation, never the legacy gain
        int hop = juce::jlimit(1, n, (int)std::llround((double)n / juce::jlimit(2.0, 16.0, (double)cfg.overlap)));
        try
        {
            c->fft->configure(n, hop, static_cast<sc::WindowType>(juce::jlimit(0, 8, cfg.window)), false);
        }
        catch (const std::exception &)
        { // a window / hop pair that leaves samples uncovered: fall back to upstream's default
            hop = n / 4;
            c->fft->configure(n, hop, sc::WindowType::Hann, false);
        }
        c->fft->prepare(cfg.sampleRate, kQuantum, 2);
        c->hop = hop;
        c->bins = c->fft->getNumBins();
        c->moduleDry.resize((size_t)c->bins);
        c->sliceBuffer.resize((size_t)c->bins);
        c->sliceSource.resize((size_t)c->bins);
        c->individualWeights.resize((size_t)c->bins);
        c->partialWorkspace = std::make_shared<sc::PartialProcessor::Workspace>();
        c->partialWorkspace->prepare(n, c->fft->getAnalysisWindow());
        c->partialWorkspace->prepareWaveforms(); // (so choosing Triangle / Saw / Square later needs no memory)
    }
    c->onFrame = [c](std::complex<float> *s, int bins, float) { runFrame(*c, s, bins); };
    c->mixer.prepare(cfg.sampleRate, kQuantum, 2);
    c->mixer.setWetDelay(coreLatency(c));
    c->mixer.setMix(chainMix01);
    { // let the mixer's 20 ms glide arrive before the first real sample
        juce::AudioBuffer<float> z(2, kQuantum);
        z.clear();
        for (int i = 0; i < (int)(cfg.sampleRate * 0.03) / kQuantum + 2; ++i) c->mixer.process(z, z, z);
        c->mixer.reset();
    }
    return c;
}
void freeCore(Core *c) { delete c; }
bool coreSpectrum(Core *c, std::vector<float> &m, double &rate, bool input) { return c && c->fft && c->fft->copyLastMagnitudes(m, &rate, input); }
void coreReset(Core *c) noexcept
{
    if (!c) return;
    if (c->fft) c->fft->reset();
    c->mixer.reset();
}

// [SC] configurePartial(): the slot's numbers into upstream's PartialProcessor
static void configurePartial(Proc &p, const float *v) noexcept
{
    auto val = [&](int i, double fallback) { return i >= 0 ? (double)v[i] : fallback; };
    const std::string &id = p.desc->id;
    const double reference = val(p.iReference, 110.0);
    p.partial->setFundamentalHz(reference);
    p.partial->setAnchorMode((int)std::lround(val(p.iAnchor, 0)) == 1 ? sc::PartialProcessor::AnchorMode::EnergyCentroid : sc::PartialProcessor::AnchorMode::BandCentre);
    const double amount = val(p.iAmount, 0.0);
    double pitch = val(p.iPitch, 12.0);
    if (id == "PartialCentroid") pitch = 12.0 * std::log2(std::max(1.0, val(p.iTarget, 1000.0)) / std::max(1.0, reference));
    const int mode = (int)std::lround(val(p.iMode, 0)) == 1 ? 1 : 0;
    const int shape = juce::jlimit(0, 3, (int)std::lround(val(p.iShape, 0)));
    p.slices = juce::jlimit(2, 64, (int)std::lround(val(p.iSlices, 16)));
    p.crossfade = juce::jlimit(0.f, 100.f, (float)val(p.iCrossfade, 0));
    if (mode == p.stageMode && shape == p.stageShape)
    {
        p.partial->updateStageParameters(0, amount, pitch);
        return;
    }
    auto &stage = p.stage[0];
    stage.type = id == "PartialStiffString" ? sc::PartialProcessor::Type::StiffString
                 : id == "PartialCentroid" ? sc::PartialProcessor::Type::Centroid
                 : id == "PartialPGrid" ? sc::PartialProcessor::Type::PGrid
                 : id == "PartialFGrid" ? sc::PartialProcessor::Type::FGrid : sc::PartialProcessor::Type::Stretcher;
    stage.mode = mode == 1 ? sc::PartialProcessor::Mode::Resynth : sc::PartialProcessor::Mode::Scale;
    stage.shape = static_cast<sc::PartialProcessor::Shape>(shape);
    stage.amount = amount;
    stage.pitch = pitch;
    p.stageMode = mode;
    p.stageShape = shape;
    p.partial->setStages(p.stage); // (one stage, no uid: copies in place)
}

void procApply(Proc *pp, const float *v) noexcept
{
    if (!pp) return;
    auto &p = *pp;
    if (p.partial)
    {
        configurePartial(p, v);
        return;
    }
    if (!p.module) return;
    const auto &params = p.desc->params;
    for (size_t i = 0; i < params.size(); ++i)
    {
        const int ti = p.treeIndex[i];
        if (ti < 0) continue;
        switch (params[i].kind)
        {
        case Kind::Float: p.tree.setDouble(ti, (double)v[i]); break;
        case Kind::Bool: p.tree.setBool(ti, v[i] > 0.5f); break;
        default: p.tree.setInt(ti, (int)std::lround(v[i])); break;
        }
    }
    p.module->updateParameters(p.tree);
    if (p.remap)
    {
        auto &pts = p.curve.points;
        const int c = p.iCurve;
        pts[0] = {0.f, v[c] * 0.01f, juce::jlimit(-1.f, 1.f, v[c + 4] * 0.01f), 1};
        pts[1] = {juce::jlimit(0.001f, 0.999f, v[c + 1] * 0.01f), v[c + 2] * 0.01f, juce::jlimit(-1.f, 1.f, v[c + 5] * 0.01f), 2};
        pts[2] = {1.f, v[c + 3] * 0.01f, 0.f, 3};
        scrr::params::TransferCurve::constrainCoordinates(pts.data(), pts.size());
        static_cast<sc::FrequencyRemapModule &>(*p.module).setEffectiveCurve(p.curve); // [SC] as applyMacroValues() does
    }
}
void procReset(Proc *p) noexcept
{
    if (!p) return;
    if (p->module) p->module->reset();
    if (p->partial) p->partial->reset();
}

Proc *makeProc(int type, const Core *core, const float *values)
{
    const auto &all = types();
    if (type <= 0 || type >= (int)all.size()) return nullptr;
    const auto &d = all[(size_t)type];
    if (d.domain != Domain::Time && !coreHasStft(core)) return nullptr; // a spectral effect needs the STFT it is made for
    auto *p = new Proc();
    p->type = type;
    p->desc = &d;
    p->domain = d.domain;
    p->serial = d.domain == Domain::Time ? 0 : core->serial;
    auto find = [&](const char *id) {
        for (size_t i = 0; i < d.params.size(); ++i)
            if (d.params[i].id == id) return (int)i;
        return -1;
    };
    const double sr = core ? core->cfg.sampleRate : 48000.0;
    if (d.domain == Domain::Partial)
    {
        p->iMode = find("mode"); p->iShape = find("shape"); p->iAnchor = find("anchor"); p->iAmount = find("amount");
        p->iTarget = find("target"); p->iPitch = find("pitch"); p->iReference = find("reference");
        p->iSlices = find("slices"); p->iCrossfade = find("crossfade");
        p->module = sc::createModule(d.id); // upstream's marker module (does nothing itself)
        p->workspace = core->partialWorkspace;
        p->partial = std::make_unique<sc::PartialProcessor>();
        p->stage.resize(1);
        p->partial->setStages(p->stage);
        const int frames = (kQuantum + core->hop - 1) / core->hop + 2; // [SC] preparePartial()
        p->partial->prepare(sr, core->cfg.fftSize, core->hop, 2, core->fft->getAnalysisWindow(), frames, p->workspace);
        configurePartial(*p, values);
        return p;
    }
    p->module = sc::createModule(d.id);
    if (!p->module)
    {
        delete p;
        return nullptr;
    }
    p->tree.setProperty("enabled", true, nullptr);
    for (size_t i = 0; i < d.params.size(); ++i)
    {
        const auto &q = d.params[i];
        p->treeIndex[i] = -1;
        if (q.id.rfind("curve.", 0) == 0) continue; // ours, not a module property
        if (q.kind == Kind::Float) p->tree.setProperty(q.id.c_str(), (double)q.def, nullptr);
        else if (q.kind == Kind::Bool) p->tree.setProperty(q.id.c_str(), q.def > 0.5f, nullptr);
        else p->tree.setProperty(q.id.c_str(), (int)q.def, nullptr);
        p->treeIndex[i] = p->tree.indexOf(q.id.c_str());
    }
    if (d.id == "FrequencyRemap")
    {
        p->remap = true;
        p->iCurve = find("curve.lowTo");
        p->curve.points.reserve(scrr::params::TransferCurve::maxPoints);
        p->curve.points = {{0, 0, 0, 1}, {0.5f, 0.5f, 0, 2}, {1, 1, 0, 3}};
        p->curve.nextPointId = 4;
    }
    const int bins = coreHasStft(core) ? core->bins : 1025;
    // [SC] syncSliceModules(): parameters, prepare, calibration, hop, parameters again
    procApply(p, values);
    p->module->prepare(sr, bins);
    if (coreHasStft(core))
    {
        p->module->setSpectrumCalibration(core->fft->getWindowAmplitudeSum());
        p->module->setHopSize(core->hop);
    }
    procApply(p, values);
    return p;
}
void freeProc(Proc *p) { delete p; }

// ===================================================================================================== processing
// [SC] recomputeSplitWeights(), without the four-rack fold
static void slicePlan(Core &c, int slices, float crossfade) noexcept
{
    if (slices == c.weightSlices && crossfade == c.weightCrossfade) return;
    c.weightSlices = slices;
    c.weightCrossfade = crossfade;
    c.sliceFirst.fill(c.bins);
    c.sliceLast.fill(-1);
    const sc::FrequencySplit mapping(c.cfg.sampleRate, slices, crossfade);
    for (int bin = 0; bin < c.bins; ++bin)
    {
        const auto individual = mapping.sliceWeightsForBin(bin, c.bins, c.cfg.sampleRate);
        c.individualWeights[(size_t)bin] = individual;
        auto include = [&](int slice, float weight) {
            if (slice < 0 || weight <= 0) return;
            c.sliceFirst[(size_t)slice] = std::min(c.sliceFirst[(size_t)slice], bin);
            c.sliceLast[(size_t)slice] = std::max(c.sliceLast[(size_t)slice], bin);
        };
        include(individual.first, individual.firstWeight);
        include(individual.second, individual.secondWeight);
    }
}

// [SC] processSlot(): one effect on one spectrum, with the frequency limit and the stage mix (range = everything)
static void processSlot(Core &c, Proc &slot, float mix, std::complex<float> *spectrum, int bins, double sr,
                        sc::PartialProcessor::SliceContext *context = nullptr) noexcept
{
    if (mix <= 0.0f) return;
    const float frequencyLimit = c.io->limitHz;
    const float low = 0.0f, high = 96000.0f;
    std::copy(spectrum, spectrum + bins, c.moduleDry.begin());
    // Keep the out-of-range signal dry, and do not let it feed shifts or
    // other spectral transforms back into the permitted processing band.
    const double binHz = sr / (2.0 * (bins - 1));
    for (int b = 0; b < bins; ++b)
        if ((double)b * binHz > frequencyLimit) spectrum[b] = {};
    const bool remap = slot.remap;
    if (remap) static_cast<sc::FrequencyRemapModule &>(*slot.module).setFrequencyLimit(frequencyLimit);
    if (slot.partial && context) slot.partial->processSlice(spectrum, bins, *context, frequencyLimit, c.channel);
    else slot.module->processSpectrum(spectrum, bins, sr);
    for (int b = 0; b < bins; ++b)
    {
        const float hz = (float)((double)b * sr / (2.0 * (bins - 1)));
        const bool selected = hz <= frequencyLimit && (remap || (hz >= low && hz <= high));
        const auto dry = c.moduleDry[(size_t)b];
        spectrum[b] = selected ? dry + (spectrum[b] - dry) * mix : dry;
        if (!std::isfinite(spectrum[b].real()) || !std::isfinite(spectrum[b].imag())) spectrum[b] = {};
    }
    if (context && (!slot.partial || mix < 1.0f || low > 0.0f || high < frequencyLimit)) context->singleSine = false;
}

// [SC] processSliceChain(): a run of Partial effects, slice by slice (every slice; upstream gives a rack every fourth)
static void processPartialRun(Core &c, const int *run, int count, std::complex<float> *spectrum, int bins, double sr) noexcept
{
    auto &first = *c.io->proc[run[0]];
    const int numSplits = first.slices;
    slicePlan(c, numSplits, first.crossfade);
    std::copy(spectrum, spectrum + bins, c.sliceSource.begin());
    const auto *source = c.sliceSource.data();
    auto *destination = spectrum;
    std::fill_n(destination, bins, std::complex<float>{});
    for (int slice = 0; slice < numSplits; ++slice)
    {
        if (c.sliceFirst[(size_t)slice] > c.sliceLast[(size_t)slice]) continue;
        std::fill(c.sliceBuffer.begin(), c.sliceBuffer.end(), std::complex<float>{});
        for (int b = c.sliceFirst[(size_t)slice]; b <= c.sliceLast[(size_t)slice]; ++b)
        {
            const auto &weight = c.individualWeights[(size_t)b];
            const float amount = weight.first == slice ? weight.firstWeight : (weight.second == slice ? weight.secondWeight : 0.0f);
            c.sliceBuffer[(size_t)b] = source[b] * amount;
        }
        const double centre = 20.0 * std::pow(std::max(40.0, c.cfg.sampleRate * 0.5) / 20.0, ((double)slice + 0.5) / numSplits);
        sc::PartialProcessor::SliceContext context{slice, centre, centre, false};
        for (int k = 0; k < count; ++k)
            processSlot(c, *c.io->proc[run[k]], c.io->gain1[run[k]], c.sliceBuffer.data(), bins, sr, &context);
        for (int b = 0; b < bins; ++b)
        {
            const double real = (double)destination[b].real() + c.sliceBuffer[(size_t)b].real();
            const double imaginary = (double)destination[b].imag() + c.sliceBuffer[(size_t)b].imag();
            destination[b] = {(float)(std::isfinite(real) ? juce::jlimit(-1.0e12, 1.0e12, real) : 0.0),
                              (float)(std::isfinite(imaginary) ? juce::jlimit(-1.0e12, 1.0e12, imaginary) : 0.0)};
        }
    }
}

// one STFT frame of the current channel through the spectral slots, in slot order
static void runFrame(Core &c, std::complex<float> *spectrum, int bins) noexcept
{
    auto &io = *c.io;
    const double sr = c.cfg.sampleRate;
    for (int s = 0; s < kSlots;)
    {
        auto *p = io.proc[s];
        if (!p || p->domain == Domain::Time || io.gain1[s] <= 0.f)
        {
            ++s;
            continue;
        }
        if (p->domain == Domain::Partial)
        { // this Partial effect and the ones right after it that slice the same way share the slices (as upstream's rack)
            int run[kSlots], count = 0;
            run[count++] = s;
            int t = s + 1;
            for (; t < kSlots; ++t)
            {
                auto *q = io.proc[t];
                if (!q || q->domain == Domain::Time || io.gain1[t] <= 0.f) continue;
                if (q->domain != Domain::Partial || q->slices != p->slices || q->crossfade != p->crossfade) break;
                run[count++] = t;
            }
            processPartialRun(c, run, count, spectrum, bins, sr);
            s = t;
        }
        else
        {
            processSlot(c, *p, io.gain1[s], spectrum, bins, sr);
            ++s;
        }
        if (c.channel == 0)
        { // meter: the frame's level after this effect (window-normalised)
            double power = 0;
            for (int b = 1; b < bins - 1; ++b) power += std::norm(spectrum[b]);
            const float lv = (float)(2.0 * std::sqrt(power) / std::max(1.0, c.fft->getWindowAmplitudeSum()));
            const int last = s - 1;
            io.level[last] = std::max(io.level[last] * 0.9f, std::isfinite(lv) ? lv : 0.f);
        }
    }
}

static inline float safe8(float x) noexcept { return std::isfinite(x) ? juce::jlimit(-8.0f, 8.0f, x) : 0.0f; }

// [SC] the order of SpectralProcessor::process(), one rack, no oversampling
void rackProcess(RackIo &io, float *L, float *R, int n) noexcept
{
    auto &c = *io.core;
    c.io = &io;
    float *ch[2] = {L, R};
    for (int k = 0; k < 2; ++k) std::copy(ch[k], ch[k] + n, c.dry[k]); // (upstream's "wetBuffer": the dry input for the mixer)
    if (c.fft)
    {
        for (auto *p : io.proc)
            if (p && p->partial) p->partial->beginBlock();
        for (int k = 0; k < 2; ++k)
        {
            c.channel = k;
            for (auto *p : io.proc)
                if (p && p->domain != Domain::Time && p->module) p->module->beginChannel(k);
            c.fft->process(ch[k], c.work[k], n, k, c.onFrame);
        }
        for (int k = 0; k < 2; ++k) std::copy(c.work[k], c.work[k] + n, ch[k]);
    }
    for (int s = 0; s < kSlots; ++s)
    {
        auto *p = io.proc[s];
        if (!p || p->domain != Domain::Time) continue;
        const float g0 = io.gain0[s], g1 = io.gain1[s];
        if (g0 <= 0.f && g1 <= 0.f) continue;
        const bool whole = g0 >= 1.f && g1 >= 1.f;
        if (!whole)
            for (int k = 0; k < 2; ++k) std::copy(ch[k], ch[k] + n, c.keep[k]);
        juce::AudioBuffer<float> buffer(ch, 2, n); // (refers to the samples; no memory taken)
        p->module->setTempo(io.tempo);
        p->module->processBuffer(buffer, c.cfg.sampleRate);
        float peak = 0;
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < n; ++i)
            {
                if (!whole)
                {
                    const float g = g0 + (g1 - g0) * (float)(i + 1) / (float)n;
                    ch[k][i] = c.keep[k][i] + g * (ch[k][i] - c.keep[k][i]);
                }
                peak = std::max(peak, std::abs(ch[k][i]));
            }
        io.level[s] = std::max(io.level[s] * 0.97f, std::isfinite(peak) ? peak : 0.f);
    }
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < n; ++i) ch[k][i] = safe8(ch[k][i]);
    // the chain's dry / wet (upstream's equal-power mixer; the dry side waits for the STFT)
    c.mixer.setMix(io.chainMix01);
    float *dp[2] = {c.dry[0], c.dry[1]};
    juce::AudioBuffer<float> dry(dp, 2, n), wet(ch, 2, n);
    c.mixer.process(dry, wet, wet);
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < n; ++i) ch[k][i] = safe8(ch[k][i]);
}
} // namespace ghostblock::p2::spectral::detail
