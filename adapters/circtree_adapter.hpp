// adapters/circtree_adapter.hpp - Adapter for Circ-Tree — circular-leaf layout for PMEM, reduces shift cost.
//
// Known repository (NOT verified — confirm before relying on):
//   * https://github.com/DICL/Circ-Tree
//
// To enable: make WITH_CIRCTREE=1
//
// TODO: confirm class name, namespace, constructor, and insert/lookup/scan
// signatures from the upstream code. The stub below assumes a class named
// 'circtree' with .insert/.lookup/.update/.remove/.scan methods; adapt as
// needed.

#pragma once
#include "../index_iface.hpp"

#ifdef WITH_CIRCTREE
#include "../third_party/circtree/circtree.h"

namespace aib {
class CircTreeAdapter : public IIndex {
public:
    CircTreeAdapter()  { /* TODO: t_ = new circtree(...); */ }
    ~CircTreeAdapter() override { /* TODO: delete t_; */ }
    bool insert(idx_key_t k, idx_val_t v) override { (void)k; (void)v; return true; }
    idx_val_t lookup(idx_key_t k) const override   { (void)k; return 0; }
    bool update(idx_key_t k, idx_val_t v) override { (void)k; (void)v; return true; }
    bool remove(idx_key_t k) override              { (void)k; return true; }
    int  scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        (void)lo; (void)n; (void)sink; return 0;
    }
    const char* name() const override { return "circtree"; }
    bool concurrent_safe() const override { return true; }
};
} // namespace aib

#else
namespace aib {
class CircTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "circtree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "circtree adapter not enabled (build with -DWITH_CIRCTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
