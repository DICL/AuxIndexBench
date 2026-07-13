// mpmc_queue.hpp - Bounded lock-free MPMC queue (Vyukov ring buffer).
//
// Each cell carries a monotonically advancing sequence number;
// producers and consumers use a single CAS per operation and busy-spin
// on contention or empty/full state.
//
// We deliberately do not use std::mutex, std::condition_variable, futex,
// or std::atomic_wait — the prompt asks for busy-waiting CAS contention
// so we can observe queueing delay precisely.
//
// Capacity must be a power of two. T must be trivially copyable.

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>

namespace aib {

constexpr size_t CACHELINE = 64;

template <typename T>
class MPMCQueue {
public:
    explicit MPMCQueue(size_t capacity)
        : capacity_(capacity), mask_(capacity - 1) {
        assert((capacity & (capacity - 1)) == 0 && "capacity must be power of 2");
        buffer_ = static_cast<Cell*>(
            ::operator new[](sizeof(Cell) * capacity, std::align_val_t{CACHELINE}));
        for (size_t i = 0; i < capacity_; ++i) {
            new (&buffer_[i]) Cell();
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
        enq_pos_.store(0, std::memory_order_relaxed);
        deq_pos_.store(0, std::memory_order_relaxed);
    }

    ~MPMCQueue() {
        for (size_t i = 0; i < capacity_; ++i) buffer_[i].~Cell();
        ::operator delete[](buffer_, std::align_val_t{CACHELINE});
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    bool try_enqueue(const T& v) {
        Cell* cell;
        size_t pos = enq_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            if (diff == 0) {
                if (enq_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) break;
            } else if (diff < 0) {
                return false;
            } else {
                pos = enq_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->value = v;
        cell->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool try_dequeue(T& out) {
        Cell* cell;
        size_t pos = deq_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
            if (diff == 0) {
                if (deq_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) break;
            } else if (diff < 0) {
                return false;
            } else {
                pos = deq_pos_.load(std::memory_order_relaxed);
            }
        }
        out = cell->value;
        cell->seq.store(pos + capacity_, std::memory_order_release);
        return true;
    }

    size_t approx_size() const {
        size_t e = enq_pos_.load(std::memory_order_relaxed);
        size_t d = deq_pos_.load(std::memory_order_relaxed);
        return e >= d ? (e - d) : 0;
    }

    size_t capacity() const { return capacity_; }

private:
    struct alignas(CACHELINE) Cell {
        std::atomic<size_t> seq;
        T                   value;
    };

    Cell*  buffer_;
    size_t capacity_;
    size_t mask_;
    alignas(CACHELINE) std::atomic<size_t> enq_pos_;
    alignas(CACHELINE) std::atomic<size_t> deq_pos_;
};

} // namespace aib
