// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// RtRenderer: Samplebrain's renderer (samplebrain/brain/src/renderer.cpp) ported line for line,
// with one change: its list of playing blocks is a fixed-capacity array made up front, so the
// audio thread never allocates (upstream used std::list::push_back and a scratch `sample` per
// call). Search, timing, mixing and every float expression are the same, in the same order:
// tests/MosaicTests.cpp null-tests it against the original renderer, sample for sample.
//
// Additions (all off by default, so the default output is upstream's):
//  - setTargetSource(): the target can be a live input stream (LiveTarget) as well as a brain;
//  - setKeyPitch(): MIDI key transposes the source blocks (multiplies the playback step);
//  - an observer called for every block chosen (feeds the mosaic view);
//  - holdTargetTime(): keeps a live target in step with the input;
//  - setShaping(): the TERRAIN page's band weights and its wildness along the target (Shaping.h).
#pragma once
#include "brain.h"
#include "renderer.h"
#include "Shaping.h"
#include <vector>

namespace mosaic
{
using ::u32;
using ::s32;

class RtRenderer
{
public:
    using search_algo = spiralcore::renderer::search_algo;

    explicit RtRenderer (size_t capacity = 32768);

    void init (spiralcore::brain* source, const spiralcore::block_source* target);
    void setSource (spiralcore::brain* s) { m_source = s; }
    void setTargetSource (const spiralcore::block_source* t) { m_target = t; }
    spiralcore::brain* getSource() const { return m_source; }

    void reset();
    void process (u32 nframes, float* buf);

    void set_search_algo (search_algo s) { m_search_algo = s; }
    void set_playing (bool s) { m_playing = s; }
    void set_volume (float s) { m_volume = s; }
    void set_n_mix (float s) { m_n_mix = s; }
    void set_target_mix (float s) { m_target_mix = s; }
    void set_slide_error (double s) { m_slide_error = s; }
    void set_stretch (u32 s) { m_stretch = s; }
    void set_autotune (float s) { m_autotune = s; }
    spiralcore::search_params* get_params() { return &m_search_params; }

    search_algo get_search_algo() const { return m_search_algo; }
    bool get_playing() const { return m_playing; }
    float get_volume() const { return m_volume; }
    float get_n_mix() const { return m_n_mix; }
    float get_target_mix() const { return m_target_mix; }
    double get_slide_error() const { return m_slide_error; }
    u32 get_stretch() const { return m_stretch; }
    float get_autotune() const { return m_autotune; }

    // ---- additions
    void setKeyPitch (float ratio) { m_key_pitch = ratio; }
    struct Observer { void (*fn) (void* ctx, u32 src, u32 tgt, u32 time) = nullptr; void* ctx = nullptr; };
    void setObserver (Observer o) { m_observer = o; }
    float getTargetTime() const { return m_target_time; }
    void holdTargetTime (float t) { m_target_time = t; }
    void primeShift (u32 hop) { m_last_tgt_shift = hop; }   // live: start without upstream's first-buffer reset
    u32 getRenderTime() const { return m_render_time; }
    size_t activeBlocks() const { return m_count; }
    size_t droppedBlocks() const { return m_dropped; }
    u32 getLastSourceIndex() const { return m_last_src; }
    // TERRAIN (nullptr = none, the default: upstream's search, to the bit). The Shaping must stay
    // alive while it is set (the engine owns it); the knobs' own NOVELTY / BOREDOM are handed in
    // every buffer, and a shaped search sets the renderer's / library's values from them.
    void setShaping (const Shaping* s) { m_shaping = s; }
    void setShapingBase (float novelty, float boredom) { m_base_novelty = novelty; m_base_boredom = boredom; }
    void setBlockBias (const float* f, u32 n) { m_block_bias = f; m_block_bias_count = f != nullptr ? n : 0; }   // per-piece factors (nullptr = none)
    void setShapingSpan (u32 blocks) { m_shaping_span = blocks; }   // a live target's "whole length" (blocks)
    float shapedNovelty() const { return m_search_params.m_usage_importance; }
    float shapedPosition() const { return m_shaped_pos; }
    float shapedWild() const { return m_shaped_wild; }
    // TERRAIN: move ORIGINAL MIX to `to` across the next process() call, sample by sample (a step would click)
    void glideTargetMix (float to) { if (! m_playing) { m_target_mix = to; m_tm_glide = false; return; } if (to != m_target_mix) { m_tm_from = m_target_mix; m_target_mix = to; m_tm_glide = true; } }

    // every setting and position (not the playing pieces): what a session file holds
    void copyStateFrom (const RtRenderer& o);

    // Same bytes as upstream's operator||(ios&, renderer&) (sessions are interchangeable).
    void stream (std::ios& s);

private:
    bool find_render_blocks (const spiralcore::block_source& target, u32 nframes);
    void render (const spiralcore::block_source& target, u32 nframes, float* buf);
    void clean_up();

    struct render_block
    {
        u32 m_index = 0, m_tgt_index = 0, m_time = 0;
        bool m_finished = false;
        float m_position = 0;
    };
    void push (u32 index, u32 tgt_index, u32 time);

    spiralcore::brain* m_source = nullptr;
    const spiralcore::block_source* m_target = nullptr;

    spiralcore::search_params m_search_params { 0, 0, 0, 100, 0 };
    float m_volume = 1;
    bool m_playing = false;
    u32 m_target_index = 0;
    u32 m_render_index = 0;
    float m_target_time = 0;
    u32 m_render_time = 0;
    u32 m_stretch = 1;
    float m_n_mix = 0;
    float m_target_mix = 0;
    float m_autotune = 0;
    search_algo m_search_algo = spiralcore::renderer::BASIC;
    double m_slide_error = 1;
    u32 m_last_tgt_shift = 0;

    std::vector<render_block> m_blocks;   // capacity fixed in the constructor
    size_t m_count = 0, m_dropped = 0;
    float m_key_pitch = 1;
    Observer m_observer;
    u32 m_last_src = 0;
    const Shaping* m_shaping = nullptr;
    float m_base_novelty = 0, m_base_boredom = 0.9f, m_shaped_pos = 0, m_shaped_wild = 0;
    bool m_time_shaped = false, m_tm_glide = false;
    float m_tm_from = 0;
    u32 m_shaping_span = 0;
    const float* m_block_bias = nullptr;
    u32 m_block_bias_count = 0;
    void shapeSearch (const spiralcore::block_source& target);
};
}
