#!/usr/bin/env python3
# UNDER THE HOOD, in plain words: every process + mode Ghost Lab ships gets a friendly name, a category (what it does to the
# sound), a one-line description and a longer "what it does"; every engine control gets a friendly name. The engine's own
# names stay on the page, small, under ours.
#
# Learned from the other CDP front ends: Soundshaper (one-word names, process tables, only the relevant controls),
# SoundThread (categories by what a process does to the sound, plain titles, a description per process and per control).
# SoundThread's process_help.json (MIT, Copyright (c) 2025 Jonathan Higgins; cdp/soundthread/) supplies the longer
# descriptions where it covers a process; everything else is written here.
#
#   gen-hood-friendly.py  ->  Source/Core/HoodFriendly.inc
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CATS = [
    # id, title, blurb
    ("time",    "TIME",          "Stretch, freeze, reverse, shuffle"),
    ("pitch",   "PITCH",         "Shift, glide, fold, speed, stack"),
    ("tone",    "TONE",          "Reshape the spectrum's colour"),
    ("texture", "TEXTURE",       "Grains, chorus, noise, crush"),
    ("space",   "SPACE + LEVEL", "Echo, pan, stereo, loudness"),
    ("blend",   "BLEND TWO",     "Needs a second sound"),
    ("tools",   "DATA TOOLS",    "Analysis and data for other processes"),
]

