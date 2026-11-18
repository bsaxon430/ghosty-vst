// BASS update checks (included by BassTests.cpp when BassPresets.h exists)

struct TestHead : juce::AudioPlayHead {
    double bpm = 140, sr = 44100, ppq0 = 0; juce::int64 pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo i; i.setBpm(bpm); i.setIsPlaying(playing); i.setPpqPosition(ppq0 + (double) pos / sr * bpm / 60.0); i.setTimeInSamples(pos); return i;
    }
};

// a 2-bar bass line at 140 BPM (16th steps), notes around C2
struct Step { int at, len, note; };
static const std::vector<Step>& bassLine() {
    static const std::vector<Step> s { { 0, 3, 36 }, { 4, 2, 36 }, { 6, 2, 39 }, { 8, 4, 36 }, { 12, 2, 43 }, { 14, 2, 41 },
                                       { 16, 3, 36 }, { 20, 2, 36 }, { 22, 2, 39 }, { 24, 6, 34 }, { 30, 2, 38 } };
    return s;
}
static std::vector<Ev> lineEvents(double bpm, int bars = 2, int transpose = 0, const std::vector<Step>* line = nullptr) {
    const auto& L = line ? *line : bassLine();
    const double st = 60.0 / bpm / 4.0; std::vector<Ev> e;
    for (int b = 0; b < bars; b += 2)
        for (auto& s : L) { const double t0 = (b * 16 + s.at) * st; e.push_back({ t0, s.note + transpose, 0.9f }); e.push_back({ t0 + s.len * st - 0.004, s.note + transpose, 0 }); }
    std::stable_sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    return e;
}
// render with a playing host (tempo + beat position), optionally timing processBlock
static Stereo renderHost(PoltergeistProcessor& p, double sr, double bpm, const std::vector<Ev>& evs, double secs, double* cpuSecs = nullptr, long* allocs = nullptr, double ppq0 = 0, int block = 512) {
    TestHead head; head.bpm = bpm; head.sr = sr; head.ppq0 = ppq0; p.setPlayHead(&head);
    p.setRateAndBufferSizeDetails(sr, block); p.prepareToPlay(sr, block);
    const int n = (int) (secs * sr); Stereo out((size_t) n * 2, 0.0f);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; midi.ensureSize(4096);
    double cpu = 0; long al = 0;
    for (int pos = 0; pos < n; pos += block) {
        midi.clear(); head.pos = pos;
        for (auto& e : evs) { const int at = (int) std::llround(e.t * sr) - pos; if (at >= 0 && at < block) midi.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, e.vel) : juce::MidiMessage::noteOff(1, e.note), at); }
        buf.clear();
        const auto t0 = std::chrono::steady_clock::now();
        gAllocs = 0; gCount = true; p.processBlock(buf, midi); gCount = false; al += gAllocs.load();
        cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (int i = 0; i < block && pos + i < n; ++i) { out[(size_t) (pos + i)] = buf.getSample(0, i); out[(size_t) (n + pos + i)] = buf.getSample(1, i); }
    }
    p.setPlayHead(nullptr);
    if (cpuSecs) *cpuSecs = cpu;
    if (allocs) *allocs = al;
    return out;
}

// ---- measurements
struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Bq lp(double f, double fs, double q = 0.7071) { Bq b; const double w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al; b.b0 = (1 - c) / 2 / a0; b.b1 = (1 - c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b; }
    static Bq hp(double f, double fs, double q = 0.7071) { Bq b; const double w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al; b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b; }
    double tick(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
static std::vector<double> filt(const float* x, int n, std::vector<Bq> chain) { std::vector<double> y((size_t) n); for (int i = 0; i < n; ++i) { double v = x[i]; for (auto& b : chain) v = b.tick(v); y[(size_t) i] = v; } return y; }
struct Meas { bool finite = true; double peakDb = -200, rmsDb = -200, sideBelowDb = -200, lowFrac = 0, hiFrac = 0; };
static Meas measure(const Stereo& s, double sr) {
    Meas m; const int n = (int) s.size() / 2; const float* L = s.data(); const float* R = s.data() + n;
    double pk = 0, e = 0;
    for (int i = 0; i < n; ++i) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) { m.finite = false; return m; } pk = juce::jmax(pk, (double) std::abs(L[i]), (double) std::abs(R[i])); e += 0.5 * (L[i] * L[i] + R[i] * R[i]); }
    m.peakDb = 20 * std::log10(pk + 1e-12); m.rmsDb = 10 * std::log10(e / n + 1e-24);
    std::vector<float> mid((size_t) n), side((size_t) n);
    for (int i = 0; i < n; ++i) { mid[(size_t) i] = 0.5f * (L[i] + R[i]); side[(size_t) i] = 0.5f * (L[i] - R[i]); }
    auto lp120 = [&] { return std::vector<Bq> { Bq::lp(120, sr), Bq::lp(120, sr) }; };
    const auto ml = filt(mid.data(), n, lp120()), sl = filt(side.data(), n, lp120());
    const auto low = filt(mid.data(), n, { Bq::lp(90, sr), Bq::lp(90, sr) }), hi = filt(mid.data(), n, { Bq::hp(300, sr), Bq::hp(300, sr) });
    double em = 0, es = 0, el = 0, eh = 0, et = 0;
    for (int i = 0; i < n; ++i) { em += ml[(size_t) i] * ml[(size_t) i]; es += sl[(size_t) i] * sl[(size_t) i]; el += low[(size_t) i] * low[(size_t) i]; eh += hi[(size_t) i] * hi[(size_t) i]; et += (double) mid[(size_t) i] * mid[(size_t) i]; }
    m.sideBelowDb = 10 * std::log10((es + 1e-20) / (em + 1e-20)); m.lowFrac = el / (et + 1e-20); m.hiFrac = eh / (et + 1e-20);
    return m;
}
// what each preset is measured / tuned with: the bass line around C2, or for LEADS the same line two octaves up (C4)
// (LEADS: bar 1 around C4, bar 2 an octave up, so the trims hold over both registers a lead is played in)
static std::vector<Ev> presetEvents(const juce::String& folder) {
    if (folder != "LEADS") return lineEvents(140);
    auto e = lineEvents(140, 2, 24); const double bar = 60.0 / 140.0 * 4;
    for (auto& x : e) if (x.t >= bar - 1e-6) x.note += 12;
    return e;
}
// integrated loudness (ITU-R BS.1770: K-weighting, 400 ms blocks, -70 LUFS and -10 LU gates)
static double lufs(const Stereo& s, double sr) {
    const int n = (int) s.size() / 2;
    auto kfilter = [&](const float* x) {
        double K = std::tan(juce::MathConstants<double>::pi * 1681.974450955533 / sr), Q = 0.7071752369554196, Vh = std::pow(10.0, 3.999843853973347 / 20), Vb = std::pow(Vh, 0.4996667741545416);
        double a0 = 1 + K / Q + K * K; Bq sh; sh.b0 = (Vh + Vb * K / Q + K * K) / a0; sh.b1 = 2 * (K * K - Vh) / a0; sh.b2 = (Vh - Vb * K / Q + K * K) / a0; sh.a1 = 2 * (K * K - 1) / a0; sh.a2 = (1 - K / Q + K * K) / a0;
        K = std::tan(juce::MathConstants<double>::pi * 38.13547087602444 / sr); Q = 0.5003270373238773; a0 = 1 + K / Q + K * K;
        Bq hp; hp.b0 = 1; hp.b1 = -2; hp.b2 = 1; hp.a1 = 2 * (K * K - 1) / a0; hp.a2 = (1 - K / Q + K * K) / a0;
        return filt(x, n, { sh, hp });
    };
    const auto l = kfilter(s.data()), r = kfilter(s.data() + n);
    const int blk = (int) (0.4 * sr), hop = blk / 4; std::vector<double> z;
    for (int a = 0; a + blk <= n; a += hop) { double e = 0; for (int i = a; i < a + blk; ++i) e += l[(size_t) i] * l[(size_t) i] + r[(size_t) i] * r[(size_t) i]; z.push_back(e / blk); }
    auto gated = [&](double thr) { double sum = 0; int c = 0; for (double x : z) if (-0.691 + 10 * std::log10(x + 1e-30) > thr) { sum += x; ++c; } return c ? sum / c : 1e-30; };
    const double rel = -0.691 + 10 * std::log10(gated(-70)) - 10;
    return -0.691 + 10 * std::log10(gated(rel));
}
// the LEADS demo phrase (A minor, 4 bars of 16ths: { at, len, note }), A4-G5
static const std::vector<Step>& leadPhrase() {
    static const std::vector<Step> phrase { { 0, 3, 69 }, { 3, 3, 72 }, { 6, 2, 76 }, { 8, 6, 74 }, { 14, 2, 72 }, { 16, 3, 71 }, { 19, 3, 72 }, { 22, 2, 74 }, { 24, 8, 76 },
                                     { 32, 3, 69 }, { 35, 3, 72 }, { 38, 2, 76 }, { 40, 4, 79 }, { 44, 4, 77 }, { 48, 3, 76 }, { 51, 3, 74 }, { 54, 2, 72 }, { 56, 8, 74 } };
    return phrase;
}
static std::vector<Ev> leadPhraseEvents(double bpm) {
    const double st = 60.0 / bpm / 4.0; std::vector<Ev> e;
    for (auto& x : leadPhrase()) { e.push_back({ x.at * st, x.note, 0.85f }); e.push_back({ (x.at + x.len) * st - 0.01, x.note, 0 }); }
    std::stable_sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    return e;
}
// a lead's loudness: the mean of the test line (C4 / C5 bars) and the A4-G5 phrase, so the trim holds over registers and note lengths
static double leadLoudness(PoltergeistProcessor& p, double* peakDb) {
    const auto a = renderHost(p, 44100, 140, presetEvents("LEADS"), 60.0 / 140.0 * 8 + 0.6);
    const auto b = renderHost(p, 44100, 128, leadPhraseEvents(128), 60.0 / 128.0 * 16 + 1.0);
    if (peakDb) *peakDb = juce::jmax(measure(a, 44100).peakDb, measure(b, 44100).peakDb);
    return 0.5 * (lufs(a, 44100) + lufs(b, 44100));
}
static int bassIndex(const char* name) { const auto& v = gs::bassPresets(); for (int i = 0; i < (int) v.size(); ++i) if (juce::String(v[(size_t) i].name) == name) return i; return 0; }
static void setPN(PoltergeistProcessor& p, const char* id, float v) { setP(p, id, v); }
static bool shown(juce::Component* c) { for (; c != nullptr && c->getParentComponent() != nullptr; c = c->getParentComponent()) if (!c->isVisible()) return false; return c != nullptr; }   // (visible up to the editor: no desktop window in the tests)

// ---- every new preset: a 2-bar line at 140 BPM
static constexpr double kTargetPeak = -7.0, kLeadLufs = -15.5;
static void testPresets() {
    const auto& v = gs::bassPresets(); const int L = PoltergeistProcessor::numLegacyPresets();
    std::map<juce::String, int> perFolder;
    double pkMin = 99, pkMax = -99, cpuMax = 0, cpuSum = 0, leadLoMin = 99, leadLoMax = -99, leadPkMax = -99; int bad = 0, monoBad = 0, subBad = 0; long allocs = 0;
    std::printf("\n%-24s %-18s %7s %7s %7s %8s %6s %6s %7s\n", "preset", "folder", "peak", "rms", "LUFS", "side<120", "low%", "hi%", "cpu%");
    for (int i = 0; i < (int) v.size(); ++i) {
        PoltergeistProcessor p; p.loadPreset(L + i);
        double cpu = 0; long al = 0;
        const double secs = 60.0 / 140.0 * 8 + 0.6;
        auto s = renderHost(p, 44100, 140, presetEvents(v[(size_t) i].folder), secs, &cpu, &al);
        const auto m = measure(s, 44100);
        double leadPk = 0; const double lu = juce::String(v[(size_t) i].folder) == "LEADS" ? leadLoudness(p, &leadPk) : lufs(s, 44100);
        if (juce::String(v[(size_t) i].folder) == "LEADS") { leadLoMin = juce::jmin(leadLoMin, lu); leadLoMax = juce::jmax(leadLoMax, lu); }
        const double cpuPct = cpu / secs * 100.0;   // mono bass: one voice
        cpuMax = juce::jmax(cpuMax, cpuPct); cpuSum += cpuPct; allocs += al;
        const bool isLead = juce::String(v[(size_t) i].folder) == "LEADS";
        if (!isLead) { pkMin = juce::jmin(pkMin, m.peakDb); pkMax = juce::jmax(pkMax, m.peakDb); } else leadPkMax = juce::jmax(leadPkMax, leadPk);
        if (!m.finite || m.rmsDb < -40) ++bad;
        if (m.sideBelowDb > -30 && m.lowFrac > 0.01) ++monoBad;   // (leads have no low end to keep mono)
        const juce::String folder = v[(size_t) i].folder;
        const bool wantSub = folder == "SUB" || folder == "808";
        if (wantSub && m.lowFrac < 0.5) ++subBad;
        perFolder[folder]++;
        std::printf("%-24s %-18s %7.2f %7.2f %7.1f %8.1f %6.1f %6.1f %7.2f%s\n", v[(size_t) i].name, v[(size_t) i].folder, m.peakDb, m.rmsDb, lu, m.sideBelowDb, m.lowFrac * 100, m.hiFrac * 100, cpuPct, m.finite ? "" : "  NaN!");
    }
    juce::String folders; for (auto& [f, n] : perFolder) folders << f << " " << n << ", ";
    check(v.size() >= 50, juce::String((int) v.size()) + " new presets: " + folders.dropLastCharacters(2));
    check(perFolder["GROWL / SCREECH"] >= 10 && perFolder["RIDDIM"] >= 6 && perFolder["WOBBLE"] >= 8 && perFolder["REESE / NEURO"] >= 6 && perFolder["ROLLING / CLEAN"] >= 8
          && perFolder["SUB"] >= 4 && perFolder["808"] >= 6 && perFolder["MID-BASS PLUCK"] >= 4 && perFolder["LEADS"] >= 10, "every folder has its minimum count");
    check(leadLoMax - leadLoMin <= 2.0 && leadPkMax <= -2.9, "LEADS loudness-matched (mean of the C4/C5 test line and the A4-G5 demo phrase): " + juce::String(leadLoMin, 1) + " .. " + juce::String(leadLoMax, 1) + " LUFS (spread " + juce::String(leadLoMax - leadLoMin, 1) + " LU), peaks at most " + juce::String(leadPkMax, 1) + " dBFS");
    check(bad == 0, "every preset renders the line: finite, audible (" + juce::String(bad) + " bad)");
    check(pkMax - pkMin <= 3.0, "bass peak levels within " + juce::String(pkMax - pkMin, 2) + " dB across presets (" + juce::String(pkMin, 1) + " .. " + juce::String(pkMax, 1) + " dBFS)");
    check(monoBad == 0, "low end mono: side/mid energy below 120 Hz under -30 dB in every preset (" + juce::String(monoBad) + " over)");
    check(subBad == 0, "SUB / 808 presets: over half their energy below 90 Hz (" + juce::String(subBad) + " short)");
    check(allocs == 0, "no heap allocations in processBlock across all presets (" + juce::String(allocs) + ")");
    std::printf("CPU per voice (mono bass voice, 44.1 kHz, 2x oversampled core): mean %.2f%%, max %.2f%% of one core\n", cpuSum / (double) v.size(), cpuMax);
}

// every preset goes quiet after its last note: no ringing filters, stuck voices, wobble leaks or DC (6 s after the last note off)
static void testTails() {
    const auto& v = gs::bassPresets(); const int L = PoltergeistProcessor::numLegacyPresets();
    const double sr = 44100, line = 60.0 / 140.0 * 8, secs = line + 6.0;
    int bad = 0; double worst = -301, worstDc = -301; juce::String worstName;
    for (int i = 0; i < (int) v.size(); ++i) {
        PoltergeistProcessor p; p.loadPreset(L + i);
        const auto s = renderHost(p, sr, 140, presetEvents(v[(size_t) i].folder), secs);
        const int n = (int) s.size() / 2, a = n - (int) (0.5 * sr);
        double pk = 0, dc = 0;
        for (int k = a; k < n; ++k) { pk = juce::jmax(pk, (double) std::abs(s[(size_t) k]), (double) std::abs(s[(size_t) (n + k)])); dc += s[(size_t) k]; }
        dc = std::abs(dc / (n - a));
        const double pkDb = 20 * std::log10(pk + 1e-15), dcDb = 20 * std::log10(dc + 1e-15);
        if (pkDb > worst) { worst = pkDb; worstName = v[(size_t) i].name; }
        worstDc = juce::jmax(worstDc, dcDb);
        if (pkDb > -90 || !std::isfinite(pk)) { ++bad; std::printf("   %s still sounds 6 s after release: %.1f dBFS\n", v[(size_t) i].name, pkDb); }
    }
    check(bad == 0, "silence after release: every preset under -90 dBFS 5.5-6 s after the last note (loudest tail " + juce::String(worst, 1) + " dBFS, "
          + worstName + "; worst DC " + juce::String(worstDc, 1) + " dBFS)");
}

// level trims: each preset aimed at kTargetPeak (prints BassTrim.h)
static void tune() {
    const auto& v = gs::bassPresets(); const int L = PoltergeistProcessor::numLegacyPresets();
    std::printf("// generated by PoltergeistBassTests tune\n");
    for (int i = 0; i < (int) v.size(); ++i) {
        PoltergeistProcessor p; p.loadPreset(L + i);
        float master = p.apvts.getRawParameterValue("master")->load();
        const bool isLead = juce::String(v[(size_t) i].folder) == "LEADS";
        for (int it = 0; it < 5; ++it) {
            setP(p, "master", master);
            const auto s = renderHost(p, 44100, 140, presetEvents(v[(size_t) i].folder), 60.0 / 140.0 * 8 + 0.6);
            const auto m = measure(s, 44100);
            // basses: the same peak; LEADS: the same loudness (they're heard over a mix), peaks kept under -3 dBFS
            double lpk = 0; const double llu = isLead ? leadLoudness(p, &lpk) : 0.0;
            const double step = isLead ? juce::jmin(kLeadLufs - llu, -3.0 - lpk) : kTargetPeak - m.peakDb;
            master = juce::jlimit(-24.0f, 6.0f, master + (float) step * 0.9f);
        }
        std::printf("        { \"%s\", %.1ff },\n", v[(size_t) i].name, master);
    }
    double lo = 99, hi = -99;
    for (int i = 0; i < PoltergeistProcessor::numLegacyPresets(); ++i) { PoltergeistProcessor p; p.loadPreset(i); const auto m = measure(renderProc(p, 44100, legacyPhrase(), 4.5), 44100); lo = juce::jmin(lo, m.peakDb); hi = juce::jmax(hi, m.peakDb); }
    std::printf("// (the original presets peak between %.1f and %.1f dBFS)\n", lo, hi);
}

