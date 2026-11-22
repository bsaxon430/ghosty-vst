// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
// Port of samplebrain/brain/src/renderer.cpp (Copyright (C) 2022 Then Try This). Keep the
// expressions identical to the original: the null test compares the two sample for sample.
#include "RtRenderer.h"
#include <climits>

using namespace spiralcore;

namespace mosaic
{
RtRenderer::RtRenderer (size_t capacity) { m_blocks.resize (capacity); }

void RtRenderer::init (brain* source, const block_source* target)
{
    m_volume = 1;
    m_playing = false;
    m_source = source;
    m_target = target;
    m_target_time = 0;
    m_render_time = 0;
    m_n_mix = 0;
    m_target_mix = 0;
    m_count = 0;
    m_search_algo = renderer::BASIC;
    m_slide_error = 1;
    m_target_index = 0;
    m_render_index = 0;
    m_stretch = 1;
    m_last_tgt_shift = 0;
    m_autotune = 0;
    ++m_resets;
}

void RtRenderer::reset()
{
    ++m_resets;   // (the tap: counts restarts; read-only elsewhere)
    m_target_time = 0;
    m_render_time = 0;
    m_target_index = 0;
    m_render_index = 0;
    m_count = 0;
    if (m_source != nullptr) m_source->jiggle();
}

void RtRenderer::push (u32 index, u32 tgt_index, u32 time)
{
    if (m_count >= m_blocks.size()) { ++m_dropped; return; }
    auto& b = m_blocks[m_count++];
    b.m_index = index; b.m_tgt_index = tgt_index; b.m_time = time; b.m_finished = false; b.m_position = 0;
    m_last_src = index;
    if (m_observer.fn != nullptr) m_observer.fn (m_observer.ctx, index, tgt_index, time);
}

void RtRenderer::process (u32 nframes, float* buf)
{
    if (m_source == nullptr || m_target == nullptr) return;
    if (! m_playing) return;
    if (! find_render_blocks (*m_target, nframes)) return;

    render (*m_target, nframes, buf);
    m_tm_glide = false;

    clean_up();

    m_render_time += nframes;
}

bool RtRenderer::find_render_blocks (const block_source& target, u32 nframes)
{
    u32 tgt_shift = target.get_block_size() - target.get_overlap();
    if (tgt_shift == 0 || target.get_num_blocks() == 0)   // (a target that was never generated)
    {
        reset();
        m_last_tgt_shift = tgt_shift;
        return false;
    }
    m_target_index = m_target_time / (float) tgt_shift;
    u32 tgt_end = (m_target_time + nframes) / (float) tgt_shift;
    u32 rnd_end = (m_render_time + nframes) / (float) tgt_shift;

    if (tgt_shift != m_last_tgt_shift ||
        tgt_end >= target.get_num_blocks() ||
        m_source->get_num_blocks() == 0)
    {
        reset();
        m_last_tgt_shift = tgt_shift;
        return false;
    }

    // (addition: TERRAIN band weights; nullptr = upstream's distance)
    m_search_params.m_band_weights = m_shaping != nullptr && m_shaping->weightsOn ? m_shaping->weights : nullptr;
    m_search_params.m_block_bias = m_shaping != nullptr ? m_block_bias : nullptr;   // (TERRAIN brightness; nullptr = upstream's)
    m_search_params.m_block_bias_count = m_block_bias_count;

    u32 counter = m_render_index;
    while (counter <= rnd_end)
    {
        u32 time = m_render_index * tgt_shift;
        u32 src_index = 0;
        if (m_shaping != nullptr || m_time_shaped) shapeSearch (target);   // (addition: TERRAIN wildness along the target)
        if (m_observer.before != nullptr) m_observer.before (m_observer.ctx);   // (the tap: read-only)

        switch (m_search_algo)
        {
            case renderer::BASIC:
                src_index = m_source->search (target.get_block (m_target_index), m_search_params);
                break;
            case renderer::REV_BASIC:
                src_index = m_source->rev_search (target.get_block (m_target_index), m_search_params);
                break;
            case renderer::SYNAPTIC:
            case renderer::SYNAPTIC_SLIDE:
                src_index = m_source->search_synapses (target.get_block (m_target_index), m_search_params);
                break;
        }

        const u32 tgtWas = m_target_index;   // (the tap)
        if (m_search_algo == renderer::SYNAPTIC_SLIDE)
        {
            push (src_index, m_target_index, time);
            if (m_source->get_current_error() < m_slide_error &&
                m_render_index % m_stretch == 0)
            {
                m_target_index++;
                m_target_time += tgt_shift;
            }
        }
        else
        {
            push (src_index, m_target_index, time);
            if (m_render_index % m_stretch == 0)
            {
                m_target_index++;
                m_target_time += tgt_shift;
            }
        }
        if (m_observer.picked != nullptr) m_observer.picked (m_observer.ctx, src_index, tgtWas, time, m_render_index, m_target_index != tgtWas);   // (the tap: read-only)
        counter++;
        m_render_index++;
    }
    return true;
}

void RtRenderer::shapeSearch (const block_source& target)
{
    const bool on = m_shaping != nullptr && m_shaping->timeShaped();
    if (! on)
    {
        if (m_time_shaped)
        {
            // the landscape went flat: the knobs' own values again
            m_search_params.m_usage_importance = m_base_novelty;
            if (m_source != nullptr) m_source->set_usage_falloff (m_base_boredom);
            m_time_shaped = false;
            m_shaped_wild = 0;
        }
        return;
    }
    const u32 n = target.get_num_blocks();
    float pos = 0;
    if (n == UINT_MAX) pos = m_shaping_span > 0 ? (float) (m_target_index % m_shaping_span) / (float) m_shaping_span : 0.0f;
    else pos = n > 1 ? (float) m_target_index / (float) (n - 1) : 0.0f;
    const float w = m_shaping->wildNow (pos);
    m_search_params.m_usage_importance = Shaping::novelty (m_base_novelty, w);
    m_source->set_usage_falloff (Shaping::boredom (m_base_boredom, w));
    m_shaped_pos = pos; m_shaped_wild = w;
    m_time_shaped = true;
}

void RtRenderer::render (const block_source& target, u32 nframes, float* buf)
{
    for (size_t bi = 0; bi < m_count; ++bi)
    {
        render_block* i = &m_blocks[bi];
        const spiralcore::sample& pcm = m_source->get_block (i->m_index).get_pcm();
        const spiralcore::sample& n_pcm = m_source->get_block (i->m_index).get_n_pcm();
        const spiralcore::sample& target_pcm = target.get_block (i->m_tgt_index).get_pcm();
        s32 offset = i->m_time - m_render_time;

        u32 block_length = pcm.get_length();

        u32 block_start = offset;
        u32 buffer_start = 0;
        if (offset < 0)
        {
            block_start = -offset;
            if (block_start >= block_length &&
                i->m_position >= block_length)
            {
                i->m_finished = true;
            }
        }
        else
        {
            block_start = 0;
            buffer_start = offset;
        }

        float pitch_scale = 1;

        if (m_autotune > 0)
        {
            pitch_scale = target.get_block (i->m_tgt_index).get_freq() /
                          m_source->get_block (i->m_index).get_freq();
            float max = 1 + (m_autotune * m_autotune) * 100.0f;
            if (pitch_scale > (max)) pitch_scale = max;
            if (pitch_scale < (1 / max)) pitch_scale = 1 / max;
        }
        if (m_key_pitch != 1.0f) pitch_scale *= m_key_pitch;   // (addition: MIDI key; 1 = upstream)

        if (! i->m_finished)
        {
            u32 buffer_pos = buffer_start;
            u32 block_pos = block_start;
            u32 block_end = pcm.get_length();

            while (i->m_position < block_end && buffer_pos < nframes)
            {
                float brain_sample = (pcm[i->m_position] * (1 - m_n_mix) +
                                      n_pcm[i->m_position] * m_n_mix);

                float target_sample = 0;

                if (block_pos < block_length)
                {
                    target_sample = target_pcm[block_pos];
                }

                if (m_tm_glide)
                {
                    // (addition: TERRAIN's ORIGINAL MIX glide; off = upstream's expression below)
                    const float tm = m_tm_from + (m_target_mix - m_tm_from) * (float) buffer_pos / (float) nframes;
                    buf[buffer_pos] += (brain_sample * (1 - tm) + target_sample * tm) * 0.2 * m_volume;
                }
                else
                buf[buffer_pos] += (brain_sample * (1 - m_target_mix) +
                                    target_sample * m_target_mix) * 0.2 * m_volume;

                i->m_position += pitch_scale;

                if (block_pos < block_length &&
                    i->m_position > block_end)
                {
                    i->m_position = 0;
                }

                ++buffer_pos;
                ++block_pos;
            }
        }
    }
}

void RtRenderer::clean_up()
{
    size_t w = 0;
    for (size_t r = 0; r < m_count; ++r)
        if (! m_blocks[r].m_finished)
        {
            if (w != r) m_blocks[w] = m_blocks[r];
            ++w;
        }
        else if (m_observer.ended != nullptr) m_observer.ended (m_observer.ctx, m_blocks[r].m_index, m_blocks[r].m_tgt_index, m_blocks[r].m_time, m_blocks[r].m_position);   // (the tap: read-only)
    m_count = w;
}

void RtRenderer::copyStateFrom (const RtRenderer& o)
{
    const float* weights = m_search_params.m_band_weights;   // (ours: the weights stay this renderer's)
    const float* bias = m_search_params.m_block_bias; const u32 biasN = m_search_params.m_block_bias_count;
    m_search_params = o.m_search_params;
    m_search_params.m_band_weights = weights;
    m_search_params.m_block_bias = bias; m_search_params.m_block_bias_count = biasN;
    m_volume = o.m_volume; m_playing = o.m_playing;
    m_target_index = o.m_target_index; m_render_index = o.m_render_index;
    m_target_time = o.m_target_time; m_render_time = o.m_render_time;
    m_stretch = o.m_stretch; m_n_mix = o.m_n_mix; m_target_mix = o.m_target_mix; m_autotune = o.m_autotune;
    m_search_algo = o.m_search_algo; m_slide_error = o.m_slide_error; m_last_tgt_shift = o.m_last_tgt_shift;
    m_count = 0;
    ++m_resets;
}

void RtRenderer::stream (std::ios& s)
{
    u32 version = 0;
    std::string id ("renderer");
    s || id || version;
    s || m_search_params;
    s || m_volume || m_playing || m_target_index || m_render_index;
    s || m_target_time || m_render_time || m_stretch;
    s || m_n_mix || m_target_mix || m_autotune;
    s || m_search_algo || m_slide_error || m_last_tgt_shift;
}
}
