// op_mix.hpp - Operation kind + mix-ratio sampler.

#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace aib {

enum class Op : uint8_t {
    Search = 0, Update = 1, Insert = 2, Delete = 3, Scan = 4,
};

inline const char* op_name(Op o) {
    switch (o) {
        case Op::Search: return "search";
        case Op::Update: return "update";
        case Op::Insert: return "insert";
        case Op::Delete: return "delete";
        case Op::Scan:   return "scan";
    }
    return "?";
}

struct OpMix {
    double search = 1.0;
    double update = 0.0;
    double insert = 0.0;
    double del    = 0.0;
    double scan   = 0.0;
};

inline OpMix parse_opmix(const std::string& s) {
    OpMix m{0,0,0,0,0};
    if (s.empty() || s == "search") return OpMix{1,0,0,0,0};
    const char* p = s.c_str();
    while (*p) {
        std::string key;
        while (*p && *p != '=') key.push_back(*p++);
        if (*p != '=') break;
        ++p;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if      (key == "s"  || key == "search") m.search = v;
        else if (key == "u"  || key == "update") m.update = v;
        else if (key == "i"  || key == "insert") m.insert = v;
        else if (key == "d"  || key == "delete") m.del    = v;
        else if (key == "sc" || key == "scan")   m.scan   = v;
        else {
            std::fprintf(stderr, "[opmix] unknown op '%s'\n", key.c_str());
            std::exit(2);
        }
        if (*p == ',') ++p;
    }
    double sum = m.search + m.update + m.insert + m.del + m.scan;
    if (sum <= 0) { std::fprintf(stderr, "[opmix] mix sums to 0\n"); std::exit(2); }
    m.search /= sum; m.update /= sum; m.insert /= sum;
    m.del    /= sum; m.scan   /= sum;
    return m;
}

class OpMixSampler {
public:
    static constexpr int BINS = 1024;
    OpMixSampler() = default;
    explicit OpMixSampler(const OpMix& m) {
        double cum[5] = {
            m.search, m.search + m.update,
            m.search + m.update + m.insert,
            m.search + m.update + m.insert + m.del, 1.0,
        };
        int bin = 0;
        for (int i = 0; i < 5; ++i) {
            int target = (int)(cum[i] * BINS);
            if (target > BINS) target = BINS;
            while (bin < target) table_[bin++] = (Op)i;
        }
        while (bin < BINS) table_[bin++] = Op::Search;
    }
    inline Op sample(uint32_t rand) const { return table_[rand % BINS]; }
private:
    std::array<Op, BINS> table_{};
};

} // namespace aib
