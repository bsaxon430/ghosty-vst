#pragma once
#include <set>
// SAMPLE LAB (v0.3): a small audio editor inside Poltergeist.
//   Sources  - every audio file you drop in (kept, so keys made from it keep working)
//   Clips    - pieces of a source placed on a bar timeline. Move, trim, stretch to fit, cut, copy, overlap (crossfade)
//   Flex     - lines dropped inside a clip; dragging a line stretches the audio around it (pitch never changes)
//   Render   - the timeline bounced to audio: drag it out to the DAW, play it, or make it the keyboard sample
//   Key map  - highlighted sections dropped onto keys (a kit built from many files)
// Time-stretch / pitch-shift: Signalsmith Stretch 1.4.0 (MIT), the offline `exact()` call.
#include "Sample.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <juce_data_structures/juce_data_structures.h>

namespace gs {

// ---- one-pass time-stretch + pitch (Signalsmith Stretch, streaming API) --------------------------------------
// The input is cut into consecutive segments; each is stretched to its own output length and transposed by its own
// pitch. One stretcher runs through all of them, so the joins are crossfaded by its overlap-add (no clicks).
// Any length works (the input is padded with silence, so even a 20 ms slice renders).
struct WarpSeg { int inLen = 0, outLen = 0; float pitch = 0; };
struct WarpOpts { bool formantKeep = true; float crunch = 0; float baseHz = 0; };
inline std::vector<float> warpRender(const float* x, int n, const std::vector<WarpSeg>& segs, double rate, const WarpOpts& o) {
    int T = 0; for (auto& s : segs) T += juce::jmax(0, s.outLen);
    std::vector<float> res((size_t) juce::jmax(0, T), 0.0f);
    if (T <= 0 || segs.empty() || n <= 0) return res;
    signalsmith::stretch::SignalsmithStretch<float> st(20261021L);   // fixed seed: the same edit renders the same audio
    const float c = juce::jlimit(0.0f, 1.0f, o.crunch);   // CRUNCH: smaller FFT + coarser hop = the gritty, smeared shifter sound
    const int block = juce::jmax(128, (int) (rate * (0.12 - 0.106 * c))), interval = juce::jmax(32, (int) (block * (0.25 + 0.25 * c)));
    st.configure(1, block, interval);
    if (o.formantKeep) { st.setFormantFactor(1.0f, true); st.setFormantBase(o.baseHz > 0 ? (float) (o.baseHz / rate) : 0.0f); }
    else st.setFormantFactor(1.0f, false);
    const int Lin = st.inputLatency(), Lout = st.outputLatency(), P = Lin + Lout + block;
    std::vector<float> X((size_t) (P + n + 2 * P), 0.0f);
    std::copy(x, x + n, X.begin() + P);
    // output position (padded) -> input position (padded) and pitch
    std::vector<double> o0, i0; double oc = 0, ic = 0;
    for (auto& s : segs) { o0.push_back(oc); i0.push_back(ic); oc += juce::jmax(0, s.outLen); ic += juce::jmax(0, s.inLen); }
    auto seg = [&](double t) { int k = (int) (std::upper_bound(o0.begin(), o0.end(), t) - o0.begin()) - 1; return juce::jlimit(0, (int) segs.size() - 1, k); };
    auto mapIn = [&](double t) {
        if (t < P) return t;
        const double u = t - P;
        if (u >= T) return P + ic + (u - T);
        const int k = seg(u); const auto& s = segs[(size_t) k];
        return P + i0[(size_t) k] + (u - o0[(size_t) k]) / juce::jmax(1, s.outLen) * juce::jmax(0, s.inLen);
    };
    auto pitchAt = [&](double t) { return segs[(size_t) seg(juce::jlimit(0.0, (double) T - 1, t - P))].pitch; };
    const int seekLen = Lin + Lout;
    { float* ip[1] = { X.data() }; st.outputSeek(ip, seekLen); }
    const int To = P + T + P;
    std::vector<float> Y((size_t) To, 0.0f);
    int fed = seekLen; const int C = 64; float lastPitch = 1e9f;
    for (int j0 = 0; j0 < To; j0 += C) {
        const int j1 = juce::jmin(To, j0 + C);
        const int target = juce::jlimit(fed, (int) X.size(), (int) std::lround(mapIn(j1 + Lout) + Lin));
        const float pz = pitchAt(j0 + Lout);
        if (std::abs(pz - lastPitch) > 1e-6f) { st.setTransposeSemitones(pz, (float) (8000.0 / rate)); lastPitch = pz; }
        float* ip[1] = { X.data() + fed }; float* op[1] = { Y.data() + j0 };
        st.process(ip, target - fed, op, j1 - j0);
        fed = target;
    }
    std::copy(Y.begin() + P, Y.begin() + P + T, res.begin());
    if (c > 0.01f) {   // a little bit-crush + sample-and-hold aliasing on top
        const float q = std::exp2(15.0f - 9.0f * c); const int hold = 1 + (int) (c * 4.0f);
        float h = 0;
        for (int i = 0; i < T; ++i) { if (i % hold == 0) h = std::round(res[(size_t) i] * q) / q; res[(size_t) i] = res[(size_t) i] * (1 - c) + h * c; }
    }
    for (auto& v : res) if (!std::isfinite(v)) v = 0.0f;
    return res;
}

struct LabMarker { double src = 0, out = 0; float pitch = 0; };   // pitch: semitones for the section that starts at this line   // seconds from the clip's start (in the source / on the timeline)

struct LabClip {
    int id = 0, source = 0;
    double srcStart = 0, srcEnd = 1;   // seconds in the source
    double start = 0, len = 1;         // beats on the timeline
    float pitch = 0, gainDb = 0; bool reverse = false;
    float headPitch = 0; bool formant = true;   // pitch of the section before the first flex line; keep formants when pitching
    std::vector<LabMarker> marks;
    double srcLen() const { return juce::jmax(0.001, srcEnd - srcStart); }
};

struct Lab {
    static constexpr double kRate = 44100.0;
    std::map<int, std::shared_ptr<SampleData>> sources;   // id -> audio (resampled to kRate when added)
    std::vector<LabClip> clips;
    int bars = 4, nextId = 1, current = 0;                 // current = source shown in the strip
    double bpm = 120;

