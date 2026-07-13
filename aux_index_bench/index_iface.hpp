// index_iface.hpp - Abstract index interface for plugging external
// PMEM-resident B+tree variants (wB+Tree, FAST&FAIR, FPTree, BzTree,
// LB+Tree, uTree, Circ-Tree, DPTree, NBTree, ...).
//
// Design notes:
//   * Virtual dispatch costs ~2-5 ns per call on modern CPUs (one
//     indirect branch). For hot read-only loops at ~100 ns per lookup
//     this is a 2-5% overhead; for the auxiliary-index regime that the
//     whole benchmark exists to study (lookup + 4 KB read + queueing),
//     it is sub-1%.
//   * If you need bit-for-bit comparison with the v4 enum-dispatch
//     IndexHandle (built-in BPlusTree / HashIndex), keep using
//     --index btree|hash. The external adapters below are compiled in
//     only when their `WITH_<NAME>=1` build flag is set, so a plain
//     `make` produces the same binary as v4.
//
// Each external index lives behind an adapter in adapters/. The
// adapter is responsible for:
//   1. Owning the underlying index instance (PMEM file mapping,
//      allocator, root pointer, etc.).
//   2. Translating our (key, val) calls into the index's native API.
//   3. Reporting any per-construction stats it has via diag().

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include "index.hpp"   // for idx_key_t / idx_val_t typedefs

namespace aib {

class IIndex {
public:
    virtual ~IIndex() = default;

    // Bulk-load is optional; default = insert in a loop.
    virtual void bulk_load(const std::vector<idx_key_t>& keys,
                           const std::vector<idx_val_t>& vals) {
        for (size_t i = 0; i < keys.size(); ++i) insert(keys[i], vals[i]);
    }

    // Core operations. The benchmark calls these on the hot path so
    // the implementation should keep them virtual-call-friendly
    // (single indirect branch; no per-call allocation; thread-safe
    // unless the adapter documents otherwise).
    virtual bool      insert(idx_key_t k, idx_val_t v) = 0;
    virtual idx_val_t lookup(idx_key_t k) const        = 0;
    virtual bool      update(idx_key_t k, idx_val_t v) = 0;
    virtual bool      remove(idx_key_t k)              = 0;
    virtual int       scan(idx_key_t lo, int n, idx_val_t* out_sink) const = 0;

    // Identification and diagnostics. `name()` is used in CSV output and
    // log lines; `diag()` is an opaque human-readable string the adapter
    // can use to report build flags, PMEM pool path, etc.
    virtual const char* name() const         = 0;
    virtual std::string diag() const         { return std::string(); }

    // True if the index implements its own concurrency control and can be
    // called from multiple threads without an external lock (e.g.
    // FAST&FAIR's per-page latches + optimistic reads). Indexes that
    // return false are serialised behind a global shared_mutex whenever
    // the workload mix contains writes and more than one worker runs.
    virtual bool thread_safe() const          { return false; }

    // Whether the implementation is internally thread-safe for the
    // operations the benchmark will perform concurrently. If false,
    // the driver wraps it with std::shared_mutex (as it does today).
    virtual bool concurrent_safe() const     { return false; }
};

} // namespace aib
