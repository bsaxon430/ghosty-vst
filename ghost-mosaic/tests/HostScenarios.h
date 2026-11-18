// Ghost Mosaic: "does it make sound in a host?" (GPL-2.0-or-later, like the rest of this fork).
//
// The same scenarios run twice:
//   - tests/HostTest.cpp loads the real Ghost Mosaic.vst3 / .component as an instrument, the way FL Studio does
//     (stereo out, no audio in, MIDI in), with its screen open, and drives it through the hook in HostHook.cpp
//     (drop sounds on SOURCE SOUNDS / TARGET, click the GENERATE button itself);
//   - MosaicTests "ready" runs them in-process on the same code.
// The audio runs on its own thread like a host's, with FL-like buffers (512, and odd varying sizes), at 44.1 and
// 48 kHz, with the transport stopped and playing. Every check is that you HEAR something, or that the status
// line says why not.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <cstdio>

namespace hostscen
{
using Cmd = std::function<juce::String (const char*, const juce::String&)>;
using Check = std::function<void (bool, const juce::String&, const juce::String&)>;

struct PlayHead : juce::AudioPlayHead
{
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> samples { 0 };
    double rate = 44100;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing.load());
        p.setTimeInSamples (samples.load());
        p.setTimeInSeconds ((double) samples.load() / rate);
        p.setBpm (120.0);
        return p;
    }
};

// the host's audio thread: calls processBlock about twice as fast as real time, FL-like block sizes
struct AudioThread : juce::Thread
{
    AudioThread (juce::AudioProcessor& p, PlayHead& h, double sr, bool varying)
        : juce::Thread ("host audio"), proc (p), head (h), rate (sr), vary (varying) {}
    ~AudioThread() override { stopThread (4000); }
    void run() override
    {
        static const int sizes[] = { 512, 441, 37, 256, 512, 1, 300, 128, 499, 64 };
        juce::AudioBuffer<float> io (juce::jmax (2, proc.getTotalNumOutputChannels()), 512);
        juce::MidiBuffer midi;
        int k = 0;
        double owed = 0;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            const int n = vary ? sizes[(size_t) (k++ % 10)] : 512;
            io.setSize (io.getNumChannels(), n, false, false, true);
            io.clear();
            midi.clear();
            const int note = noteOn.exchange (-1);
            if (note >= 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
            if (noteOff.exchange (false)) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            {
                const juce::ScopedLock l (lock);
                proc.processBlock (io, midi);
            }
            float pk = 0;
            for (int c = 0; c < io.getNumChannels(); ++c) pk = juce::jmax (pk, io.getMagnitude (c, 0, n));
            if (! std::isfinite (pk)) bad = true;
            peak.store (juce::jmax (peak.load(), pk));
            blocks.fetch_add (1);
            if (head.playing.load()) head.samples.fetch_add (n);
            owed += n / rate * 1000.0 / 2.0;   // (twice real time)
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (t0 + owed > now + 1) juce::Thread::sleep ((int) (t0 + owed - now));
        }
    }
    float takePeak() { return peak.exchange (0); }
    juce::AudioProcessor& proc;
    PlayHead& head;
    double rate;
    bool vary;
    juce::CriticalSection lock;
    std::atomic<float> peak { 0 };
    std::atomic<int> noteOn { -1 };
    std::atomic<bool> noteOff { false }, bad { false };
    std::atomic<juce::int64> blocks { 0 };
};

inline juce::File writeWav (const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& b, double sr)
{
    auto f = dir.getChildFile (name + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> wr (w);
        wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    return f;
}

// sounds of our own (synthesised here, nothing recorded): a few hits and tones as sources, a short melody as target
inline juce::AudioBuffer<float> tone (double sr, double secs, double hz, double decay, double noise, int seed)
{
    juce::Random r (seed);
    juce::AudioBuffer<float> b (1, (int) (sr * secs));
    double ph = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        ph += hz * (1.0 + 0.3 * std::exp (-t * 30)) / sr;
        const double env = std::exp (-t * decay) * juce::jmin (1.0, t * 400);
        b.setSample (0, i, (float) (0.5 * env * ((1 - noise) * std::sin (juce::MathConstants<double>::twoPi * ph) + noise * (r.nextFloat() * 2 - 1))));
    }
    return b;
}
inline juce::AudioBuffer<float> melody (double sr, double secs)
{
    juce::AudioBuffer<float> b (2, (int) (sr * secs));
    b.clear();
    const double notes[] = { 220, 277.2, 329.6, 440, 329.6, 246.9, 293.7, 370 };
    const int step = (int) (sr * 0.25);
    for (int k = 0, at = 0; at < b.getNumSamples(); ++k, at += step)
    {
        auto t = tone (sr, 0.5, notes[k % 8] * (k % 16 < 8 ? 1 : 0.5), 6, 0.05, k);
        const int n = juce::jmin (t.getNumSamples(), b.getNumSamples() - at);
        for (int c = 0; c < 2; ++c) b.addFrom (c, at, t, 0, 0, n, 0.7f);
    }
    return b;
}

