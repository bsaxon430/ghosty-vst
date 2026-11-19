// Ghost Graph checks. Run: GhostGraphTests [all|parse|fresh|sound|midi|tempo|state|speed|ui]
#include "../GraphProcessor.h"
#include "../GraphEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int passed = 0, failed = 0;
static void check (bool ok, const juce::String& what)
{
    if (ok) ++passed; else ++failed;
    std::printf ("[%s] %s\n", ok ? "ok" : "FAIL]", what.toRawUTF8());
}

static void setParam (GraphProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Stats { float peak = 0, rms = 0; bool finite = true; };
static Stats render (GraphProcessor& p, double seconds, juce::MidiBuffer* firstMidi = nullptr, juce::AudioBuffer<float>* keep = nullptr)
{
    const int bs = 512;
    juce::AudioBuffer<float> buf (2, bs);
    Stats st;
    double sum = 0; long long count = 0;
    const int blocks = (int) std::ceil (seconds * 48000.0 / bs);
    if (keep) keep->setSize (2, blocks * bs);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0 && firstMidi) midi = *firstMidi;
        p.processBlock (buf, midi);
        if (p.fakeHost.use && p.fakeHost.playing) p.fakeHost.ppq += bs * p.fakeHost.bpm / 60.0 / 48000.0;
        for (int c = 0; c < 2; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = 0; i < bs; ++i)
            {
                if (! std::isfinite (d[i])) st.finite = false;
                st.peak = std::max (st.peak, std::abs (d[i]));
                sum += d[i] * d[i]; ++count;
            }
            if (keep) keep->copyFrom (c, b * bs, buf, c, 0, bs);
        }
    }
    st.rms = (float) std::sqrt (sum / juce::jmax (1LL, count));
    return st;
}

static double pitchOf (const juce::AudioBuffer<float>& b, int from, int len)
{
    const float* d = b.getReadPointer (0);
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < from + len; ++i)
        if (d[i - 1] <= 0 && d[i] > 0) { if (first < 0) first = i; last = i; ++crossings; }
    if (crossings < 2) return 0;
    return (crossings - 1) * 48000.0 / (last - first);
}

