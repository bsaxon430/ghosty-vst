# 👻 GhostBlock plugins · v1 beta

**22 free, open-source music plugins for Mac by GhostBlock**: synths, drum machines, MIDI generators and effects, each with its own hand-built interface. VST3 for FL Studio and Ableton Live, universal builds for Apple Silicon and Intel Macs (macOS 11 or newer).

> **Version 1, in beta.** Everything here is v1 and still in beta, so expect a few rough edges. Bug reports are welcome in [Issues](https://github.com/bsaxon430/ghosty-vst/issues).

🌐 **See them all:** [bsaxon430.github.io/ghosty-vst](https://bsaxon430.github.io/ghosty-vst/)

## ⬇️ Install

**One step:** open Terminal on your Mac and paste

```
curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/install-plugins.sh | bash
```

It downloads every plugin, puts them in `~/Library/Audio/Plug-Ins/VST3` and clears the macOS download block. Any older copy is moved to a backup folder, never deleted.

**By hand:** download [all plugins](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/GhostBlock-All-Plugins-mac-universal.zip) or one from the list below, unzip, and drag the `.vst3` into `~/Library/Audio/Plug-Ins/VST3`. If macOS blocks it, run `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3`.

**Then rescan:** FL Studio: Options › Manage plugins › Find more plugins. Ableton Live: Settings › Plug-Ins › Rescan.

## 🎛️ The plugins

### Synths & instruments

| Plugin | What it is | Download |
|---|---|---|
| **SÉANCE** | *12-operator hybrid synth*: Twelve spirits sit around a table, and every thread you draw between them is an FM connection. It pulls the engines of Ghosty Pro 2, Ghost Bass and Poltergeist 2 into one synth, with a step sequencer and its own sub and FX. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Seance-mac-universal.zip) |
| **Poltergeist** | *FM spirit synth*: The original FM ghost synth. Drag ghosts into orbit to link them, pick a sound from the tag row, then turn the six macro knobs: Haunt, Spectre, Possess, Drift, Veil and Curse. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Poltergeist-mac-universal.zip) |
| **Poltergeist 2** | *FM synth on the Six Sines engine*: A flying ghost machine you play by connecting hoses. Behind it is a back room for the wiring, a MORPH board that blends four sounds, an FX page of spectral effects, a SUB layer and a studio view for when you want plain knobs. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Poltergeist-2-mac-universal.zip) |
| **Ghosty** | *Free synth*: The first GhostBlock plugin: a small, friendly synth with the smiling ghost that started everything. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghosty-mac-universal.zip) |
| **Ghosty Pro** | *Synth*: The original Ghosty Pro: two oscillators, envelopes, a mod matrix and macros across SYNTH, MOD, BEND, TRAP, MORPH and FX pages, with a stack browser for trap sounds. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghosty-Pro-mac-universal.zip) |
| **Ghosty Pro 2** | *Wavetable synth on the Vital engine*: Vital's wavetable engine in the Ghosty look. MORPH spirit boards blend four sounds with a puck that can orbit on its own, and twelve ready-made boards are built in. Three looks: Ghost, Black and a watercolor Light. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghosty-Pro-2-mac-universal.zip) |
| **Ghost Bass** | *Bass synth on the Surge XT engine*: Surge XT's sound engine turned into a bass machine, with a clean Bombaz sub underneath and a blend board for mixing bass sounds without them clashing. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Bass-mac-universal.zip) |
| **Ghost Atlas** | *Sample instrument*: A sample-based instrument where every sound gets its own painted scene. Layer sounds, browse the library and run them through a full effects rack. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Atlas-mac-universal.zip) |

### Drums & beats

