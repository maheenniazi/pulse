// =============================================================================
//  pulse/object_pool.hpp  --  a free-list allocator for hot-path objects
//
//  Why this exists: calling `new`/`delete` (or make_unique) for every single
//  order would hammer the global allocator and scatter orders all over the
//  heap, trashing the CPU cache. In a matching engine that processes millions
//  of messages, that allocation cost dominates. Instead we carve objects out
//  of big contiguous blocks and recycle freed slots through a free list, so a
//  steady-state allocate/free is just a couple of pointer swaps -- no syscalls,
//  no heap contention, and good cache locality.
// =============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace pulse {

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t block_size = 4096) : block_size_(block_size) {}

    // Non-copyable: the pool owns raw storage and hands out pointers into it.
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // Movable: the storage blocks live on the heap, so moving the owning
    // vector leaves every handed-out pointer (and the free list) valid.
    ObjectPool(ObjectPool&&)            = default;
    ObjectPool& operator=(ObjectPool&&) = default;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) {
        if (free_list_ == nullptr) {
            grow();
        }
        // Pop a recycled slot off the free list...
        Slot* slot = free_list_;
        free_list_ = slot->next;
        ++live_;
        // ...and construct a T in place (placement new).
        return std::construct_at(reinterpret_cast<T*>(slot), std::forward<Args>(args)...);
    }

    void deallocate(T* obj) noexcept {
        std::destroy_at(obj);
        Slot* slot = reinterpret_cast<Slot*>(obj);
        slot->next = free_list_;   // push the slot back onto the free list
        free_list_ = slot;
        --live_;
    }

    [[nodiscard]] std::size_t live() const noexcept { return live_; }

private:
    // Each slot is either a constructed T, or (when free) a node in the free
    // list. A union lets the same memory serve both roles -- the classic
    // free-list-overlaid-on-storage trick.
    union Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        Slot* next;
    };

    void grow() {
        auto block = std::make_unique<Slot[]>(block_size_);
        // Thread every slot in the fresh block onto the free list.
        for (std::size_t i = 0; i < block_size_; ++i) {
            block[i].next = free_list_;
            free_list_    = &block[i];
        }
        blocks_.push_back(std::move(block));
    }

    std::size_t                              block_size_;
    std::vector<std::unique_ptr<Slot[]>>     blocks_;      // owns all storage
    Slot*                                    free_list_{nullptr};
    std::size_t                              live_{0};
};

}  // namespace pulse
