// From nih-plug (https://github.com/ardura/nih-plug, the revision Subhoofer 2.2.3 builds against:
// fc2ae2da8e8aef414f53a51c24ecf378448c93e1), src/util.rs. ISC License, Copyright (c) 2022-2024 Robbert van der Helm.
// Only the two items Subhoofer's harmonic path uses, verbatim.

pub const MINUS_INFINITY_DB: f32 = -100.0;

#[inline]
pub fn db_to_gain(dbs: f32) -> f32 {
    if dbs > MINUS_INFINITY_DB {
        10.0f32.powf(dbs * 0.05)
    } else {
        0.0
    }
}
