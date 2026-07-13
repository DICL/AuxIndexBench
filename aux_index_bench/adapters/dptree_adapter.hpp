// adapters/dptree_adapter.hpp - Adapter for DPTree
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_DPTREE
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <dptree.hpp>

namespace aib {
class DPTreeAdapter : public IIndex {
public:
    DPTreeAdapter() = default;
    ~DPTreeAdapter() override = default;

    bool insert(idx_key_t k, idx_val_t v) override {
        t_.insert((uint64_t)k, (uint64_t)v);
        return true;
    }

    idx_val_t lookup(idx_key_t k) const override {
        uint64_t encoded = 0;
        if (!const_cast<DPTreeAdapter*>(this)->t_.lookup((uint64_t)k, encoded))
            return 0;
        if (encoded & 1ULL) return 0; // tombstone
        return (idx_val_t)const_cast<DPTreeAdapter*>(this)->t_.strip_upsert_value(encoded);
    }

    bool update(idx_key_t k, idx_val_t v) override {
        t_.insert((uint64_t)k, (uint64_t)v);
        return true;
    }

    bool remove(idx_key_t k) override {
        t_.erase((uint64_t)k);
        return true;
    }

    int scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        if (n <= 0) return 0;
        uint64_t hi = (lo > std::numeric_limits<uint64_t>::max() - (uint64_t)n)
                    ? std::numeric_limits<uint64_t>::max()
                    : (uint64_t)lo + (uint64_t)n;
        std::vector<uint64_t> encoded;
        encoded.reserve((size_t)n);
        const_cast<DPTreeAdapter*>(this)->t_.lookup_range((uint64_t)lo, hi, encoded);
        idx_val_t acc = 0;
        int got = 0;
        for (uint64_t v : encoded) {
            if (got >= n) break;
            if (v & 1ULL) continue; // tombstone
            acc ^= (idx_val_t)const_cast<DPTreeAdapter*>(this)->t_.strip_upsert_value(v);
            ++got;
        }
        if (sink) *sink ^= acc;
        return got;
    }

    const char* name() const override { return "dptree"; }
    std::string diag() const override {
        auto& self = *const_cast<DPTreeAdapter*>(this);
        return "buffer=" + std::to_string(self.t_.get_buffer_tree_size()) +
               " static=" + std::to_string(self.t_.get_static_size()) +
               " merges=" + std::to_string(self.t_.get_merges());
    }
    bool concurrent_safe() const override { return false; }
private:
    dtree::dpftree<uint64_t, uint64_t> t_;
};
} // namespace aib

#else
namespace aib {
class DPTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "dptree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "dptree adapter not enabled (build with -DWITH_DPTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
