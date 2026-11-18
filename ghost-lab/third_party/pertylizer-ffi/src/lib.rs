//! Ghost Matter <-> Pertylizer: a C interface (glue only, AGPL-3.0-only).
//! Each object wraps one of Pertylizer's own modules, set through its own descriptor (parameter names and ranges are
//! Pertylizer's), processed through its own `process`. Nothing allocates after `*_new`: the port maps and buffers are made
//! once at the largest block size, and blocks are processed in pieces of at most that size.
#![allow(unsafe_code)]
#![allow(clippy::missing_safety_doc)]

use std::collections::HashMap;
use std::os::raw::{c_char, c_int};
use synth_core::{
    AudioBuffer, AudioEffect, Bpm, Describable, InputPorts, ModuleParam, Param, PolyModule, PortName, ProcessContext,
    SampleCount, SampleRate,
};
use synth_modules::effects::reverb::Reverb;
use synth_modules::spatial_panner::SpatialPanner;
use synth_modules::{Euclidean, TuringMachine};

const MAX_BLOCK: usize = 1024;

// counts every allocation made by the Rust side (the tests check none happen while audio plays)
struct Counting;
static ALLOCS: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
unsafe impl std::alloc::GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: std::alloc::Layout) -> *mut u8 { ALLOCS.fetch_add(1, std::sync::atomic::Ordering::Relaxed); unsafe { std::alloc::System.alloc(l) } }
    unsafe fn dealloc(&self, p: *mut u8, l: std::alloc::Layout) { unsafe { std::alloc::System.dealloc(p, l) } }
    unsafe fn realloc(&self, p: *mut u8, l: std::alloc::Layout, n: usize) -> *mut u8 { ALLOCS.fetch_add(1, std::sync::atomic::Ordering::Relaxed); unsafe { std::alloc::System.realloc(p, l, n) } }
}
#[global_allocator]
static GLOBAL: Counting = Counting;
#[unsafe(no_mangle)]
pub extern "C" fn gm_rust_allocs() -> usize { ALLOCS.load(std::sync::atomic::Ordering::Relaxed) }

struct Params(Vec<(String, Param)>);
impl Params {
    fn of(d: &dyn Describable) -> Self { Params(d.descriptor().parameters.into_iter().map(|p| (p.type_id, p.id)).collect()) }
    fn find(&self, name: &str) -> Option<Param> { self.0.iter().find(|(n, _)| n == name).map(|(_, p)| *p) }
}

unsafe fn name_of<'a>(s: *const c_char) -> &'a str {
    if s.is_null() { return ""; }
    unsafe { std::ffi::CStr::from_ptr(s) }.to_str().unwrap_or("")
}

fn ctx(sr: f32, n: usize, bpm: f32) -> ProcessContext<'static> {
    ProcessContext { sample_rate: SampleRate::new(sr), samples: SampleCount::new(n), tempo: Bpm::new(bpm), ..ProcessContext::default() }
}

// ------------------------------------------------------------------------------------------------ poly modules
pub struct Poly<M: PolyModule> {
    m: M,
    params: Params,
    sr: f32,
    bpm: f32,
    ins: Vec<(PortName, AudioBuffer)>,
    outs: HashMap<PortName, AudioBuffer>,
}

