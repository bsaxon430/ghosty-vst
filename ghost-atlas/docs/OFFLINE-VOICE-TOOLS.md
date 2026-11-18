# RAVE and DiffSinger: looked at as offline tools

The brief asked for these two to be judged as optional offline sound-design tools, not as real-time parts of the
instrument, and with no GPU assumed. Neither is in Ghost Atlas. This is what each would take.

## RAVE (ACIDS, IRCAM)

What it is: a neural autoencoder for audio. A trained model turns sound into a compact code and back; played through a
model trained on something else (voices, organs, water), a sound comes back in that model's timbre.

- Code: [acids-ircam/RAVE](https://github.com/acids-ircam/RAVE), installed with `pip install acids-rave`; a trained
  model is exported to a single TorchScript file and run with PyTorch.
- Speed: built for real time on a CPU, so offline rendering on this Mac is practical. No GPU is needed to *use* a
  model. Training one does need a GPU (the project lists 5 to 6 GB of GPU memory as the minimum).
- Licence: the published pretrained models are CC-BY-NC-4.0, that is, non-commercial. That matters here: sounds
  rendered through them should not go into music that is sold.
- What it would add: a third way to make new instruments, beside MUTATE and REIMAGINE. Play a sound at a set of notes
  (REIMAGINE already does this), pass each note through a model, and import the result as an instrument.
- Why it is not in: it brings PyTorch (hundreds of megabytes) into a plug-in that otherwise needs nothing installed, and
  the usable models are non-commercial. As a separate command-line step it fits: REIMAGINE's note-by-note files are
  plain WAV, so a script can run them through a model and drop the folder back on the Library page.

Verdict: worth trying by hand for texture work, with the licence in mind. Not worth building in.

## DiffSinger (OpenVPI's maintained version)

What it is: a singing synthesiser. Given notes and syllables it sings them in a trained voice.

- Code: [openvpi/DiffSinger](https://github.com/openvpi/DiffSinger), Apache-2.0. Trained voices are exported to ONNX
  and are normally used through the OpenUTAU editor.
- It needs a trained voice (each with its own terms) and a score with phonemes: it sings lines, it does not turn an
  arbitrary sample into a voice.
- Speed: diffusion synthesis is slow without a GPU. Offline rendering on a CPU works but is not instant.
- What it would add: real sung material made to order (sustained vowels at every pitch, short phrases), rendered in
  OpenUTAU and imported as an instrument. That is a way to fill the Choirs and Vocal Pads worlds with voices that are
  yours to use.
- Why it is not in: it is a different kind of tool (a score in, a performance out), it depends on voices whose terms
  have to be read one by one, and the importer already takes what it produces.

Verdict: the more useful of the two for this instrument, as a source of new vocal recordings made outside it. The
importer needs nothing new: name the rendered files by note and drop the folder on the Library page.
