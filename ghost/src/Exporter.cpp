#include "Exporter.h"
#include "GraphSong.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace ghost
{
namespace
{
struct OfflineHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};

juce::String safeName (juce::String s)
{
    s = s.replaceCharacters ("/\\:*?\"<>|", "---------").trim();
    return s.isEmpty() ? juce::String ("part") : s.substring (0, 60);
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) juce::jmax (1, b.getNumChannels()), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// the fundamental of a low, steady sound (an 808): autocorrelation between 28 and 320 Hz; 0 when it isn't clear
double fundamental (const float* x, int n, double sr)
{
    if (n < 256) return 0.0;
    const int lo = (int) (sr / 320.0), hi = juce::jmin (n / 2, (int) (sr / 28.0));
    double e0 = 0; for (int i = 0; i < n - hi; ++i) e0 += (double) x[i] * x[i];
    if (e0 < 1.0e-6) return 0.0;
    double best = 0; int bestLag = 0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double s = 0; for (int i = 0; i < n - hi; ++i) s += (double) x[i] * x[i + lag];
        if (s / e0 > best) { best = s / e0; bestLag = lag; }
    }
    return best > 0.55 && bestLag > 0 ? sr / bestLag : 0.0;
}

// the hits in a drum track played alone: where its loudness (the peak of each 5 ms) jumps well above the last 20 ms
// (a long 808's own wobble doesn't count; a new hit always does), at least 45 ms apart
std::vector<std::pair<int, float>> findHits (const juce::AudioBuffer<float>& b, double sr)
{
    std::vector<std::pair<int, float>> hits;
    const int n = b.getNumSamples(), hop = 128, win = 256;
    if (n < win * 2) return hits;
    const int frames = (n - win) / hop;
    std::vector<float> env ((size_t) frames);
    float peak = 0;
    for (int f = 0; f < frames; ++f)
    {
        float m = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) { auto* x = b.getReadPointer (c) + f * hop; for (int i = 0; i < win; ++i) m = juce::jmax (m, std::abs (x[i])); }
        env[(size_t) f] = m; peak = juce::jmax (peak, m);
    }
    if (peak < 1.0e-4f) return hits;
    const int gap = juce::jmax (1, (int) (0.045 * sr / hop));
    int last = -gap;
    for (int f = 0; f < frames; ++f)
    {
        float before = 0; int cnt = 0;
        for (int k = juce::jmax (0, f - 8); k < f - 1; ++k) { before += env[(size_t) k]; ++cnt; }
        before = cnt > 0 ? before / (float) cnt : 0.0f;
        if (env[(size_t) f] > 0.1f * peak && env[(size_t) f] > 1.6f * before + 0.02f * peak && f - last >= gap)
        {
            int at = f * hop;   // the sample where it starts
            for (int i = f * hop; i < f * hop + win && i < n; ++i)
            {
                float v = 0; for (int c = 0; c < b.getNumChannels(); ++c) v = juce::jmax (v, std::abs (b.getSample (c, i)));
                if (v > 0.25f * env[(size_t) f]) { at = i; break; }
            }
            float lp = 0; for (int k = f; k < juce::jmin (frames, f + 8); ++k) lp = juce::jmax (lp, env[(size_t) k]);
            hits.push_back ({ at, lp / peak });
            last = f;
        }
    }
    return hits;
}
} // namespace

// one plug-in played offline: from 'ppq 0' (or stopped), these notes in, its sound out
struct Exporter::Render
{
    juce::AudioPluginInstance* p = nullptr;
    OfflineHead head;
    bool playing = true;
    int total = 0, done = 0, discard = 0;
    juce::MidiBuffer events;                     // at absolute sample positions (from the start of the kept part)
    juce::AudioBuffer<float> out, scratch;
    juce::MidiBuffer midiOut;                    // what it sends out (ArpEZator), absolute positions
    std::function<void (float*, float*, int)> chain;   // after the plug-in (TONE / FX / level)
    double rate = 48000, bpm = 120;
    int block = 512;

