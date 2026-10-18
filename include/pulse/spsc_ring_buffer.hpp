// =============================================================================
//  pulse/spsc_ring_buffer.hpp  --  lock-free single-producer/single-consumer
//                                   queue
//
//  This is the primitive that lets a market-data thread hand messages to the
//  matching thread without ever taking a lock. Exactly one thread pushes and
//  exactly one thread pops; under that contract we can coordinate with two
//  atomic indices and the right memory ordering instead of a mutex.
//
//  WHY THE MEMORY ORDERING MATTERS
//  -------------------------------
//  The producer writes the payload into slot[i], THEN publishes the new head
//  with a release store. The consumer reads the head with an acquire load,
//  and the acquire/release pair guarantees that once the consumer sees the
//  new head, it also sees the payload write that happened-before it. Use the
//  wrong ordering (e.g. relaxed) and the consumer can read a slot before the
//  producer's write is visible -- a data race that shows up as corrupted
//  messages only under load, on only some CPUs. This pattern is the bread and
//  butter of low-latency C++.
//
//  FALSE SHARING
//  -------------
//  head_ and tail_ are padded to separate cache lines (alignas(64)). If they
//  shared a line, the producer touching head_ would invalidate the consumer's
//  cached copy of tail_ on every push (and vice versa), serialising the two
//  threads through the cache-coherence protocol -- "false sharing". Padding
//  keeps each thread's hot index on its own line.
// =============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace pulse {

// Capacity must be a power of two so we can wrap with a cheap bit-mask instead
// of a modulo. One slot is always left empty to distinguish "full" from
// "empty", so a buffer of Capacity holds up to Capacity-1 live elements.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    SpscRingBuffer() = default;
    SpscRingBuffer(const SpscRingBuffer&)            = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Producer side. Returns false if the queue is full (caller decides whether
    // to spin, drop, or back off).
    [[nodiscard]] bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        // acquire so we see the consumer's latest tail before deciding "full".
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);  // publish payload + slot
        return true;
    }

    // Consumer side. Returns std::nullopt if the queue is empty.
    [[nodiscard]] std::optional<T> pop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        // acquire so the payload write "happens-before" this read is visible.
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T item = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return item;
    }

    [[nodiscard]] bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Cache-line size. Pinned to 64 (true on all current x86-64 and Apple
    // silicon) rather than std::hardware_destructive_interference_size, whose
    // value can differ between translation units and trips an ABI warning.
    static constexpr std::size_t kLine = 64;

    std::array<T, Capacity> buffer_{};
    alignas(kLine) std::atomic<std::size_t> head_{0};  // written by producer
    alignas(kLine) std::atomic<std::size_t> tail_{0};  // written by consumer
};

}  // namespace pulse
