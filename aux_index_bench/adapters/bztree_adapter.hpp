// adapters/bztree_adapter.hpp - Adapter for BzTree
#pragma once
#include "../index_iface.hpp"

#ifdef WITH_BZTREE
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include "../third_party/bztree/bztree.h"

namespace aib {
class BzTreeAdapter : public IIndex {
public:
    BzTreeAdapter() {
        init_pmwcas_once();
        pool_ = new pmwcas::DescriptorPool(1 << 20, 1, false);
        bztree::BzTree::ParameterSet param(3072, 1024, 4096);
        t_ = bztree::BzTree::New(param, pool_);
    }

    ~BzTreeAdapter() override {
        delete t_;
        delete pool_;
        pmwcas::Thread::ClearRegistry();
    }

    bool insert(idx_key_t k, idx_val_t v) override {
        uint64_t key = encode_key(k);
        auto rc = t_->Insert(reinterpret_cast<const char*>(&key), sizeof(key), (uint64_t)v);
        return rc.IsOk() || rc.IsKeyExists();
    }

    idx_val_t lookup(idx_key_t k) const override {
        uint64_t key = encode_key(k);
        uint64_t payload = 0;
        auto rc = t_->Read(reinterpret_cast<const char*>(&key), sizeof(key), &payload);
        return rc.IsOk() ? (idx_val_t)payload : 0;
    }

    bool update(idx_key_t k, idx_val_t v) override {
        uint64_t key = encode_key(k);
        auto rc = t_->Upsert(reinterpret_cast<const char*>(&key), sizeof(key), (uint64_t)v);
        return rc.IsOk();
    }

    bool remove(idx_key_t k) override {
        uint64_t key = encode_key(k);
        auto rc = t_->Delete(reinterpret_cast<const char*>(&key), sizeof(key));
        return rc.IsOk() || rc.IsNotFound();
    }

    int scan(idx_key_t lo, int n, idx_val_t* sink) const override {
        if (n <= 0) return 0;
        uint64_t key = encode_key(lo);
        auto it = t_->RangeScanBySize(reinterpret_cast<const char*>(&key), sizeof(key),
                                      (uint32_t)n);
        idx_val_t acc = 0;
        int got = 0;
        while (got < n) {
            auto r = it->GetNext();
            if (!r) break;
            acc ^= (idx_val_t)r->GetPayload();
            ++got;
        }
        if (sink) *sink ^= acc;
        return got;
    }

    const char* name() const override { return "bztree"; }
    bool concurrent_safe() const override { return true; }

private:
    static uint64_t encode_key(idx_key_t k) {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap64((uint64_t)k);
#else
        uint64_t x = (uint64_t)k;
        x = ((x & 0x00ff00ff00ff00ffULL) << 8)  | ((x & 0xff00ff00ff00ff00ULL) >> 8);
        x = ((x & 0x0000ffff0000ffffULL) << 16) | ((x & 0xffff0000ffff0000ULL) >> 16);
        return (x << 32) | (x >> 32);
#endif
    }

    static void init_pmwcas_once() {
        static std::once_flag once;
        std::call_once(once, [] {
            pmwcas::InitLibrary(pmwcas::DefaultAllocator::Create,
                                pmwcas::DefaultAllocator::Destroy,
                                pmwcas::LinuxEnvironment::Create,
                                pmwcas::LinuxEnvironment::Destroy);
        });
    }

    pmwcas::DescriptorPool* pool_ = nullptr;
    bztree::BzTree* t_ = nullptr;
};
} // namespace aib

#else
namespace aib {
class BzTreeAdapter : public IIndex {
public:
    bool insert(idx_key_t, idx_val_t) override { fail(); return false; }
    idx_val_t lookup(idx_key_t) const override { fail(); return 0; }
    bool update(idx_key_t, idx_val_t) override { fail(); return false; }
    bool remove(idx_key_t) override            { fail(); return false; }
    int  scan(idx_key_t, int, idx_val_t*) const override { fail(); return 0; }
    const char* name() const override          { return "bztree (disabled)"; }
private:
    [[noreturn]] static void fail() {
        std::fprintf(stderr, "bztree adapter not enabled (build with -DWITH_BZTREE).\n");
        std::exit(2);
    }
};
} // namespace aib
#endif