    void begin (juce::AudioPluginInstance* plugin, int keepSamples, int latency, bool play, double sr, double tempo, int blk)
    {
        p = plugin; playing = play; rate = sr; bpm = tempo; block = blk;
        discard = juce::jmax (0, latency);
        total = keepSamples + discard;
        out.setSize (2, keepSamples); out.clear();
        scratch.setSize (juce::jmax (2, juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels())), block);
        // a moment of silence first: what the live sound left ringing stops
        juce::AudioPlayHead::PositionInfo info; info.setIsPlaying (false); info.setBpm (bpm); info.setPpqPosition (0.0);
        head.info = info; p->setPlayHead (&head);
        juce::MidiBuffer off;
        for (int i = 0; i < (int) (0.25 * rate / block) + 1; ++i)
        {
            scratch.clear(); off.clear();
            if (i == 0) for (int ch = 1; ch <= 16; ++ch) off.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
            p->processBlock (scratch, off);
        }
    }
    // a few blocks; true when finished
    bool run (int blocks)
    {
        juce::MidiBuffer m;
        for (int b = 0; b < blocks && done < total; ++b)
        {
            const int len = juce::jmin (block, total - done);
            juce::AudioPlayHead::PositionInfo info;
            info.setBpm (bpm); info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setIsPlaying (playing);
            const double ppq = (double) (done - discard) / rate * bpm / 60.0;
            info.setPpqPosition (juce::jmax (0.0, ppq));
            info.setPpqPositionOfLastBarStart (std::floor (juce::jmax (0.0, ppq) / 4.0) * 4.0);
            info.setTimeInSamples ((juce::int64) juce::jmax (0, done - discard));
            info.setTimeInSeconds ((double) juce::jmax (0, done - discard) / rate);
            if (done < discard) info.setIsPlaying (false);   // (the latency part: the sound of the plug-in catches up)
            head.info = info;
            m.clear();
            const int from = done - discard;
            for (const auto ev : events)
                if (ev.samplePosition >= from && ev.samplePosition < from + len) m.addEvent (ev.getMessage(), ev.samplePosition - from);
            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), scratch.getNumChannels(), len);
            view.clear();
            p->processBlock (view, m);
            for (const auto ev : m) midiOut.addEvent (ev.getMessage(), juce::jmax (0, from) + ev.samplePosition);
            auto* l = view.getWritePointer (0);
            auto* r = view.getWritePointer (p->getTotalNumOutputChannels() > 1 ? 1 : 0);
            if (r == l) { r = view.getWritePointer (1); juce::FloatVectorOperations::copy (r, l, len); }
            if (chain) chain (l, r, len);
            for (int i = 0; i < len; ++i)
            {
                const int at = done + i - discard;
                if (at >= 0 && at < out.getNumSamples()) { out.setSample (0, at, l[i]); out.setSample (1, at, r[i]); }
            }
            done += len;
        }
        return done >= total;
    }
};

Exporter::Exporter (GhostProcessor& p) : proc (p) {}

Exporter::~Exporter()
{
    stopTimer();
    if (isRunning) finish (false);
}

