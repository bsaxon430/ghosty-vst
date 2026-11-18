# Demo export gate: every way audio or MIDI can leave a GhostBlock plugin

Owner rule: in DEMO mode (no licence key) it must be impossible to get MIDI or audio files out of any of the eight
plugins. Everything else in the demo works. The other demo limits are the 2 s drop-out every 20 s and a reopened
project starting at the default sound.

## How it works

- **One gate:** `Shared/ExportGate.h`, `ghosty::exportAllowed (license, uiForHint, "plugin.path-id")`. Every path below asks
  it on the message thread, before it writes a file, opens a save dialog, starts a drag or sets the clipboard.
- **Refused (demo):** the UNLOCK button blinks (`UnlockButton::nudge`) and a small hint shows near it:
  "Export is part of the full version. Click UNLOCK...". The hint is non-modal and fades by itself. The caller stops:
  nothing is written, no dialog opens, no drag starts. Paths that already showed a toast keep it.
- **Back-stops:** processor writers, render workers, timers and the late drag after a render re-check silently with
  `ghosty::exportUnlocked`. The drag helper `exportgate::dragFiles (license, ...)` also refuses while locked.
- **One raw drag:** the only `performExternalDragDropOfFiles` left in the collection is inside `exportgate::dragFiles`.
  The only export save dialog is `exportgate::saveDialog`.
- **Fail closed:**
  - `WaveRegion` (the shared waveform WAV grip) exports nothing unless its host wires `canExport` to the gate.
  - `ProcessLabPanel::Host::allowExport (ui)` defaults to refused.
- **Not exports (still allowed in demo):** the plugin's own formats. These are presets, scenes, boards, snapshots,
  setups, beats and `.ghostywt` user wavetables (see Ghosty Pro below), plus project state. Exception: a format is gated
  when it holds rendered audio or MIDI that a common tool turns into WAV or MIDI.
- **CLAP:** the CLAP builds compile the same shared code target as VST3/AU/Standalone (clap-juce-extensions wraps it),
  so they carry the same gate. Each plugin's `_CLAP` target was built with it.
- **License.h:** the old `License::allowExport()` modal message box is gone, so a leftover call fails to compile.

## Tests

- **`demogate` mode in every plugin's test app:**

  | Plugin | Command |
  |---|---|
  | Ghosty Pro | `ProTests demogate` |
  | Ghost Box | `GhostBoxTests demogate` |
  | ArpEZator | `ArpEZatorTests demogate` |
| Ghost Slice | `GhostSliceTests demogate` |
  | Mr. Mickens | `MickensTests demogate` |
  | Poltergeist | `PoltergeistTests demogate` |
  | Ghosty Beats | `BeatsTests demogate` |
  | Ghosty Spectrum | `SpectrumLabTests demogate` |

  Run them under `xvfb-run -a`. Each one:
  1. Moves HOME, TMPDIR and XDG_* into a fresh sandbox folder.
  2. Builds the real processor and editor.
  3. Triggers every path below through the real UI code: mouse gestures on the drag pills, grips and tiles, button and
     menu handlers, the waveform grip.
  4. Checks, while LOCKED: the gate refused that id, the hint showed, no external drag, no save dialog, no new audio or
     MIDI file anywhere in the sandbox, and the clipboard unchanged.
  5. Checks, while UNLOCKED: the gate allowed it, and a real drag of existing files, a saved file, or a clipboard
     change happened.

  The path count must equal the inventory count below. Shared harness: `Shared/tests/DemoGateHarness.h`.
- **Test hooks:** test targets only (every `*Tests` target gets `GHOSTY_TEST_HOOKS=1`, plugins never do).
  - `License::testForce()` forces locked or unlocked.
  - `exportgate::log()` records the gate answers, hints, drags and saves.
  - Drags and save dialogs never reach the OS.
