#!/bin/sh
# Runs GhostAtlasTests on a headless Linux display that can hold a real window. Some checks use the window as a host
# does (a mouse over it, the keyboard given to it and taken back), and a bare Xvfb has no window manager: the atoms
# every window manager creates are missing, and a JUCE window cannot be put on its desktop (BadAtom on WM_PROTOCOLS).
# x-atoms.py creates them; -noreset keeps them when that script disconnects. Needs xvfb-run and python-xlib.
#
#   tools/checks-headless.sh <path to GhostAtlasTests> [arguments for it]
here=$(cd "$(dirname "$0")" && pwd)
bin=$1; shift
GHOST_ATLAS_WINDOW_CHECKS=1; export GHOST_ATLAS_WINDOW_CHECKS
exec xvfb-run -a -s "-screen 0 1920x1200x24 -noreset" sh -c 'python3 "$0/x-atoms.py"; bin=$1; shift; exec "$bin" "$@"' "$here" "$bin" "$@"
