#pragma once
// Mr. Mickens 2: the factory presets. Original names, built from the built-in re-created breaks and the plugin's own
// parameters (no files needed). A preset = reset to defaults, then: the 4 bays (family|name keys, "-" keeps the default),
// a drum pattern (0 two-step, 1 halftime, 2 roller, 3 empty), and "id=value" parameter settings.
#include <juce_core/juce_core.h>

namespace mk {

struct Preset { const char* lens; const char* name; const char* bays; int pattern; const char* params; };

inline const std::vector<Preset>& factoryPresets() {
    static const std::vector<Preset> P {
        // ---- JUNGLE / ROLLERS
        { "JUNGLE", "Haunted Roller",        "Haunt|Haunt - Original;Two Step|Two Step - Ghosts;-;-", 0, "bpm=172 b1_level=0.7 b2_mute=0 b2_level=0.45 b2_role=2 b1_role=1 il_mode=1 il_duck=0.6 b1_remix=25 b1_lock=80 fill_every=1 fill_len=2 fill_style=1 fill_int=0.6 reverb=0.1 crunch=0.3" },
        { "JUNGLE", "Static Steppers",       "Static|Static - Original;Stepper|Stepper - Ghosts;-;-", 0, "bpm=170 b1_level=0.7 b1_remix=40 b1_dna_mode=1 b1_lock=70 b2_mute=0 b2_level=0.5 b2_pan=0.3 b1_pan=-0.2 b2_remix=30 fill_every=2 fill_style=0 hat_lvl=0.5" },
        { "JUNGLE", "Two Step Ghost Train",  "Two Step|Two Step - Original;Haunt|Haunt - Ghosts;-;-", 0, "bpm=174 b1_remix=55 b1_dna_mode=1 b1_lock=60 b1_lk_pos=0 b2_mute=0 b2_level=0.4 b2_role=2 b1_role=1 il_mode=2 fill_every=1 fill_style=1 fill_int=0.8 crunch=0.35" },
        { "JUNGLE", "Canyon Chase",          "Canyon|Canyon - Original;Jungle Roller|Jungle Roller - Original;-;-", 2, "bpm=172 b1_remix=35 b1_lock=75 b2_mute=0 b2_level=0.55 b2_role=3 b1_role=1 il_mode=1 il_duck=0.5 lp_mix=0.6 lp_cut=0.75 flt_auto=1 ftr_src=1 ftr_amt=1.5 ftr_dec=0.1" },
        { "JUNGLE", "Rolling Thunder",       "Rolling|Rolling - Original;Rolling|Rolling - Ghosts;-;-", 2, "bpm=174 b1_remix=20 b2_mute=0 b2_level=0.5 b2_nudge=8 b2_grid=2 b2_pan=0.4 b1_pan=-0.3 b2_remix=45 b2_dna_mode=1 fill_every=1 fill_style=2 fill_int=0.7 duck_rel=0.12" },
        { "JUNGLE", "Tightrope Walk",        "Tightrope|Tightrope - Original;Verdict|Verdict - Ghosts;-;-", 0, "bpm=168 b1_remix=30 b1_lock=85 b2_mute=0 b2_level=0.45 b2_pitch=2 b2_decay=0.5 fill_every=2 fill_style=1 crunch=0.4 bits=0.15 reverb=0.15" },
        { "JUNGLE", "Haunt Tail Tear-Up",       "Haunt Tail|Haunt Tail - Original;Haunt|Haunt - Fill;-;-", 0, "bpm=176 b1_remix=60 b1_dna_mode=2 b1_lock=70 b1_lk_pos=0 b2_mute=0 b2_level=0.5 b2_role=4 b1_role=1 il_mode=1 fill_every=1 fill_len=2 fill_style=0 fill_int=0.9 hp_mix=0.5 hp_cut=0.3" },
        { "JUNGLE", "Dub Pressure",          "Dub Break|Dub Break - Room;Static|Static - Ghosts;-;-", 1, "bpm=170 b1_remix=15 b2_mute=0 b2_level=0.4 b2_nudge=4 b2_grid=1 reverb=0.35 size=0.8 lp_mix=0.8 lp_cut=0.55 phr_len=3 phr_amt=2 phr_shape=2 duck_rel=0.25" },
        // ---- HALFTIME / DUB
        { "HALFTIME", "Slow Weight",         "Halftime|Halftime - Original;-;-;-", 1, "bpm=172 b1_level=0.75 b1_remix=20 b1_lock=90 snr_lvl=1 kick_lvl=1 b1_duck_k=0.5 crunch=0.45 reverb=0.2 lp_mix=0.5 lp_cut=0.7" },
        { "HALFTIME", "Stubborn Stomp",      "Stubborn|Stubborn - Dirty;Halftime|Halftime - Ghosts;-;-", 1, "bpm=170 b1_remix=35 b1_dna_mode=1 b2_mute=0 b2_level=0.45 b2_role=2 b1_role=1 il_mode=1 fill_every=2 fill_style=0 bits=0.25 crunch=0.5" },
        { "HALFTIME", "Crawler Pit",         "Crawler|Crawler - Room;Bonecrack|Bonecrack - Original;-;-", 1, "bpm=168 b1_remix=45 b1_lock=65 b2_mute=0 b2_level=0.5 b2_pitch=-3 b2_decay=0.35 b2_pan=0.5 b1_pan=-0.4 reverb=0.25 hp_mix=0.4 hp_cut=0.25 flt_route=1" },
        { "HALFTIME", "Dub Break Chamber",   "Dub Break|Dub Break - Original;-;-;-", 3, "bpm=172 b1_level=0.8 b1_remix=25 b1_lock=80 reverb=0.4 size=0.9 cb_mix=0.5 cb_tune=2 cb_fb=0.8 lp_mix=0.7 lp_cut=0.6 flt_auto=1 duck_rel=0.3" },
        { "HALFTIME", "Verdict Bounce",      "Verdict|Verdict - Original;Chill|Chill - Ghosts;-;-", 1, "bpm=174 b1_remix=30 b2_mute=0 b2_level=0.4 b2_nudge=2 b2_grid=1 b2_role=3 b1_role=1 il_mode=4 il_duck=0.4 crunch=0.3 hat_lvl=0.4" },
        { "HALFTIME", "Heatwave Drag",       "Heatwave|Heatwave - Dirty;-;-;-", 1, "bpm=166 b1_remix=50 b1_dna_mode=2 b1_lock=60 b1_lk_ks=1 b1_lk_pos=0 fill_every=2 fill_style=3 fill_int=0.6 bits=0.3 crunch=0.6 lp_mix=0.6 lp_cut=0.65" },
        // ---- IDM / GLITCH
        { "IDM", "Fractured Stepper",        "Stepper|Stepper - Original;-;-;-", 3, "bpm=160 b1_level=0.8 b1_remix=85 b1_dna_mode=2 b1_lock=40 b1_lk_pos=0 b1_lk_swing=0 fill_every=1 fill_len=1 fill_style=3 fill_int=1 st_mode=2 st_gate=0.6 st_vdec=0.3 st_move=0.8 bits=0.2" },
        { "IDM", "Mutation Lab",             "Proud|Proud - Original;Swapout|Swapout - Ghosts;-;-", 3, "bpm=150 b1_remix=100 b1_dna_mode=2 b1_lock=30 b1_lk_ks=1 b1_lk_pos=0 b1_lk_acc=0 b1_lk_ghost=0 b2_mute=0 b2_level=0.5 b2_remix=70 b2_dna_mode=1 b2_role=2 b1_role=1 il_mode=5 flt_route=2 lp_mix=1 hp_mix=1 fm_mix=1 flt_morph=0.3 flt_auto=1" },
        { "IDM", "Glitch Cymbal Run",        "Cymbal Run|Cymbal Run - Original;Static|Static - Fill;-;-", 3, "bpm=140 b1_remix=75 b1_dna_mode=1 b1_lock=50 b2_mute=0 b2_level=0.45 b2_remix=90 b2_dna_mode=2 b2_lock=20 b2_pan=0.6 b1_pan=-0.5 st_mode=2 st_gate=0.4 st_pitch=5 bits=0.4 crunch=0.3" },
        { "IDM", "Reversal Theory",          "Chill|Chill - Original;-;-;-", 3, "bpm=128 b1_remix=70 b1_dna_mode=0 b1_lock=70 b1_lk_pos=0 b1_decay=0.6 fill_every=1 fill_len=2 fill_style=3 fill_int=0.8 cb_mix=0.7 cb_tune=1 cb_note=50 cb_fb=-0.8 flt_route=1 ftr_src=3 ftr_amt=-2 ftr_dec=0.08" },
        { "IDM", "Ratchet Garden",           "Bonecrack|Bonecrack - Ghosts;Crawler|Crawler - Original;-;-", 3, "bpm=145 b1_remix=65 b1_dna_mode=1 b1_lock=55 b1_lk_ghost=0 b2_mute=0 b2_level=0.4 b2_remix=55 b2_role=3 b1_role=1 il_mode=4 il_duck=0.7 st_mode=1 st_gate=0.7 st_vdec=0.5 st_pitch=1" },
        { "IDM", "Broken Clock",             "Swapout|Swapout - Original;Heatwave|Heatwave - Ghosts;-;-", 3, "bpm=100 b1_remix=90 b1_dna_mode=2 b1_lock=35 b1_lk_pos=0 b1_lk_swing=0 b1_grid=1 b1_rnd_start=1 b2_mute=0 b2_level=0.45 b2_rnd_chops=1 b2_rnd_amt=0.5 b2_pitch=-5 st_mode=3 st_vdec=0.4 reverb=0.3" },
        { "IDM", "Tape Stop Ritual",         "Haunt|Haunt - Dirty;-;-;-", 0, "bpm=160 b1_remix=95 b1_dna_mode=2 b1_lock=50 b1_lk_ks=1 b1_lk_pos=0 st_mode=3 st_gate=1 st_vdec=0.2 st_pitch=-4 st_move=0.5 fill_every=1 fill_style=4 fill_int=0.9 bits=0.15" },
        { "IDM", "Phrase Scanner",           "Stepper|Stepper - Room;Canyon|Canyon - Ghosts;-;-", 3, "bpm=152 b1_remix=60 b1_dna_mode=1 b1_lock=60 b2_mute=0 b2_level=0.45 b2_role=2 b1_role=1 il_mode=2 flt_route=2 lp_mix=1 bp_mix=1 nt_mix=1 fm_mix=1 flt_morph=0.5 phr_len=3 phr_amt=4 phr_shape=0 flt_auto=1" },
        // ---- BASS MUSIC / CHOPS
        { "BASS", "Wobble Chops",            "Static|Static - Dirty;-;-;-", 1, "bpm=140 b1_level=0.8 b1_remix=50 b1_dna_mode=1 b1_lock=70 b1_decay=0.4 crunch=0.7 lp_mix=1 lp_cut=0.45 res=0.8 flt_route=1 ftr_src=1 ftr_amt=3 ftr_dec=0.2 ftr_res=0.5 flt_auto=1 kick_lvl=1 snr_lvl=1" },
        { "BASS", "Halfstep Grinder",        "Halftime|Halftime - Dirty;Stubborn|Stubborn - Original;-;-", 1, "bpm=140 b1_remix=40 b1_lock=80 b2_mute=0 b2_level=0.5 b2_decay=0.3 b2_pitch=-7 b2_role=3 b1_role=1 il_mode=1 bits=0.35 crunch=0.6 hp_mix=0.5 hp_cut=0.2 flt_route=1" },
        { "BASS", "Comb Riddim",             "Two Step|Two Step - Dirty;-;-;-", 1, "bpm=150 b1_remix=45 b1_dna_mode=1 b1_lock=70 cb_mix=1 cb_tune=1 cb_note=38 cb_fb=0.9 res=0.6 flt_route=0 flt_auto=1 crunch=0.5 st_mode=0 st_gate=0.5 st_div=2 reverb=0.1" },
        { "BASS", "Stutter Snaps",           "Bonecrack|Bonecrack - Original;Proud|Proud - Fill;-;-", 1, "bpm=140 b1_remix=30 b2_mute=0 b2_level=0.5 b2_remix=60 b2_dna_mode=1 b2_role=4 b1_role=1 il_mode=1 fill_every=1 fill_len=1 fill_style=2 fill_int=1 st_mode=1 st_gate=0.8 st_vdec=0.6 st_pitch=3 crunch=0.45" },
        { "BASS", "Formant Growl Break",     "Crawler|Crawler - Dirty;-;-;-", 1, "bpm=145 b1_remix=55 b1_dna_mode=2 b1_lock=60 b1_lk_ks=1 fm_mix=1 fm_vow=0.6 bp_mix=0.6 bp_cut=0.35 res=0.85 flt_route=1 phr_len=1 phr_amt=2.5 phr_shape=2 ftr_src=2 ftr_amt=1.5 crunch=0.6 flt_auto=1" },
        { "BASS", "Neuro Pressure",          "Rolling|Rolling - Dirty;Haunt Tail|Haunt Tail - Dirty;-;-", 0, "bpm=174 b1_remix=70 b1_dna_mode=1 b1_lock=65 b1_lk_pos=0 b2_mute=0 b2_level=0.5 b2_pitch=-12 b2_decay=0.5 b2_role=2 b1_role=1 il_mode=5 bits=0.3 crunch=0.8 hp_mix=0.6 hp_cut=0.35 flt_route=1 flt_auto=1" },
        // ---- PROGRESSIVE HOUSE / GROOVE TOOLS
        { "PROG", "Clean Groove Tool",       "Chill|Chill - Original;-;-;-", 3, "bpm=126 b1_level=0.6 b1_remix=0 b1_pitch=0 b1_fit=1 reverb=0.05 crunch=0 hp_mix=0.8 hp_cut=0.45 flt_auto=1 interp=2 volume=0.45" },
        { "PROG", "Hypnotic Hats",           "Heatwave|Heatwave - Original;Cymbal Run|Cymbal Run - Room;-;-", 3, "bpm=124 b1_level=0.5 b1_remix=10 b1_lock=95 b2_mute=0 b2_level=0.4 b2_pan=0.5 b1_pan=-0.5 hp_mix=1 hp_cut=0.55 flt_auto=1 reverb=0.08 interp=2 crunch=0" },
        { "PROG", "Polished Proud",          "Proud|Proud - Room;-;-;-", 3, "bpm=128 b1_level=0.6 b1_remix=20 b1_lock=90 b1_lk_pos=1 fill_every=3 fill_len=1 fill_style=0 fill_int=0.4 hp_mix=0.7 hp_cut=0.4 reverb=0.12 size=0.5 interp=2 crunch=0" },
        { "PROG", "Deep Swapout",            "Swapout|Swapout - Room;Tightrope|Tightrope - Ghosts;-;-", 3, "bpm=122 b1_level=0.55 b1_remix=15 b2_mute=0 b2_level=0.35 b2_role=3 b1_role=1 il_mode=1 il_duck=0.3 lp_mix=0.5 lp_cut=0.8 hp_mix=0.6 hp_cut=0.4 flt_auto=1 interp=2 crunch=0 reverb=0.1" },
        { "PROG", "Peak Time Fills",         "Canyon|Canyon - Room;Stepper|Stepper - Fill;-;-", 3, "bpm=128 b1_level=0.6 b1_remix=25 b1_lock=85 b2_mute=0 b2_level=0.45 b2_role=4 b1_role=1 il_mode=1 fill_every=2 fill_len=2 fill_style=2 fill_int=0.7 phr_len=4 phr_amt=3 phr_shape=0 hp_mix=1 hp_cut=0.3 flt_auto=1 interp=2 crunch=0" },
        { "PROG", "Breakdown Riser",         "Verdict|Verdict - Room;-;-;-", 3, "bpm=126 b1_level=0.6 b1_remix=10 st_mode=1 st_gate=0.9 st_vdec=0.1 st_pitch=1 st_div=1 phr_len=4 phr_amt=5 phr_shape=0 hp_mix=1 hp_cut=0.2 flt_auto=1 reverb=0.3 size=0.8 interp=2 crunch=0" },
        // ---- FILTER PERFORMANCE
        { "FILTER", "Kick Sweeper",          "Haunt|Haunt - Original;-;-;-", 0, "bpm=172 b1_remix=20 lp_mix=1 lp_cut=0.35 res=0.75 ftr_src=1 ftr_amt=3 ftr_dec=0.15 ftr_res=0.6 flt_route=1 flt_auto=1" },
        { "FILTER", "Snare Talker",          "Static|Static - Original;-;-;-", 0, "bpm=170 b1_remix=25 fm_mix=1 fm_vow=0.2 ftr_src=2 ftr_amt=1.2 ftr_dec=0.12 flt_route=1 flt_auto=1 crunch=0.3" },
        { "FILTER", "Morph Journey",         "Two Step|Two Step - Original;-;-;-", 0, "bpm=174 b1_remix=30 flt_route=2 lp_mix=1 hp_mix=1 bp_mix=1 nt_mix=1 cb_mix=1 fm_mix=1 cb_tune=2 cb_fb=0.7 flt_morph=0 phr_len=4 phr_amt=1 phr_shape=2 flt_auto=1" },
        { "FILTER", "Tempo Comb",            "Stepper|Stepper - Original;-;-;-", 0, "bpm=168 b1_remix=15 cb_mix=1 cb_tune=2 cb_fb=0.85 cb_cut=0.5 res=0.5 flt_route=0 flt_auto=1 dry=0.5" },
        { "FILTER", "Phrase Arch 8",         "Rolling|Rolling - Original;-;-;-", 2, "bpm=174 b1_remix=20 lp_mix=1 lp_cut=0.3 res=0.7 phr_len=4 phr_amt=4 phr_shape=2 flt_route=1 flt_auto=1" },
        { "FILTER", "Serial Squeeze",        "Canyon|Canyon - Dirty;-;-;-", 0, "bpm=170 b1_remix=35 b1_dna_mode=1 flt_route=1 lp_mix=1 lp_cut=0.6 hp_mix=0.8 hp_cut=0.3 bp_mix=0.5 bp_cut=0.5 res=0.6 ftr_src=3 ftr_amt=1 ftr_dec=0.06 flt_auto=1 crunch=0.4" },
    };
    return P;
}

} // namespace mk