struct Env
{
    juce::AudioProcessor& proc;
    Cmd cmd;
    Check check;
    std::function<void (int)> pump;     // run the host's message loop for n ms (the screen, timers)
    juce::String label;
    bool expectScreen = true;            // (Linux: a hosted plug-in's own window is not tried; the hook then calls what the button calls)
};

// waits (pumping the message loop) until `done` or the timeout; returns the seconds it took, or -1
inline double waitFor (Env& e, std::function<bool()> done, double timeoutS)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < timeoutS * 1000)
    {
        if (done()) return (juce::Time::getMillisecondCounterHiRes() - t0) / 1000;
        e.pump (10);
    }
    return -1;
}
inline bool idle (Env& e, double timeoutS = 300) { return waitFor (e, [&] { return e.cmd ("busy", {}) == "0"; }, timeoutS) >= 0; }
// press GENERATE (the button's click arrives through the message loop, as a mouse click does) and wait until it took
inline bool generate (Env& e, AudioThread& a)
{
    a.takePeak();
    e.cmd ("generate", {});
    return waitFor (e, [&] { return e.cmd ("pending", {}) == "1"; }, 10) >= 0;
}
inline float listen (Env& e, AudioThread& a, double seconds)
{
    a.takePeak();
    e.pump ((int) (seconds * 1000));
    return a.takePeak();
}

