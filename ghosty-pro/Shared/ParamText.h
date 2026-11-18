#pragma once
// Parameter text -> value helpers, so host text round-trips (value -> text -> value -> same text; clap-validator param-conversions).
#include <juce_core/juce_core.h>
#include <string>

namespace ghosty::ptext
{
// clap-juce-extensions passes the host's UTF-8 text to JUCE as a plain char* (read byte by byte), so "±7 ST" arrives as
// "Â±7 ST": put the bytes back together as UTF-8 (text that is already right, or plain ASCII, is returned as it is)
inline juce::String fromHost (const juce::String& t)
{
    std::string bytes; bool high = false;
    for (auto p = t.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c > 0xff) return t;
        if (c >= 0x80) high = true;
        bytes.push_back ((char) (unsigned char) c);
    }
    if (! high) return t;
    if (! juce::CharPointer_UTF8::isValidString (bytes.data(), (int) bytes.size())) return t;
    return juce::String::fromUTF8 (bytes.data(), (int) bytes.size());
}

// a choice's index from its text: exact item, then ignoring case, then a plain index number; otherwise the first item (as JUCE does)
inline int choiceIndex (const juce::StringArray& items, const juce::String& text)
{
    const auto s = fromHost (text).trim();
    int i = items.indexOf (s);
    if (i < 0) i = items.indexOf (s, true);
    if (i < 0 && s.isNotEmpty() && s.containsOnly ("0123456789")) i = s.getIntValue();
    return juce::jlimit (0, juce::jmax (0, items.size() - 1), juce::jmax (0, i));
}

// "C#3" / "Db3" / "C3" -> MIDI note (octaveForMiddleC: the octave number MIDI note 60 is shown with); -1 if it isn't a note name
inline int noteFromName (const juce::String& text, int octaveForMiddleC = 3)
{
    const auto s = fromHost (text).trim().toUpperCase();
    if (s.isEmpty()) return -1;
    static const int base[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto l = s[0];
    if (l < 'A' || l > 'G') return -1;
    int n = base[l - 'A'], k = 1;
    if (k < s.length() && (s[k] == '#' || s[k] == 0x266f)) { ++n; ++k; }
    else if (k < s.length() && (s[k] == 'B' || s[k] == 0x266d) && k + 1 < s.length()) { --n; ++k; }
    const auto oct = s.substring (k).trim();
    if (oct.isEmpty() || ! oct.containsOnly ("-0123456789")) return -1;
    return n + 12 * (oct.getIntValue() - octaveForMiddleC) + 60;
}
} // namespace ghosty::ptext
