/*
 * Poltergeist 2 (GhostBlock) - SUB: the harmonic generators of Subhoofer, ported from Rust to C++.
 *
 * Subhoofer v2.2.3 by Ardura - https://github.com/ardura/Subhoofer (commit 19ea26b500fe0f3fc0516bde02b565af80501ae6),
 * "Harmonic and Subharmonic Bass Enhancement", license GPL-3.0-or-later (its Cargo.toml).
 * Copyright (C) Ardura <azviscarra@gmail.com>. Parts of Subhoofer derive from Airwindows (Chris Johnson, MIT) and
 * Duro Console, as its own comments say.
 *
 * THIS FILE IS A PORT, not upstream's file: upstream is Rust (src/lib.rs, src/SweetenX.rs; both are kept, unmodified,
 * in libs/ghostblock-sub/subhoofer for comparison). Every function below is a line-by-line transcription of the Rust
 * function of the same name: the same constants, the same order of operations, 32-bit floats throughout. It is checked
 * against upstream's own Rust source text, compiled by rustc (libs/ghostblock-sub/reference; p2-sub-tests "port").
 *
 * What is ported: the six harmonic algorithms of `AlgorithmType` (A Bass 3, A Bass 2, 8 Harmonic Stack, Duro Console,
 * TanH Transfer, Custom), the "Hoof Hardness" stage (chebyshev_tape / leaf_saturation), SweetenX, the DC high-pass and
 * the dry/wet + output stage of Subhoofer::process.
 * What is NOT ported: the subharmonic generator (the "sub bump" octave divider) - Poltergeist 2 synthesises its sub at
 * the exact pitch (Bombaz), an octave divider guessing from zero crossings has nothing to add - and the plugin shell
 * (nih-plug parameters, meters, the egui interface).
 *
 * Differences from the Rust text, all of them marked [PORT]:
 *   - `hp_a1` (the DC high-pass pole, -0.995 upstream) is a member so the plugin can keep the corner where upstream
 *     has it at 44.1 kHz when this runs at another rate. The default is upstream's.
 *   - `shapeLeft` / `dcLeft`: the left channel of process() split in two (before / after the DC high-pass), for the
 *     plugin, which works in mono. They are the same arithmetic (the tests compare them with process()).
 *
 * This file is GPL-3.0-or-later like its source.
 */
#pragma once
#include <cmath>
#include <cstdint>

