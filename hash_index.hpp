// hash_index.hpp - Chaining hash table with the same public surface
// as BPlusTree.

#pragma once
#include "index.hpp"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <new>

namespace aib {

class HashIndex {
public:
    static constexpr int INLINE_SLOTS = 4;

    struct alignas(64) Bucket {
        int       n;
        int       _pad;
        idx_key_t keys[INLINE_SLOTS];
        idx_val_t vals[INLINE_SLOTS];
        Bucket*   next;
    };
    static_assert(sizeof(Bucket) <= 192, "bucket should fit a few cache lines");

    HashIndex(size_t bucket_count, int shift = 0) : shift_(shift) {
        size_t v = 1;
        while (v < bucket_count) v <<= 1;
        nb_   = v;
        mask_ = nb_ - 1;
        buckets_ = static_cast<Bucket*>(
            std::aligned_alloc(64, sizeof(Bucket) * nb_));
        for (size_t i = 0; i < nb_; ++i) {
            new (&buckets_[i]) Bucket();
            buckets_[i].n = 0;
            buckets_[i].next = nullptr;
        }
    }

    ~HashIndex() {
        for (size_t i = 0; i < nb_; ++i) {
            Bucket* b = buckets_[i].next;
            while (b) { Bucket* n = b->next; b->~Bucket(); std::free(b); b = n; }
            buckets_[i].~Bucket();
        }
        std::free(buckets_);
    }

    bool insert(idx_key_t k, idx_val_t v) {
        Bucket* b = &buckets_[index_of(k)];
        for (Bucket* cur = b; cur; cur = cur->next) {
            for (int i = 0; i < cur->n; ++i) {
                if (cur->keys[i] == k) { cur->vals[i] = v; return false; }
            }
        }
        Bucket* tail = b;
        while (tail->n == INLINE_SLOTS && tail->next) tail = tail->next;
        if (tail->n == INLINE_SLOTS) {
            void* p = std::aligned_alloc(64, sizeof(Bucket));
            Bucket* nb = new (p) Bucket();
            nb->n = 0; nb->next = nullptr;
            tail->next = nb;
            tail = nb;
            ++overflow_buckets_;
        }
        tail->keys[tail->n] = k;
        tail->vals[tail->n] = v;
        ++tail->n;
        ++items_;
        return true;
    }

    inline idx_val_t lookup(idx_key_t k) const {
        const Bucket* b = &buckets_[index_of(k)];
        for (const Bucket* cur = b; cur; cur = cur->next) {
            int n = cur->n;
            for (int i = 0; i < n; ++i) {
                if (cur->keys[i] == k) return cur->vals[i];
            }
        }
        return 0;
    }

    inline bool update(idx_key_t k, idx_val_t newv) {
        Bucket* b = &buckets_[index_of(k)];
        for (Bucket* cur = b; cur; cur = cur->next) {
            for (int i = 0; i < cur->n; ++i) {
                if (cur->keys[i] == k) { cur->vals[i] = newv; return true; }
            }
        }
        return false;
    }

    inline bool remove(idx_key_t k) { return update(k, 0); }

    inline int scan(idx_key_t start_key, int n, idx_val_t* out_sink) const {
        const Bucket* cur = &buckets_[index_of(start_key)];
        idx_val_t acc = 0;
        int taken = 0;
        while (cur && taken < n) {
            for (int i = 0; i < cur->n && taken < n; ++i) {
                acc ^= cur->vals[i];
                ++taken;
            }
            cur = cur->next;
        }
        if (out_sink) *out_sink ^= acc;
        return taken;
    }

    size_t bucket_count()     const { return nb_; }
    size_t item_count()       const { return items_; }
    size_t overflow_buckets() const { return overflow_buckets_; }
    size_t bytes() const { return (nb_ + overflow_buckets_) * sizeof(Bucket); }

    void occupancy_stats(uint64_t& max_chain, double& mean_chain,
                         uint64_t& empty, uint64_t& over_threshold,
                         int threshold = 8) const {
        max_chain = 0; empty = 0; over_threshold = 0;
        uint64_t total = 0;
        for (size_t i = 0; i < nb_; ++i) {
            uint64_t cnt = 0;
            for (const Bucket* c = &buckets_[i]; c; c = c->next) cnt += c->n;
            if (cnt == 0) ++empty;
            if (cnt > max_chain) max_chain = cnt;
            if (cnt > (uint64_t)threshold) ++over_threshold;
            total += cnt;
        }
        mean_chain = nb_ ? (double)total / nb_ : 0;
    }

private:
    Bucket* buckets_ = nullptr;
    size_t  nb_ = 0, mask_ = 0;
    int     shift_ = 0;
    size_t  items_ = 0;
    size_t  overflow_buckets_ = 0;

    inline size_t index_of(idx_key_t k) const {
        return (size_t)((k >> shift_) & (idx_key_t)mask_);
    }
};

} // namespace aib
