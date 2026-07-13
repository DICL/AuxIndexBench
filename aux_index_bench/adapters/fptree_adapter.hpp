// adapters/fptree_adapter.hpp - Adapter for FPTree
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_FPTREE
#include <cstdint>
#include <vector>
#include "../third_party/fptree/fptree.h"

namespace aib {
class FPTreeAdapter : public IIndex {
public:
    FPTreeAdapter() : t_(new FPtree()) {}
    ~FPTreeAdapter() override { delete t_; }

    bool insert(idx_key_t k, idx_val_t v) override {
        return t_->insert(KV((uint64_t)k, (uint64_t)v));
    }

    idx_val_t lookup(idx_key_t k) const override {
        return (idx_val_t)t_->find((uint64_t)k);
    }

    bool update(idx_key_t k, idx_val_t v) override {
        if (!t_->update(KV((uint64_t)k, (uint64_t)v))) {
            return t_->insert(KV((uint64_t)k, (uint64_t)v));
        }
        return true;
    }

    bool remove(idx_key_t k) override {
        return t_->deleteKey((uint64_t)k);
    }

    int scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        if (n <= 0) return 0;
        std::vector<KV> out((size_t)n);
        uint64_t got = t_->rangeScan((uint64_t)lo, (uint64_t)n,
                                     reinterpret_cast<char*>(out.data()));
        idx_val_t acc = 0;
        for (uint64_t i = 0; i < got; ++i) acc ^= (idx_val_t)out[i].value;
        if (sink) *sink ^= acc;
        return (int)got;
    }

    const char* name() const override { return "fptree"; }
    // The vendored FPTree implements the paper's concurrency scheme:
    // tbb::speculative_spin_rw_mutex (HTM) for inner-node traversal plus
    // per-leaf atomic CAS locks. Declaring thread_safe lets E12 measure
    // that scheme instead of stacking our global RW-lock on top of it.
    // Caveat: on CPUs without TSX/RTM the tbb speculative lock degrades
    // to a plain global spin RW-lock *inside* fptree — scaling may then
    // still be poor, but that is fptree's own behaviour, which is what
    // we want to observe.
    bool thread_safe() const override { return true; }
    bool concurrent_safe() const override { return true; }
private:
    FPtree* t_;
};
} // namespace aib

#else
namespace aib {
class FPTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "fptree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "fptree adapter not enabled (build with -DWITH_FPTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
