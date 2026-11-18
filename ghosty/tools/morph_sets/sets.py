# Ghosty Pro 2 MORPH SETS: a base sound + four ghosts (A = the base), each a variation of the base's knobs
# ops: ('add',name,d) ('set',name,v) ('mul',name,m) ('fx',fx,mix)  ; motion: 0 STILL 1 CIRCLE 2 EIGHT 3 WANDER 4 GLIDE 5 STEP
# division index: 4 1/8, 7 1/4, 10 1/2, 12 1 BAR, 13 2 BARS, 14 4 BARS, 15 8 BARS ; blend 0 smooth 1 islands 2 nearest2
SETS = [
 dict(name="SUB TO SNARL", base="Rubber Sub", motion=0, div=12, blend=0, depth=0.25, ghosts=[
   ("SUB", []),
   ("OPEN", [('add','filter_1_cutoff',26),('set','filter_1_resonance',0.42)]),
   ("SNARL", [('add','filter_1_cutoff',14),('set','distortion_drive',14),('set','distortion_mix',0.8)]),
   ("BITE", [('add','filter_1_cutoff',34),('set','filter_1_resonance',0.6),('add','osc_3_level',0.25)]) ]),
 dict(name="REESE ROTATION", base="Neon Reese", motion=4, div=13, blend=0, depth=0.25, ghosts=[
   ("REESE", []),
   ("TURN", [('set','osc_1_wave_frame',256),('set','osc_2_wave_frame',220),('add','filter_1_cutoff',8)]),
   ("HOLLOW", [('set','filter_1_blend',1.0),('add','filter_1_cutoff',10)]),
   ("SWARM", [('mul','osc_1_unison_detune',1.7),('mul','osc_2_unison_detune',1.7),('add','filter_1_cutoff',12),('set','chorus_dry_wet',0.6)]) ]),
 dict(name="WOBBLE BOARD", base="Wobble Ghost", motion=2, div=10, blend=0, depth=0.35, ghosts=[
   ("WOB", []),
   ("CLOSED", [('add','filter_1_cutoff',-22)]),
   ("SCREAM", [('add','filter_1_cutoff',28),('set','filter_1_resonance',0.7)]),
   ("CRUNCH", [('add','filter_1_cutoff',12),('set','distortion_drive',16),('add','osc_1_distortion_amount',0.35)]) ]),
 dict(name="ACID SWAP", base="Acid Ghost", motion=2, div=12, blend=0, depth=0.3, ghosts=[
   ("ACID", []),
   ("SQUELCH", [('add','filter_1_cutoff',20),('set','filter_1_resonance',0.85)]),
   ("DEEP", [('add','filter_1_cutoff',-16),('set','filter_1_resonance',0.5)]),
   ("BURN", [('add','filter_1_cutoff',8),('set','distortion_drive',18),('set','distortion_mix',1.0)]) ]),
 dict(name="HALO SKY", base="Supersaw Halo", motion=4, div=14, blend=0, depth=0.25, ghosts=[
   ("HALO", []),
   ("WIDE", [('mul','osc_1_unison_detune',1.8),('mul','osc_2_unison_detune',1.8),('set','chorus_dry_wet',0.7)]),
   ("SOFT", [('add','filter_1_cutoff',-26)]),
   ("SPACE", [('set','reverb_dry_wet',0.55),('add','filter_1_cutoff',-8)]) ]),
 dict(name="CHOIR FOG", base="Haunted Choir", motion=3, div=14, blend=0, depth=0.3, ghosts=[
   ("CHOIR", []),
   ("VOWEL", [('set','osc_1_wave_frame',220)]),
   ("DUSK", [('add','filter_1_cutoff',-14)]),
   ("CATHEDRAL", [('set','reverb_dry_wet',0.6),('set','chorus_dry_wet',0.8)]) ]),
 dict(name="GLASS TIDES", base="Glass Fog", motion=4, div=14, blend=0, depth=0.25, ghosts=[
   ("FOG", []),
   ("SHIMMER", [('set','osc_3_wave_frame',250),('add','osc_3_level',0.2),('add','filter_1_cutoff',14)]),
   ("DEEP", [('add','filter_1_cutoff',-22),('add','osc_2_level',0.15)]),
   ("SWIRL", [('set','phaser_dry_wet',0.9),('mul','osc_1_unison_detune',1.6)]) ]),
 dict(name="RHODES MOODS", base="Ghost Rhodes", motion=0, div=12, blend=0, depth=0.25, ghosts=[
   ("RHODES", []),
   ("FELT", [('add','filter_1_cutoff',-22)]),
   ("SUITCASE", [('set','chorus_dry_wet',0.75),('set','osc_2_wave_frame',140)]),
   ("DRIVEN", [('set','distortion_drive',24),('add','filter_1_cutoff',16)]) ]),
 dict(name="STAB SWEEP", base="Club Chord Stab", motion=4, div=12, blend=0, depth=0.25, ghosts=[
   ("STAB", []),
   ("DARK", [('add','filter_1_cutoff',-24)]),
   ("BRIGHT", [('add','filter_1_cutoff',22),('set','filter_1_resonance',0.6)]),
   ("SHIFT", [('set','osc_1_wave_frame',220),('set','osc_2_wave_frame',40)]) ]),
 dict(name="PLUCK SHAPES", base="Crystal Pluck", motion=5, div=7, blend=2, depth=0.25, ghosts=[
   ("PLUCK", []),
   ("GLASS", [('add','filter_1_cutoff',20),('set','osc_1_wave_frame',220)]),
   ("MUTED", [('add','filter_1_cutoff',-12)]),
   ("ECHO", [('set','delay_dry_wet',0.55),('set','reverb_dry_wet',0.45)]) ]),
 dict(name="ORGAN TIDE", base="Tidal Organ Pad", motion=4, div=14, blend=0, depth=0.25, ghosts=[
   ("ORGAN", []),
   ("DRAWBAR", [('set','osc_1_wave_frame',220),('set','osc_2_wave_frame',40)]),
   ("CHAPEL", [('add','filter_1_cutoff',-20),('set','reverb_dry_wet',0.55)]),
   ("JET", [('set','flanger_dry_wet',0.9),('add','filter_1_cutoff',8)]) ]),
 dict(name="SEANCE DRIFT", base="Seance Texture", motion=3, div=15, blend=0, depth=0.35, ghosts=[
   ("TEXTURE", []),
   ("TURN", [('set','osc_1_wave_frame',200)]),
   ("BLOOM", [('mul','osc_1_unison_detune',1.8),('set','flanger_dry_wet',0.85),('add','filter_1_cutoff',8)]),
   ("DUST", [('add','filter_1_cutoff',-18),('set','reverb_dry_wet',0.6)]) ]),
]