namespace ghostblock::p2::sub::subhoofer
{
// src/lib.rs: enum AlgorithmType (the order is upstream's; the names are its #[name = ...] attributes)
enum class AlgorithmType : int
{
    ABass3 = 0,    // "A Bass 3"
    ABass2,        // "A Bass 2"
    BBass,         // "8 Harmonic Stack"
    CBass,         // "Duro Console"
    TanH,          // "TanH Transfer"
    CustomSliders, // "Custom"
};
constexpr int kAlgorithms = 6;
inline const char *algorithmName(AlgorithmType a)
{
    static const char *const n[kAlgorithms] = {"A Bass 3", "A Bass 2", "8 Harmonic Stack", "Duro Console", "TanH Transfer", "Custom"};
    return n[(int)a < 0 || (int)a >= kAlgorithms ? 0 : (int)a];
}

// nih_plug::util::db_to_gain (ISC, Robbert van der Helm): 10.0f32.powf(dbs * 0.05), 0 at or under -100 dB
inline float db_to_gain(float dbs) { return dbs > -100.0f ? std::pow(10.0f, dbs * 0.05f) : 0.0f; }

// f32::powi as rustc / LLVM compute it for these calls (repeated squaring, compiler-rt's __powisf2)
inline float powi(float a, int b)
{
    const bool recip = b < 0;
    float r = 1.0f;
    while (true)
    {
        if (b & 1)
            r *= a;
        b /= 2;
        if (b == 0)
            break;
        a *= a;
    }
    return recip ? 1.0f / r : r;
}

// src/lib.rs: fn chebyshev_tape - "Modified function from Duro Console for different behavior - hoof hardness"
inline float chebyshev_tape(float sample, float drive)
{
    const float dry = 1.0f - drive;
    const float peak = std::fmax(std::fabs(sample), 1.0f);
    const float x = sample / peak;
    const float x2 = x * x;
    const float x3 = x * x2;
    const float x5 = x3 * x2;
    const float x6 = x3 * x3;
    const float y = x - 0.166667f * x3 + 0.00833333f * x5 - 0.000198413f * x6 + 0.0000000238f * x6 * drive;
    return dry * sample + (1.0f - dry) * y / (1.0f + std::fabs(y));
}

// src/lib.rs: fn tape_saturation - "Modified tape saturation using transfer function from Duro Console"
inline float tape_saturation(float input_signal, float drive)
{
    const float idrive = drive;
    const float tanh_saturation = std::tanh(input_signal * idrive);
    const float linear_saturation = input_signal * idrive;
    const float output_sample = tanh_saturation + (linear_saturation - tanh_saturation) * 0.5f;
    return output_sample - input_signal;
}

// src/lib.rs: fn b_bass_saturation - the "8 Harmonic Stack"
inline float b_bass_saturation(float signal, float harmonic_strength)
{
    float summed = 0.0f;
    for (int j = 1; j <= 8; ++j)
    {
        if (j % 2 == 1)
        {
            const float harmonic_component = harmonic_strength * 3.0f * std::cos(signal * (float)j);
            const float harmonic_component2 = harmonic_strength * std::sin(signal * (float)j);
            summed += harmonic_component + harmonic_component2;
            continue;
        }
        else if (j % 2 == 0)
        {
            switch (j)
            {
            case 4: harmonic_strength *= 0.6f; break;
            case 6: harmonic_strength *= 4.0f; break;
            default: harmonic_strength *= 1.0f; break;
            }
            const float harmonic_component = harmonic_strength * std::sin(signal * (float)j);
            summed += harmonic_component;
        }
    }
    return summed - signal;
}

// src/lib.rs: fn c_bass_saturation - "Modified 'odd_saturation' from Duro Console"
inline float c_bass_saturation(float signal, float harmonic_strength)
{
    float summed = 0.0f;
    for (int j = 1; j <= 7; ++j)
    {
        const float harmonic_component = harmonic_strength * 0.3f * std::sin(signal * (float)j) - signal;
        const float harmonic_component2 = harmonic_strength * std::cos(signal * (float)j) - signal;
        summed += harmonic_component + harmonic_component2;
    }
    return summed / 7.0f;
}

// src/lib.rs: fn custom_sincos_saturation
inline float custom_sincos_saturation(float signal, float harmonic_strength1, float harmonic_strength2, float harmonic_strength3, float harmonic_strength4)
{
    float summed = 0.0f;
    float harmonic_component = harmonic_strength1 * std::cos(signal * 1.0f) - signal;
    summed += harmonic_component;
    harmonic_component = harmonic_strength2 * std::sin(signal * 2.0f) - signal;
    summed += harmonic_component;
    harmonic_component = harmonic_strength3 * std::cos(signal * 3.0f) - signal;
    summed += harmonic_component;
    const float harmonic_component2 = harmonic_strength4 * std::sin(signal * 4.0f) - signal;
    summed += harmonic_component2;
    return summed;
}

// src/lib.rs: fn leaf_saturation - "'Leaf' Saturation designed by Ardura"
inline float leaf_saturation(float input_signal, float threshold, float drive)
{
    const float range = 6.0f;
    const float min_value = 1.0f;
    const float drive_db = min_value + drive * range;
    const float signal_holder = input_signal * db_to_gain(drive_db);
    const float q = signal_holder / 999.0f;
    const float curve = q * q; // (signal_holder / 999.0).powf(2.0): LLVM compiles powf(x, 2.0) as x * x
    float y = signal_holder / threshold;
    y = (2.0f / 3.14159274101257324f) * std::atan(y);
    return (threshold + (1.0f - threshold) * curve) * y;
}

// src/SweetenX.rs: fn process - "Converted Airwindows' Sweeten to work like my other algorithm approximation".
// One channel of it (upstream does the left with buffer[0..8) and the right with buffer[8..16), the same way).
inline float sweeten_channel(float in, float overallscale, float drive, int harmonic, float *buffer /* 8 values */)
{
    int cycle_end = (int)std::floor(overallscale);
    if (cycle_end < 1)
        cycle_end = 1;
    if (cycle_end > 4)
        cycle_end = 4;
    float sweet_sample = in;
    for (int j = 0; j < cycle_end; ++j)
    {
        const float sv = sweet_sample;
        sweet_sample = (sweet_sample + buffer[j]) * 0.5f;
        buffer[j] = sv;
    }
    sweet_sample = powi(sweet_sample, harmonic) * drive;
    for (int j = cycle_end; j < cycle_end * 2; ++j)
    {
        const float sv = sweet_sample;
        sweet_sample = (sweet_sample + buffer[j]) * 0.5f;
        buffer[j] = sv;
    }
    return in - sweet_sample;
}

// what Subhoofer::process is given for one sample (its smoothed parameter values)
struct Settings
{
    AlgorithmType h_algorithm{AlgorithmType::ABass3};
    float harmonics{0.000580f};    // "Harmonics", 0 .. 1 (skewed), upstream's default
    float hoof_hardness{0.0093f};  // "Hoof Hardness", 0 .. 0.30, upstream's default
    float custom_harmonics1{0.0f}; // "Harmonic 1..4", 0 .. 400 (the Custom algorithm)
    float custom_harmonics2{0.0f};
    float custom_harmonics3{0.0f};
    float custom_harmonics4{0.0f};
    float dry_wet{1.0f};           // "Dry/Wet", 0 .. 1
    float output_gain{1.0f};       // "Output Gain" as a factor
    float overall_scale{1.0f};     // sample rate / 44100, as process() computes it
};

// The harmonic part of Subhoofer::process (src/lib.rs), for one stereo sample at a time. The state is upstream's:
// `buffer` (SweetenX: ONE buffer shared by all thirteen calls of A Bass 3, exactly as upstream does it) and the four
// values of the DC high-pass.
struct Harmonics
{
    float buffer[16]{};
    float prev_processed_in_r{0.0f}, prev_processed_out_r{0.0f}, prev_processed_in_l{0.0f}, prev_processed_out_l{0.0f};
    float hp_a1{-0.995f}; // [PORT] upstream: `let hp_a1: f32 = -0.995;`