impl<M: PolyModule + Describable> Poly<M> {
    fn new(mut m: M, sr: f32, ins: &[PortName], outs: &[PortName]) -> Self {
        let params = Params::of(&m);
        let d = m.descriptor();
        if let Some(o) = m.mod_offsets_mut() { o.populate(&d); }
        m.set_sample_rate(SampleRate::new(sr));
        let mut om = HashMap::with_capacity(outs.len() * 2);
        for p in outs { om.insert(*p, AudioBuffer::new(MAX_BLOCK)); }
        Poly { m, params, sr, bpm: 120.0, ins: ins.iter().map(|p| (*p, AudioBuffer::new(MAX_BLOCK))).collect(), outs: om }
    }
    fn set(&mut self, name: &str, v: f32) -> bool {
        match self.params.find(name) { Some(p) => { self.m.set_param(p.with_f32(v)); true } None => false }
    }
    // one piece (n <= MAX_BLOCK): copy inputs in, process, outputs readable in self.outs
    fn run(&mut self, n: usize, fill: impl Fn(usize, &mut [f32])) {
        for (k, (_, b)) in self.ins.iter_mut().enumerate() { b.resize(n); fill(k, b.as_mut_slice()); }
        let refs: [(PortName, &AudioBuffer); 2];
        let c = ctx(self.sr, n, self.bpm);
        match self.ins.len() {
            0 => self.m.process(InputPorts::empty(), &mut self.outs, &c),
            1 => { let r = [(self.ins[0].0, &self.ins[0].1)]; self.m.process(InputPorts::new(&r), &mut self.outs, &c) }
            _ => { refs = [(self.ins[0].0, &self.ins[0].1), (self.ins[1].0, &self.ins[1].1)]; self.m.process(InputPorts::new(&refs), &mut self.outs, &c) }
        }
    }
    fn out(&self, p: PortName) -> &[f32] { self.outs.get(&p).map(|b| b.as_slice()).unwrap_or(&[]) }
}

// ---- Spatial Panner (mono in, stereo out)
pub type GmPanner = Poly<SpatialPanner>;

