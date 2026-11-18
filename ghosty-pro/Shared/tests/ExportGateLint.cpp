// ExportGateLint: the source check behind the demo export gate (Shared/ExportGate.h, docs/DEMO-EXPORT-GATE.md).
// A new way to get audio or MIDI out of a plugin cannot slip in without a gate:
//   1. performExternalDragDropOfFiles may only appear in Shared/ExportGate.h (everything else calls exportgate::dragFiles
//      after ghosty::exportAllowed).
//   2. every saveMode FileChooser, clipboard write, audio writer (createWriterFor) and MIDI file write (.writeTo on a
//      MidiFile) carries an annotation on its line:  // GATE: <gate id>     (gated by that id)
//                                               or  // GATE: via-caller    (a helper; every caller asked the gate first)
//                                               or  // NOT-EXPORT: <why>   (presets / state / caches / sound files the plugin reads back)
//   3. every "<plugin>.<path>" gate id used in a plugin source is listed in that plugin's demogate test (the paths table),
//      every id in a test is used by a source, and every `// GATE:` id is a real gate id.
// Exit code = the number of problems. Run: ExportGateLint (the source folder is compiled in).
#include <juce_core/juce_core.h>
#include <cstdio>
#include <map>
#include <set>

#ifndef GHOSTY_SOURCE_DIR
 #error "GHOSTY_SOURCE_DIR must point at the ghosty-pro folder"
#endif

int main()
{
    const juce::File root (GHOSTY_SOURCE_DIR);
    int problems = 0;
    auto bad = [&] (const juce::String& m) { ++problems; std::printf ("LINT %s\n", m.toRawUTF8()); };
    const juce::StringArray dirs { "ArpEZator", "BeatsX", "GhostBox", "GhostSlice", "GhostyPro", "Mickens", "Poltergeist", "Shared", "SpectrumX" };
    const juce::StringArray prefixes { "arp", "beats", "box", "pro", "polt", "mick", "spec", "slice" };
    std::set<juce::String> srcIds, testIds, annotIds;
    std::map<juce::String, juce::String> where;
    int sites = 0;
    auto idsIn = [&] (const juce::String& line, std::set<juce::String>& into, const juce::String& loc)
    {
        int i = 0;
        while ((i = line.indexOfChar (i, '"')) >= 0)
        {
            const int j = line.indexOfChar (i + 1, '"'); if (j < 0) break;
            const auto lit = line.substring (i + 1, j);
            const auto pre = lit.upToFirstOccurrenceOf (".", false, false);
            if (lit.contains (".") && prefixes.contains (pre) && lit.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789.-")) { into.insert (lit); where[lit] = loc; }
            i = j + 1;
        }
    };
    for (auto& d : dirs)
        for (const auto& e : juce::RangedDirectoryIterator (root.getChildFile (d), true, "*.cpp;*.h;*.inc;*.hpp", juce::File::findFiles))
        {
            const auto f = e.getFile();
            const auto rel = f.getRelativePathFrom (root);
            if (rel.contains ("third_party") || rel.contains ("/legacy/")) continue;
            const bool isTest = rel.contains ("/tests/");
            const bool isGate = rel == "Shared/ExportGate.h";
            juce::StringArray lines; lines.addLines (f.loadFileAsString());
            const bool hasMidiFile = lines.joinIntoString ("\n").contains ("MidiFile");
            for (int n = 0; n < lines.size(); ++n)
            {
                const auto& L = lines[n]; const auto loc = rel + ":" + juce::String (n + 1);
                if (isTest) { if (L.contains ("demogate::Drag") || L.contains ("demogate::File") || L.contains ("demogate::Clipboard") || L.contains ("demogate::Auto")) idsIn (L, testIds, loc); continue; }
                const auto code = L.upToFirstOccurrenceOf ("//", false, false);
                const auto comment = L.fromFirstOccurrenceOf ("//", false, false);
                if (L.containsIgnoreCase ("xport") || L.contains ("ExportGate")) idsIn (code, srcIds, loc);
                const bool annotated = comment.contains ("GATE:") || comment.contains ("NOT-EXPORT:");
                if (comment.contains ("GATE:"))
                {
                    auto id = comment.fromFirstOccurrenceOf ("GATE:", false, false).trim().upToFirstOccurrenceOf (" ", false, false).trimCharactersAtEnd (",;)");
                    if (id != "via-caller") { annotIds.insert (id); where[id] = loc; }   // via-caller: a helper whose every caller asked the gate
                }
                if (isGate) continue;
                if (code.contains ("performExternalDragDropOfFiles")) bad (loc + ": raw external drag (use exportgate::dragFiles after ghosty::exportAllowed)");
                const bool saveChooser = code.contains ("saveMode");
                const bool clip = code.contains ("copyTextToClipboard");
                const bool writer = code.contains ("createWriterFor") || (code.contains ("replaceWithData") && (L.contains (".mid") || L.contains ("bytes") || L.contains ("wav")));
                const bool midiWrite = hasMidiFile && (code.contains (".writeTo") || code.contains ("->writeTo")) && ! code.contains ("xml") && ! code.contains ("Xml") && ! code.contains ("x.writeTo") && ! code.contains ("x->writeTo");
                if (saveChooser || clip || writer || midiWrite)
                {
                    ++sites;
                    if (! annotated) bad (loc + ": " + (saveChooser ? "save dialog" : clip ? "clipboard write" : writer ? "audio writer" : "MIDI file write") + " without '// GATE: <id>' or '// NOT-EXPORT: <why>'");
                }
            }
        }
    for (auto& id : srcIds) if (testIds.count (id) == 0) bad ("gate id '" + id + "' (" + where[id] + ") is not in any demogate test");
    for (auto& id : testIds) if (srcIds.count (id) == 0) bad ("demogate test id '" + id + "' (" + where[id] + ") is not used by any plugin source");
    for (auto& id : annotIds) if (srcIds.count (id) == 0) bad ("annotation '// GATE: " + id + "' (" + where[id] + ") names no gate id");
    std::printf ("ExportGateLint: %d gate ids in sources, %d in tests, %d annotated write / save / clipboard sites, %d problem%s\n",
                 (int) srcIds.size(), (int) testIds.size(), sites, problems, problems == 1 ? "" : "s");
    std::map<juce::String, int> per; for (auto& id : srcIds) per[id.upToFirstOccurrenceOf (".", false, false)]++;
    for (auto& [p, c] : per) std::printf ("  %-6s %d gated paths\n", p.toRawUTF8(), c);
    return problems;
}