// a single sustained note through the engine with a patch, for filter checks
static Stereo renderPatch(gs::Patch patch, int note, double secs, double sr = 44100) {
    auto snap = gs::snapFromPatch(patch);
    return renderEngine(snap, sr, { { 0.0, note, 0.9f }, { secs - 0.05, note, 0 } }, secs);
}
static gs::Patch basicPatch() { gs::Patch p = gs::Summoner::base(); p["mono"] = 1; p["master"] = -6; p[gs::opId(0, "level")] = 0.8f; p[gs::opId(0, "a")] = 0.002f; p[gs::opId(0, "s")] = 1.0f; p[gs::opId(0, "d")] = 1.0f; return p; }
// magnitude at frequency f over x[a..b) (Goertzel)
static double magAt(const float* x, int a, int b, double f, double sr) {
    const double w = 2 * juce::MathConstants<double>::pi * f / sr, c = 2 * std::cos(w); double s1 = 0, s2 = 0;
    for (int i = a; i < b; ++i) { const double s0 = x[i] * (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * (i - a) / (b - a))) + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (b - a);
}

static void testFormant() {
    // a saw at 65.4 Hz through FILTER 1 = FORMANT at five vowel positions; find the two strongest formant regions
    const double sr = 44100, f0 = 65.406;
    std::vector<std::pair<double, double>> found;
    juce::String txt;
    for (int k = 0; k < 5; ++k) {
        auto p = basicPatch(); p[gs::opId(0, "wtype")] = 3; p["f1_on"] = 1; p["f1_type"] = 8; p["f1_res"] = 0.8f;
        p["f1_cut"] = 20.0f * std::pow(2.0f, (k / 4.0f) * 9.9658f);   // the cutoff knob is the vowel: a e i o u
        auto s = renderPatch(p, 36, 1.0, sr); const int n = (int) s.size() / 2;
        std::vector<double> h; for (int m = 1; m * f0 < 4000; ++m) h.push_back(magAt(s.data(), n / 3, n - 2000, m * f0, sr));
        // F1 = strongest harmonic below 1000 Hz, F2 = strongest between 650 and 3200 Hz at least 300 Hz above F1
        int i1 = 0; for (int m = 0; m < (int) h.size(); ++m) if ((m + 1) * f0 < 1000 && h[(size_t) m] > h[(size_t) i1]) i1 = m;
        int i2 = -1; for (int m = 0; m < (int) h.size(); ++m) { const double f = (m + 1) * f0; if (f > 650 && f < 3200 && f > (i1 + 1) * f0 + 300 && (i2 < 0 || h[(size_t) m] > h[(size_t) i2])) i2 = m; }
        found.push_back({ (i1 + 1) * f0, (i2 + 1) * f0 });
        txt << "AEIOU"[k] << " " << juce::roundToInt((i1 + 1) * f0) << "/" << juce::roundToInt((i2 + 1) * f0) << "  ";
    }
    int distinct = 0;
    for (size_t a = 0; a < found.size(); ++a) for (size_t b = a + 1; b < found.size(); ++b)
        if (std::abs(std::log(found[a].first / found[b].first)) > 0.12 || std::abs(std::log(found[a].second / found[b].second)) > 0.12) ++distinct;
    const bool aeIOk = found[0].first > found[2].first && found[2].second > found[0].second;   // A has the higher F1, I the higher F2
    check(distinct == 10 && aeIOk, "FORMANT filter: vowels distinguishable, formant peaks move (F1/F2 Hz) " + txt);
}

static void testComb() {
    // white noise through COMB + with full keytrack, the comb pitch follows the played note
    const double sr = 44100;
    auto measurePitch = [&](int note) {
        auto p = basicPatch(); p[gs::opId(0, "level")] = 0.0f; p["nz_level"] = 0.8f; p["nz_type"] = 0;
        p["f1_on"] = 1; p["f1_type"] = 6; p["f1_cut"] = 261.63f; p["f1_key"] = 1.0f; p["f1_res"] = 0.9f;
        auto s = renderPatch(p, note, 1.0, sr); const int n = (int) s.size() / 2;
        // autocorrelation peak between 1.5 ms and 20 ms
        const int a = n / 3, b = n - 2000; double best = -1; int lag = 0;
        std::vector<double> acv((size_t) (sr / 50) + 2, 0.0);
        for (int L = (int) (sr / 700); L < (int) (sr / 50); ++L) { double c = 0; for (int i = a; i < b - L; ++i) c += s[(size_t) i] * s[(size_t) (i + L)]; acv[(size_t) L] = c; if (c > best) best = c; }
        for (int L = (int) (sr / 700) + 1; L < (int) (sr / 50) - 1; ++L) if (acv[(size_t) L] >= 0.8 * best && acv[(size_t) L] >= acv[(size_t) L - 1] && acv[(size_t) L] >= acv[(size_t) L + 1]) { lag = L; break; }
        // refine with a parabola
        auto ac = [&](int L) { double c = 0; for (int i = a; i < b - L; ++i) c += s[(size_t) i] * s[(size_t) (i + L)]; return c; };
        const double y0 = ac(lag - 1), y1 = ac(lag), y2 = ac(lag + 1), d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
        return sr / (lag + d);
    };
    const double f48 = measurePitch(48), f60 = measurePitch(60), f55 = measurePitch(55);
    const double e48 = 130.81, e60 = 261.63, e55 = 196.0;
    const bool ok = std::abs(f48 / e48 - 1) < 0.03 && std::abs(f60 / e60 - 1) < 0.03 && std::abs(f55 / e55 - 1) < 0.03;
    check(ok, "COMB keytracks: C3 " + juce::String(f48, 1) + " Hz (130.8), G3 " + juce::String(f55, 1) + " Hz (196.0), C4 " + juce::String(f60, 1) + " Hz (261.6)");
}

static void testWobbleSync() {
    // WOBBLE (gate shape, 1/8) ducks all ghost levels; its edges must land on the host's 1/8 grid, also when the host starts mid-bar
    const double sr = 44100, bpm = 140;
    for (double ppq0 : { 0.0, 3.37 }) {
        PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Clean Ghost Bass"));
        setP(p, "sub_on", 0); setP(p, gs::modId(1, 0).toRawUTF8(), 0); setP(p, gs::modId(3, 0).toRawUTF8(), 0);
        setP(p, "wob_div", 9); setP(p, "wob_smooth", 0); setP(p, "wob_n", 4);
        const float X[4] = { 0, 0.4995f, 0.5f, 0.9995f }, Y[4] = { 1, 1, 0, 0 };
        for (int i = 0; i < 4; ++i) { setP(p, ("wob_x" + juce::String(i + 1)).toRawUTF8(), X[i]); setP(p, ("wob_y" + juce::String(i + 1)).toRawUTF8(), Y[i]); }
        setP(p, "mm1_src", 1); setP(p, "mm1_dst", gs::mdLvlAll); setP(p, "mm1_amt", -1.0f);
        const double secs = 60.0 / bpm * 4 * 8;   // 8 bars, 64 eighths
        auto s = renderHost(p, sr, bpm, { { 0.0, 96, 0.9f }, { secs - 0.1, 96, 0 } }, secs, nullptr, nullptr, ppq0, 512);
        const int n = (int) s.size() / 2, lat = p.getLatencySamples();
        // rising edges: the first sample above 2% of the peak after at least 4 ms of silence (a 2 kHz tone, so the edge is sharp)
        float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(s[(size_t) i]));
        if (std::getenv("WOBDBG")) for (int w = 0; w < 40; ++w) { float m = 0; for (int i = (int) (sr * 0.5) + w * 600; i < (int) (sr * 0.5) + w * 600 + 600; ++i) m = juce::jmax(m, std::abs(s[(size_t) i])); std::printf("%.3f ", m / pk); }
        const double eighth = 60.0 / bpm / 2.0 * sr;
        std::vector<double> errs; int quiet = 0;
        for (int i = (int) (sr * 0.3); i < n; ++i) {
            const float a = std::abs(s[(size_t) i]);
            if (a < pk * 0.005f) { ++quiet; continue; }
            if (a > pk * 0.02f && quiet > (int) (0.004 * sr)) {
                const double beatsAt = ppq0 + (i - lat) / sr * bpm / 60.0;
                const double cyc = beatsAt / 0.5, frac = cyc - std::floor(cyc);
                errs.push_back((frac - 0.5) * eighth);
            }
            if (a > pk * 0.02f) quiet = 0;
        }
        if (std::getenv("WOBDBG")) for (size_t q = 0; q < errs.size(); q += 8) std::printf("  edge %d err %.1f\n", (int) q, errs[q]);
        double mx = 0, first = 0, last = 0; const int q8 = juce::jmin(8, (int) errs.size());
        for (int q = 0; q < q8; ++q) { first += errs[(size_t) q] / q8; last += errs[errs.size() - 1 - (size_t) q] / q8; }
        for (double x : errs) mx = juce::jmax(mx, std::abs(x));
        check(errs.size() >= 60 && mx < 16 && std::abs(last - first) < 4.0,
              "WOBBLE locks to the host (start ppq " + juce::String(ppq0, 2) + "): " + juce::String((int) errs.size()) + " edges over 8 bars, max offset " + juce::String(mx, 1)
              + " samples (8-sample control step + edge detector), drift " + juce::String(last - first, 2) + " samples");
    }
}

static void testSubSolo() {
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    const auto full = measure(renderHost(p, 44100, 140, lineEvents(140), 3.0), 44100);
    PoltergeistProcessor q; q.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); setP(q, "sub_solo", 1);
    auto s = renderHost(q, 44100, 140, lineEvents(140), 3.0); const auto solo = measure(s, 44100);
    const int n = (int) s.size() / 2; float lr = 0; for (int i = 0; i < n; ++i) lr = juce::jmax(lr, std::abs(s[(size_t) i] - s[(size_t) (n + i)]));
    check(solo.hiFrac < 1e-3 && full.hiFrac > 0.05 && lr < 1e-6f && solo.rmsDb > -40,
          "SUB SOLO isolates the sub: energy above 300 Hz " + juce::String(10 * std::log10(solo.hiFrac + 1e-20), 1) + " dB (full sound " + juce::String(10 * std::log10(full.hiFrac), 1)
          + " dB), L = R, sub level " + juce::String(solo.rmsDb, 1) + " dB RMS");
    // CLEAN SUB bypasses the filters and drive: with the sub alone (ghosts off) the output is the same whatever the filter does
    auto subOnly = [&](float cut) { PoltergeistProcessor r; r.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Pure Sub Spirit")); setP(r, "f1_on", 1); setP(r, "f1_type", 0); setP(r, "f1_cut", cut); setP(r, "dist_on", 1); return renderHost(r, 44100, 140, lineEvents(140), 2.0); };
    const double d = diffDb(subOnly(40.0f), subOnly(18000.0f));
    check(d < -90, "CLEAN SUB skips filters and DIST: " + juce::String(d, 1) + " dB difference between filter shut / open");
}

static void testSummon() {
    const char* words[] = { "growl", "screech", "riddim", "wobble", "reese", "neuro", "rolling", "clean bass", "deep sub", "808", "yoi", "talking", "dubstep" };
    juce::String got; int ok = 0;
    for (auto* w : words) { juce::String cat; auto p = gs::BassSummon::summon(w, 7, &cat); if (!p.empty()) ++ok; got << w << " -> " << cat.upToFirstOccurrenceOf(" (", false, false) << "; "; }
    juce::String c2; const bool legacyBell = gs::BassSummon::summon("dark bell", 3, &c2).empty();
    check(ok == 13 && legacyBell, "SUMMON knows the bass words (" + juce::String(ok) + "/13): " + got.dropLastCharacters(2) + "; 'dark bell' stays with the original SUMMON");
    // two summons of the same word differ, the same seed repeats
    auto a = gs::BassSummon::summon("growl", 1, nullptr), b = gs::BassSummon::summon("growl", 2, nullptr), a2 = gs::BassSummon::summon("growl", 1, nullptr);
    check(a == a2 && a != b, "SUMMON growl: same seed = same sound, new seed = a new take");
}