    double beatsTotal() const { return bars * 4.0; }
    double beatSec() const { return 60.0 / bpm; }

    int addSource(std::shared_ptr<SampleData> s) {
        if (!s) return 0;
        if (std::abs(s->rate - kRate) > 1.0) {   // one rate for everything so stretching never shifts pitch
            auto r = std::make_shared<SampleData>(); r->name = s->name; r->rate = kRate;
            const double ratio = s->rate / kRate; const int n = (int) (s->size() / ratio);
            r->data.resize((size_t) n); for (int i = 0; i < n; ++i) r->data[(size_t) i] = s->at(i * ratio);
            r->buildFrames(); s = r;
        }
        const int id = nextId++; sources[id] = s; current = id; return id;
    }
    LabClip* clip(int id) { for (auto& c : clips) if (c.id == id) return &c; return nullptr; }

    // a section of a source placed at `beat`, at its natural speed (length in beats from the tempo)
    int addClip(int src, double a, double b, double beat) {
        LabClip c; c.id = nextId++; c.source = src; c.srcStart = juce::jmin(a, b); c.srcEnd = juce::jmax(a, b);
        c.start = beat; c.len = c.srcLen() / beatSec(); clips.push_back(c); return c.id;
    }

    // piecewise map: (src seconds, out seconds) points from clip start to end
    static std::vector<LabMarker> points(const LabClip& c, double outSec) {
        std::vector<LabMarker> p { { 0, 0, c.headPitch } };
        auto m = c.marks; std::sort(m.begin(), m.end(), [](auto& a, auto& b) { return a.src < b.src; });
        for (auto& k : m) if (k.src > p.back().src + 0.005 && k.src < c.srcLen() - 0.005 && k.out > p.back().out + 0.005 && k.out < outSec - 0.005) p.push_back(k);
        p.push_back({ c.srcLen(), outSec, 0.0f });
        return p;
    }
    double outToSrc(const LabClip& c, double outSecIn) const {   // where on the source a timeline spot of the clip comes from
        const auto p = points(c, c.len * beatSec());
        for (size_t i = 1; i < p.size(); ++i) if (outSecIn <= p[i].out) { const double t = (outSecIn - p[i - 1].out) / juce::jmax(1e-6, p[i].out - p[i - 1].out); return p[i - 1].src + t * (p[i].src - p[i - 1].src); }
        return c.srcLen();
    }