# key: "prog name" or "prog name mode" -> (category, title, one line, [SoundThread key or None], [about override])
P = {
    "pvoc anal 1": ("tools", "Analyse", "Turn a sound into spectral data (the engine's first step for spectral work)", "pvoc_anal_1"),
    "pvoc anal 2": ("tools", "Analyse: Envelope Only", "Spectral analysis that keeps only the spectral envelope"),
    "pvoc anal 3": ("tools", "Analyse: Magnitudes Only", "Spectral analysis that keeps only the channel magnitudes"),
    "pvoc synth": ("tools", "Resynthesise", "Turn spectral data back into sound", "pvoc_synth"),
    "pvoc extract": ("tone", "Channel Filter", "Rebuild the sound from only some analysis channels (odd, even, a band)"),
    "stretch spectrum 1": ("tone", "Stretch Spectrum Up", "Pull the partials above a split point apart: bells, metal, glass", "stretch_spectrum_1"),
    "stretch spectrum 2": ("tone", "Stretch Spectrum Down", "Squeeze or spread the partials below a split point", "stretch_spectrum_2"),
    "stretch time 1": ("time", "Time Stretch", "Make it longer or shorter without changing its pitch"),
    "stretch time 2": ("tools", "Time Stretch: Length Only", "Work out how long a time stretch would be (makes no sound)"),
    "strange shift 1": ("pitch", "Frequency Shift", "Move every partial by the same number of Hz: instantly inharmonic"),
    "strange shift 2": ("pitch", "Frequency Shift Above", "Frequency-shift only the part of the spectrum above a split"),
    "strange shift 3": ("pitch", "Frequency Shift Below", "Frequency-shift only the part of the spectrum below a split"),
    "strange shift 4": ("pitch", "Frequency Shift Band", "Frequency-shift only a band you choose", "strange_shift_4"),
    "strange shift 5": ("pitch", "Frequency Shift Outside", "Frequency-shift everything outside a band you choose"),
    "strange glis 1": ("pitch", "Shepard Tones", "Endless rising or falling glides inside the sound's own shape", "strange_glis_1"),
    "strange glis 2": ("pitch", "Inharmonic Glides", "Gliding inharmonic partials inside the sound's own shape", "strange_glis_2"),
    "strange glis 3": ("pitch", "Self Glide", "The sound's own partials glide up or down", "strange_glis_3"),
    "strange waver 1": ("pitch", "Waver", "Swing back and forth between harmonic and inharmonic", "strange_waver_1"),
    "strange waver 2": ("pitch", "Waver (Set Stretch)", "Waver, choosing how inharmonic the far end gets"),
    "strange invert 1": ("tone", "Invert Spectrum", "Loud partials go quiet and quiet ones get loud", "strange_invert_1"),
    "strange invert 2": ("tone", "Invert, Keep Shape", "Invert the spectrum but keep the sound's own loudness shape"),
    "focus accu": ("time", "Accumulate", "Every band rings on until something louder arrives: sustain trails", "focus_accu"),
    "focus exag": ("tone", "Exaggerate", "Push the spectrum's shape to extremes", "focus_exag"),
    "focus focus": ("tone", "Focus", "Pull the energy onto the strongest peaks", "focus_focus"),
    "focus fold": ("pitch", "Fold Into Range", "Octave-fold every partial into a pitch range you set", "focus_fold"),
    "focus freeze 1": ("time", "Freeze Loudness", "Hold the channel levels still between times you choose"),
    "focus freeze 2": ("time", "Freeze Pitches", "Hold the channel frequencies still between times you choose"),
    "focus freeze 3": ("time", "Freeze Both", "Hold levels and frequencies still between times you choose"),
    "focus hold": ("time", "Hold", "Stop on chosen moments and hold them: the sound gets longer"),
    "focus step": ("time", "Stepped Freeze", "Step through the sound in frozen frames", "focus_step"),
    "blur avrg": ("tone", "Average", "Smooth the energy across neighbouring channels", "blur_avrg"),
    "blur blur": ("time", "Blur", "Smear the spectrum over time", "blur_blur"),
    "blur suppress": ("tone", "Remove Loudest", "Take out the loudest partials, moment by moment", "blur_suppress"),
    "blur chorus 1": ("texture", "Chorus: Levels", "Randomise the partials' levels"),
    "blur chorus 2": ("texture", "Chorus: Pitches", "Randomise the partials' frequencies"),
    "blur chorus 3": ("texture", "Chorus: Pitches Up", "Randomise the partials' frequencies upwards only"),
    "blur chorus 4": ("texture", "Chorus: Pitches Down", "Randomise the partials' frequencies downwards only"),
    "blur chorus 5": ("texture", "Chorus", "Randomise levels and frequencies: chorus, then grains, then noise", "blur_chorus_5"),
    "blur chorus 6": ("texture", "Chorus Up", "Randomise levels, and frequencies upwards only"),
    "blur chorus 7": ("texture", "Chorus Down", "Randomise levels, and frequencies downwards only"),
    "blur drunk": ("time", "Drunk Walk", "Stagger randomly back and forth through the sound", "blur_drunk"),
    "blur shuffle": ("time", "Shuffle", "Cut the sound into groups of frames and reorder them"),
    "blur weave": ("time", "Weave", "Step forwards and backwards through the frames in a pattern"),
    "blur noise": ("texture", "Add Noise", "Pour noise into the spectrum", "blur_noise"),
    "blur scatter": ("texture", "Thin Randomly", "Randomly thin out the spectrum", "blur_scatter"),
    "blur spread": ("texture", "Spread Peaks", "Spread the spectral peaks for a controlled noisiness", "blur_spread"),
    "modify loudness 1": ("space", "Gain", "Turn it up or down by a factor", "modify_loudness_1"),
    "modify loudness 2": ("space", "Gain in dB", "Turn it up or down in decibels"),
    "modify loudness 3": ("space", "Normalise", "Bring the loudest peak up to full level (or a level you set)"),
    "modify loudness 4": ("space", "Force Level", "Force the loudest peak to a level"),
    "modify loudness 5": ("space", "Match Second Sound", "Make its loudest peak match the second sound's"),
    "modify loudness 6": ("space", "Flip Phase", "Turn the waveform upside down"),
    "modify loudness 7": ("tools", "Find Loudest", "Tell you which of several sounds is loudest (makes no sound)"),
    "modify loudness 8": ("space", "Even Out Levels", "Bring several sounds to the level of the loudest"),
    "modify loudness 11": ("space", "Loudness Shape", "Shape the loudness with an envelope stretched over the sound"),
    "modify loudness 12": ("space", "Loudness Shape (dB)", "Shape the loudness with a dB envelope stretched over the sound"),
    "modify space 1": ("space", "Pan", "Place or move the sound in the stereo field"),
    "modify space 2": ("space", "Swap Left + Right", "Mirror the stereo image"),
    "modify space 3": ("tools", "Mirror Pan Data", "Mirror a pan data file (left becomes right)"),
    "modify space 4": ("space", "Narrow Stereo", "Squeeze the stereo image towards the middle"),
    "modify speed 1": ("pitch", "Tape Speed", "Change speed and pitch together, like a tape machine"),
    "modify speed 2": ("pitch", "Tape Speed in Semitones", "Change speed and pitch by a number of semitones", "modify_speed_2"),
    "modify speed 3": ("tools", "Tape Speed: Info", "Report what a varying speed change would do (makes no sound)"),
    "modify speed 4": ("tools", "Tape Speed: Info in Semitones", "Report what a varying semitone change would do (makes no sound)"),
    "modify speed 5": ("time", "Speed Up / Slow Down", "Accelerate or decelerate towards a goal time", "modify_speed_5"),
    "modify speed 6": ("pitch", "Vibrato", "Add a vibrato"),
    "modify revecho 1": ("space", "Delay", "Echo with feedback and a dry / wet mix"),
    "modify revecho 2": ("space", "Modulated Delay", "Echo whose delay time wobbles, for chorus and flange", "modify_revecho_2"),
    "modify revecho 3": ("space", "Stadium Echo", "Big public-address style echoes"),
    "modify brassage 1": ("texture", "Grain Pitch Shift", "Pitch-shift by re-building the sound from grains", "modify_brassage_1"),
    "modify brassage 2": ("texture", "Grain Time Stretch", "Stretch by re-building the sound from grains", "modify_brassage_2"),
    "modify brassage 3": ("texture", "Grain Reverb", "A cloud of grains around the sound, like a reverb"),
    "modify brassage 4": ("texture", "Scramble", "Scramble the order of the grains", "modify_brassage_4"),
    "modify brassage 5": ("texture", "Granulate", "Break the sound into a stream of separate grains", "modify_brassage_5"),
    "modify brassage 6": ("texture", "Brassage", "Grain size, density, pitch and space in one go", "modify_brassage_6"),
    "modify brassage 7": ("texture", "Brassage: Everything", "Every grain control, each with a random range ('full monty')"),
    "modify sausage": ("texture", "Multi-Sound Brassage", "Grains from several sounds scrambled together", "modify_sausage"),
    "modify radical 1": ("time", "Reverse", "Play it backwards", "modify_radical_1"),
    "modify radical 2": ("time", "Shred", "Cut into chunks and shuffle them, again and again"),
    "modify radical 3": ("time", "Tape Scrub", "Hand-wind it back and forth over a tape head", "modify_radical_3"),
    "modify radical 4": ("texture", "Bit Crush", "Lower the sample rate and bit depth", "modify_radical_4"),
    "modify radical 5": ("texture", "Ring Modulate", "Multiply it with a sine wave: metallic, robotic", "modify_radical_5"),
    "modify radical 6": ("blend", "Cross Modulate", "Multiply two sounds together", "modify_radical_6"),
    "modify radical 7": ("texture", "Quantise Bits", "Reduce the bit resolution"),
    "modify spaceform": ("tools", "Make Pan Wave", "Write a sine-shaped pan path for PAN to use (makes data, not sound)"),
    "modify scaledpan": ("space", "Pan Along a Path", "Pan with a path that is stretched to the sound's length"),
    "modify findpan": ("tools", "Find Pan Position", "Tell you where a sound sits in a stereo file (makes no sound)"),
    "modify convolve 1": ("blend", "Convolve", "Imprint the second sound's body onto the first"),
    "modify convolve 2": ("blend", "Convolve + Transpose", "Convolve, transposing the second sound over time"),
    "modify stack": ("pitch", "Stack", "Stack transposed copies on top of each other: instant chords", "modify_stack"),
    "modify shudder": ("space", "Shudder", "Random stereo tremors and bursts"),
    "combine make": ("tools", "Build From Pitch + Formants", "Make a sound from pitch data and formant data alone"),
    "combine make2": ("tools", "Build From Pitch + Formants + Envelope", "Make a sound from pitch, formant and loudness data"),
    "combine sum": ("blend", "Add Spectra", "Add the two sounds' spectra together", "combine_sum"),
    "combine diff": ("blend", "Subtract Spectra", "Take the second sound's spectrum away from the first", "combine_diff"),
    "combine interleave": ("blend", "Interleave Frames", "Alternate slices of frames from each sound", "combine_interleave"),
    "combine max": ("blend", "Loudest of Each", "Keep whichever sound is loudest in each channel", "combine_max"),
    "combine mean 1": ("blend", "Average: Pitches", "Average the two sounds (levels and pitches)", "combine_mean_1"),
    "combine mean 2": ("blend", "Average: Frequencies", "Average the two sounds (levels and frequencies)"),
    "combine mean 3": ("blend", "Average Pitch, Levels of 1st", "Average pitches, keep the first sound's levels", "combine_mean_3"),
    "combine mean 4": ("blend", "Average Freq, Levels of 1st", "Average frequencies, keep the first sound's levels"),
    "combine mean 5": ("blend", "Average Pitch, Levels of 2nd", "Average pitches, keep the second sound's levels"),
    "combine mean 6": ("blend", "Average Freq, Levels of 2nd", "Average frequencies, keep the second sound's levels"),
    "combine mean 7": ("blend", "Average Pitch, Loudest Levels", "Average pitches, keep the louder level of the two"),
    "combine mean 8": ("blend", "Average Freq, Loudest Levels", "Average frequencies, keep the louder level of the two"),
    "combine cross": ("blend", "Cross Synthesis", "Give the first sound the second sound's levels: talking instruments", "combine_cross"),
    "morph glide": ("blend", "Glide Between Frames", "Glide between two single frozen frames", "morph_glide"),
    "morph bridge 1": ("blend", "Bridge", "Bridge from one sound into the other", "morph_bridge_1"),
    "morph bridge 2": ("blend", "Bridge: Quieter Level", "Bridge, following the quieter of the two moment to moment", "morph_bridge_2"),
    "morph bridge 3": ("blend", "Bridge: Level of 1st", "Bridge, following the first sound's level", "morph_bridge_3"),
    "morph bridge 4": ("blend", "Bridge: Level of 2nd", "Bridge, following the second sound's level"),
    "morph bridge 5": ("blend", "Bridge: Level 1st to 2nd", "Bridge, the level moving from the first sound's to the second's"),
    "morph bridge 6": ("blend", "Bridge: Level 2nd to 1st", "Bridge, the level moving from the second sound's to the first's"),
    "morph morph 1": ("blend", "Morph", "Morph one sound into the other", "morph_morph_1"),
    "morph morph 2": ("blend", "Morph (Smooth Curve)", "Morph along a smooth S-curve"),
    "repitch getpitch 1": ("tools", "Get Pitch Data", "Track the pitch and save it as pitch data"),
    "repitch getpitch 2": ("tools", "Get Pitch as Text", "Track the pitch and save it as time / Hz pairs"),
    "repitch analenv": ("tools", "Get Loudness Data", "Save the loudness of each frame as data"),
    "formants get": ("tools", "Get Formant Data", "Save the sound's formants (its vowel shape) as data"),
    "envel extract 1": ("tools", "Get Envelope", "Save the loudness envelope as data"),
    "envel extract 2": ("tools", "Get Envelope as Text", "Save the loudness envelope as time / level pairs"),
}

