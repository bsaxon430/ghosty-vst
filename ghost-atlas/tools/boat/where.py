"""Where this pipeline's folders are. They are taken from the environment, so nothing here names one machine's paths:

  FILM_WORK    make_film.py's work folder: src/<world>/NN.png (the frames as recorded), big/<world>/NN.png (enlarged)
  STILLS_SRC   the eighteen paintings' source frames, s01a.png ... s09b.png (1206 x 658)
  BOAT_WORK    a folder for this pipeline's own files: masks, painted frames, the boat's track, the enlarged results
  SR_DIR       the folder with sr.py and realesr-general-x4v3.pth (the enlarging model the paintings went through)
  LAMA_MODEL   big-lama.pt (see lama.py)

The order things were run in (after make_film.py extract and enlarge):

  stills.py          the ten paintings that have no film, painted (masks.py says where the boat is in each)
  track.py           where the boat is in every film frame
  film_inpaint.py    every film frame painted (film_masks.py: the painting's mask, carried to the boat's place)
  cropsheet.py       ... looked at, world by world; what was left behind got a place in film_masks.py's REACH
  blips.py           ... and searched for things that are in one frame only
  film_hold.py       the still background behind the boat kept the same through a film (the snow world's bank)
  finish_stills.py   the eighteen paintings and the nine small pictures at the plug-in's size (assets/worlds/)
  film_enlarge.py    the film's enlarged frames with the painted part laid in; these replace FILM_WORK/big
  make_film.py pack  the film packs

flicker.py measures whether the painted part shimmers from frame to frame; grid.py is for drawing masks by eye.
"""
import os, sys


def need(name):
    v = os.environ.get(name)
    if not v: sys.exit(f"set {name} (see tools/boat/where.py)")
    return v


def film(): return need("FILM_WORK")
def stills(): return need("STILLS_SRC")
def work(): return need("BOAT_WORK")
def sr(): return need("SR_DIR")