static std::unique_ptr<GraphProcessor> fresh()
{
    auto p = std::make_unique<GraphProcessor>();
    p->prepareToPlay (48000.0, 512);
    return p;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String g = argc > 1 ? juce::String (argv[1]) : "all";
    auto want = [&] (const char* name) { return g == "all" || g == name; };

    if (want ("parse"))
    {
        for (auto& s : gg::factoryScenes())
        {
            auto pr = gg::Program::compile (s.text);
            check (pr->ok() && pr->numVoices() > 0, "scene " + juce::String (s.name) + " reads (" + juce::String (pr->numVoices()) + " voices)");
            int defined = 0;
            for (double x = -8; x < 8; x += 0.05) { pr->setX (x); for (int v = 0; v < pr->numVoices(); ++v) if (std::isfinite (pr->voice (v))) ++defined; }
            check (defined > 50, "scene " + juce::String (s.name) + " makes notes");
        }
        int ti = 0;
        for (auto& tr : gg::factoryTracks())
        {
            ++ti;
            for (size_t si = 0; si < tr.scenes.size(); ++si)
            {
                auto pr = gg::Program::compile (tr.scenes[si]);
                juce::String err;
                for (auto& e : pr->getErrors()) err << " [line " << e.line << ": " << e.message << "]";
                int defined = 0;
                if (pr->ok())
                    for (double x = -8; x < 8; x += 0.05) { pr->setX (x); for (int v = 0; v < pr->numVoices(); ++v) if (std::isfinite (pr->voice (v))) ++defined; }
                check (pr->ok() && defined > 20, juce::String (tr.name) + " scene " + juce::String ((int) si + 1) + " reads and plays (" + juce::String (pr->numVoices()) + " voices)" + err);
            }
        }
        auto ne = gg::Program::compile ("y = [{x != 1: 2}]");
        ne->setX (1); const bool neOff = ! std::isfinite (ne->voice (0)); ne->setX (2);
        check (neOff && ne->voice (0) == 2.0, "!= works");
        auto t = gg::Program::compile ("y = [kick]\nh = frac(x/2)\nkick = {h < 0.3: 1.5e^(-14h) - 2.1}");
        t->setX (0.0);
        check (std::abs (t->voice (0) - (1.5 - 2.1)) < 1e-9, "kick at the start of its hit = 1.5 - 2.1");
        t->setX (1.0);
        check (! std::isfinite (t->voice (0)), "kick silent between hits");
        auto c = gg::Program::compile ("y = [{0.5 < frac(x) < 0.7: 1, 2}, 3w^2, i s]\nw = 2\ni = 3\ns = 4");
        c->setX (0.6);
        auto bare = gg::Program::compile ("y = [a]\na = {x > 0}");
        bare->setX (1); const double b1 = bare->voice (0); bare->setX (-1);
        check (b1 == 1.0 && ! std::isfinite (bare->voice (0)), "a bare {condition} is 1 where true, silent elsewhere");
        check (c->ok() && c->voice (0) == 1 && c->voice (1) == 12 && c->voice (2) == 12, "chained comparisons + implicit multiplication");
        auto bad = gg::Program::compile ("y = [a]\nb = frac(x");
        check (! bad->ok() && bad->getErrors().size() == 2 && bad->getErrors()[0].line == 1, "errors name their lines");
        auto loop = gg::Program::compile ("y = [q]\nq = q + 1");
        loop->setX (1);
        check (loop->ok() && ! std::isfinite (loop->voice (0)), "a variable that uses itself is silent, not a crash");
        auto deep = gg::Program::compile ("y = [F(1)]\nF(a) = F(a + 1)");
        deep->setX (1);
        check (! std::isfinite (deep->voice (0)), "a function that never ends is silent, not a crash");
    }

    if (want ("fresh"))
    {
        auto a = fresh(), b = fresh();
        check (a->getNumScenes() == 6, "a new Ghost Graph has the six scenes");
        check (a->getScene (0).text == gg::factoryScenes()[0].text, "scene one is the original");
        a->setSceneText (0, "y = [0]");
        check (b->getScene (0).text == gg::factoryScenes()[0].text, "a second Ghost Graph stays at the factory scenes");
        check (b->apvts.getRawParameterValue ("chain")->load() > 0.5f && b->apvts.getRawParameterValue ("sync")->load() > 0.5f,
               "fresh start: CHAIN and SYNC on");
    }

    if (want ("sound"))
    {
        auto p = fresh();
        auto s0 = render (*p, 1.0);
        check (s0.peak == 0.0f, "SYNC on and the DAW stopped: silent");
        p->fakeHost = { true, true, 0.0, 120.0 };
        auto s1 = render (*p, 4.0);
        check (s1.finite && s1.rms > 0.01f, "DAW playing: it plays (rms " + juce::String (s1.rms, 3) + ")");
        check (s1.peak < 1.0f, "never clips past full scale (peak " + juce::String (s1.peak, 3) + ")");
        setParam (*p, "chain", 0.0f);
        for (int i = 1; i <= 6; ++i)
        {
            setParam (*p, "scene", (float) i);
            p->fakeHost = { true, true, 0.0, 120.0 };
            auto s = render (*p, 8.0);
            check (s.finite && s.rms > 0.01f && s.peak < 1.0f, "scene " + juce::String (i) + " sounds (rms " + juce::String (s.rms, 3) + ")");
        }
        for (int w = 0; w < 4; ++w)
        {
            setParam (*p, "wave", (float) w);
            auto s = render (*p, 2.0);
            check (s.finite && s.rms > 0.005f, "wave " + juce::String (w) + " sounds");
        }
        auto q = fresh();
        setParam (*q, "sync", 0.0f);
        auto s2 = render (*q, 2.0);
        check (s2.rms > 0.01f, "SYNC off + PLAY: plays on its own clock");
        setParam (*q, "play", 0.0f);
        setParam (*q, "space", 0.0f);
        render (*q, 0.1);
        auto s3 = render (*q, 0.5);
        check (s3.peak < 1e-3f, "PLAY off: falls silent");
    }

    if (want ("midi"))
    {
        auto p = fresh();
        setParam (*p, "sync", 0.0f);
        setParam (*p, "keys", 1.0f);
        setParam (*p, "space", 0.0f);
        setParam (*p, "volume", -12.0f);
        setParam (*p, "tune", 0.0f);
        setParam (*p, "wave", 0.0f);
        p->setSceneText (0, "y = [0]");
        setParam (*p, "chain", 0.0f);
        setParam (*p, "scene", 1.0f);
        auto s0 = render (*p, 0.5);
        check (s0.peak < 1e-4f, "KEYS on, no key held: silent");
        juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> b;
        auto s1 = render (*p, 0.5, &on, &b);
        const double f1 = pitchOf (b, 4800, 9600);
        check (s1.rms > 0.01f && std::abs (f1 - 220.0) < 2.0, "A3 held: y = 0 plays A 220 (" + juce::String (f1, 1) + " Hz)");
        juce::MidiBuffer swap;
        swap.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
        swap.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
        render (*p, 0.5, &swap, &b);
        const double f2 = pitchOf (b, 4800, 9600);
        check (std::abs (f2 - 440.0) < 3.0, "A4 held: an octave up (" + juce::String (f2, 1) + " Hz)");
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        render (*p, 0.05, &off);
        auto s3 = render (*p, 0.3);
        check (s3.peak < 1e-3f, "key let go: silent");
    }

    if (want ("tempo"))
    {
        auto p = fresh();
        {   // Track 1's scenes run at their own speeds, like the first video (scene 4 is the slow one: 15 s at 75 BPM)
            auto q = fresh();
            q->loadTrack (0);
            double beat = 0; const double lens[] = { 16 / 1.0, 16 / 1.058, 16 / 1.164, 16 / 0.853 };
            for (int k = 0; k < 3; ++k) beat += lens[k];
            q->fakeHost = { true, true, beat + lens[3] * 0.5, 75.0 };
            juce::AudioBuffer<float> b1 (2, 32); juce::MidiBuffer m1;
            q->processBlock (b1, m1);
            check (q->displayScene.load() == 3 && std::abs (q->displayX.load()) < 0.05, "Track 1: scene four is slower, like the video (x = " + juce::String (q->displayX.load(), 3) + ")");
        }
        for (int i = 0; i < 6; ++i) p->setSceneSpeed (i, 1.0);
        p->fakeHost = { true, true, 4.0, 120.0 };
        juce::AudioBuffer<float> buf (2, 32);
        juce::MidiBuffer m;
        p->processBlock (buf, m);
        const double expect = 4.0 + 32 * 2.0 / 48000.0 - 8.0;
        check (std::abs (p->displayX.load() - expect) < 1e-6, "1x: x follows the DAW's beat (x = " + juce::String (p->displayX.load(), 4) + ")");
        setParam (*p, "rate", 2.0f);
        p->fakeHost.ppq = 4.0;
        p->processBlock (buf, m);
        check (std::abs (p->displayX.load() - (2 * (4.0 + 32 * 2.0 / 48000.0) - 8.0)) < 1e-6, "2x: x moves twice as fast");
        setParam (*p, "rate", 0.0f);
        p->fakeHost.ppq = 40.0;
        p->processBlock (buf, m);
        check (p->displayScene.load() == 1, "CHAIN: 1/2x at beat 40 is page 2 (scene two)");
        setParam (*p, "rate", 1.0f);
        p->fakeHost.ppq = 16.0 * 5 + 1;
        p->processBlock (buf, m);
        check (p->displayScene.load() == 5, "CHAIN: beat 81 plays scene six");
        p->fakeHost.ppq = 16.0 * 6 + 1;
        p->processBlock (buf, m);
        check (p->displayScene.load() == 0, "CHAIN: after the last scene it starts over");
    }

    if (want ("tracks"))
    {
        auto p = fresh();
        const auto tracks = gg::factoryTracks();
        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            p->loadTrack (i);
            check (p->getNumScenes() == (int) tracks[(size_t) i].scenes.size(), juce::String (tracks[(size_t) i].name) + " loads all its scenes");
            check (std::abs (p->apvts.getRawParameterValue ("tempo")->load() - (float) tracks[(size_t) i].bpm) < 0.5f, juce::String (tracks[(size_t) i].name) + " sets its tempo");
            p->fakeHost = { true, true, 0.0, tracks[(size_t) i].bpm };
            auto st = render (*p, 16.0 * tracks[(size_t) i].scenes.size() * 60.0 / tracks[(size_t) i].bpm);   // the whole track
            check (st.finite && st.rms > 0.01f && st.peak < 1.0f, juce::String (tracks[(size_t) i].name) + " plays all the way through (rms " + juce::String (st.rms, 3) + ")");
        }
    }

    if (g == "render" && argc >= 6)   // render <track 1-5> <scene 1-8> <seconds> <out.wav>
    {
        auto p = fresh();
        const int ti = juce::String (argv[2]).getIntValue() - 1, si = juce::String (argv[3]).getIntValue();
        // track 1-5: a factory track; a negative number -SEED*10-STYLE: a generated song (style 1-5, 0 = any)
        if (ti >= 0) p->loadTrack (ti);
        else { const int code = -(ti + 1); p->generateTrack ((uint32_t) (code / 10), code % 10 - 1); }
        if (si > 0) { setParam (*p, "chain", 0.0f); setParam (*p, "scene", (float) si); }   // scene 0: the whole track, scene after scene
        p->fakeHost = { true, true, 0.0, (double) p->apvts.getRawParameterValue ("tempo")->load() };
        juce::AudioBuffer<float> b;
        render (*p, juce::String (argv[4]).getDoubleValue(), nullptr, &b);
        juce::File out { juce::String (argv[5]) };
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), 48000.0, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return 0;
    }

    if (g == "shot" && argc >= 8)   // shot <track> <scene> <beat in page> <w> <h> <out.png>: the window with the strip hidden
    {
        auto p = fresh();
        const int ti = juce::String (argv[2]).getIntValue() - 1, si = juce::String (argv[3]).getIntValue();
        p->loadTrack (ti);
        setParam (*p, "chain", 0.0f);
        setParam (*p, "scene", (float) si);
        if (auto lk = std::getenv ("GG_LOOK")) setParam (*p, "look", (float) juce::String (lk).getIntValue());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (juce::String (argv[5]).getIntValue(), juce::String (argv[6]).getIntValue());
        p->fakeHost = { true, true, juce::String (argv[4]).getDoubleValue() - 0.01, 120.0 };
        render (*p, 0.005);
        auto* ge = dynamic_cast<GraphEditor*> (ed.get());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        ge->showStrip (false);
        if (std::getenv ("GG_EDIT") != nullptr)
        {
            ge->setEditing (true);
            ge->showStrip (true);
            auto& gv = ge->getGraph();
            for (double yy = 2.4; yy > -2.4; yy -= 0.01)   // light up one note, as if the mouse were on it
            {
                auto h = gv.hitTest ({ gv.xToPx (-5.0), gv.yToPx (yy) });
                if (h.v >= 0) { gv.setHighlight (h.v, h.x0, h.x1); break; }
            }
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File out { juce::String (argv[7]) };
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }

    if (g == "stats")   // how musical: clashes between voices, how busy, how much bars repeat (videos vs GENERATE)
    {
        struct St { double clash = 0, busy = 0, repeat = 0; int n = 0; };
        auto measure = [] (const std::string& text, St& st)
        {
            auto pr = gg::Program::compile (text);
            if (! pr->ok()) return;
            int clashSteps = 0, multi = 0, onSteps = 0, steps = 0;
            std::vector<std::vector<int>> bar (4);
            for (int k = 0; k < 64; ++k)
            {
                pr->setX (-8.0 + k * 0.25 + 0.06);
                std::vector<int> pcs;
                for (int v = 0; v < pr->numVoices(); ++v)
                {
                    if (pr->voiceKind (v) != 0) continue;
                    const auto& nm = pr->voiceLabel (v);
                    if (nm == "kick" || nm == "boom" || nm == "tap" || nm == "tom" || nm == "rim") continue;
                    const double y = pr->voice (v);
                    if (! std::isfinite (y)) continue;
                    pcs.push_back ((int) std::lround (y * 12.0));
                }
                ++steps; if (! pcs.empty()) ++onSteps;
                int sig = 0; for (int q : pcs) sig = sig * 31 + ((q % 12) + 12) % 12;
                bar[(size_t) (k / 16)].push_back (sig);
                if (pcs.size() >= 2)
                {
                    ++multi;
                    bool clash = false;
                    for (size_t a = 0; a < pcs.size(); ++a)
                        for (size_t b = a + 1; b < pcs.size(); ++b)
                        {
                            const int iv = ((pcs[a] - pcs[b]) % 12 + 12) % 12;
                            if (iv == 1 || iv == 11 || iv == 6) clash = true;
                        }
                    if (clash) ++clashSteps;
                }
            }
            int same = 0; for (int k = 0; k < 16; ++k) if (bar[0][(size_t) k] == bar[2][(size_t) k]) ++same;
            st.clash += multi ? (double) clashSteps / multi : 0; st.busy += (double) onSteps / steps; st.repeat += same / 16.0; ++st.n;
        };
        St vid, gen;
        for (auto& tr : gg::factoryTracks()) for (auto& sc : tr.scenes) measure (sc, vid);
        St per[gg::Generator::kStyles];
        for (uint32_t seed = 1; seed <= 200; ++seed) { gg::Generator gn (seed); for (auto& sc : gn.track()) { measure (sc, gen); measure (sc, per[gn.style()]); } }
        auto line = [] (const char* name, const St& st) { std::printf ("%-28s clash %.3f   busy %.2f   bar 1 = bar 3: %.2f   (%d scenes)\n", name, st.clash / st.n, st.busy / st.n, st.repeat / st.n, st.n); };
        line ("the videos", vid); line ("GENERATE (all)", gen);
        for (auto& tr : gg::factoryTracks()) { St t; for (auto& sc : tr.scenes) measure (sc, t); line (("video " + tr.name).c_str(), t); }
        for (int i = 0; i < gg::Generator::kStyles; ++i) line (gg::Generator::styleName (i), per[i]);
        return 0;
    }

    if (g == "trackshots")
    {
        juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile ("trackshots");
        dir.createDirectory();
        auto p = fresh();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        const auto tracks = gg::factoryTracks();
        for (int i = 0; i < (int) tracks.size() + 2; ++i)
        {
            const bool genTrack = i >= (int) tracks.size();
            if (genTrack) p->generateTrack ((uint32_t) (i * 11 + 3), i - (int) tracks.size()); else p->loadTrack (i);
            for (int sc = 0; sc < p->getNumScenes(); ++sc)
            {
                p->fakeHost = { true, true, sc * 16.0 + 15.0, 120.0 };
                render (*p, 0.02);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds());
                juce::FileOutputStream os (dir.getChildFile ("t" + juce::String (i + 1) + "s" + juce::String (sc + 1) + ".png"));
                os.setPosition (0); os.truncate();
                juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
    }

    if (want ("generate"))
    {
        int good = 0, total = 0, voicesOk = 0;
        juce::String firstBad;
        int perStyle[gg::Generator::kStyles] {};
        for (uint32_t seed = 1; seed <= 300; ++seed)
        {
            gg::Generator gen (seed);
            ++perStyle[gen.style()];
            for (auto& sc : gen.track())
            {
                ++total;
                auto pr = gg::Program::compile (sc);
                int defined = 0;
                if (pr->ok())
                    for (double x = -8; x < 8; x += 0.05) { pr->setX (x); for (int v = 0; v < pr->numVoices(); ++v) if (std::isfinite (pr->voice (v))) ++defined; }
                if (pr->ok() && defined > 50) ++good;
                else if (firstBad.isEmpty()) { firstBad = "style " + juce::String (gen.style()) + ": " + juce::String (sc); for (auto& e : pr->getErrors()) firstBad << " [line " << e.line << ": " << e.message << "]"; }
                if (pr->numVoices() <= 8) ++voicesOk;
            }
            gg::Generator again (seed);
            const auto one = again.sceneAgain (2);
            if (! gg::Program::compile (one)->ok() && firstBad.isEmpty()) firstBad = "sceneAgain: " + juce::String (one);
        }
        check (good == total, "GENERATE: " + juce::String (good) + " of " + juce::String (total) + " generated scenes read and play" + (firstBad.isEmpty() ? juce::String() : "\n" + firstBad));
        check (voicesOk == total, "GENERATE: never more than 8 voices");
        bool all = true; for (int c : perStyle) all = all && c > 20;
        check (all, "GENERATE: every style comes up");
        gg::Generator a (42), b (42), c (43);
        check (a.track() == b.track() && gg::Generator (42).track() != c.track(), "GENERATE: same seed, same song; new seed, new song");

        auto p = fresh();
        for (int st = 0; st < gg::Generator::kStyles; ++st)
        {
            p->generateTrack (7 + (uint32_t) st, st);
            const double bpm = p->apvts.getRawParameterValue ("tempo")->load();
            p->fakeHost = { true, true, 0.0, bpm };
            auto stt = render (*p, p->getNumScenes() * 16 * 60.0 / bpm);
            check (stt.finite && stt.rms > 0.05f && stt.peak < 1.0f, juce::String (gg::Generator::styleName (st)) + ": a generated song plays all the way through (rms " + juce::String (stt.rms, 3) + ")");
        }
        check (p->canUndoGenerate(), "GENERATE can be undone");
        p->undoGenerate();
        check (p->getNumScenes() >= 5, "undo brings the scenes back");
        std::printf ("---- a generated riff-house scene:\n%s", gg::Generator (7, 1).track()[1].c_str());
    }

    if (want ("drag"))
    {
        auto p = fresh();
        p->fakeHost = { true, true, 2.0, 75.0 };
        render (*p, 0.02);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* ge = dynamic_cast<GraphEditor*> (ed.get());
        ge->setEditing (true);
        check (ge->isEditing() && ge->getGraph().editMode, "EDIT turns on editing (typing + dragging)");
        check (p->apvts.getRawParameterValue ("chain")->load() < 0.5f, "EDIT loops the scene you are editing");
        auto& gv = ge->getGraph();
        // scene one, voice 2 is "-1": the long held note across the page
        auto h = gv.hitTest ({ gv.xToPx (-4.0), gv.yToPx (-1.0) });
        check (h.v == 1 && h.part == 0 && h.x0 < -7.9 && h.x1 > 7.9, "grab the held note in the middle (voice " + juce::String (h.v) + ", " + juce::String (h.x0, 3) + " to " + juce::String (h.x1, 3) + ")");
        gv.onDragStart (h.v, h.x0, h.x1);
        gv.onDrag (0, 2.0, 7.0 / 12.0, false);
        gv.onDrag (0, 2.0, 7.0 / 12.0, true);
        auto pr = gg::Program::compile (p->getScene (0).text);
        pr->setX (-7.0); const double before = pr->voice (1);
        pr->setX (0.0);  const double moved = pr->voice (1);
        check (pr->ok() && ! std::isfinite (before) && std::abs (moved - (-1.0 + 7.0 / 12.0)) < 1e-9, "moving it 2 beats later and up a fifth rewrites the math (" + juce::String (moved, 4) + ")");
        check (p->getScene (0).text.find ("edit1 = {") != std::string::npos && p->getScene (0).text.find ("y = [kick, edit1,") != std::string::npos, "the voice now reads edit1 in y = [...]");
        // drag its end: longer can't go past the page, so make it shorter, then tilt the end up
        h = gv.hitTest ({ gv.xToPx (7.9), gv.yToPx (-1.0 + 7.0 / 12.0) });
        check (h.v == 1 && h.part == 2, "grab the end of the moved note");
        gv.onDragStart (h.v, h.x0, h.x1);
        gv.onDrag (2, -4.0, 0.0, true);
        pr = gg::Program::compile (p->getScene (0).text);
        pr->setX (5.0);
        check (pr->ok() && ! std::isfinite (pr->voice (1)), "pulling the end left makes it shorter");
        check (p->getScene (0).text.find ("edit2") == std::string::npos, "dragging the same note again updates edit1 (no edit2)");
        h = gv.hitTest ({ gv.xToPx (3.9), gv.yToPx (-1.0 + 7.0 / 12.0) });
        gv.onDragStart (h.v, h.x0, h.x1);
        gv.onDrag (2, 0.0, 5.0 / 12.0, true);
        pr = gg::Program::compile (p->getScene (0).text);
        pr->setX (-5.9); const double ya = pr->voice (1);
        pr->setX (3.99); const double yb = pr->voice (1);
        check (pr->ok() && std::abs (ya - (-5.0 / 12.0)) < 0.01 && std::abs (yb - 0.0) < 0.01, "pulling the end up tilts it into a diagonal (" + juce::String (ya, 3) + " to " + juce::String (yb, 3) + ")");
        // a drum hit (the kick), moved
        h = gv.hitTest ({ gv.xToPx (-8.0 + 0.05), gv.yToPx (1.5 * std::exp (-14.0 * 0.025) - 2.1) });
        check (h.v == 0, "grab a kick");
        gv.onDragStart (h.v, h.x0, h.x1);
        gv.onDrag (0, 1.0, 0.0, true);
        pr = gg::Program::compile (p->getScene (0).text);
        check (pr->ok(), "a moved kick still reads");
        p->fakeHost = { true, true, 0.0, 75.0 };
        auto st = render (*p, 6.0);
        check (st.finite && st.rms > 0.01f && st.peak < 1.0f, "an edited scene plays");
        check (ge->undoCount() == 4, "four drags, four undos");
        for (int i = 0; i < 4; ++i) ge->pressUndo();
        check (p->getScene (0).text == gg::factoryScenes()[0].text, "UNDO brings the original math back");
        const std::string afterAll = [&] { for (int i = 0; i < 4; ++i) ge->pressRedo(); return p->getScene (0).text; }();
        check (afterAll != gg::factoryScenes()[0].text && gg::Program::compile (afterAll)->ok(), "REDO puts all four drags back");
        ge->pressUndo(); ge->pressRedo();
        check (p->getScene (0).text == afterAll, "UNDO then REDO lands on the same math");
        ge->setEditing (false);
        check (! ge->getGraph().editMode, "DONE ends editing");
    }

    if (want ("state"))
    {
        auto a = fresh();
        a->setSceneText (2, "y = [x/8]   # mine");
        a->addScene();
        setParam (*a, "wave", 2.0f);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = fresh();
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        check (b->getNumScenes() == 7, "saved project: 7 scenes come back");
        check (b->getScene (2).text == "y = [x/8]   # mine", "saved project: your equations come back");
        check ((int) b->apvts.getRawParameterValue ("wave")->load() == 2, "saved project: the knobs come back");
    }

    if (want ("speed"))
    {
        auto p = fresh();
        p->fakeHost = { true, true, 0.0, 120.0 };
        setParam (*p, "wave", 2.0f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (*p, 96.0);   // all six scenes
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 96000.0 * 0.15, "96 s of audio renders in " + juce::String (ms, 0) + " ms (under 15% of real time)");
    }

    if (want ("ui"))
    {
        auto p = fresh();
        p->fakeHost = { true, true, 6.0, 120.0 };
        render (*p, 0.1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        check (ed->getWidth() == 1054 && ed->getHeight() == 760, "the window opens at 1054 x 760 (fits FL Studio on a 1440 x 900 MacBook)");
        auto* ge = dynamic_cast<GraphEditor*> (ed.get());
        check (ge != nullptr && ge->getGraph().numVoices() == 5, "the graph shows scene one's 5 voices");
        check (ge != nullptr && ge->getMath().hasContent() && ge->getMath().isVisible(), "scene one's equations are typeset");
        juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile ("uishots");
        if (auto env = std::getenv ("GG_SHOTS")) dir = juce::File (juce::String (env));
        dir.createDirectory();
        for (int k = 0; k < 6; ++k)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            juce::FileOutputStream os (dir.getChildFile (k == 0 ? "ui-open-size.png" : "ui-scene-" + juce::String (k + 1) + ".png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
            setParam (*p, "chain", 0.0f);
            setParam (*p, "scene", (float) (k + 2));
            p->fakeHost = { true, true, 9.0 + k * 2, 120.0 };
            render (*p, 0.05);
        }
        ed->setSize (1500, 980);
        auto big = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::FileOutputStream os2 (dir.getChildFile ("ui-big.png"));
        os2.setPosition (0); os2.truncate();
        juce::PNGImageFormat().writeImageToStream (big, os2);
        check (big.getWidth() == 1500, "the window resizes");
        for (int lk = 1; lk <= 4; ++lk)
        {
            setParam (*p, "look", (float) lk);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
            auto im = ed->createComponentSnapshot (ed->getLocalBounds());
            juce::FileOutputStream os3 (dir.getChildFile ("ui-look-" + juce::String (lk) + ".png"));
            os3.setPosition (0); os3.truncate();
            juce::PNGImageFormat().writeImageToStream (im, os3);
            check (im.getWidth() == 1500, juce::String ("LOOK ") + ggui::theme (lk).name + " draws");
        }
        // hover tips, drawn in each look over that look's page
        {
            juce::Image tips (juce::Image::ARGB, 1500, 5 * 150, true);
            const juce::String tip = "Change the look: PAPER (the videos), CHROMA (rainbow watercolour), INK (the watercolour ghost), WINE (the burgundy video), TI 84 (a graphing calculator)";
            for (int lk = 0; lk < 5; ++lk)
            {
                setParam (*p, "look", (float) lk);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
                auto page = ed->createComponentSnapshot ({ 300, 200, 1500, 150 });
                juce::Graphics g (tips);
                g.drawImageAt (page, 0, lk * 150);
                auto& lf = ed->getLookAndFeel();
                auto r = lf.getTooltipBounds (tip, { 600, 40 }, { 0, 0, 1500, 150 });
                juce::Graphics::ScopedSaveState ss (g);
                g.setOrigin (r.getX(), lk * 150 + r.getY());
                lf.drawTooltip (g, tip, r.getWidth(), r.getHeight());
            }
            juce::FileOutputStream os4 (dir.getChildFile ("ui-tips.png"));
            os4.setPosition (0); os4.truncate();
            juce::PNGImageFormat().writeImageToStream (tips, os4);
        }
        ed.reset();
        check (true, "the window closes cleanly");
    }

    std::printf ("%d checks passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
