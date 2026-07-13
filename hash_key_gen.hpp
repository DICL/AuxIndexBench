// hash_key_gen.hpp - Synthetic 64-bit hash key generator with tunable
// bit-level collision concentration.
//
// Model:
//   * 64-bit hash space divided into `intervals` equal-length intervals.
//   * For each interval, one bit position is the dominant bit (fixed at
//     init when dominant_bit_fixed=true). Dominant value resampled per key.
//   * Every other position `pos` takes the dominant value with probability
//     P(pos) = 0.5 + 0.5 * exp(-½ ((pos - interval_center) / σ)²).
//
// σ=0.4 (paper default) yields ~53% of keys in 25% of buckets
// (verified with run_skew_check.cpp at 256 buckets, 10^6 keys).

#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <random>
#include <vector>
#include <cassert>

namespace aib {

class HashKeyGen {
public:
    HashKeyGen(int intervals, double sigma, uint64_t seed,
               bool dominant_bit_fixed = true)
        : intervals_(intervals),
          interval_bits_(64 / intervals),
          sigma_(sigma),
          dom_fixed_(dominant_bit_fixed),
          rng_(seed)
    {
        assert(64 % intervals == 0 && "intervals must divide 64");
        double center = (interval_bits_ - 1) / 2.0;
        prob_.resize(interval_bits_);
        for (int off = 0; off < interval_bits_; ++off) {
            double d = (off - center) / sigma_;
            prob_[off] = 0.5 + 0.5 * std::exp(-0.5 * d * d);
        }
        fixed_dom_off_.resize(intervals_);
        for (int iv = 0; iv < intervals_; ++iv) {
            int off = (int)(uni_(rng_) * interval_bits_);
            if (off >= interval_bits_) off = interval_bits_ - 1;
            fixed_dom_off_[iv] = off;
        }
    }

    uint64_t next() {
        uint64_t key = 0;
        for (int iv = 0; iv < intervals_; ++iv) {
            int base = iv * interval_bits_;
            int dom_off;
            if (dom_fixed_) {
                dom_off = fixed_dom_off_[iv];
            } else {
                dom_off = (int)(uni_(rng_) * interval_bits_);
                if (dom_off >= interval_bits_) dom_off = interval_bits_ - 1;
            }
            int dom_val = (uni_(rng_) < 0.5) ? 0 : 1;
            if (dom_val) key |= (1ULL << (base + dom_off));
            for (int off = 0; off < interval_bits_; ++off) {
                if (off == dom_off) continue;
                double p_match = prob_[off];
                int bit = (uni_(rng_) < p_match) ? dom_val : (1 - dom_val);
                if (bit) key |= (1ULL << (base + off));
            }
        }
        return key;
    }

    int    intervals()     const { return intervals_; }
    int    interval_bits() const { return interval_bits_; }
    double sigma()         const { return sigma_; }

private:
    int intervals_, interval_bits_;
    double sigma_;
    bool   dom_fixed_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uni_{0.0, 1.0};
    std::vector<double> prob_;
    std::vector<int>    fixed_dom_off_;
};

} // namespace aib