# the engine's own control names (cdparams) -> ours
C = {
    "2ND_FILE_OFFSET": "Second sound starts at", "2nd_FILE_ENTRY_TIME": "Second sound comes in at",
    "ACCELERATION": "Acceleration", "AMP_INTERP_AT_END": "Level blend at end", "AMP_INTERP_AT_START": "Level blend at start",
    "AMP_INTERP_END": "Level morph ends at", "AMP_INTERP_EXPONENT": "Level morph curve", "AMP_INTERP_START": "Level morph starts at",
    "AMP_SPREAD": "Level randomness", "ANALWINDOW_OVERLAP": "Analysis overlap", "ANALYSIS_POINTS": "Analysis size",
    "ATTACK_TIME": "Attack is at", "AT_TIME": "At time", "AVERAGE_CHUNKLENGTH": "Chunk length",
    "AVERAGING_SPAN": "Channels to average", "BALANCE_CHANNEL_LEVELS": "Balance left + right", "BASE_FRQ_OF_STRETCH": "Stretch starts at (Hz)",
    "BIT_RESOLUTION": "Bits", "BLURRING_FACTOR": "Blur amount", "BOTTOM_FRQ": "Lowest frequency", "BOTTOM_RESYNTH_CHANNEL": "Lowest channel",
    "CHANNELS_TO_COMPARE": "Channels to compare", "CHANNELS_TO_SUPRESS": "How many to remove", "CROSSOVER": "Crossover",
    "CUT_SCATTER": "Cut randomness", "CYCLES_PER_SECOND": "Vibrato speed", "CYCLE_LENGTH": "Pan cycle length",
    "DATAREDUCTION_(SEMIT)": "Data reduction (semitones)", "DATA_REDUCTION": "Data reduction", "DECAY_RATE": "Decay",
    "DEGREE_OF_SPREADING": "Spread", "DELAYED_SIGNAL_IN_MIX": "Wet mix", "DELAY_(MS)": "Delay time (ms)", "DENSITY": "Grain density",
    "DENSITY_LIMIT": "Density range to", "DON'T_NORMALISE": "Don't normalise", "DURATION": "Length", "ENDSPLICE_(MS)": "Grain fade-out (ms)",
    "ENDSPLICE_LIMIT_(MS)": "Fade-out range to (ms)", "ENDTIME_IN_FILE1": "Ends in first sound at", "ENTRY_DELAY_OF_MODULATION": "Wobble starts after",
    "EVENT_WIDTH_MAXIMUM": "Longest shudder", "EVENT_WIDTH_MINIMUM": "Shortest shudder", "EXAGGERATION": "Exaggeration",
    "EXPONENT": "Curve", "EXPONENTIAL_SPLICES": "Curved fades", "FEEDBACK": "Feedback", "FILTER_BANDWIDTH": "Peak width",
    "FORMANT_BANDS": "Formant detail", "FORMANT_QUICKSEARCH": "Quick formant search", "FREEZE_DATA": "Freeze times",
    "FREQUENCY": "Shudder rate", "FRQ-BLOCKS_TO_KEEP": "Blocks to keep", "FRQ_DIVIDE": "Split point (Hz)",
    "FRQ_INTERP_AT_END": "Pitch blend at end", "FRQ_INTERP_AT_START": "Pitch blend at start", "FRQ_INTERP_END": "Pitch morph ends at",
    "FRQ_INTERP_EXPONENT": "Pitch morph curve", "FRQ_INTERP_START": "Pitch morph starts at", "FRQ_RANGE_OF_BLOCK": "Block size",
    "FRQ_SPREAD": "Pitch randomness", "GAIN": "Gain", "GLISS_RATE": "Glide speed", "GOAL_TIME": "Goal time",
    "GRAINSIZE_(MS)": "Grain size (ms)", "GRAINSIZE_LIMIT_(MS)": "Grain size range to (ms)", "GRAIN_LOUDNESS_RANGE": "Grain loudness",
    "GROUPSIZE_(WINDOWS)": "Group size (frames)", "HIGHEST_UPWARD_TRANSPOSITION": "Highest pitch (semitones)", "HIGH_FRQ": "Band top (Hz)",
    "HIGH_FRQ_LIMIT": "Highest frequency", "HIGH_PITCH_LIMIT_(HZ)": "Highest pitch (Hz)", "HOLD_DATA": "Hold times",
    "HOW_MUCH_OF_OUPUT_TO_MAKE": "How much to make", "IN-TUNE_RANGE_(SEMIT)": "In-tune range (semitones)",
    "INPUT_CHAN_TO_EXTRACT_(-ve_N:_OUTPUT_SPATIALISE_TO_N_CHANS)": "Input channel", "INPUT_GAIN": "Input gain",
    "INPUT_PRESCALING": "Input level", "INTERPOLATION": "How much", "INVERT_DELAYED_SIGNAL": "Flip the echo",
    "KEEP_PITCH_ZEROS": "Keep unpitched gaps", "KEEP_RANDOM_SET_ONLY": "Keep a random set only", "LEAFSIZE_(WINDOWS)": "Slice size (frames)",
    "LENGTH_OF_TAIL_TO_SOUND": "Tail length", "LEVEL": "Level", "LEVEL_LOSS_WITH_DISTANCE": "Fade with distance",
    "LINEAR_FRQ_SHIFT": "Shift (Hz)", "LOG_INTERP_CHANGING_FRQ": "Smooth changing shift", "LOUDNESS_DEPTH_MAXIMUM": "Deepest shudder",
    "LOUDNESS_DEPTH_MINIMUM": "Lightest shudder", "LOUDNESS_ENVELOPE": "Loudness envelope", "LOUDNESS_RANGE_LIMIT": "Loudness range to",
    "LOWEST_DOWNWARD_TRANSPOSITION": "Lowest pitch (semitones)", "LOW_FRQ": "Band bottom (Hz)", "LOW_FRQ_LIMIT": "Lowest frequency",
    "LOW_FRQ_MODULATION_DEPTH": "Wobble depth", "LOW_PITCH_LIMIT_(HZ)": "Lowest pitch (Hz)", "MAX_PEAKS_TO_FIND": "Peaks to find",
    "MAX_SPECTRAL_STRETCH": "Most inharmonic", "MINIMUM_OUTPUT_DURATION": "Length at least", "MIN_WINDOWS_TO_CONFIRM_PITCH": "Frames to confirm a pitch",
    "MODULATING_FREQUENCY": "Ring frequency (Hz)", "MODULATION_FRQ:_-ve_FOR_RANDOM": "Wobble speed (minus = random)", "MORE_BODY_IN_SPECTRUM": "More body",
    "NARROWING": "Narrowing", "NOISE_SATURATION": "Noise amount", "NORMALISE": "Normalise", "NORMALISE_OUTPUT": "Normalise",
    "NO_DECCELERATION": "No slow-down", "NUMBER_OF_ECHOS": "Number of echoes", "NUMBER_OF_ITEMS": "Number of copies",
    "NUMBER_OF_SHREDS": "Times to shred", "OUTFILE_DURATION": "Result length", "OUTFILE_TIMES": "Times in the result",
    "OUTPUT_LENGTH_(SECS)": "Result length (s)", "OVERALL_GAIN": "Overall gain", "PAN_POSITION": "Pan position", "PAN_WIDTH": "Pan width",
    "PARTIAL_SPACING_(HZ)": "Partial spacing (Hz)", "PHASE_OF_MODULATION": "Wobble phase", "PITCHSHIFT(SEMITONES)": "Pitch shift (semitones)",
    "PITCHSHIFT_(SEMIT)": "Pitch shift (semitones)", "PITCHSHIFT_LIMIT_(SEMIT)": "Pitch range to (semitones)", "PITCH_NOT_INTERPOLATED": "Stepped pitch",
    "PRESCALING": "Input level", "PROCESS_DEPTH": "Depth", "QUANTISATION": "Time step", "RANDOMISATION": "Randomness",
    "RANDOM_SEED": "Random seed", "RANGE_IN_WINDOWS": "Wander range (frames)", "REJECT_ZERO_STEPS": "Always move",
    "RETAIN_SUBZERO_AMPLITUDES": "Keep negative levels", "SAMPLING_RATE_DIVISION": "Sample rate divide", "SCATTER": "Grain scatter",
    "SCRUBS_END_NO_EARLIER_THAN": "Scrubs end after", "SCRUBS_START_NO_LATER_THAN": "Scrubs start before", "SEARCHRANGE_(MS)": "Search range (ms)",
    "SEE_STACK_LEVELS": "Show stack levels", "SELECT_A_CHANNEL": "Odd or even channels", "SEMITONE_DEPTH": "Vibrato depth (semitones)",
    "SEMITONE_TRANSPOSITION_OF_CONVOLVING_FILE": "Transpose second sound", "SHUFFLE_DATA": "Shuffle pattern",
    "SIGNAL_TO_NOISE_RATIO_(dB)": "Signal to noise (dB)", "SINGLE_SCRUB_(version_7+_only)": "One forward scrub only", "SMOOTHER": "Smoother",
    "SPATIAL_POSITION": "Grain position", "SPATIAL_POSITION_LIMIT": "Position range to", "SPATIAL_SPREAD": "Stereo spread",
    "STACK_LEAN": "Top copy loudness", "STADIUM_SIZE_MULTIPLIER": "Stadium size", "STARTSPLICE_(MS)": "Grain fade-in (ms)",
    "STARTSPLICE_LIMIT_(MS)": "Fade-in range to (ms)", "STARTTIME_IN_FILE": "Start at", "STARTTIME_IN_FILE1": "Starts in first sound at",
    "START_PHASE": "Start position", "START_TIME": "Start at", "STRETCH_EXPONENT": "Stretch curve", "TIMESHRINK": "Speed through the sound",
    "TIMESHRINK_LIMIT": "Speed range to", "TIMESTEP": "Step every", "TIMESTRETCH_MULTIPLIER": "Stretch amount", "TOP_FRQ": "Highest frequency",
    "TOP_OF_SPECTRUM_STRETCH": "Stretch at the top", "TOP_RESYNTH_CHANNEL": "Highest channel", "TRANSPOSITION": "Transpose",
    "TRANSPOSITION_DATA": "Interval between copies", "USE_OTHER_ALGORITHM": "Other tracking method", "VALID_HARMONICS_COUNT": "Harmonics needed",
    "VIBRATO_FRQ": "Waver speed", "WEAVE_DATA": "Weave pattern", "WINDOWCNT_FOR_STABILITY": "Frames for stability", "WINDOW_SIZE_(MS)": "Window (ms)",
    "ZERO_CHANNELS_OUTSIDE_RANGE": "Silence outside the range",
    # usage words with no control in the engine's table (data files in and out)
    "PITCHFILE": "Pitch data", "FORMANTFILE": "Formant data", "ENVFILE": "Envelope data", "BFIL": "Pitch text file",
    "PFIL": "Pitch data file", "OUTENVFILE": "Envelope data file", "OUTBRKFILE": "Envelope text file",
    # switched choices
    "PITCHWISE": "by pitch", "FRQWISE": "by frequency",
}

