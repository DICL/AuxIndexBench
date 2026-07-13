// histogram.hpp - Cheap latency histogram with octave-style buckets.

#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>

namespace aib {

class Histogram {
public:
    static constexpr int NUM_OCTAVES = 40;
    static constexpr int SUBBUCKETS  = 8;
    static constexpr int NUM_BUCKETS = NUM_OCTAVES * SUBBUCKETS;

    void add(uint64_t ns) {
        int b = bucket(ns);
        ++counts_[b];
        ++total_;
        sum_ += ns;
        if (ns > max_) max_ = ns;
    }

    void merge(const Histogram& other) {
        for (int i = 0; i < NUM_BUCKETS; ++i) counts_[i] += other.counts_[i];
        total_ += other.total_;
        sum_   += other.sum_;
        max_    = std::max(max_, other.max_);
    }

    uint64_t total()   const { return total_; }
    uint64_t sum_ns()  const { return sum_; }
    double   mean_ns() const { return total_ ? (double)sum_ / total_ : 0.0; }
    uint64_t max_ns()  const { return max_; }

    uint64_t percentile(double p) const {
        if (total_ == 0) return 0;
        uint64_t target = (uint64_t)((double)total_ * p);
        if (target >= total_) target = total_ - 1;
        uint64_t accum = 0;
        for (int i = 0; i < NUM_BUCKETS; ++i) {
            accum += counts_[i];
            if (accum > target) return bucket_lower_edge(i);
        }
        return bucket_lower_edge(NUM_BUCKETS - 1);
    }

private:
    std::array<uint64_t, NUM_BUCKETS> counts_{};
    uint64_t total_ = 0;
    uint64_t sum_   = 0;
    uint64_t max_   = 0;

    static int bucket(uint64_t ns) {
        if (ns < (uint64_t)SUBBUCKETS) return (int)ns;
        int oct = 63 - __builtin_clzll(ns);
        if (oct >= NUM_OCTAVES) oct = NUM_OCTAVES - 1;
        uint64_t base = 1ULL << oct;
        uint64_t step = base / SUBBUCKETS;
        if (step == 0) step = 1;
        int sub = (int)((ns - base) / step);
        if (sub >= SUBBUCKETS) sub = SUBBUCKETS - 1;
        return oct * SUBBUCKETS + sub;
    }

    static uint64_t bucket_lower_edge(int b) {
        int oct = b / SUBBUCKETS;
        int sub = b % SUBBUCKETS;
        if (oct == 0) return (uint64_t)sub;
        uint64_t base = 1ULL << oct;
        uint64_t step = base / SUBBUCKETS;
        return base + (uint64_t)sub * step;
    }
};

} // namespace aib
