// adapters/wbtree_adapter.hpp - Adapter for wB+Tree
// (Chen & Jin, "Persistent B+-Trees in Non-Volatile Main Memory,"
//  VLDB 2015).
//
// wB+Tree uses write-atomic operations + slot indirection arrays to
// limit the number of cache-line flushes per insert.
//
// Known repositories (NOT verified — confirm before relying on):
//   * https://github.com/cosmoss-jigu/wbtree   (one of several
//     reimplementations; original authors' code may not be public)
//   * Some forks live inside index-microbench:
//     https://github.com/wangziqi2016/index-microbench
//
// To enable:
//   make WITH_WBTREE=1
//
// TODO:
//   * Confirm the upstream API. The common surface is:
//       wbtree*  t = new_wbtree();
//       wbtree_insert(t, key, val);
//       value = wbtree_lookup(t, key);
//   * Some forks are C-only; if so, this header may need extern "C"
//     wrapping.

#pragma once
#include "../index_iface.hpp"

#ifdef WITH_WBTREE

// Adjust path to match your clone.
extern "C" {
  #include "../third_party/wbtree/wbtree.h"
}

namespace aib {

class WBTreeAdapter : public IIndex {
public:
    WBTreeAdapter()  { t_ = wbtree_new(); }
    ~WBTreeAdapter() override { wbtree_free(t_); }

    bool insert(idx_key_t k, idx_val_t v) override {
        wbtree_insert(t_, k, (void*)(uintptr_t)v);
        return true;
    }
    idx_val_t lookup(idx_key_t k) const override {
        void* r = wbtree_lookup(t_, k);
        return (idx_val_t)(uintptr_t)r;
    }
    bool update(idx_key_t k, idx_val_t v) override {
        // wB+Tree typically supports update via delete+insert.
        wbtree_delete(t_, k);
        wbtree_insert(t_, k, (void*)(uintptr_t)v);
        return true;
    }
    bool remove(idx_key_t k) override { wbtree_delete(t_, k); return true; }

    int scan(idx_key_t lo, int n, idx_val_t* out_sink) const override {
        // TODO: many wB+Tree forks do not expose range scan. If yours
        // does, wire it up here. Otherwise leave as 0 and note in the
        // CSV that scan is not supported.
        (void)lo; (void)n; (void)out_sink; return 0;
    }

    const char* name() const override { return "wbtree"; }

private:
    wbtree* t_;
};

} // namespace aib

#else
namespace aib {
class WBTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "wbtree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "wbtree adapter not enabled (build with -DWITH_WBTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
