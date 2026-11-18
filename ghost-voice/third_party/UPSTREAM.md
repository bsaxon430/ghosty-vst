# Upstream code inside Ghost Voice (untouched copies)

Ghost Voice's chopper is a fork of INTERSECT: these folders are byte-for-byte copies of the upstream projects at the
commits below. Nothing in them is edited; Ghost Voice only adds its own UI and features around them (Source/Chop).

| folder | project | licence | version |
|---|---|---|---|
| intersect/src, themes | INTERSECT by tucktuckg00se, https://github.com/tucktuckg00se/INTERSECT | GPL-3.0 | b40c2fb30dd6ecc3688f95386af32e3af6f0e246 (2026-11-11) |
| intersect/signalsmith-stretch | Signalsmith Stretch, https://github.com/Signalsmith-Audio/signalsmith-stretch | MIT | 57b93f4e9206a089a45387eaa39bdc9f310d3308 |
| intersect/signalsmith-linear | Signalsmith Linear, https://github.com/Signalsmith-Audio/linear | MIT | 5668673560146a9cfe38c25315071e3fd68c8317 |
| intersect/bungee (src, bungee) | Bungee, https://github.com/bungee-audio-stretch/bungee | MPL-2.0 | 2745718c2751bd655d7c5ed4853955de492bb39f |
| intersect/bungee/submodules/pffft | PFFFT (Bungee's submodule) | BSD-like (see its LICENSE) | as pinned by Bungee |
| intersect/bungee/submodules/eigen | Eigen (Bungee's submodule; headers only) | MPL-2.0 | c29c800126982c561e8d0b9255dc65474cd98de3 |
| intersect/third_party/velociloops | VelociLoops, https://github.com/kunitoki/VelociLoops | see its LICENSE | ce00fd46ebe7e0cbad3adbad448945bbd9cce172 |
| flayr-chop/Analysis.h | Flayr Chop's analysis engine (tempo, key, transients, cue modes), https://github.com/FlayrLabs/flayr-chop | AGPL-3.0 | c7baf83c752ff8b50a7e45ad80862fd8bbc10226 (2026-10-15) |

INTERSECT is built with JUCE 8.0.14 (its own pin). The one build-level adjustment: INTERSECT's PluginProcessor.cpp is compiled
with `-DcreatePluginFilter=intersectCreatePluginFilter` so its plug-in entry point does not clash with Ghost Voice's (the
file itself is unchanged), and INTERSECT's own standalone app (StandaloneApp.cpp) is not compiled (Ghost Voice is the app).
Stem separation is built without ONNX Runtime (it has no Intel Mac package; INTERSECT turns it off there too).