// ================= FINAL GATE review round
// render a patch through the processor (so the engine revision, ramps and the master stage are the real ones)
static Stereo renderP(PoltergeistProcessor& p, double sr, const std::vector<Ev>& evs, double secs, int block = 512, std::function<void(int, PoltergeistProcessor&)> perBlock = {}, int prepBlock = -1) {
    TestHead head; head.sr = sr; p.setPlayHead(&head);
    p.setRateAndBufferSizeDetails(sr, prepBlock > 0 ? prepBlock : block); p.prepareToPlay(sr, prepBlock > 0 ? prepBlock : block);
    const int n = (int) (secs * sr); Stereo out((size_t) n * 2, 0.0f);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; midi.ensureSize(4096); int bi = 0;
    for (int pos = 0; pos < n; pos += block, ++bi) {
        midi.clear(); head.pos = pos;
        for (auto& e : evs) { const int at = (int) std::llround(e.t * sr) - pos; if (at >= 0 && at < block) midi.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, e.vel) : juce::MidiMessage::noteOff(1, e.note), at); }
        if (perBlock) perBlock(bi, p);
        buf.clear(); p.processBlock(buf, midi);
        for (int i = 0; i < block && pos + i < n; ++i) { out[(size_t) (pos + i)] = buf.getSample(0, i); out[(size_t) (n + pos + i)] = buf.getSample(1, i); }
    }
    p.setPlayHead(nullptr);
    return out;
}
static void applyP(PoltergeistProcessor& p, const gs::Patch& patch) { p.applyPatch(patch); }
static double dbv(double x) { return 20 * std::log10(x + 1e-15); }
// inharmonic / harmonic energy (20 Hz - 20 kHz) of a steady tone (2^16 FFT, Blackman-Harris; +-6 bins round each harmonic = harmonic)
static double inharmonicDb(const Stereo& s, int from, double f0, double sr) {
    const int order = 16, N = 1 << order; juce::dsp::FFT fft(order);
    std::vector<float> buf((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) { const double t = (double) i / N; const double w = 0.35875 - 0.48829 * std::cos(2 * juce::MathConstants<double>::pi * t) + 0.14128 * std::cos(4 * juce::MathConstants<double>::pi * t) - 0.01168 * std::cos(6 * juce::MathConstants<double>::pi * t); buf[(size_t) i] = s[(size_t) (from + i)] * (float) w; }
    fft.performFrequencyOnlyForwardTransform(buf.data(), true);
    const double bw = sr / N; double h = 0, nh = 0;
    for (int k = (int) (20 / bw); k < N / 2 && k * bw < 20000; ++k) { const double f = k * bw, r = f / f0, near = std::abs(r - std::round(r)) * f0 / bw; const double e = (double) buf[(size_t) k] * buf[(size_t) k]; if (near <= 6) h += e; else nh += e; }
    return 10 * std::log10((nh + 1e-30) / (h + 1e-30));
}
static gs::Patch revPatch() { auto p = basicPatch(); p["mono_below"] = 0; return p; }   // base() = the current engine revision

static void testBigBlock() {
    // a host that prepared 256 but sends 1024: played in pieces, the same as four 256 blocks
    auto ev = lineEvents(140);
    PoltergeistProcessor a; a.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    PoltergeistProcessor b; b.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    const auto sa = renderP(a, 44100, ev, 2.0, 1024, {}, 256), sb = renderP(b, 44100, ev, 2.0, 256);
    const auto m = measure(sa, 44100);
    check(m.finite && m.rmsDb > -40 && diffDb(sa, sb) < -90, "blocks bigger than prepared (prepare 256, play 1024): finite, same as 256-sample blocks (" + juce::String(diffDb(sa, sb), 1) + " dB)");
}
static void testMasterClean() {
    // the shipped clean sub: harmonic distortion of the 32.7 Hz sub (the original engine's always-on clip gave ~2 %)
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Pure Sub Spirit")); setP(p, "mono_below", 0);
    const auto s = renderP(p, 44100, { { 0.0, 36, 0.9f }, { 2.9, 36, 0 } }, 3.0);
    const int n = (int) s.size() / 2, a = n / 3, b = n - 4410; const double f0 = 32.703, h1 = magAt(s.data(), a, b, f0, 44100);
    double hd = 0; for (int k = 2; k <= 9; ++k) { const double hk = magAt(s.data(), a, b, k * f0, 44100); hd += hk * hk; }
    const double thd = std::sqrt(hd) / h1;
    // a hot signal still never passes 1.2 (the safety knee)
    auto hot = revPatch(); hot[gs::opId(0, "wtype")] = 3; hot["master"] = 6; hot[gs::opId(0, "level")] = 1;
    PoltergeistProcessor q; applyP(q, hot); double pk = 0; for (float x : renderP(q, 44100, { { 0.0, 36, 1.0f }, { 0.9, 36, 0 } }, 1.0)) pk = juce::jmax(pk, (double) std::abs(x));
    check(thd < 0.001 && pk <= 1.2001, "clean master: 'Pure Sub Spirit' THD " + juce::String(100 * thd, 3) + " % (was ~1.9 %), a +6 dB saw peaks at " + juce::String(pk, 3) + " (limit 1.2)");
}
static void testZipper() {
    auto run = [&](const char* id, float from, float to, int blocks, const char* preset) {
        PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex(preset)); setP(p, "mono_below", 0);
        const int b0 = 40;
        auto s = renderP(p, 44100, { { 0.0, 36, 0.9f }, { 3.9, 36, 0 } }, 4.0, 512, [&](int bi, PoltergeistProcessor& q) { if (bi >= b0 && bi < b0 + blocks) setP(q, id, from + (to - from) * (float) (bi - b0) / (float) blocks); });
        const int n = (int) s.size() / 2; double js = 0, jq = 0;
        for (int i = b0 * 512 + 1; i < (b0 + blocks) * 512; ++i) js = juce::jmax(js, (double) std::abs(s[(size_t) i] - s[(size_t) i - 1]));
        for (int i = (b0 + blocks + 10) * 512; i < n - 8820; ++i) jq = juce::jmax(jq, (double) std::abs(s[(size_t) i] - s[(size_t) i - 1]));
        return js / (jq + 1e-12);
    };
    const double m = run("master", -24, 0, 86, "Pure Sub Spirit"), su = run("sub_level", 0.2f, 1.0f, 43, "Pure Sub Spirit"), g = run("op1_level", 0.1f, 0.9f, 43, "Clean Ghost Bass");
    check(m < 1.5 && su < 1.5 && g < 1.5, "no zipper: biggest sample step while sweeping vs steady: MASTER x" + juce::String(m, 2) + ", SUB LEVEL x" + juce::String(su, 2) + ", GHOST OUT x" + juce::String(g, 2) + " (was x5 / x80 / x12)");
}
static void testDelayRetrigger() {
    auto p = revPatch(); p[gs::opId(0, "dly")] = 0.3f; p[gs::opId(0, "r")] = 2.0f; p[gs::opId(0, "wtype")] = 1; p["master"] = 0;
    PoltergeistProcessor q; applyP(q, p);
    auto s = renderP(q, 44100, { { 0.0, 48, 0.9f }, { 0.5, 48, 0 }, { 0.6, 48, 0.9f }, { 1.5, 48, 0 } }, 1.8);
    auto rms = [&](double t0, double t1) { double e = 0; const int a = (int) (t0 * 44100), b = (int) (t1 * 44100); for (int i = a; i < b; ++i) e += s[(size_t) i] * s[(size_t) i]; return std::sqrt(e / (b - a)); };
    // (measured above 30 Hz: the DC blocker's sub-audio settling after the 4 ms fade is not sound)
    { std::vector<Bq> hp { Bq::hp(30, 44100), Bq::hp(30, 44100) }; for (auto& x : s) { double y = x; for (auto& q2 : hp) y = q2.tick(y); x = (float) y; } }
    const double sus = dbv(rms(0.3, 0.45)), del = dbv(rms(0.65, 0.85)), after = dbv(rms(1.0, 1.2));
    if (std::getenv("DLYDBG")) for (double t = 0.58; t < 0.95; t += 0.01) std::printf("  %.2f %.1f\n", t, dbv(rms(t, t + 0.01)));
    check(del < sus - 60 && std::abs(after - sus) < 1.0, "DELAY after a retrigger is silent: " + juce::String(del, 1) + " dB (sustain " + juce::String(sus, 1) + " dB, was -17.8 dB), then " + juce::String(after, 1) + " dB");
}
static void testStealFade() {
    auto p = revPatch(); p["mono"] = 0; p["voices"] = 2; p[gs::opId(0, "wtype")] = 1; p[gs::opId(0, "r")] = 1.0f; p["master"] = 0;
    PoltergeistProcessor q; applyP(q, p);
    auto s = renderP(q, 44100, { { 0.0, 48, 0.9f }, { 0.0, 52, 0.9f }, { 1.0, 55, 0.9f }, { 2.0, 48, 0 }, { 2.0, 52, 0 }, { 2.0, 55, 0 } }, 2.5);
    const int at = 44100; double before = 0, atSteal = 0;
    for (int i = 20000; i < 44000; ++i) before = juce::jmax(before, (double) std::abs(s[(size_t) i] - s[(size_t) i - 1]));
    for (int i = at - 64; i < at + 400; ++i) atSteal = juce::jmax(atSteal, (double) std::abs(s[(size_t) i] - s[(size_t) i - 1]));
    check(atSteal < before * 1.5, "voice steal without a click: biggest step at the steal x" + juce::String(atSteal / before, 2) + " of the steady signal's (was x14)");
}
static void testSustainPedal() {
    PoltergeistProcessor q; q.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Clean Ghost Bass")); setP(q, "mono", 0);
    TestHead head; q.setPlayHead(&head); q.setRateAndBufferSizeDetails(44100, 512); q.prepareToPlay(44100, 512);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0); midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 1); midi.addEvent(juce::MidiMessage::noteOff(1, 48), 400);
    double held = 0, after = 0;
    for (int b = 0; b < 160; ++b) {
        if (b == 100) midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        buf.clear(); q.processBlock(buf, midi); midi.clear();
        if (b > 60 && b < 90) held = juce::jmax(held, (double) buf.getMagnitude(0, 512));
        if (b > 140) after = juce::jmax(after, (double) buf.getMagnitude(0, 512));
    }
    q.setPlayHead(nullptr);
    check(dbv(held) > -30 && dbv(after) < -80, "sustain pedal: note held " + juce::String(dbv(held), 1) + " dBFS 0.7 s after its note-off, gone after pedal up (" + juce::String(dbv(after), 1) + " dBFS)");
}
static void testWobblePage() {
    // the WOBBLE page alone (no matrix slot): TARGET + DEPTH make the shape audible; its playhead runs while the page is open
    PoltergeistProcessor q; q.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Clean Ghost Bass"));
    for (int m = 1; m <= gs::kModSlots; ++m) setP(q, ("mm" + juce::String(m) + "_src").toRawUTF8(), 0);
    setP(q, "wob_n", 4); setP(q, "wob_smooth", 0); setP(q, "wob_dst", (float) gs::mdVolume); setP(q, "wob_amt", 1.0f);   // a gate: on for half a cycle
    { const float X[4] = { 0, 0.4995f, 0.5f, 0.9995f }, Y[4] = { 1, 1, 0, 0 }; for (int i = 0; i < 4; ++i) { setP(q, ("wob_x" + juce::String(i + 1)).toRawUTF8(), X[i]); setP(q, ("wob_y" + juce::String(i + 1)).toRawUTF8(), Y[i]); } }
    setP(q, "ott_amt", 0);   // (OTT's upward compression would lift the quiet half: measure the wobble itself)
    q.engine.wobView.store(true);
    auto s = renderP(q, 44100, { { 0.0, 36, 0.9f }, { 1.9, 36, 0 } }, 2.0);
    double mn = 9, mx = 0; const int w = 1102;
    for (int i = 22050; i + w < 80000; i += w) { double pk = 0; for (int k = i; k < i + w; ++k) pk = juce::jmax(pk, (double) std::abs(s[(size_t) k])); mn = juce::jmin(mn, pk); mx = juce::jmax(mx, pk); }
    const float ui = q.engine.wobUi.load(); q.engine.wobView.store(false);
    check(mx > 0.05 && mn < mx * 0.1 && ui >= 0.0f, "WOBBLE page TARGET = Volume, DEPTH 100%: 25 ms peaks swing " + juce::String(dbv(mn / mx), 1) + " dB; playhead at " + juce::String(ui, 2));
}
static void testFilterDrive() {
    double worst = 0; juce::String txt;
    for (int type : { 0, 1, 2, 9 }) {
        double r[2];
        for (int k = 0; k < 2; ++k) {
            auto p = revPatch(); p[gs::opId(0, "wtype")] = 3; p["f1_on"] = 1; p["f1_type"] = (float) type; p["f1_cut"] = 800; p["f1_res"] = 0.3f; p["f1_drive"] = k ? 0.01f : 0.0f;
            PoltergeistProcessor q; applyP(q, p); auto s = renderP(q, 44100, { { 0.0, 36, 0.9f }, { 1.4, 36, 0 } }, 1.5);
            const int n = (int) s.size() / 2; double e = 0; for (int i = n / 3; i < n - 8820; ++i) e += s[(size_t) i] * s[(size_t) i]; r[k] = std::sqrt(e / (n - 8820 - n / 3));
        }
        const double d = dbv(r[1]) - dbv(r[0]); worst = juce::jmax(worst, std::abs(d)); txt << gs::kFltTypes[type] << " " << juce::String(d, 2) << ", ";
    }
    check(worst < 0.3, "FILTER DRIVE 0 -> 0.01 changes the level by under 0.3 dB (" + txt.dropLastCharacters(2) + " dB; was +2 to +3.6)");
}
static void testAliasing() {
    auto run = [&](std::function<void(gs::Patch&)> set, int note, int quality) {
        auto p = revPatch(); set(p); PoltergeistProcessor q; applyP(q, p); setP(q, "quality", (float) quality);
        auto s = renderP(q, 44100, { { 0.0, note, 0.9f }, { 2.9, note, 0 } }, 3.0);
        return inharmonicDb(s, 22050, 440.0 * std::exp2((note - 69) / 12.0), 44100);
    };
    auto bend = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; p[gs::opId(0, "wave")] = 3; };
    auto fm = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; p[gs::opId(1, "wtype")] = 1; p[gs::opId(1, "s")] = 1; p[gs::opId(1, "d")] = 1; p[gs::modId(1, 0)] = 0.8f; p["macro_haunt"] = 0.8f; };
    auto sync = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; p["sync_amt"] = 0.6f; };
    const double b2 = run(bend, 36, 0), b4 = run(bend, 60, 0), saw = run([](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; }, 36, 0);
    check(b2 < -55 && b4 < -50, "WAVE / BEND 1.0 on a saw table, inharmonic energy: C2 " + juce::String(b2, 1) + " dB, C4 " + juce::String(b4, 1) + " dB (was -36 / -30; plain saw " + juce::String(saw, 1) + " dB)");
    if (std::getenv("ALIASDBG")) {
        auto fmSine = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 1; p[gs::opId(1, "wtype")] = 1; p[gs::opId(1, "s")] = 1; p[gs::opId(1, "d")] = 1; p[gs::modId(1, 0)] = 0.8f; p["macro_haunt"] = 0.8f; };
        auto fmMorph = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 0; p[gs::opId(0, "wave")] = 2; p[gs::opId(1, "s")] = 1; p[gs::opId(1, "d")] = 1; p[gs::modId(1, 0)] = 0.8f; p["macro_haunt"] = 0.8f; };
        for (int q = 0; q < 3; ++q) std::printf("q%d: fm saw table %.1f, fm sine table %.1f, fm morph saw %.1f, saw %.1f\n", q, run(fm, 36, q), run(fmSine, 36, q), run(fmMorph, 36, q), run([](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; }, 36, q));
    }
    const double f2 = run(fm, 36, 0), f8 = run(fm, 36, 2), s2 = run(sync, 48, 0), s8 = run(sync, 48, 2);
    check(f2 < -60 && f8 < -60 && s2 < -60 && s8 < s2 - 6, "FM on a saw table (HAUNT 0.8) inharmonic " + juce::String(f2, 1) + " dB at 2x (was -26.6), " + juce::String(f8, 1) + " at 8x; SYNC 0.6 " + juce::String(s2, 1) + " dB (2x, was -24.9) / " + juce::String(s8, 1) + " dB (8x)");
    auto fold = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; p["dist_on"] = 1; p["dist_type"] = 3; p["dist_drive"] = 0.6f; };
    auto poss = [](gs::Patch& p) { p[gs::opId(0, "wtype")] = 3; p["macro_possess"] = 0.9f; };
    const double d2 = run(fold, 36, 0), d4 = run(fold, 36, 1), d8 = run(fold, 36, 2), p2 = run(poss, 36, 0), p8 = run(poss, 36, 2);
    check(d4 < d2 - 6 && d8 < d4 - 3 && p8 < p2 - 10, "QUALITY on drive: DIST fold 0.6 inharmonic " + juce::String(d2, 1) + " / " + juce::String(d4, 1) + " / " + juce::String(d8, 1) + " dB (2x / 4x / 8x); POSSESS 0.9 " + juce::String(p2, 1) + " -> " + juce::String(p8, 1) + " dB (8x)");
}
static void testQualityLevels() {
    // the same preset at 2x / 4x / 8x: same loudness, finite; and 96 kHz caps at 4x
    double r[3] {}; bool fin = true;
    for (int qi = 0; qi < 3; ++qi) { PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); setP(p, "quality", (float) qi); const auto m = measure(renderP(p, 44100, lineEvents(140), 2.0), 44100); r[qi] = m.rmsDb; fin = fin && m.finite; }
    check(fin && std::abs(r[1] - r[0]) < 1.0 && std::abs(r[2] - r[0]) < 1.0 && gs::Engine::effectiveFactor(96000, 8) == 4 && gs::Engine::effectiveFactor(192000, 8) == 2,
          "QUALITY 2x / 4x / 8x: RMS " + juce::String(r[0], 2) + " / " + juce::String(r[1], 2) + " / " + juce::String(r[2], 2) + " dB; 96 kHz runs 8x as 4x, 192 kHz as 2x");
}
static void testNanGuard() {
    // a corrupt sample feeds NaN: the block is dropped (silence, never NaN), and once it's gone the next note plays normally
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Clean Ghost Bass"));
    auto bad = std::make_shared<gs::SampleData>(); bad->data.assign(44100, std::numeric_limits<float>::quiet_NaN()); bad->rate = 44100; bad->buildFrames();
    p.engine.setSample(bad); setP(p, "smp_level", 0.9f);
    auto s1 = renderP(p, 44100, { { 0.0, 48, 0.9f }, { 0.4, 48, 0 } }, 0.6);
    bool fin1 = true; for (float x : s1) fin1 = fin1 && std::isfinite(x);
    p.engine.setSample(nullptr); setP(p, "smp_level", 0.0f);
    TestHead head; p.setPlayHead(&head);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
    double pk = 0; bool fin2 = true;
    for (int b = 0; b < 40; ++b) { buf.clear(); p.processBlock(buf, midi); midi.clear(); for (int i = 0; i < 512; ++i) fin2 = fin2 && std::isfinite(buf.getSample(0, i)); if (b > 10) pk = juce::jmax(pk, (double) buf.getMagnitude(0, 512)); }
    p.setPlayHead(nullptr);
    check(fin1 && fin2 && dbv(pk) > -30, "NaN guard: a NaN sample gives silence (never NaN), the next note plays at " + juce::String(dbv(pk), 1) + " dBFS");
}
static void testLevelModFm() {
    // LFO -> 'Ghost 2 Level' where ghost 2 is a silent modulator: the FM depth now follows it (the timbre moves)
    auto p = revPatch(); p[gs::opId(0, "wtype")] = 1; p[gs::opId(1, "wtype")] = 1; p[gs::opId(1, "level")] = 0.0f; p[gs::opId(1, "s")] = 1; p[gs::opId(1, "d")] = 1; p[gs::modId(1, 0)] = 0.7f;
    p["mm1_src"] = 2; p["mm1_dst"] = (float) gs::mdLvl2; p["mm1_amt"] = -1.0f; p["lfo1_rate"] = 2; p["lfo1_shape"] = 4;
    PoltergeistProcessor q; applyP(q, p); auto s = renderP(q, 44100, { { 0.0, 48, 0.9f }, { 1.9, 48, 0 } }, 2.0);
    // high-frequency share (above 600 Hz) in 50 ms windows: moves when the FM depth moves
    double lo = 9, hi = 0; const int w = 2205;
    const double f0 = 130.81;   // C3; a ratio-2 modulator puts sidebands on the odd harmonics
    for (int i = 22050; i + w < 80000; i += w) { const double h = magAt(s.data(), i, i + w, f0 * 3, 44100) + magAt(s.data(), i, i + w, f0 * 5, 44100), f = magAt(s.data(), i, i + w, f0, 44100) + 1e-9; lo = juce::jmin(lo, h / f); hi = juce::jmax(hi, h / f); }
    check(dbv(hi) - dbv(lo) > 12, "a level mod on a silent modulator moves its FM depth: overtone ratio swings " + juce::String(dbv(hi) - dbv(lo), 1) + " dB");
}
static void testSubFollows() {
    // a short ghost pluck over a long sub: SUB FOLLOWS = OWN keeps the sub for its release
    auto make = [](int mode) { auto p = revPatch(); p[gs::opId(0, "d")] = 0.08f; p[gs::opId(0, "s")] = 0.0f; p["sub_on"] = 1; p["sub_level"] = 0.8f; p["sub_env"] = (float) mode; p["sub_a"] = 0.002f; p["sub_r"] = 0.2f; return p; };
    double late[2];
    for (int k = 0; k < 2; ++k) {
        PoltergeistProcessor q; applyP(q, make(k == 0 ? 0 : 2)); setP(q, "sub_solo", 1);
        auto s = renderP(q, 44100, { { 0.0, 36, 0.9f }, { 0.8, 36, 0 } }, 1.0);
        double e = 0; for (int i = (int) (0.5 * 44100); i < (int) (0.7 * 44100); ++i) e += s[(size_t) i] * s[(size_t) i]; late[k] = dbv(std::sqrt(e / (0.2 * 44100)));
    }
    check(late[1] > -30 && late[0] < late[1] - 30, "SUB FOLLOWS: 0.5 s into a short pluck the sub is " + juce::String(late[0], 1) + " dB following ghost 1, " + juce::String(late[1], 1) + " dB on its own envelope");
}
static void testMonoRetrig() {
    // tied notes in mono: RETRIG restarts the envelope (a decaying pluck re-attacks on every note)
    auto make = [](bool rt) { auto p = revPatch(); p[gs::opId(0, "d")] = 0.15f; p[gs::opId(0, "s")] = 0.0f; p["mono_retrig"] = rt ? 1.0f : 0.0f; return p; };
    double second[2];
    for (int k = 0; k < 2; ++k) {
        PoltergeistProcessor q; applyP(q, make(k == 1));
        auto s = renderP(q, 44100, { { 0.0, 36, 0.9f }, { 0.5, 39, 0.9f }, { 0.6, 36, 0 }, { 1.0, 39, 0 } }, 1.2);
        double pk = 0; for (int i = (int) (0.5 * 44100); i < (int) (0.6 * 44100); ++i) pk = juce::jmax(pk, (double) std::abs(s[(size_t) i])); second[k] = dbv(pk);
    }
    check(second[1] > second[0] + 20, "mono RETRIG: a tied second note re-attacks (" + juce::String(second[1], 1) + " dBFS vs legato " + juce::String(second[0], 1) + " dBFS)");
}
static void testUnits() {
    PoltergeistProcessor p; auto txt = [&](const char* id, float v) { auto* r = p.apvts.getParameter(id); return r->getText(r->convertTo0to1(v), 0); };
    auto back = [&](const char* id, const char* t) { auto* r = p.apvts.getParameter(id); return r->convertFrom0to1(r->getValueForText(t)); };
    const juce::String a = txt("f1_cut", 1250), b = txt("op1_a", 0.012f), c = txt("master", -6), d = txt("op1_level", 0.45f), e = txt("mm1_amt", -0.42f);
    const bool ok = a == "1.25 kHz" && b == "12.0 ms" && c == "-6.0 dB" && d == "45 %" && e == "-42 %" && std::abs(back("f1_cut", "2.5 kHz") - 2500) < 1 && std::abs(back("op1_a", "250 ms") - 0.25f) < 1e-4f;
    check(ok, "knob read-outs with units: " + a + ", " + b + ", " + c + ", " + d + ", " + e + "; typed '2.5 kHz' / '250 ms' read back");
}

static void testCpuWorst() {
    // the heaviest case: 6 ghosts + noise, 7-voice unison, both filters, every effect, 8-note chord
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    for (int o = 0; o < 6; ++o) setP(p, gs::opId(o, "level").toRawUTF8(), 0.3f);
    setP(p, "nz_level", 0.2f); setP(p, "macro_spectre", 1.0f); setP(p, "mono", 0); setP(p, "voices", 8);
    std::vector<Ev> ev; for (int k = 0; k < 8; ++k) { ev.push_back({ 0.0, 36 + k * 3, 0.9f }); ev.push_back({ 2.9, 36 + k * 3, 0 }); }
    double cpu = 0; renderHost(p, 44100, 140, ev, 3.0, &cpu);
    std::printf("CPU worst case: 8 voices x 7 unison x 6 ghosts + noise, both filters, DIST + MULTIBAND + SQUEEZE: %.1f%% of one core (%.2f%% per voice)\n", cpu / 3.0 * 100, cpu / 3.0 * 100 / 8);
#if defined(__SANITIZE_ADDRESS__)
    std::printf("(sanitizer build: the real-time limit is only checked in Release)\n");
    if (false)
#endif
    check(cpu / 3.0 < 0.9, "worst-case patch runs in real time (" + juce::String(cpu / 3.0 * 100, 1) + "% of one core)");
}

static void testEnvLoop() {
    // DAHDSR: delay holds the note silent, hold keeps it at full level, loop retriggers while held
    auto p = basicPatch(); p[gs::opId(0, "dly")] = 0.1f; p[gs::opId(0, "a")] = 0.01f; p[gs::opId(0, "hold")] = 0.1f; p[gs::opId(0, "d")] = 0.08f; p[gs::opId(0, "s")] = 0.1f; p[gs::opId(0, "loop")] = 1;
    auto s = renderPatch(p, 48, 1.0); const double sr = 44100;
    auto rmsAt = [&](double t0, double t1) { double e = 0; int a = (int) (t0 * sr), b = (int) (t1 * sr); for (int i = a; i < b; ++i) e += s[(size_t) i] * s[(size_t) i]; return std::sqrt(e / (b - a)); };
    const double silent = rmsAt(0.0, 0.09), held = rmsAt(0.12, 0.2);
    int peaks = 0; double prev = 0; bool up = false;   // count envelope cycles (loop) in 0.25..0.95 s
    for (double t = 0.25; t < 0.95; t += 0.005) { const double r = rmsAt(t, t + 0.005); if (r > prev * 1.05) up = true; else if (up && r < prev * 0.95) { ++peaks; up = false; } prev = r; }
    check(silent < held * 0.01 && peaks >= 2, "DAHDSR + loop: delay " + juce::String(20 * std::log10(silent / held + 1e-9), 1) + " dB, hold at full level, " + juce::String(peaks) + " loop cycles while held");
}