    // one clip -> audio at kRate, exactly its timeline length. One continuous Signalsmith pass: the flex map retimes,
    // each section between two lines can carry its own pitch (crossfaded by the stretcher's overlap-add, no clicks).
    std::vector<float> renderClip(const LabClip& c) const {
        auto it = sources.find(c.source); if (it == sources.end()) return {};
        const SampleData& S = *it->second;
        const double outSec = c.len * beatSec();
        const int outN = juce::jmax(1, (int) std::round(outSec * kRate));
        const int i0 = juce::jlimit(0, S.size(), (int) (c.srcStart * kRate)), i1 = juce::jlimit(i0, S.size(), (int) (c.srcEnd * kRate));
        std::vector<float> in(S.data.begin() + i0, S.data.begin() + i1);
        if (c.reverse) std::reverse(in.begin(), in.end());
        const auto p = points(c, outSec);
        std::vector<WarpSeg> segs; bool plain = true;
        for (size_t k = 1; k < p.size(); ++k) {
            WarpSeg sg; sg.inLen = (int) std::round(p[k].src * kRate) - (int) std::round(p[k - 1].src * kRate);
            sg.outLen = (k + 1 == p.size() ? outN : (int) std::round(p[k].out * kRate)) - (int) std::round(p[k - 1].out * kRate);
            sg.pitch = c.pitch + p[k - 1].pitch;
            if (sg.inLen != sg.outLen || std::abs(sg.pitch) > 0.001f) plain = false;
            segs.push_back(sg);
        }
        std::vector<float> out;
        if (plain) { out = in; out.resize((size_t) outN, 0.0f); }
        else { WarpOpts o; o.formantKeep = c.formant; out = warpRender(in.data(), (int) in.size(), segs, kRate, o); out.resize((size_t) outN, 0.0f); }
        const float g = juce::Decibels::decibelsToGain(c.gainDb);
        const int edge = juce::jmin(outN / 2, (int) (0.003 * kRate));   // 3 ms click guards
        for (int i = 0; i < outN; ++i) { float w = 1; if (i < edge) w = (float) i / edge; if (outN - 1 - i < edge) w = juce::jmin(w, (float) (outN - 1 - i) / edge); out[(size_t) i] *= g * w; }
        return out;
    }
    // sections between flex lines (in time order): pitch of section k (0 = before the first line)
    static int sectionCount(const LabClip& c, double outSec) { return (int) points(c, outSec).size() - 1; }
    int sectionAt(const LabClip& c, double outSecIn) const {
        const auto p = points(c, c.len * beatSec());
        for (size_t k = 1; k < p.size(); ++k) if (outSecIn < p[k].out) return (int) k - 1;
        return (int) p.size() - 2;
    }
    std::pair<double, double> sectionSpan(const LabClip& c, int k) const {   // out seconds
        const auto p = points(c, c.len * beatSec()); k = juce::jlimit(0, (int) p.size() - 2, k); return { p[(size_t) k].out, p[(size_t) k + 1].out };
    }
    float* sectionPitch(LabClip& c, int k) const {
        const auto p = points(c, c.len * beatSec());
        if (k <= 0 || k >= (int) p.size() - 1) return k == 0 ? &c.headPitch : nullptr;
        for (auto& m : c.marks) if (std::abs(m.src - p[(size_t) k].src) < 1e-9) return &m.pitch;
        return nullptr;
    }

