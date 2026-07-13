// run_skew_check.cpp - Sanity check for the synthetic hash key generator.
//
// Verifies that σ=0.4 yields ~53% of keys in 25% of buckets, as stated
// in the paper.
//
// Build:  g++ -O3 -std=c++17 -march=native run_skew_check.cpp -o skew_check
// Run:    ./skew_check [sigma] [intervals] [bucket_log] [num_keys]

#include "hash_key_gen.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <numeric>

int main(int argc, char** argv) {
    double sigma     = argc > 1 ? std::strtod(argv[1], nullptr) : 0.4;
    int    intervals = argc > 2 ? std::atoi(argv[2])            : 8;
    int    bucketlog = argc > 3 ? std::atoi(argv[3])            : 8;
    size_t n         = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : (size_t)1'000'000;

    size_t nb = (size_t)1 << bucketlog;
    uint64_t mask = nb - 1;
    std::vector<uint64_t> counts(nb, 0);

    aib::HashKeyGen gen(intervals, sigma, 42);
    for (size_t i = 0; i < n; ++i) ++counts[gen.next() & mask];

    std::vector<uint64_t> sorted = counts;
    std::sort(sorted.begin(), sorted.end(), std::greater<uint64_t>());

    size_t top_buckets = nb / 4;
    uint64_t top_mass = std::accumulate(sorted.begin(), sorted.begin() + top_buckets, (uint64_t)0);

    double total = (double)n;
    double cum_share = 0, gini_acc = 0;
    for (size_t i = 0; i < nb; ++i) {
        cum_share += (double)sorted[nb - 1 - i] / total;
        gini_acc += cum_share;
    }
    double gini = 1.0 - 2.0 / nb * gini_acc + 1.0 / nb;

    std::printf("sigma=%.2f intervals=%d buckets=%zu (2^%d) keys=%zu\n",
                sigma, intervals, nb, bucketlog, n);
    std::printf("top-25%% buckets capture %.1f%% of keys  (paper: ~53%% at sigma=0.4)\n",
                100.0 * top_mass / total);
    std::printf("top-10%% buckets capture %.1f%% of keys\n",
                100.0 * std::accumulate(sorted.begin(), sorted.begin() + nb/10,
                                        (uint64_t)0) / total);
    std::printf("max bucket count = %lu (uniform expect = %.1f)\n",
                (unsigned long)sorted.front(), (double)n / nb);
    std::printf("Gini coefficient = %.4f  (0=uniform, 1=all-in-one)\n", gini);
    return 0;
}