- **`ExportGateLint`** (Shared tests) is the source check that stops a new path slipping in. It fails when:
  - a `performExternalDragDropOfFiles` call appears outside ExportGate.h.
  - a `saveMode` chooser, clipboard write, `createWriterFor`, MIDI `writeTo` or MIDI/WAV `replaceWithData` line has no
    `// GATE: <id>`, `// GATE: via-caller` or `// NOT-EXPORT: <why>` annotation.
  - a gate id in a plugin source is missing from that plugin's demogate test, or a test id is missing from the sources.

## Demo drop-out (License.h DemoGate)

- **Every plugin:** 2 s of silence every 20 s while locked. The store page FAQ (ghosty repo `docs/store/index.html`), the
  EULA and the UNLOCK tooltip all say 20 s, which is correct. The only "45 s" was a stale note in
  `ops/claude-memory/ghostblock-plugins-status.md`.
- **Call site:** all eight processors call `demo.process (buffer, license.isUnlocked())` at the end of `processBlock`.
  The early returns before it are only silent, cleared-buffer paths: bypass, no channels, chunked re-entry.
- **Ghosty Spectrum fix:** it never called `demo.prepare (sampleRate)`, so its drop-out ran on a 44.1 kHz clock. That
  meant every 18.4 s at 48 kHz and every 9.2 s at 96 kHz; it now calls it in `prepareToPlay`.
- **Measured** by `checkDropout` in the harness: Ghost Box has a 1.94 s gap at 18.03 s while locked and none when
  unlocked.

## Inventory: 64 gated paths

- **Columns:**
  - "file:line" is where the gate is asked.
  - "Before" is the state before this change:
    - "modal" = refused through the old `License::allowExport()` message box, with no UNLOCK hint.
    - "silent" = refused with no hint.
    - "no" = it exported in demo.
- **Kinds:** Drag = external drag to the DAW / Finder. File = a saved or written file. Auto = a background write to
  disk; it has no hint because no click started it, but locked it writes nothing.
- **Plugins:**

| Plugin | Paths |
|---|---|
| ArpEZator | 11 |
| Ghost Box | 3 |
| Ghosty Pro | 3 |
| Mr. Mickens | 5 |
| Poltergeist | 0 |
| Ghosty Beats | 11 |
| Ghosty Spectrum | 26 |
| Ghost Slice | 3 |

### ArpEZator: 11 paths (priority 1)

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| arp.pill.midi | lane DRAG pills (chords / arp / melody / bass-808) and DRAG MIDI (mix): .mid | Drag | ArpEZator/ArpEditor.cpp:252 | modal |
| arp.pill.midi-stems | DRAG MIDI with STEMS: one .mid per lane | Drag | ArpEditor.cpp:252 | modal |
| arp.pill.audio | DRAG AUDIO, or Alt-drag of a lane pill: renders WAV, then drags | Drag | ArpEditor.cpp:252 (+ late drag :2128 re-checks) | modal |
| arp.pill.audio-stems | DRAG AUDIO with STEMS: WAV per lane + kick | Drag | ArpEditor.cpp:252 | modal |
| arp.pill.click-midi | click on a DRAG pill: saves a .mid copy to Documents/Ghosty/ArpEZator/MIDI | File | ArpEditor.cpp:260 | modal |
| arp.pill.click-audio | click on DRAG AUDIO: renders WAV(s) to Documents/Ghosty/ArpEZator/Audio | File | ArpEditor.cpp:260 | modal |
| arp.section.midi | LOOP SECTION brace MIDI grip | Drag | ArpEditor.cpp:1935 | modal |
| arp.section.audio | LOOP SECTION brace WAV grip | Drag | ArpEditor.cpp:1935 | modal |
| arp.section.prerender | the loop's WAV, rendered ahead from the timer into Documents/.../Audio | Auto | ArpEditor.cpp:1978 | modal, and the old code could raise the licence box from the timer and from `paint` |
| arp.hook.midi-drag | HOOK LAB tile dragged out: .mid | Drag | ArpEZator/HookLabView.cpp:370 | modal |
| arp.sample.region-wav | YOUR SAMPLE waveform WAV grip: region WAV | Drag | ArpEZator/SampleView.cpp:49 | modal |

