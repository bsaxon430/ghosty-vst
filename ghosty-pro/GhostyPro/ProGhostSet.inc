// THE GHOST SET (2026-11-21): Ghosty Pro's hand-made factory sounds, version 1 beta. { category, name, FX scene, spec }.
// Made the way the Ghosty Pro 2 bank is voiced: one clear idea per sound, clean (little FX, nothing smeared), playable from the
// first note, the four STUDIO macros wired (mc= template). Levels: ProGhostSetTrims.inc (ProTests levels).

// ---- BASS
{ "Bass", "Rubber Sub", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Classic o1_pos=0 sub_lvl=.55 sub_shp=Sine f_type=lp24 cutoff=.36 res=.05 fenv=.18 f_a=.001 f_d=.12 f_s=0 glide=.03 attack=.001 decay=.6 sustain=.9 release=.08 cl_mono=180" },
{ "Bass", "Velvet 808", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Classic o1_pos=0 sub_lvl=.4 mod=env2>pitch:.18 e2_a=.001 e2_d=.07 e2_s=0 attack=.001 decay=2.2 sustain=0 release=.6 curve=.7 glide=.08 f_type=lp12 cutoff=.5 drive=.18 cl_mono=200" },
{ "Bass", "Neon Reese", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Saw o1_uni=4 o1_det=.24 o1_spr=.35 o2_on=1 o2_tbl=Saw o2_oct=-1 o2_lvl=.45 sub_lvl=.3 f_type=lp24 cutoff=.5 res=.12 f_drive=.25 mod=lfo1>cut:.08 l1_rate=.25 l1_shape=Triangle cl_mono=150" },
{ "Bass", "Wobble Ghost", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=FMGrowl o1_pos=.3 o2_on=1 o2_tbl=Saw o2_lvl=.4 sub_lvl=.35 f_type=lp24 cutoff=.42 res=.3 f_drive=.3 mod=lfo1>cut:.45 mod=lfo1>pos1:.3 l1_sync=1 l1_div=1/8 l1_shape=Sine l1_trig=1 cl_mono=150" },
{ "Bass", "House Organ Bass", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Organ o1_pos=.25 sub_lvl=.35 attack=.002 decay=.32 sustain=.35 release=.08 f_type=lp12 cutoff=.55 fenv=.25 f_d=.18 f_s=.2 cl_mono=160" },
{ "Bass", "Square Bounce", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Pulse o1_pos=.1 sub_lvl=.3 sub_shp=Square sub_oct=-1_oct attack=.001 decay=.28 sustain=.15 release=.06 f_type=lp24 cutoff=.4 res=.22 fenv=.42 f_a=.001 f_d=.16 f_s=0 glide=.04" },
{ "Bass", "Acid Spirit", "dub", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Saw o1_pos=0 f_type=lp24 cutoff=.34 res=.62 fenv=.55 f_a=.001 f_d=.22 f_s=.05 vel_f=.3 f_drive=.35 glide=.06 legato=1 decay=.4 sustain=.7" },
{ "Bass", "Tape Mono", "tape", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Saw o2_on=1 o2_tbl=Pulse o2_fine=-7 o2_lvl=.5 sub_lvl=.25 f_type=lp24 cutoff=.44 res=.15 fenv=.3 f_d=.25 f_s=.15 drive=.15 glide=.05" },
{ "Bass", "Deep Pluck Bass", "", "mc=aggr mac1=.5 mac2=.5 @bass o1_tbl=Hollow o1_pos=.3 sub_lvl=.45 attack=.001 decay=.5 sustain=0 release=.2 curve=.7 f_type=lp24 cutoff=.38 fenv=.38 f_d=.2 f_s=0" },

// ---- LEADS
{ "Leads", "Supersaw Halo", "wide", "mc=lead mac1=.5 mac2=.5 @ssaw o1_uni=9 o1_det=.36 o1_spr=.95 o2_on=1 o2_tbl=Saw o2_oct=1 o2_uni=3 o2_det=.2 o2_lvl=.25 f_type=lp24 cutoff=.74 res=.05 attack=.004 release=.35 verb=.2 echo=.12" },
{ "Leads", "Square Hero", "", "mc=lead mac1=.5 mac2=.5 @lead @vib o1_tbl=Pulse o1_pos=.05 o1_uni=2 o1_det=.08 f_type=lp24 cutoff=.66 res=.08 fenv=.15 f_d=.4 f_s=.6 glide=.06 echo=.18 verb=.15" },
{ "Leads", "Ghost Whistle", "space", "mc=lead mac1=.5 mac2=.5 @lead @vib o1_tbl=Classic o1_pos=0 o1_oct=1 nz_lvl=.05 nz_col=.8 attack=.04 release=.4 f_type=lp12 cutoff=.68 glide=.1 echo=.2 verb=.3" },
{ "Leads", "Sync Scream", "", "mc=lead mac1=.5 mac2=.5 @lead o1_tbl=Sync o1_pos=.3 mod=env2>pos1:.45 e2_a=.001 e2_d=.5 e2_s=.2 o1_uni=2 o1_det=.1 f_type=lp24 cutoff=.7 res=.1 f_drive=.3 glide=.04 echo=.15" },
{ "Leads", "Formant Singer", "space", "mc=lead mac1=.5 mac2=.5 @lead @vib o1_tbl=Formant o1_pos=.3 mod=lfo2>pos1:.25 l2_rate=.35 attack=.03 f_type=lp12 cutoff=.72 glide=.08 chorus=.15 verb=.3" },
{ "Leads", "Soft Saw Lead", "", "mc=lead mac1=.5 mac2=.5 @lead o1_tbl=Saw o1_uni=3 o1_det=.12 o1_spr=.5 f_type=lp24 cutoff=.6 res=.06 fenv=.18 f_d=.35 f_s=.5 attack=.01 glide=.05 echo=.2 verb=.18" },
{ "Leads", "Glass Flute", "space", "mc=lead mac1=.5 mac2=.5 @lead @vib o1_tbl=Glass o1_pos=.15 nz_lvl=.08 nz_col=.7 attack=.05 release=.35 f_type=lp12 cutoff=.64 verb=.32 echo=.15" },

// ---- KEYS
{ "Keys", "Ghost Rhodes", "", "mc=pluck mac1=.5 mac2=.5 @keys o1_tbl=Classic o1_pos=.08 o2_on=1 o2_tbl=Glass o2_oct=1 o2_lvl=.18 mod=vel>cut:.2 decay=1.9 sustain=.18 release=.6 f_type=lp12 cutoff=.6 fenv=.22 chorus=.25 phaser=.1 verb=.18" },
{ "Keys", "Lofi Felt Piano", "lofi", "mc=pluck mac1=.5 mac2=.5 @keys o1_tbl=Hollow o1_pos=.2 o2_on=1 o2_tbl=Classic o2_lvl=.35 attack=.004 decay=1.4 sustain=.08 release=.45 curve=.8 f_type=lp12 cutoff=.48 fenv=.25 f_d=.6 verb=.2" },
{ "Keys", "Club Chord Stab", "", "mc=pluck mac1=.5 mac2=.5 o1_tbl=Saw o1_uni=5 o1_det=.25 o1_spr=.8 o2_on=1 o2_tbl=Organ o2_lvl=.35 attack=.001 decay=.35 sustain=.12 release=.25 f_type=lp24 cutoff=.48 res=.12 fenv=.48 f_a=.001 f_d=.24 f_s=0 echo=.18 verb=.2" },
{ "Keys", "Bell Organ", "", "mc=stack mac1=.5 mac2=.5 o1_tbl=Organ o1_pos=.4 o2_on=1 o2_tbl=Glass o2_oct=1 o2_lvl=.25 attack=.004 decay=1.2 sustain=.7 release=.4 f_type=lp12 cutoff=.7 chorus=.3 verb=.2" },
{ "Keys", "Warm EP Wide", "wide", "mc=pluck mac1=.5 mac2=.5 @keys o1_tbl=Classic o1_pos=.15 o1_uni=3 o1_det=.1 o1_spr=.8 o2_on=1 o2_tbl=Harmonics o2_pos=.2 o2_lvl=.2 decay=2.2 sustain=.22 release=.7 f_type=lp12 cutoff=.56 fenv=.2 chorus=.2 verb=.22" },

// ---- PLUCKS
{ "Plucks", "Crystal Pluck", "crystal", "mc=pluck mac1=.5 mac2=.5 @pluck o1_tbl=Glass o1_pos=.35 o2_on=1 o2_tbl=Classic o2_oct=1 o2_lvl=.2 cutoff=.5 fenv=.45 decay=.5 release=.4 echo=.22 verb=.25" },
{ "Plucks", "Kalimba Ghost", "", "mc=pluck mac1=.5 mac2=.5 @pluck o1_tbl=MODAL o1_eh=.45 o1_et=.55 o1_em=.35 attack=.001 decay=.9 release=.6 f_type=lp12 cutoff=.72 fenv=.15 verb=.22" },
{ "Plucks", "Trap Bell Pluck", "", "mc=pluck mac1=.5 mac2=.5 @bell o1_tbl=FMMetal o1_pos=.2 o2_on=1 o2_tbl=Classic o2_lvl=.35 decay=1.2 release=1 f_type=lp12 cutoff=.7 verb=.25 echo=.18" },
{ "Plucks", "Hollow Pizz", "", "mc=pluck mac1=.5 mac2=.5 @pluck o1_tbl=Hollow o1_pos=.45 o1_uni=2 o1_det=.08 decay=.28 release=.22 cutoff=.46 fenv=.4 f_d=.18 verb=.18" },
{ "Plucks", "Future Pluck Chord", "", "mc=pluck mac1=.5 mac2=.5 @pluck o1_tbl=Saw o1_uni=6 o1_det=.3 o1_spr=.9 chord=1 decay=.4 release=.32 cutoff=.42 res=.18 fenv=.55 f_d=.26 echo=.25 verb=.25 fx_lim_on=1 fx_lim_ceiling=-.5 fx_lim_gain=3 fx_lim_release=60" },
{ "Plucks", "Digital Marimba", "", "mc=pluck mac1=.5 mac2=.5 @pluck o1_tbl=Digital o1_pos=.3 o2_on=1 o2_tbl=Classic o2_oct=-1 o2_lvl=.3 decay=.45 release=.35 f_type=lp12 cutoff=.6 fenv=.3 f_d=.2 verb=.18" },

// ---- PADS
{ "Pads", "Warm Blanket", "space", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Saw o1_uni=6 o1_det=.22 o2_on=1 o2_tbl=Classic o2_oct=-1 o2_lvl=.35 f_type=lp24 cutoff=.46 res=.05 mod=lfo2>cut:.06 l2_rate=.12 chorus=.25 verb=.4" },
{ "Pads", "Glass Fog", "shimmer", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Glass o1_pos=.3 mod=lfo2>pos1:.3 l2_rate=.08 o2_on=1 o2_tbl=Choir o2_lvl=.25 nz_lvl=.04 nz_col=.8 f_type=lp12 cutoff=.6 verb=.45" },
{ "Pads", "Haunted Choir", "space", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Choir o1_pos=.35 o1_uni=4 o1_det=.18 mod=lfo2>pos1:.2 l2_rate=.1 f_type=lp12 cutoff=.62 attack=.6 release=2 chorus=.2 verb=.45" },
{ "Pads", "Tidal Organ Pad", "", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Organ o1_pos=.3 o1_uni=3 o1_det=.12 phaser=.35 mod=lfo2>cut:.08 l2_rate=.15 f_type=lp12 cutoff=.6 verb=.35" },
{ "Pads", "Night Drone", "space", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Harmonics o1_pos=.4 mod=lfo2>pos1:.4 l2_rate=.05 o2_on=1 o2_tbl=Saw o2_oct=-1 o2_uni=4 o2_lvl=.3 f_type=lp24 cutoff=.42 res=.1 attack=1.5 release=3 verb=.5" },
{ "Pads", "Soft Strings", "", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Saw o1_uni=7 o1_det=.2 o1_spr=.9 attack=.45 release=1.2 f_type=lp24 cutoff=.52 res=.04 l1_rate=4.8 l1_fade=1 mod=lfo1>fine:.06 chorus=.3 verb=.35" },

// ---- BELLS
{ "Bells", "Ghost Bells", "shimmer", "mc=pluck mac1=.5 mac2=.5 @bell o1_tbl=Glass o1_pos=.5 o2_on=1 o2_tbl=FMMetal o2_oct=1 o2_lvl=.2 f_type=lp12 cutoff=.8 verb=.35" },
{ "Bells", "Music Box", "", "mc=pluck mac1=.5 mac2=.5 @bell o1_tbl=Classic o1_pos=0 o1_oct=1 o2_on=1 o2_tbl=Glass o2_oct=2 o2_lvl=.2 decay=1.6 release=1.4 f_type=lp12 cutoff=.76 verb=.28 echo=.12" },

// ---- RHYTHM & ARPS
{ "Rhythm & Arps", "Gated Ghosts", "", "mc=stack mac1=.5 mac2=.5 @pad o1_tbl=Saw o1_uni=6 o1_det=.25 attack=.01 release=.3 mod=lfo1>vol:-.9 l1_sync=1 l1_div=1/16 l1_shape=Square l1_trig=1 f_type=lp24 cutoff=.58 verb=.25" },
{ "Rhythm & Arps", "Pump Chords", "", "mc=stack mac1=.5 mac2=.5 o1_tbl=Saw o1_uni=5 o1_det=.24 o1_spr=.85 chord=1 attack=.004 decay=1 sustain=.8 release=.3 mod=lfo1>vol:-.7 l1_sync=1 l1_div=1/4 l1_shape=Saw_Up l1_trig=1 f_type=lp24 cutoff=.58 verb=.2" },
{ "Rhythm & Arps", "Night Arp", "", "mc=pluck mac1=.5 mac2=.5 @pluck @arp o1_tbl=Pulse o1_pos=.2 cutoff=.48 fenv=.45 decay=.25 release=.2 echo=.28 verb=.2" },

// ---- FX & ATMOS
{ "FX & Atmos", "Spirit Riser", "space", "o1_tbl=Saw o1_uni=6 o1_det=.3 nz_lvl=.18 nz_col=.6 attack=4 decay=1 sustain=1 release=1.5 f_type=lp12 cutoff=.25 mod=env2>cut:.6 e2_a=4 e2_d=.1 e2_s=1 mod=env2>pitch:.12 verb=.45 mc=stack mac1=.5 mac2=.5" },
{ "FX & Atmos", "Haunted Impact", "", "o1_tbl=FMGrowl o1_pos=.4 o1_oct=-1 sub_lvl=.5 nz_lvl=.25 nz_col=.3 mod=env2>pitch:-.35 e2_a=.001 e2_d=.6 e2_s=0 attack=.001 decay=2.5 sustain=0 release=1.5 f_type=lp24 cutoff=.6 verb=.45 mc=aggr mac1=.5 mac2=.5" },
{ "FX & Atmos", "Seance Texture", "space", "o1_tbl=TEXTURE o1_esrc=Choir o1_eh=.4 o1_et=.55 o1_em=.6 o1_tsize=.7 o1_tfb=.3 attack=1 decay=2 sustain=.9 release=2.5 f_type=lp12 cutoff=.56 verb=.45 mc=stack mac1=.5 mac2=.5" },
