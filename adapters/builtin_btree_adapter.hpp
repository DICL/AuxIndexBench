// adapters/builtin_btree_adapter.hpp - Wraps the in-tree BPlusTree behind
// the IIndex virtual interface. Always available (no build flag).

#pragma once
#include "../index.hpp"
#include "../index_iface.hpp"
#include <vector>
#include <algorithm>

namespace aib {

class BuiltinBTreeAdapter : public IIndex {
public:
    BuiltinBTreeAdapter() = default;

    void bulk_load(const std::vector<idx_key_t>& keys,
                   const std::vector<idx_val_t>& vals) override {
        // BPlusTree needs sorted input; co-sort.
        std::vector<size_t> order(keys.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return keys[a] < keys[b]; });
        std::vector<idx_key_t> sk(keys.size());
        std::vector<idx_val_t> sv(vals.size());
        for (size_t i = 0; i < order.size(); ++i) {
            sk[i] = keys[order[i]];
            sv[i] = vals[order[i]];
        }
        bt_.bulk_load(sk, sv);
    }

    bool      insert(idx_key_t /*k*/, idx_val_t /*v*/) override { return false; /* not supported (use bulk_load) */ }
    idx_val_t lookup(idx_key_t k) const                override { return bt_.lookup(k); }
    bool      update(idx_key_t k, idx_val_t v)         override { return bt_.update(k, v); }
    bool      remove(idx_key_t k)                      override { return bt_.remove(k); }
    int       scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        return bt_.scan(lo, n, sink);
    }

    const char* name() const override { return "builtin-btree"; }
    std::string diag() const override {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "nodes=%zu bytes=%.2f MB height=%d",
                      bt_.node_count(), bt_.bytes() / (1024.0 * 1024.0),
                      bt_.height());
        return std::string(buf);
    }

private:
    mutable BPlusTree bt_;
};

} // namespace aib
