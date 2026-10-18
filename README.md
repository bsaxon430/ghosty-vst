# Ghosty for Ableton (Windows)

## 3 steps

1. **Unzip** this folder to your Desktop.
2. **Double-click `INSTALL-GHOSTY.bat`** and click **Yes** when Windows asks for permission.
   Leave the window open. The first time takes about 20–40 minutes, because it downloads Microsoft's free C++ tools and builds the plugins. When it's done it says **DONE!**
3. **In Ableton:** go to **Options → Preferences → Plug-Ins**, turn on **"Use VST3 Plug-in System Folders"**, and click **Rescan**.
   Both plugins are under **Plug-Ins → VST3 → GhostBlock**.

## Using them

- **Ghosty** (synth): drag it onto a MIDI track and play your keyboard or a MIDI clip. Pick a preset at the top, draw on the wave, and drag the ghosts.
- **Ghosty Beats** (drums): drag it onto a MIDI track and hit Play in Ableton. It follows Live's tempo.
  - Tap squares in the grid to make a beat, or pick one from the list at the top.
  - Hold the purple pads to stutter and hold TAPE STOP to slow it down.
  - MIDI notes C1–G1 play the 8 drums live.

## If something goes wrong

- **It says the tools didn't install:** restart the PC and double-click `INSTALL-GHOSTY.bat` again.
- **It says the build hit an error:** open Claude Code in this folder and type
  `Run INSTALL-GHOSTY.bat and fix any build errors.`
- **The plugins don't show in Ableton:** make sure you clicked **Rescan** and that the VST3 system folder option is on.