- **Back-stops:** the writers `writeMidiFile` and `writeHookMidi` (ArpProcessor.cpp) re-check silently, and every drag
  goes through `exportgate::dragFiles (license, ...)`.
- **Not found:** no MIDI clipboard, no "send to" handoff and no save-MIDI dialog. The `.arpz` idea preset is internal.
  It does hold the notes as JSON, but no common tool reads it.

### Ghost Box: 3 paths

- Ghost Box is an effect on the DAW's own track. Presets, scenes (GHOST SCENES), boards, snapshots and motion are its own XML.
- GHOST CAPTURE (2026-10-26) added the only ways to get audio out: the clips on the CAPTURE tab's shelf (its own processed
  output, kept in memory, never written while locked). The shared rack code in GhostyPro/fx has no export.
- `GhostBoxTests demogate`:
  - checks the editor writes and drags nothing, locked or unlocked;
  - runs the 3 paths on a real processor that played audio, with one capture on the shelf and the editor on the CAPTURE page;
  - measures the drop-out.

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| box.capture.drag | a capture tile dragged into the DAW: `<stem>.wav` (32-bit float) written to Documents/Ghosty/Ghost Box/Captures, then dragged | Drag | GhostBox/BoxProcessor.cpp (clipDrag) | new |
| box.capture.save | SAVE AS on a tile: the WAV through the save dialog | File | BoxProcessor.cpp (clipSaveAs) | new |
| box.capture.slice | SLICE on a tile: the WAV + `<stem>.ghostclip.json` (docs/research/ghost-box/CAPTURE-SIDECAR.md), both dragged | Drag | BoxProcessor.cpp (clipSendToSlice) | new |

### Ghosty Pro: 3 paths

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| pro.wt.export-wav | WAVETABLE EDITOR EXPORT WAV: the table as a float32 WAV, through a save dialog | File | GhostyPro/ProWavetableEditor.h:516 | no |
| pro.wt.save | WAVETABLE EDITOR SAVE: a .ghostywt in the user bank | File | ProWavetableEditor.h:477 | no |
| pro.wt.save-as | WAVETABLE EDITOR SAVE AS: a .ghostywt | File | ProWavetableEditor.h:477 | no |

- **Why .ghostywt is gated:** it is JSON with base64 float32 PCM frames, so it is trivially turned into a WAV.
- **Not found:** no external drag, clipboard or MIDI writer anywhere in GhostyPro/ or GhostyPro/fx.

### Mr. Mickens: 5 paths

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| mick.mix.wav | DRAG WAV OUT: the mix render | Drag | Mickens/MickensEditor.cpp:888 | modal |
| mick.stems.wav | DRAG STEMS OUT: breaks / kick / snare / hats WAVs | Drag | MickensEditor.cpp:889 | modal |
| mick.bay.midi | each bay's MIDI grip | Drag | MickensEditor.cpp:514 | modal |
| mick.section.midi | LOOP SECTION MIDI grip | Drag | MickensEditor.cpp:673 | modal |
| mick.section.wav | LOOP SECTION WAV grip | Drag | MickensEditor.cpp:674 | modal |

- **Back-stop:** the render workers and `renderTo` re-check silently.
- **Not exports:**
  - BREAKS FOLDER holds only the user's own input WAVs. Renders go to TMPDIR.
  - `.mickens` setups and presets hold parameters and the user's own dropped samples.

### Poltergeist: 0 paths

