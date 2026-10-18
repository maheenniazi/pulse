// =============================================================================
//  tests/test_spsc.cpp  --  correctness tests for the lock-free SPSC queue
// =============================================================================
#include "pulse/spsc_ring_buffer.hpp"
#include "test_framework.hpp"

#include <thread>
#include <vector>

using namespace pulse;

TEST(spsc_single_threaded_fifo) {
    SpscRingBuffer<int, 8> q;
    CHECK(q.empty());
    for (int i = 0; i < 7; ++i) CHECK(q.push(i));  // capacity-1 usable slots
    CHECK(!q.push(99));                            // full
    for (int i = 0; i < 7; ++i) {
        auto v = q.pop();
        CHECK(v.has_value());
        CHECK_EQ(v.value(), i);                    // strict FIFO order
    }
    CHECK(!q.pop().has_value());                   // empty again
}

// The real test: two threads, no locks, and every item must arrive exactly
// once and in order. Built with -fsanitize=thread in CI, this flushes out any
// missing acquire/release ordering as a reported data race.
TEST(spsc_concurrent_producer_consumer) {
    constexpr int N = 1'000'000;
    SpscRingBuffer<int, 1024> q;

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) { /* spin until the consumer drains a slot */ }
        }
    });

    int  next_expected = 0;     // the value we require next if order holds
    bool in_order      = true;
    std::thread consumer([&] {
        int received = 0;
        while (received < N) {
            if (auto v = q.pop()) {
                if (v.value() != next_expected) in_order = false;
                ++next_expected;
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    CHECK(in_order);                  // every value arrived in producer order
    CHECK_EQ(next_expected, N);       // and all N of them arrived exactly once
}
