// Ghosty Beats tests: the STYLE ENGINE (StyleGen.h): every kit's GENERATE <STYLE> and VARY. Included from BeatsTests.cpp after the helpers.
namespace S = gb::sty;

// one generation's main rows as x / . grids (a producer's view): kick, 808, snare, clap, hat, open hat, shaker and the first percussion
static void printStyleGrid(BeatsXProcessor& q, const gb::Pattern& pt, const std::array<uint8_t, gb::kTracks>& kinds) {
    for (int t = 0; t < gb::kTracks; ++t) {
        const auto& row = pt.steps[(size_t) t]; bool any = false; for (int s = 0; s < pt.length; ++s) any = any || row[(size_t) s].vel; if (!any) continue;
        std::printf("    %2d %-22s ", t + 1, q.model.tracks[(size_t) t].sound.substring(0, 22).toRawUTF8());
        const int L = pt.lenOf(t);
        for (int s = 0; s < pt.length; ++s) { if (s && s % 16 == 0) std::printf("|"); const auto& st = row[(size_t) (s % L)];
            char ch = '.'; if (st.vel) { ch = st.roll > 1 ? 'r' : (st.slide() ? '/' : (st.vel >= 110 ? 'X' : (st.vel >= 60 ? 'x' : 'o'))); if (st.flam) ch = 'f'; }
            std::printf("%c", ch); }
        std::printf("%s\n", kinds[(size_t) t] == S::KKick ? "  kick" : kinds[(size_t) t] == S::KBass ? "  808" : kinds[(size_t) t] == S::KSnare ? "  snare" : kinds[(size_t) t] == S::KHat ? "  hat" : "");
    }
}
static void styleGrid(const char* kit, int seeds) {   // (diagnostic) stylegrid KIT [N]: N generations printed as grids
    BeatsXProcessor q; q.applyKit(kitIndexOf(kit), true); setParam(q, "songmode", 0);
    const int st = q.kitStyle(); const auto kinds = S::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
    for (int i = 0; i < seeds; ++i) { q.generateForKit(); const auto& pt = q.model.patterns[(size_t) q.currentPattern()];
        std::printf("  %s seed %d: %s, %.0f BPM, %d steps\n", kit, q.model.toolSeed, S::isStyle(st) ? S::describe(st, q.model.toolSeed).toRawUTF8() : "own generator", q.apvts.getRawParameterValue("bpm")->load(), pt.length);
        printStyleGrid(q, pt, kinds); }
    q.varyPattern(); std::printf("  VARY (seed %d):\n", q.model.toolSeed); printStyleGrid(q, q.model.patterns[(size_t) q.currentPattern()], kinds);
}

