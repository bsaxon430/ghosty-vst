/*
 * Six Sines
 *
 * A synth with audio rate modulation.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/six-sines
 */

#include "configuration.h"
#include <clap/clap.h>
#include <chrono>

#include <clap/helpers/plugin.hh>
#include "synth/synth.h"
#include "presets/preset-manager.h"

#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hxx>

#include <memory>
#include "sst/plugininfra/patch-support/patch_base_clap_adapter.h"
#include "sst/plugininfra/cpufeatures.h"

#include "sst/voicemanager/midi1_to_voicemanager.h"
#include "sst/clap_juce_shim/clap_juce_shim.h"

#include "ui/six-sines-editor.h"
#include "ghostblock/p2-editor.h" // Poltergeist 2: the GhostBlock editor (the first one; -DP2_CLASSIC_EDITOR=ON)
#include "ghostblock/machine/machine-editor.h" // Poltergeist 2: the ghost-machine editor (the default)
#include "ghostblock/machine/sizing.h"        // Poltergeist 2: its window size (75 - 200 %, fixed shape)
#include "ghostblock/morph.h"     // Poltergeist 2: MORPH, the blend board
#include "ghostblock/spectral/spectral-fx.h" // SPECTRAL FX: the spectral effect rack after the synth
#include "ghostblock/sub/sub-bass.h"         // SUB: the sub-bass layer under the synth
#include "libMTSClient.h"                    // SUB: the note's retuning, read as the engine's voices read it

#include <sst/cpputils/rtsan_support.h>

#include <clapwrapper/vst3.h>
#include <clapwrapper/auv2.h>
#include <numeric>
#include <algorithm>

namespace baconpaul::six_sines
{

extern const clap_plugin_descriptor *getDescriptor();
extern const clap_plugin_descriptor *getMultiOutDescriptor();

namespace clapimpl
{

static constexpr clap::helpers::MisbehaviourHandler misLevel =
    clap::helpers::MisbehaviourHandler::Ignore;
static constexpr clap::helpers::CheckingLevel checkLevel = clap::helpers::CheckingLevel::Maximal;

using plugHelper_t = clap::helpers::Plugin<misLevel, checkLevel>;

template <bool multiOut>
struct SixSinesClap : public plugHelper_t, sst::clap_juce_shim::EditorProvider
{
    SixSinesClap(const clap_host *h)
        : plugHelper_t(multiOut ? getMultiOutDescriptor() : getDescriptor(), h)
    {
        engine = std::make_unique<Synth>(multiOut);

        engine->clapHost = h;

        clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
#if P2_CLASSIC_EDITOR
        clapJuceShim->setResizable(false); // Poltergeist 2: one fixed size that fits a 1440 x 900 screen
#else
        clapJuceShim->setResizable(true); // Poltergeist 2 machine: 75 - 200 %, fixed shape (ghostblock/machine/sizing.h)
#endif
    }
    virtual ~SixSinesClap() {};

