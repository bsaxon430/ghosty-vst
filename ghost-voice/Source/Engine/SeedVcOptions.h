// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// UNDER THE HOOD (build 4): the complete option list of Seed-VC's singing conversion, taken from the version the installer pins
// (helper/install-engine.sh: Plachtaa/seed-vc @ 51383ef), its inference.py (argparse, lines 412-423) and app_svc.py (its Gradio
// controls and its own argparse). Names, defaults and ranges are Seed-VC's own; docs/CONTROLS.md lists the same table.
// This file holds the table, the equivalent command line, and the models the installer actually has. It touches no sound.
#pragma once
#include "VoiceEngine.h"

namespace gv::hood
{
enum class Id { source, target, output, steps, length, cfg, f0, autoF0, semis, checkpoint, config, fp16, share, gpu, count };
// how the page treats an option: number / switch / three-way (automatic, on, off) / model list / shown but set elsewhere / not used on a Mac
enum class Kind { number, toggle, threeWay, model, modelConfig, given, notUsed };

struct Option
{
    Id id;
    const char* flag;            // Seed-VC's own option name, e.g. "--diffusion-steps"
    const char* title;           // our plain-words name
    const char* where;           // which upstream file defines it
    const char* type;            // upstream's argparse type
    const char* upstreamDefault; // upstream's default
    const char* range;           // upstream's range (app_svc.py's Gradio control), or "" for none
    const char* ours;            // the Ghost Voice control it is the same value as (or "")
    const char* jobKey;          // the key in the job file helper/ghostvoice_engine.py gets ("" = none)
    const char* help;            // what it does, from Seed-VC's own words
    Kind kind;
    double lo = 0, hi = 0, step = 0;
};
const std::vector<Option>& options();             // in inference.py's order, then app_svc.py's extras

// the value on screen / in the job for one option, as text (the one source of truth is EngineSettings)
juce::String valueText (Id, const EngineSettings&);
// the command line of Seed-VC's inference.py for the blend `mix`, exactly the arguments helper/ghostvoice_engine.py builds from the
// job (seedvc_command there; tests/GhostVoiceTests.cpp and CI compare the two). appleGpu = what --fp16 is when it is automatic.
juce::String commandLine (const Job&, float mix, bool appleGpu);

// the singing models the installer has on disk (Seed-VC's two published pairs; the singing one is what the installer fetches)
struct Model { juce::String label, checkpoint, config; bool needsF0; };
std::vector<Model> installedModels();             // only pairs whose two files are really there
// Seed-VC's own pick for the f0-condition setting (what it loads when --checkpoint is not given)
juce::String ownPick (bool f0Condition);
// settings with every Seed-VC option back at Ghost Voice's start (build 3's values) / at Seed-VC's own defaults (inference.py)
void setGhostVoiceStart (EngineSettings&);
void setSeedVcDefaults (EngineSettings&);
}
