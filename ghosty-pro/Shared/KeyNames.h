#pragma once
// KEY-MATCHED DRUMS (owner 2026-10-30): "if any drum sound has a key, place the proper drum sounds in the proper song key: a kick from
// CHIMAERA and the song in E minor -> take the E minor one from CHIMAERA. Our plug-ins should always work like this."
// keyOfName reads a sound's key from its name, the LAST key word: "mau5 kick 07 Em", "808 Bass One Shot C", "pad Dbmaj", "Bass G# Minor".
// A bare letter counts only as the name's last word ("Kick 03 F"; not "Kick A Hard"). Same rules as Block Step / Ghost House (apply_keykit.py).
#include <juce_core/juce_core.h>
#include <regex>
#include <string>
#include <cctype>

namespace ghosty::keys {
struct NameKey { int root = -1; int quality = 0; bool valid() const { return root >= 0; } };   // quality: 1 minor, 2 major, 0 unknown
inline NameKey keyOfName(const juce::String& name) {
    std::string s = name.toStdString();
    for (const char* ext : { ".wav", ".WAV", ".aif", ".aiff", ".mp3", ".flac", ".ogg" }) { const std::string e(ext); if (s.size() > e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0) { s.resize(s.size() - e.size()); break; } }
    static const std::regex rx(R"((?:^|[\s_\-(\[.,])([A-G])(#|b|-?sharp|-?flat)?[\s_-]?(minor|major|min|maj|Minor|Major|Min|Maj|MINOR|MAJOR|MIN|MAJ|m)?(?=$|[\s_\-)\].,]))");
    NameKey last;
    for (auto it = std::sregex_iterator(s.begin(), s.end(), rx); it != std::sregex_iterator(); ++it) {
        const auto& m = *it; const std::string acc = m[2].str(); std::string q = m[3].str(); if (q != "m") for (auto& ch : q) ch = (char) std::tolower((unsigned char) ch);
        const bool end = (size_t) (m.position(0) + m.length(0)) >= s.size();
        if (acc.empty() && q.empty() && ! end) continue;
        if (acc.empty() && q.empty()) { const juce::String before = juce::String(s.substr(0, (size_t) m.position(0))).trimEnd().toLowerCase();
            bool skip = false; for (auto* w : { "take", "var", "version", "layer", "part", "kit", "vol", "v" }) if (before.endsWith(w)) skip = true; if (skip) continue; }
        static const int pc[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int r = pc[m[1].str()[0] - 'A'];
        if (acc == "#" || acc.find("sharp") != std::string::npos) ++r; else if (acc == "b" || acc.find("flat") != std::string::npos) --r;
        last.root = (r + 12) % 12; last.quality = (q == "m" || q == "min" || q == "minor") ? 1 : (q == "maj" || q == "major") ? 2 : 0;
    }
    return last;
}
inline const char* noteName(int r) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[(r % 12 + 12) % 12]; }
} // namespace ghosty::keys
