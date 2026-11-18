#pragma once
// GHOST CAPTURE (Ghost Box): a rolling history of the PROCESSED output (exactly the samples the plugin hands back to the host), and
// a shelf of the last 8 captures taken from it. Real processed audio, never a reconstruction.
//
// History: stereo float ring, min (64 s, 64 MB) at the current rate = 16 bars down to 60 BPM in 4/4. It is allocated on the message
// thread (prepareToPlay, or when HISTORY is switched on) and published through an atomic pointer; the audio thread only copies each
// block in (memcpy into the ring, no allocation, no lock) and adds a SEGMENT MARK whenever the song position stops being a straight
// line (transport start / stop, a tempo change, a jump / loop). The marks map ring samples to song time, so a capture of N bars is
// exactly N bars of song time even across tempo changes.
//
// CAPTURE (message thread): the last N bars (2 / 4 / 8 / 16) ending on the latest bar line the output has passed (latency-compensated:
// output sample s carries song time ppq(s) - latency). Transport stopped: the last N bars at the current tempo, ending now. A jump or
// loop inside the window: the last N bars of samples at the current tempo (documented in CAPTURE-SIDECAR.md). The copy is a lock-free
// read: the ring keeps a written-sample counter, the region is copied, then the counter is checked again (the region must not have
// been overwritten meanwhile; a 0.5 s safety margin is kept in front of the write head).
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace boxcapture {

inline const juce::StringArray& barNames() { static juce::StringArray s { "2 BARS", "4 BARS", "8 BARS", "16 BARS" }; return s; }
inline int barsOf (int i) noexcept { return 2 << juce::jlimit (0, 3, i); }
constexpr double kMaxSeconds = 64.0;
constexpr double kMaxBytes = 64.0 * 1024.0 * 1024.0;
constexpr int kShelf = 8;

class History
{
public:
    struct Mark { juce::int64 at = 0; double ppq = 0, spb = 24000; bool playing = false; };
    struct Buf
    {
        std::vector<float> l, r; int size = 0;
        std::vector<Mark> marks;                      // ring of segment marks
        std::atomic<juce::int64> written { 0 };        // samples written since the buffer was made
        std::atomic<juce::int64> markCount { 0 };
        double sr = 48000;
        // audio-thread state: where the song should be at the next sample (a straight-line check)
        double nextPpq = -1.0e9, lastSpb = 0; bool lastPlaying = false;
    };
    static constexpr int kMarks = 4096;

    ~History() { auto* b = buf.exchange (nullptr); delete b; retired.clear(); }

    static int sizeFor (double sr) { return (int) std::min (kMaxSeconds * sr, kMaxBytes / (2.0 * sizeof (float))); }
    // message thread: (re)allocate for this rate (prepareToPlay; audio is stopped) or switch HISTORY on / off
    void prepare (double sr, bool on)
    {
        enabled.store (on);
        auto* cur = buf.load();
        if (! on) { publish (nullptr); return; }
        if (cur != nullptr && std::abs (cur->sr - sr) < 1.0e-9) { cur->written.store (0); cur->markCount.store (0); cur->nextPpq = -1.0e9; return; }   // a restart: empty, keep the memory
        auto nb = std::make_unique<Buf>();
        nb->sr = sr; nb->size = std::max (1, sizeFor (sr));
        nb->l.assign ((size_t) nb->size, 0.0f); nb->r.assign ((size_t) nb->size, 0.0f); nb->marks.resize (kMarks);
        publish (nb.release());
    }
    bool isOn() const { return enabled.load(); }
    double seconds() const { auto* b = buf.load(); return b == nullptr ? 0.0 : (double) std::min<juce::int64> (b->written.load(), b->size) / b->sr; }
    double capacitySeconds() const { auto* b = buf.load(); return b == nullptr ? 0.0 : b->size / b->sr; }
    size_t bytes() const { auto* b = buf.load(); return b == nullptr ? 0 : (size_t) b->size * 2 * sizeof (float); }

    // ---- audio thread: the block the plugin is about to hand back, its song position / tempo
    void write (const float* L, const float* R, int n, double ppq, double spb, bool playing) noexcept
    {
        inProcess.store (1, std::memory_order_release);
        if (auto* b = buf.load (std::memory_order_acquire); b != nullptr && n > 0)
        {
            const juce::int64 w = b->written.load (std::memory_order_relaxed);
            const bool straight = playing == b->lastPlaying && std::abs (spb - b->lastSpb) < 1.0e-9 && (! playing || std::abs (ppq - b->nextPpq) * spb < 0.5);   // (within half a sample)
            if (! straight || b->markCount.load (std::memory_order_relaxed) == 0)
            {
                const auto k = b->markCount.load (std::memory_order_relaxed);
                b->marks[(size_t) (k % kMarks)] = { w, ppq, spb, playing };
                b->markCount.store (k + 1, std::memory_order_release);
            }
            b->lastPlaying = playing; b->lastSpb = spb; b->nextPpq = ppq + (double) n / spb;
            int pos = (int) (w % b->size), done = 0;
            while (done < n)
            {
                const int c = std::min (n - done, b->size - pos);
                std::memcpy (b->l.data() + pos, L + done, (size_t) c * sizeof (float));
                std::memcpy (b->r.data() + pos, (R != nullptr ? R : L) + done, (size_t) c * sizeof (float));
                done += c; pos = 0;
            }
            b->written.store (w + n, std::memory_order_release);
        }
        blocks.fetch_add (1, std::memory_order_acq_rel);
        inProcess.store (0, std::memory_order_release);
    }

    // ---- message thread: the last `bars` bars (see the header). latency = the plugin's reported latency in samples.
    struct Result { bool ok = false; juce::AudioBuffer<float> audio; double sr = 48000, bpm = 120; int bars = 4; bool barAligned = false; juce::String why; };
    Result capture (int bars, double beatsPerBar, double barStartPpq, bool haveBar, int latency) const
    {
        Result res; res.bars = bars;
        auto* b = buf.load();
        if (b == nullptr) { res.why = "HISTORY is off"; return res; }
        res.sr = b->sr;
        const juce::int64 w = b->written.load (std::memory_order_acquire), mc = b->markCount.load (std::memory_order_acquire);
        if (w <= 0 || mc <= 0) { res.why = "Nothing played yet"; return res; }
        // the marks still valid (oldest first)
        std::vector<Mark> m;
        for (juce::int64 k = std::max<juce::int64> (0, mc - kMarks); k < mc; ++k) m.push_back (b->marks[(size_t) (k % kMarks)]);
        const auto mcAfter = b->markCount.load (std::memory_order_acquire);
        if (mcAfter - std::max<juce::int64> (0, mc - kMarks) > kMarks) m.erase (m.begin(), m.begin() + (long) std::min<juce::int64> ((juce::int64) m.size(), mcAfter - mc));   // (overwritten while read)
        if (m.empty()) { res.why = "Nothing played yet"; return res; }
        const auto& last = m.back();
        const double spbNow = last.spb;
        res.bpm = 60.0 * b->sr / spbNow;
        const double beats = bars * beatsPerBar;
        juce::int64 s0 = -1, s1 = w;
        auto songAt = [&] (const Mark& mk, juce::int64 s) { return mk.ppq + (double) (s - mk.at) / mk.spb - (double) latency / mk.spb; };   // song time a sample carries
        if (last.playing)
        {
            const double nowSong = songAt (last, w);
            const double origin = haveBar ? barStartPpq : 0.0;
            const double endSong = origin + std::floor ((nowSong - origin) / beatsPerBar + 1.0e-9) * beatsPerBar, startSong = endSong - beats;
            // walk back through the playing segments; every one of them must continue the song in a straight line
            auto sampleOf = [&] (double song, juce::int64& out) -> bool
            {
                for (int i = (int) m.size(); --i >= 0;)
                {
                    const auto& mk = m[(size_t) i];
                    if (! mk.playing) return false;
                    const juce::int64 segEnd = i + 1 < (int) m.size() ? m[(size_t) i + 1].at : w;
                    const double a = songAt (mk, mk.at), z = songAt (mk, segEnd);
                    if (song >= a - 1.0e-9 && song <= z + 1.0e-9) { out = mk.at + (juce::int64) std::llround ((song - a) * mk.spb); return true; }
                    if (i > 0)
                    {   // the previous segment must end where this one starts (a tempo change), else the window crosses a jump / loop
                        const auto& pv = m[(size_t) i - 1];   // (the latency in beats differs on both sides of a tempo change: allowed)
                        const double tol = std::abs ((double) latency * (1.0 / pv.spb - 1.0 / mk.spb)) + 2.0 / mk.spb;
                        if (! pv.playing || std::abs (songAt (pv, mk.at) - a) > tol) return false;
                    }
                }
                return false;
            };
            juce::int64 e = 0, s = 0;
            if (sampleOf (endSong, e) && sampleOf (startSong, s) && e > s)
            {
                s0 = s; s1 = e; res.barAligned = true;
                res.bpm = beats * 60.0 * b->sr / (double) (e - s);   // (one tempo: the host's exactly, within a sample's rounding)
                bool oneSeg = true; for (auto& mk : m) if (mk.at > s0 && mk.at < s1) oneSeg = false;
                if (oneSeg) res.bpm = 60.0 * b->sr / spbNow;
            }
        }
        if (s0 < 0) { s1 = w; s0 = w - (juce::int64) std::llround (beats * spbNow); }   // stopped / a jump inside: the last N bars of samples at the current tempo
        const juce::int64 margin = (juce::int64) (0.5 * b->sr);
        if (s0 < 0 || s0 < w - (juce::int64) b->size + margin)
        {
            res.why = "Not enough history yet for " + juce::String (bars) + " bars (" + juce::String ((double) std::min<juce::int64> (w, b->size) / b->sr, 1) + " s of " + juce::String (b->size / b->sr, 0) + " s)";
            return res;
        }
        const int n = (int) (s1 - s0);
        res.audio.setSize (2, n);
        int pos = (int) (s0 % b->size), done = 0;
        while (done < n)
        {
            const int c = std::min (n - done, b->size - pos);
            res.audio.copyFrom (0, done, b->l.data() + pos, c); res.audio.copyFrom (1, done, b->r.data() + pos, c);
            done += c; pos = 0;
        }
        if (b->written.load (std::memory_order_acquire) - s0 > b->size) { res.why = "The history moved on while copying: try again"; res.audio.setSize (0, 0); return res; }
        res.ok = true;
        return res;
    }

    // tests
    juce::int64 writtenNow() const { auto* b = buf.load(); return b == nullptr ? 0 : b->written.load(); }
    int ringSize() const { auto* b = buf.load(); return b == nullptr ? 0 : b->size; }
    void retire()
    {
        const bool idle = inProcess.load() == 0; const auto bl = blocks.load();
        retired.erase (std::remove_if (retired.begin(), retired.end(), [idle, bl] (const auto& r) { return idle || bl >= r.second + 2; }), retired.end());
    }

private:
    std::atomic<Buf*> buf { nullptr }; std::atomic<bool> enabled { true };
    std::vector<std::pair<std::unique_ptr<Buf>, juce::int64>> retired;
    std::atomic<int> inProcess { 0 }; std::atomic<juce::int64> blocks { 0 };
    void publish (Buf* nb)
    {
        auto* old = buf.exchange (nb, std::memory_order_acq_rel);
        if (old != nullptr) retired.push_back ({ std::unique_ptr<Buf> (old), blocks.load() });
        retire();
    }
};

// one capture on the shelf (message thread owns it; the audio thread only reads `audio` while it previews)
struct Clip
{
    int id = 0; juce::String name; double sr = 48000, bpm = 120; int bars = 4; bool reversed = false, barAligned = false; juce::int64 timeMs = 0;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    std::vector<float> peaks;                     // min / max pairs for the tile's waveform
    void makePeaks (int points = 240)
    {
        peaks.assign ((size_t) points * 2, 0.0f);
        if (audio == nullptr || audio->getNumSamples() == 0) return;
        const int n = audio->getNumSamples();
        for (int p = 0; p < points; ++p)
        {
            const int a = (int) ((juce::int64) n * p / points), z = std::max (a + 1, (int) ((juce::int64) n * (p + 1) / points));
            float lo = 0, hi = 0;
            for (int c = 0; c < audio->getNumChannels(); ++c) { auto mm = juce::FloatVectorOperations::findMinAndMax (audio->getReadPointer (c, a), z - a); lo = std::min (lo, mm.getStart()); hi = std::max (hi, mm.getEnd()); }
            peaks[(size_t) p * 2] = lo; peaks[(size_t) p * 2 + 1] = hi;
        }
    }
    juce::String fileStem() const
    {
        return "Ghost Box capture " + juce::Time (timeMs).formatted ("%Y-%m-%d %H-%M-%S") + " " + juce::String (bars) + " bars " + juce::String (bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " bpm" + (reversed ? " reversed" : "");
    }
    // the sidecar Ghost Slice reads (docs/research/ghost-box/CAPTURE-SIDECAR.md)
    juce::String sidecarJson() const
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("bpm", bpm); o->setProperty ("bars", bars); o->setProperty ("sampleRate", sr); o->setProperty ("source", "ghostbox");
        return juce::JSON::toString (juce::var (o.get()), false);
    }
};

// writes a clip as a 32-bit float stereo WAV (callers ask the export gate first)
inline bool writeWav (const Clip& c, const juce::File& f)
{
    if (c.audio == nullptr) return false;
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto os = std::make_unique<juce::FileOutputStream> (f);
    if (! os->openedOk()) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), c.sr, 2, 32, {}, 0));   // GATE: via-caller
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (*c.audio, 0, c.audio->getNumSamples());
}

} // namespace boxcapture
