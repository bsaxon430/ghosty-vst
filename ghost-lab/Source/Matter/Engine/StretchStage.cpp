// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). Glue to PaulXStretch's own StretchAudioSource (StretchStage.h).
#include "StretchStage.h"
#include "PS_Source/StretchSource.h"
#include "Hood.h"

// PaulXStretch's own FFT size rounding (verbatim from its PluginProcessor.cpp)
namespace psplugin
{
#include "../PluginProcessor-optimizebufsize.inc"
}

namespace matter
{
bool stretchIsOff (const StretchSettings& s)
{
    return s.amount <= 1.001f && std::abs (s.pitch) < 0.01f && s.up < 0.001f && s.down < 0.001f && s.spread < 0.001f
        && ! hood::anyPxsPin (s) && ! s.freeFilter.isValid();   // a control moved (or the free filter drawn) in UNDER THE HOOD means the stretch runs
}

int stretchFftSize (float size)
{
    // PaulstretchpluginAudioProcessor::setFFTSize with the default prebuffering (m_prebuffer_amount 1)
    return psplugin::optimizebufsize ((int) std::pow (2.0, 7.0 + juce::jlimit (0.0f, 1.0f, size) * 10.0));
}

namespace
{
struct Run
{
    // the plug-in's spectral module switches (m_sm_enab_pars), its defaults: frequency shift and pitch shift on
    juce::AudioParameterBool enab[9] {
        { { "enab_specmodule0", 1 }, "Enable harmonics", false }, { { "enab_specmodule1", 1 }, "Enable tonal vs noise", false },
        { { "enab_specmodule2", 1 }, "Enable frequency shift", true }, { { "enab_specmodule3", 1 }, "Enable pitch shift", true },
        { { "enab_specmodule4", 1 }, "Enable ratios", false }, { { "enab_specmodule5", 1 }, "Enable spread", false },
        { { "enab_specmodule6", 1 }, "Enable filter", false }, { { "enab_specmodule7", 1 }, "Enable free filter", false },
        { { "enab_specmodule8", 1 }, "Enable compressor", false } };
    std::array<juce::AudioParameterBool*, 9> pars;
    juce::AudioFormatManager afm;
    ProcessParameters ppar;
    BinauralBeatsParameters bbpar;
    std::unique_ptr<StretchAudioSource> sc;
    juce::AudioBuffer<float> source;

