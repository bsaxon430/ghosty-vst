// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Reading (never writing) a few numbers the original engine keeps private, for the tap:
//   block::m_fft, m_mfcc, m_n_fft, m_n_mfcc   a piece's measurements (what the search compares)
//   brain::m_current_block_index              the piece the library is "at" (the last one returned)
//   brain::m_window, window::m_current_type   the fade every piece was given
// samplebrain/ is not edited. C++ lets an explicit template instantiation name a private member
// ([temp.spec]: "the usual access checking rules do not apply to names used to specify explicit
// instantiations"), which hands us a pointer-to-member for each; we only ever read through them.
//
// Include this from ONE .cpp only (Tap.cpp): the instantiations below must appear once in the program.
//
// THE FALLBACK: build with GHOSTMOSAIC_TAP_PEEK=0 if a compiler ever refuses the above. Then
//   - the "at" piece is a mirror of the last pick (unknown for the first pick after a restart, and flagged);
//   - the measurements are made again from each piece's own audio with the original FFT and MFCC code
//     (slower library hand-over on the worker; file-loaded libraries may differ in the last bit);
//   - the fade shape is not reported, and a live target's measurements are not available.
#pragma once
#include "brain.h"

#ifndef GHOSTMOSAIC_TAP_PEEK
 #define GHOSTMOSAIC_TAP_PEEK 1
#endif

#if GHOSTMOSAIC_TAP_PEEK
namespace mosaic::tap::peek
{
template <class Tag, typename Tag::type Member> struct Rob { friend typename Tag::type get (Tag) { return Member; } };

#define GHOSTMOSAIC_TAP_PEEK_AT(Name, Class, Member, Type) \
    struct Name { using type = Type Class::*; friend type get (Name); }; \
    template struct Rob<Name, &Class::Member>;

GHOSTMOSAIC_TAP_PEEK_AT (Bands,      spiralcore::block,  m_fft,                 spiralcore::sample)
GHOSTMOSAIC_TAP_PEEK_AT (Timbre,     spiralcore::block,  m_mfcc,                spiralcore::sample)
GHOSTMOSAIC_TAP_PEEK_AT (FlatBands,  spiralcore::block,  m_n_fft,               spiralcore::sample)
GHOSTMOSAIC_TAP_PEEK_AT (FlatTimbre, spiralcore::block,  m_n_mfcc,              spiralcore::sample)
GHOSTMOSAIC_TAP_PEEK_AT (At,         spiralcore::brain,  m_current_block_index, u32)
GHOSTMOSAIC_TAP_PEEK_AT (Fade,       spiralcore::brain,  m_window,              spiralcore::window)
GHOSTMOSAIC_TAP_PEEK_AT (FadeType,   spiralcore::window, m_current_type,        spiralcore::window::type)

#undef GHOSTMOSAIC_TAP_PEEK_AT
}
#endif