inline void run (Env& e, double sr, bool varyingBlocks, const juce::File& dir)
{
    auto& p = e.proc;
    const auto L = " [" + e.label + ", " + juce::String ((int) sr) + " Hz, " + (varyingBlocks ? "varying buffers" : "512 buffers") + "]";
    // ---- the buses FL Studio sees
    e.check (p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2 && p.getBusCount (false) >= 1
                 && p.getChannelLayoutOfBus (false, 0) == juce::AudioChannelSet::stereo() && p.acceptsMidi(),
             "an instrument with stereo out, no audio in, MIDI in" + L,
             juce::String (p.getTotalNumInputChannels()) + " in, " + juce::String (p.getTotalNumOutputChannels()) + " out");

    PlayHead head; head.rate = sr;
    p.setPlayHead (&head);
    p.setRateAndBufferSizeDetails (sr, 512);
    p.prepareToPlay (sr, 512);
    AudioThread audio (p, head, sr, varyingBlocks);
    audio.startThread (juce::Thread::Priority::highest);
    const bool screen = e.cmd ("editor", {}) == "1";
    if (e.expectScreen) e.check (screen, "the screen is open (the checks press its own GENERATE button)" + L, {});

    // ---- 1. a new instance opens ready to play, and GENERATE plays with the transport stopped
    idle (e);
    const auto first = e.cmd ("state", {});
    e.check (e.cmd ("sounds", {}).startsWith ("0 ") == false && first.startsWith ("Ready"),
             "a new instance opens ready to play (built-in sounds and target)" + L, e.cmd ("sounds", {}) + "; status line: " + first);
    e.check (listen (e, audio, 0.5) == 0.0f, "a new instance is quiet until you press something" + L, {});
    generate (e, audio);
    double t = waitFor (e, [&] { return audio.takePeak() > 0.01f; }, 120);
    e.check (t >= 0, "GENERATE on a new instance plays (DAW transport stopped)" + L, "sound after " + juce::String (t, 2) + " s; " + e.cmd ("state", {}));
    e.pump (300);
    e.check (e.cmd ("state", {}).startsWith ("Playing"), "the status line says Playing" + L, e.cmd ("state", {}));

    // ---- 2. your own sources, NO target: GENERATE uses the built-in target and plays
    juce::StringArray srcs;
    srcs.add (writeWav (dir, "hit-a", tone (sr, 0.6, 110, 8, 0.6, 1), sr).getFullPathName());
    srcs.add (writeWav (dir, "hit-b", tone (sr, 0.4, 900, 14, 0.8, 2), sr).getFullPathName());
    srcs.add (writeWav (dir, "pluck", tone (sr, 1.5, 330, 3, 0.05, 3), sr).getFullPathName());
    srcs.add (writeWav (dir, "pad", tone (sr, 2.5, 196, 0.5, 0.1, 4), sr).getFullPathName());
    e.cmd ("clear", {}); idle (e); e.pump (200);
    e.check (e.cmd ("state", {}).startsWith ("Silent: no source sounds"), "with nothing loaded the status line says why it is silent" + L, e.cmd ("state", {}));
    e.cmd ("sources", srcs.joinIntoString ("\n")); idle (e); e.pump (200);
    e.check (e.cmd ("state", {}).startsWith ("Silent: no target"), "sources but no target: the status line says so" + L, e.cmd ("state", {}));
    generate (e, audio);
    t = waitFor (e, [&] { return audio.takePeak() > 0.01f; }, 120);
    e.check (t >= 0 && e.cmd ("notice", {}).contains ("built-in"), "sources, no target: GENERATE uses the built-in target and plays" + L,
             "sound after " + juce::String (t, 2) + " s; " + e.cmd ("sounds", {}) + "; note: " + e.cmd ("notice", {}));

    // ---- 3. your own target
    e.cmd ("target", writeWav (dir, "melody", melody (sr, 6), sr).getFullPathName());
    idle (e);
    generate (e, audio);
    t = waitFor (e, [&] { return audio.takePeak() > 0.01f; }, 120);
    e.check (t >= 0 && e.cmd ("sounds", {}).contains ("'melody'"), "sources + your target: GENERATE plays" + L, "sound after " + juce::String (t, 2) + " s; " + e.cmd ("sounds", {}));

    // ---- 4. PLAY BY = DAW play: silent with the transport stopped (and the screen says why), plays when it plays
    e.cmd ("set", "play_mode=2");
    generate (e, audio); idle (e);
    waitFor (e, [&] { return e.cmd ("state", {}).contains ("DAW play"); }, 30);
    const float stopped = listen (e, audio, 1.0);
    e.check (stopped == 0.0f && e.cmd ("state", {}).contains ("DAW play"), "PLAY BY DAW play, transport stopped: silent, and the status line says why" + L,
             "peak " + juce::String (stopped) + "; " + e.cmd ("state", {}));
    head.playing = true;
    t = waitFor (e, [&] { return audio.takePeak() > 0.01f; }, 10);
    e.check (t >= 0, "PLAY BY DAW play: plays when the transport plays" + L, "sound after " + juce::String (t, 2) + " s");
    head.playing = false;
    e.pump (300);

    // ---- 5. PLAY BY = MIDI keys: the status line says hold a note; a note plays
    e.cmd ("set", "play_mode=1"); e.pump (300); audio.takePeak();
    e.check (listen (e, audio, 0.5) == 0.0f && e.cmd ("state", {}).contains ("MIDI keys"), "PLAY BY MIDI keys: silent until a note, and the status line says so" + L, e.cmd ("state", {}));
    audio.noteOn = 60;
    t = waitFor (e, [&] { return audio.takePeak() > 0.01f; }, 10);
    e.check (t >= 0, "PLAY BY MIDI keys: a held note plays" + L, "sound after " + juce::String (t, 2) + " s");
    audio.noteOff = true;
    e.cmd ("set", "play_mode=0");
    e.pump (200);

    // ---- 6. a long song as a source: a clear note, a progress bar with time left, the screen keeps moving,
    //         GENERATE pressed during the build plays as soon as that one build is done
    {
        const double secs = juce::SystemStats::getEnvironmentVariable ("MOSAIC_LONG_SECONDS", "50").getDoubleValue();
        e.cmd ("stop", {}); e.cmd ("clear", {}); idle (e);   // (stopped: the sound below can only come from GENERATE)
        e.cmd ("sources", writeWav (dir, "long-song", melody (sr, secs), sr).getFullPathName());
        e.check (waitFor (e, [&] { return e.cmd ("notice", {}).contains ("Tip"); }, 60) >= 0, "a long source gets a gentle note with the build time" + L, e.cmd ("notice", {}));
        e.check (waitFor (e, [&] { return e.cmd ("state", {}).startsWith ("Building library"); }, 60) >= 0, "the status line shows the library build" + L, e.cmd ("state", {}));
        generate (e, audio);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        double worstPump = 0; bool sawLeft = false, sawPct = false; juce::String sample;
        audio.takePeak();
        double heard = -1;
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 900000)
        {
            const auto a = juce::Time::getMillisecondCounterHiRes();
            e.pump (20);
            worstPump = juce::jmax (worstPump, juce::Time::getMillisecondCounterHiRes() - a - 20);
            const auto st = e.cmd ("state", {});
            if (st.contains ("left")) { sawLeft = true; sample = st; }
            if (st.contains ("%") && ! st.contains ("Building library... 0%")) sawPct = true;
            if (audio.takePeak() > 0.01f) { heard = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000; break; }
        }
        e.check (sawLeft && sawPct && sample.contains ("then it plays"), "while it builds: the percentage and the time left, and that it plays after" + L, sample);
        e.check (heard >= 0, "the long song's library finishes and plays by itself (GENERATE pressed during the build)" + L,
                 "sound " + juce::String (heard, 1) + " s after GENERATE; " + e.cmd ("status", {}));
        e.check (worstPump < 250, "the screen doesn't freeze while it builds" + L, "slowest message-loop turn " + juce::String (worstPump, 0) + " ms");
    }

    e.check (! audio.bad.load(), "no NaN / inf in the output" + L, juce::String (audio.blocks.load()) + " buffers");
    audio.stopThread (4000);
    p.releaseResources();
    p.setPlayHead (nullptr);
}
}
