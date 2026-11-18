// Copyright (C) 2022 Then Try This
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#ifndef SEARCH_PARAMS
#define SEARCH_PARAMS

#include "spiralcore/types.h"
#include "spiralcore/stream.h"
#include <iostream>

#pragma once

namespace spiralcore {

class search_params {
public:
    search_params(float ratio, float n_ratio, int s1, int e1, float usage_importance) :
    m_ratio(ratio),
        m_n_ratio(n_ratio),
        m_fft1_start(s1),
        m_fft1_end(e1),
        m_usage_importance(usage_importance),
        m_num_synapses(20),
        m_stickyness(0),
        m_band_weights(0),
        m_block_bias(0),
        m_block_bias_count(0)
        {}

    float m_ratio;
    float m_n_ratio;
    u32 m_fft1_start;
    u32 m_fft1_end;
    float m_usage_importance;
    u32 m_num_synapses;
    f32 m_stickyness;
    // Ghost Mosaic patch 2 (TERRAIN): an optional weight per FFT band (100 floats) for the
    // matching distance. 0 = off: the distance is upstream's, to the bit. Not streamed (session
    // files stay upstream's format); the owner of the array keeps it alive while it is set.
    const float *m_band_weights;
    // Ghost Mosaic patch 3 (TERRAIN, build 10): an optional factor per library block (indexed by the
    // block's id, which is its index in the brain) that scales its distance before NOVELTY's usage
    // blend: < 1 = preferred. 0 = off: upstream's distance, to the bit. Not streamed.
    const float *m_block_bias;
    u32 m_block_bias_count;
};

 std::ios &operator||(std::ios &s, search_params &b);

}

#endif
