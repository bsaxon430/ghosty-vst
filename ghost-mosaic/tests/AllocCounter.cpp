// Ghost Mosaic tests: counts heap allocations made on a thread while counting is switched on.
#include <atomic>
#include <cstdlib>
#include <new>

namespace alloccount
{
thread_local bool tCounting = false;
std::atomic<long> gCount { 0 };
void start() { tCounting = true; }
long stop() { tCounting = false; return gCount.exchange (0); }
}

static void* countedAlloc (std::size_t n)
{
    if (alloccount::tCounting) alloccount::gCount.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
static void* countedAlignedAlloc (std::size_t n, std::size_t a)
{
    if (alloccount::tCounting) alloccount::gCount.fetch_add (1);
    void* p = nullptr;
    if (posix_memalign (&p, a < sizeof (void*) ? sizeof (void*) : a, n ? n : 1) != 0) throw std::bad_alloc();
    return p;
}

void* operator new (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new (std::size_t n, std::align_val_t a) { return countedAlignedAlloc (n, (std::size_t) a); }
void* operator new[] (std::size_t n, std::align_val_t a) { return countedAlignedAlloc (n, (std::size_t) a); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
