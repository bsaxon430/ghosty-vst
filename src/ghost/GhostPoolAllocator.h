/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * A fixed-pool node allocator for SurgeSynthesizer's voice lists. Upstream keeps the playing
 * voices in std::list, so every note-on allocated a list node on the audio thread (measured:
 * one heap allocation per note in surge-xt-tests "[ghostbass]"). With this allocator each list
 * gets its own pool, made when the synth is constructed, and note-ons take nodes from it.
 * Falls back to the heap only if more than kBlocks nodes are alive at once (never, in practice:
 * Surge caps voices far below that). Each list is only ever touched by one thread at a time
 * (the audio thread, or the owner during construction / destruction), as upstream assumes.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTPOOLALLOCATOR_H
#define GHOSTBASS_SRC_GHOST_GHOSTPOOLALLOCATOR_H

#include <cstddef>
#include <memory>
#include <new>

namespace ghost
{
struct NodePool
{
    static constexpr size_t kBlock = 64, kBlocks = 512;
    alignas(std::max_align_t) unsigned char mem[kBlock * kBlocks];
    void *freeList{nullptr};
    NodePool()
    {
        for (size_t i = kBlocks; i-- > 0;)
        {
            void *b = mem + i * kBlock;
            *reinterpret_cast<void **>(b) = freeList;
            freeList = b;
        }
    }
    bool owns(const void *p) const
    {
        auto *c = static_cast<const unsigned char *>(p);
        return c >= mem && c < mem + sizeof(mem);
    }
};

template <typename T> struct PoolAllocator
{
    using value_type = T;
    std::shared_ptr<NodePool> pool;

    PoolAllocator() : pool(std::make_shared<NodePool>()) {}
    template <typename U> PoolAllocator(const PoolAllocator<U> &o) noexcept : pool(o.pool) {}

    T *allocate(size_t n)
    {
        if (n == 1 && sizeof(T) <= NodePool::kBlock && alignof(T) <= alignof(std::max_align_t) &&
            pool->freeList)
        {
            void *b = pool->freeList;
            pool->freeList = *reinterpret_cast<void **>(b);
            return static_cast<T *>(b);
        }
        return std::allocator<T>().allocate(n);
    }
    void deallocate(T *p, size_t n) noexcept
    {
        if (pool->owns(p))
        {
            *reinterpret_cast<void **>(p) = pool->freeList;
            pool->freeList = p;
            return;
        }
        std::allocator<T>().deallocate(p, n);
    }
    template <typename U> bool operator==(const PoolAllocator<U> &o) const noexcept
    {
        return pool == o.pool;
    }
    template <typename U> bool operator!=(const PoolAllocator<U> &o) const noexcept
    {
        return pool != o.pool;
    }
};
} // namespace ghost

#endif
