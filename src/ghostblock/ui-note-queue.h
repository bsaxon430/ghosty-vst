/*
 * Poltergeist 2 (GhostBlock) - the on-screen keyboard's notes.
 *
 * The editor's keyboard pushes note on / off here (message thread); the CLAP process() pops them at the
 * top of each host buffer and hands them to the voice manager exactly like host notes. Plumbing only:
 * nothing in the engine's sound path changes.
 */
#pragma once
#include <cstdint>
#include "sst/cpputils/ring_buffer.h"

namespace ghostblock::p2
{
struct UiNote
{
    bool on{false};
    int16_t key{60};
    float velocity{0.8f};
};
using UiNoteQueue = sst::cpputils::SimpleRingBuffer<UiNote, 256>;
} // namespace ghostblock::p2
