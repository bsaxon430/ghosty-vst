#!/usr/bin/env python3
"""A bare Xvfb has no window manager, so the atoms every window manager creates do not exist, and a JUCE window
cannot be put on its desktop (BadAtom on WM_PROTOCOLS). This creates them, so the checks can use a real window."""
from Xlib import display
d = display.Display()
for name in ("WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE", "_NET_WM_STATE", "_NET_WM_WINDOW_TYPE",
             "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_NAME", "UTF8_STRING", "_NET_WM_PING", "_NET_SUPPORTED"):
    d.intern_atom(name)
d.sync(); d.close()
