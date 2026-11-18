/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * MORPH / BLEND BOARD work on Surge's own patch format (the "sub3" blob: header + XML +
 * wavetables), so every result is an ordinary patch that Surge itself loads, saves and shows in
 * DEEP mode. Nothing here runs on the audio thread: results are loaded through Surge's patch-file
 * queue (parsed on Surge's patch-load thread).
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTPATCHTOOLS_H
#define GHOSTBASS_SRC_GHOST_GHOSTPATCHTOOLS_H

#include <cstdint>
#include <string>
#include <vector>

class SurgeSynthesizer;

namespace ghost
{
using Blob = std::vector<uint8_t>;

namespace dna
{
Blob capture(SurgeSynthesizer *s);                       // the current patch (non-audio thread)
bool readFxp(const std::string &path, Blob &out);        // .fxp -> blob
bool writeFxp(const std::string &path, const Blob &b, const std::string &name);
// load a blob through Surge's patch-file queue (writes a temp .fxp in the user data folder)
bool queueLoad(SurgeSynthesizer *s, const Blob &b, const std::string &name);
// make sure every sample a blob refers to is in the bank (call off the audio thread)
void preloadSamples(SurgeSynthesizer *s, const void *data, int size);
std::string patchName(const Blob &b);
// MORPH (build 7): one patch that plays A and B. When they share their structure (every switch /
// type / choice equal, the same modulation routings, wavetables, LFO shapes and sounds) the result
// is A, with a <morph mode=1> listing B's value of each continuous control that differs (TRUE
// MORPH). Otherwise A's scene A stays, B's scene A becomes scene B (its parameters, modulation,
// wavetables, LFO shapes, sounds and its two scene insert FX, copied as Surge stores them) in
// Surge's Dual scene mode, <morph mode=2> (SCENE BLEND). why: what happened, in plain words.
Blob morphPatch(SurgeSynthesizer *s, const Blob &a, const Blob &b, const std::string &pathA, const std::string &nameA,
                const std::string &pathB, const std::string &nameB, int &mode, std::string &why,
                const std::vector<std::pair<std::string, std::string>> &extra = {});
// BLEND BOARD (build 11): 2 to 4 sounds in the board's corners. Two sounds: morphPatch (TRUE MORPH or SCENE BLEND) with the board's
// attributes. Three or four: the first sound (lowest corner) is the patch, and for every continuous control the value of each sound
// is listed (<morph mode=3>); a control of a module that one sound builds differently from the first stays at the first sound's value
// for that sound (so "shared controls" blend, differently built modules stay). mode: 1 true morph, 2 scene blend, 3 board; 0 = failed.
struct BoardSlot
{
    int corner;
    Blob blob;
    std::string path, name;
};
Blob boardPatch(SurgeSynthesizer *s, const std::vector<BoardSlot> &slots, float px, float py, int &mode, std::string &why);
} // namespace dna
} // namespace ghost

#endif
