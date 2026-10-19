# 👻 Ghosty

Five ghost instruments by **GhostBlock**, free for Windows and Mac.

| | |
|---|---|
| 👻 **Ghosty** | Wavetable synth: 81 presets, 16 wave shapes + draw your own, LFO (synced, 5 targets), glide / mono / legato, 4 filter types + drive + its own envelope, mod wheel + velocity, scales, chords, arpeggiator, slide keyboard, ghost XY pad, **save your own sounds** |
| 🥁 **Ghosty Beats** | Drum machine, rebuilt: 16 tracks left-to-right, **61 built-in drums + drag in your own samples**, 10 kits (Trap, Drill, Boom Bap, Lofi, House, Dubstep, Afrobeats, Phonk, Reggaeton, Jersey Club), per-step velocity / **rolls & triplets** / chance / nudge, 16 patterns (16-64 steps), save & load patterns, **SONG** arranger, **MPC pads** with record + note repeat, glue / DJ filter / echo / reverb / crush / drive, stutter + tape stop, **drag out WAV, MIDI or every stem** |
| 🌈 **Ghosty Spectrum** | Rebuilt: **paint a loop on the sound spectrum**. Pick from 52 synth sounds (Poltergeist engine), 61 drums or your own samples, **DRAW** melodies (snapped to your key), **DROP** drums (low = kick, high = hats), **CONNECT** notes into slides, watch a live spectrogram paint itself, then **drag the loop or stems out**. Save / load canvases |
| 🔮 **Poltergeist** | FM spirit synth: 4 ghosts you drag onto each other to haunt (FM), **drop any audio file in** (granular, wavetable, vocal chops), talking "yoi" filter, bubbles, 16-step rhythm sequencer, comb / sync / OTT, scale lock + one-finger chords + arp, **SAMPLE LAB** (cut, stretch-to-bars, flex lines, crossfades, keys built from many files, keep-tempo pitch, drag the result out), type-what-you-want **SUMMON** ("yoi growl", "bubbly pluck", "tearout", "chop"), 38 presets |
| 🦄 **Mr. Mickens** | Dark hardcore **drum & bass break machine**: 100 classic break rhythms (Amen, Think, Apache, Funky Drummer, Hot Pants and more, re-played on an original vintage kit, 5 variations each) plus **your own breaks** (drop on a bay, or keep them in the *Breaks folder*, listed first). 4 bays chopped to the grid with **NUDGE** (snaps to the grid you pick), **RANDOM START** / **RANDOM CHOPS** / **DICE STARTS** for a new combo every time, pitch / chop length / pan, **fit to tempo**. Hard kick / snare / hats machine with **per-drum sidechain** (each bay ducks only from the drums you choose). Crunch + bits, **every filter in parallel** (low / high / band / notch / comb / formant), chorus, reverb, **stutter with its own filter + envelope**. Drag out WAV or stems, save / load |

---

## ⬇️ Step 1: Download

