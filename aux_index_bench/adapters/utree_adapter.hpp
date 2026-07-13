// adapters/utree_adapter.hpp - Adapter for uTree
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_UTREE
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

// The uTree header defines a global `btree` class and `entry_key_t` typedef.
// Rename them while including the header to avoid conflicts with FAST&FAIR.
#define btree       utree_btree
#define entry_key_t utree_entry_key_t
#include "../third_party/utree/utree.h"
#undef entry_key_t
#undef btree

#ifndef USE_PMDK
// Pool pointers for uTree's DRAM-mode bump allocator.
//
// Upstream declares these `__thread` because the original uTree bench
// gives every thread its own mmap region (hence SPACE_PER_THREAD). Our
// harness constructs the index on the main thread and calls into it
// from worker threads, so we patched utree.h to use a single shared
// pool: `start_addr` is set once at init, and `curr_addr` is an atomic
// bump pointer (fetch_add, cache-line rounded) so that concurrent
// inserts — permitted by uTree's per-page btree mutexes and CAS-linked
// list — allocate safely without a global lock.
char* start_addr = nullptr;
std::atomic<char*> curr_addr{nullptr};
#endif

namespace aib {
class UTreeAdapter : public IIndex {
public:
    UTreeAdapter() {
#ifndef USE_PMDK
        map_size_ = SPACE_PER_THREAD;
        void* p = mmap(nullptr, map_size_, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) {
            std::fprintf(stderr, "uTree mmap(%zu) failed: %s\n",
                         map_size_, std::strerror(errno));
            std::exit(2);
        }
        start_addr = static_cast<char*>(p);
        curr_addr = start_addr;
#endif
        t_ = new utree_btree();
    }

    ~UTreeAdapter() override {
        delete t_;
#ifndef USE_PMDK
        if (start_addr && map_size_) munmap(start_addr, map_size_);
        curr_addr.store(nullptr);
        start_addr = nullptr;
#endif
    }

    bool insert(idx_key_t k, idx_val_t v) override {
        t_->insert((utree_entry_key_t)k, reinterpret_cast<char*>((uintptr_t)v));
        return true;
    }

    idx_val_t lookup(idx_key_t k) const override {
        char* r = t_->search((utree_entry_key_t)k);
        return (idx_val_t)(uintptr_t)r;
    }

    bool update(idx_key_t k, idx_val_t v) override {
        t_->insert((utree_entry_key_t)k, reinterpret_cast<char*>((uintptr_t)v));
        return true;
    }

    bool remove(idx_key_t k) override {
        t_->remove((utree_entry_key_t)k);
        return true;
    }

    int scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        (void)lo; (void)n; (void)sink;
        return 0;
    }

    const char* name() const override { return "utree"; }
    // uTree implements its own concurrency control: FAST&FAIR-style
    // per-page std::mutex for btree writers + switch_counter optimistic
    // reads, and a CAS-linked persistent list layer. The one gap was the
    // DRAM-mode bump allocator, which we made atomic (see utree.h
    // alloc() and the pool-pointer comment above) — so concurrent access
    // without the harness's global lock is now safe.
    bool thread_safe() const override { return true; }
    bool concurrent_safe() const override { return false; }

private:
    utree_btree* t_ = nullptr;
    size_t map_size_ = 0;
};
} // namespace aib

#else
namespace aib {
class UTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "utree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "utree adapter not enabled (build with -DWITH_UTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
