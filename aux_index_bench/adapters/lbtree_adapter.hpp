// adapters/lbtree_adapter.hpp - Adapter for LB+-Tree
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_LBTREE
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../third_party/lbtree/lbtree-src/lbtree.h"
// LB+-Tree's tree.h defines min/max macros. Remove them before returning to
// benchmark headers / STL code.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef swap
#undef swap
#endif

// The upstream LB+-Tree code expects a few benchmark-driver globals from
// common/tree.cc.  We do not link that driver because it provides its own
// main(), so the adapter supplies the minimal globals used by lbtree.cc.
tree* the_treep = nullptr;
int worker_thread_num = 0;
const char* nvm_file_name = nullptr;

namespace aib {
class LBTreeAdapter : public IIndex {
public:
    LBTreeAdapter() {
        init_pools_once();
        char* meta = reinterpret_cast<char*>(nvmpool_alloc(4 * KB));
        t_ = new lbtree(meta, false);
    }

    ~LBTreeAdapter() override { delete t_; }

    // Upstream LB+-Tree REQUIRES bulkload before any insert/lookup: an
    // empty tree has tree_root == NULL and insert dereferences it (the
    // SIGSEGV / RTM-abort-forever seen when the default insert-loop
    // bulk_load fallback was used). Feed the sorted key array through
    // the native bulkload API instead.
    //
    // Note upstream bulkload stores the KEY ITSELF as each entry's
    // record pointer (lp->ch(j) = (void*)mykey); the vals[] argument is
    // therefore ignored here. The benchmark never validates lookup
    // payloads, and any update() afterwards overwrites the recptr with
    // our real value.
    void bulk_load(const std::vector<idx_key_t>& keys,
                   const std::vector<idx_val_t>& vals) override {
        (void)vals;
        if (keys.empty()) return;
        const std::vector<idx_key_t>* src = &keys;
        std::vector<idx_key_t> sorted;
        if (!std::is_sorted(keys.begin(), keys.end())) {
            sorted = keys;
            std::sort(sorted.begin(), sorted.end());
            src = &sorted;
        }
        VecKeyInput in(src);
        t_->bulkload((int)src->size(), &in, bulk_fill_factor());
    }

    bool insert(idx_key_t k, idx_val_t v) override {
        t_->insert((key_type)k, reinterpret_cast<void*>((uintptr_t)v));
        return true;
    }

    idx_val_t lookup(idx_key_t k) const override {
        int pos = -1;
        void* leaf = t_->lookup((key_type)k, &pos);
        if (leaf == nullptr || pos < 0) return 0;
        return (idx_val_t)(uintptr_t)t_->get_recptr(leaf, pos);
    }

    bool update(idx_key_t k, idx_val_t v) override {
        // LB+-Tree insert overwrites an existing key if present.
        t_->insert((key_type)k, reinterpret_cast<void*>((uintptr_t)v));
        return true;
    }

    bool remove(idx_key_t k) override {
        t_->del((key_type)k);
        return true;
    }

    int scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        // The upstream LB+-Tree API exposes point lookup/insert/delete, but no
        // bounded range-scan method with a stable public signature.
        (void)lo; (void)n; (void)sink;
        return 0;
    }

    const char* name() const override { return "lbtree"; }
    std::string diag() const override {
        return "level=" + std::to_string(t_->level());
    }
    // LB+-Tree stays behind the harness's global lock: thread_safe()
    // remains false (IIndex default). This is REQUIRED when built with
    // LBTREE_NO_RTM — the transactions that used to protect concurrent
    // access are compiled out.

private:
    // keyInput view over our key vector for the native bulkload.
    class VecKeyInput : public keyInput {
    public:
        explicit VecKeyInput(const std::vector<idx_key_t>* ks) : ks_(ks) {}
        Int64 get_key(Int64 index) override {
            return (Int64)(*ks_)[(size_t)index];
        }
    private:
        const std::vector<idx_key_t>* ks_;
    };

    static float bulk_fill_factor() {
        const char* s = std::getenv("AIB_LBTREE_BFILL");
        if (s && *s) {
            float f = (float)std::atof(s);
            if (f > 0.1f && f <= 1.0f) return f;
        }
        return 1.0f;  // static-then-upsert workload: dense leaves are best
    }

    static void init_pools_once() {
        static bool initialized = false;
        if (initialized) {
            worker_id = 0;
            return;
        }
        worker_thread_num = 1;
        worker_id = 0;

        const long long mem_mb = getenv_ll("AIB_LBTREE_MEM_MB", 1024);
        const long long nvm_mb = getenv_ll("AIB_LBTREE_NVM_MB", 1024);
        static std::string nvm_path;
        const char* nvm_file = std::getenv("AIB_LBTREE_NVMFILE");
        if (nvm_file == nullptr || *nvm_file == '\0') {
            const char* dir = std::getenv("AIB_PMEM_DIR");
            if (dir == nullptr || *dir == '\0') dir = "/pmem0/mmkim505";
            nvm_path = std::string(dir) + "/aib_lbtree.pool";
            nvm_file = nvm_path.c_str();
        }
        std::fprintf(stderr, "[lbtree] nvm pool: %s (%lld MB)\n",
                     nvm_file, nvm_mb);

        the_thread_mempools.init(worker_thread_num, mem_mb * MB, 4096);
        the_thread_nvmpools.init(worker_thread_num, nvm_file, nvm_mb * MB);
        initialized = true;
    }

    static long long getenv_ll(const char* name, long long defv) {
        const char* s = std::getenv(name);
        if (!s || !*s) return defv;
        char* end = nullptr;
        long long v = std::strtoll(s, &end, 10);
        return (end && *end == '\0' && v > 0) ? v : defv;
    }

    lbtree* t_ = nullptr;
};
} // namespace aib

#else
namespace aib {
class LBTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "lbtree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "lbtree adapter not enabled (build with -DWITH_LBTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