| Your computer | Click to download |
|---|---|
| 🪟 **Windows 10 / 11** | [**Ghosty-Windows.zip**](https://github.com/bsaxon430/ghosty-vst/releases/download/latest/Ghosty-Windows.zip) |
| 🍎 **Mac** (M1/M2/M3/M4 or Intel, macOS 11+) | [**Ghosty-Mac.zip**](https://github.com/bsaxon430/ghosty-vst/releases/download/latest/Ghosty-Mac.zip) |

Not sure? If your computer has a Start button, it's Windows. If it has an Apple logo in the top-left corner, it's a Mac.

All versions: [Releases page](https://github.com/bsaxon430/ghosty-vst/releases/latest)

---

## 🪟 Step 2 (Windows): Install

1. Open your **Downloads** folder, **right-click** `Ghosty-Windows.zip` and choose **Extract All** > **Extract**.
2. In the folder that opens, **double-click `Install Ghosty.bat`** and click **Yes**.
   - If a blue box says *"Windows protected your PC"*, click **More info** > **Run anyway**.
3. When it says **Done!**, press any key. Now go to Step 3 for your DAW.

<details><summary>Rather do it by hand?</summary>

Copy the four `.vst3` folders from the `VST3` folder into `C:\Program Files\Common Files\VST3`.
</details>

## 🍎 Step 2 (Mac): Install

1. Open **Downloads** and double-click `Ghosty-Mac.zip` to unzip it.
2. Open the **Ghosty for Mac** folder and **double-click `Install Ghosty.command`**.
3. If the Mac says it *can't be opened* (this is normal for free plug-ins):
   - Open **System Settings** > **Privacy & Security**, scroll down, click **Open Anyway**, then **Open**.
   - Double-click `Install Ghosty.command` again.
4. When the Terminal window says **Done!**, close it. Now go to Step 3.

<details><summary>Still blocked?</summary>

Open the **Terminal** app, type `bash ` (with a space), drag `Install Ghosty.command` into the window, and press Return.
</details>

---

## 🎛️ Step 3: Open it in your DAW

| DAW | What to do | Where Ghosty shows up |
|---|---|---|
| **Ableton Live** (Win/Mac) | Windows: **Options > Preferences > Plug-Ins**, turn on **Use VST3 Plug-in System Folders**, click **Rescan**. Mac: **Settings > Plug-Ins > Rescan** | Browser > **Plug-Ins > VST3 > GhostBlock** |
| **FL Studio** (Win/Mac) | **Options > Manage plugins > Find more plugins** | Click **+** in the Channel Rack, or Plugin database > Generators |
| **Logic Pro / GarageBand** (Mac) | Just open it (uses the AU version) | New Software Instrument track > **AU Instruments > GhostBlock** |
| **Bitwig** | **Settings > Locations**, make sure VST3 is on, it rescans by itself | Browser > Instruments > GhostBlock |
| **Reaper** | **Options > Preferences > Plug-ins > VST > Re-scan** | Add FX > **VST3i** > Ghosty |
| **Studio One** | **Options > Locations > VST Plug-ins > Reset Blocklist**, restart | Browser > Instruments > GhostBlock |
| **Cubase** | **Studio > VST Plug-in Manager > Rescan** | Instrument track > GhostBlock |

Load any of them (**Ghosty**, **Ghosty Beats**, **Ghosty Spectrum**, **Poltergeist**, **Mr. Mickens**) as an **instrument** on a MIDI track.

### No DAW? Use the apps
The **Standalone** folder has apps that work on their own: double-click and play.
(Mac, first time: right-click the app > **Open**.)

---

## 🆘 Help

- **It doesn't show up:** do the rescan step again, then restart your DAW.
- **Mac: "damaged" or "can't be opened":** run `Install Ghosty.command` again (it fixes this).
- **Ghosty Beats is silent:** press play in your DAW (it follows the DAW's tempo), or play MIDI notes C1 to G1.
- **Ghosty Spectrum:** pick a scene at the top, hit **PLAY**, and click on the colorful field. Use **+ TONE** / **+ DRUM** to place sounds.
- **Mr. Mickens is silent:** press play in your DAW, or hit **PLAY** in the plugin. Bays 2-4 start muted; un-mute them.
- **Mr. Mickens keys / pads:** C1 kick, D1 snare, F#1 hat, A#1 open hat. **C2 / C#2 / D2 / D#2** = hold Bay 1-4 (plays its break from its start point until you let go). **C3 and up** = one slice per key from the bay set to **KEYS**. On screen: **HOLD TO PLAY** in each bay, **HIT** on each drum lane.
- **Mr. Mickens real breaks:** click **BREAKS FOLDER**, put your own break WAVs there (sub-folders become sections), then **RESCAN**. They stay on your computer.
- **Update:** download again and run the installer; it replaces the old version.

---

<details><summary>For coders: build from source</summary>

JUCE 8 + CMake. Windows: run `INSTALL-GHOSTY.bat`. Anywhere:
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Every push to `main` builds Windows + Mac in GitHub Actions and updates the **latest** release.
</details>
