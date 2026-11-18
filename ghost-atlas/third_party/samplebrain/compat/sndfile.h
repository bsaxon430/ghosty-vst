// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Stand-in for the few libsndfile calls the unmodified Samplebrain engine makes
// (brain::load_sound: sf_open + sf_read_float). The real reading is done with JUCE's
// audio format readers (WAV, AIFF, FLAC, Ogg, MP3), see Source/Engine/Compat.cpp,
// so the engine files compile byte-for-byte as upstream wrote them.
#pragma once
#include <cstdint>

typedef int64_t sf_count_t;

struct SF_INFO
{
    sf_count_t frames = 0;
    int samplerate = 0;
    int channels = 0;
    int format = 0;
    int sections = 0;
    int seekable = 0;
};

struct SNDFILE_tag;
typedef struct SNDFILE_tag SNDFILE;

enum { SFM_READ = 0x10, SFM_WRITE = 0x20, SFM_RDWR = 0x30 };

SNDFILE* sf_open (const char* path, int mode, SF_INFO* info);
sf_count_t sf_read_float (SNDFILE* f, float* ptr, sf_count_t items);
int sf_close (SNDFILE* f);