// ---- screenshots
static void bassShots(const juce::File& dir) {
    dir.createDirectory();
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    setP(p, "nz_level", 0.1f); setP(p, gs::modId(4, 0).toRawUTF8(), 0.3f); setP(p, gs::modId(6, 1).toRawUTF8(), 0.2f);
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor()));
    ed->setSize(1040, 744);
    auto snap = [&](const char* name) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png; juce::File f = dir.getChildFile(name); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os);
        std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
    };
    ed->selectGhost(0); ed->testShow(0, 0, 0); snap("bass_ghosts.png");
    ed->selectGhost(6); snap("bass_noise_ghost.png");
    ed->testShow(7, 0, 0); snap("bass_filters_sub_fx.png");
    ed->testShow(8, 0, 0); snap("bass_wobble.png");
    ed->testShow(0, 0, 0); p.bypassParam->setValueNotifyingHost(1.0f); snap("bypass_on_dark.png");
    ed->testBypassDark(false); snap("bypass_on_light_button.png"); p.bypassParam->setValueNotifyingHost(0.0f); snap("bypass_off_light_button.png"); ed->testBypassDark(true);
    {   // the FM page's ADD SAMPLE panel with a sample
        auto smp = std::make_shared<gs::SampleData>(); smp->rate = 44100; smp->name = "Ghost Choir Stab"; smp->data.resize(66150);
        for (int i = 0; i < 66150; ++i) smp->data[(size_t) i] = 0.5f * (float) std::exp(-i / 20000.0) * (float) (std::sin(i * 0.0627) + 0.6 * std::sin(i * 0.0791) + 0.4 * std::sin(i * 0.0940));
        smp->buildFrames(); p.addSample(smp); ed->sampleAdded(); snap("add_sample_panel.png");
    }
}

// ---- demos: 8 bars each with a simple synthesized beat under the bass
static void addDrums(Stereo& s, double sr, double bpm, int bars, bool dnb) {
    const int n = (int) s.size() / 2; const double beat = 60.0 / bpm;
    auto kick = [&](double t) { const int a = (int) (t * sr); double ph = 0; for (int i = 0; i < (int) (0.35 * sr) && a + i < n; ++i) { const double tt = i / sr; ph += (45 + 120 * std::exp(-tt * 30)) / sr; const float x = (float) (std::sin(2 * juce::MathConstants<double>::pi * ph) * std::exp(-tt * 9) * 0.55); s[(size_t) (a + i)] += x; s[(size_t) (n + a + i)] += x; } };
    juce::Random r(3);
    auto snare = [&](double t) { const int a = (int) (t * sr); for (int i = 0; i < (int) (0.22 * sr) && a + i < n; ++i) { const double tt = i / sr; const float x = (float) ((r.nextFloat() * 2 - 1) * std::exp(-tt * 18) * 0.32 + std::sin(2 * juce::MathConstants<double>::pi * 190 * tt) * std::exp(-tt * 30) * 0.2); s[(size_t) (a + i)] += x; s[(size_t) (n + a + i)] += x; } };
    auto hat = [&](double t) { const int a = (int) (t * sr); float hp = 0, prev = 0; for (int i = 0; i < (int) (0.05 * sr) && a + i < n; ++i) { const float w = r.nextFloat() * 2 - 1; hp = 0.6f * (hp + w - prev); prev = w; const float x = hp * (float) std::exp(-i / sr * 70) * 0.12f; s[(size_t) (a + i)] += x * 0.8f; s[(size_t) (n + a + i)] += x; } };
    for (int b = 0; b < bars; ++b) {
        const double t0 = b * 4 * beat;
        if (dnb) { kick(t0); kick(t0 + 2.5 * beat); snare(t0 + beat); snare(t0 + 3 * beat); }
        else { kick(t0); snare(t0 + 2 * beat); if (b % 2) kick(t0 + 3.5 * beat); }
        for (int h = 0; h < 8; ++h) hat(t0 + h * beat / 2 + (dnb ? 0 : beat / 4));
    }
}
static void writeWav(const juce::File& f, const Stereo& s, double sr) {
    const int n = (int) s.size() / 2; juce::AudioBuffer<float> b(2, n);
    float pk = 1e-6f; for (float x : s) pk = juce::jmax(pk, std::abs(x));
    const float g = pk > 0.97f ? 0.97f / pk : 1.0f;
    for (int i = 0; i < n; ++i) { b.setSample(0, i, s[(size_t) i] * g); b.setSample(1, i, s[(size_t) (n + i)] * g); }
    f.deleteFile(); juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, n); }
    std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
}
static void bassDemos(const juce::File& dir) {
    dir.createDirectory(); const double sr = 44100;
    struct Part { const char* file; std::vector<std::pair<const char*, int>> presets; double bpm; bool dnb; std::vector<Step> line; };
    // dubstep: half-time feel, long held growl notes switching sound every 2 bars
    const std::vector<Step> dub { { 0, 6, 36 }, { 6, 2, 36 }, { 8, 4, 39 }, { 12, 4, 34 }, { 16, 3, 36 }, { 20, 2, 41 }, { 22, 2, 39 }, { 24, 8, 36 } };
    const std::vector<Step> rid { { 0, 2, 36 }, { 3, 2, 36 }, { 6, 2, 36 }, { 8, 2, 36 }, { 11, 2, 36 }, { 14, 2, 39 }, { 16, 2, 36 }, { 19, 2, 36 }, { 22, 2, 36 }, { 24, 4, 34 }, { 28, 4, 35 } };
    const std::vector<Step> roll { { 0, 4, 36 }, { 4, 2, 36 }, { 6, 2, 43 }, { 8, 4, 36 }, { 12, 2, 39 }, { 14, 2, 41 }, { 16, 4, 34 }, { 20, 2, 34 }, { 22, 2, 41 }, { 24, 4, 36 }, { 28, 4, 39 } };
    const std::vector<Step> prog { { 2, 2, 36 }, { 6, 2, 36 }, { 10, 2, 36 }, { 14, 2, 36 }, { 18, 2, 32 }, { 22, 2, 32 }, { 26, 2, 34 }, { 30, 2, 34 } };
    const std::vector<Part> parts {
        { "demo_dubstep_growl.wav", { { "Grave Growler", 0 }, { "Yoi Phantom", 0 }, { "Monster Mouth", 0 }, { "Talking Tomb", 0 } }, 140, false, dub },
        { "demo_riddim.wav", { { "Riddim Revenant", 0 }, { "Reaper Bounce", 0 }, { "Chain Rattle", 0 }, { "Riddim Revenant", 0 } }, 140, false, rid },
        { "demo_reese_neuro_roller.wav", { { "Reese Revenant", 0 }, { "Neuro Necro", 0 }, { "Phase Phantom", 0 }, { "Neuro Gnasher", 0 } }, 172, true, roll },
        { "demo_clean_progressive_rolling.wav", { { "Rolling Phantom", 0 }, { "Night Drive", 0 }, { "Progressive Pulse", 0 }, { "Deep Rolling Spirit", 0 } }, 124, false, prog },
    };
    for (auto& part : parts) {
        const int barsEach = 2; const double barLen = 60.0 / part.bpm * 4;
        Stereo all; const int total = (int) (barLen * 8 * sr + sr * 0.5); all.assign((size_t) total * 2, 0.0f);
        for (size_t k = 0; k < part.presets.size(); ++k) {
            PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex(part.presets[k].first));
            auto ev = lineEvents(part.bpm, barsEach, 0, &part.line);
            auto s = renderHost(p, sr, part.bpm, ev, barLen * barsEach + 0.3, nullptr, nullptr, k * barsEach * 4.0);
            const int off = (int) (k * barsEach * barLen * sr), n = (int) s.size() / 2;
            for (int i = 0; i < n && off + i < total; ++i) { all[(size_t) (off + i)] += s[(size_t) i]; all[(size_t) (total + off + i)] += s[(size_t) (n + i)]; }
        }
        addDrums(all, sr, part.bpm, 8, part.dnb);
        writeWav(dir.getChildFile(part.file), all, sr);
    }
}

// ---- LEADS: one short melodic phrase per lead (A minor, 128 BPM, 4 bars), and the folder on screen
static void leadDemos(const juce::File& dir) {
    dir.createDirectory(); const double sr = 44100, bpm = 128;
    // 16th steps: { at, len, note }
    const auto& phrase = leadPhrase();
    const auto& v = gs::bassPresets();
    for (int i = 0; i < (int) v.size(); ++i) {
        if (juce::String(v[(size_t) i].folder) != "LEADS") continue;
        PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + i);
        const double st = 60.0 / bpm / 4.0; std::vector<Ev> e;
        for (auto& x : phrase) { e.push_back({ x.at * st, x.note, 0.85f }); e.push_back({ (x.at + x.len) * st - 0.01, x.note, 0 }); }
        std::stable_sort(e.begin(), e.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
        auto s = renderHost(p, sr, bpm, e, 64 * st + 2.5);
        writeWav(dir.getChildFile("lead_" + juce::String(v[(size_t) i].name).toLowerCase().replaceCharacter(' ', '_') + ".wav"), s, sr);
    }
}
static void leadsShot(const juce::File& file) {
    // the editor with a LEADS preset loaded, and the preset list's LEADS folder drawn over it with the plugin's own menu look
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Metal Banshee"));
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor()));
    ed->setSize(1040, 744); ed->testShow(7, 0, 0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::Graphics g(img); auto& lf = ed->getLookAndFeel();
    const auto& v = gs::bassPresets(); std::vector<int> leads; for (int i = 0; i < (int) v.size(); ++i) if (juce::String(v[(size_t) i].folder) == "LEADS") leads.push_back(i);
    const int itemH = 24, w = 230, x0 = 334, y0 = 52, h = (int) leads.size() * itemH + 8 + itemH;
    g.saveState(); g.setOrigin(x0, y0); g.reduceClipRegion(0, 0, w, h);
    lf.drawPopupMenuBackground(g, w, h);
    lf.drawPopupMenuSectionHeader(g, { 0, 4, w, itemH }, "BASS: LEADS");
    for (size_t k = 0; k < leads.size(); ++k)
        lf.drawPopupMenuItem(g, { 0, 4 + itemH * (int) (k + 1), w, itemH }, false, true, k == 3, juce::String(v[(size_t) leads[k]].name) == "Metal Banshee", false, v[(size_t) leads[k]].name, {}, nullptr, nullptr);
    g.restoreState();
    juce::PNGImageFormat png; file.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())) png.writeImageToStream(img, *os);
    std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
}

// ================= VINTAGE CHARACTER (Shared/VintageCharacter.h)
static std::vector<float> vcRun(int mode, float amount, const std::vector<float>& in, double sr, bool noQuant = false, bool noNoise = false, float drive = 0, bool noFilters = false) {
    vc::Character c; c.prepare(sr, 512); c.setMode(mode); c.setAmount(amount); c.setDrive(drive); c.testNoQuantise = noQuant; c.testNoNoise = noNoise; c.testNoFilters = noFilters;
    std::vector<float> x = in; for (size_t a = 0; a < x.size(); a += 512) { float* p[1] = { x.data() + a }; c.process(p, 1, (int) juce::jmin((size_t) 512, x.size() - a)); }
    return x;
}
static void testVintage() {
    const double sr = 44100; const int N = 1 << 16;
    auto sine = [&](double f, double a, int n) { std::vector<float> v((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (a * std::sin(2 * juce::MathConstants<double>::pi * f * i / sr)); return v; };
    juce::String txt; bool ok = true;
    for (int m = 1; m <= 3; ++m) {
        const auto S = vc::spec(m);
        // effective rate: the raw hold staircase (filters, quantiser and noise out of the way): new values per second
        const auto y = vcRun(m, 1.0f, sine(997.0, 0.5, (int) sr * 2), sr, true, true, 0, true);
        int steps = 0; for (size_t i = 1; i < y.size(); ++i) if (y[i] != y[i - 1]) ++steps;
        const double rateMeas = steps / 2.0;
        // effective bits: the quantiser's error on a 0.7 sine, as an ENOB relative to full scale
        const auto in = sine(997.0, 0.7, N), yq = vcRun(m, 1.0f, in, sr, false, true), yc = vcRun(m, 1.0f, in, sr, true, true);
        double e = 0; for (int i = 4096; i < N; ++i) { const double r = (double) yq[(size_t) i] - yc[(size_t) i]; e += r * r; }
        const double enob = (20 * std::log10(0.70710678 / std::sqrt(e / (N - 4096))) - 1.76) / 6.02;
        const double enobWant = S.bits - (S.dither ? 0.79 : 0.0);
        // noise floor: silence in
        const auto z = vcRun(m, 1.0f, std::vector<float>((size_t) N, 0.0f), sr);
        double zn = 0; for (float x : z) zn += (double) x * x; const double floorDb = 10 * std::log10(zn / N + 1e-30);
        const bool good = std::abs(rateMeas / S.rate - 1) < 0.01 && std::abs(enob - enobWant) < 0.75 && std::abs(floorDb - S.floorDb) < 1.0;
        ok = ok && good;
        txt << vc::kModeNames[m] << ": " << juce::String(rateMeas, 0) << " Hz (" << juce::String(S.rate, 0) << "), " << juce::String(enob, 2) << " bits (" << juce::String(enobWant, 2) << "), floor "
            << juce::String(floorDb, 1) << " dBFS (" << juce::String(S.floorDb, 1) << ")" << (good ? "; " : " <- OFF SPEC; ");
    }
    check(ok, "VINTAGE models measure to spec: " + txt.dropLastCharacters(2));
    // AMOUNT 0 / mode Off: bit-exact bypass
    std::vector<float> rnd((size_t) 20000); juce::Random r(5); for (auto& x : rnd) x = r.nextFloat() * 2 - 1;
    bool exact = true; for (int m = 0; m <= 3; ++m) { const auto o = vcRun(m, m == 0 ? 1.0f : 0.0f, rnd, sr); exact = exact && std::memcmp(o.data(), rnd.data(), rnd.size() * sizeof(float)) == 0; }
    // garbage in: NaN / inf / huge never come out
    std::vector<float> bad = rnd; for (size_t i = 0; i < bad.size(); i += 97) bad[i] = (i % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? std::numeric_limits<float>::infinity() : 1.0e6f);
    bool fin = true; double pk = 0; for (int m = 1; m <= 3; ++m) for (float d : { 0.0f, 1.0f }) for (float x : vcRun(m, 1.0f, bad, sr, false, false, d)) { fin = fin && std::isfinite(x); pk = juce::jmax(pk, (double) std::abs(x)); }
    // CPU: 10 s of stereo per mode
    std::vector<float> L((size_t) sr * 10), R2((size_t) sr * 10); for (size_t i = 0; i < L.size(); ++i) { L[i] = rnd[i % rnd.size()] * 0.5f; R2[i] = -L[i]; }
    double worstCpu = 0;
    for (int m = 1; m <= 3; ++m) {
        vc::Character c; c.prepare(sr, 512); c.setMode(m); c.setAmount(1.0f); c.setDrive(0.5f);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t a = 0; a < L.size(); a += 512) { float* p[2] = { L.data() + a, R2.data() + a }; c.process(p, 2, (int) juce::jmin((size_t) 512, L.size() - a)); }
        worstCpu = juce::jmax(worstCpu, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 10.0 * 100.0);
    }
    check(exact && fin && pk < 1.5 && worstCpu < 2.0, "VINTAGE: AMOUNT 0 / Off is bit-exact, NaN / inf / 1e6 in -> finite out (peak " + juce::String(pk, 2) + "), CPU " + juce::String(worstCpu, 3) + " % of a core per stereo track");
    // the Sample Ghost: off = the sample as loaded; on = a character copy that plays; the project keeps the raw sample + settings
    auto smp = std::make_shared<gs::SampleData>(); smp->rate = 44100; smp->name = "test chord";
    smp->data.resize(44100); for (int i = 0; i < 44100; ++i) smp->data[(size_t) i] = 0.3f * (float) (std::sin(i * 0.0627) + std::sin(i * 0.0791) + std::sin(i * 0.0940)); smp->buildFrames();
    PoltergeistProcessor a; a.setSampleGhost(smp);
    const bool offSame = a.engine.getSample().get() == smp.get();
    setP(a, "smp_vc", 1); a.applyCharacter();
    auto played = a.engine.getSample(); double diff = 0; for (int i = 0; i < 44100; ++i) diff = juce::jmax(diff, (double) std::abs(played->data[(size_t) i] - smp->data[(size_t) i]));
    juce::MemoryBlock mb; a.getStateInformation(mb);
    PoltergeistProcessor b; b.restoreState(mb.getData(), (int) mb.getSize());
    auto pb = b.engine.getSample(); double rt = 0; bool rawKept = b.rawSample && std::abs(b.rawSample->data[1000] - smp->data[1000]) < 1e-3f;
    if (pb && (int) pb->data.size() == (int) played->data.size()) for (size_t i = 0; i < played->data.size(); ++i) rt = juce::jmax(rt, (double) std::abs(pb->data[i] - played->data[i])); else rt = 9;
    check(offSame && diff > 0.01 && rawKept && rt < 0.0079, "Sample Ghost character: off plays the sample as loaded, 8-bit changes it (max " + juce::String(diff, 3) + "), the project keeps the raw sample and the setting (re-made copy within " + juce::String(rt, 4) + ", 1 LSB of 8 bit: the project stores 16-bit audio)");
}
// the same character on the CHOP slices and the Sample Lab keys (one shared control), saved with the project
static void testVintageEverywhere() {
    auto smp = std::make_shared<gs::SampleData>(); smp->rate = 44100; smp->name = "test phrase"; smp->data.resize(88200);
    for (int i = 0; i < 88200; ++i) { const double env = std::abs(std::sin(i * 3.14159 / 11025.0)); smp->data[(size_t) i] = (float) (0.5 * env * (std::sin(i * 0.0627) + 0.5 * std::sin(i * 0.181))); }
    smp->buildFrames();
    PoltergeistProcessor a; a.chopLoad(smp, false); a.chopRenderNow(); a.labMapKey(60, smp);
    const bool offSame = a.chopKitUi == a.rawChopKit && a.keyPlay[60] == a.keySample(60) && a.chopKitUi && a.chopKitUi->n > 1;
    setP(a, "smp_vc", 1); a.applyCharacter();
    double dChop = 0, dKey = 0;
    const auto k = a.chopKitUi, r = a.rawChopKit;
    bool sameShape = k && r && k->n == r->n && k != r;
    if (sameShape) for (int i = 0; i < k->n; ++i) { sameShape = sameShape && k->slot[i].len == r->slot[i].len; for (int j = 0; j < k->slot[i].len; ++j) dChop = juce::jmax(dChop, (double) std::abs(k->slot[i].d[j] - r->slot[i].d[j])); }
    for (int j = 0; j < smp->size(); ++j) dKey = juce::jmax(dKey, (double) std::abs(a.keyPlay[60]->data[(size_t) j] - smp->data[(size_t) j]));
    // project round trip: raw audio + the setting, the character copies re-made on load
    juce::MemoryBlock mb; a.getStateInformation(mb);
    PoltergeistProcessor b; b.restoreState(mb.getData(), (int) mb.getSize()); b.chopRenderNow();
    double rtChop = 0, rtKey = 0; const auto kb = b.chopKitUi;
    if (kb && k && kb->n == k->n) { for (int i = 0; i < k->n; ++i) for (int j = 0; j < juce::jmin(k->slot[i].len, kb->slot[i].len); ++j) rtChop = juce::jmax(rtChop, (double) std::abs(k->slot[i].d[j] - kb->slot[i].d[j])); } else rtChop = 9;
    if (b.keyPlay[60]) { for (int j = 0; j < smp->size(); ++j) rtKey = juce::jmax(rtKey, (double) std::abs(b.keyPlay[60]->data[(size_t) j] - a.keyPlay[60]->data[(size_t) j])); } else rtKey = 9;
    // and back off: everything plays the raw audio again
    setP(a, "smp_vc", 0); a.applyCharacter();
    const bool offAgain = a.chopKitUi == a.rawChopKit && a.keyPlay[60] == a.keySample(60);
    check(offSame && sameShape && dChop > 0.01 && dKey > 0.01 && rtChop < 0.0079 && rtKey < 0.0079 && offAgain,
          "VINTAGE on CHOP slices + Lab keys: off = raw, 8-bit changes " + juce::String(a.rawChopKit ? a.rawChopKit->n : 0) + " slices (max " + juce::String(dChop, 3) + ") and the key (max " + juce::String(dKey, 3)
          + "), re-made after reload within " + juce::String(juce::jmax(rtChop, rtKey), 4) + ", off again = raw");
}

// ================= PROCESS LAB inside the SAMPLE LAB (Shared/ProcessLab*)
static std::shared_ptr<gs::SampleData> testPhrase(double secs = 2.0) {
    auto smp = std::make_shared<gs::SampleData>(); smp->rate = gs::Lab::kRate; smp->name = "test phrase"; const int n = (int) (secs * smp->rate); smp->data.resize((size_t) n);
    for (int i = 0; i < n; ++i) { const double env = std::abs(std::sin(i * 3.14159 / 11025.0)); smp->data[(size_t) i] = (float) (0.5 * env * (std::sin(i * 0.0627) + 0.5 * std::sin(i * 0.181 + 0.0001 * i))); }
    smp->buildFrames(); return smp;
}
// the PROCESS LAB page left (owner 2026-10-26); an old project's PROCESS LAB data (the chain, each recording at its own rate and
// channels, the processed result as the Lab source) still loads, plays from its keys and saves unchanged: the processor keeps it.
static void testProcessLab() {
    PoltergeistProcessor p; const auto src = testPhrase(); const int id = p.lab.addSource(src);
    // a committed PROCESS LAB result (what the panel's APPLY did): the second half reversed
    plab::Audio a; a.rate = gs::Lab::kRate; a.ch.push_back(src->data); const int half = src->size() / 2; std::reverse(a.ch[0].begin() + half, a.ch[0].end());
    const bool replaced = p.replaceLabSource(id, a);
    const auto& now = p.lab.sources[id]->data; bool rev = (int) now.size() == src->size();
    for (int i = 0; rev && i < src->size() - half; ++i) rev = now[(size_t) (half + i)] == src->data[(size_t) (src->size() - 1 - i)];
    juce::ValueTree pl("PLAB"); pl.setProperty("seed", 777, nullptr); juce::ValueTree op("OP"); op.setProperty("type", "SpecBlur", nullptr); pl.appendChild(op, nullptr); p.setPlabState(pl);
    p.labMapKey(60, p.lab.sources[id]);
    juce::MemoryBlock mb; p.getStateInformation(mb);
    const auto xml = PoltergeistProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize());
    PoltergeistProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    const bool chainBack = q.plabState().isEquivalentTo(pl), srcBack = q.labOriginals.count(id) && q.lab.sources.count(id), keyBack = q.keySample(60) != nullptr;
    juce::MemoryBlock again; q.getStateInformation(again); const auto xml2 = PoltergeistProcessor::getXmlFromBinary(again.getData(), (int) again.getSize());
    bool same = xml && xml2; for (auto* tag : { "PLAB", "PLABSRC", "LAB", "KEYS" }) { auto* x = xml->getChildByName(tag); auto* y = xml2->getChildByName(tag); same = same && x && y && x->isEquivalentTo(y, false); }
    // an old project without PROCESS LAB data: empty chain
    PoltergeistProcessor r; const bool oldEmpty = !r.plabState().isValid() || r.plabState().getNumChildren() == 0;
    // the audition player still runs at the end of processBlock (an old project may have it set up) and stops
    p.setRateAndBufferSizeDetails(44100, 512); p.prepareToPlay(44100, 512);
    auto clip = std::make_shared<plab::Audio>(); clip->rate = 44100; clip->ch.push_back(std::vector<float>(44100, 0.25f));
    p.labAudition.play(clip, false); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer mid; double pk = 0;
    for (int b = 0; b < 10; ++b) { buf.clear(); p.processBlock(buf, mid); pk = juce::jmax(pk, (double) buf.getMagnitude(0, 512)); }
    p.labAudition.stop(); for (int b = 0; b < 10; ++b) { buf.clear(); p.processBlock(buf, mid); }
    const double after = buf.getMagnitude(0, 512);
    // a stereo 48 kHz file: kept at 48 kHz in stereo through a committed result and a save / load (16-bit)
    bool stereoOk = false; juce::String t2;
    {
        auto f = juce::File::createTempFile(".wav"); const int n = 48000;
        { juce::AudioBuffer<float> b(2, n); for (int i = 0; i < n; ++i) { const float e = (float) std::abs(std::sin(i * 3.14159 / 12000.0)); b.setSample(0, i, 0.5f * e * (float) std::sin(i * 0.05)); b.setSample(1, i, 0.3f * e * (float) std::sin(i * 0.11)); }
          juce::WavAudioFormat w; if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream())) if (auto* wr = w.createWriterFor(os.get(), 48000, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> W(wr); W->writeFromAudioSampleBuffer(b, 0, n); } }
        PoltergeistProcessor p2; juce::String err; const int sid = p2.labAddFile(f, err);
        auto before = p2.labProcessAudio(sid); const bool native = before.channels() == 2 && before.rate == 48000.0;
        plab::Audio r2 = before; for (auto& c : r2.ch) std::reverse(c.begin(), c.end()); p2.replaceLabSource(sid, r2);
        juce::MemoryBlock m2; p2.getStateInformation(m2); PoltergeistProcessor q2; q2.restoreState(m2.getData(), (int) m2.getSize());
        const auto back = q2.labProcessAudio(sid); double d = 9;
        if (back.channels() == 2 && back.length() == r2.length()) { d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < back.length(); ++i) d = juce::jmax(d, (double) std::abs(back.ch[(size_t) c][(size_t) i] - r2.ch[(size_t) c][(size_t) i])); }
        const int tl = (int) q2.lab.sources[sid]->data.size(), want = (int) std::floor(before.length() * 44100.0 / 48000.0);
        stereoOk = native && d < 3.0 / 32767.0 && std::abs(tl - want) <= 1;
        t2 << "a 48 kHz stereo file stays 2 ch at 48 kHz through a result + save / load (max diff " << juce::String(d, 6) << "), timeline copy " << tl << " samples (" << want << ")";
        f.deleteFile();
    }
    check(replaced && rev && chainBack && srcBack && keyBack && same && oldEmpty && pk > 0.1 && after < 1e-6 && stereoOk,
          "old PROCESS LAB data (no page any more): result kept " + juce::String((int) rev) + ", chain / recordings / key back after load " + juce::String((int) chainBack) + juce::String((int) srcBack) + juce::String((int) keyBack)
          + ", re-saved unchanged " + juce::String((int) same) + ", audition " + juce::String(pk, 2) + " then " + juce::String(after, 6) + "; " + t2);
}