    std::unique_ptr<Synth> engine;
    size_t blockPos{0};
    // Poltergeist 2: notes from the editor's on-screen keyboard (played like host notes)
    ghostblock::p2::UiNoteQueue uiNotes;
    // Poltergeist 2: MORPH (the blend board): up to four sounds, each its own unchanged engine, mixed by the puck
    ghostblock::p2::Morph morph;
    static constexpr int kMix = 1 + ghostblock::p2::Morph::N; // the main engine + the corner engines
    std::vector<float> mixBuf[kMix][2];
    std::vector<float> mixOut[kMix][2]; // each source's share of the mix (its gain applied), for the BLEND GUARD
    uint32_t mixCap{0};
    std::array<size_t, ghostblock::p2::Morph::N> cornerBlockPos{};
    float mixGain[kMix][2]{{1.f, 1.f}};
    float mixCoef{0.001f};
    // SPECTRAL FX: the rack on the plugin's stereo output (after the engine and MORPH's mix). Off = output untouched.
    ghostblock::p2::spectral::SpectralFx spectral;
    uint32_t spectralLatencyTold{0};
    void spectralMain() // main thread: build what changed; tell the host what it must re-read
    {
        if (spectral.serviceMainThread() && _host.canUseParams())
        {
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
            _host.paramsRescan(CLAP_PARAM_RESCAN_INFO);
        }
        if (isActive() && spectral.takeRestartRequest())
            _host.host()->request_restart(_host.host()); // the latency changes at the next activate()
    }
    bool spectralParamEvent(const clap_event_header_t *ev)
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID || ev->type != CLAP_EVENT_PARAM_VALUE)
            return false;
        auto pv = reinterpret_cast<const clap_event_param_value *>(ev);
        if (!ghostblock::p2::spectral::SpectralFx::isParam(pv->param_id))
            return false;
        spectral.setParam(pv->param_id, pv->value);
        return true;
    }
    void spectralProcess(const clap_process *process) noexcept // audio thread, after everything else
    {
        spectral.sendToHost(process->out_events);
        if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32)
        {
            auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();
            spectral.process(process->audio_outputs[0].data32[0], process->audio_outputs[0].data32[1],
                             process->frames_count, process->transport ? process->transport->tempo : 120.0);
        }
        if (spectral.takeRestartRequest())
            _host.host()->request_restart(_host.host());
        if (spectral.takeMainThreadRequest())
            _host.host()->request_callback(_host.host());
    }
    bool implementsLatency() const noexcept override { return true; }
    uint32_t latencyGet() const noexcept override { return spectral.latency(); }

    // SUB: the sub-bass layer (src/ghostblock/sub). It follows the note the main engine plays and joins the output
    // after SPECTRAL FX, as late as the rack's latency. Off = the output is not read or written.
    ghostblock::p2::sub::SubBass sub;
    uint32_t subSerial{0};        // counts the notes that began a new attack
    uint32_t subKeySerial[128]{}; // the attack each key belongs to
    bool subKeyDown[128]{};
    int subKeysDown{0};
    // a key went down / up (audio thread; host notes, MIDI, the on-screen keyboard)
    void subNote(int key, bool on) noexcept
    {
        if (key < 0 || key > 127)
            return;
        if (on)
        {
            // a mono patch plays a key pressed over a held one legato (no new attack); everything else attacks
            const bool legato = engine->patch.output.playMode.value > 0.5f && subKeysDown > 0;
            subKeySerial[key] = legato ? subSerial : ++subSerial;
            if (!subKeyDown[key])
            {
                subKeyDown[key] = true;
                ++subKeysDown;
            }
        }
        else if (subKeyDown[key])
        {
            subKeyDown[key] = false;
            --subKeysDown;
        }
    }
    void subMidi(const uint8_t *d) noexcept
    {
        const uint8_t st = d[0] & 0xF0;
        if (st == 0x90 && d[2] > 0)
            subNote(d[1] & 0x7F, true);
        else if (st == 0x80 || st == 0x90)
            subNote(d[1] & 0x7F, false);
    }
    // After an engine block: which voice the sub follows - the lowest key that is held (else the lowest still
    // ringing out) - and that voice's pitch, exactly as Voice::renderBlock tunes it, and its volume envelope.
    template <size_t BS> void subTrackT(uint32_t frame) noexcept
    {
        ghostblock::p2::sub::SubBass::Track t;
        Voice<BS> *best = nullptr;
        bool bestHeld = false;
        float bestEnv = 0.f;
        for (auto *v = engine->template headAs<BS>(); v; v = v->nextVoice())
        {
            const bool held = v->voiceValues.gated && v->fadeBlocks < 0;
            const float e = v->out.env.outputCache[BS - 1];
            bool better;
            if (!best)
                better = true;
            else if (held != bestHeld)
                better = held;
            else if (v->voiceValues.key != best->voiceValues.key)
                better = v->voiceValues.key < best->voiceValues.key;
            else
                better = e > bestEnv; // (unison: the copies of one key share its pitch; the loudest speaks for them)
            if (better)
            {
                best = v;
                bestHeld = held;
                bestEnv = e;
            }
        }
        if (best)
        {
            const auto &vv = best->voiceValues;
            const auto &mv = engine->monoValues;
            auto &o = best->out;
            float base = (float)vv.key, total = (float)vv.key;
            if (mv.mtsHasMaster)
            {
                const float rt = (float)MTS_RetuningInSemitones(mv.mtsClient, (char)vv.key, (char)vv.channel);
                base += rt;
                if (mv.mpeActive)
                {
                    const float bend = vv.mpeBendInSemisLag.v;
                    const float floatKey = (float)vv.key + bend;
                    const int loKey = std::clamp((int)std::floor(floatKey), 0, 126);
                    const float frac = std::clamp(floatKey - (float)loKey, 0.f, 1.f);
                    const float lo = (float)MTS_RetuningInSemitones(mv.mtsClient, (char)loKey, (char)vv.channel);
                    const float hi = (float)MTS_RetuningInSemitones(mv.mtsClient, (char)(loKey + 1), (char)vv.channel);
                    total += bend + (1.f - frac) * lo + frac * hi;
                }
                else
                    total += rt;
            }
            else
                total += vv.mpeBendInSemisLag.v;
            total += ((mv.pitchBend >= 0) ? o.bendUp : o.bendDown) * mv.pitchBend;
            total += vv.portaDiff * (float)vv.portaSign;
            total += vv.noteExpressionTuningInSemisLag.v;
            total += o.fineTune * 0.01f + o.ftModNode.coarseTune + o.ftModNode.level * 2 + o.ftModNode.coarseLevel;
            static constexpr float octFac[7] = {1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f, 2.0f, 4.0f, 8.0f};
            const int octSh = std::clamp((int)std::round(o.octTranspose), -3, 3);
            t.sounding = true;
            t.key = vv.key;
            t.hz = 440.0f * std::exp2((base - 69.0f) / 12.0f) * octFac[octSh + 3];
            t.modSemis = total - base;
            t.engineGlides = vv.portaDiff > 1e-5f;
            t.env = std::clamp(bestEnv * (1.f - o.velSen * (1.f - vv.velocityLag.v)), 0.f, 1.f);
            t.attack = subKeySerial[vv.key & 127];
        }
        sub.track(frame, t);
    }
    void subTrack(uint32_t frame) noexcept
    {
        if (!sub.wantsTrack())
            return;
        switch (engine->blockSize)
        {
        case 16:
            subTrackT<16>(frame);
            break;
        case 32:
            subTrackT<32>(frame);
            break;
        default:
            subTrackT<8>(frame);
            break;
        }
    }
    bool subParamEvent(const clap_event_header_t *ev)
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID || ev->type != CLAP_EVENT_PARAM_VALUE)
            return false;
        auto pv = reinterpret_cast<const clap_event_param_value *>(ev);
        if (!ghostblock::p2::sub::SubBass::isParam(pv->param_id))
            return false;
        sub.setParam(pv->param_id, pv->value);
        return true;
    }
    void subProcess(const clap_process *process) noexcept // audio thread, after SPECTRAL FX
    {
        sub.sendToHost(process->out_events);
        if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32)
        {
            auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();
            sub.process(process->audio_outputs[0].data32[0], process->audio_outputs[0].data32[1], process->frames_count,
                        spectral.latency());
        }
    }

  protected:
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        // The audio thread is stopped here; seed it from the main-thread source of truth. The DAW
        // session state (MPE / smoothing) is seeded at construction and thereafter reaches the
        // audio thread only through the SET_AUDIO_DAW_STATE queue, so it is not touched here.
        engine->patch.copyValuesFrom(engine->patchMain);
        engine->setSampleRate(sampleRate);
        // Poltergeist 2: MORPH buffers + the corner engines at this rate (audio is stopped here)
        morph.activate(sampleRate);
        mixCap = std::max<uint32_t>(maxFrameCount, 1);
        for (auto &b : mixBuf)
            for (auto &c : b)
                c.assign(mixCap, 0.f);
        for (auto &b : mixOut)
            for (auto &c : b)
                c.assign(mixCap, 0.f);
        mixCoef = 1.f - std::exp(-1.f / (0.02f * (float)sampleRate)); // ~20 ms glide on every gain
        cornerBlockPos.fill(0);
        // SPECTRAL FX: built for this rate; its latency (0 when off) may only change here
        sub.activate(sampleRate); // SUB
        spectral.activate(sampleRate);
        if (spectral.latency() != spectralLatencyTold)
        {
            spectralLatencyTold = spectral.latency();
            if (_host.canUseLatency())
                _host.latencyChanged();
        }
        return true;
    }

    void onMainThread() noexcept override
    {
        engine->onMainThread();
        spectralMain(); // SPECTRAL FX
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        return isInput ? 1 : (multiOut ? 7 : 1);
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (isInput)
        {
            if (index != 0)
                return false;
            info->id = 82649;
            info->in_place_pair = CLAP_INVALID_ID;
            strncpy(info->name, "Audio In", sizeof(info->name));
            info->flags = 0;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            return true;
        }
        if (index > (multiOut ? 6 : 0))
            return false;
        info->id = 75241 + index;
        info->in_place_pair = CLAP_INVALID_ID;
        if (index == 0)
            strncpy(info->name, "Main Out", sizeof(info->name));
        else
            snprintf(info->name, sizeof(info->name) - 1, "Operator %d", index);
        if (index == 0)
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        else
            info->flags = 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    bool implementsAudioPortsActivation() const noexcept override { return true; }
    bool audioPortsActivationCanActivateWhileProcessing() const noexcept override { return true; }
    bool audioPortsActivationSetActive(bool is_input, uint32_t port_index, bool is_active,
                                       uint32_t sample_size) noexcept override
    {
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return isInput ? 1 : 0; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        assert(isInput);
        assert(index == 0);
        if (!isInput || index != 0)
            return false;

        info->id = 17252;
        info->supported_dialects =
            CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE | CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        strncpy(info->name, "Note Input", CLAP_NAME_SIZE - 1);
        return true;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        const auto status = process_nonblocking(process);
        spectralProcess(process); // SPECTRAL FX: the last thing on the engine's output
        subProcess(process);      // SUB: joins after the rack, so nothing in the plugin processes it
        return status;
    }

    clap_process_status process_nonblocking(const clap_process *process) SST_CPPUTILS_NONBLOCKING
    {
        auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();

        auto ev = process->in_events;
        auto outq = process->out_events;
        auto sz = ev->size(ev);

        const clap_event_header_t *nextEvent{nullptr};
        uint32_t nextEventIndex{0};
        if (sz != 0)
        {
            nextEvent = ev->get(ev, nextEventIndex);
        }

        if (process->transport)
        {
            engine->monoValues.tempoSyncRatio = process->transport->tempo / 120.0;
            auto tflags = process->transport->flags;
            // Only trust the song position when the host is both playing and exposes a
            // seconds timeline; otherwise free-run our own clock.
            engine->monoValues.isPlayingAndHasSecondsTimeline =
                (tflags & CLAP_TRANSPORT_IS_PLAYING) &&
                (tflags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE);
            if (engine->monoValues.isPlayingAndHasSecondsTimeline)
            {
                engine->monoValues.hostSongPosSeconds =
                    process->transport->song_pos_seconds / (double)CLAP_SECTIME_FACTOR;
                // Anchor here, before any events are dispatched. A note-on at frame 0 of
                // this buffer attacks (and snapshots its SONGPOS LFO phase) before the first
                // engine->process() runs, so the resync inside processInternal would land too
                // late and the LFO would read the stale free-run value.
                engine->monoValues.songPosSeconds = engine->monoValues.hostSongPosSeconds;
            }
        }
        else
        {
            engine->monoValues.tempoSyncRatio = 1.f;
            engine->monoValues.isPlayingAndHasSecondsTimeline = false;
        }
        // Re-anchor songPosSeconds to the host position on the first engine block of this
        // buffer; subsequent blocks advance from there. (Also done above so frame-0 note-ons
        // see the anchored value; this suppresses the per-block advance on the first block.)
        engine->monoValues.songPosNeedsResync = true;

        // the host's other work between buffers has usually evicted our tables
        engine->preloadTables(sz != 0);
        engine->refreshMtsMaster();

        // Poltergeist 2: the on-screen keyboard, at the start of this buffer
        ghostblock::p2::UiNote uiN[64];
        int nUi = 0;
        while (nUi < 64)
        {
            auto un = uiNotes.pop();
            if (!un)
                break;
            uiN[nUi++] = *un;
        }
        for (int i = 0; i < nUi; ++i)
        {
            // MORPH's OCTAVE: THIS SOUND's corner moves the main engine's keys (only while MORPH is on)
            const int key = uiN[i].on ? morph.keyOn(0, 0, uiN[i].key) : morph.keyHeld(0, 0, uiN[i].key);
            subNote(key, uiN[i].on); // SUB
            if (uiN[i].on)
                engine->voiceManager->processNoteOnEvent(0, 0, key, -1, uiN[i].velocity, 0.f);
            else
                engine->voiceManager->processNoteOffEvent(0, 0, key, -1, 0.f);
        }
        morph.sendToHost(process->out_events); // Poltergeist 2: the editor's MORPH edits, as host gestures
        if constexpr (!multiOut)
        {
            // Poltergeist 2: MORPH on = every engine plays the notes, the puck mixes them (MORPH off: as upstream)
            if (morph.on.load(std::memory_order_relaxed))
                return processMorph(process, uiN, nUi);
        }

        static constexpr int outBus{multiOut ? 1 + numOps : 1};
        static constexpr int outChan{multiOut ? (1 + numOps) * 2 : 2};
        float *out[outChan];
        for (auto i = 0; i < outBus; ++i)
        {
            auto lo = process->audio_outputs[i].data32;
            out[2 * i] = lo[0];
            out[2 * i + 1] = lo[1];
        }

        const float *audioInL{nullptr}, *audioInR{nullptr};
        if (process->audio_inputs_count > 0 && process->audio_inputs[0].data32)
        {
            audioInL = process->audio_inputs[0].data32[0];
            audioInR = process->audio_inputs[0].data32[1];
        }

        for (auto s = 0U; s < process->frames_count; ++s)
        {
            engine->pushAudioIn(audioInL ? audioInL[s] : 0.f, audioInR ? audioInR[s] : 0.f);

            if (blockPos == 0)
            {
                // Only realy need to run events when we do the block process
                while (nextEvent && nextEvent->time <= s)
                {
                    handleEvent(nextEvent);
                    nextEventIndex++;
                    if (nextEventIndex < sz)
                        nextEvent = ev->get(ev, nextEventIndex);
                    else
                        nextEvent = nullptr;
                }

                engine->process(outq);
                subTrack(s); // SUB: what the engine plays from here on
            }

            for (auto i = 0; i < outChan; ++i)
                out[i][s] = engine->output[i][blockPos];

            blockPos++;
            if (blockPos == engine->blockSize)
            {
                blockPos = 0;
            }
        }

        while (nextEvent)
        {
            handleEvent(nextEvent);
            nextEventIndex++;
            if (nextEventIndex < sz)
                nextEvent = ev->get(ev, nextEventIndex);
            else
                nextEvent = nullptr;
        }
        return CLAP_PROCESS_CONTINUE;
    }

    // ---- Poltergeist 2: MORPH ----------------------------------------------------------------------------------
    bool morphParamEvent(const clap_event_header_t *ev)
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID || ev->type != CLAP_EVENT_PARAM_VALUE)
            return false;
        auto pv = reinterpret_cast<const clap_event_param_value *>(ev);
        if (!ghostblock::p2::Morph::isParam(pv->param_id))
            return false;
        morph.setParam(pv->param_id, pv->value);
        morph.changed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // Each engine runs exactly as the upstream loop runs the main one (events at its block starts), into its own
    // buffer; then the puck's gains (smoothed) mix them. Corner engines get the notes / MIDI / expressions, never
    // the host's parameter changes (those belong to THIS SOUND).
    bool handleNoteEventFor(Synth &e, int slot, const clap_event_header_t *ev)
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return true;
        auto &vm = e.voiceManager;
        switch (ev->type)
        {
        case CLAP_EVENT_MIDI:
        {
            uint8_t d[3];
            morph.midi(slot, reinterpret_cast<const clap_event_midi *>(ev)->data, d); // the corner's OCTAVE
            sst::voicemanager::applyMidi1Message(*vm, reinterpret_cast<const clap_event_midi *>(ev)->port_index, d);
        }
            break;
        case CLAP_EVENT_NOTE_ON:
        {
            auto n = reinterpret_cast<const clap_event_note *>(ev);
            vm->processNoteOnEvent(n->port_index, n->channel, morph.keyOn(slot, n->channel, n->key), n->note_id, n->velocity, 0.f);
        }
        break;
        case CLAP_EVENT_NOTE_OFF:
        {
            auto n = reinterpret_cast<const clap_event_note *>(ev);
            vm->processNoteOffEvent(n->port_index, n->channel, morph.keyHeld(slot, n->channel, n->key), n->note_id, n->velocity);
        }
        break;
        case CLAP_EVENT_NOTE_EXPRESSION:
        {
            auto n = reinterpret_cast<const clap_event_note_expression *>(ev);
            vm->routeNoteExpression(n->port_index, n->channel, n->key < 0 ? n->key : morph.keyHeld(slot, n->channel, n->key), n->note_id, n->expression_id, n->value);
        }
        break;
        default:
            break;
        }
        return true;
    }
    template <bool isMain>
    void runEngine(Synth &e, size_t &bp, const clap_process *process, float *L, float *R, int slot = 0)
    {
        auto ev = process->in_events;
        const auto sz = ev->size(ev);
        uint32_t ni = 0;
        const clap_event_header_t *next = sz ? ev->get(ev, 0) : nullptr;
        const float *inL{nullptr}, *inR{nullptr};
        if (process->audio_inputs_count > 0 && process->audio_inputs[0].data32)
        {
            inL = process->audio_inputs[0].data32[0];
            inR = process->audio_inputs[0].data32[1];
        }
        auto dispatch = [&](const clap_event_header_t *h) {
            if constexpr (isMain)
                handleEvent(h);
            else
                handleNoteEventFor(e, slot, h);
        };
        for (auto s = 0U; s < process->frames_count; ++s)
        {
            e.pushAudioIn(inL ? inL[s] : 0.f, inR ? inR[s] : 0.f);
            if (bp == 0)
            {
                while (next && next->time <= s)
                {
                    dispatch(next);
                    ++ni;
                    next = ni < sz ? ev->get(ev, ni) : nullptr;
                }
                if constexpr (isMain)
                {
                    e.process(process->out_events);
                    subTrack(s); // SUB: it follows THIS SOUND's engine
                }
                else
                    e.process(&quietOut);
            }
            L[s] = e.output[0][bp];
            R[s] = e.output[1][bp];
            if (++bp == e.blockSize)
                bp = 0;
        }
        while (next)
        {
            dispatch(next);
            ++ni;
            next = ni < sz ? ev->get(ev, ni) : nullptr;
        }
    }
    static bool quietPush(const clap_output_events *, const clap_event_header_t *) { return true; }
    const clap_output_events quietOut{nullptr, &quietPush};

    clap_process_status processMorph(const clap_process *process, const ghostblock::p2::UiNote *uiN, int nUi)
    {
        using ghostblock::p2::Morph;
        const uint32_t F = process->frames_count;
        float *oL = process->audio_outputs[0].data32[0], *oR = process->audio_outputs[0].data32[1];
        if (F > mixCap)
        { // (a host block bigger than it promised: play THIS SOUND alone rather than allocate here)
            runEngine<true>(*engine, blockPos, process, oL, oR);
            return CLAP_PROCESS_CONTINUE;
        }
        // the corner engines: the keyboard notes, the host's tempo, their tables
        std::array<Synth *, Morph::N> ce{};
        for (int c = 0; c < Morph::N; ++c)
        {
            auto *e = morph.kind[(size_t)c].load(std::memory_order_relaxed) == Morph::OWN ? morph.live[(size_t)c].load() : nullptr;
            ce[(size_t)c] = e;
            if (!e)
                continue;
            for (int i = 0; i < nUi; ++i)
            {
                // (the main loop above already moved the keys for THIS SOUND; the corner's own OCTAVE from the played key)
                const int raw = uiN[i].key;
                if (uiN[i].on)
                    e->voiceManager->processNoteOnEvent(0, 0, morph.keyOn(1 + c, 0, raw), -1, uiN[i].velocity, 0.f);
                else
                    e->voiceManager->processNoteOffEvent(0, 0, morph.keyHeld(1 + c, 0, raw), -1, 0.f);
            }
            e->monoValues.tempoSyncRatio = engine->monoValues.tempoSyncRatio;
            e->monoValues.isPlayingAndHasSecondsTimeline = engine->monoValues.isPlayingAndHasSecondsTimeline;
            e->monoValues.hostSongPosSeconds = engine->monoValues.hostSongPosSeconds;
            e->monoValues.songPosSeconds = engine->monoValues.songPosSeconds;
            e->monoValues.songPosNeedsResync = true;
            e->preloadTables(process->in_events->size(process->in_events) != 0);
            e->refreshMtsMaster();
        }
        // ORBIT: where the puck is this buffer (locked to the song's bars while it plays)
        {
            double beats = -1, tempo = 120;
            if (auto *t = process->transport)
            {
                tempo = t->tempo;
                if ((t->flags & CLAP_TRANSPORT_IS_PLAYING) && (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE))
                    beats = (double)t->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
            }
            morph.advance(beats, tempo, F, morph.sampleRate);
        }
        // the gains the puck asks for (equal power), with each corner's volume and pan
        float w[Morph::N]{};
        float tgt[kMix][2]{};
        if (morph.weights(w))
        {
            for (int c = 0; c < Morph::N; ++c)
            {
                const int k = morph.kind[(size_t)c].load(std::memory_order_relaxed);
                if (k == Morph::EMPTY || (k == Morph::OWN && !ce[(size_t)c]))
                    continue;
                const float g = std::sqrt(w[c]) * std::pow(10.f, morph.volDb[(size_t)c].load() / 20.f);
                const float p = std::clamp(morph.pan[(size_t)c].load(), -1.f, 1.f);
                const int slot = k == Morph::THIS_SOUND ? 0 : 1 + c;
                tgt[slot][0] += g * (p > 0 ? 1.f - p : 1.f);
                tgt[slot][1] += g * (p < 0 ? 1.f + p : 1.f);
            }
        }
        else
            tgt[0][0] = tgt[0][1] = 1.f;

        runEngine<true>(*engine, blockPos, process, mixBuf[0][0].data(), mixBuf[0][1].data());
        for (int c = 0; c < Morph::N; ++c)
            if (ce[(size_t)c])
                runEngine<false>(*ce[(size_t)c], cornerBlockPos[(size_t)c], process, mixBuf[1 + c][0].data(), mixBuf[1 + c][1].data(), 1 + c);
        // each source's share (its gain gliding), then the BLEND GUARD (phase + body) and the sum
        bool live[kMix]{};
        for (int m = 0; m < kMix; ++m)
        {
            const bool exists = m == 0 || ce[(size_t)(m - 1)] != nullptr;
            float pk = 0.f;
            for (uint32_t s = 0; s < F; ++s)
            {
                mixGain[m][0] += (tgt[m][0] - mixGain[m][0]) * mixCoef;
                mixGain[m][1] += (tgt[m][1] - mixGain[m][1]) * mixCoef;
                const float a = exists ? mixBuf[m][0][s] * mixGain[m][0] : 0.f, b = exists ? mixBuf[m][1][s] * mixGain[m][1] : 0.f;
                mixOut[m][0][s] = a;
                mixOut[m][1][s] = b;
                pk = std::max(pk, std::max(std::fabs(mixGain[m][0]), std::fabs(mixGain[m][1])));
            }
            live[m] = exists && (pk > 1e-5f || tgt[m][0] != 0.f || tgt[m][1] != 0.f); // (a share that has faded out is left out)
        }
        for (uint32_t s0 = 0; s0 < F; s0 += 512)
        {
            const int n = (int)std::min<uint32_t>(512, F - s0);
            float *sl[ghostblock::BlendGuard::kSrc]{}, *sr[ghostblock::BlendGuard::kSrc]{};
            for (int m = 0; m < kMix && m < ghostblock::BlendGuard::kSrc; ++m)
                if (live[m])
                {
                    sl[m] = mixOut[m][0].data() + s0;
                    sr[m] = mixOut[m][1].data() + s0;
                }
            morph.guard.phase(sl, sr, n);
            for (int s = 0; s < n; ++s)
            {
                float l = 0, r = 0;
                for (int m = 0; m < kMix; ++m)
                    if (live[m])
                    {
                        l += mixOut[m][0][s0 + s];
                        r += mixOut[m][1][s0 + s];
                    }
                oL[s0 + s] = l;
                oR[s0 + s] = r;
            }
            morph.guard.body(sl, sr, oL + s0, oR + s0, n);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    void reset() noexcept override
    {
        engine->voiceManager->allSoundsOff();
        for (auto &l : morph.live)
            if (auto *e = l.load())
                e->voiceManager->allSoundsOff();
        spectral.requestReset(); // SPECTRAL FX: forget tails
        sub.requestReset();      // SUB: silence
        for (auto &k : subKeyDown)
            k = false;
        subKeysDown = 0;
    }

    bool handleEvent(const clap_event_header_t *nextEvent)
    {
        auto &vm = engine->voiceManager;
        if (nextEvent->space_id == CLAP_CORE_EVENT_SPACE_ID)
        {
            switch (nextEvent->type)
            {
            case CLAP_EVENT_MIDI:
            {
                auto mevt = reinterpret_cast<const clap_event_midi *>(nextEvent);
                uint8_t d[3];
                morph.midi(0, mevt->data, d); // MORPH's OCTAVE on THIS SOUND's corner (nothing while MORPH is off)
                subMidi(d);                   // SUB
                sst::voicemanager::applyMidi1Message(*vm, mevt->port_index, d);
            }
            break;

            case CLAP_EVENT_NOTE_ON:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
                const int key = morph.keyOn(0, nevt->channel, nevt->key); // MORPH's OCTAVE (THIS SOUND's corner)
                subNote(key, true);                                         // SUB
                vm->processNoteOnEvent(nevt->port_index, nevt->channel, key, nevt->note_id,
                                       nevt->velocity, 0.f);
            }
            break;

            case CLAP_EVENT_NOTE_OFF:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
                const int key = morph.keyHeld(0, nevt->channel, nevt->key); // where it was played
                subNote(key, false);                                          // SUB
                auto nid = nevt->note_id;
                // nid = -1;
                vm->processNoteOffEvent(nevt->port_index, nevt->channel, key, nid,
                                        nevt->velocity);
            }
            break;
            case CLAP_EVENT_PARAM_VALUE:
            {
                if (morphParamEvent(nextEvent))
                    break; // Poltergeist 2: one of MORPH's own params
                if (spectralParamEvent(nextEvent))
                    break; // SPECTRAL FX: one of the rack's params
                if (subParamEvent(nextEvent))
                    break; // SUB: one of its params
                auto pevt = reinterpret_cast<const clap_event_param_value *>(nextEvent);
                auto par =
                    sst::plugininfra::patch_support::paramFromClapEvent<Param>(pevt, engine->patch);
                if (par)
                {
                    engine->handleParamValue(par, pevt->param_id, pevt->value);
                }
            }
            break;

            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                auto nevt = reinterpret_cast<const clap_event_note_expression *>(nextEvent);
                vm->routeNoteExpression(nevt->port_index, nevt->channel, nevt->key < 0 ? nevt->key : morph.keyHeld(0, nevt->channel, nevt->key), nevt->note_id,
                                        nevt->expression_id, nevt->value);
            }
            break;
            default:
            {
                SXSNLOG("Unknown inbound event of type " << nextEvent->type);
            }
            break;
            }
        }
        return true;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *ostream) noexcept override
    {
        // patchMain is authoritative. If no editor is open to keep it current, drain any pending
        // audio-thread updates into it first (we are the only consumer then). With the editor open,
        // the idle owns the queue, so patchMain can lag the audio thread by up to one idle tick
        // during an automation burst — a window of inconsistency, not a race.
        if (!engine->editorActive.load())
            engine->drainAudioToMainInto(engine->patchMain);

        if (!sst::plugininfra::patch_support::patchToOutStream(engine->patchMain, ostream, true))
            return false;
        // Poltergeist 2: MORPH rides after the patch's terminating zero (an upstream reader stops at that zero)
        const auto ms = morph.toState();
        const char *c = ms.c_str();
        int64_t left = (int64_t)ms.size() + 1;
        while (left > 0)
        {
            auto r = ostream->write(ostream, c, (uint64_t)left);
            if (r <= 0)
                return false;
            left -= r;
            c += r;
        }
        // SPECTRAL FX: its settings ride after MORPH's zero (older builds stop reading before them)
        const auto ss = spectral.toState();
        c = ss.c_str();
        left = (int64_t)ss.size() + 1;
        while (left > 0)
        {
            auto r = ostream->write(ostream, c, (uint64_t)left);
            if (r <= 0)
                return false;
            left -= r;
            c += r;
        }
        // SUB: its settings ride after SPECTRAL FX's zero (older builds stop reading before them)
        const auto bs = sub.toState();
        c = bs.c_str();
        left = (int64_t)bs.size() + 1;
        while (left > 0)
        {
            auto r = ostream->write(ostream, c, (uint64_t)left);
            if (r <= 0)
                return false;
            left -= r;
            c += r;
        }
        return true;
    }
    bool stateLoad(const clap_istream *istream) noexcept override
    {
        // Load into temps so a parse failure never leaves the engine half-written.
        auto tmp = std::make_unique<Patch>();
        Synth::DawStateMain loadedState{};
        tmp->dawExtraStateFrom = [&](TiXmlElement &e) { Synth::fromDawExtraState(e, loadedState); };

        // Poltergeist 2: read it all; the patch is the text up to the first zero, MORPH (if any) follows it
        std::string all;
        {
            char buf[4096];
            int64_t rd;
            while ((rd = istream->read(istream, buf, sizeof(buf))) > 0)
                all.append(buf, (size_t)rd);
        }
        if (all.empty())
            return false;
        const auto z = all.find('\0');
        tmp->fromState(all.substr(0, z));
        std::string morphState = z == std::string::npos ? std::string() : all.substr(z + 1);

        engine->patchMain.copyValuesFrom(*tmp);

        // 1.1-era sessions carry no <mpe> element; derive MPE from the legacy in-patch slots (now
        // in patchMain). Resolved on the main thread where patchMain is authoritative.
        if (!loadedState.main.mpeFromExtraState)
        {
            loadedState.audio.mpeActive = engine->patchMain.output.legacyMpeActive.value > 0.5f;
            loadedState.audio.mpeBendRange =
                (int)std::round(engine->patchMain.output.legacyMpeBendRange.value);
        }
        engine->dawStateMain = loadedState;
        engine->uiForceRebuild++; // an open editor rebuilds from patchMain
        // Poltergeist 2: the blend board (an older / upstream state has none: MORPH stays as it is)
        if (!morphState.empty() && morphState.rfind("P2MORPH", 0) == 0)
        {
            morph.fromState(morphState);
            for (auto &o : morph.owned)
                if (o)
                    o->reconcileWavetables();
        }

        // SPECTRAL FX: its section, if the state has one (a state from before it: the rack is off and empty)
        {
            bool found = false;
            for (auto pos = z; pos != std::string::npos && !found; pos = all.find('\0', pos + 1))
                if (all.compare(pos + 1, 11, "P2SPECTRAL ") == 0)
                    found = spectral.fromState(all.substr(pos + 1, all.find('\0', pos + 1) - pos - 1));
            if (!found)
                spectral.resetToDefaults();
            spectralMain();
        }
        // SUB: its section, if the state has one (a state from before it: SUB is off)
        {
            bool found = false;
            for (auto pos = z; pos != std::string::npos && !found; pos = all.find('\0', pos + 1))
                if (all.compare(pos + 1, 6, "P2SUB ") == 0)
                    found = sub.fromState(all.substr(pos + 1, all.find('\0', pos + 1) - pos - 1));
            if (!found)
                sub.resetToDefaults();
        }

        if (isActive())
        {
            // Push the loaded patch into the audio-thread `patch`; this also rescans the host.
            Synth::sendEntirePatchToAudio(engine->patchMain, engine->mainToAudio, _host.host());
        }
        else if (_host.canUseParams())
        {
            // Not running: the next activate() copies patchMain into patch. Just tell the host to
            // re-read the loaded values / names (it reads them from patchMain). INFO carries the
            // macro-name changes; issue it separately from VALUES|TEXT.
            _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
            _host.paramsRescan(CLAP_PARAM_RESCAN_INFO);
        }

        // Transport the loaded AudioDawState (mpe / smoothing) to the audio thread by value; the
        // handler stores the engine's queue-inbound copy and applies it into monoValues. Drains on
        // the next process (or the first process after activate, when inactive).
        Synth::MainToAudioMsg des{Synth::MainToAudioMsg::SET_AUDIO_DAW_STATE};
        des.audioDawState = engine->dawStateMain.audio;
        engine->mainToAudio.push(des);
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    // Poltergeist 2: the engine's params, then MORPH's own (ON, X, Y, ORBIT, RATE, SIZE) at the end
    uint32_t paramsCount() const noexcept override
    {
        return engine->patchMain.params.size() + ghostblock::p2::Morph::kParams +
               ghostblock::p2::spectral::SpectralFx::kParams + // SPECTRAL FX: the rack's params
               ghostblock::p2::sub::SubBass::kParams;          // SUB: its params come last
    }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (paramIndex >= engine->patchMain.params.size() + ghostblock::p2::Morph::kParams + ghostblock::p2::spectral::SpectralFx::kParams) // SUB
            return sub.paramInfo(paramIndex - (uint32_t)engine->patchMain.params.size() - ghostblock::p2::Morph::kParams -
                                     ghostblock::p2::spectral::SpectralFx::kParams,
                                 info);
        if (paramIndex >= engine->patchMain.params.size() + ghostblock::p2::Morph::kParams) // SPECTRAL FX
            return spectral.paramInfo(paramIndex - (uint32_t)engine->patchMain.params.size() - ghostblock::p2::Morph::kParams, info);
        if (paramIndex >= engine->patchMain.params.size())
            return morph.paramInfo(paramIndex - (uint32_t)engine->patchMain.params.size(), info);
        // All param reads come from patchMain (the main-thread source of truth); never
        // engine->patch.
        auto ok =
            sst::plugininfra::patch_support::patchParamsInfo(paramIndex, info, engine->patchMain);
        if (!ok)
            return ok;

        // patchParamsInfo cookies the patch it read, but the host hands the cookie back on
        // param events we resolve on the audio thread, so it must point into `patch`
        info->cookie = engine->clapCookieFor(info->id);

        // The macro amplitude param is tagged with isPrimaryMacroFeature; for that
        // single param swap the host-displayed name to "Foo (Macro N)" when the
        // user has renamed the macro. Every other macro param is left alone.
        auto *param = engine->patchMain.params[paramIndex];
        if (param->meta.hasFeature(isPrimaryMacroFeature))
        {
            int idx = (info->id - Patch::MacroNode::idBase) / Patch::MacroNode::idStride;
            if (idx >= 0 && idx < (int)numMacros)
            {
                const auto &nameBuf = engine->patchMain.macroNames[idx];
                std::string userName(nameBuf.data());
                auto def = Patch::MacroNode::defaultGroupName(idx);
                if (!userName.empty() && userName != def)
                {
                    auto fullName = userName + " (" + def + ")";
                    strncpy(info->name, fullName.c_str(), CLAP_NAME_SIZE - 1);
                    info->name[CLAP_NAME_SIZE - 1] = 0;
                }
            }
        }
        return ok;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        if (ghostblock::p2::Morph::isParam(paramId))
        {
            *value = morph.getParam(paramId);
            return true;
        }
        if (ghostblock::p2::spectral::SpectralFx::isParam(paramId)) // SPECTRAL FX
        {
            *value = spectral.getParam(paramId);
            return true;
        }
        if (ghostblock::p2::sub::SubBass::isParam(paramId)) // SUB
        {
            *value = sub.getParam(paramId);
            return true;
        }
        return sst::plugininfra::patch_support::patchParamsValue(paramId, value, engine->patchMain);
    }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        if (ghostblock::p2::Morph::isParam(paramId))
            return morph.valueToText(paramId, value, display, size);
        if (ghostblock::p2::spectral::SpectralFx::isParam(paramId)) // SPECTRAL FX
            return spectral.valueToText(paramId, value, display, size);
        if (ghostblock::p2::sub::SubBass::isParam(paramId)) // SUB
            return sub.valueToText(paramId, value, display, size);
        return sst::plugininfra::patch_support::patchParamsValueToText(paramId, value, display,
                                                                       size, engine->patchMain);
    }
    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        if (ghostblock::p2::Morph::isParam(paramId))
            return morph.textToValue(paramId, display, value);
        if (ghostblock::p2::spectral::SpectralFx::isParam(paramId)) // SPECTRAL FX
            return spectral.textToValue(paramId, display, value);
        if (ghostblock::p2::sub::SubBass::isParam(paramId)) // SUB
            return sub.textToValue(paramId, display, value);
        return sst::plugininfra::patch_support::patchParamsTextToValue(paramId, display, value,
                                                                       engine->patchMain);
    }
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        // paramsFlush is audio-thread when the plugin is ACTIVE, main-thread when INACTIVE.
        if (isActive())
        {
            // Audio thread: route param changes through the queue into `patch`.
            auto sz = in->size(in);
            for (uint32_t i = 0; i < sz; ++i)
                handleEvent(in->get(in, i));
            engine->processUIQueue(out);
            morph.sendToHost(out);
            spectral.sendToHost(out); // SPECTRAL FX
            sub.sendToHost(out);      // SUB
        }
        else
        {
            for (uint32_t i = 0; i < in->size(in); ++i)
                if (!morphParamEvent(in->get(in, i)) && !spectralParamEvent(in->get(in, i))) // SPECTRAL FX
                    subParamEvent(in->get(in, i));                                           // SUB
            morph.sendToHost(out);
            spectral.sendToHost(out); // SPECTRAL FX
            sub.sendToHost(out);      // SUB
            spectralMain();
            // Main thread: patchMain is the truth; update it in place and echo out.
            engine->paramsFlushMainThread(in, out);
        }
    }

    bool implementsPresetLoad() const noexcept override { return true; }
    bool presetLoadFromLocation(uint32_t location_kind, const char *location,
                                const char *load_key) noexcept override
    {
        if (location_kind ==
            clap_preset_discovery_location_kind::CLAP_PRESET_DISCOVERY_LOCATION_FILE)
        {
            try
            {
                SXSNLOG("Loading preset from file '" << location << "'");
                auto p = fs::path(fs::u8path(location));
                if (p.extension() == ".sxsnp")
                {
                    std::ifstream t(p);
                    if (!t.is_open())
                        return false;
                    std::stringstream buffer;
                    buffer << t.rdbuf();

                    auto tmp = std::make_unique<Patch>();
                    tmp->fromState(buffer.str());

                    engine->patchMain.copyValuesFrom(*tmp);
                    // Name is main-thread-only patch state; a preset takes the filename, set on
                    // patchMain directly (after copyValuesFrom, which copied the streamed name).
                    auto dn = p.filename().replace_extension("").u8string();
                    memset(engine->patchMain.name, 0, sizeof(engine->patchMain.name));
                    strncpy(engine->patchMain.name, dn.c_str(), sizeof(engine->patchMain.name) - 1);
                    engine->patchMain.dirty = false;
                    // no PresetManager here to place the file in the user tree, so record no
                    // preset rather than leaving the previous one standing
                    engine->dawStateMain.main.recordPreset();
                    engine->uiForceRebuild++;

                    if (isActive())
                        Synth::sendEntirePatchToAudio(engine->patchMain, engine->mainToAudio,
                                                      _host.host());
                    else if (_host.canUseParams())
                    {
                        _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
                        _host.paramsRescan(CLAP_PARAM_RESCAN_INFO);
                    }
                    return true;
                }
                else
                {
                    SXSNLOG("File extension is not .sxsnp '" << p.extension().u8string() << "'");
                }
            }
            catch (const fs::filesystem_error &e)
            {
                SXSNLOG("File System Error " << e.what() << " with " << location);
            }
        }
        else if (location_kind ==
                 clap_preset_discovery_location_kind::CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
        {
            auto idx = std::atoi(load_key);
            // This is a bit of a pattern problem. The preset manager should be on the engine.
            auto pm = presets::PresetManager(_host.host());
            if (idx < 0 || idx >= pm.factoryPatchVector.size())
                return false;

            auto &p = pm.factoryPatchVector[idx];

            // TODO : Assert the keys match here. loadFactoryPreset writes patchMain (name / dirty /
            // macro names via fromState) and funnels params into the audio patch; force an open
            // editor to rebuild from patchMain.
            pm.loadFactoryPreset(engine->patchMain, engine->mainToAudio, p.first, p.second);
            auto loaded = presets::PresetManager::LoadedPreset::factory(p.first, p.second);
            engine->dawStateMain.main.recordPreset(loaded.sessionKind(), loaded.category,
                                                   loaded.sessionPath());
            engine->uiForceRebuild++;
            return true;
        }
        return false;
    }

  public:
    bool implementsGui() const noexcept override { return clapJuceShim != nullptr; }
    std::unique_ptr<sst::clap_juce_shim::ClapJuceShim> clapJuceShim;
#if P2_CLASSIC_EDITOR
    ADD_SHIM_IMPLEMENTATION(clapJuceShim)
#else
    // ADD_SHIM_IMPLEMENTATION(clapJuceShim), written out: the same calls, except that the machine editor's window keeps
    // its shape (1040:744) between 75 % and 200 % and never outgrows the screen (adjust_size, get_resize_hints), so
    // the CLAP host and the VST3 / AU wrappers (checkSizeConstraint, resizeView) size it the way its grip does
    bool guiCanResize() const noexcept override { return clapJuceShim->guiCanResize(); }
    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override
    {
        return clapJuceShim->guiIsApiSupported(api, isFloating);
    }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        return clapJuceShim->guiCreate(api, isFloating);
    }
    void guiDestroy() noexcept override { clapJuceShim->guiDestroy(); }
    bool guiSetParent(const clap_window *window) noexcept override
    {
        return clapJuceShim->guiSetParent(window);
    }
    bool guiSetScale(double scale) noexcept override { return clapJuceShim->guiSetScale(scale); }
    bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept override
    {
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = true;
        hints->aspect_ratio_width = ghostblock::p2::machine::sizing::W;
        hints->aspect_ratio_height = ghostblock::p2::machine::sizing::H;
        return true;
    }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override
    {
        // host sizes are in the host's pixels: through the gui scale (1 on macOS) to ours and back
        const double g = juce::jmax(0.25, clapJuceShim->getGuiScale());
        uint32_t w = (uint32_t)std::lround(*width / g), h = (uint32_t)std::lround(*height / g);
        ghostblock::p2::machine::sizing::adjust(w, h, ghostblock::p2::machine::sizing::displayCap(machineEditor.getComponent()));
        if (g != 1.0)
        {
            *width = (uint32_t)std::lround(w * g);
            *height = (uint32_t)std::lround(*width * (double)ghostblock::p2::machine::sizing::H / ghostblock::p2::machine::sizing::W);
        }
        else
        {
            *width = w;
            *height = h;
        }
        return true;
    }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        return clapJuceShim->guiSetSize(width, height);
    }
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        return clapJuceShim->guiGetSize(width, height);
    }
    bool guiShow() noexcept override { return clapJuceShim->guiShow() && onShow(); }
    std::function<bool(void)> onShow{[]() { return true; }};
    juce::Component::SafePointer<ghostblock::p2::machine::MachineEditor> machineEditor;
