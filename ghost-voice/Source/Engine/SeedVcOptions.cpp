// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "SeedVcOptions.h"

namespace gv::hood
{
const std::vector<Option>& options()
{
    // 14 options: inference.py's 12 argparse options, then the 2 more that only app_svc.py has (--share, --gpu).
    // app_svc.py's Gradio controls for steps / length / cfg / auto-f0 / pitch carry the ranges used here.
    static const std::vector<Option> o {
        { Id::source,     "--source",             "Performance",   "inference.py",          "str",   "./examples/source/source_s1.wav", "",         "the performance card",              "source",     "The audio that is re-sung: its melody, timing and words are kept.", Kind::given },
        { Id::target,     "--target",             "Reference voice","inference.py",         "str",   "./examples/reference/s1p1.wav",   "",         "voice A / voice B",                 "a, b",       "The reference audio of the voice to sing with. Ghost Voice passes voice A's first take below 50 % B and voice B's first take from 50 % B (the blend itself is the lab's patch, below).", Kind::given },
        { Id::output,     "--output",             "Output folder", "inference.py",          "str",   "./reconstructed",                 "",         "(automatic)",                       "out_dir",    "Where the sung WAV is written. Ghost Voice gives every blend its own folder and moves the WAV to mix_NNN.wav.", Kind::given },
        { Id::steps,      "--diffusion-steps",    "Diffusion steps","inference.py, app_svc.py","int", "30 (app_svc.py: 10)",             "1 - 200",  "QUALITY",                           "steps",      "Number of diffusion steps. More is cleaner and slower; Seed-VC suggests 50-100 for best quality.", Kind::number, 1, 200, 1 },
        { Id::length,     "--length-adjust",      "Length adjust", "inference.py, app_svc.py","float","1.0",                             "0.5 - 2.0","(only here)",                       "length_adjust","Below 1.0 speeds the singing up, above 1.0 slows it down. Anything but 1.0 changes the length of the result.", Kind::number, 0.5, 2.0, 0.01 },
        { Id::cfg,        "--inference-cfg-rate", "Inference CFG rate","inference.py, app_svc.py","float","0.7",                       "0.0 - 1.0","FOLLOW",                        "cfg",        "Classifier-free guidance rate. Seed-VC says it has a subtle influence.", Kind::number, 0.0, 1.0, 0.01 },
        { Id::f0,         "--f0-condition",       "F0 condition (singing model)","inference.py","str2bool","False (Ghost Voice: True)", "",       "(only here)",                       "f0_condition","Use the pitch-conditioned (singing) model. On: the 44.1 kHz singing model with RMVPE pitch. Off: the speech model (22.05 kHz), which ignores auto-F0 and pitch shift.", Kind::toggle },
        { Id::autoF0,     "--auto-f0-adjust",     "Auto F0 adjust","inference.py, app_svc.py","str2bool","False (app_svc.py: True)",     "",         "MATCH RANGE",                       "auto_f0",    "Roughly moves the melody to match the target voice's range. Only works with the F0 condition on.", Kind::toggle },
        { Id::semis,      "--semi-tone-shift",    "Pitch shift",   "inference.py, app_svc.py","int", "0",                               "-24 - 24", "PITCH",                             "semitones",  "Pitch shift in semitones. Only works with the F0 condition on.", Kind::number, -24, 24, 1 },
        { Id::checkpoint, "--checkpoint",         "Checkpoint",    "inference.py, app_svc.py","str", "None (Seed-VC picks its own)",    "",         "(only here)",                       "checkpoint", "Path to the model checkpoint. Empty: Seed-VC loads its own published model for the F0 condition setting.", Kind::model },
        { Id::config,     "--config",             "Config",        "inference.py, app_svc.py","str", "None (Seed-VC picks its own)",    "",         "(goes with the checkpoint)",        "config",     "Path to the model's config file; it must belong to the checkpoint, so it follows the checkpoint choice.", Kind::modelConfig },
        { Id::fp16,       "--fp16",               "Half precision (fp16)","inference.py, app_svc.py","str2bool","True",              "",         "(Apple GPU switch sets the automatic value)","fp16",   "Run the model in half precision. Ghost Voice's automatic value (what build 3 always did): on for the Apple GPU, off on the CPU.", Kind::threeWay },
        { Id::share,      "--share",              "Share",         "app_svc.py only",       "str2bool","False",                         "",         "(not used)",                        "",           "Makes Seed-VC's Gradio web page public. Ghost Voice never starts that page, so there is nothing to share (and nothing leaves your Mac).", Kind::notUsed },
        { Id::gpu,        "--gpu",                "GPU id",        "app_svc.py only",       "int",   "0",                               "",         "(not used)",                        "",           "Which CUDA graphics card to use. Macs have no CUDA; Ghost Voice uses the Apple GPU or the CPU (the Apple GPU switch and CPU only).", Kind::notUsed },
    };
    return o;
}

namespace
{
juce::String fmtG (double v)       // Python's '%g' for the numbers used here
{
    auto s = juce::String (v, 6);
    if (s.contains (".")) { s = s.trimCharactersAtEnd ("0"); s = s.trimCharactersAtEnd ("."); }
    return s;
}
juce::String tf (bool b) { return b ? "True" : "False"; }
juce::String jsonNumber (const juce::var& v) { return juce::JSON::toString (v, true); }     // the same digits the job file has
juce::String pyFloat (juce::String t) { return t.containsAnyOf (".eE") ? t : t + ".0"; }   // Python prints a float with its ".0"
}

juce::String valueText (Id id, const EngineSettings& s)
{
    switch (id)
    {
        case Id::steps: return juce::String (s.steps);
        case Id::length: return pyFloat (jsonNumber (juce::var (std::round ((double) s.lengthAdjust * 1000.0) / 1000.0)));
        case Id::cfg: return juce::String ((double) s.follow, 2);
        case Id::f0: return tf (s.f0Condition);
        case Id::autoF0: return tf (s.autoPitch);
        case Id::semis: return juce::String (s.semitones);
        case Id::fp16: return s.fp16 < 0 ? "automatic" : tf (s.fp16 == 1);
        case Id::checkpoint: return s.checkpoint.isEmpty() ? "None (Seed-VC picks its own)" : s.checkpoint;
        case Id::config: return s.config.isEmpty() ? "None (Seed-VC picks its own)" : s.config;
        default: return {};
    }
}

juce::String commandLine (const Job& j, float mix, bool appleGpu)
{
    // the arguments are read back from the job file text itself, so the page shows what the helper really receives
    const auto v = juce::JSON::parse (jobToJson (j));
    auto num = [&] (const char* key, const juce::var& dflt) { return pyFloat (v.hasProperty (key) ? jsonNumber (v[key]) : jsonNumber (dflt)); };
    const float m = juce::jlimit (0.0f, 1.0f, mix);
    const auto& a = j.a; const auto& b = j.b;
    const juce::String target = m < 0.5f ? (a.isEmpty() ? "" : a[0]) : (b.isEmpty() ? "" : b[0]);
    juce::StringArray p { "python", "inference.py", "--source", j.performance, "--target", target, "--output", j.outDir.getChildFile (".render_0").getFullPathName(),
                          "--diffusion-steps", juce::String (v["steps"].operator int()),
                          "--length-adjust", num ("length_adjust", 1.0),
                          "--inference-cfg-rate", num ("cfg", 0.7),
                          "--f0-condition", tf (v.hasProperty ("f0_condition") ? (bool) v["f0_condition"] : true),
                          "--auto-f0-adjust", tf ((bool) v["auto_f0"]), "--semi-tone-shift", juce::String (v["semitones"].operator int()) };
    if (j.settings.checkpoint.isNotEmpty()) { p.add ("--checkpoint"); p.add (j.settings.checkpoint); }
    if (j.settings.config.isNotEmpty()) { p.add ("--config"); p.add (j.settings.config); }
    p.add ("--fp16"); p.add (tf (v.hasProperty ("fp16") ? (bool) v["fp16"] : appleGpu));
    for (auto& f : a) { p.add ("--identity-a"); p.add (f); }
    for (auto& f : b) { p.add ("--identity-b"); p.add (f); }
    p.add ("--identity-mix-b"); p.add (fmtG (m)); p.add ("--fused-identity-no-prompt"); p.add (tf (j.settings.detail ? false : true));
    return p.joinIntoString (" ");
}

namespace
{
struct Pair { const char* ckpt; const char* cfg; const char* label; bool f0; };
// Seed-VC's two published pairs, as inference.py's load_models() names them
const Pair kPairs[] {
    { "DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth", "config_dit_mel_seed_uvit_whisper_base_f0_44k.yml", "Singing model (44.1 kHz, F0)", true },
    { "DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth", "config_dit_mel_seed_uvit_whisper_small_wavenet.yml", "Speech model (22.05 kHz)", false },
};
}

std::vector<Model> installedModels()
{
    std::vector<Model> out;
    const auto root = engineDir().getChildFile ("seed-vc").getChildFile ("checkpoints");
    if (! root.isDirectory()) return out;
    for (auto& pr : kPairs)
    {
        for (auto& c : root.findChildFiles (juce::File::findFiles, true, pr.ckpt))
        {
            const auto cfg = c.getSiblingFile (pr.cfg);
            if (cfg.existsAsFile()) { out.push_back ({ pr.label, c.getFullPathName(), cfg.getFullPathName(), pr.f0 }); break; }
        }
    }
    return out;
}

juce::String ownPick (bool f0Condition) { return f0Condition ? kPairs[0].ckpt : kPairs[1].ckpt; }

void setGhostVoiceStart (EngineSettings& s)
{
    const EngineSettings d;
    s.steps = d.steps; s.lengthAdjust = d.lengthAdjust; s.follow = d.follow; s.f0Condition = d.f0Condition; s.autoPitch = d.autoPitch;
    s.semitones = d.semitones; s.fp16 = d.fp16; s.checkpoint = d.checkpoint; s.config = d.config;
}

void setSeedVcDefaults (EngineSettings& s)
{   // inference.py's argparse defaults (steps 30, length 1.0, cfg 0.7, f0 False, auto-f0 False, shift 0, fp16 True, no checkpoint)
    s.steps = 30; s.lengthAdjust = 1.0f; s.follow = 0.7f; s.f0Condition = false; s.autoPitch = false;
    s.semitones = 0; s.fp16 = 1; s.checkpoint = {}; s.config = {};
}
}
