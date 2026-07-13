// index.hpp - Cache-conscious B+tree index for benchmarking.
//
// Implementation notes:
//   - Fixed fanout, cache-line-aligned nodes
//   - Sorted keys per node, linear search inside (branch-predictor friendly)
//   - Leaves store 8-byte payload values
//   - Bulk-loaded from a sorted key array; single-writer assumed for updates
//
// This is intentionally minimal: the benchmark's point is to compare
// "index-only" vs "index-as-auxiliary" workloads, not to compete with
// state-of-the-art indexes on absolute numbers.

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace aib {

using idx_key_t = uint64_t;
using idx_val_t = uint64_t;

// Fanout chosen so an inner node spans a few cache lines.
// Inner node: FANOUT keys + (FANOUT+1) child pointers.
static constexpr int FANOUT = 15;

struct Node {
    bool     is_leaf;
    int      n;                  // number of keys
    idx_key_t keys[FANOUT];
    // For inner nodes: children[0..n]
    // For leaves: vals[0..n-1], next pointer for range scans
    union {
        Node*    children[FANOUT + 1];
        idx_val_t vals[FANOUT];
    };
    Node* next_leaf; // only meaningful for leaves
};

class BPlusTree {
public:
    BPlusTree() : root_(nullptr) {}
    ~BPlusTree() { destroy(root_); }

    // Bulk-load from sorted (key,val) pairs. Builds leaves first, then
    // recursively builds inner levels.
    void bulk_load(const std::vector<idx_key_t>& keys,
                   const std::vector<idx_val_t>& vals) {
        assert(keys.size() == vals.size());
        const size_t n = keys.size();

        // Build leaves.
        std::vector<Node*> level;
        Node* prev_leaf = nullptr;
        for (size_t i = 0; i < n; i += FANOUT) {
            Node* leaf = alloc_node();
            leaf->is_leaf = true;
            int cnt = (int)std::min<size_t>(FANOUT, n - i);
            leaf->n = cnt;
            for (int j = 0; j < cnt; ++j) {
                leaf->keys[j] = keys[i + j];
                leaf->vals[j] = vals[i + j];
            }
            leaf->next_leaf = nullptr;
            if (prev_leaf) prev_leaf->next_leaf = leaf;
            prev_leaf = leaf;
            level.push_back(leaf);
        }

        // Build inner levels.
        while (level.size() > 1) {
            std::vector<Node*> parents;
            for (size_t i = 0; i < level.size(); i += (FANOUT + 1)) {
                Node* inner = alloc_node();
                inner->is_leaf = false;
                int cnt = (int)std::min<size_t>(FANOUT + 1, level.size() - i);
                inner->n = cnt - 1;
                for (int j = 0; j < cnt; ++j) inner->children[j] = level[i + j];
                for (int j = 1; j < cnt; ++j) inner->keys[j - 1] = first_key(level[i + j]);
                inner->next_leaf = nullptr;
                parents.push_back(inner);
            }
            level.swap(parents);
        }

        root_ = level.empty() ? nullptr : level[0];
    }

    // Point lookup: returns 0 if not found.
    inline idx_val_t lookup(idx_key_t k) const {
        Node* node = root_;
        while (node && !node->is_leaf) {
            int i = 0;
            while (i < node->n && k >= node->keys[i]) ++i;
            node = node->children[i];
        }
        if (!node) return 0;
        for (int i = 0; i < node->n; ++i) {
            if (node->keys[i] == k) return node->vals[i];
        }
        return 0;
    }

    // Update (upsert without split). Returns true on hit.
    inline bool update(idx_key_t k, idx_val_t newv) {
        Node* node = root_;
        while (node && !node->is_leaf) {
            int i = 0;
            while (i < node->n && k >= node->keys[i]) ++i;
            node = node->children[i];
        }
        if (!node) return false;
        for (int i = 0; i < node->n; ++i) {
            if (node->keys[i] == k) { node->vals[i] = newv; return true; }
        }
        return false;
    }

    inline bool remove(idx_key_t k) { return update(k, 0); }

    // Range scan: walks up to `n` entries following next_leaf pointers.
    inline int scan(idx_key_t lo, int n, idx_val_t* out_sink) const {
        Node* node = root_;
        while (node && !node->is_leaf) {
            int i = 0;
            while (i < node->n && lo >= node->keys[i]) ++i;
            node = node->children[i];
        }
        if (!node) return 0;
        int idx = 0;
        while (idx < node->n && node->keys[idx] < lo) ++idx;
        int taken = 0;
        idx_val_t acc = 0;
        while (node && taken < n) {
            while (idx < node->n && taken < n) {
                acc ^= node->vals[idx++];
                ++taken;
            }
            node = node->next_leaf;
            idx  = 0;
        }
        if (out_sink) *out_sink ^= acc;
        return taken;
    }

    size_t node_count() const { return node_count_; }
    size_t bytes() const { return node_count_ * sizeof(Node); }
    int    height() const {
        int h = 0;
        for (Node* n = root_; n && !n->is_leaf; n = n->children[0]) ++h;
        return h + 1;
    }

private:
    Node*  root_;
    size_t node_count_ = 0;

    Node* alloc_node() {
        void* p = std::aligned_alloc(64, sizeof(Node));
        Node* n = new (p) Node();
        node_count_++;
        return n;
    }

    static idx_key_t first_key(Node* n) {
        while (!n->is_leaf) n = n->children[0];
        return n->keys[0];
    }

    void destroy(Node* n) {
        if (!n) return;
        if (!n->is_leaf) for (int i = 0; i <= n->n; ++i) destroy(n->children[i]);
        n->~Node();
        std::free(n);
    }
};

} // namespace aib