#endif
    ADD_SHIM_LINUX_TIMER(clapJuceShim)
    std::unique_ptr<juce::Component> createEditor() override
    {
        // Poltergeist 2: the GhostBlock editor on the same patch and queues the upstream editor uses
        // (the ghost machine by default; the first editor stays selectable at compile time: -DP2_CLASSIC_EDITOR=ON)
#if P2_CLASSIC_EDITOR
        using P2Editor = ghostblock::p2::Poltergeist2Editor;
#else
        using P2Editor = ghostblock::p2::machine::MachineEditor;
#endif
        auto res = std::make_unique<P2Editor>(
            engine->patchMain, engine->wavetableHandoff, engine->audioToMain, engine->mainToAudio,
            engine->audioOutputRing, engine->editorActive, engine->uiForceRebuild,
            engine->dawStateMain, _host.host(), uiNotes, &morph, &spectral // SPECTRAL FX: + the rack
#if !P2_CLASSIC_EDITOR
            ,
            &sub // SUB (the ghost machine's SUB sign and panel)
#endif
        );
#if !P2_CLASSIC_EDITOR
        res->analyzerDefaults = engine->defaultsProvider.get(); // the SPYGLASS keeps its resolution where upstream's analyzer does
        // the grip / the size menu: ask the host for the new size (it answers with set_size)
        res->onRequestSize = [this](int w, int h) {
            if (!_host.canUseGui() || !clapJuceShim->isEditorAttached())
                return true; // no window of the host's yet: just take the size (the host reads it with get_size)
            const double g = clapJuceShim->getGuiScale();
            return _host.guiRequestResize((uint32_t)std::lround(w * g), (uint32_t)std::lround(h * g));
        };
        machineEditor = res.get();
#endif
        onShow = []() { return true; };
        res->repaint();
        return res;
    }

    bool registerOrUnregisterTimer(clap_id &id, int ms, bool reg) override
    {
        if (!_host.canUseTimerSupport())
            return false;
        if (reg)
        {
            _host.timerSupportRegister(ms, &id);
        }
        else
        {
            _host.timerSupportUnregister(id);
        }
        return true;
    }

    bool registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg) override
    {
        if (!_host.canUsePosixFdSupport())
            return false;
        if (reg)
        {
            _host.posixFdSupportRegister(fd, flags);
        }
        else
        {
            _host.posixFdSupportUnregister(fd);
        }
        return true;
    }

    static uint32_t vst3_getNumMIDIChannels(const clap_plugin *plugin, uint32_t note_port)
    {
        return 16;
    }
    static uint32_t vst3_supportedNoteExpressions(const clap_plugin *plugin)
    {
        return clap_supported_note_expressions::AS_VST3_NOTE_EXPRESSION_TUNING |
               clap_supported_note_expressions::AS_VST3_NOTE_EXPRESSION_PAN;
    }

    static bool CLAP_ABI auv2_get_param_order(const clap_plugin_t *plugin, size_t *order,
                                              size_t param_count) noexcept
    {
        auto *self = static_cast<SixSinesClap *>(plugin->plugin_data);
        auto &params = self->engine->patchMain.params;
        // Poltergeist 2: MORPH's own params come after the engine's, in their own order
        if (param_count != params.size() + ghostblock::p2::Morph::kParams + ghostblock::p2::spectral::SpectralFx::kParams + // SPECTRAL FX
                               ghostblock::p2::sub::SubBass::kParams)                                                     // SUB
            return false;
        std::iota(order, order + param_count, (size_t)0);
        std::sort(order, order + params.size(),
                  [&params](size_t a, size_t b)
                  {
                      auto aBack = params[a]->meta.version;
                      auto bBack = params[b]->meta.version;
                      if (aBack != bBack)
                          return aBack < bBack;
                      return params[a]->meta.id < params[b]->meta.id;
                  });
        return true;
    }

    const void *extension(const char *id) noexcept override
    {
        if (strcmp(id, CLAP_PLUGIN_AS_VST3) == 0)
        {
            static clap_plugin_as_vst3 v3p{vst3_getNumMIDIChannels, vst3_supportedNoteExpressions};
            return &v3p;
        }
        if (strcmp(id, CLAP_PLUGIN_AUV2_PARAM_ORDERING) == 0)
        {
            static clap_plugin_auv2_param_ordering_t auv2po{auv2_get_param_order};
            return &auv2po;
        }

        return nullptr;
    }
};

} // namespace clapimpl