bool Exporter::start()
{
    if (isRunning || proc.arp == nullptr && proc.graph == nullptr) { status = isRunning ? "already making it" : "the parts aren't open yet"; return false; }
    src = proc.source.load();
    if (src == GhostProcessor::FromGraph && proc.graphLive.load() == nullptr) { status = "Ghost Graph has no song yet"; return false; }
    rate = proc.rate(); block = proc.block(); bpm = proc.shownBpm.load();
    parts.clear(); drums.clear(); songMidi = juce::File(); mixStem = juce::File();
    for (auto& v : laneNotes) v.clear();
    for (int l = 0; l < kLanes; ++l) { laneStems[(size_t) l].setSize (0, 0); laneHasStem[(size_t) l] = false; }
    beatsTouched = false; beatsOriginal.reset(); drumAt = shotAt = 0;
    // how long: ArpEZator's loop, or Ghost Graph's song
    beats = 16.0;
    if (src == GhostProcessor::FromGraph) beats = proc.graphLive.load()->beats;
    else if (proc.arp)
    {
        juce::MemoryBlock st; proc.arp->getStateInformation (st);
        const auto raw = componentStateOf (st);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize()))
        {
            const auto idea = juce::JSON::parse (xml->getStringAttribute ("idea"));
            const int bars = (int) idea.getProperty ("bars", 4);
            beats = juce::jlimit (4, 64, bars) * 4.0;
        }
    }
    beats = juce::jlimit (4.0, 256.0, beats);
    // the folder
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("GhostBlock/GHOST/Exports")
                 .getChildFile (stamp + (src == GhostProcessor::FromGraph ? " GRAPH" : " ARPEZ"));
    for (auto sub : { "MIDI", "Stems", "Sounds" }) folder.getChildFile (sub).createDirectory();

    // the live sound pauses (the parts are played here for a few seconds)
    proc.exporting = true;
    const int keep = (int) std::ceil (beats * 60.0 / bpm * rate);

    jobs.clear();
    jobs.push_back ([this] { return proc.inAudio.load() == 0; });   // (wait for the audio thread to let go)
    // 1. the notes
    if (src == GhostProcessor::FromGraph)
    {
        jobs.push_back ([this]
        {
            auto* song = proc.graphLive.load();
            for (auto& e : song->events)
            {
                if (e.target < 4) laneNotes[(size_t) e.target].push_back ({ e.beat, e.note, e.velocity, e.on });
                else if (e.on)
                {
                    const int t = e.note - 36;
                    auto it = std::find_if (drums.begin(), drums.end(), [t] (const DrumTrack& d) { return d.track == t; });
                    if (it == drums.end()) { drums.push_back ({}); it = drums.end() - 1; it->track = t; }
                    it->hits.push_back ({ e.beat, e.velocity, 0 });
                    it->used = true;
                }
            }
            return true;
        });
    }
    else
    {
        jobs.push_back ([this, keep]
        {
            if (! current)
            {
                if (! proc.arp) return true;
                current = std::make_unique<Render>();
                current->begin (proc.arp.get(), keep, 0, true, rate, bpm, block);
                status = "ArpEZator's notes";
            }
            if (! current->run (64)) return false;
            for (const auto ev : current->midiOut)
            {
                const auto m = ev.getMessage();
                if (! m.isNoteOnOrOff()) continue;
                for (int l = 0; l < kLanes; ++l)
                    if (m.getChannel() == laneMidiChannel (l))
                        laneNotes[(size_t) l].push_back ({ ev.samplePosition / rate * bpm / 60.0, m.getNoteNumber(), m.getVelocity(), m.isNoteOn() });
            }
            current.reset();
            return true;
        });
    }
    // 2. each lane as heard
    for (int l = 0; l < kLanes; ++l)
    {
        jobs.push_back ([this, l, keep]
        {
            auto& L = proc.lanes[(size_t) l];
            if (! L.plugin || laneNotes[(size_t) l].empty()) return true;
            if (! current)
            {
                current = std::make_unique<Render>();
                const int lat = L.pluginLatency + L.fxLatency;
                current->begin (L.plugin.get(), keep, lat, true, rate, bpm, block);
                for (auto& n : laneNotes[(size_t) l])
                {
                    const int at = (int) std::llround (n.beat * 60.0 / bpm * rate);
                    current->events.addEvent (n.on ? juce::MidiMessage::noteOn (1, n.note, (juce::uint8) juce::jlimit (1, 127, n.vel)) : juce::MidiMessage::noteOff (1, n.note), juce::jlimit (0, keep - 1, at));
                }
                for (int ch = 1; ch <= 1; ++ch) current->events.addEvent (juce::MidiMessage::allNotesOff (ch), keep - 1);
                // TONE, FX, level - as in the live chain
                auto lp = std::make_shared<juce::dsp::StateVariableTPTFilter<float>>(), hp = std::make_shared<juce::dsp::StateVariableTPTFilter<float>>();
                juce::dsp::ProcessSpec spec { rate, (juce::uint32) block, 2 };
                lp->prepare (spec); hp->prepare (spec);
                lp->setType (juce::dsp::StateVariableTPTFilterType::lowpass); hp->setType (juce::dsp::StateVariableTPTFilterType::highpass);
                lp->setResonance (0.6f); hp->setResonance (0.6f);
                const float tone = proc.apvts.getRawParameterValue (juce::String (laneName (l)).toLowerCase() + "_tone")->load();
                lp->setCutoffFrequency ((float) juce::jmin (rate * 0.45, 300.0 * std::pow (20000.0 / 300.0, 1.0 + juce::jmin (0.0f, tone))));
                hp->setCutoffFrequency ((float) (20.0 * std::pow (1200.0 / 20.0, juce::jmax (0.0f, tone))));
                const float gain = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue (juce::String (laneName (l)).toLowerCase() + "_level")->load(), -59.9f);
                L.fx.activate (rate);   // (a clean start for its FX)
                auto* fx = &L.fx; const double tempo = bpm;
                current->chain = [lp, hp, tone, gain, fx, tempo] (float* a, float* b, int n)
                {
                    if (std::abs (tone) > 0.01f)
                    {
                        float* ch[] { a, b }; juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n); juce::dsp::ProcessContextReplacing<float> ctx (blk);
                        if (tone < 0) lp->process (ctx); else hp->process (ctx);
                    }
                    fx->process (a, b, (uint32_t) n, tempo);
                    juce::FloatVectorOperations::multiply (a, gain, n); juce::FloatVectorOperations::multiply (b, gain, n);
                };
                status = juce::String (laneName (l)) + ": " + L.soundName;
            }
            if (! current->run (24)) return false;
            laneStems[(size_t) l].makeCopyOf (current->out);
            laneHasStem[(size_t) l] = true;
            current.reset();
            proc.lanes[(size_t) l].fx.activate (rate);
            return true;
        });
    }
    // 3. the drums
    jobs.push_back ([this]
    {   // which Ghosty Beats tracks there are (from its state)
        if (! proc.beats) return true;
        proc.beats->getStateInformation (beatsOriginal);
        const auto raw = componentStateOf (beatsOriginal);
        juce::ValueTree t;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize())) t = juce::ValueTree::fromXml (*xml);
        if (! t.isValid()) t = juce::ValueTree::readFromData (raw.getData(), raw.getSize());
        std::function<void (const juce::ValueTree&)> walk = [&] (const juce::ValueTree& v)
        {
            if (v.hasProperty ("sound") && v.hasProperty ("i"))
            {
                const int tr = (int) v["i"];
                auto it = std::find_if (drums.begin(), drums.end(), [tr] (const DrumTrack& d) { return d.track == tr; });
                if (it == drums.end() && src == GhostProcessor::FromArpez && v["sound"].toString().isNotEmpty() && tr >= 0 && tr < 16) { drums.push_back ({}); it = drums.end() - 1; it->track = tr; }
                if (it != drums.end() && it->sound.isEmpty()) it->sound = v["sound"].toString();
            }
            for (const auto& c : v) walk (c);
        };
        if (t.isValid()) walk (t);
        std::sort (drums.begin(), drums.end(), [] (const DrumTrack& a, const DrumTrack& b) { return a.track < b.track; });
        return true;
    });
    jobs.push_back ([this, keep]
    {   // each drum track alone
        auto& at = drumAt;
        if (! proc.beats) return true;
        if (! current)
        {
            if (at >= drums.size()) return true;
            auto& d = drums[at];
            current = std::make_unique<Render>();
            if (src == GhostProcessor::FromGraph)
            {
                current->begin (proc.beats.get(), keep, proc.beats->getLatencySamples(), false, rate, bpm, block);
                for (auto& h : d.hits) current->events.addEvent (juce::MidiMessage::noteOn (10, 36 + d.track, (juce::uint8) juce::jlimit (1, 127, h.vel)), juce::jlimit (0, keep - 1, (int) std::llround (h.beat * 60.0 / bpm * rate)));
            }
            else
            {   // ARPEZ: Ghosty Beats plays its own pattern with only this track on (its SOLO)
                const auto raw = componentStateOf (beatsOriginal);
                juce::ValueTree t; bool wasXml = false;
                if (auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize())) { t = juce::ValueTree::fromXml (*xml); wasXml = true; }
                if (! t.isValid()) t = juce::ValueTree::readFromData (raw.getData(), raw.getSize());
                const int tr = d.track;
                std::function<void (juce::ValueTree)> walk = [&] (juce::ValueTree v)
                {
                    if (v.hasProperty ("sound") && v.hasProperty ("i")) { v.setProperty ("solo", (int) v["i"] == tr, nullptr); if ((int) v["i"] == tr) v.setProperty ("mute", false, nullptr); }
                    for (auto c : v) walk (c);
                };
                walk (t);
                juce::MemoryBlock back;
                if (wasXml) { if (auto x = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*x, back); }
                else { juce::MemoryOutputStream mo (back, false); t.writeToStream (mo); }
                const auto host = Catalog::wrapForVst3Host (back);
                proc.beats->setStateInformation (host.getData(), (int) host.getSize());
                beatsTouched = true;
                current->begin (proc.beats.get(), keep, proc.beats->getLatencySamples(), true, rate, bpm, block);
            }
            status = "drums: " + d.sound;
        }
        if (! current->run (24)) return false;
        auto& d = drums[at];
        d.stem.makeCopyOf (current->out);
        if (src == GhostProcessor::FromArpez)
        {   // the hits, from the sound
            for (auto& h : findHits (d.stem, rate)) d.hits.push_back ({ h.first / rate * bpm / 60.0, juce::jlimit (1, 127, (int) std::lround (40 + 87 * std::sqrt (h.second))), 0 });
            d.used = ! d.hits.empty();
        }
        current.reset();
        ++at;
        return false;
    });
    jobs.push_back ([this]
    {   // each drum's sound, one hit
        auto& at = shotAt;
        if (! proc.beats) return true;
        while (at < drums.size() && ! drums[at].used) ++at;
        if (at >= drums.size()) return true;
        auto& d = drums[at];
        if (beatsTouched)
        {   // (its pattern back as it was before the one-shots, so a solo can't hide a sound)
            proc.beats->setStateInformation (beatsOriginal.getData(), (int) beatsOriginal.getSize());
            beatsTouched = false;
        }
        Render r;
        const int len = (int) (3.0 * rate);
        r.begin (proc.beats.get(), len, proc.beats->getLatencySamples(), false, rate, bpm, block);
        r.events.addEvent (juce::MidiMessage::noteOn (10, 36 + d.track, (juce::uint8) 110), 0);
        while (! r.run (64)) {}
        int end = len; while (end > 64 && r.out.getMagnitude (end - 64, 64) < 0.0005f) end -= 64;
        d.shot.setSize (2, juce::jmax (64, end));
        for (int c = 0; c < 2; ++c) d.shot.copyFrom (c, 0, r.out, c, 0, d.shot.getNumSamples());
        d.shot.applyGainRamp (juce::jmax (0, d.shot.getNumSamples() - 256), juce::jmin (256, d.shot.getNumSamples()), 1.0f, 0.0f);
        // an 808 line: each hit's note from its pitch against the one-shot's
        const auto low = d.sound.toLowerCase();
        if (src == GhostProcessor::FromArpez && (low.contains ("808") || low.contains ("bass")))
        {
            const int s0 = (int) (0.015 * rate), sl = (int) (0.12 * rate);
            const double base = fundamental (d.shot.getReadPointer (0) + juce::jmin (s0, d.shot.getNumSamples() - 1), juce::jmin (sl, d.shot.getNumSamples() - s0), rate);
            if (base > 0)
                for (size_t i = 0; i < d.hits.size(); ++i)
                {
                    const int a = (int) std::llround (d.hits[i].beat * 60.0 / bpm * rate) + s0;
                    const int b = i + 1 < d.hits.size() ? (int) std::llround (d.hits[i + 1].beat * 60.0 / bpm * rate) : d.stem.getNumSamples();
                    const int n = juce::jmin (sl, b - a);
                    if (a < 0 || n < 512 || a + n > d.stem.getNumSamples()) continue;
                    const double f = fundamental (d.stem.getReadPointer (0) + a, n, rate);
                    if (f > 0) d.hits[i].semis = juce::jlimit (-24, 24, (int) std::lround (12.0 * std::log2 (f / base)));
                }
        }
        status = "drum sounds: " + d.sound;
        ++at;
        return false;
    });
    // 4. the files
    jobs.push_back ([this, keep]
    {
        if (beatsTouched && proc.beats) proc.beats->setStateInformation (beatsOriginal.getData(), (int) beatsOriginal.getSize());
        beatsTouched = false;
        const int tpq = 960;
        auto tick = [tpq] (double beat) { return beat * tpq; };
        juce::MidiFile all; all.setTicksPerQuarterNote (tpq);
        {
            juce::MidiMessageSequence t0;
            t0.addEvent (juce::MidiMessage::textMetaEvent (3, "GHOST"), 0);
            t0.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / bpm)), 0);
            t0.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
            t0.addEvent (juce::MidiMessage::endOfTrack(), tick (beats));
            all.addTrack (t0);
        }
        auto writeOne = [&] (const juce::String& name, const juce::MidiMessageSequence& seq) -> juce::File
        {
            juce::MidiFile one; one.setTicksPerQuarterNote (tpq);
            juce::MidiMessageSequence head;
            head.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / bpm)), 0);
            head.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
            one.addTrack (head);
            one.addTrack (seq);
            auto f = folder.getChildFile ("MIDI").getChildFile (safeName (name) + ".mid");
            f.deleteFile();
            juce::FileOutputStream os (f); if (os.openedOk()) one.writeTo (os);
            all.addTrack (seq);
            return f;
        };
        // the lanes
        for (int l = 0; l < kLanes; ++l)
        {
            auto& L = proc.lanes[(size_t) l];
            if (laneNotes[(size_t) l].empty()) continue;
            Part part; part.name = juce::String (laneName (l)) + " - " + L.soundName;
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String (laneName (l)) + " " + L.soundName), 0);
            std::array<double, 128> onAt; onAt.fill (-1.0);
            std::array<int, 128> vel {};
            for (auto& n : laneNotes[(size_t) l])
            {
                const int nn = juce::jlimit (0, 127, n.note);
                if (n.on) { if (onAt[(size_t) nn] >= 0) seq.addEvent (juce::MidiMessage::noteOff (1, nn), tick (n.beat)); onAt[(size_t) nn] = n.beat; vel[(size_t) nn] = n.vel; seq.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) juce::jlimit (1, 127, n.vel)), tick (n.beat)); }
                else if (onAt[(size_t) nn] >= 0)
                {
                    double off = n.beat; if (off <= onAt[(size_t) nn]) off += beats;   // (a note that ends after the loop starts again)
                    seq.addEvent (juce::MidiMessage::noteOff (1, nn), tick (juce::jmin (off, beats)));
                    onAt[(size_t) nn] = -1.0;
                }
            }
            for (int nn = 0; nn < 128; ++nn) if (onAt[(size_t) nn] >= 0) seq.addEvent (juce::MidiMessage::noteOff (1, nn), tick (beats));
            seq.sort(); seq.updateMatchedPairs();
            part.midi = writeOne (part.name, seq);
            if (laneHasStem[(size_t) l])
            {
                part.stem = folder.getChildFile ("Stems").getChildFile (safeName (part.name) + ".wav");
                writeWav (part.stem, laneStems[(size_t) l], rate);
            }
            // the sound, as its synth's own preset (with your edits)
            if (L.plugin)
            {
                juce::MemoryBlock hs; L.plugin->getStateInformation (hs);
                auto raw = componentStateOf (hs);
                const auto base = folder.getChildFile ("Sounds").getChildFile (safeName (juce::String (laneName (l)) + " - " + L.soundName));
                if (L.engine == Engine::GhostyPro2)
                {
                    auto text = raw.toString(); if (text.endsWithChar (0)) text = text.dropLastCharacters (1);
                    text = text.trimEnd();
                    // Ghosty Pro 2 (2.0.22 on) has the same FX rack on its SPECTRAL tab: the lane's FX ride inside the preset
                    if (L.fx.on() && text.startsWithChar ('{'))
                        text = "{\"ghost_spectral\":" + juce::JSON::toString (juce::var (juce::String (L.fx.toState())), true) + (text.substring (1).trimStart().startsWithChar ('}') ? "" : ",") + text.substring (1);
                    part.sound = base.withFileExtension ("vital"); part.sound.replaceWithText (text);
                }
                else if (L.engine == Engine::Poltergeist2)
                {
                    const auto s = raw.toString(); const int z = s.indexOfChar (0);
                    part.sound = base.withFileExtension ("sxsnp"); part.sound.replaceWithText (z >= 0 ? s.substring (0, z) : s);
                }
                else
                {
                    const auto j = juce::JSON::parse (raw.toString());
                    const auto preset = j.getProperty ("preset", {});
                    part.sound = base.withFileExtension ("atlas"); part.sound.replaceWithText (juce::JSON::toString (preset.isObject() ? preset : j, false));
                }
                if (L.fx.on() && L.engine != Engine::GhostyPro2)
                    folder.getChildFile ("Sounds").getChildFile (safeName (juce::String (laneName (l)) + " FX")).withFileExtension ("ghostfx").replaceWithText (juce::String (L.fx.toState()));
            }
            parts.push_back (part);
        }
        // the drums: one part each
        for (auto& d : drums)
        {
            if (! d.used) continue;
            Part part; part.name = "Drum " + juce::String (d.track + 1) + " - " + d.sound;
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, d.sound), 0);
            for (size_t i = 0; i < d.hits.size(); ++i)
            {
                const auto& h = d.hits[i];
                const double next = i + 1 < d.hits.size() ? d.hits[i + 1].beat : beats;
                const int nn = juce::jlimit (0, 127, 60 + h.semis);   // C5 = the sound as it is (FL's sampler)
                seq.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) h.vel), tick (h.beat));
                seq.addEvent (juce::MidiMessage::noteOff (1, nn), tick (juce::jmin (next, h.beat + 0.25)));
            }
            seq.sort(); seq.updateMatchedPairs();
            part.midi = writeOne (part.name, seq);
            if (d.stem.getNumSamples() > 0) { part.stem = folder.getChildFile ("Stems").getChildFile (safeName (part.name) + ".wav"); writeWav (part.stem, d.stem, rate); }
            if (d.shot.getNumSamples() > 0) { part.sound = folder.getChildFile ("Sounds").getChildFile (safeName (part.name) + ".wav"); writeWav (part.sound, d.shot, rate); }
            parts.push_back (part);
        }
        songMidi = folder.getChildFile ("MIDI").getChildFile ("GHOST song (all parts).mid");
        songMidi.deleteFile();
        { juce::FileOutputStream os (songMidi); if (os.openedOk()) all.writeTo (os); }
        // the whole mix
        juce::AudioBuffer<float> mix (2, keep); mix.clear();
        for (int l = 0; l < kLanes; ++l) if (laneHasStem[(size_t) l]) for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, laneStems[(size_t) l], c, 0, juce::jmin (keep, laneStems[(size_t) l].getNumSamples()));
        for (auto& d : drums) if (d.used && d.stem.getNumSamples() > 0) for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, d.stem, c, 0, juce::jmin (keep, d.stem.getNumSamples()));
        const float pk = mix.getMagnitude (0, keep); if (pk > 0.98f) mix.applyGain (0.98f / pk);
        mixStem = folder.getChildFile ("Stems").getChildFile ("GHOST mix.wav");
        writeWav (mixStem, mix, rate);
        folder.getChildFile ("HOW TO.txt").replaceWithText (
            "GHOST - your song as FL files\n\n"
            "1. Drag MIDI/GHOST song (all parts).mid onto FL's Channel Rack: FL makes a channel per part with its notes.\n"
            "2. A synth part: click its channel, swap it for the synth (Ghosty Pro 2 / Poltergeist 2 / Ghost Atlas) and open\n"
            "   its sound from Sounds/ (.vital in Ghosty Pro 2, .sxsnp in Poltergeist 2, .atlas in Ghost Atlas).\n"
            "   Its FX: a Ghosty Pro 2 sound carries them (its SPECTRAL tab). For the others, put GHOST FX on that\n"
            "   channel's mixer track and open the lane's .ghostfx from Sounds/.\n"
            "3. A drum part: drag its .wav from Sounds/ onto its channel (C5 = the sound as it is).\n"
            "4. Or use Stems/: every part exactly as heard (sound, TONE, FX, level), and GHOST mix.wav.\n"
            "Tempo: " + juce::String (bpm, 1) + " BPM, " + juce::String ((int) (beats / 4.0)) + " bars.\n");
        return true;
    });
    total = (int) jobs.size(); doneJobs = 0;
    isRunning = true;
    startedAt = juce::Time::getMillisecondCounter();
    status = "starting";
    startTimerHz (60);
    if (onChange) onChange();
    return true;
}

bool Exporter::step()
{
    if (doneJobs >= (int) jobs.size()) return false;
    if (jobs[(size_t) doneJobs]()) ++doneJobs;
    return doneJobs < (int) jobs.size();
}

void Exporter::timerCallback()
{
    const auto t0 = juce::Time::getMillisecondCounter();
    bool more = true;
    while (more && juce::Time::getMillisecondCounter() - t0 < 40) more = step();   // (about 40 ms of work per tick)
    if (onChange) onChange();
    if (! more) finish (true);
}

void Exporter::finish (bool ok)
{
    stopTimer();
    current.reset();
    if (beatsTouched && proc.beats) proc.beats->setStateInformation (beatsOriginal.getData(), (int) beatsOriginal.getSize());
    beatsTouched = false;
    for (auto& L : proc.lanes) L.fx.activate (proc.rate());
    proc.exporting = false;
    isRunning = false;
    status = ok ? "done in " + juce::String ((juce::Time::getMillisecondCounter() - startedAt) / 1000.0, 1) + " s" : juce::String ("stopped");
    if (onChange) onChange();
}

bool Exporter::runBlocking()
{
    if (! start()) return false;
    stopTimer();
    while (step()) {}
    finish (true);
    return true;
}
} // namespace ghost