    // the whole timeline -> one sample. Overlapping clips crossfade (equal power) across the overlap.
    std::shared_ptr<SampleData> render(const juce::String& name = "Sample Lab") const {
        const int total = (int) std::round(beatsTotal() * beatSec() * kRate);
        juce::AudioBuffer<float> buf(1, juce::jmax(1, total)); buf.clear();
        auto sorted = clips; std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.start < b.start; });
        for (size_t k = 0; k < sorted.size(); ++k) {
            const auto& c = sorted[k];
            auto audio = renderClip(c); const int n = (int) audio.size();
            const int at = (int) std::round(c.start * beatSec() * kRate);
            int fadeIn = 0, fadeOut = 0;
            for (size_t j = 0; j < sorted.size(); ++j) {
                if (j == k) continue;
                const double a0 = sorted[j].start, a1 = a0 + sorted[j].len, b0 = c.start, b1 = c.start + c.len;
                if (a0 < b0 && a1 > b0) fadeIn = juce::jmax(fadeIn, (int) ((juce::jmin(a1, b1) - b0) * beatSec() * kRate));
                if (a0 > b0 && a0 < b1) fadeOut = juce::jmax(fadeOut, (int) ((b1 - a0) * beatSec() * kRate));
            }
            for (int i = 0; i < n; ++i) {
                const int t = at + i; if (t < 0 || t >= total) continue;
                float w = 1;
                if (fadeIn > 0 && i < fadeIn) w *= std::sin(juce::MathConstants<float>::halfPi * i / fadeIn);
                if (fadeOut > 0 && n - i <= fadeOut) w *= std::sin(juce::MathConstants<float>::halfPi * (n - i) / fadeOut);
                buf.addSample(0, t, audio[(size_t) i] * w);
            }
        }
        const float pk = buf.getMagnitude(0, 0, buf.getNumSamples());
        if (pk > 0.95f) buf.applyGain(0.95f / pk);   // overlaps can stack up: keep the bounce clean
        auto s = std::make_shared<SampleData>();
        s->data.assign(buf.getReadPointer(0), buf.getReadPointer(0) + buf.getNumSamples());
        s->rate = kRate; s->name = name; s->made = true; s->buildFrames();   // a render of the edit: made by the plugin
        return s;
    }

    // a highlighted section of a source as its own sample (for keys / the keyboard sample)
    std::shared_ptr<SampleData> section(int src, double a, double b, const juce::String& name) const {
        auto it = sources.find(src); if (it == sources.end()) return nullptr;
        const auto& S = *it->second;
        const int i0 = juce::jlimit(0, S.size(), (int) (juce::jmin(a, b) * kRate)), i1 = juce::jlimit(i0, S.size(), (int) (juce::jmax(a, b) * kRate));
        if (i1 - i0 < 64) return nullptr;
        auto s = std::make_shared<SampleData>(); s->rate = kRate; s->name = name;
        s->data.assign(S.data.begin() + i0, S.data.begin() + i1);
        const int edge = juce::jmin((i1 - i0) / 2, 132);
        for (int i = 0; i < edge; ++i) { s->data[(size_t) i] *= (float) i / edge; s->data[s->data.size() - 1 - (size_t) i] *= (float) i / edge; }
        s->made = S.made; s->buildFrames();
        return s;
    }

    // ---- save / load (inside the DAW project)
    // keepMade false (demo): recordings the plugin made (s->made) are left out, with the pieces cut from them; *leftOut counts them
    juce::ValueTree toTree(bool keepMade = true, int* leftOut = nullptr) const {
        juce::ValueTree t("LAB"); t.setProperty("bars", bars, nullptr); t.setProperty("next", nextId, nullptr); t.setProperty("current", current, nullptr);
        std::set<int> gone;
        for (auto& [id, s] : sources) {
            if (s->made && !keepMade) { gone.insert(id); if (leftOut) ++*leftOut; continue; }
            juce::ValueTree st("SRC"); st.setProperty("id", id, nullptr); st.setProperty("name", s->name, nullptr); st.setProperty("data", s->toBase64(), nullptr);
            if (s->made) st.setProperty("made", true, nullptr);
            t.appendChild(st, nullptr);
        }
        for (auto& c : clips) {
            if (gone.count(c.source)) continue;
            juce::ValueTree ct("CLIP");
            ct.setProperty("id", c.id, nullptr); ct.setProperty("src", c.source, nullptr); ct.setProperty("a", c.srcStart, nullptr); ct.setProperty("b", c.srcEnd, nullptr);
            ct.setProperty("start", c.start, nullptr); ct.setProperty("len", c.len, nullptr); ct.setProperty("pitch", c.pitch, nullptr); ct.setProperty("gain", c.gainDb, nullptr); ct.setProperty("rev", c.reverse, nullptr);
            juce::String m; for (auto& k : c.marks) m << k.src << ":" << k.out << ":" << k.pitch << ";";
            ct.setProperty("hp", c.headPitch, nullptr); ct.setProperty("fmt", c.formant, nullptr);
            ct.setProperty("marks", m, nullptr); t.appendChild(ct, nullptr);
        }
        return t;
    }
    void fromTree(const juce::ValueTree& t) {
        sources.clear(); clips.clear();
        bars = t.getProperty("bars", 4); nextId = t.getProperty("next", 1); current = t.getProperty("current", 0);
        for (auto st : t) {
            if (st.hasType("SRC")) { if (auto s = SampleData::fromBase64(st.getProperty("data").toString(), kRate, st.getProperty("name").toString())) { SampleData::readMade(s.get(), st); sources[(int) st.getProperty("id")] = s; } }
            else if (st.hasType("CLIP")) {
                LabClip c; c.id = st.getProperty("id"); c.source = st.getProperty("src"); c.srcStart = st.getProperty("a"); c.srcEnd = st.getProperty("b");
                c.start = st.getProperty("start"); c.len = st.getProperty("len"); c.pitch = (float) (double) st.getProperty("pitch"); c.gainDb = (float) (double) st.getProperty("gain"); c.reverse = st.getProperty("rev");
                c.headPitch = (float) (double) st.getProperty("hp", 0.0); c.formant = st.getProperty("fmt", true);
                for (auto& part : juce::StringArray::fromTokens(st.getProperty("marks").toString(), ";", "")) if (part.contains(":")) {
                    auto f = juce::StringArray::fromTokens(part, ":", ""); c.marks.push_back({ f[0].getDoubleValue(), f[1].getDoubleValue(), f.size() > 2 ? (float) f[2].getDoubleValue() : 0.0f }); }
                clips.push_back(c);
            }
        }
    }
};

// Keys built from sections (a kit across files). Raw pointers go to the audio thread; the processor keeps every
// SampleData alive for the whole session so the audio thread never frees memory.
struct KeyMap { const SampleData* keys[128] {}; };

// Pitch a sample without changing its length (for "keep tempo" on the keyboard sample): -24..+24 semitones
inline std::vector<std::vector<float>> pitchedSet(const SampleData& s, std::atomic<bool>& cancel) {
    std::vector<std::vector<float>> out(49);
    const int n = juce::jmin(s.size(), (int) (s.rate * 12.0));   // up to 12 s
    if (n < 64) return {};
    std::vector<float> in(s.data.begin(), s.data.begin() + n);
    for (int k = 0; k < 49; ++k) {
        if (cancel.load()) return {};
        if (k == 24) { out[24] = in; continue; }
        out[(size_t) k].assign((size_t) n, 0.0f);
        signalsmith::stretch::SignalsmithStretch<float> st; st.presetDefault(1, (float) s.rate);
        st.setTransposeSemitones((float) (k - 24));
        float* ip[1] = { in.data() }; float* op[1] = { out[(size_t) k].data() };
        st.exact(ip, n, op, n);
    }
    return out;
}

} // namespace gs