    void reset()
    {
        for (auto &b : buffer)
            b = 0.0f;
        prev_processed_in_r = prev_processed_out_r = prev_processed_in_l = prev_processed_out_l = 0.0f;
    }

    // "Add: Original signal + Harmonics + Sub signal" and "Hardness Saturation" for one channel. `other` is the other
    // channel's input (A Bass 2 computes BOTH channels from the left input upstream; that is kept: see `fromLeft`).
    // sub_bump * sub_gain is 0 here (the subharmonic generator is not ported).
    float shape(float in, float inLeft, bool rightChannel, const Settings &s)
    {
        float *buf = buffer + (rightChannel ? 8 : 0);
        float processed_sample;
        switch (s.h_algorithm)
        {
        case AlgorithmType::ABass3:
        {
            const float harmonic2 = sweeten_channel(in, s.overall_scale, 26.470589f, 2, buf);
            const float harmonic3 = sweeten_channel(in, s.overall_scale, 8.941176f, 3, buf);
            const float harmonic4 = sweeten_channel(in, s.overall_scale, 0.1764706f, 4, buf);
            const float harmonic5 = sweeten_channel(in, s.overall_scale, 0.0f, 5, buf);
            const float harmonic6 = sweeten_channel(in, s.overall_scale, 0.0f, 6, buf);
            const float harmonic7 = sweeten_channel(in, s.overall_scale, 0.0f, 7, buf);
            const float harmonic8 = sweeten_channel(in, s.overall_scale, 0.0f, 8, buf);
            const float harmonic9 = sweeten_channel(in, s.overall_scale, 171.76471f, 9, buf);
            const float octave = in * in * in * in * in * 0.5f;
            // "Start from 5th"
            const float harmonic10 = sweeten_channel(octave, s.overall_scale, 0.0f, 2, buf);
            const float harmonic11 = sweeten_channel(octave, s.overall_scale, 4000.0f, 3, buf);
            const float harmonic12 = sweeten_channel(octave, s.overall_scale, 11764706.0f, 4, buf);
            const float harmonic13 = sweeten_channel(octave, s.overall_scale, 5294118000.0f, 5, buf);
            const float harmonic14 = sweeten_channel(octave, s.overall_scale, 17647059000.0f, 6, buf);
            processed_sample = in;
            processed_sample += (harmonic2 + harmonic3 + harmonic4 + harmonic5 + harmonic6 + harmonic7 + harmonic8 + harmonic9 + harmonic10 + harmonic11 + harmonic12 +
                                 harmonic13 + harmonic14) *
                                    (s.harmonics * 1497.00599f) +
                                (0.0f * 0.0f);
            const float scale = db_to_gain(-21.2f);
            processed_sample *= scale;
            break;
        }
        case AlgorithmType::ABass2:
        {
            // "Ardura's new Algorithm for 2024" (upstream feeds in_l to both channels)
            processed_sample = custom_sincos_saturation(inLeft, s.harmonics * 31.422043f, s.harmonics * 189.29568f, s.harmonics * 25.0f, s.harmonics * 26.197401f) + (0.0f * 0.0f);
            const float h = (processed_sample * 2.0f) - processed_sample * processed_sample; // .powf(2.0) = x * x
            processed_sample += h * 0.0070118904f;
            processed_sample = db_to_gain(-2.4f) * processed_sample;
            break;
        }
        case AlgorithmType::BBass:
            processed_sample = b_bass_saturation(in, s.harmonics) + (0.0f * 0.0f);
            processed_sample = db_to_gain(8.7f) * processed_sample;
            break;
        case AlgorithmType::CBass:
            if (s.harmonics > 0.0f)
                processed_sample = c_bass_saturation(in, s.harmonics) + (0.0f * 0.0f);
            else
                processed_sample = 0.0f * 0.0f;
            break;
        case AlgorithmType::TanH:
            processed_sample = tape_saturation(in, s.harmonics) + (0.0f * 0.0f);
            processed_sample = db_to_gain(8.0f) * processed_sample;
            break;
        case AlgorithmType::CustomSliders:
        default:
            processed_sample = custom_sincos_saturation(in, s.harmonics * s.custom_harmonics1, s.harmonics * s.custom_harmonics2, s.harmonics * s.custom_harmonics3,
                                                        s.harmonics * s.custom_harmonics4) +
                               (0.0f * 0.0f);
            processed_sample = db_to_gain(-4.2f) * processed_sample;
            break;
        }
        // "Hardness Saturation"
        if (s.h_algorithm == AlgorithmType::ABass3)
        {
            const float threshold = db_to_gain(-30.0f);
            const float leaf_wet = leaf_saturation(in, threshold, 0.5f);
            const float scaler = 0.0016129f * s.hoof_hardness * 100.0f; // "0.0015 default"
            processed_sample = scaler * leaf_wet + (1.0f - scaler) * processed_sample;
        }
        else
            processed_sample = chebyshev_tape(processed_sample, s.hoof_hardness);
        return processed_sample;
    }