static void testStyleEngine() {
    section("STYLE ENGINE: GENERATE <STYLE> for every kit: lengths, determinism, variation, style rules, locks, VARY, tempo, MIDI, renders");
    const auto& P = S::profiles(); std::array<bool, gb::kTracks> none {};
    // every kit maps to a generator: 40 profiles + the three kits with their own
    {   int withProfile = 0, own = 0; juce::StringArray missing;
        for (auto& k : gb::allKits()) { const int st = S::styleFor(k.name); if (S::isStyle(st)) { if (juce::String(S::profileOf(st).kit) == k.name) ++withProfile; else missing.add(k.name); } else ++own; }
        std::set<juce::String> names; for (auto& pr : P) names.insert(pr.kit);
        std::printf("  %d kits: %d with a profile, %d with their own generator%s%s | %d profiles, every profile a kit: %s\n", (int) gb::allKits().size(), withProfile, own, missing.isEmpty() ? "" : ", NO PROFILE: ", missing.joinIntoString(", ").toRawUTF8(), (int) P.size(), names.size() == P.size() ? "unique" : "DUPLICATE NAMES");
        int famMin = 99; for (auto& pr : P) famMin = std::min(famMin, (int) pr.fams.size());
        CHECK(withProfile + own == (int) gb::allKits().size() && own == 4 && missing.isEmpty() && names.size() == P.size() && (int) P.size() == (int) gb::allKits().size() - 4 && famMin >= 4, "every kit has a style (%d + %d), >= 4 families each (min %d)", withProfile, own, famMin);
        bool kitsExist = true; for (auto& pr : P) kitsExist = kitsExist && juce::String(gb::allKits()[(size_t) kitIndexOf(pr.kit)].name) == pr.kit; CHECK(kitsExist, "every profile names a real kit");
        // cells are well formed: 16 characters (after a prefix), kicks never on a main-snare 16th in the four-on-the-floor / half-time families' A cells
        int bad = 0; for (auto& pr : P) for (auto& f : pr.fams) { auto ok16 = [&](const char* c) { if (!c || !*c) return true; int w, from; const int len = S::cellLen(c, w, from); const int n = (int) std::strlen(c + from); return len > 0 ? n == len : n == 16; };
            for (auto* c : { f.kA, f.kB, f.sn, f.snB, f.p1, f.p2, f.p3, f.p4 }) if (!ok16(c)) { ++bad; std::printf("  bad cell in %s / %s: %s\n", pr.kit, f.name, c); } }
        CHECK(bad == 0, "%d malformed cells", bad);
    }
    // the four groups the style rules are checked on
    const std::set<juce::String> fourFloor { "House", "Tech House", "Afro House", "Club Progressive", "Big Room", "Electro House", "Melodic Techno", "Hardstyle", "ANALOG PUNCH" },
                                 halfTime { "Trap", "Trap Hard", "TRAP GOLD", "Dubstep", "Dubstep Heavy", "Riddim", "Brostep", "Future Bass", "Drill", "Drill Dark", "Cinematic" },
                                 boomBap { "Boom Bap", "Boom Bap Dusty", "BOOM BAP CLASSIC", "CRUNCH TWELVE", "SWING TWELVE" }, backbeat { "Pop", "POP SHINE", "Phonk", "Phonk Drift", "Garage", "Jersey Club", "TEXAS BOUNCE", "DIGITAL EIGHTIES", "ANALOG BOOM", "Lofi", "Lo-fi Tape" };
    const int N = 40; int kitsOk = 0, totalGen = 0, detBad = 0, rangeBad = 0, lenBad = 0, varyLow = 0, famLow = 0, ruleBad = 0, lockBad = 0, varyKickBad = 0, varyChangeLow = 0, tempoBad = 0, endBad = 0, silentRole = 0;
    for (int pi = 0; pi < (int) P.size(); ++pi) {
        const auto& pr = P[(size_t) pi]; const int style = S::kStyleBase + pi;
        BeatsXProcessor q; q.applyKit(kitIndexOf(pr.kit), true); setParam(q, "songmode", 0); const auto roles = q.roles();
        const auto kinds = S::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
        auto trackOf = [&](int kind) { for (int t = 0; t < gb::kTracks; ++t) if (kinds[(size_t) t] == kind) return t; return -1; };
        const int tK = trackOf(S::KKick), tS = trackOf(S::KSnare) >= 0 ? trackOf(S::KSnare) : trackOf(S::KClap), tH = trackOf(S::KHat);
        if (tK < 0 || tS < 0 || tH < 0) { std::printf("  %s: no kick / snare / hat track (%d %d %d)\n", pr.kit, tK, tS, tH); ++silentRole; }
        std::set<uint64_t> hashes; std::set<int> fams; int seedRule = 0, det = 0, range = 0, len = 0, ending = 0, ghosts = 0, swung = 0;
        for (int seed = 1; seed <= N; ++seed) {
            gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, style, seed, 0, &kinds); gb::Pattern again; again.clear(); gb::rhythm::generate(again, roles, none, style, seed, 0, &kinds);
            ++totalGen; fams.insert(S::family(pr, seed));
            if (std::memcmp(pt.steps.data(), again.steps.data(), sizeof(pt.steps)) == 0 && pt.length == again.length) ++det;
            if (pt.length == 64 && pt.res == 1 && pt.fillEvery == 0 && pt.seed == seed) ++len;
            uint64_t h = 1469598103934665603ULL; bool inRange = true;
            for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < gb::kMaxSteps; ++s) { const auto& st = pt.steps[(size_t) t][(size_t) s]; if (!st.vel) continue; h = (h ^ (uint64_t) (t * 131 + s)) * 1099511628211ULL; h = (h ^ st.vel) * 1099511628211ULL;
                if (s >= 64 || st.vel > 127 || st.roll < 1 || st.roll > 16 || st.nudge < -50 || st.nudge > 50 || st.pitch < -24 || st.pitch > 24 || st.rate > 64 || std::abs(st.velRamp) > 100 || std::abs(st.accel) > 100 || st.prob < 1 || st.prob > 100 || st.hold > gb::kMaxSteps) inRange = false; }
            if (inRange) ++range; hashes.insert(h);
            // style rules
            bool ok = true; auto kickAt = [&](int s) { return tK >= 0 && pt.steps[(size_t) tK][(size_t) s].vel > 0; }; auto snAt = [&](int s) { return tS >= 0 ? (int) pt.steps[(size_t) tS][(size_t) s].vel : 0; };
            if (fourFloor.count(pr.kit)) { for (int b = 0; b < 4; ++b) for (int k = 0; k < 16; k += 4) if (!(b == 3 && k >= 8) && !kickAt(b * 16 + k)) ok = false; }
            if (halfTime.count(pr.kit)) { for (int b = 0; b < 3; ++b) { const bool on3 = snAt(b * 16 + 8) >= 90 || snAt(b * 16 + 7) >= 90; if (!on3) ok = false; for (int k : { 4, 12 }) if (snAt(b * 16 + k) >= 90) ok = false; } }
            if (backbeat.count(pr.kit) || boomBap.count(pr.kit) || juce::String(pr.kit) == "Drum & Bass" || fourFloor.count(pr.kit)) {   // the backbeat family: snare / clap on 2 and 4 (unless the family is half time: Pop PP3, DIGITAL EIGHTIES DE5)
                const auto& F = pr.fams[(size_t) S::family(pr, seed)]; const bool ht = juce::String(F.sn) == "........X......."; if (!ht) for (int b = 0; b < 3; ++b) for (int k : { 4, 12 }) if (snAt(b * 16 + k) < 80) ok = false; }
            if (pr.flags & S::FNoKickUnderSnare) for (int s = 0; s < 64; ++s) if (kickAt(s) && pt.steps[(size_t) tK][(size_t) s].vel < 110 && snAt(s) >= 90 && !(s >= 56 && S::fillFrom(pr.fams[(size_t) S::family(pr, seed)].fill) <= s % 16)) ok = false;   // (an accented kick may sit under the snare: 4x4 garage)
            if (juce::String(pr.kit) == "Drum & Bass") for (int b = 0; b < 3; ++b) for (int k : { 4, 12 }) if (snAt(b * 16 + k) < 100) ok = false;
            if (tH >= 0 && pr.swHat >= 58) { int n = 0, sw = 0; for (int s = 0; s < 48; ++s) { const auto& st = pt.steps[(size_t) tH][(size_t) s]; if (st.vel && (s & 1)) { ++n; if (st.nudge >= 10) ++sw; } } if (n > 0 && sw == n) ++swung; else if (n > 0) ok = false; }
            { bool g = false; for (int t = 0; t < gb::kTracks; ++t) if (kinds[(size_t) t] == S::KSnare || kinds[(size_t) t] == S::KSnare2) for (int s = 0; s < 64; ++s) { const auto& st = pt.steps[(size_t) t][(size_t) s]; if (st.vel && st.vel < 60) g = true; } if (g) ++ghosts; }
            if (ok) ++seedRule;
            { const auto& F = pr.fams[(size_t) S::family(pr, seed)]; bool any = F.fill == S::FNone; for (int t = 0; t < gb::kTracks && !any; ++t) for (int s = 60; s < 64 && !any; ++s) if (pt.steps[(size_t) t][(size_t) s].vel) any = true;   // something plays into the next phrase
              if (any) ++ending; }
        }
        // locks: a locked hat track with a marker is untouched; a locked step too
        {   gb::Pattern pt; pt.clear(); std::array<bool, gb::kTracks> lk {}; if (tH >= 0) { lk[(size_t) tH] = true; pt.steps[(size_t) tH][3].vel = 77; }
            if (tK >= 0) { pt.steps[(size_t) tK][5].vel = 66; pt.steps[(size_t) tK][5].flags |= 2; }
            gb::rhythm::generate(pt, roles, lk, style, 7, 0, &kinds);
            bool okL = tH < 0 || (pt.steps[(size_t) tH][3].vel == 77 && pt.steps[(size_t) tH][0].vel == 0); if (tK >= 0) okL = okL && pt.steps[(size_t) tK][5].vel == 66 && pt.steps[(size_t) tK][5].locked();
            if (!okL) { ++lockBad; std::printf("  %s: locks not kept\n", pr.kit); } }
        // VARY keeps the kick row and the family, changes the rest; the processor: tempo lane
        int varyKickOk = 0, varyChanged = 0; { for (int seed = 1; seed <= 10; ++seed) { gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, style, seed, 0, &kinds); gb::Pattern v = pt; gb::rhythm::generate(v, roles, none, S::kVaryBase + pi, seed + 500, 0, &kinds);
            bool kickSame = tK < 0 || std::memcmp(pt.steps[(size_t) tK].data(), v.steps[(size_t) tK].data(), sizeof(gb::Step) * 64) == 0; if (kickSame && v.seed == pt.seed && v.length == 64) ++varyKickOk;
            if (gb::rhythm::hamming(pt, v) >= 2 || (tH < 0 && tS < 0)) ++varyChanged; } }
        int tempoOk = 0; std::set<int> tempos; for (int seed = 1; seed <= 30; ++seed) { const int b = S::tempoFor(style, seed); tempos.insert(b); if (b >= pr.bpmLo && b <= pr.bpmHi) ++tempoOk; }
        q.model.toolSeed = 11; q.generatePattern(style); const float bpm = q.apvts.getRawParameterValue("bpm")->load(); const bool bpmOk = std::lround(bpm) == S::tempoFor(style, q.model.toolSeed) && q.model.patterns[(size_t) q.currentPattern()].length == 64;
        const bool okKit = det == N && range == N && len == N && (int) hashes.size() >= N * 3 / 4 && (int) fams.size() == (int) pr.fams.size() && seedRule == N && lockBad == 0 && varyKickOk == 10 && varyChanged >= 9 && tempoOk == 30 && bpmOk && ending == N && (!boomBap.count(pr.kit) || ghosts >= N * 4 / 5);
        if (okKit) ++kitsOk;
        detBad += N - det; rangeBad += N - range; lenBad += N - len; if ((int) hashes.size() < N * 3 / 4) ++varyLow; if ((int) fams.size() != (int) pr.fams.size()) ++famLow; ruleBad += N - seedRule; varyKickBad += 10 - varyKickOk; if (varyChanged < 9) ++varyChangeLow; tempoBad += (30 - tempoOk) + (bpmOk ? 0 : 1); endBad += N - ending;
        std::printf("  %-18s %2d fam | %d seeds: det %2d range %2d len %2d distinct %2d rules %2d ending %2d ghosts %2d swung %2d | VARY kick kept %2d/10 changed %2d/10 | tempo %d-%d (%d values) proc %.0f BPM %s\n",
                    pr.kit, (int) pr.fams.size(), N, det, range, len, (int) hashes.size(), seedRule, ending, ghosts, swung, varyKickOk, varyChanged, pr.bpmLo, pr.bpmHi, (int) tempos.size(), bpm, okKit ? "ok" : "<-- CHECK");
    }
    std::printf("  %d / %d kits pass everything; %d generations: non-deterministic %d, out of range %d, wrong length %d, too alike %d, families missing %d, rule breaks %d, no phrase ending %d | locks broken %d, VARY kick changed %d, VARY too alike %d, tempo misses %d, missing roles %d\n",
                kitsOk, (int) P.size(), totalGen, detBad, rangeBad, lenBad, varyLow, famLow, ruleBad, endBad, lockBad, varyKickBad, varyChangeLow, tempoBad, silentRole);
    CHECK(detBad == 0 && rangeBad == 0 && lenBad == 0, "deterministic, in range, 4 bars (%d / %d / %d misses)", detBad, rangeBad, lenBad);
    CHECK(varyLow == 0 && famLow == 0, "real variation across seeds (%d kits too alike, %d kits missing families)", varyLow, famLow);
    CHECK(ruleBad == 0, "style rules (four on the floor / half time / backbeat / no kick under the snare / swung garage hats / D&B snares): %d breaks", ruleBad);
    CHECK(endBad == 0 && lockBad == 0 && silentRole == 0, "phrase endings %d missing, locks %d broken, roles %d missing", endBad, lockBad, silentRole);
    CHECK(varyKickBad == 0 && varyChangeLow == 0 && tempoBad == 0, "VARY keeps the kick (%d misses) and changes the rest (%d kits too alike); tempo lanes (%d misses)", varyKickBad, varyChangeLow, tempoBad);
    CHECK(kitsOk == (int) P.size(), "%d / %d kits pass", kitsOk, (int) P.size());
    // the kits with their own generator route to it, and the existing generators' output is untouched by the routing
    {   int ok = 0; for (auto* k : { "DRILL", "LATIN", "TRAP CLASSIC", "Reggaeton" }) { const int st = S::styleFor(k); ok += (juce::String(k) == "DRILL" && st == gb::drill::kStyle) || ((juce::String(k) == "LATIN" || juce::String(k) == "Reggaeton") && st == gb::latin::kStyle) || (juce::String(k) == "TRAP CLASSIC" && st == gb::trapc::kStyle); }
        BeatsXProcessor q; q.applyKit(kitIndexOf("TRAP CLASSIC"), true); setParam(q, "songmode", 0); const auto tk = gb::trapc::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
        gb::Pattern a; a.clear(); gb::rhythm::generate(a, q.roles(), none, gb::trapc::kStyle, 10, 0, &tk); q.model.toolSeed = 9; q.generateForKit(); const auto& b = q.model.patterns[(size_t) q.currentPattern()];
        const bool same = std::memcmp(a.steps.data(), b.steps.data(), sizeof(a.steps)) == 0;
        std::printf("  own generators routed %d / 4; GENERATE on the TRAP CLASSIC kit == trapc::generate(seed 10): %s\n", ok, same ? "yes" : "NO");
        CHECK(ok == 4 && same, "DRILL / LATIN / TRAP CLASSIC keep their generators");
    }
    // a user kit with unknown sounds: the kinds come from the roles; starter beats A / B of every kit are untouched by this build (pinned by the kit tests elsewhere)
    {   BeatsXProcessor q; q.applyKit(kitIndexOf("Trap"), true); auto k = S::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
        std::printf("  Trap kit kinds:"); for (int t = 0; t < gb::kTracks; ++t) std::printf(" %d", k[(size_t) t]); std::printf("\n");
        CHECK(k[0] == S::KBass && k[1] == S::KKick && k[2] == S::KSnare && k[3] == S::KClap && k[4] == S::KHat && k[5] == S::KOpen && k[6] == S::KRim && k[10] == S::KCrash && k[11] == S::KCowbell && k[12] == S::KShaker && k[13] == S::KSnap && k[15] == S::KSwell, "Trap kit: every slot its part");
        gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, q.roles(), none, S::kStyleBase, 3, 0, nullptr); int hits = 0; for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < 64; ++s) hits += pt.steps[(size_t) t][(size_t) s].vel > 0;
        CHECK(hits > 40, "kinds from the roles alone still make a beat (%d hits)", hits);
        pt.clear(); pt.res = 2; pt.length = 32; gb::rhythm::generate(pt, q.roles(), none, S::kStyleBase, 3, 0, &k); CHECK(pt.length == 128 && pt.res == 2, "a 1/32 pattern gets the 4 bars on its own grid (%d steps)", pt.length);
    }
    // MIDI export of a generated beat, and offline renders of a few styles: not silent, not clipping
    {   BeatsXProcessor q; q.applyKit(kitIndexOf("Jersey Club"), true); setParam(q, "songmode", 0); q.model.toolSeed = 4; q.generateForKit(); q.midiChannel = 10; auto f = q.exportMidi(false);
        juce::MidiFile mf; juce::FileInputStream in(f); int notes = 0; if (in.openedOk() && mf.readFrom(in)) for (int tr = 0; tr < mf.getNumTracks(); ++tr) for (auto* e : *mf.getTrack(tr)) if (e->message.isNoteOn()) ++notes;
        std::printf("  MIDI export of a generated Jersey Club beat: %d notes\n", notes); CHECK(notes >= 60, "MIDI export after GENERATE (%d notes)", notes);
    }
    {   int ok = 0; for (auto* k : { "Trap", "Drill", "Boom Bap", "House", "Jersey Club", "Garage", "Drum & Bass", "Cinematic", "IDM Glitch", "ANALOG BOOM" }) {
            BeatsXProcessor q; q.applyKit(kitIndexOf(k), true); setParam(q, "songmode", 0); q.model.toolSeed = 5; q.generateForKit(); q.publish();
            const auto b = renderBars(q, 4); const double l = lufs(b); const float tp = truePeak4x(b), pk = peakOf(b);
            std::printf("  render %-12s %.1f LUFS, sample peak %.2f dBFS, true peak %.2f dBFS\n", k, l, juce::Decibels::gainToDecibels(pk), juce::Decibels::gainToDecibels(tp));
            if (l > -30 && l < -6 && pk <= 1.0f) ++ok; }   // (audible, and the limiter holds the samples inside full scale; the inter-sample peak is printed for information)
        CHECK(ok == 10, "renders of generated beats: %d / 10 audible and not clipping", ok);
    }
}
