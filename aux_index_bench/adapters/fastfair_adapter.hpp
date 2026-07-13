// adapters/fastfair_adapter.hpp - Adapter for FAST & FAIR
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_FASTFAIR
#include <cstdint>

// The upstream FAST&FAIR header defines global names such as `btree`,
// `entry_key_t`, and `cpu_pause`. Rename them while including the header so
// they do not collide with bench.cpp or other third_party indexes.
#define btree       fastfair_btree
#define entry_key_t fastfair_entry_key_t
#define cpu_pause   fastfair_cpu_pause
#include <btree.h>
#undef cpu_pause
#undef entry_key_t
#undef btree

namespace aib {

class FastFairAdapter : public IIndex {
public:
    FastFairAdapter() : bt_(new fastfair_btree()) {}
    ~FastFairAdapter() override { delete bt_; }

    bool insert(idx_key_t k, idx_val_t v) override {
        bt_->btree_insert((fastfair_entry_key_t)k, (char*)(uintptr_t)v);
        return true;
    }

    idx_val_t lookup(idx_key_t k) const override {
        char* r = bt_->btree_search((fastfair_entry_key_t)k);
        return (idx_val_t)(uintptr_t)r;
    }

    bool update(idx_key_t k, idx_val_t v) override {
        // FAST&FAIR's common public API has insert/delete/search. Re-insert
        // after delete to emulate update.
        bt_->btree_delete((fastfair_entry_key_t)k);
        bt_->btree_insert((fastfair_entry_key_t)k, (char*)(uintptr_t)v);
        return true;
    }

    bool remove(idx_key_t k) override {
        bt_->btree_delete((fastfair_entry_key_t)k);
        return true;
    }

    int scan(idx_key_t lo, int n, idx_val_t* out_sink) const override {
        // Upstream btree_search_range(min,max,buf) has no buffer-size argument,
        // so using it safely from this benchmark is not possible without
        // modifying upstream. Report scan unsupported instead of risking an
        // overflow.
        (void)lo; (void)n; (void)out_sink;
        return 0;
    }

    const char* name() const override { return "fastfair"; }
    // The vendored source is the `concurrent/` variant: per-page mutexes
    // for writers plus switch_counter-based optimistic reads. Safe for
    // concurrent lookup/update from multiple threads without an external
    // lock.
    bool thread_safe() const override { return true; }
    bool concurrent_safe() const override { return true; }

private:
    fastfair_btree* bt_;
};

} // namespace aib

#else  // !WITH_FASTFAIR

namespace aib {
class FastFairAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "fastfair (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr,
            "fastfair adapter requested but binary was built without "
            "-DWITH_FASTFAIR.\n");
        std::exit(2);
    }
};
} // namespace aib

#endif // WITH_FASTFAIR
