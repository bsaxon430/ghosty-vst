// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Compat.h"
#include <sndfile.h>
#include <lo/lo.h>
#include <fftw3.h>
#include <cstdarg>
#include <map>

namespace mosaic::compat
{
namespace
{
struct MemSound { juce::AudioBuffer<float> audio; double rate = 44100; };
std::mutex& memLock() { static std::mutex m; return m; }
std::map<juce::String, std::shared_ptr<MemSound>>& memSounds() { static std::map<juce::String, std::shared_ptr<MemSound>> m; return m; }
thread_local double tEngineRate = 0;
thread_local StatusSink* tSink = nullptr;
}

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager* fm = [] { auto* f = new juce::AudioFormatManager(); f->registerBasicFormats(); return f; }();
    return *fm;
}

void putMemorySound (const juce::String& key, const juce::AudioBuffer<float>& audio, double rate)
{
    auto s = std::make_shared<MemSound>();
    s->audio = audio; s->rate = rate;
    std::lock_guard<std::mutex> l (memLock());
    memSounds()[key] = s;
}
void removeMemorySound (const juce::String& key) { std::lock_guard<std::mutex> l (memLock()); memSounds().erase (key); }
bool hasMemorySound (const juce::String& key) { std::lock_guard<std::mutex> l (memLock()); return memSounds().count (key) > 0; }
void setThreadEngineRate (double r) { tEngineRate = r; }
void setThreadStatusSink (StatusSink* s) { tSink = s; }

void initFftThreads()
{
    static std::once_flag once;
    std::call_once (once, [] { fftw_make_planner_thread_safe(); });
}

static void resampleInto (const juce::AudioBuffer<float>& in, double inRate, double outRate, Audio& out)
{
    const int ch = in.getNumChannels();
    const int64_t n = in.getNumSamples();
    out.channels = ch;
    if (outRate <= 0 || std::abs (inRate - outRate) < 0.5)
    {
        out.rate = (int) std::lround (inRate);
        out.frames = n;
        out.interleaved.resize ((size_t) (n * ch));
        for (int c = 0; c < ch; ++c)
        {
            auto* src = in.getReadPointer (c);
            for (int64_t i = 0; i < n; ++i) out.interleaved[(size_t) (i * ch + c)] = src[i];
        }
        return;
    }
    const double ratio = inRate / outRate;
    const int64_t m = (int64_t) std::floor ((double) n / ratio);
    out.rate = (int) std::lround (outRate);
    out.frames = m;
    out.interleaved.assign ((size_t) (m * ch), 0.0f);
    std::vector<float> tmp ((size_t) m), padded ((size_t) n + 256, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + n, padded.begin());
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, padded.data(), tmp.data(), (int) m, (int) padded.size(), 0);
        for (int64_t i = 0; i < m; ++i) out.interleaved[(size_t) (i * ch + c)] = tmp[(size_t) i];
    }
}

bool readAudio (const juce::String& pathOrKey, Audio& out, juce::String* error)
{
    if (pathOrKey.startsWith ("mem:"))
    {
        std::shared_ptr<MemSound> s;
        { std::lock_guard<std::mutex> l (memLock()); auto it = memSounds().find (pathOrKey); if (it != memSounds().end()) s = it->second; }
        if (s == nullptr) { if (error) *error = "missing sound " + pathOrKey; return false; }
        resampleInto (s->audio, s->rate, tEngineRate, out);
        return true;
    }
    juce::File f (pathOrKey);
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr) { if (error) *error = "can't read " + f.getFileName(); return false; }
    const int64_t maxFrames = (int64_t) (r->sampleRate * 60.0 * 20.0);   // 20 minutes per sound at most
    const int64_t n = juce::jmin ((int64_t) r->lengthInSamples, maxFrames);
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, r->numChannels), (int) n);
    r->read (&buf, 0, (int) n, 0, true, true);
    resampleInto (buf, r->sampleRate, tEngineRate, out);
    return true;
}
} // namespace mosaic::compat

// ---------------------------------------------------------------- libsndfile stand-in
struct SNDFILE_tag { mosaic::compat::Audio audio; int64_t readPos = 0; };
static thread_local std::unique_ptr<SNDFILE_tag> tLastOpened;   // brain::load_sound never closes: freed on the next open

SNDFILE* sf_open (const char* path, int mode, SF_INFO* info)
{
    if (mode != SFM_READ || path == nullptr || info == nullptr) return nullptr;
    auto f = std::make_unique<SNDFILE_tag>();
    if (! mosaic::compat::readAudio (juce::String::fromUTF8 (path), f->audio)) return nullptr;
    info->frames = f->audio.frames;
    info->channels = f->audio.channels;
    info->samplerate = f->audio.rate;
    info->format = 0; info->sections = 1; info->seekable = 1;
    tLastOpened = std::move (f);
    return tLastOpened.get();
}

sf_count_t sf_read_float (SNDFILE* f, float* ptr, sf_count_t items)
{
    if (f == nullptr) return 0;
    const auto total = (sf_count_t) f->audio.interleaved.size();
    const auto n = juce::jmin (items, total - f->readPos);
    if (n <= 0) return 0;
    std::copy (f->audio.interleaved.begin() + f->readPos, f->audio.interleaved.begin() + f->readPos + n, ptr);
    f->readPos += n;
    return n;
}

int sf_close (SNDFILE* f)
{
    if (f != nullptr && f == tLastOpened.get()) tLastOpened.reset();
    return 0;
}

// ---------------------------------------------------------------- liblo stand-in (status reports)
struct lo_address_compat { juce::String url; };

lo_address lo_address_new_from_url (const char* url) { return new lo_address_compat { juce::String::fromUTF8 (url ? url : "") }; }
void lo_address_free (lo_address a) { delete a; }

int lo_send (lo_address, const char* path, const char* types, ...)
{
    auto* sink = mosaic::compat::tSink;
    if (sink == nullptr || ! *sink) return 0;
    juce::StringArray args;
    va_list ap;
    va_start (ap, types);
    for (const char* t = types; t != nullptr && *t; ++t)
    {
        if (*t == 's') args.add (juce::String::fromUTF8 (va_arg (ap, const char*)));
        else if (*t == 'i') args.add (juce::String (va_arg (ap, int)));
        else if (*t == 'f') args.add (juce::String (va_arg (ap, double)));
    }
    va_end (ap);
    (*sink) (juce::String::fromUTF8 (path), args);
    return 0;
}