// a phrase from Poltergeist, then the same through a PROCESS LAB chain (brassage + spectral blur + iterate + normalise)
static void plabDemo(const juce::File& dir) {
    dir.createDirectory(); const double sr = 44100, bpm = 100;
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Hollow Halo"));
    auto s = renderHost(p, sr, bpm, leadPhraseEvents(bpm), 60.0 / bpm * 16 + 1.0);
    const int n = (int) s.size() / 2;
    writeWav(dir.getChildFile("processlab_0_source.wav"), s, sr);
    plab::Audio a; a.rate = sr; a.ch.push_back(std::vector<float>(s.begin(), s.begin() + n)); a.ch.push_back(std::vector<float>(s.begin() + n, s.end()));
    plab::Chain c; c.seed = 21;
    plab::Op b(plab::OpType::Brassage); { const float v[] = { 60, 3, 120, 7, 1.5f }; for (int k = 0; k < 5; ++k) b.p[(size_t) k] = v[k]; } c.ops.push_back(b);
    plab::Op bl(plab::OpType::SpecBlur); bl.p[1] = 130; c.ops.push_back(bl);   // blur width in ms (about the old 12 frames)
    plab::Op it(plab::OpType::Iterate); { const float v[] = { 3, 2, 10, 4, 20 }; for (int k = 0; k < 5; ++k) it.p[(size_t) k] = v[k]; } c.ops.push_back(it);
    c.ops.push_back(plab::Op(plab::OpType::Normalise));
    plab::Region r = plab::Region::all(a);
    if (plab::applyChain(a, r, c)) {
        Stereo y((size_t) a.length() * 2); for (int i = 0; i < a.length(); ++i) { y[(size_t) i] = a.ch[0][(size_t) i]; y[(size_t) (a.length() + i)] = a.ch[1][(size_t) i]; }
        writeWav(dir.getChildFile("processlab_1_brassage_blur_iterate.wav"), y, sr);
    }
}
// ================= BYPASS (Shared/Bypass.h)
static void testBypass() {
    const double sr = 44100; const int B = 256;
    auto make = [&](PoltergeistProcessor& p) { p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Clean Ghost Bass")); p.setRateAndBufferSizeDetails(sr, B); p.prepareToPlay(sr, B); };
    auto setBy = [](PoltergeistProcessor& p, bool on) { p.bypassParam->setValueNotifyingHost(on ? 1.0f : 0.0f); };
    // 1) engaged mid-note: < -120 dBFS within 20 ms, no click in the fade, ~no CPU while silent
    PoltergeistProcessor p; make(p);
    juce::AudioBuffer<float> buf(2, B); juce::MidiBuffer m; std::vector<float> out; double tSilent = 0, tPlay = 0;
    const int engageBlock = 40, releaseBlock = 140, nBlocks = 200;
    for (int b = 0; b < nBlocks; ++b) {
        m.clear(); if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
        if (b == engageBlock) setBy(p, true);
        if (b == 60) m.addEvent(juce::MidiMessage::noteOn(1, 55, 0.9f), 10);   // pressed while bypassed: ignored
        if (b == 80) m.addEvent(juce::MidiMessage::noteOff(1, 48), 0);         // released while bypassed
        if (b == releaseBlock) setBy(p, false);
        buf.clear(); const auto t0 = std::chrono::steady_clock::now(); p.processBlock(buf, m); const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (b > engageBlock + 10 && b < releaseBlock) tSilent += dt; else if (b > 5 && b < engageBlock) tPlay += dt;
        for (int i = 0; i < B; ++i) out.push_back(buf.getSample(0, i));
    }
    const int e0 = engageBlock * B, w20 = (int) (0.020 * sr); double after = 0, stepFade = 0, stepSteady = 0;
    for (int i = e0 + w20; i < releaseBlock * B; ++i) after = juce::jmax(after, (double) std::abs(out[(size_t) i]));
    for (int i = e0 + 1; i < e0 + w20; ++i) stepFade = juce::jmax(stepFade, (double) std::abs(out[(size_t) i] - out[(size_t) i - 1]));
    double pk = 0; for (int i = e0 - 4410; i < e0; ++i) { stepSteady = juce::jmax(stepSteady, (double) std::abs(out[(size_t) i] - out[(size_t) i - 1])); pk = juce::jmax(pk, (double) std::abs(out[(size_t) i])); }
    const double ramp = 0.006 * sr, allow = stepSteady + 2.5 * pk / ramp;
    // after release: nothing was held (55 ignored, 48 released while bypassed): silence, no stuck note
    double stuck = 0; for (int i = releaseBlock * B + 2205; i < nBlocks * B; ++i) stuck = juce::jmax(stuck, (double) std::abs(out[(size_t) i]));
    const double cpuSilent = tSilent / ((releaseBlock - engageBlock - 11) * B / sr) * 100, cpuPlay = tPlay / ((engageBlock - 6) * B / sr) * 100;
    // 2) the parameter: host-visible bypass, automatable, last in the list
    auto* bp = p.getBypassParameter(); const auto& params = p.getParameters();
    const bool paramOk = bp != nullptr && bp->isAutomatable() && params.getLast() == bp && bp->getName(32) == "Bypass";
    // 3) presets / SUMMON / random never touch it; it travels with the project; an old project loads with it off
    PoltergeistProcessor q; setBy(q, true);
    q.loadPreset(3); q.loadPreset(PoltergeistProcessor::numLegacyPresets() + 5); q.summon("growl heavy"); q.summon(""); q.summon("dark bell");
    const bool keptOn = q.bypassParam->get();
    juce::MemoryBlock mb; q.getStateInformation(mb); PoltergeistProcessor r; r.restoreState(mb.getData(), (int) mb.getSize());
    juce::MemoryBlock old; juce::File(juce::File::getCurrentWorkingDirectory().getChildFile("ref/oldstate.bin")).loadFileAsData(old);
    PoltergeistProcessor o; setBy(o, true); bool oldOff = true; if (old.getSize() > 0) { o.restoreState(old.getData(), (int) old.getSize()); oldOff = !o.bypassParam->get(); }
    const bool ok = after < std::pow(10.0, -120.0 / 20.0) && stepFade <= allow && stuck < 1e-6 && cpuSilent < 0.2 && paramOk && keptOn && r.bypassParam->get() && oldOff;
    check(ok, "BYPASS: 20 ms after engaging " + juce::String(dbv(after), 1) + " dBFS, fade step " + juce::String(stepFade, 4) + " (allowed " + juce::String(allow, 4) + "), CPU while bypassed "
          + juce::String(cpuSilent, 3) + " % (playing " + juce::String(cpuPlay, 2) + " %), no stuck note after release (" + juce::String(dbv(stuck), 0) + " dBFS), host bypass parameter ok " + juce::String((int) paramOk)
          + ", presets / SUMMON leave it on " + juce::String((int) keptOn) + ", saved on " + juce::String((int) r.bypassParam->get()) + ", old project loads off " + juce::String((int) oldOff));
}

// ================= LIGHT / DARK theme
static double contrast(juce::Colour a, juce::Colour b) {
    auto lum = [](juce::Colour c) { auto ch = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
                                    return 0.2126 * ch(c.getFloatRed()) + 0.7152 * ch(c.getFloatGreen()) + 0.0722 * ch(c.getFloatBlue()); };
    const double l1 = lum(a), l2 = lum(b); return (juce::jmax(l1, l2) + 0.05) / (juce::jmin(l1, l2) + 0.05);
}
static const ghosty::themes::Theme kThemes[3] = { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light };
static void testThemeContrast() {
    juce::String txt; bool ok = true;
    for (auto t : kThemes) {
        const auto p = gsui::Palette::make(t);
        double body = 99, sec = 99, acc = 99, ghosts = 99, onPurple = 99;
        for (auto surf : { p.bg, p.card, p.card2 }) { body = juce::jmin(body, contrast(p.ink, surf)); sec = juce::jmin(sec, contrast(p.grey, surf)); }
        for (auto a : { p.purple, p.violet, p.green, p.pink, p.amber, p.blue, p.red, p.cyan }) acc = juce::jmin(acc, contrast(a, p.card));
        for (int g = 0; g < 7; ++g) ghosts = juce::jmin(ghosts, contrast(p.ghost[g], p.card));
        onPurple = contrast(p.onAccent, p.purple);   // (what "on" buttons draw)
        const bool good = body >= 4.5 && sec >= 4.5 && acc >= 3 && ghosts >= 3 && onPurple >= 4.5;
        ok = ok && good;
        txt << ghosty::themes::displayName(t) << ": body " << juce::String(body, 1) << ", secondary " << juce::String(sec, 1) << ", accents " << juce::String(acc, 1) << ", ghosts " << juce::String(ghosts, 1)
            << ", text on purple " << juce::String(onPurple, 1) << (good ? "; " : " <- LOW; ");
    }
    check(ok, "theme tokens (min over window / panel / cells; text >= 4.5, accents + ghosts >= 3): " + txt.dropLastCharacters(2));
}
// the text actually drawn: every visible label / button / combo box on every page, the PROCESS LAB, the tour overlay, a pop-up menu and
// a tooltip, in each theme. Per item: the background = the most common colour in its inner area, the text = the pixel that differs most.
struct PixCheck { double worst = 99; juce::String where; int items = 0; };
static int pixDisabled = 0, pixPunct = 0;
static void pixText(const juce::Image& img, juce::Rectangle<int> r, const juce::String& name, PixCheck& pc, double minText = 1.6) {
    r = r.getIntersection(img.getBounds()); if (r.getWidth() < 4 || r.getHeight() < 4) return;
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    std::map<juce::uint32, int> hist;
    for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) ++hist[bd.getPixelColour(x, y).getARGB() | 0xff000000u];
    juce::uint32 bgc = 0; int bn = -1; for (auto& [c, n] : hist) if (n > bn) { bn = n; bgc = c; }
    const juce::Colour bg(bgc); double best = 1;
    for (auto& [c, n] : hist) best = juce::jmax(best, contrast(juce::Colour(c), bg));
    if (best < minText) return;   // (no text drawn here)
    ++pc.items;
    if (best < pc.worst) { pc.worst = best; pc.where = name; }
}
static void pixComponents(juce::Component& root, juce::Component& c, const juce::Image& img, const juce::String& ctx, PixCheck& pc, std::vector<std::pair<double, juce::String>>& low) {
    for (auto* k : c.getChildren()) {
        if (!k->isVisible() || k->getWidth() <= 0) continue;
        if (!k->isEnabled()) { ++pixDisabled; continue; }   // (inactive controls: exempt from the contrast rule, as in WCAG 1.4.3)
        juce::String text; juce::Rectangle<int> inner = root.getLocalArea(k, k->getLocalBounds());
        if (auto* l = dynamic_cast<juce::Label*>(k)) text = l->getText();
        else if (auto* b = dynamic_cast<juce::Button*>(k)) { text = b->getButtonText(); inner = inner.reduced(juce::jmin(8, inner.getWidth() / 6), juce::jmin(5, inner.getHeight() / 4)); }
        else if (auto* cb = dynamic_cast<juce::ComboBox*>(k)) { text = cb->getText(); inner = inner.reduced(4, 4).withTrimmedRight(22); }
        if (text.trim().isNotEmpty() && !text.containsAnyOf("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")) ++pixPunct;   // ('-', '<', '>': 1-px glyphs, never fully covered)
        else if (text.trim().isNotEmpty()) {
            PixCheck one; pixText(img, inner, ctx + " '" + text.substring(0, 24) + "'", one);
            if (one.items) { ++pc.items; if (one.worst < pc.worst) { pc.worst = one.worst; pc.where = one.where; } if (one.worst < 4.5) low.push_back({ one.worst, one.where }); }
        }
        pixComponents(root, *k, img, ctx, pc, low);
    }
}
static void testThemeTextPixels() {
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    auto smp = testPhrase(3.0); smp->name = "Ghost Choir Stab"; p.addSample(smp);
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    juce::String txt; bool ok = true; std::vector<std::pair<double, juce::String>> low; int total = 0;
    for (auto t : kThemes) {
        ed->testTheme(t); PixCheck pc;
        auto shot = [&](const juce::String& ctx) {
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            pixComponents(*ed, *ed, img, ghosty::themes::displayName(t) + " " + ctx, pc, low); };
        for (int pg : { 0, 2, 3, 4, 7, 8, 9 }) { ed->testShow(pg, 0, 0); if (pg == 0) ed->selectGhost(0); shot("page " + juce::String(pg)); }
        ed->testShow(0, 0, 0); ed->selectGhost(gs::kNoise); shot("noise ghost");
        ed->selectGhost(7); ed->testRefresh(); shot("ADD SAMPLE panel"); ed->selectGhost(0);
        {   // a pop-up menu (normal + highlighted item) and a tooltip, drawn by the look-and-feel
            auto& lf = ed->getLookAndFeel(); juce::Image img(juce::Image::ARGB, 220, 90, true);
            { juce::Graphics g(img); lf.drawPopupMenuBackground(g, 220, 60);
              lf.drawPopupMenuItem(g, { 0, 4, 220, 24 }, false, true, false, false, false, "Ghost Choir Stab", {}, nullptr, nullptr);
              lf.drawPopupMenuItem(g, { 0, 30, 220, 24 }, false, true, true, false, false, "Highlighted item", {}, nullptr, nullptr);
              g.saveState(); g.setOrigin(0, 62); lf.drawTooltip(g, "A tooltip", 220, 26); g.restoreState(); }
            for (auto [r, n] : { std::pair<juce::Rectangle<int>, const char*> { { 4, 6, 200, 20 }, "menu item" }, { { 4, 32, 200, 20 }, "highlighted menu item" }, { { 4, 64, 200, 22 }, "tooltip" } }) {
                PixCheck one; pixText(img, r, ghosty::themes::displayName(t) + " " + n, one);
                if (one.items) { ++pc.items; if (one.worst < pc.worst) { pc.worst = one.worst; pc.where = one.where; } if (one.worst < 4.5) low.push_back({ one.worst, one.where }); }
            }
        }
        ok = ok && pc.worst >= 4.5 && pc.items > 120; total += pc.items;
        txt << ghosty::themes::displayName(t) << " " << pc.items << " items, worst " << juce::String(pc.worst, 2) << " (" << pc.where << "); ";
    }
    std::sort(low.begin(), low.end());
    juce::String lows; for (size_t i = 0; i < juce::jmin<size_t>(12, low.size()); ++i) lows << juce::String(low[i].first, 2) << " " << low[i].second << " | ";
    check(ok, "text contrast measured in pixels (" + juce::String(total) + " texts, " + juce::String(pixDisabled) + " disabled controls and " + juce::String(pixPunct) + " symbol-only labels skipped: every page, the NOISE and ADD SAMPLE panels, a menu, a tooltip; >= 4.5): " + txt + (lows.isEmpty() ? juce::String() : "LOW: " + lows));
    ed->testTheme(ghosty::themes::Theme::Dark);
}
static void testThemeState() {
    // saved as theme_name; an old project with the old key: "light" -> LIGHT, "dark" -> BLACK; no key -> BLACK; presets / SUMMON keep the look
    PoltergeistProcessor a; a.uiTheme = 1; a.loadPreset(5); a.summon("growl");
    juce::MemoryBlock mb; a.getStateInformation(mb); PoltergeistProcessor b; b.restoreState(mb.getData(), (int) mb.getSize());
    const bool purpleKept = a.uiTheme == 1 && b.uiTheme == 1 && juce::String::createStringFromData(mb.getData(), (int) mb.getSize()).isNotEmpty();
    auto withKeys = [&](std::function<void(juce::ValueTree&)> f) {
        PoltergeistProcessor x; juce::MemoryBlock m; x.getStateInformation(m); auto xml = juce::AudioProcessor::getXmlFromBinary(m.getData(), (int) m.getSize()); auto v = juce::ValueTree::fromXml(*xml);
        f(v); juce::MemoryBlock o; juce::AudioProcessor::copyXmlToBinary(*v.createXml(), o); PoltergeistProcessor y; y.uiTheme = 1; y.restoreState(o.getData(), (int) o.getSize()); return y.uiTheme; };
    const int oldLight = withKeys([](juce::ValueTree& v) { v.removeProperty("theme_name", nullptr); v.setProperty("uiTheme", "light", nullptr); });
    const int oldDark = withKeys([](juce::ValueTree& v) { v.removeProperty("theme_name", nullptr); v.setProperty("uiTheme", "dark", nullptr); });
    const int noKey = withKeys([](juce::ValueTree& v) { v.removeProperty("theme_name", nullptr); v.removeProperty("uiTheme", nullptr); });
    const int named = withKeys([](juce::ValueTree& v) { v.setProperty("theme_name", "light", nullptr); });
    juce::MemoryBlock old; juce::File::getCurrentWorkingDirectory().getChildFile("ref/oldstate.bin").loadFileAsData(old);
    PoltergeistProcessor o; o.uiTheme = 2; if (old.getSize() > 0) o.restoreState(old.getData(), (int) old.getSize());
    // the button cycles BLACK -> PURPLE -> LIGHT and names the next look
    PoltergeistProcessor e; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(e.createEditor())); ed->setSize(1040, 744);
    juce::String seq = ed->testThemeButton()->getButtonText(); juce::String states = juce::String(e.uiTheme);
    for (int i = 0; i < 3; ++i) { ed->testThemeButton()->onClick(); seq << ">" << ed->testThemeButton()->getButtonText(); states << ">" << e.uiTheme; }
    const bool cycle = seq == "PURPLE>LIGHT>BLACK>PURPLE" && states == "0>1>2>0";
    check(purpleKept && oldLight == 2 && oldDark == 0 && noKey == 0 && named == 2 && (old.getSize() == 0 || o.uiTheme == 0) && cycle,
          "theme: saved as theme_name (PURPLE reloads PURPLE " + juce::String((int) purpleKept) + "), old key light -> " + juce::String(oldLight) + ", dark -> " + juce::String(oldDark) + ", no key -> " + juce::String(noKey)
          + ", the old project -> " + juce::String(o.uiTheme) + "; the button shows " + seq + " (look " + states + ")");
}
static void testThemeOpaque() {
    PoltergeistProcessor p; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    p.addSample(testPhrase(1.0));
    int see = 0, n = 0; juce::String which; bool edClearAny = false;
    for (auto t : kThemes) {
        ed->testTheme(t);
        for (auto* c : ed->testPanels()) {
            if (!c || c->getWidth() <= 0 || c->getHeight() <= 0) continue; ++n;
            juce::Image img(juce::Image::ARGB, c->getWidth(), c->getHeight(), true);
            { juce::Graphics g(img); c->paintEntireComponent(g, true); }
            bool clear = false; juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < img.getHeight() && !clear; ++y) for (int x = 0; x < img.getWidth(); ++x) if (bd.getPixelColour(x, y).getAlpha() < 255) { clear = true; break; }
            if (clear) { ++see; which << ghosty::themes::displayName(t) << ":" << c->getName() << "(" << c->getWidth() << "x" << c->getHeight() << ") "; }
            if (!c->isOpaque()) { ++see; which << "not-opaque-flag "; }
        }
        for (int pg : { 0, 2, 3, 4, 7, 8, 9, 10 }) {   // the whole editor on every page (10 = GHOSTS with the ADD SAMPLE panel)
            ed->testShow(pg == 10 ? 0 : pg, 0, 0); ed->selectGhost(pg == 10 ? 7 : 0);
            juce::Image all(juce::Image::ARGB, 1040, 744, true); { juce::Graphics g(all); ed->paintEntireComponent(g, true); }
            juce::Image::BitmapData bd(all, juce::Image::BitmapData::readOnly); bool edClear = false;
            for (int y = 0; y < 744 && !edClear; y += 3) for (int x = 0; x < 1040; x += 3) if (bd.getPixelColour(x, y).getAlpha() < 255) { edClear = true; break; }
            if (edClear) { edClearAny = true; which << ghosty::themes::displayName(t) << ":page" << pg << " "; }
        }
    }
    ed->testTheme(ghosty::themes::Theme::Dark);
    check(see == 0 && n >= 18 && !edClearAny, "BLACK / PURPLE / LIGHT: " + juce::String(n) + " panels painted alone leave no see-through pixel, nor does the editor on any page or with the ADD SAMPLE panel (" + which + ")");
}
static void themeShots(const juce::File& dir) {
    dir.createDirectory();
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    auto smp = testPhrase(3.0); smp->name = "Ghost Choir Stab"; p.addSample(smp); setP(p, "smp_level", 0.8f);
    p.morphAddGhost(0.25f, 0.35f, "current"); p.morphAddGhost(0.75f, 0.3f, "preset:5"); p.morphAddGhost(0.5f, 0.78f, "preset:" + juce::String(PoltergeistProcessor::numLegacyPresets() + 3));
    setP(p, "morph_on", 1.0f); setP(p, "morph_x", 0.42f); setP(p, "morph_y", 0.45f);
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    const char* names[] = { "ghosts", nullptr, "move", "fx", "play", nullptr, nullptr, "bass", "wobble", "morph" };
    for (auto t : kThemes) {
        ed->testTheme(t);
        auto snap = [&](const juce::String& name) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f = dir.getChildFile("poltergeist-" + ghosty::themes::toString(t) + "-" + name + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os);
            std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        for (int pg = 0; pg < 10; ++pg) if (names[pg]) { ed->testShow(pg, 0, 0); if (pg == 0) ed->selectGhost(0); snap(names[pg]); }
        ed->testShow(0, 0, 0); ed->selectGhost(gs::kNoise); snap("noise-ghost");
        ed->selectGhost(7); ed->testRefresh(); snap("add-sample"); ed->selectGhost(0);
        p.bypassParam->setValueNotifyingHost(1.0f); ed->testShow(7, 0, 0); snap("bypassed"); p.bypassParam->setValueNotifyingHost(0.0f);
    }
}