const clap_plugin *makePlugin(const clap_host *h, bool multiOut)
{
    if (multiOut)
    {
        auto res = new baconpaul::six_sines::clapimpl::SixSinesClap<true>(h);
        return res->clapPlugin();
    }
    else
    {
        auto res = new baconpaul::six_sines::clapimpl::SixSinesClap<false>(h);
        return res->clapPlugin();
    }
}

// SPECTRAL FX (testing only): the rack and the engine of a plugin made by makePlugin(host, false)
ghostblock::p2::spectral::SpectralFx *getSpectralFromPlugin(const clap_plugin_t *plugin)
{
    return &static_cast<clapimpl::SixSinesClap<false> *>(plugin->plugin_data)->spectral;
}
// SUB (testing only)
ghostblock::p2::sub::SubBass *getSubFromPlugin(const clap_plugin_t *plugin)
{
    return &static_cast<clapimpl::SixSinesClap<false> *>(plugin->plugin_data)->sub;
}
Synth *getSynthFromPlugin(const clap_plugin_t *plugin)
{
    return static_cast<clapimpl::SixSinesClap<false> *>(plugin->plugin_data)->engine.get();
}

// Used for testing only
const Patch *getPatchFromPlugin(const clap_plugin_t *plugin)
{
    auto *self = static_cast<clapimpl::SixSinesClap<false> *>(plugin->plugin_data);
    return &self->engine->patch;
}
} // namespace baconpaul::six_sines

namespace chlp = clap::helpers;
namespace bpss = baconpaul::six_sines::clapimpl;

template class chlp::Plugin<bpss::misLevel, bpss::checkLevel>;
template class chlp::HostProxy<bpss::misLevel, bpss::checkLevel>;
