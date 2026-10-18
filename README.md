# 👻 Ghosty

Four glitchy ghost instruments by **GhostBlock**, free for Windows and Mac.

| | |
|---|---|
| 👻 **Ghosty** | Wavetable synth: 81 presets, 16 wave shapes, scales, chords, arpeggiator, slide keyboard, ghost XY pad |
| 🥁 **Ghosty Beats** | 8-track drum machine, synced to your DAW, stutter, tape stop, reverse |
| 🌈 **Ghosty Spectrum** | Place tones and drums right on the 20 Hz to 20 kHz spectrum and watch them glow |
| 🔮 **Poltergeist** | FM spirit synth: 4 ghosts you drag onto each other to haunt (FM), 6 big macros (HAUNT, SPECTRE, POSSESS, DRIFT, VEIL, CURSE), type-what-you-want **SUMMON** ("dark trap bell", "wobble growl", "808 slide"), 26 presets |

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

Load **Ghosty**, **Ghosty Beats** or **Ghosty Spectrum** as an **instrument** on a MIDI track.

### No DAW? Use the apps
The **Standalone** folder has apps that work on their own: double-click and play.
(Mac, first time: right-click the app > **Open**.)

---

## 🆘 Help

- **It doesn't show up:** do the rescan step again, then restart your DAW.
- **Mac: "damaged" or "can't be opened":** run `Install Ghosty.command` again (it fixes this).
- **Ghosty Beats is silent:** press play in your DAW (it follows the DAW's tempo), or play MIDI notes C1 to G1.
- **Ghosty Spectrum:** pick a scene at the top, hit **PLAY**, and click on the colorful field. Use **+ TONE** / **+ DRUM** to place sounds.
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