// ================= slider convention, tooltips, version, export gate
static juce::MouseEvent mouseAt(juce::Component& c, float x, float y, juce::ModifierKeys m, int clicks = 1) {
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, { x, y }, m, 1.0f, 0, 0, 0, 0, &c, &c, now, { x, y }, now, clicks, false);
}
// (the CHOP page's BAR controls left with the page; the knobs' part of the convention)
static void testSliderConvention() {
    PoltergeistProcessor p; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744); ed->testShow(7, 0, 0);
    const juce::ModifierKeys L(juce::ModifierKeys::leftButtonModifier);
    auto* cut = ed->testKnobSlider("f1_cut"); const double typed = cut ? cut->getValueFromText("2.5 kHz") : 0;
    // automation still drives the controls
    p.apvts.getParameter("f1_cut")->setValueNotifyingHost(p.apvts.getParameter("f1_cut")->convertTo0to1(640.0f));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    const bool autoOk = cut && std::abs(cut->getValue() - 640.0) < 1.0;
    // a rotary knob keeps relative drag (no jump on click)
    bool knobRel = false; if (cut) { cut->setPopupDisplayEnabled(false, false, nullptr);   // (its value bubble is a desktop window: none headless)
        const double v0 = cut->getValue(); cut->mouseDown(mouseAt(*cut, 2, 2, L)); cut->mouseUp(mouseAt(*cut, 2, 2, L)); knobRel = std::abs(cut->getValue() - v0) < 1e-6; }
    // the ADD SAMPLE knobs are real knobs too (typed in their units)
    ed->testShow(0, 0, 0); ed->selectGhost(7); auto* lvl = ed->testKnobSlider("smp_level");
    check(std::abs(typed - 2500) < 1 && autoOk && knobRel && lvl != nullptr && shown(lvl),
          "slider convention: '2.5 kHz' -> " + juce::String(typed, 0) + ", automation drives the knob " + juce::String((int) autoOk) + ", knobs keep relative drag " + juce::String((int) knobRel) + ", the ADD SAMPLE LEVEL knob shows " + juce::String((int) (lvl && shown(lvl))));
}
static void testPolish() {
    // tooltips: one TooltipWindow per editor
    PoltergeistProcessor p; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    std::function<int(juce::Component&)> countTips = [&](juce::Component& c) { int n = dynamic_cast<juce::TooltipWindow*>(&c) ? 1 : 0; for (auto* k : c.getChildren()) n += countTips(*k); return n; };
    const int tips = countTips(*ed);
    // every ADD SAMPLE control says what it does
    int untipped = 0; juce::String which;
    for (auto* c : ed->testSampleComps()) {
        if (dynamic_cast<juce::Label*>(c)) continue;
        juce::String tip; if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(c)) tip = t->getTooltip();
        if (auto* k = dynamic_cast<gsui::Knob*>(c)) tip = k->slider->getTooltip();
        if (auto* sw = c->getChildComponent(0); tip.isEmpty() && sw) if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(sw)) tip = t->getTooltip();
        if (tip.isEmpty()) { ++untipped; which << typeid(*c).name() << " "; }
    }
    // brand words gone from what users see: parameter names, preset names, SUMMON categories
    juce::StringArray bad { "OTT", "Melodyne", "Serum", "Massive", "Sylenth" }; juce::String hits;
    for (auto* prm : p.getParameters()) for (auto& b : bad) if (prm->getName(64).containsWholeWord(b)) hits << prm->getName(64) << "; ";
    for (int i = 0; i < p.getNumPrograms(); ++i) for (auto& b : bad) if (p.getProgramName(i).containsWholeWord(b)) hits << p.getProgramName(i) << "; ";
    const bool squeeze = p.apvts.getParameter("ott_amt")->getName(64) == "Squeeze" && p.getProgramName(33) == "Squeeze Phantom";
    check(tips == 1 && untipped == 0 && hits.isEmpty() && squeeze,
          "polish: " + juce::String(tips) + " tooltip window per editor, every ADD SAMPLE control has a tooltip (" + juce::String(untipped) + " without " + which + "), no brand words in parameter / preset names ("
          + (hits.isEmpty() ? juce::String("none") : hits) + "), OTT shown as Squeeze " + juce::String((int) squeeze));
}

// ================= ENVELOPE TRACER
static void testTracer() {
    const double sr = 44100; const int B = 64;
    auto make = [&](PoltergeistProcessor& q, bool loop) {
        auto pt = revPatch(); pt[gs::opId(0, "a")] = loop ? 0.02f : 0.1f; pt[gs::opId(0, "d")] = loop ? 0.05f : 0.2f; pt[gs::opId(0, "s")] = loop ? 0.3f : 0.5f; pt[gs::opId(0, "r")] = 0.3f;
        pt[gs::opId(0, "loop")] = loop ? 1.0f : 0.0f; pt["flt_a"] = 0.05f; pt["flt_d"] = 0.1f; pt["flt_s"] = 0.3f; pt["flt_r"] = 0.2f;
        applyP(q, pt); q.setRateAndBufferSizeDetails(sr, B); q.prepareToPlay(sr, B); q.engine.traceOn.store(true);
    };
    // 1) the reported level follows the analytic envelope (attack, decay, sustain rest, release) for the amp AND the filter envelope
    PoltergeistProcessor p; make(p, false);
    juce::AudioBuffer<float> buf(2, B); juce::MidiBuffer m; double worstA = 0, worstF = 0, worstFrac = 0; int stagesSeen = 0, lastStage = -1;
    const double tOff = 1.0;
    auto amp = [&](double t) { const double a = 0.1, d = 0.2, S = 0.5, r = 0.3; if (t < tOff) return t < a ? t / a : S + (1 - S) * std::exp(-6.9 * (t - a) / d); return S * std::exp(-6.9 * (t - tOff) / r); };
    auto flt = [&](double t) { const double a = 0.05, d = 0.1, S = 0.3, r = 0.2; if (t < tOff) return t < a ? t / a : S + (1 - S) * std::exp(-6.9 * (t - a) / d); return S * std::exp(-6.9 * (t - tOff) / r); };
    for (int b = 0; b < (int) (1.6 * sr / B); ++b) {
        m.clear(); if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0); if (b == (int) (tOff * sr / B)) m.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
        buf.clear(); p.processBlock(buf, m);
        gs::Engine::TraceSnap ts; if (!p.engine.readTrace(ts) || ts.count < 1) continue;
        const double t = (b + 1) * B / sr, tq = b * B / sr;   // (note-off lands at the block start)
        const double tt = t >= tOff ? t - (t - tq) * 0.0 : t;
        worstA = juce::jmax(worstA, std::abs(ts.v[0].level[0] - amp(tt))); worstF = juce::jmax(worstF, std::abs(ts.v[0].level[7] - flt(tt)));
        if (ts.v[0].stage[0] == 4 && t > 0.11 && t < 0.29) worstFrac = juce::jmax(worstFrac, std::abs(ts.v[0].frac[0] - (t - 0.1) / 0.2));
        if (ts.v[0].stage[0] != lastStage) { lastStage = ts.v[0].stage[0]; ++stagesSeen; }
    }
    // 2) loop: attack -> decay -> attack ... while held
    PoltergeistProcessor l; make(l, true); int wraps = 0, prev = 0;
    for (int b = 0; b < (int) (0.6 * sr / B); ++b) {
        m.clear(); if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0); buf.clear(); l.processBlock(buf, m);
        gs::Engine::TraceSnap ts; if (l.engine.readTrace(ts) && ts.count > 0) { const int st = ts.v[0].stage[0]; if (prev == 4 && st == 2) ++wraps; prev = st; }
    }
    // 3) polyphony: the newest 4 voices, newest first
    PoltergeistProcessor pp; make(pp, false); setP(pp, "mono", 0); setP(pp, "voices", 8); pp.prepareToPlay(sr, B);
    juce::String notes; gs::Engine::TraceSnap tp {};
    for (int b = 0; b < 200; ++b) { m.clear(); if (b % 20 == 0 && b < 120) m.addEvent(juce::MidiMessage::noteOn(1, 48 + b / 20, 0.9f), 0); buf.clear(); pp.processBlock(buf, m); }
    pp.engine.readTrace(tp); for (int i = 0; i < tp.count; ++i) notes << tp.v[i].note << " ";
    const bool polyOk = tp.count == 4 && tp.v[0].note == 53 && tp.v[1].note == 52 && tp.v[2].note == 51 && tp.v[3].note == 50;
    // 4) no tearing: a reader thread hammers the snapshot while rapid notes play
    PoltergeistProcessor st; make(st, false); setP(st, "mono", 0); st.prepareToPlay(sr, 32);
    std::atomic<bool> run { true }; std::atomic<long> good { 0 }, torn { 0 };
    std::thread reader([&] { gs::Engine::TraceSnap ts; while (run.load()) { if (st.engine.readTrace(ts)) ++good; else if (ts.torn) ++torn; } });
    { juce::AudioBuffer<float> b2(2, 32); juce::Random rr(9);
      for (int b = 0; b < 6000; ++b) { juce::MidiBuffer mm; if (rr.nextInt(3) == 0) mm.addEvent(juce::MidiMessage::noteOn(1, 40 + rr.nextInt(30), 0.8f), rr.nextInt(32)); if (rr.nextInt(3) == 0) mm.addEvent(juce::MidiMessage::noteOff(1, 40 + rr.nextInt(30)), rr.nextInt(32)); b2.clear(); st.processBlock(b2, mm); } }
    run.store(false); reader.join();
    // 5) display only: the render is bit-identical with the tracer on, and costs nothing measurable
    auto render = [&](bool on, double* cpu) {
        PoltergeistProcessor q; q.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); setP(q, "mono", 0);
        q.setRateAndBufferSizeDetails(sr, B); q.prepareToPlay(sr, B); q.engine.traceOn.store(on);
        const auto ev = lineEvents(140); std::vector<float> out; juce::AudioBuffer<float> bb(2, B); double c = 0;
        for (int pos = 0; pos < (int) (4.0 * sr); pos += B) { juce::MidiBuffer mm; for (auto& e : ev) { const int at = (int) std::llround(e.t * sr) - pos; if (at >= 0 && at < B) mm.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, e.vel) : juce::MidiMessage::noteOff(1, e.note), at); }
            bb.clear(); const auto t0 = std::chrono::steady_clock::now(); q.processBlock(bb, mm); c += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            for (int i = 0; i < B; ++i) { out.push_back(bb.getSample(0, i)); out.push_back(bb.getSample(1, i)); } }
        if (cpu) *cpu = c / 4.0 * 100.0; return out;
    };
    double cOff = 0, cOn = 0, cOff2 = 0, cOn2 = 0; const auto a1 = render(false, &cOff), a2 = render(true, &cOn); render(false, &cOff2); render(true, &cOn2);
    const bool same = a1 == a2; const double dCpu = 0.5 * ((cOn - cOff) + (cOn2 - cOff2));
    const bool ok = worstA < 0.01 && worstF < 0.01 && worstFrac < 0.02 && stagesSeen >= 4 && wraps >= 3 && polyOk && torn.load() == 0 && good.load() > 100 && same
