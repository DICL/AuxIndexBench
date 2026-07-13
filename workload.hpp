// workload.hpp - Post-lookup workloads.
//
//   1. None           - index-only baseline
//   2. Polluter       - touch N bytes of an unrelated working set
//   3. ObjectAccess   - read a fixed-size object
//   4. StorageStack   - touch metadata + payload, mimicking a fs/storage path

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <random>
#include <cstring>
#include <algorithm>

namespace aib {

enum class WorkloadKind { None, Polluter, ObjectAccess, StorageStack };

struct alignas(64) Sink { volatile uint64_t v = 0; };

class Workload {
public:
    Workload(WorkloadKind kind, size_t per_call_bytes, size_t universe_bytes,
             uint64_t seed)
        : kind_(kind), per_call_bytes_(per_call_bytes), universe_bytes_(universe_bytes) {
        if (kind_ == WorkloadKind::None) return;
        if (per_call_bytes_ == 0) per_call_bytes_ = 64;

        // Align universe to a cache-line multiple so we can index by line.
        constexpr size_t LINE = 64;
        size_t total_lines = universe_bytes_ / LINE;
        if (total_lines == 0) total_lines = 1;
        universe_bytes_ = total_lines * LINE;
        buf_.assign(universe_bytes_, 0);
        for (size_t i = 0; i < universe_bytes_; i += 4096) buf_[i] = (uint8_t)(i & 0xff);

        if (kind_ == WorkloadKind::StorageStack) {
            bio_meta_.assign(256 * 1024, 0);
            pagecache_meta_.assign(1 << 20, 0);
            inode_meta_.assign(512 * 1024, 0);
            for (size_t i = 0; i < bio_meta_.size();       i += 4096) bio_meta_[i]      = 1;
            for (size_t i = 0; i < pagecache_meta_.size(); i += 4096) pagecache_meta_[i]= 1;
            for (size_t i = 0; i < inode_meta_.size();     i += 4096) inode_meta_[i]    = 1;
        }

        std::mt19937_64 rng(seed);

        // Random permutation of every cache line in the universe. Iterating
        // this permutation in *sequential* order gives us the physical-address
        // randomness we want, without a per-op random lookup in the offsets
        // array (which would itself pollute the cache and confuse the timing).
        //
        // Storing line indices as uint32_t caps the universe at 2^32 * 64 B =
        // 256 GiB, which is plenty. For a 512 MiB universe the permutation
        // costs 32 MiB (8 M entries * 4 B); that's a one-off setup cost.
        line_perm_.resize(total_lines);
        for (size_t i = 0; i < total_lines; ++i) line_perm_[i] = (uint32_t)i;
        std::shuffle(line_perm_.begin(), line_perm_.end(), rng);
        total_lines_ = total_lines;
        lines_per_call_ = per_call_bytes_ / LINE;
        if (lines_per_call_ == 0) lines_per_call_ = 1;

        // ObjectAccess/StorageStack still want a slot-based offsets_ array so
        // repeated tags read the same object payload. Polluter no longer uses
        // this table.
        size_t slots = (universe_bytes_ + per_call_bytes_ - 1) / per_call_bytes_;
        if (slots == 0) slots = 1;
        offsets_.resize(slots);
        for (size_t i = 0; i < slots; ++i) offsets_[i] = i;
        std::shuffle(offsets_.begin(), offsets_.end(), rng);
    }

    inline uint64_t run(uint64_t tag) {
        switch (kind_) {
            case WorkloadKind::None:         return tag;
            case WorkloadKind::Polluter:     return polluter(tag);
            case WorkloadKind::ObjectAccess: return object_access(tag);
            case WorkloadKind::StorageStack: return storage_stack(tag);
        }
        return tag;
    }

    size_t per_call_bytes() const { return per_call_bytes_; }
    size_t universe_bytes() const { return universe_bytes_; }

private:
    WorkloadKind kind_;
    size_t per_call_bytes_, universe_bytes_;
    std::vector<uint8_t> buf_;
    std::vector<size_t>  offsets_;
    std::vector<uint32_t> line_perm_;
    size_t total_lines_ = 0;
    size_t lines_per_call_ = 0;
    std::vector<uint8_t> bio_meta_, pagecache_meta_, inode_meta_;

    // Polluter: touch `lines_per_call_` random cache lines drawn from a
    // pre-shuffled permutation of the whole universe. The physical
    // addresses of consecutive touches are unrelated, so:
    //   (a) hardware stride prefetchers can't help;
    //   (b) LLC set index bits vary from touch to touch, so pressure is
    //       spread across all cache sets rather than concentrated in the
    //       64 or 128 sets you'd hit with a contiguous 4-KiB stride;
    //   (c) different `per_call_bytes` values scan different *counts* of
    //       lines but at the same per-line address entropy — so any
    //       remaining nonlinearity in per-op latency isn't a polluter
    //       artefact.
    // The permutation walk itself is sequential, so line_perm_ costs one
    // cold miss per prefetch stream, not one per line.
    inline uint64_t polluter(uint64_t tag) {
        // Cheap hash of `tag` for the permutation start offset.
        size_t start = (size_t)(tag * 0x9E3779B97F4A7C15ULL) % total_lines_;
        uint64_t acc = 0;
        for (size_t i = 0; i < lines_per_call_; ++i) {
            size_t idx = start + i;
            if (idx >= total_lines_) idx -= total_lines_;
            uint32_t line_id = line_perm_[idx];
            uint8_t* p = &buf_[(size_t)line_id * 64];
            acc += *(uint64_t*)p;
            *(uint64_t*)p = acc ^ tag;
        }
        return acc;
    }

    inline uint64_t object_access(uint64_t tag) {
        size_t slot = offsets_[tag % offsets_.size()];
        const uint8_t* p = &buf_[slot * per_call_bytes_];
        uint64_t acc = 0;
        for (size_t off = 0; off < per_call_bytes_; off += 8)
            acc += *(const uint64_t*)(p + off);
        return acc;
    }

    inline uint64_t storage_stack(uint64_t tag) {
        size_t s1 = (tag * 0x9E3779B97F4A7C15ULL) % (bio_meta_.size()       / 128);
        size_t s2 = (tag * 0xBF58476D1CE4E5B9ULL) % (pagecache_meta_.size() / 128);
        size_t s3 = (tag * 0x94D049BB133111EBULL) % (inode_meta_.size()     / 256);
        uint64_t acc = 0;
        const uint64_t* p1 = (const uint64_t*)&bio_meta_[s1 * 128];
        const uint64_t* p2 = (const uint64_t*)&pagecache_meta_[s2 * 128];
        const uint64_t* p3 = (const uint64_t*)&inode_meta_[s3 * 256];
        for (int i = 0; i < 128/8; ++i) acc += p1[i];
        for (int i = 0; i < 128/8; ++i) acc += p2[i];
        for (int i = 0; i < 256/8; ++i) acc += p3[i];
        size_t slot = offsets_[tag % offsets_.size()];
        const uint8_t* payload = &buf_[slot * per_call_bytes_];
        for (size_t off = 0; off < per_call_bytes_; off += 64)
            acc += *(const uint64_t*)(payload + off);
        return acc;
    }
};

} // namespace aib