| Plugin | What it is | Download |
|---|---|---|
| **Mr Mickens** | *Break machine*: A dark hardcore break machine: four break bays, 100 breaks, break DNA, fills, stutter, a deck and a drum machine underneath. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Mr-Mickens-mac-universal.zip) |
| **Ghosty Beats** | *Drum machine*: A drum machine and sequencer with its own Ghost Kit sounds, a tracker, rhythm lab, pads and song mode. Opens in a rainy neon city, with an ink-and-watercolor light look too. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghosty-Beats-mac-universal.zip) |
| **Block Step** | *Vertical drum tracker*: Sketch Aphex Twin, DnB and trap patterns step by step. GENERATE, LINEAR and WILD write beats for you, with piano and acid lines in the same key. Opens in Ghost City. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Block-Step-mac-universal.zip) |
| **Ghost House** | *House drum tracker*: Block Step's house-music twin, with its own GhostBlock drum packs, swing and a groove engine built for four-on-the-floor. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-House-mac-universal.zip) |
| **Ghost Break** | *Jungle and DnB break instrument*: Four bays of real breaks with RANDOM START and RANDOM BREAKS, a drum sequencer underneath, and Phantom FX effects on every bay. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Break-mac-universal.zip) |

### MIDI & generators

| Plugin | What it is | Download |
|---|---|---|
| **ArpEZator** | *Arp and melody writer*: Professor Bernstein picks a mood (Sunrise, Dusk, Midnight, Trap or Hip Hop) and writes chords, an arp, a melody and a bass line. Edit them in the piano roll and drag the MIDI or audio into your song. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/ArpEZator-mac-universal.zip) |
| **Ghost Hum** | *Sing or beatbox to MIDI*: Hum a melody or beatbox a groove and it comes out as MIDI notes you can drag straight into your DAW. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Hum-mac-universal.zip) |
| **Ghost Slice** | *Sound-to-music generator*: Drop in a vocal or any sound and press GENERATE. It makes musical takes from it: instruments, pads and chops, all snapped to your key. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Slice-mac-universal.zip) |

### Effects & sound design

| Plugin | What it is | Download |
|---|---|---|
| **Phantom FX** | *24-effect rack*: The open-source 24-effect rack: parallel lanes, modulation, a perform page and a 3D spatial page, in three hand-painted looks. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Phantom-FX-mac-universal.zip) |
| **Ghost Box** | *Effect box*: The original 24-effect fun box that Phantom FX grew out of: scenes, morph, a board and motion. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Box-mac-universal.zip) |
| **Ghost Lab** | *Experimental sound organism*: Put a sound in the specimen tube and stretch it, freeze it, smear it or mutate it. Built on CDP8 and PaulXStretch, with an Under the Hood page for every engine. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Lab-mac-universal.zip) · [effect](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Lab-FX-mac-universal.zip) |
| **Ghost Mosaic** | *Sample mosaic (instrument + effect)*: Samplebrain's engine with a GhostBlock face: it rebuilds one sound out of tiny pieces of other sounds. Comes as an instrument and an effect. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Mosaic-mac-universal.zip) · [effect](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Mosaic-FX-mac-universal.zip) |
| **Ghosty Spectrum** | *Spectral sound lab*: Paint the sound itself. Draw on the spectrum, turn images into sound, and work through paint, ghost, chaos and DNA pages, then loop and arrange what you made. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghosty-Spectrum-mac-universal.zip) |
| **Ghost Voice** | *Voice creation suite*: Drag in a vocal, chop it, play it on the keys, and use the sing engine to make new voices from your own takes. | [VST3](https://github.com/bsaxon430/ghosty-vst/releases/latest/download/Ghost-Voice-mac-universal.zip) |

## 🧩 Source

Every plugin is free software. Many are built on open-source projects (Surge XT, Vital, Six Sines, CDP, Samplebrain, Steam Audio, JUCE and more); each one's licence and credits ship inside its download.

- `main`: the original Ghosty synth
- `src/*` branches: the source snapshot of each plugin
- `builds` branch: the universal Mac build that makes the [Releases](https://github.com/bsaxon430/ghosty-vst/releases)

Windows builds aren't part of v1 beta yet.