#if !defined(__SANITIZE_ADDRESS__)
                    && dCpu < 0.1   // (timing is only checked in Release)
#endif
                    ;
    check(ok, "envelope tracer: amp level vs analytic max err " + juce::String(worstA, 4) + ", filter " + juce::String(worstF, 4) + ", decay position err " + juce::String(worstFrac, 4) + " (" + juce::String(stagesSeen)
          + " stages seen), loop wraps " + juce::String(wraps) + ", newest voices " + notes.trim() + ", " + juce::String((int) good.load()) + " concurrent reads, " + juce::String((int) torn.load()) + " torn, render bit-identical "
          + juce::String((int) same) + ", CPU +" + juce::String(dCpu, 3) + " % (" + juce::String(cOff, 2) + " -> " + juce::String(cOn, 2) + ")");
}
static void tracerShots(const juce::File& dir) {
    dir.createDirectory();
    for (auto t : kThemes) for (bool filter : { false, true }) {
        PoltergeistProcessor p; auto pt = revPatch(); pt[gs::opId(0, "a")] = 0.15f; pt[gs::opId(0, "d")] = 0.3f; pt[gs::opId(0, "s")] = 0.5f; pt[gs::opId(0, "r")] = 0.5f;
        pt["flt_a"] = 0.1f; pt["flt_d"] = 0.25f; pt["flt_s"] = 0.35f; pt["flt_r"] = 0.4f; pt["mono"] = 0; applyP(p, pt);
        std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744); ed->testTheme(t); ed->testShow(0, 0, 0); ed->testEnvMode(0, filter);
        p.setRateAndBufferSizeDetails(44100, 256); p.prepareToPlay(44100, 256); p.engine.traceOn.store(true);
        juce::AudioBuffer<float> buf(2, 256); int b = 0;
        auto runTo = [&](double t, int noteOn, int noteOff) { while (b * 256 < (int) (t * 44100)) { juce::MidiBuffer m; if (b == 0 && noteOn >= 0) { m.addEvent(juce::MidiMessage::noteOn(1, noteOn, 0.9f), 0); m.addEvent(juce::MidiMessage::noteOn(1, noteOn + 7, 0.9f), 200); }
                                                                                 if (noteOff >= 0 && b * 256 >= (int) (t * 44100) - 256) { m.addEvent(juce::MidiMessage::noteOff(1, noteOff, 0.9f), 0); m.addEvent(juce::MidiMessage::noteOff(1, noteOff + 7, 0.9f), 0); }
                                                                                 buf.clear(); p.processBlock(buf, m); ++b; } };
        const char* nm[] = { "1-attack", "2-decay", "3-sustain", "4-release" }; const double ts[] = { 0.08, 0.3, 0.8, 1.05 };
        for (int k = 0; k < 4; ++k) {
            runTo(ts[k], 48, k == 3 ? 48 : -1);
            for (int f = 0; f < 4; ++f) { auto* evw = ed->getChildComponent(0); juce::ignoreUnused(evw); juce::MessageManager::getInstance()->runDispatchLoopUntil(40); }
            auto img = ed->createComponentSnapshot(juce::Rectangle<int>(560, 130, 470, 90), true, 2.0f);   // the envelope view, at 2x
            juce::PNGImageFormat png; auto f = dir.getChildFile(juce::String("poltergeist-tracer-") + ghosty::themes::toString(t) + "-" + (filter ? "filter-" : "amp-") + nm[k] + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os);
            std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }
}

// ================= ADD SAMPLE (owner 2026-10-26): the FM page's one sample feature
static juce::MouseEvent mouseEv(juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, juce::ModifierKeys m, bool dragged, int clicks = 1) {
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, pos, m, 1.0f, 0, 0, 0, 0, &c, &c, now, downPos, now, clicks, dragged);
}
// a pitched test file (a 110 Hz = A1 "growl": harmonics + a little vibrato) written as WAV / AIFF / FLAC
static juce::File writeTestFile(const juce::String& ext, double f0 = 110.0) {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("polt-add-sample-test" + ext); f.deleteFile();
    const int n = 44100 * 2; juce::AudioBuffer<float> b(1, n); double ph = 0;
    for (int i = 0; i < n; ++i) { ph += f0 * (1.0 + 0.004 * std::sin(i * 0.0007)) / 44100.0; double x = 0; for (int h = 1; h <= 8; ++h) x += std::sin(2 * juce::MathConstants<double>::pi * h * ph) / h; b.setSample(0, i, (float) (0.4 * x) * juce::jmin(1.0f, (float) i / 400.0f)); }
    std::unique_ptr<juce::AudioFormat> fmt; if (ext == ".wav") fmt.reset(new juce::WavAudioFormat()); else if (ext == ".aif") fmt.reset(new juce::AiffAudioFormat()); else fmt.reset(new juce::FlacAudioFormat());
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = fmt->createWriterFor(os.get(), 44100, 1, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> W(w); W->writeFromAudioSampleBuffer(b, 0, n); }
    return f;
}
static float rawP(PoltergeistProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }

// the slot's waveform: START / END / LOOP with the GhostBlock gestures (Shared/WaveRegion.h); no WAV grip, no drag-out
static void testWaveRegionWiring() {
    PoltergeistProcessor p; auto smp = testPhrase(2.0); p.setSampleGhost(smp); setP(p, "smp_level", 0.8f); p.testLicense = 1;
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744); ed->testShow(0, 0, 0); ed->selectGhost(7);
    auto* wr = ed->testSampleWave();
    if (!wr || !shown(wr)) { check(false, "ADD SAMPLE panel shows its WaveRegion"); return; }
    juce::Image tmp(juce::Image::ARGB, 1040, 744, true); { juce::Graphics g(tmp); ed->paintEntireComponent(g, false); }   // (syncs peaks + region from the params)
    const juce::ModifierKeys L(juce::ModifierKeys::leftButtonModifier), R(juce::ModifierKeys::rightButtonModifier);
    auto gesture = [&](float fromF, float toF, juce::ModifierKeys m, float y) {
        const juce::Point<float> d(wr->xOf(fromF), y), u(wr->xOf(toF), y);
        wr->mouseDown(mouseEv(*wr, d, d, m, false)); if (std::abs(toF - fromF) > 0) wr->mouseDrag(mouseEv(*wr, u, d, m, true)); wr->mouseUp(mouseEv(*wr, u, d, m, true)); };
    const float flagY = 8.0f, midY = wr->waveArea().getCentreY();
    gesture(0.0f, 0.25f, L, flagY); const float st = rawP(p, "smp_start");
    gesture(1.0f, 0.80f, L, flagY);
    const float st2 = rawP(p, "smp_start"), ln = rawP(p, "smp_len"), endF = st2 + ln * (1 - st2);
    gesture(0.40f, 0.40f, R, midY); const float st3 = rawP(p, "smp_start");
    gesture(0.50f, 0.70f, R, midY);
    const float lst = rawP(p, "smp_lst"), lend = rawP(p, "smp_lend"); const bool loopOn = rawP(p, "smp_loop") > 0.5f;
    // the engine plays START, then holds inside the LOOP
    p.setRateAndBufferSizeDetails(44100, 256); p.prepareToPlay(44100, 256);
    juce::AudioBuffer<float> buf(2, 256); float phMin = 9, phMax = -9;
    for (int b = 0; b < 400; ++b) { juce::MidiBuffer m; if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0); buf.clear(); p.processBlock(buf, m);
        if (b > 200) { const float ph = p.engine.smpPlayhead.load(); phMin = juce::jmin(phMin, ph); phMax = juce::jmax(phMax, ph); } }
    const bool holds = phMin >= lst - 0.01f && phMax <= lend + 0.01f;
    // the LOOP pill = one-shot; the region travels with the project
    { const auto pill = wr->loopPill().getCentre(); wr->mouseDown(mouseEv(*wr, pill, pill, L, false)); wr->mouseUp(mouseEv(*wr, pill, pill, L, false)); }
    const bool pillOff = rawP(p, "smp_loop") < 0.5f;
    juce::MemoryBlock mb; p.getStateInformation(mb); PoltergeistProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    bool round = true; for (auto* id : { "smp_start", "smp_len", "smp_loop", "smp_lst", "smp_lend" }) round = round && std::abs(rawP(q, id) - rawP(p, id)) < 1e-6f;
    // no WAV grip: nothing to grab, a drag where it used to be drags nothing (licensed or not), no message about exporting
    juce::StringArray dragged; juce::String msg;
    wr->performDrag = [&](const juce::StringArray& f) { dragged = f; };
    wr->onMessage = [&](const juce::String& m) { msg = m; };
    { auto r = wr->region(); r.loopOn = false; wr->setRegion(r); }
    const auto wa = wr->waveArea(); const juce::Point<float> g0(wr->xOf(wr->region().end) - 24, wa.getY() + 13);
    wr->mouseDown(mouseEv(*wr, g0, g0, L, false)); wr->mouseDrag(mouseEv(*wr, g0.translated(40, 0), g0, L, true)); wr->mouseUp(mouseEv(*wr, g0.translated(40, 0), g0, L, true));
    const bool noGrip = wr->gripRect().isEmpty() && dragged.isEmpty() && !msg.containsIgnoreCase("full version");
    const bool ok = std::abs(st - 0.25f) < 0.01f && std::abs(endF - 0.80f) < 0.01f && std::abs(st3 - 0.40f) < 0.01f && loopOn && std::abs(lst - 0.5f) < 0.01f && std::abs(lend - 0.7f) < 0.01f
                    && holds && pillOff && round && noGrip;
    check(ok, "ADD SAMPLE waveform: START flag -> " + juce::String(st, 3) + ", END flag -> " + juce::String(endF, 3) + ", right-click START " + juce::String(st3, 3) + ", right-drag LOOP " + juce::String(lst, 3) + ".." + juce::String(lend, 3)
          + " (on " + juce::String((int) loopOn) + "), the sample holds inside it (" + juce::String(phMin, 3) + ".." + juce::String(phMax, 3) + "), LOOP pill = one-shot " + juce::String((int) pillOff) + ", saved + restored " + juce::String((int) round)
          + ", no WAV grip / no drag-out " + juce::String((int) noGrip));
}