    Run (const juce::AudioBuffer<float>& in, const StretchSettings& s)
    {
        // PaulXStretch's own parameters by id: the page's setting, what you set UNDER THE HOOD, or the plug-in's own default
        auto V = [&s] (const char* id) { return hood::pxs (s, id); };
        auto pinned = [&s] (const char* id) { return s.pins.count (std::string ("pxs.") + id) > 0; };
        for (int i = 0; i < 9; ++i) pars[(size_t) i] = &enab[i];
        // the plug-in's "Enable ..." switches; ours for the modules our controls use (the ratios and spread)
        for (int i = 0; i < 9; ++i) enab[i] = V (("enab_specmodule" + juce::String (i)).toRawUTF8()) > 0.5f;
        if (! pinned ("enab_specmodule4")) enab[4] = s.up > 0.001f || s.down > 0.001f;
        if (! pinned ("enab_specmodule5")) enab[5] = s.spread > 0.001f;
        afm.registerBasicFormats();
        // the plug-in's constructor
        sc = std::make_unique<StretchAudioSource> (2, &afm, pars);
        sc->setOnsetDetection ((double) V ("onsetdetect_0"));
        sc->setLoopingEnabled (true);
        sc->setFFTWindowingType ((int) std::lround (V ("window")));
        auto env = std::make_shared<breakpoint_envelope>();
        env->SetName ("Free filter"); env->AddNode ({ 0.0, 0.75 }); env->AddNode ({ 1.0, 0.75 });
        env->set_reset_nodes (env->get_all_nodes());
        if (s.freeFilter.isValid()) env->restoreState (s.freeFilter);   // the nodes you drew (the plug-in's own saved form)
        // the plug-in's "Free filter" transform parameters (only when you set one)
        if (pinned ("freefilter_shiftx_0")) env->m_transform_x_shift = V ("freefilter_shiftx_0");
        if (pinned ("freefilter_shifty_0")) env->m_transform_y_shift = V ("freefilter_shifty_0");
        if (pinned ("freefilter_scaley_0")) env->m_transform_y_scale = V ("freefilter_scaley_0");
        if (pinned ("freefilter_tilty_0")) env->m_transform_y_tilt = V ("freefilter_tilty_0");
        if (pinned ("freefilter_randomybands0")) env->m_transform_y_random_bands = (int) std::lround (V ("freefilter_randomybands0"));
        if (pinned ("freefilter_randomyrate0")) env->m_transform_y_random_rate = (int) std::lround (V ("freefilter_randomyrate0"));
        if (pinned ("freefilter_randomyamount0")) env->m_transform_y_random_amount = V ("freefilter_randomyamount0");
        sc->setFreeFilterEnvelope (env);
        // the sound, as an in-memory source (the plug-in's capture / memory buffer path)
        source.makeCopyOf (in);
        if (source.getNumChannels() == 1) { source.setSize (2, source.getNumSamples(), true); source.copyFrom (1, 0, source, 0, 0, source.getNumSamples()); }
        sc->setAudioBufferAsInputSource (&source, (int) kRate, source.getNumSamples());
        // offlineRender: play range, one loop (looping off), volume, rate, no dry preview, dry rate 1, not paused
        sc->setPlayRange ({ (double) V ("playrange_start0"), (double) V ("playrange_end0") }, true);
        sc->setLoopingEnabled (false);
        sc->setMainVolume (juce::jlimit (-24.0, 12.0, (double) V ("mainvolume0"))); // "Main volume" (default -3 dB)
        sc->setRate (juce::jlimit (0.1, 1024.0, (double) V ("stretchamount0")));    // "Stretch amount"
        // the plug-in's "Bypass stretch" (its dry path at the "Dry playrate") and "Freeze", only when you set them
        sc->setDryPlayrate (juce::jlimit (0.1, 8.0, (double) V ("dryplayrate0")));
        sc->setPreviewDry (V ("bypass_stretch0") > 0.5f);
        sc->setFreezing (V ("freeze0") > 0.5f);
        sc->setPaused (false);
        sc->setLoopXFadeLength ((double) V ("loopxfadelen0"));                      // "Loop xfade length" default 0.01
        sc->setFFTSize (stretchFftSize (V ("fftsize0")), true);
        // the processing chain order (PaulXStretch's own order editor), only when you moved a position
        bool orderPinned = false;
        for (int i = 0; i < 9; ++i) orderPinned = orderPinned || pinned (("order" + juce::String (i)).toRawUTF8());
        if (orderPinned)
        {
            std::vector<SpectrumProcess> order;
            std::array<bool, 9> used {};
            bool valid = true;
            for (int i = 0; i < 9; ++i)
            {
                const int t = (int) std::lround (V (("order" + juce::String (i)).toRawUTF8()));
                if (t < 0 || t > 8 || used[(size_t) t]) { valid = false; break; }
                used[(size_t) t] = true;
                order.push_back (SpectrumProcess ((SpectrumProcessType) t, pars[(size_t) t]));
            }
            if (valid) sc->setSpectrumProcessOrder (order);
        }
        // updateStretchParametersFromPluginParameters
        ppar.pitch_shift.cents = (int) std::lround (V ("pitchshift0") * 100.0f);
        ppar.freq_shift.Hz = V ("freqshift0");
        ppar.spread.bandwidth = V ("spread0");
        ppar.compressor.power = V ("compress0");
        ppar.harmonics.nharmonics = (int) std::lround (V ("numharmonics0")); ppar.harmonics.freq = V ("harmonicsfreq0");
        ppar.harmonics.bandwidth = V ("harmonicsbw0"); ppar.harmonics.gauss = V ("harmonicsgauss0") > 0.5f;
        ppar.octave.om2 = V ("octavemixm2_0"); ppar.octave.om1 = V ("octavemixm1_0"); ppar.octave.o0 = V ("octavemix0_0");
        ppar.octave.o1 = V ("octavemix1_0"); ppar.octave.o15 = V ("octavemix15_0"); ppar.octave.o2 = V ("octavemix2_0");
        ppar.ratiomix.ratiolevels = { V ("octavemixm2_0"), V ("octavemixm1_0"), V ("octavemix0_0"), V ("octavemix1_0"), V ("octavemix15_0"),
                                      V ("octavemix2_0"), V ("octavemix_extra0_0"), V ("octavemix_extra1_0") };
        for (int i = 0; i < 8; ++i) ppar.ratiomix.ratios[(size_t) i] = V (("ratiomix_ratio_" + juce::String (i) + "_0").toRawUTF8());
        ppar.filter.low = V ("filter_low_0"); ppar.filter.high = V ("filter_high_0");
        ppar.tonal_vs_noise.bandwidth = V ("tonalvsnoisebw_0"); ppar.tonal_vs_noise.preserve = V ("tonalvsnoisepreserve_0");
        // binaural beats: the plug-in's four parameters (only when you moved one)
        if (pinned ("binauralbeats") || pinned ("binauralbeatsmono") || pinned ("binauralbeatsfreq") || pinned ("binauralbeatsmode"))
        {
            bbpar.stereo_mode = (BB_STEREO_MODE) (int) std::lround (V ("binauralbeatsmode"));
            bbpar.mono = V ("binauralbeatsmono");
            bbpar.free_edit.extreme_y.set_min (0.05f); bbpar.free_edit.extreme_y.set_max (50.0f);
            const float ratio = (V ("binauralbeatsfreq") - 0.05f) / (50.0f - 0.05f);
            bbpar.free_edit.set_posy (0, ratio); bbpar.free_edit.set_posy (1, ratio);
            bbpar.free_edit.update_curve (2);
            bbpar.free_edit.set_enabled (V ("binauralbeats") > 0.5f);
        }
        sc->setProcessParameters (&ppar, &bbpar);
        sc->setNumOutChannels (2);
        sc->prepareToPlay (1024, kRate);
    }
};
}

int stretchLength (int inSamples, const StretchSettings& s)
{
    if (inSamples <= 0) return 0;
    juce::AudioBuffer<float> dummy (2, inSamples);
    dummy.clear();
    Run r (dummy, s);
    const double secs = r.sc->getOutputDurationSecondsForRange (r.sc->getPlayRange(), r.sc->getFFTSize());
    return (int) juce::jmin ((double) kMaxMatterSeconds * kRate, secs * kRate);
}

juce::AudioBuffer<float> stretchSound (const juce::AudioBuffer<float>& in, const StretchSettings& s, const std::atomic<bool>* cancel)
{
    if (stretchIsOff (s) || in.getNumSamples() == 0) return in;
    Run r (in, s);
    const double secs = r.sc->getOutputDurationSecondsForRange (r.sc->getPlayRange(), r.sc->getFFTSize());
    const int total = (int) juce::jmin ((double) kMaxMatterSeconds * kRate, secs * kRate);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    constexpr int block = 1024;   // the offline render's block size
    juce::AudioBuffer<float> blk (2, block);
    for (int at = 0; at < total; at += block)
    {
        if (cancel != nullptr && cancel->load()) { out.setSize (2, juce::jmax (1, at), true); break; }
        blk.clear();
        juce::AudioSourceChannelInfo info (&blk, 0, block);
        r.sc->getNextAudioBlock (info);
        const int n = juce::jmin (block, total - at);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, at, blk, c, 0, n);
    }
    // the plug-in's "Pass input through": its processBlock adds the dry input to what it plays; here the dry sound is added from the top
    if (hood::pxs (s, "passthrough0") > 0.5f)
        for (int c = 0; c < 2; ++c) out.addFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, juce::jmin (in.getNumSamples(), out.getNumSamples()));
    return out;
}
}