    // "Remove DC Offset with single pole HP" (hp_b0 = 1, hp_b1 = -1, hp_gain = 1)
    static float dc(float processed_sample, float &prev_in, float &prev_out, float hp_a1)
    {
        const float hp_b0 = 1.0f, hp_b1 = -1.0f, hp_gain = 1.0f;
        processed_sample = hp_gain * processed_sample;
        const float temp_sample = hp_b0 * processed_sample + hp_b1 * prev_in - hp_a1 * prev_out;
        prev_in = processed_sample;
        prev_out = temp_sample;
        return temp_sample;
    }

    // One stereo sample of Subhoofer::process from "Normalize really small values" to the output (the input gain is
    // the caller's): in -> harmonics -> hardness -> DC high-pass -> in + wet * dry_wet -> * output_gain.
    void process(float &l, float &r, const Settings &s)
    {
        float in_l = l, in_r = r;
        if (std::fabs(in_l) < 1.18e-23f)
            in_l = 0.1f * 1.18e-17f;
        if (std::fabs(in_r) < 1.18e-23f)
            in_r = 0.1f * 1.18e-17f;
        float processed_sample_l = shape(in_l, in_l, false, s);
        float processed_sample_r = shape(in_r, in_l, true, s);
        processed_sample_l = dc(processed_sample_l, prev_processed_in_l, prev_processed_out_l, hp_a1);
        processed_sample_r = dc(processed_sample_r, prev_processed_in_r, prev_processed_out_r, hp_a1);
        // "Calculate dry/wet mix"
        const float wet_gain = s.dry_wet;
        processed_sample_l = in_l + processed_sample_l * wet_gain;
        processed_sample_r = in_r + processed_sample_r * wet_gain;
        processed_sample_l = processed_sample_l * s.output_gain;
        processed_sample_r = processed_sample_r * s.output_gain;
        l = processed_sample_l;
        r = processed_sample_r;
    }

    // [PORT] the left channel alone, in the two halves the plugin needs: harmonics + hardness, then the DC high-pass
    float shapeLeft(float in, const Settings &s)
    {
        if (std::fabs(in) < 1.18e-23f)
            in = 0.1f * 1.18e-17f;
        return shape(in, in, false, s);
    }
    float dcLeft(float processed) { return dc(processed, prev_processed_in_l, prev_processed_out_l, hp_a1); }
    // [PORT] the DC high-pass begins at this value (as if it had always been there) instead of at 0
    void primeLeft(float processed)
    {
        prev_processed_in_l = processed;
        prev_processed_out_l = 0.0f;
    }