# our own longer words where theirs describe their app's workflow
ABOUT = {
    "pvoc anal 1": "Converts the sound into spectral frames: how loud every frequency band is, moment by moment. The spectral processes "
                   "run this for you before they start; run it on its own to see what the analysis does (the result is played back as sound).",
    "pvoc synth": "Turns spectral frames back into an ordinary sound. The spectral processes run this for you when they finish.",
}

def nice(name):
    """an engine name with no entry above: ENGINE_NAME_(MS) -> Engine name (ms)"""
    s = name.replace("_", " ").strip()
    s = s.lower()
    return s[:1].upper() + s[1:]

def main():
    st = json.load(open(os.path.join(HERE, "soundthread", "process_help.json")))
    cat = json.loads("".join(re.findall(r'R"HOODJSON\((.*?)\)HOODJSON"', open(os.path.join(ROOT, "Source/Core/HoodCatalog.inc")).read(), re.S)))
    dlg = json.loads("".join(re.findall(r'R"HOODJSON\((.*?)\)HOODJSON"', open(os.path.join(ROOT, "Source/Core/HoodDialogs.inc")).read(), re.S)))
    keys = []
    for p in cat["processes"]:
        modes = sorted({m for f in p["forms"] for m in f["modes"]})
        for m in (modes or [0]):
            keys.append((p["prog"], p["name"], m))
    out = {"cats": [{"id": c[0], "title": c[1], "blurb": c[2]} for c in CATS], "entries": [], "controls": {}}
    missing = []
    for prog, name, m in keys:
        k = f"{prog} {name} {m}" if m else f"{prog} {name}"
        e = P.get(k) or P.get(f"{prog} {name}")
        if e is None: missing.append(k); continue
        about = ""
        if len(e) > 3 and e[3] and e[3] in st:
            about = st[e[3]].get("description", "").strip()
            # their words about their own app (threads, nodes, menus) don't apply here, and the UI names no project
            sents = re.split(r'(?<=[.!?])\s+', about)
            about = " ".join(x for x in sents if not re.search(r'thread|node|Help >|CDP|SoundThread|patch', x, re.I)).strip()
        if k in ABOUT: about = ABOUT[k]
        out["entries"].append({"prog": prog, "name": name, "mode": m, "cat": e[0], "title": e[1], "short": e[2], "about": about,
                               "credit": "SoundThread" if about else ""})
    names = set()
    for d in dlg["dialogs"]:
        for it in d["items"]:
            for n in (it.get("name"), it.get("name2"), it.get("name3")):
                if n: names.add(n)
    for n in ("PITCHFILE", "FORMANTFILE", "ENVFILE", "BFIL", "PFIL", "OUTENVFILE", "OUTBRKFILE"): names.add(n)
    for n in sorted(names):
        out["controls"][n] = C.get(n, nice(n))
    unnamed = sorted(n for n in names if n not in C)
    if missing: raise SystemExit("no friendly entry for: " + ", ".join(missing))
    text = json.dumps(out, indent=0, ensure_ascii=True)
    chunks = [text[i:i + 12000] for i in range(0, len(text), 12000)]
    with open(os.path.join(ROOT, "Source/Core/HoodFriendly.inc"), "w") as f:
        f.write("// GENERATED by cdp/gen-hood-friendly.py (our plain words for UNDER THE HOOD; longer descriptions from SoundThread,\n")
        f.write("// MIT, Copyright (c) 2025 Jonathan Higgins, where it covers a process). Do not edit.\n")
        f.write("static const char* const kHoodFriendlyChunks[] = {\n")
        for c in chunks: f.write('R"HOODJSON(' + c + ')HOODJSON",\n')
        f.write("nullptr\n};\n")
    print(len(out["entries"]), "entries,", len(out["controls"]), "control names (", len(unnamed), "by rule:", ", ".join(unnamed), ")")

main()