// the slot itself: tabs, ADD from WAV / AIFF / FLAC (+ MP3 reader), ROOT auto + override, the SAMPLE ghost's link + AS WAVE, the engine
static void testAddSample() {
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler"));
    std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    juce::String tabs; for (auto& t : ed->testTabs()) tabs << t << " ";
    const bool tabsOk = tabs.trim() == "GHOSTS MOVE FX PLAY BASS WOBBLE MORPH";
    // the old page numbers show GHOSTS (nothing is left behind them)
    ed->testShow(1, 0, 0); const bool oldPage = shown(ed->testField()); ed->testShow(0, 0, 0);
    // ADD from a file: classic, whole file, LOOP on, ROOT found (A1 = MIDI 45 -> PITCH +15), it HAUNTS ghost 1 and is heard, the SAMPLE ghost's panel shows
    juce::String files; bool filesOk = true; int roots[3] {};
    const char* exts[] = { ".wav", ".aif", ".flac" };
    for (int k = 0; k < 3; ++k) {
        const auto f = writeTestFile(exts[k]); ed->loadSample(f); roots[k] = p.detectedRoot;
        auto smp = p.engine.getSample(); const bool loaded = smp && smp->size() > 44100;
        filesOk = filesOk && loaded && roots[k] == 45; files << exts[k] << " root " << roots[k] << (loaded ? "" : " NOT LOADED") << "; "; f.deleteFile();
    }
    juce::AudioFormatManager fm; fm.registerBasicFormats(); const bool mp3 = fm.findFormatForFileExtension(".mp3") != nullptr;
    const bool setOk = (int) rawP(p, "smp_mode") == 0 && rawP(p, "smp_start") == 0.0f && rawP(p, "smp_len") == 1.0f && rawP(p, "smp_loop") > 0.5f && std::abs(rawP(p, "smp_pitch") - 15.0f) < 1e-4f
                       && rawP(p, "smp_level") > 0.01f && rawP(p, "smp_fm") > 0.01f && (int) rawP(p, "smp_fm_to") == 0 && p.sampleRoot() == 45;
    bool panelShown = true, ghostHidden = true;
    for (auto* c : ed->testSampleComps()) panelShown = panelShown && shown(c);
    if (auto* k = ed->testKnobSlider(gs::opId(0, "ratio"))) ghostHidden = !shown(k);
    // ROOT override (C3 -> PITCH 12) and AUTO again
    auto* root = ed->testSampleRoot(); root->setSelectedId(48 + 1, juce::sendNotificationSync); const float pOver = rawP(p, "smp_pitch");
    root->setSelectedId(1000, juce::sendNotificationSync); const float pAuto = rawP(p, "smp_pitch");
    const bool rootOk = std::abs(pOver - 12.0f) < 1e-4f && std::abs(pAuto - 15.0f) < 1e-4f && root->getText().contains("A1");
    // drag the SAMPLE ghost onto ghost 2: it haunts ghost 2; its link's dot sets HAUNT; double-click breaks it
    auto* field = ed->testField(); const juce::ModifierKeys L(juce::ModifierKeys::leftButtonModifier);
    const auto a = ed->testGhostPos(7), b = ed->testGhostPos(1);
    field->mouseDown(mouseEv(*field, a, a, L, false)); field->mouseDrag(mouseEv(*field, b, a, L, true)); field->mouseUp(mouseEv(*field, b, a, L, true));
    const int target = (int) rawP(p, "smp_fm_to");
    const auto h = ed->testLinkHandle(7, 1); const float fm0 = rawP(p, "smp_fm");
    field->mouseDown(mouseEv(*field, h, h, L, false)); field->mouseDrag(mouseEv(*field, h.translated(0, -40), h, L, true)); field->mouseUp(mouseEv(*field, h.translated(0, -40), h, L, true));
    const float fm1 = rawP(p, "smp_fm");
    field->mouseDoubleClick(mouseEv(*field, h, h, L, false, 2)); const float fm2 = rawP(p, "smp_fm");
    const bool linkOk = target == 1 && fm1 > fm0 + 0.1f && fm2 == 0.0f;
    // the engine: HAUNT on ghost 2 vs ghost 1 sound different; AS WAVE changes ghost 1's sound; Off = exactly as without it; no allocations
    auto renderWith = [&](int fmTo, float fm, int waveTo, long* allocs) {
        PoltergeistProcessor q; q.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); q.addSample(testPhrase(1.0));
        setP(q, "smp_level", 0.0f); setP(q, "smp_fm", fm); setP(q, "smp_fm_to", (float) fmTo); setP(q, "smp_wave_to", (float) waveTo);
        return renderHost(q, 44100, 140, legacyPhrase(), 2.0, nullptr, allocs); };
    long al = 0; const auto g1 = renderWith(0, 0.6f, 0, nullptr), g2 = renderWith(1, 0.6f, 0, nullptr), none = renderWith(0, 0.0f, 0, nullptr), w1 = renderWith(0, 0.0f, 1, &al);
    const double dTarget = diffDb(g1, g2), dWave = diffDb(none, w1);
    bool finite = true; double pk = 0; for (float x : w1) { finite = finite && std::isfinite(x); pk = juce::jmax(pk, (double) std::abs(x)); }
    PoltergeistProcessor q0; q0.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); q0.addSample(testPhrase(1.0)); setP(q0, "smp_level", 0.0f); setP(q0, "smp_fm", 0.0f);
    const auto off = renderHost(q0, 44100, 140, legacyPhrase(), 2.0);   // (smp_wave_to never touched: its default)
    const bool offSame = off.size() == none.size() && std::memcmp(off.data(), none.data(), off.size() * sizeof(float)) == 0;
    const bool engOk = dTarget > -40 && dWave > -40 && finite && pk > 0.01 && pk < 4.0 && offSame && al == 0;
    // CLEAR: no sample, the ghost panel's empty state; a fresh instance: Ghost 1 / Off (the old behaviour)
    ed->selectGhost(7); ed->testSampleButton(true)->onClick(); const bool cleared = p.engine.getSample() == nullptr && p.rawSample == nullptr;
    PoltergeistProcessor fresh; const bool defaults = (int) rawP(fresh, "smp_fm_to") == 0 && (int) rawP(fresh, "smp_wave_to") == 0;
    // factory presets that use the sample engine (they keep playing: no preset carries sample audio, the engine is unchanged)
    juce::StringArray usesSample;
    for (int i = 0; i < fresh.getNumPrograms(); ++i) { PoltergeistProcessor x; x.loadPreset(i); if ((int) rawP(x, "smp_mode") != 0 || rawP(x, "smp_level") > 0 || rawP(x, "smp_fm") > 0) usesSample.add(x.getProgramName(i)); }
    std::printf("   factory presets that set the sample engine: %s\n", usesSample.isEmpty() ? "none" : usesSample.joinIntoString(", ").toRawUTF8());
    check(tabsOk && oldPage && filesOk && mp3 && setOk && panelShown && ghostHidden && rootOk && linkOk && engOk && cleared && defaults,
          "ADD SAMPLE: tabs '" + tabs.trim() + "', old page numbers show GHOSTS " + juce::String((int) oldPage) + "; " + files + "MP3 reader " + juce::String((int) mp3) + "; set up as an FM source (classic, whole, LOOP, PITCH +15, heard + haunts ghost 1) "
          + juce::String((int) setOk) + ", its panel shows " + juce::String((int) panelShown) + " in place of the ghost knobs " + juce::String((int) ghostHidden) + "; ROOT override C3 -> PITCH " + juce::String(pOver, 0) + ", AUTO -> " + juce::String(pAuto, 0)
          + "; dragged onto ghost 2 -> haunts ghost " + juce::String(target + 1) + ", dot drag " + juce::String(fm0, 2) + " -> " + juce::String(fm1, 2) + ", double-click -> " + juce::String(fm2, 2)
          + "; engine: target ghost 2 vs 1 " + juce::String(dTarget, 1) + " dB apart, AS WAVE vs off " + juce::String(dWave, 1) + " dB, finite, peak " + juce::String(pk, 2) + ", default = untouched bit-exact " + juce::String((int) offSame)
          + ", " + juce::String(al) + " allocations; CLEAR " + juce::String((int) cleared) + "; new instance Ghost 1 / Off " + juce::String((int) defaults) + "; factory presets using the sample engine: " + juce::String(usesSample.size()));
}
// ================= SNAPSHOT (Shared/SnapshotBank.h)
static void testSnapshots() {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("polt-snap-" + juce::String(juce::Time::currentTimeMillis())); dir.deleteRecursively();
    const auto phrase = legacyPhrase(); const double secs = 2.0;
    std::vector<Stereo> refs; juce::StringArray names;
    {
        PoltergeistProcessor p; p.snapshots().setFolder(dir);
        p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); setP(p, "flt_cut", 0.3f);
        refs.push_back(renderProc(p, 44100, phrase, secs)); p.snapshots().take();
        p.loadPreset(4); auto smp = testPhrase(1.5); smp->name = "snap sample"; p.setSampleGhost(smp); setP(p, "smp_level", 0.8f);   // a sample-based sound
        refs.push_back(renderProc(p, 44100, phrase, secs)); p.snapshots().take();
        p.summon("dirty wobble"); setP(p, "flt_cut", 0.7f);
        refs.push_back(renderProc(p, 44100, phrase, secs)); p.snapshots().take("My wobble");
        for (int i = 0; i < 3; ++i) names.add(p.snapshots().info(2 - i).name);
    }
    dir.getChildFile("snapshot-junk.xml").replaceWithText("<GHOSTSNAPSHOT name=\"broken\"><PARAMS ");   // a damaged file
    // a new instance (restart): lists them, the damaged one skipped; each restores exactly (render compare)
    PoltergeistProcessor q; q.snapshots().setFolder(dir); auto& b = q.snapshots();
    double worst = -300; bool namesOk = true;
    juce::String per; for (int i = 0; i < 3; ++i) { q.loadPreset(7); b.load(2 - i); namesOk &= q.patchName == names[i]; const double d = diffDb(refs[(size_t) i], renderProc(q, 44100, phrase, secs)); per << juce::String(d, 1) << " "; worst = juce::jmax(worst, d); }
    b.load(1); auto smpBack = q.engine.getSample(); const bool sampleKept = smpBack && smpBack->name == "snap sample" && std::abs(smpBack->size() - (int) (1.5 * 44100)) < 2;
    // cycle with wrap
    b.load(0); b.next(); const int c1 = b.current(); b.next(); b.next(); const int c3 = b.current(); b.prev(); const int c4 = b.current();
    // the look and BYPASS stay as they are
    q.uiTheme = 1; q.bypassParam->setValueNotifyingHost(1.0f); b.load(1); const bool kept = q.uiTheme == 1 && q.bypassParam->get(); q.bypassParam->setValueNotifyingHost(0.0f);
    // loading while playing (the LOAD path): finite, how long it goes quiet
    double maxAbs = 0; int quiet = 0, longestQuiet = 0; bool finite = true;
    {
        q.setRateAndBufferSizeDetails(44100, 512); q.prepareToPlay(44100, 512); b.load(2);
        juce::AudioBuffer<float> buf(2, 512);
        for (int k = 0; k < 120; ++k) {
            juce::MidiBuffer m; if (k == 0) { m.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0); m.addEvent(juce::MidiMessage::noteOn(1, 55, 0.9f), 0); }
            if (k == 40) b.load(0); if (k == 80) b.load(1);
            buf.clear(); q.processBlock(buf, m);
            for (int i = 0; i < 512; ++i) { const float x = buf.getSample(0, i); if (!std::isfinite(x)) finite = false; maxAbs = juce::jmax(maxAbs, (double) std::abs(x));
                if (k > 10) { if (std::abs(x) < 1e-4f) longestQuiet = juce::jmax(longestQuiet, ++quiet); else quiet = 0; } }
        }
    }
    check(b.size() == 3 && b.corruptCount() == 1 && worst < -90 && namesOk && sampleKept && c1 == 1 && c3 == 0 && c4 == 2 && kept && finite,
          "SNAPSHOT: 3 taken, a new instance lists them (damaged file skipped " + juce::String(b.corruptCount()) + "), each restores exactly (worst render difference " + juce::String(worst, 1)
          + " dB: " + per + "), names " + names.joinIntoString(" / ") + ", the sample travels inside (" + juce::String((int) sampleKept) + "), cycle with wrap " + juce::String(c1) + juce::String(c3) + juce::String(c4)
          + ", look + BYPASS kept " + juce::String((int) kept) + "; loading while a chord plays: finite, peak " + juce::String(dbv(maxAbs), 1) + " dBFS, longest quiet gap " + juce::String(longestQuiet / 44.1, 1) + " ms (the LOAD path)");
    // the editor: SNAPSHOT button, key S, the SNAPSHOTS bank in the preset menu, < > cycle while a snapshot is loaded
    {
        std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(q.createEditor())); ed->setSize(1040, 744);
        ed->testSnapBar()->snapBtn.onClick(); ed->testKey(juce::KeyPress('s'));   // a plain S is a piano key: no snapshot
        ed->testKey(juce::KeyPress('s', juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier, 0));
        const int n = b.size(), inMenu = ed->testSnapshotMenuItems({}), found = ed->testSnapshotMenuItems("wobble");
        ed->testPresetMenuResult(300000 + b.indexOfName("My wobble")); const auto shown = ed->testPresetName();
        const int before = b.current(); ed->testPresetStep(1); const int after = b.current();
        ed->testPresetMenuResult(3); const bool left = b.current() == -1;   // a factory preset: out of the SNAPSHOTS bank
        check(n == 5 && inMenu == 5 && found == 1 && shown.contains("My wobble") && after == (before + 1) % n && left,
              "SNAPSHOT in the editor: button + Shift+Alt+S saved (a plain S does nothing) (" + juce::String(n) + " now), the preset menu's SNAPSHOTS bank lists " + juce::String(inMenu) + ", search finds 'wobble' " + juce::String(found)
              + ", loading one shows \"" + shown + "\", > steps to the next snapshot " + juce::String(before) + ">" + juce::String(after) + ", a factory preset leaves the bank " + juce::String((int) left));
    }
    dir.deleteRecursively();
}
// ================= MORPH BOARD (Shared/SpiritBoard.h + Morph.h)
static void testMorph() {
    // (the engine has free-running randomness: every render compare uses a FRESH processor's first render)
    const auto phrase = legacyPhrase(); const double secs = 1.6;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("polt-morph-" + juce::String(juce::Time::currentTimeMillis())); dir.deleteRecursively();
    juce::ValueTree stA, stB; juce::String snapFile;
    { PoltergeistProcessor p; p.snapshots().setFolder(dir);
      p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Grave Growler")); setP(p, "mono", 1.0f); stA = p.captureSound(); p.snapshots().take("A"); snapFile = p.snapshots().info(0).file.getFileName();
      p.loadPreset(5); setP(p, "mono", 0.0f); stB = p.captureSound(); }
    auto refOf = [&](const juce::ValueTree& st) { PoltergeistProcessor r; r.restoreSound(st); return renderProc(r, 44100, phrase, secs); };
    const auto refA = refOf(stA), refB = refOf(stB);
    // a board: ghost A from the snapshot, ghost B = the sound on the knobs
    auto make = [&](float x, float y, bool on = true) {
        auto p = std::make_unique<PoltergeistProcessor>(); p->snapshots().setFolder(dir); p->restoreSound(stB);
        p->morphAddGhost(0.2f, 0.5f, "snap:" + snapFile); p->morphAddGhost(0.8f, 0.5f, "current");
        setP(*p, "morph_on", on ? 1.0f : 0.0f); setP(*p, "morph_x", x); setP(*p, "morph_y", y); return p; };
    const double dA = diffDb(refA, renderProc(*make(0.2f, 0.5f), 44100, phrase, secs));
    const double dB = diffDb(refB, renderProc(*make(0.8f, 0.5f), 44100, phrase, secs));
    const double dOff = diffDb(refB, renderProc(*make(0.2f, 0.5f, false), 44100, phrase, secs));
    auto p = make(0.5f, 0.5f);
    auto setXY = [&](float x, float y) { setP(*p, "morph_x", x); setP(*p, "morph_y", y); };
    auto runBlocks = [&](int nb) { juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; bool fin = true;
                                   for (int k = 0; k < nb; ++k) { buf.clear(); p->processBlock(buf, m); for (int i = 0; i < 512; ++i) fin &= std::isfinite(buf.getSample(0, i)); } return fin; };
    p->setRateAndBufferSizeDetails(44100, 512); p->prepareToPlay(44100, 512); runBlocks(60);
    // the midpoint: continuous parameters = the average in the normalised domain (a parameter that differs: the first one found)
    const auto vA = p->morph.valuesFromState(stA), vB = p->morph.valuesFromState(stB);
    int ic = -1; for (int i = 0; i < p->morph.n; ++i) if (!p->morph.params[(size_t) i].discrete && !p->morph.params[(size_t) i].keep && std::abs(vA[(size_t) i] - vB[(size_t) i]) > 0.1f) { ic = i; break; }
    float want = 0, got = 0; juce::String pname;
    if (ic >= 0) { auto* cp = p->morph.params[(size_t) ic].prm; pname = cp->getParameterID(); want = cp->convertFrom0to1(0.5f * (vA[(size_t) ic] + vB[(size_t) ic])); got = p->morph.out[(size_t) ic].load(); }
    const bool midOk = ic >= 0 && std::abs(got - want) <= 1e-3f * juce::jmax(1.0f, std::abs(want));
    // discrete: the leading ghost with hysteresis (mono: A = on, B = off)
    const int im = p->morph.index["mono"]; auto monoNow = [&] { return p->morph.out[(size_t) im].load() > 0.5f; };
    setXY(0.2f, 0.5f); runBlocks(4); const bool h0 = monoNow();
    setXY(0.51f, 0.5f); runBlocks(4); const bool h1 = monoNow();   // B leads by 0.066: still A
    setXY(0.56f, 0.5f); runBlocks(4); const bool h2 = monoNow();   // B leads by 0.38: B
    setXY(0.49f, 0.5f); runBlocks(4); const bool h3 = monoNow();   // A leads by 0.066: still B
    const bool hyst = h0 && h1 && !h2 && !h3;
    // automation: X / Y moved by the host between blocks -> the live puck follows; an orbit + random moves never give NaN
    setXY(0.3f, 0.7f); runBlocks(2); const bool autoOk = std::abs(p->morph.shared.livePx.load() - 0.3f) < 1e-4f && std::abs(p->morph.shared.livePy.load() - 0.7f) < 1e-4f;
    p->board.orbit.mode = ghosty::spirit::Orbit::RandomWalk; p->board.orbit.sync = false; p->board.orbit.rate = 3; p->morphPublish();
    bool finite = true; juce::Random rnd(7);
    { juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0); juce::AudioBuffer<float> buf(2, 512); p->processBlock(buf, on); }
    for (int k = 0; k < 80; ++k) { setXY(rnd.nextFloat() * 1.4f - 0.2f, rnd.nextFloat()); finite &= runBlocks(1); }
    // state round trip (board + ghost sounds): a project saved at a puck position renders the same in a new instance; and a board file
    auto s1 = make(0.35f, 0.4f); juce::MemoryBlock mb; s1->getStateInformation(mb);
    PoltergeistProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    const double dRt = diffDb(renderProc(*s1, 44100, phrase, secs), renderProc(q, 44100, phrase, secs));
    const bool fileOk = s1->saveBoard("unit test board") && [&] { PoltergeistProcessor r; const bool ok = r.loadBoard(s1->boardsDir().getChildFile("unit test board.ghostboard")); return ok && r.board.ghosts.size() == 2 && r.ghostSounds.size() == 2; }();
    s1->boardsDir().getChildFile("unit test board.ghostboard").deleteFile();
    check(dA < -90 && dB < -90 && midOk && hyst && autoOk && finite && dRt < -90 && fileOk && dOff < -90 && q.board.ghosts.size() == 2,
          "MORPH BOARD: puck on ghost A (from a snapshot) = A " + juce::String(dA, 1) + " dB, on B = B " + juce::String(dB, 1) + " dB; midpoint " + pname + " " + juce::String(got, 3) + " (wanted " + juce::String(want, 3)
          + "); MONO follows the leader with hysteresis " + juce::String((int) h0) + juce::String((int) h1) + juce::String((int) h2) + juce::String((int) h3) + " (on, still on, off, still off); X / Y automation reaches the puck "
          + juce::String((int) autoOk) + "; random moves + orbit finite " + juce::String((int) finite) + "; project round trip " + juce::String(dRt, 1) + " dB, board file " + juce::String((int) fileOk) + "; MORPH off = the knobs " + juce::String(dOff, 1) + " dB");
    dir.deleteRecursively();
}
static void vintageDemos(const juce::File& dir) {
    // a clean source made here (a Poltergeist lead phrase over the synthesized test beat), then each character at full AMOUNT
    dir.createDirectory(); const double sr = 44100, bpm = 100;
    PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex("Glass Specter"));
    auto s = renderHost(p, sr, bpm, leadPhraseEvents(bpm), 60.0 / bpm * 16 + 1.0);
    addDrums(s, sr, bpm, 4, false);
    const int n = (int) s.size() / 2;
    writeWav(dir.getChildFile("vintage_0_clean.wav"), s, sr);
    const char* files[] = { "", "vintage_1_8bit_home_computer.wav", "vintage_2_12bit_classic_sampler.wav", "vintage_3_early90s_lofi.wav" };
    for (int m = 1; m <= 3; ++m) {
        Stereo y = s; vc::Character c; c.prepare(sr, 512); c.setMode(m); c.setAmount(1.0f);
        for (int a = 0; a < n; a += 512) { float* ch[2] = { y.data() + a, y.data() + n + a }; c.process(ch, 2, juce::jmin(512, n - a)); }
        writeWav(dir.getChildFile(files[m]), y, sr);
    }
}

static int runNew(int argc, char** argv) {
    const juce::String cmd = argc > 1 ? juce::String(argv[1]) : juce::String();
    if (cmd == "shots" && argc > 2) { bassShots(juce::File(argv[2])); return 0; }
    if (cmd == "demo" && argc > 2) { bassDemos(juce::File(argv[2])); return 0; }
    if (cmd == "leaddemo" && argc > 2) { leadDemos(juce::File(argv[2])); return 0; }
    if (cmd == "vintagedemo" && argc > 2) { vintageDemos(juce::File(argv[2])); return 0; }
    if (cmd == "plabdemo" && argc > 2) { plabDemo(juce::File(argv[2])); return 0; }
    if (cmd == "themeshots" && argc > 2) { themeShots(juce::File(argv[2])); return 0; }
    if (cmd == "tracershots" && argc > 2) { tracerShots(juce::File(argv[2])); return 0; }
    if (cmd == "leadshot" && argc > 2) { leadsShot(juce::File(argv[2])); return 0; }
    if (cmd == "tune") { tune(); return 0; }
    if (cmd == "probe" && argc > 2) {   // probe "Preset Name" id=value ... -> level / spectrum numbers
        PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets() + bassIndex(argv[2]));
        for (int a = 3; a < argc; ++a) { juce::String kv(argv[a]); setP(p, kv.upToFirstOccurrenceOf("=", false, false).toRawUTF8(), kv.fromFirstOccurrenceOf("=", false, false).getFloatValue()); }
        const auto m = measure(renderHost(p, 44100, 140, lineEvents(140), 60.0 / 140.0 * 8 + 0.6), 44100);
        std::printf("%s: peak %.2f rms %.2f side %.1f low %.1f%% hi %.1f%% finite %d\n", argv[2], m.peakDb, m.rmsDb, m.sideBelowDb, m.lowFrac * 100, m.hiFrac * 100, (int) m.finite);
        return 0;
    }
    if (cmd == "ref") return -1;
    if (cmd == "only" && argc > 2) {   // only NAME: one review-round check
        const juce::String w(argv[2]);
        const std::map<juce::String, void (*)()> t { { "bigblock", testBigBlock }, { "master", testMasterClean }, { "zipper", testZipper }, { "delay", testDelayRetrigger },
            { "steal", testStealFade }, { "pedal", testSustainPedal }, { "wobble", testWobblePage }, { "drive", testFilterDrive }, { "alias", testAliasing },
            { "quality", testQualityLevels }, { "nan", testNanGuard }, { "levelfm", testLevelModFm }, { "subenv", testSubFollows }, { "retrig", testMonoRetrig }, { "units", testUnits },
            { "presets", testPresets }, { "vintage", testVintage }, { "vintage2", testVintageEverywhere }, { "plab", testProcessLab }, { "bypass", testBypass }, { "sliders", testSliderConvention }, { "tracer", testTracer }, { "waveregion", testWaveRegionWiring }, { "addsample", testAddSample }, { "snapshot", testSnapshots }, { "morph", testMorph }, { "polish", testPolish }, { "theme", testThemeContrast }, { "themestate", testThemeState }, { "textpix", testThemeTextPixels }, { "opaque", testThemeOpaque }, { "tails", testTails }, { "cpu", testCpuWorst } };
        if (auto it = t.find(w); it != t.end()) it->second();
        std::printf("%d failure(s)\n", failures); return failures;
    }
    testPresets(); testTails(); testFormant(); testComb(); testWobbleSync(); testSubSolo(); testSummon(); testEnvLoop();
    testBigBlock(); testMasterClean(); testZipper(); testDelayRetrigger(); testStealFade(); testSustainPedal(); testWobblePage(); testFilterDrive();
    testAliasing(); testQualityLevels(); testNanGuard(); testLevelModFm(); testSubFollows(); testMonoRetrig(); testUnits(); testVintage(); testVintageEverywhere(); testProcessLab(); testBypass(); testThemeContrast(); testThemeState(); testThemeOpaque(); testThemeTextPixels(); testSliderConvention(); testPolish(); testTracer(); testWaveRegionWiring(); testAddSample(); testSnapshots(); testMorph();
    testCpuWorst();
    return -1;
}
