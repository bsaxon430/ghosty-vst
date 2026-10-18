#pragma once
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

struct LabMarker { double src = 0, out = 0; };   // seconds from the clip's start (in the source / on the timeline)

struct LabClip {
    int id = 0, source = 0;
    double srcStart = 0, srcEnd = 1;   // seconds in the source
    double start = 0, len = 1;         // beats on the timeline
    float pitch = 0, gainDb = 0; bool reverse = false;
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
        std::vector<LabMarker> p { { 0, 0 } };
        auto m = c.marks; std::sort(m.begin(), m.end(), [](auto& a, auto& b) { return a.src < b.src; });
        for (auto& k : m) if (k.src > p.back().src + 0.005 && k.src < c.srcLen() - 0.005 && k.out > p.back().out + 0.005 && k.out < outSec - 0.005) p.push_back(k);
        p.push_back({ c.srcLen(), outSec });
        return p;
    }
    double outToSrc(const LabClip& c, double outSecIn) const {   // where on the source a timeline spot of the clip comes from
        const auto p = points(c, c.len * beatSec());
        for (size_t i = 1; i < p.size(); ++i) if (outSecIn <= p[i].out) { const double t = (outSecIn - p[i - 1].out) / juce::jmax(1e-6, p[i].out - p[i - 1].out); return p[i - 1].src + t * (p[i].src - p[i - 1].src); }
        return c.srcLen();
    }

    // one clip -> audio at kRate, exactly its timeline length, pitch kept (plus its own transpose)
    std::vector<float> renderClip(const LabClip& c) const {
        auto it = sources.find(c.source); if (it == sources.end()) return {};
        const SampleData& S = *it->second;
        const double outSec = c.len * beatSec();
        const int outN = juce::jmax(1, (int) std::round(outSec * kRate));
        std::vector<float> out((size_t) outN, 0.0f);
        const auto p = points(c, outSec);
        for (size_t k = 1; k < p.size(); ++k) {
            const int o0 = (int) std::round(p[k - 1].out * kRate), o1 = juce::jmin(outN, (int) std::round(p[k].out * kRate));
            double s0 = c.srcStart + p[k - 1].src, s1 = c.srcStart + p[k].src;
            if (c.reverse) { const double a = c.srcEnd - p[k].src, b = c.srcEnd - p[k - 1].src; s0 = a; s1 = b; }
            const int i0 = juce::jlimit(0, S.size(), (int) (s0 * kRate)), i1 = juce::jlimit(i0, S.size(), (int) (s1 * kRate));
            const int inN = i1 - i0, segN = o1 - o0;
            if (inN < 32 || segN < 32) continue;
            std::vector<float> in(S.data.begin() + i0, S.data.begin() + i1);
            if (c.reverse) std::reverse(in.begin(), in.end());
            std::vector<float> seg((size_t) segN, 0.0f);
            if (inN == segN && std::abs(c.pitch) < 0.01f) seg = in;
            else {
                signalsmith::stretch::SignalsmithStretch<float> st; st.presetDefault(1, (float) kRate);
                st.setTransposeSemitones(c.pitch);
                float* ip[1] = { in.data() }; float* op[1] = { seg.data() };
                st.exact(ip, inN, op, segN);
            }
            std::copy(seg.begin(), seg.end(), out.begin() + o0);
        }
        const float g = juce::Decibels::decibelsToGain(c.gainDb);
        const int edge = juce::jmin(outN / 2, (int) (0.003 * kRate));   // 3 ms click guards
        for (int i = 0; i < outN; ++i) { float w = 1; if (i < edge) w = (float) i / edge; if (outN - 1 - i < edge) w = juce::jmin(w, (float) (outN - 1 - i) / edge); out[(size_t) i] *= g * w; }
        return out;
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
        s->rate = kRate; s->name = name; s->buildFrames();
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
        s->buildFrames();
        return s;
    }

    // ---- save / load (inside the DAW project)
    juce::ValueTree toTree() const {
        juce::ValueTree t("LAB"); t.setProperty("bars", bars, nullptr); t.setProperty("next", nextId, nullptr); t.setProperty("current", current, nullptr);
        for (auto& [id, s] : sources) { juce::ValueTree st("SRC"); st.setProperty("id", id, nullptr); st.setProperty("name", s->name, nullptr); st.setProperty("data", s->toBase64(), nullptr); t.appendChild(st, nullptr); }
        for (auto& c : clips) {
            juce::ValueTree ct("CLIP");
            ct.setProperty("id", c.id, nullptr); ct.setProperty("src", c.source, nullptr); ct.setProperty("a", c.srcStart, nullptr); ct.setProperty("b", c.srcEnd, nullptr);
            ct.setProperty("start", c.start, nullptr); ct.setProperty("len", c.len, nullptr); ct.setProperty("pitch", c.pitch, nullptr); ct.setProperty("gain", c.gainDb, nullptr); ct.setProperty("rev", c.reverse, nullptr);
            juce::String m; for (auto& k : c.marks) m << k.src << ":" << k.out << ";";
            ct.setProperty("marks", m, nullptr); t.appendChild(ct, nullptr);
        }
        return t;
    }
    void fromTree(const juce::ValueTree& t) {
        sources.clear(); clips.clear();
        bars = t.getProperty("bars", 4); nextId = t.getProperty("next", 1); current = t.getProperty("current", 0);
        for (auto st : t) {
            if (st.hasType("SRC")) { if (auto s = SampleData::fromBase64(st.getProperty("data").toString(), kRate, st.getProperty("name").toString())) sources[(int) st.getProperty("id")] = s; }
            else if (st.hasType("CLIP")) {
                LabClip c; c.id = st.getProperty("id"); c.source = st.getProperty("src"); c.srcStart = st.getProperty("a"); c.srcEnd = st.getProperty("b");
                c.start = st.getProperty("start"); c.len = st.getProperty("len"); c.pitch = (float) (double) st.getProperty("pitch"); c.gainDb = (float) (double) st.getProperty("gain"); c.reverse = st.getProperty("rev");
                for (auto& part : juce::StringArray::fromTokens(st.getProperty("marks").toString(), ";", "")) if (part.contains(":")) c.marks.push_back({ part.upToFirstOccurrenceOf(":", false, false).getDoubleValue(), part.fromFirstOccurrenceOf(":", false, false).getDoubleValue() });
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
