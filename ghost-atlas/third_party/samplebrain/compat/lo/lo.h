// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Stand-in for the two liblo calls the unmodified engine's status reporter makes
// (status.cpp: lo_address_new_from_url + lo_send of /report, /sound-item, /sound-item-refresh).
// Messages go to the plug-in's status line, and (when OSC is switched on) out over UDP
// with JUCE's OSC module - see Source/Engine/Compat.cpp.
#pragma once
#include <cstdarg>   // (real liblo pulls this in; status.cpp relies on it)

struct lo_address_compat;
typedef lo_address_compat* lo_address;

lo_address lo_address_new_from_url (const char* url);
void lo_address_free (lo_address a);
int lo_send (lo_address target, const char* path, const char* types, ...);
