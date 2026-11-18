// SEANCE: Vital's synthesis code includes Projucer's "JuceHeader.h" only for a few macros. This stands in for it,
// so the oscillator builds without JUCE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <atomic>
#include <complex>
#include <utility>
#include <type_traits>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <climits>
#include <vector>
#include <map>
#include <set>
#include <random>
#if defined(__APPLE__)
#define JUCE_MAC 1
// Vital's FFT wrapper uses Accelerate on a Mac and includes it from inside its own namespace; JUCE had already
// included it globally. Do the same here.
#include <Accelerate/Accelerate.h>
#endif
#ifndef JUCE_LEAK_DETECTOR
#define JUCE_LEAK_DETECTOR(x)
#endif
#ifndef JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x) x(const x &) = delete; x &operator=(const x &) = delete;
#endif

// ---- for Vital's wavetable-building code (src/common/wavetable): the few JUCE things it uses, in plain C++ ----
class String
{
  public:
    String() = default;
    String(const char *c) : s(c) {}
    String(const std::string &x) : s(x) {}
    std::string toStdString() const { return s; }

  private:
    std::string s;
};
class MemoryOutputStream
{
  public:
    explicit MemoryOutputStream(size_t reserve = 0) { d.reserve(reserve); }
    const void *getData() const { return d.data(); }
    size_t getDataSize() const { return d.size(); }
    void push(char c) { d.push_back(c); }

  private:
    std::vector<char> d;
};
struct Base64 // standard base64, as juce::Base64
{
    static bool convertFromBase64(MemoryOutputStream &out, const std::string &in)
    {
        unsigned acc = 0;
        int bits = 0;
        for (unsigned char c : in)
        {
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') break;
            else continue;
            acc = (acc << 6) | (unsigned)v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push((char)((acc >> bits) & 0xff));
            }
        }
        return true;
    }
    static String toBase64(const void *data, size_t n)
    {
        static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const unsigned char *p = (const unsigned char *)data;
        std::string o;
        for (size_t i = 0; i < n; i += 3)
        {
            const unsigned b = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0u) | (i + 2 < n ? (unsigned)p[i + 2] : 0u);
            o += t[(b >> 18) & 63];
            o += t[(b >> 12) & 63];
            o += i + 1 < n ? t[(b >> 6) & 63] : '=';
            o += i + 2 < n ? t[b & 63] : '=';
        }
        return String(o);
    }
};
namespace ProjectInfo
{
static const char *const versionString = "1.5.5";
}