- **Removed 2026-10-26** (owner: everything sample-related except a simple ADD SAMPLE slot moved to Ghost Slice). The
  SAMPLE, SAMPLE LAB, PROCESS LAB and VOCAL CHOP pages left the editor, and with them all nine gated paths:

  | removed id | what left with it |
  |---|---|
  | polt.sample.wav-grip | SAMPLE page waveform WAV grip |
  | polt.lab.edit-drag | SAMPLE LAB DRAG OUT AS WAV (your edit) |
  | polt.lab.chops-drag | SAMPLE LAB DRAG ALL CHOPS |
  | polt.lab.highlight-drag | SAMPLE LAB DRAG HIGHLIGHT |
  | polt.lab.strip-grip | SAMPLE LAB source-strip WAV grip |
  | polt.lab.piece-drag | SAMPLE LAB Alt-drag of a timeline piece |
  | polt.chop.midi-drag | VOCAL CHOP DRAG MIDI (and the processor's `writeChopMidi`) |
  | polt.chop.audio-drag | VOCAL CHOP DRAG AUDIO (and `writeChopAudio` / `renderChopPattern`) |
  | polt.plab.drag | PROCESS LAB DRAG (Shared ProcessLabPanel; the panel is no longer built into Poltergeist) |

  The processor's WAV writers behind them (`writeTempWav`, `writeWavNoGate`, `labExportChops / Range / Piece`) are gone too.
- **ADD SAMPLE** (the FM page's SAMPLE ghost) has a waveform (`WaveRegion` with `showExportGrip = false`): no WAV grip, no
  drag-out, no gate wired. It only reads files in.
- **Not exports:** presets, user sounds, snapshots, boards and the project (they still carry an old project's sample, Lab,
  CHOP and PROCESS LAB data, so it loads and plays as before; the savegate rules below still apply).
- `PoltergeistTests demogate`:
  - opens the editor with a sample, an old project's Lab / CHOP data and the ADD SAMPLE panel, locked and unlocked, uses the
    waveform where a grip used to be, and checks nothing is written or dragged;
  - runs the 0-path inventory.

### Ghosty Beats: 11 paths

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| beats.drag-midi | DRAG MIDI (pattern / song) | Drag | BeatsX/BeatsXEditor.cpp:228 | modal |
| beats.drag-audio | DRAG AUDIO mix | Drag | BeatsXEditor.cpp:230 | modal |
| beats.drag-stems | DRAG AUDIO with STEMS | Drag | BeatsXEditor.cpp:230 | modal |
| beats.drag-melody-midi | DRAG MELODY MIDI | Drag | BeatsXEditor.cpp:389 | modal |
| beats.drag-beat-melody-midi | DRAG BEAT + MELODY MIDI | Drag | BeatsXEditor.cpp:390 | modal |
| beats.track-midi-drag | track row MIDI handle | Drag | BeatsXEditor.cpp:765 | writer only |
| beats.track-wav-drag | track row WAV handle | Drag | BeatsXEditor.cpp:765 | writer only |
| beats.region-wav-drag | track sound waveform WAV grip | Drag | BeatsXEditor.cpp:487 | silent |
| beats.slice-wav-drag | slice WAV drag-out | Drag | BeatsX/EditorParts.h:671 | silent + modal |
| beats.sound-wav-drag | ⇣ one sound of the SOUNDS list as a WAV | Drag | BeatsX/EditorParts.h (SoundList::dragOut) | silent + modal |
| beats.export-xm | MELODY > Export as XM (notes + samples) | File | BeatsXEditor.cpp:518 | modal, raw save dialog |
| beats.export-it | MELODY > Export as IT | File | BeatsXEditor.cpp:518 | modal, raw save dialog |

- **Not exports:** the pattern and sound drags are internal (`startDragging`). Shared Sounds is only read here.

### Ghosty Spectrum: 26 paths

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| spec.pattern.wav-drag | DRAG PATTERN OUT (WAV) | Drag | SpectrumX/SpecProcessor.cpp:482 | modal |
| spec.pattern.stems-drag | DRAG STEMS OUT | Drag | SpecProcessor.cpp:488 | modal |
| spec.lab.wav-drag | LAB DRAG WAV OUT | Drag | SpectrumX/LabPanel.cpp:468 | modal |
| spec.lab.region-drag | LAB waveform WAV grip | Drag | LabPanel.cpp:589 | modal |
| spec.lab.send-beats | LAB SEND TO BEATS (WAV into Documents/Ghosty/Shared Sounds) | File | LabPanel.cpp:544 | modal |
| spec.plab.drag | PROCESS page DRAG | Drag | SpectrumX/LabProcessPage.h:29 | modal |
| spec.arcs.midi-drag | ARCS DRAG MIDI | Drag | SpectrumX/ArcsPage.cpp:663 | modal |
| spec.arcs.midi-save | ARCS "Save the page as MIDI" dialog | File | ArcsPage.cpp:668 | modal |
| spec.arcs.audio-render | ARCS RENDER AUDIO (WAV to temp) | File | ArcsPage.cpp:200 | modal |
| spec.arcs.audio-drag | ARCS DRAG AUDIO | Drag | ArcsPage.cpp:689 | modal |
| spec.arcs.loop-wav-drag | ARCS loop WAV grip | Drag | ArcsPage.cpp:192 | modal |
| spec.arcs.loop-midi-drag | ARCS loop MIDI grip | Drag | ArcsPage.cpp:192 | modal |
| spec.arcs.loop-prerender | ARCS loop WAV rendered ahead to temp | Auto | ArcsPage.cpp:179 | **no** |
| spec.arrange.mix-drag | ARRANGE MIX chip | Drag | SpectrumX/ArrangePage.cpp:1111 | modal |
| spec.arrange.stems-drag | ARRANGE STEMS chip | Drag | ArrangePage.cpp:1058 | modal |
| spec.arrange.midi-drag | ARRANGE MIDI chip | Drag | ArrangePage.cpp:1111 | modal |
| spec.arrange.loop-wav-drag | ARRANGE loop WAV grip | Drag | ArrangePage.cpp:1094 | modal |
| spec.arrange.loop-midi-drag | ARRANGE loop MIDI grip | Drag | ArrangePage.cpp:1094 | modal |
| spec.arrange.render | RENDER / "Render this section" (Documents/.../Arrange Bounces) | File | ArrangePage.cpp:1058 | modal |
| spec.arrange.send-beats | SEND TO BEATS / "Send this clip to Ghosty Beats" (Shared Sounds) | File | ArrangePage.cpp:881 | modal |
| spec.arrange.auto-mix | the MIX pre-rendered to temp in the background | Auto | ArrangePage.cpp:1146 | **no** |
| spec.voice.wav-drag | VOICE DRAG WAV OUT | Drag | SpectrumX/VoicePage.cpp:545 | modal |
| spec.voice.set-drag | VOICE DRAG SET (a folder) | Drag | VoicePage.cpp:546 | modal |
| spec.voice.render-set | VOICE RENDER SET (WAVs to temp) | File | VoicePage.cpp:755 | modal |
| spec.voice.render-set-shared | VOICE RENDER SET to Shared Sounds | File | VoicePage.cpp:755 | modal |
| spec.voice.region-drag | VOICE source waveform WAV grip | Drag | VoicePage.cpp:575 | modal |

- **Page hooks** (ArcsPage, ArrangePage, VoicePage `allowExport (ui, id)`): unwired now means refused. Before, a page
  with no hook exported freely.
- **Not exports:**
  - The ARCS clipboard copy is ARCS line shapes in the page's own text, pasted back into ARCS. It is not MIDI.
  - LAB SAVE PICTURE is a lossy magnitude spectrogram PNG.
  - `.ghostarcs` and `.ghostspec` are line and canvas data, plus the user's own dropped samples.

### Ghost Slice: 3 paths (GENERATE, 2026-11-02: the old editor and its 4 paths are gone)

| id | what leaves | kind | file:line | before |
|---|---|---|---|---|
| slice.drag.wav | DRAG WAV: the current take through the FX (-14 LUFS, true peak < -1 dBTP), 24-bit WAV, dragged | Drag | GhostSlice/SliceEditor.cpp (dragWavOut) | new |
| slice.drag.midi | DRAG MIDI: the take's melody / chords / phrase as a .mid, dragged | Drag | GhostSlice/SliceEditor.cpp (dragMidiOut) | new |
| slice.take.save | SAVE TAKE: the take's WAV + .mid into Documents/Ghosty/Ghost Slice/Takes | File | GhostSlice/SliceEditor.cpp (saveTakeClicked) | new |

- **Back-stops:** `SliceProcessor::writeTakeWav` / `writeTakeMidi` / `saveTake` re-check silently with `ghosty::exportUnlocked`; the writers in
  `GhostSlice/Export.cpp` are `// GATE: via-caller`.
- **Not exports:** the session state holds the sound's path + fingerprint and every take's recipe (seeds and settings, no audio):
  a reopened session re-renders the takes. RECORD keeps the user's own input as a WAV in Documents/Ghosty/Ghost Slice/Recordings so
  the session can find its sound again (`// NOT-EXPORT`: nothing the plugin made). While locked, a reopened session starts empty.
- **Test:** `GhostSliceTests demogate` (the three paths locked / unlocked through the editor's DRAG pills and SAVE TAKE, then the drop-out
  with a held INSTRUMENT note: locked gap 1.94 s at 18.03 s, none unlocked).

## The three follow-ups (decided by the owner, done)

1. **ArpEZator live MIDI out.** While locked, no note-on is sent in the same 2 s window every 20 s as the audio
   drop-out (`DemoGate::midiMuted`, the same clock as `DemoGate::process`). Note-offs still go out, so nothing hangs. A
   recorded MIDI track has the same holes as the audio.
   - Test (`ArpEZatorTests demogate`): locked = 0 note-ons in 18-20 s (34 before, 34 after; note-offs still sent),
     with the audio silent from 18.03 to 19.97 s. Unlocked = 36 note-ons in 18-20 s.
2. **Saves while locked leave plugin-made audio out.** Each embedded buffer now carries a `made` flag; a missing flag
   means imported, so old projects are untouched. The user's own imported samples are still saved. A click save that
   left something out shows the UNLOCK hint (`exportgate::renderedAudioLeftOut`); a host project save does it silently.
   - **Poltergeist:**
     - Left out: RESAMPLE, PROCESS LAB APPLY / BOUNCE->NEW and Sample Lab renders. Cuts and copies inherit the flag.
       (Those pages left in 2026-10; an old project's made audio is still flagged and still left out of locked saves.)
     - Saves affected: project state, user sounds, snapshots and boards.
     - Test: `PoltergeistTests savegate`.
   - **Ghosty Spectrum:**
     - Left out: ARRANGE clips that are not user imports (bounce, LAB / ARCS / VOICE / pattern sends), and their FLAC,
       notes and re-render state. Also LAB PROCESS results and the canvas rendered into morph A / B.
     - Test: `SpectrumLabTests savegate`.
   - **Ghosty Pro:**
     - Left out: user wavetables made in the WAVETABLE EDITOR (GENERATE, drawing, harmonic / phase bars, START FROM
       the oscillator table).
     - Kept: tables IMPORTed from a user file.
     - Saves affected: presets, snapshots, boards and project state.
     - Test: `ProTests savegate`.
3. **Ghosty Beats drum cache is scrambled.**
   - The file is now `library-v8-<rate>.bin`: header in clear, payload XORed with an xorshift32 key stream seeded per
     version and rate.
   - The version bump rebuilds every cache once and deletes the old `library-v<8-*.bin` files.
   - Test (`BeatsTests cachegate`): library and kit render hashes match the pre-change v7 build bit for bit
     (`7970282f0a2f54e7`, `4f28447203cb220f`), and no PCM run or sound name is in clear.

Still as before: Mr. Mickens setup / preset saves and Ghosty Beats SAVE BEAT work in demo. They are internal saves and
hold only the user's own audio.
