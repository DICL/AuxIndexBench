// adapters/builtin_hash_adapter.hpp - Wraps HashIndex behind IIndex.

#pragma once
#include "../hash_index.hpp"
#include "../index_iface.hpp"

namespace aib {

class BuiltinHashAdapter : public IIndex {
public:
    BuiltinHashAdapter(size_t buckets, int shift = 0)
        : ht_(buckets, shift) {}

    bool insert(idx_key_t k, idx_val_t v) override { return ht_.insert(k, v); }
    idx_val_t lookup(idx_key_t k) const  override  { return ht_.lookup(k); }
    bool update(idx_key_t k, idx_val_t v) override { return ht_.update(k, v); }
    bool remove(idx_key_t k)              override { return ht_.remove(k); }
    int  scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        return ht_.scan(lo, n, sink);
    }

    const char* name() const override { return "builtin-hash"; }
    std::string diag() const override {
        uint64_t mx = 0, em = 0, ov = 0;
        double mean = 0;
        ht_.occupancy_stats(mx, mean, em, ov, 8);
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "buckets=%zu items=%zu bytes=%.2f MB max_chain=%lu mean=%.2f empty=%lu",
                      ht_.bucket_count(), ht_.item_count(),
                      ht_.bytes() / (1024.0 * 1024.0),
                      (unsigned long)mx, mean, (unsigned long)em);
        return std::string(buf);
    }

private:
    HashIndex ht_;
};

} // namespace aib