#[unsafe(no_mangle)]
pub extern "C" fn gm_panner_new(sr: f32) -> *mut GmPanner {
    Box::into_raw(Box::new(Poly::new(SpatialPanner::new(), sr, &[PortName::IN], &[PortName::OUT_L, PortName::OUT_R])))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_panner_free(p: *mut GmPanner) { if !p.is_null() { drop(unsafe { Box::from_raw(p) }); } }
/// a parameter by Pertylizer's own name (x, y, z, distance, diffusion, er_level, direct_level, absorption, air_absorption)
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_panner_set(p: *mut GmPanner, name: *const c_char, v: f32) -> c_int {
    unsafe { (*p).set(name_of(name), v) as c_int }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_panner_process(p: *mut GmPanner, input: *const f32, out_l: *mut f32, out_r: *mut f32, n: c_int) {
    let p = unsafe { &mut *p };
    let n = n.max(0) as usize;
    let (inp, ol, or) = unsafe { (std::slice::from_raw_parts(input, n), std::slice::from_raw_parts_mut(out_l, n), std::slice::from_raw_parts_mut(out_r, n)) };
    let mut at = 0;
    while at < n {
        let m = (n - at).min(MAX_BLOCK);
        p.run(m, |_, b| b.copy_from_slice(&inp[at..at + m]));
        ol[at..at + m].copy_from_slice(&p.out(PortName::OUT_L)[..m]);
        or[at..at + m].copy_from_slice(&p.out(PortName::OUT_R)[..m]);
        at += m;
    }
}

// ---- Euclidean + Turing machine, clocked by us (a gate on each step), read back per sample
pub type GmEuclid = Poly<Euclidean>;
pub type GmTuring = Poly<TuringMachine>;

#[unsafe(no_mangle)]
pub extern "C" fn gm_euclid_new(sr: f32) -> *mut GmEuclid {
    Box::into_raw(Box::new(Poly::new(Euclidean::new(), sr, &[PortName::CLOCK], &[PortName::GATE, PortName::ACCENT])))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_euclid_free(p: *mut GmEuclid) { if !p.is_null() { drop(unsafe { Box::from_raw(p) }); } }
/// steps, pulses, rotation, swing
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_euclid_set(p: *mut GmEuclid, name: *const c_char, v: f32) -> c_int { unsafe { (*p).set(name_of(name), v) as c_int } }
#[unsafe(no_mangle)]
pub extern "C" fn gm_turing_new(sr: f32) -> *mut GmTuring {
    Box::into_raw(Box::new(Poly::new(TuringMachine::new(), sr, &[PortName::CLOCK], &[PortName::PITCH, PortName::GATE])))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_turing_free(p: *mut GmTuring) { if !p.is_null() { drop(unsafe { Box::from_raw(p) }); } }
/// mutation, range, scale, length
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_turing_set(p: *mut GmTuring, name: *const c_char, v: f32) -> c_int { unsafe { (*p).set(name_of(name), v) as c_int } }

/// clock: n samples of the clock gate (0 / 1). gate, accent: the Euclidean module's outputs.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_euclid_process(p: *mut GmEuclid, clock: *const f32, gate: *mut f32, accent: *mut f32, n: c_int) {
    let p = unsafe { &mut *p };
    let n = n.max(0) as usize;
    let (ck, g, a) = unsafe { (std::slice::from_raw_parts(clock, n), std::slice::from_raw_parts_mut(gate, n), std::slice::from_raw_parts_mut(accent, n)) };
    let mut at = 0;
    while at < n {
        let m = (n - at).min(MAX_BLOCK);
        p.run(m, |_, b| b.copy_from_slice(&ck[at..at + m]));
        g[at..at + m].copy_from_slice(&p.out(PortName::GATE)[..m]);
        a[at..at + m].copy_from_slice(&p.out(PortName::ACCENT)[..m]);
        at += m;
    }
}
/// pitch: the Turing machine's pitch output (its own units), gate its gate
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_turing_process(p: *mut GmTuring, clock: *const f32, pitch: *mut f32, gate: *mut f32, n: c_int) {
    let p = unsafe { &mut *p };
    let n = n.max(0) as usize;
    let (ck, pi, g) = unsafe { (std::slice::from_raw_parts(clock, n), std::slice::from_raw_parts_mut(pitch, n), std::slice::from_raw_parts_mut(gate, n)) };
    let mut at = 0;
    while at < n {
        let m = (n - at).min(MAX_BLOCK);
        p.run(m, |_, b| b.copy_from_slice(&ck[at..at + m]));
        pi[at..at + m].copy_from_slice(&p.out(PortName::PITCH)[..m]);
        g[at..at + m].copy_from_slice(&p.out(PortName::GATE)[..m]);
        at += m;
    }
}

// ------------------------------------------------------------------------------------------------ Reverb (stereo effect)
pub struct GmReverb { r: Reverb, params: Params, sr: f32, inter: Vec<f32>, out: Vec<f32> }

#[unsafe(no_mangle)]
pub extern "C" fn gm_reverb_new(sr: f32) -> *mut GmReverb {
    let mut r = Reverb::new();
    r.set_sample_rate(SampleRate::new(sr));
    let params = Params::of(&r);
    Box::into_raw(Box::new(GmReverb { r, params, sr, inter: vec![0.0; MAX_BLOCK * 2], out: vec![0.0; MAX_BLOCK * 2] }))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_reverb_free(p: *mut GmReverb) { if !p.is_null() { drop(unsafe { Box::from_raw(p) }); } }
/// room_size, decay, damping, diffusion, pre_delay, low_cut, width, mix (Pertylizer's own names and units)
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_reverb_set(p: *mut GmReverb, name: *const c_char, v: f32) -> c_int {
    let p = unsafe { &mut *p };
    match p.params.find(unsafe { name_of(name) }) { Some(q) => { p.r.set_param(q.with_f32(v)); 1 } None => 0 }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_reverb_process(p: *mut GmReverb, l: *mut f32, r: *mut f32, n: c_int) {
    let p = unsafe { &mut *p };
    let n = n.max(0) as usize;
    let (l, r) = unsafe { (std::slice::from_raw_parts_mut(l, n), std::slice::from_raw_parts_mut(r, n)) };
    let mut at = 0;
    while at < n {
        let m = (n - at).min(MAX_BLOCK);
        for i in 0..m { p.inter[2 * i] = l[at + i]; p.inter[2 * i + 1] = r[at + i]; }
        let c = ctx(p.sr, m, 120.0);
        p.r.process(&p.inter[..2 * m], &mut p.out[..2 * m], &c);
        for i in 0..m { l[at + i] = p.out[2 * i]; r[at + i] = p.out[2 * i + 1]; }
        at += m;
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_reverb_reset(p: *mut GmReverb) { unsafe { (*p).r.reset() } }

/// the tempo the clocked modules see (Euclidean / Turing use it only when free-running)
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gm_euclid_set_bpm(p: *mut GmEuclid, bpm: f32) { unsafe { (*p).bpm = bpm } }
