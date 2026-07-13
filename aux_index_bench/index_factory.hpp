// index_factory.hpp - Build an IIndex* from a string name.

#pragma once
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>

#include "index_iface.hpp"
#include "adapters/builtin_btree_adapter.hpp"
#include "adapters/builtin_hash_adapter.hpp"
#include "adapters/fastfair_adapter.hpp"
#include "adapters/wbtree_adapter.hpp"
#include "adapters/fptree_adapter.hpp"
#include "adapters/bztree_adapter.hpp"
#include "adapters/lbtree_adapter.hpp"
#include "adapters/utree_adapter.hpp"
#include "adapters/circtree_adapter.hpp"
#include "adapters/dptree_adapter.hpp"
#include "adapters/nbtree_adapter.hpp"

namespace aib {

struct IndexConfig {
    // Used only for builtin-hash.
    size_t hash_buckets = 1 << 18;
};

inline std::unique_ptr<IIndex>
make_index(const std::string& name, const IndexConfig& cfg) {
    if (name == "btree" || name == "builtin-btree")
        return std::make_unique<BuiltinBTreeAdapter>();
    if (name == "hash"  || name == "builtin-hash")
        return std::make_unique<BuiltinHashAdapter>(cfg.hash_buckets);
    if (name == "fastfair") return std::make_unique<FastFairAdapter>();
    if (name == "wbtree")   return std::make_unique<WBTreeAdapter>();
    if (name == "fptree")   return std::make_unique<FPTreeAdapter>();
    if (name == "bztree")   return std::make_unique<BzTreeAdapter>();
    if (name == "lbtree")   return std::make_unique<LBTreeAdapter>();
    if (name == "utree")    return std::make_unique<UTreeAdapter>();
    if (name == "circtree") return std::make_unique<CircTreeAdapter>();
    if (name == "dptree")   return std::make_unique<DPTreeAdapter>();
    if (name == "nbtree")   return std::make_unique<NBTreeAdapter>();
    std::fprintf(stderr, "unknown --index name: %s\n", name.c_str());
    std::fprintf(stderr,
        "  available: btree, hash, fastfair, wbtree, fptree, bztree,\n"
        "             lbtree, utree, circtree, dptree, nbtree\n"
        "  (external adapters require their WITH_<NAME> build flag)\n");
    std::exit(2);
}

} // namespace aib