    // [PORT] what shape() gives as "Harmonics" goes to 0: the same expressions with harmonic strength 0 (no state is
    // touched). The plugin takes this part - a plain copy of the input, louder, quieter or upside down - out of the
    // layer. (Duro Console: upstream returns 0 at exactly 0; this is what it tends to just above 0.)
    // With strength 0 every sin / cos term of the saturation functions is 0 * something = 0, so they are left out
    // here and only the "- signal" parts are added up, in upstream's order: the result is the same to the last bit as
    // calling the functions above with 0 (shapeZeroReference does exactly that; p2-sub-tests compares the two).
    static float shapeZero(float in, const Settings &s)
    {
        if (std::fabs(in) < 1.18e-23f)
            in = 0.1f * 1.18e-17f;
        float processed_sample;
        switch (s.h_algorithm)
        {
        case AlgorithmType::ABass3:
        {
            processed_sample = in * db_to_gain(-21.2f);
            const float scaler = 0.0016129f * s.hoof_hardness * 100.0f;
            return scaler * leaf_saturation(in, db_to_gain(-30.0f), 0.5f) + (1.0f - scaler) * processed_sample;
        }
        case AlgorithmType::ABass2:
            processed_sample = sincosZero(in);
            processed_sample += ((processed_sample * 2.0f) - processed_sample * processed_sample) * 0.0070118904f;
            processed_sample = db_to_gain(-2.4f) * processed_sample;
            break;
        case AlgorithmType::BBass:
            processed_sample = db_to_gain(8.7f) * (0.0f - in);
            break;
        case AlgorithmType::CBass:
        {
            float summed = 0.0f;
            for (int j = 1; j <= 7; ++j)
                summed += (0.0f - in) + (0.0f - in);
            processed_sample = summed / 7.0f;
            break;
        }
        case AlgorithmType::TanH:
            processed_sample = db_to_gain(8.0f) * tape_saturation(in, 0.0f);
            break;
        case AlgorithmType::CustomSliders:
        default:
            processed_sample = db_to_gain(-4.2f) * sincosZero(in);
            break;
        }
        return chebyshev_tape(processed_sample, s.hoof_hardness);
    }
    // custom_sincos_saturation(signal, 0, 0, 0, 0) without its four sin / cos calls
    static float sincosZero(float signal)
    {
        float summed = 0.0f;
        summed += 0.0f - signal;
        summed += 0.0f - signal;
        summed += 0.0f - signal;
        summed += 0.0f - signal;
        return summed;
    }
    // the same by calling the ported functions themselves with strength 0 (the tests' yardstick for shapeZero)
    static float shapeZeroReference(float in, const Settings &s)
    {
        if (std::fabs(in) < 1.18e-23f)
            in = 0.1f * 1.18e-17f;
        float processed_sample;
        switch (s.h_algorithm)
        {
        case AlgorithmType::ABass3:
        {
            processed_sample = in * db_to_gain(-21.2f);
            const float scaler = 0.0016129f * s.hoof_hardness * 100.0f;
            return scaler * leaf_saturation(in, db_to_gain(-30.0f), 0.5f) + (1.0f - scaler) * processed_sample;
        }
        case AlgorithmType::ABass2:
            processed_sample = custom_sincos_saturation(in, 0.0f, 0.0f, 0.0f, 0.0f);
            processed_sample += ((processed_sample * 2.0f) - processed_sample * processed_sample) * 0.0070118904f;
            processed_sample = db_to_gain(-2.4f) * processed_sample;
            break;
        case AlgorithmType::BBass:
            processed_sample = db_to_gain(8.7f) * b_bass_saturation(in, 0.0f);
            break;
        case AlgorithmType::CBass:
            processed_sample = c_bass_saturation(in, 0.0f);
            break;
        case AlgorithmType::TanH:
            processed_sample = db_to_gain(8.0f) * tape_saturation(in, 0.0f);
            break;
        case AlgorithmType::CustomSliders:
        default:
            processed_sample = db_to_gain(-4.2f) * custom_sincos_saturation(in, 0.0f, 0.0f, 0.0f, 0.0f);
            break;
        }
        return chebyshev_tape(processed_sample, s.hoof_hardness);
    }
};
} // namespace ghostblock::p2::sub::subhoofer
