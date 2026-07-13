// bench.cpp - Auxiliary-Index Benchmark (v4)
//
// Driver for the benchmark. Combines:
//   * --index {btree|hash}
//   * --workload {none|polluter|object|storage}  + --bytes / --universe
//   * --dist {uniform|zipf}  + --theta
//   * --arrival {batch|poisson}  + --rate / --cv2
//   * --op-mix s=...,u=...,i=...,d=...,sc=...
//   * --clients N  + lock-free MPMC queue with --queue-capacity
//   * Time-varying λ(t) via --sin-* / --level-* / --burst-*
//   * --hash-buckets / --hash-sigma / --hash-intervals
//
// Emits one CSV line per run; per-op p50/p99/p99.9/p99.99/max.

#include "index.hpp"
#include "hash_index.hpp"
#include "hash_key_gen.hpp"
#include "workload.hpp"
#include "perfctr.hpp"
#include "mpmc_queue.hpp"
#include "histogram.hpp"
#include "traffic_model.hpp"
#include "op_mix.hpp"
#include "index_iface.hpp"
#include "index_factory.hpp"

#include <linux/perf_event.h>

#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>

using namespace aib;
using clk    = std::chrono::steady_clock;
using nanos  = std::chrono::nanoseconds;

// ---------- Index dispatch ---------------------------------------------------
// v5+: all indexes go through IIndex (virtual). Built-in btree/hash and
// external adapters share the same interface; factory dispatches by name.

// ---------- Arguments --------------------------------------------------------

enum class Arrival { Batch, Poisson };

struct Args {
    size_t       keys           = 1'000'000;
    size_t       queries        = 1'000'000;
    WorkloadKind workload       = WorkloadKind::None;
    size_t       bytes          = 0;
    size_t       universe       = 256ULL * 1024 * 1024;
    std::string  dist           = "uniform";
    double       theta          = 0.99;
    int          repeats        = 3;
    uint64_t     seed           = 0xC0FFEE;
    Arrival      arrival        = Arrival::Batch;
    double       rate           = 0.0;
    double       cv2            = 1.0;
    int          clients        = 1;
    int          workers        = 1;   // batch-mode worker threads (E12)
    size_t       queue_capacity = 1 << 16;
    std::string  op_mix_str     = "search";
    int          scan_len       = 16;
    TrafficParams traffic;
    // v4+: index identification by name; passes through index_factory.
    std::string  index_name     = "btree";   // factory string name
    size_t       hash_buckets   = 1 << 18;
    int          hash_intervals = 8;
    double       hash_sigma     = 0.4;
    bool         hash_dom_fixed = true;
    bool         csv_output     = false;  // print machine-readable CSV after human summary
    bool         human_output   = true;
};

static WorkloadKind parse_workload(const char* s) {
    if (!std::strcmp(s, "none"))     return WorkloadKind::None;
    if (!std::strcmp(s, "polluter")) return WorkloadKind::Polluter;
    if (!std::strcmp(s, "object"))   return WorkloadKind::ObjectAccess;
    if (!std::strcmp(s, "storage"))  return WorkloadKind::StorageStack;
    std::fprintf(stderr, "unknown workload: %s\n", s); std::exit(2);
}
static const char* workload_name(WorkloadKind w) {
    switch (w) {
        case WorkloadKind::None:         return "none";
        case WorkloadKind::Polluter:     return "polluter";
        case WorkloadKind::ObjectAccess: return "object";
        case WorkloadKind::StorageStack: return "storage";
    }
    return "?";
}
static Arrival parse_arrival(const char* s) {
    if (!std::strcmp(s, "batch"))   return Arrival::Batch;
    if (!std::strcmp(s, "poisson")) return Arrival::Poisson;
    std::fprintf(stderr, "unknown arrival: %s\n", s); std::exit(2);
}
static const char* arrival_name(Arrival a) {
    return a == Arrival::Batch ? "batch" : "poisson";
}
static size_t pow2_ceil(size_t x) { size_t v = 1; while (v < x) v <<= 1; return v; }

static Args parse_args(int argc, char** argv) {
    Args a;
    a.traffic.base_rate    = 0.0;
    a.traffic.sin_amp      = 0.0;
    a.traffic.sin_period_s = 1.0;
    a.traffic.level_period_s = 0.0;
    a.traffic.level_lo     = 1.0;
    a.traffic.level_hi     = 1.0;
    a.traffic.burst_prob   = 0.0;
    a.traffic.burst_tick_s = 0.1;
    a.traffic.burst_dur_mean_s   = 0.05;
    a.traffic.burst_pareto_alpha = 1.5;
    for (int i = 1; i < argc; ++i) {
        auto eat = [&](const char* flag) {
            return !std::strcmp(argv[i], flag) && i + 1 < argc;
        };
        if      (eat("--keys"))           a.keys     = std::strtoull(argv[++i], nullptr, 10);
        else if (eat("--queries"))        a.queries  = std::strtoull(argv[++i], nullptr, 10);
        else if (eat("--workload"))       a.workload = parse_workload(argv[++i]);
        else if (eat("--bytes"))          a.bytes    = std::strtoull(argv[++i], nullptr, 10);
        else if (eat("--universe"))       a.universe = std::strtoull(argv[++i], nullptr, 10);
        else if (eat("--dist"))           a.dist     = argv[++i];
        else if (eat("--theta"))          a.theta    = std::strtod(argv[++i], nullptr);
        else if (eat("--repeats"))        a.repeats  = std::atoi(argv[++i]);
        else if (eat("--seed"))           a.seed     = std::strtoull(argv[++i], nullptr, 10);
        else if (eat("--arrival"))        a.arrival  = parse_arrival(argv[++i]);
        else if (eat("--rate"))           a.rate     = std::strtod(argv[++i], nullptr);
        else if (eat("--cv2"))            a.cv2      = std::strtod(argv[++i], nullptr);
        else if (eat("--clients"))        a.clients  = std::atoi(argv[++i]);
        else if (eat("--workers"))        a.workers  = std::atoi(argv[++i]);
        else if (eat("--queue-capacity")) {
            // 0 = auto: size the queue to hold the whole query stream in
            // Poisson mode, so the producer never blocks on a full queue
            // (backpressure silently clamps the offered rate and hides
            // queueing delay from queue_mean_ns).
            size_t qc = std::strtoull(argv[++i], nullptr, 10);
            a.queue_capacity = qc == 0 ? 0 : pow2_ceil(qc);
        }
        else if (eat("--op-mix"))         a.op_mix_str = argv[++i];
        else if (eat("--scan-len"))       a.scan_len   = std::atoi(argv[++i]);
        else if (eat("--sin-amp"))        a.traffic.sin_amp        = std::strtod(argv[++i], nullptr);
        else if (eat("--sin-period"))     a.traffic.sin_period_s   = std::strtod(argv[++i], nullptr);
        else if (eat("--level-period"))   a.traffic.level_period_s = std::strtod(argv[++i], nullptr);
        else if (eat("--level-lo"))       a.traffic.level_lo       = std::strtod(argv[++i], nullptr);
        else if (eat("--level-hi"))       a.traffic.level_hi       = std::strtod(argv[++i], nullptr);
        else if (eat("--burst-prob"))     a.traffic.burst_prob     = std::strtod(argv[++i], nullptr);
        else if (eat("--burst-tick"))     a.traffic.burst_tick_s   = std::strtod(argv[++i], nullptr);
        else if (eat("--burst-dur-mean")) a.traffic.burst_dur_mean_s   = std::strtod(argv[++i], nullptr);
        else if (eat("--burst-pareto"))   a.traffic.burst_pareto_alpha = std::strtod(argv[++i], nullptr);
        else if (eat("--index")) a.index_name = argv[++i];
        else if (eat("--hash-buckets"))   a.hash_buckets   = pow2_ceil(std::strtoull(argv[++i], nullptr, 10));
        else if (eat("--hash-intervals")) a.hash_intervals = std::atoi(argv[++i]);
        else if (eat("--hash-sigma"))     a.hash_sigma     = std::strtod(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--hash-dom-resample")) a.hash_dom_fixed = false;
        else if (!std::strcmp(argv[i], "--csv")) a.csv_output = true;
        else if (!std::strcmp(argv[i], "--no-human")) a.human_output = false;
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); std::exit(2); }
    }
    return a;
}

// ---------- Zipf -------------------------------------------------------------

class ZipfGen {
public:
    ZipfGen(size_t n, double theta, uint64_t seed)
        : n_(n), theta_(theta), rng_(seed) {
        // The YCSB rejection-free formula has a singularity at theta == 1
        // (alpha = 1/(1-theta) blows up). It is valid on both sides of 1,
        // so nudge exact 1.0 off the pole rather than crash or emit NaN.
        if (std::abs(theta_ - 1.0) < 1e-9) {
            std::fprintf(stderr,
                "[zipf] WARNING: theta=1.0 is a singularity of the YCSB "
                "generator; using theta=0.9999 instead.\n");
            theta_ = 0.9999;
        }
        zetan_ = 0.0;
        for (size_t i = 1; i <= n_; ++i) zetan_ += std::pow((double)i, -theta_);
        zeta2_ = 1.0 + std::pow(0.5, theta_);
        alpha_ = 1.0 / (1.0 - theta_);
        eta_   = (1.0 - std::pow(2.0 / n_, 1.0 - theta_)) / (1.0 - zeta2_ / zetan_);
    }
    size_t next() {
        double u  = uni_(rng_);
        double uz = u * zetan_;
        if (uz < 1.0)                         return 0;
        if (uz < 1.0 + std::pow(0.5, theta_)) return 1;
        size_t v = (size_t)((n_ - 1) * std::pow(eta_ * u - eta_ + 1.0, alpha_));
        if (v >= n_) v = n_ - 1;
        return v;
    }
private:
    size_t n_;
    double theta_, zetan_, zeta2_, alpha_, eta_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uni_{0.0, 1.0};
};

// ---------- Queue / time helpers --------------------------------------------

struct QueueEntry {
    idx_key_t key;
    uint64_t  t_enqueue_ns;
    Op        op;
    int       scan_len;
};
static_assert(sizeof(QueueEntry) <= 32, "keep entry small");

static inline uint64_t now_ns(clk::time_point t0) {
    return (uint64_t)std::chrono::duration_cast<nanos>(clk::now() - t0).count();
}
static inline void aib_cpu_pause() {
#if defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}
static inline void spin_until(clk::time_point t0, uint64_t target_ns) {
    for (;;) { if (now_ns(t0) >= target_ns) return; aib_cpu_pause(); }
}

// ---------- Shared state -----------------------------------------------------

struct SharedState {
    MPMCQueue<QueueEntry>* q = nullptr;
    IIndex*                idx = nullptr;
    Workload*              work = nullptr;
    std::shared_mutex      idx_lock;
    bool                   needs_lock = false;
    std::atomic<bool>      done{false};
    std::atomic<uint64_t>  produced{0};
    std::atomic<uint64_t>  consumed{0};
};

struct OpHists {
    Histogram per_op[5];
    Histogram total;             // service time (lookup + polluter)
    Histogram total_queue;       // queue wait only
    Histogram total_e2e;         // end-to-end (queue + service)
    Histogram total_lookup;      // lookup only (t_lookup - t_start)
    Histogram total_poll;        // polluter only (t_end - t_lookup)
    void merge(const OpHists& o) {
        for (int i = 0; i < 5; ++i) per_op[i].merge(o.per_op[i]);
        total.merge(o.total);
        total_queue.merge(o.total_queue);
        total_e2e.merge(o.total_e2e);
        total_lookup.merge(o.total_lookup);
        total_poll.merge(o.total_poll);
    }
};

// ---------- Client thread ----------------------------------------------------

static void client_thread(SharedState* s, clk::time_point t0, OpHists* h) {
    QueueEntry e;
    volatile uint64_t sink = 0;
    int idle = 0;
    for (;;) {
        if (!s->q->try_dequeue(e)) {
            if (s->done.load(std::memory_order_acquire) &&
                s->q->approx_size() == 0) break;
            // Backoff: spin briefly for latency, then yield the core.
            // Pure busy-spin here starves the single producer when
            // clients ≈ cores (observed in E7: 16 spinning consumers
            // collapse the offered rate).
            if (++idle < 64) aib_cpu_pause();
            else std::this_thread::yield();
            continue;
        }
        idle = 0;
        uint64_t t_start = now_ns(t0);
        uint64_t v = 0;
        idx_val_t scan_sink = 0;
        switch (e.op) {
            case Op::Search:
                if (s->needs_lock) {
                    std::shared_lock<std::shared_mutex> lk(s->idx_lock);
                    v = s->idx->lookup(e.key);
                } else { v = s->idx->lookup(e.key); }
                break;
            case Op::Update:
            case Op::Insert: {
                std::unique_lock<std::shared_mutex> lk(s->idx_lock);
                s->idx->update(e.key, e.key ^ 0xA5A5A5A5);
                v = e.key;
                break;
            }
            case Op::Delete: {
                std::unique_lock<std::shared_mutex> lk(s->idx_lock);
                s->idx->remove(e.key);
                v = e.key;
                break;
            }
            case Op::Scan: {
                std::shared_lock<std::shared_mutex> lk(s->idx_lock);
                s->idx->scan(e.key, e.scan_len, &scan_sink);
                v = (uint64_t)scan_sink;
                break;
            }
        }
        uint64_t t_lookup = now_ns(t0);
        v = s->work->run(v);
        uint64_t t_end = now_ns(t0);
        h->per_op[(int)e.op].add(t_end - t_start);
        h->total.add(t_end - t_start);
        h->total_lookup.add(t_lookup - t_start);
        h->total_poll.add(t_end - t_lookup);
        h->total_queue.add(t_start - e.t_enqueue_ns);
        h->total_e2e.add(t_end - e.t_enqueue_ns);
        sink ^= v;
        s->consumed.fetch_add(1, std::memory_order_relaxed);
    }
    (void)sink;
}

// ---------- Inter-arrival ----------------------------------------------------

class InterArrivalGen {
public:
    InterArrivalGen(double rate, double cv2, uint64_t seed) : rng_(seed) {
        set_rate(rate, cv2);
    }
    // Only reconstruct the gamma distribution when cv² actually changes.
    // Rate changes are absorbed by scaling the sample post-hoc: a sample
    // from Gamma(k, 1) has mean k, so sample/(rate*k) has mean 1/rate.
    void set_rate(double rate, double cv2) {
        if (cv2 <= 0) cv2 = 1e-6;
        if (rate <= 0) rate = 1.0;
        double k = 1.0 / cv2;
        if (!initialized_ || std::abs(cv2 - cur_cv2_) > 1e-12) {
            gamma_ = std::gamma_distribution<double>(k, 1.0);
            cur_cv2_ = cv2;
            cur_k_   = k;
            initialized_ = true;
        }
        cur_rate_ = rate;
    }
    uint64_t next_ns() {
        double s = gamma_(rng_) / (cur_rate_ * cur_k_);
        if (s < 0) s = 0;
        double ns = s * 1e9;
        if (ns > 1e18) ns = 1e18;
        return (uint64_t)ns;
    }
private:
    std::mt19937_64 rng_;
    std::gamma_distribution<double> gamma_;
    double cur_cv2_ = 0.0;
    double cur_k_   = 1.0;
    double cur_rate_ = 1.0;
    bool   initialized_ = false;
};

// ---------- Run results ------------------------------------------------------

struct RunResult {
    double  wall_ns_total = 0;
    OpHists h;
    uint64_t produced = 0;
    uint64_t consumed = 0;
    uint64_t producer_lag_ns_total = 0;
    double   lambda_mean = 0, lambda_min = 0, lambda_max = 0;
};

// ---------- Batch mode -------------------------------------------------------
//
// workers == 1 reproduces the original single-threaded closed loop.
// workers > 1 shards the query stream into contiguous ranges, one per
// worker thread; all workers start together (barrier) and hammer the
// index concurrently. Locking:
//   * read-only mix                  → no lock (any worker count)
//   * writes + idx->thread_safe()    → no lock; the index's own
//                                      concurrency control is exercised
//   * writes + !thread_safe()        → global shared_mutex (writers
//                                      exclusive, readers shared) — the
//                                      index is serialised; reported
//                                      throughput measures that too.

struct BatchWorkerArgs {
    IIndex* idx;
    Workload* work;                  // per-worker Workload instance
    const std::vector<idx_key_t>* qkeys;
    size_t begin, end;
    const OpMixSampler* mix;
    bool use_lock;
    std::shared_mutex* idx_lock;
    int scan_len;
    uint32_t lcg_seed;
    clk::time_point t0;
    OpHists* h;
    std::atomic<int>* start_gate;    // simple spin barrier
};

static void batch_worker(BatchWorkerArgs a) {
    // Barrier: everyone decrements, then spins until zero.
    a.start_gate->fetch_sub(1, std::memory_order_acq_rel);
    while (a.start_gate->load(std::memory_order_acquire) > 0) aib_cpu_pause();

    volatile uint64_t sink = 0;
    uint32_t lcg = a.lcg_seed;
    for (size_t i = a.begin; i < a.end; ++i) {
        lcg = lcg * 1664525u + 1013904223u;
        Op op = a.mix->sample(lcg);
        idx_key_t k = (*a.qkeys)[i];
        uint64_t t_start = now_ns(a.t0);
        idx_val_t scan_sink = 0;
        uint64_t v = 0;
        switch (op) {
            case Op::Search:
                if (a.use_lock) {
                    std::shared_lock<std::shared_mutex> lk(*a.idx_lock);
                    v = a.idx->lookup(k);
                } else { v = a.idx->lookup(k); }
                break;
            case Op::Update:
            case Op::Insert: {
                // NOTE: Insert is an upsert of a key inside the loaded
                // range — it dirties node cache lines and exercises the
                // write path, but does not grow the tree (no SMO). True
                // inserts have index-specific semantics; see docs.
                if (a.use_lock) {
                    std::unique_lock<std::shared_mutex> lk(*a.idx_lock);
                    a.idx->update(k, k ^ 0xA5A5A5A5);
                } else { a.idx->update(k, k ^ 0xA5A5A5A5); }
                v = k;
                break;
            }
            case Op::Delete: {
                if (a.use_lock) {
                    std::unique_lock<std::shared_mutex> lk(*a.idx_lock);
                    a.idx->remove(k);
                } else { a.idx->remove(k); }
                v = k;
                break;
            }
            case Op::Scan: {
                if (a.use_lock) {
                    std::shared_lock<std::shared_mutex> lk(*a.idx_lock);
                    a.idx->scan(k, a.scan_len, &scan_sink);
                } else { a.idx->scan(k, a.scan_len, &scan_sink); }
                v = (uint64_t)scan_sink;
                break;
            }
        }
        uint64_t t_lookup = now_ns(a.t0);
        v = a.work->run(v);
        uint64_t t_end = now_ns(a.t0);
        a.h->per_op[(int)op].add(t_end - t_start);
        a.h->total.add(t_end - t_start);
        a.h->total_e2e.add(t_end - t_start);
        a.h->total_lookup.add(t_lookup - t_start);
        a.h->total_poll.add(t_end - t_lookup);
        sink ^= v;
    }
    (void)sink;
}

static RunResult run_batch(IIndex* idx, std::vector<Workload>& works,
                           const std::vector<idx_key_t>& qkeys,
                           const OpMixSampler& mix, bool use_lock,
                           int scan_len, int workers, uint64_t seed) {
    RunResult r;
    std::shared_mutex idx_lock;
    if (workers < 1) workers = 1;

    std::vector<OpHists> per_thread(workers);
    std::atomic<int> gate(workers);
    auto t0 = clk::now();

    std::vector<std::thread> ts;
    size_t per = qkeys.size() / workers;
    for (int w = 0; w < workers; ++w) {
        size_t b = (size_t)w * per;
        size_t e = (w == workers - 1) ? qkeys.size() : b + per;
        BatchWorkerArgs wa{
            idx, &works[(size_t)w], &qkeys, b, e, &mix, use_lock, &idx_lock,
            scan_len, (uint32_t)(seed ^ (0xCAFEBABEu + 0x9E37u * (uint32_t)w)),
            t0, &per_thread[(size_t)w], &gate};
        ts.emplace_back(batch_worker, wa);
    }
    for (auto& t : ts) t.join();
    auto t1 = clk::now();
    r.wall_ns_total = std::chrono::duration<double, std::nano>(t1 - t0).count();
    r.produced = r.consumed = qkeys.size();
    for (auto& h : per_thread) r.h.merge(h);
    return r;
}

// ---------- Poisson mode -----------------------------------------------------

static RunResult run_poisson(IIndex* idx, Workload& work,
                             const std::vector<idx_key_t>& qkeys,
                             const Args& a) {
    RunResult r;
    MPMCQueue<QueueEntry> q(a.queue_capacity);
    SharedState s;
    s.q = &q; s.idx = idx; s.work = &work;

    OpMix mix = parse_opmix(a.op_mix_str);
    OpMixSampler mix_sampler(mix);
    s.needs_lock = (mix.update + mix.insert + mix.del) > 0
                   && a.clients > 1 && !idx->thread_safe();

    // ---- Pregenerate the arrival schedule -------------------------------
    // Gamma sampling costs ~100-300 ns per draw; at Mops-scale offered
    // rates that alone exceeds the inter-arrival budget, so a producer
    // that samples inline falls behind schedule and its own catch-up
    // bursts swamp the burstiness (cv²) and traffic-shape signals the
    // experiment is trying to inject (observed as huge producer lag and
    // non-monotonic queue delays in E4/E8). Generating the entire
    // schedule and the per-op choices BEFORE the clock starts reduces
    // the producer's runtime work to spin-until-deadline + enqueue.
    // Memory: 9 B/query (0.9 MB per 100K queries).
    std::vector<uint64_t> sched(qkeys.size());
    std::vector<uint8_t>  op_choice(qkeys.size());
    double lam_sum = 0, lam_min = 1e30, lam_max = 0;
    {
        TrafficParams tp = a.traffic;
        tp.base_rate = a.rate;
        // Estimated experiment duration for the sine mean correction:
        //   duration ≈ queries / rate (approximate on purpose; the
        // partial-cycle correction only needs the right ballpark).
        tp.total_duration_s = (a.rate > 0) ? (double)qkeys.size() / a.rate : 0.0;
        TrafficModel traffic(tp, a.seed ^ 0xC0FFEE99);
        InterArrivalGen ia(a.rate, a.cv2, a.seed ^ 0x12345);
        uint32_t lcg = (uint32_t)(a.seed ^ 0xDEADBEEF);
        uint64_t t = 0;
        for (size_t i = 0; i < qkeys.size(); ++i) {
            double lam = traffic.lambda_at(t * 1e-9);
            lam_sum += lam;
            if (lam < lam_min) lam_min = lam;
            if (lam > lam_max) lam_max = lam;
            ia.set_rate(lam, a.cv2);
            t += ia.next_ns();
            sched[i] = t;
            lcg = lcg * 1664525u + 1013904223u;
            op_choice[i] = (uint8_t)mix_sampler.sample(lcg);
        }
    }

    std::vector<OpHists> per_thread(a.clients);
    std::vector<std::thread> clients;
    auto t0 = clk::now();
    for (int i = 0; i < a.clients; ++i)
        clients.emplace_back(client_thread, &s, t0, &per_thread[i]);

    uint64_t lag_total = 0;

    for (size_t i = 0; i < qkeys.size(); ++i) {
        spin_until(t0, sched[i]);
        QueueEntry e{qkeys[i], now_ns(t0), (Op)op_choice[i], a.scan_len};
        lag_total += e.t_enqueue_ns - sched[i];

        int idle = 0;
        while (!q.try_enqueue(e)) {
            if (++idle < 64) aib_cpu_pause();
            else std::this_thread::yield();
            e.t_enqueue_ns = now_ns(t0);
        }
        s.produced.fetch_add(1, std::memory_order_relaxed);
    }
    s.done.store(true, std::memory_order_release);
    for (auto& c : clients) c.join();
    auto t1 = clk::now();
    r.wall_ns_total = std::chrono::duration<double, std::nano>(t1 - t0).count();
    r.produced = s.produced.load();
    r.consumed = s.consumed.load();
    r.producer_lag_ns_total = lag_total;
    r.lambda_mean = lam_sum / std::max<size_t>(1, qkeys.size());
    r.lambda_min  = lam_min == 1e30 ? 0 : lam_min;
    r.lambda_max  = lam_max;
    for (auto& h : per_thread) r.h.merge(h);
    return r;
}

// ---------- Human-readable / CSV emit ----------------------------------------

static std::string format_count(uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

static std::string format_ns(double ns) {
    char buf[64];
    if (ns < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.0f ns", ns);
    } else if (ns < 1000.0 * 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.2f us", ns / 1000.0);
    } else if (ns < 1000.0 * 1000.0 * 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.2f ms", ns / 1e6);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f s", ns / 1e9);
    }
    return std::string(buf);
}

static double throughput_mops(const RunResult& r) {
    double sec = r.wall_ns_total * 1e-9;
    return sec > 0.0 ? (double)r.consumed / sec / 1e6 : 0.0;
}

static void emit_human_setup(const Args& a, size_t bytes_per_call,
                             const IIndex* idx, const OpMix& mix,
                             bool needs_lock) {
    std::printf("\n=== Benchmark configuration ===\n");
    std::printf("Index        : %s %s\n", idx->name(), idx->diag().c_str());
    std::printf("Workload     : %s", workload_name(a.workload));
    if (bytes_per_call > 0) std::printf(" (%zu B extra data access per op)", bytes_per_call);
    std::printf("\n");
    std::printf("Data size    : %s keys, %s operations\n",
                format_count(a.keys).c_str(), format_count(a.queries).c_str());
    std::printf("Key dist.    : %s", a.dist.c_str());
    if (a.dist == "zipf") std::printf(" (theta=%.3f)", a.theta);
    std::printf("\n");
    std::printf("Arrival      : %s", arrival_name(a.arrival));
    if (a.arrival == Arrival::Poisson)
        std::printf(" (target rate=%.0f ops/s, cv2=%.3f)", a.rate, a.cv2);
    std::printf("\n");
    if (a.arrival == Arrival::Poisson) {
        std::printf("Clients      : %d (queue capacity=%zu)\n",
                    a.clients, a.queue_capacity);
    } else {
        std::printf("Workers      : %d (closed-loop batch)\n", a.workers);
    }
    std::printf("Operation mix: search %.1f%%, update %.1f%%, insert %.1f%%, delete %.1f%%, scan %.1f%%",
                100.0 * mix.search, 100.0 * mix.update, 100.0 * mix.insert,
                100.0 * mix.del, 100.0 * mix.scan);
    if (mix.scan > 0.0) std::printf(" (scan_len=%d)", a.scan_len);
    std::printf("\n");
    std::printf("Locking      : %s\n",
                needs_lock ? "global RW-lock (index not thread-safe)"
                : (mix.search < 1.0)
                    ? "elided (single thread, or index has own concurrency control)"
                    : "not needed for read-only workload");
    std::printf("===============================\n");
}

static void emit_human_run(int run_id, const RunResult& r) {
    const Histogram& T = r.h.total;
    const Histogram& Q = r.h.total_queue;
    std::printf("\nRun %d\n", run_id);
    std::printf("  elapsed time : %.3f ms\n", r.wall_ns_total / 1e6);
    std::printf("  throughput   : %.3f Mops/s\n", throughput_mops(r));
    std::printf("  operations   : produced %s, consumed %s\n",
                format_count(r.produced).c_str(), format_count(r.consumed).c_str());
    std::printf("  service time : mean %s, p50 %s, p99 %s, p99.9 %s, p99.99 %s, max %s\n",
                format_ns(T.mean_ns()).c_str(),
                format_ns((double)T.percentile(0.50)).c_str(),
                format_ns((double)T.percentile(0.99)).c_str(),
                format_ns((double)T.percentile(0.999)).c_str(),
                format_ns((double)T.percentile(0.9999)).c_str(),
                format_ns((double)T.max_ns()).c_str());
    std::printf("  queue delay  : mean %s, p99 %s, max %s\n",
                format_ns(Q.mean_ns()).c_str(),
                format_ns((double)Q.percentile(0.99)).c_str(),
                format_ns((double)Q.max_ns()).c_str());
    if (r.lambda_mean > 0.0 || r.lambda_max > 0.0) {
        std::printf("  arrival rate : min %.0f, mean %.0f, max %.0f ops/s\n",
                    r.lambda_min, r.lambda_mean, r.lambda_max);
        std::printf("  producer lag : total %s\n",
                    format_ns((double)r.producer_lag_ns_total).c_str());
    }
}

static void emit_human_op_breakdown(const RunResult& r) {
    std::printf("\nOperation latency breakdown\n");
    std::printf("  %-8s %12s %12s %12s\n", "op", "mean", "p99", "p99.99");
    for (int i = 0; i < 5; ++i) {
        const Histogram& H = r.h.per_op[i];
        if (H.total() == 0) continue;
        std::printf("  %-8s %12s %12s %12s\n",
                    op_name((Op)i),
                    format_ns(H.mean_ns()).c_str(),
                    format_ns((double)H.percentile(0.99)).c_str(),
                    format_ns((double)H.percentile(0.9999)).c_str());
    }
}

static void emit_human_best(const RunResult& best, int repeats) {
    std::printf("\n=== Summary (throughput: best run; latency: all %d runs merged) ===\n",
                repeats);
    std::printf("Throughput   : %.3f Mops/s\n", throughput_mops(best));
    std::printf("Elapsed time : %.3f ms\n", best.wall_ns_total / 1e6);
    std::printf("Mean latency : %s\n", format_ns(best.h.total.mean_ns()).c_str());
    std::printf("p99 latency  : %s\n", format_ns((double)best.h.total.percentile(0.99)).c_str());
    std::printf("p99.9 latency: %s\n", format_ns((double)best.h.total.percentile(0.999)).c_str());
    std::printf("p99.99 lat.  : %s\n", format_ns((double)best.h.total.percentile(0.9999)).c_str());
    emit_human_op_breakdown(best);
    std::printf("========================\n");
}

static void emit_csv_header() {
    std::printf("workload,bytes_per_call,universe,dist,theta,arrival,rate,cv2,clients,"
                "workers,"
                "queue_capacity,op_mix,scan_len,sin_amp,sin_period,level_period,"
                "level_lo,level_hi,burst_prob,keys,queries,wall_ns,throughput_mops,"
                "produced,"
                "consumed,producer_lag_ns,lambda_mean,lambda_min,lambda_max,"
                "svc_mean_ns,svc_p50_ns,svc_p99_ns,svc_p999_ns,svc_p9999_ns,"
                "svc_max_ns,queue_mean_ns,queue_p50_ns,queue_p99_ns,queue_p999_ns,"
                "queue_p9999_ns,queue_max_ns,"
                "e2e_mean_ns,e2e_p50_ns,e2e_p99_ns,e2e_p999_ns,e2e_p9999_ns,"
                "e2e_max_ns,"
                "lookup_mean_ns,lookup_p50_ns,lookup_p99_ns,lookup_p999_ns,"
                "lookup_p9999_ns,lookup_max_ns,"
                "poll_mean_ns,poll_p50_ns,poll_p99_ns,poll_p999_ns,"
                "poll_p9999_ns,poll_max_ns");
    for (int i = 0; i < 5; ++i) {
        std::printf(",%s_mean_ns,%s_p99_ns,%s_p9999_ns",
                    op_name((Op)i), op_name((Op)i), op_name((Op)i));
    }
    std::printf("\n");
}

static void emit_csv(const Args& a, size_t bytes_per_call,
                     const RunResult& best, const OpMix& mix) {
    const Histogram& T = best.h.total;
    const Histogram& Q = best.h.total_queue;
    const Histogram& E = best.h.total_e2e;
    const Histogram& L = best.h.total_lookup;
    const Histogram& P = best.h.total_poll;
    emit_csv_header();
    std::printf(
        "%s,%zu,%zu,%s,%.3f,%s,%.0f,%.3f,%d,%d,%zu,"
        "\"%s\",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,"
        "%zu,%zu,%.0f,%.4f,%lu,%lu,%lu,"
        "%.0f,%.0f,%.0f,"
        "%.2f,%lu,%lu,%lu,%lu,%lu,"
        "%.2f,%lu,%lu,%lu,%lu,%lu,"
        "%.2f,%lu,%lu,%lu,%lu,%lu,"
        "%.2f,%lu,%lu,%lu,%lu,%lu,"
        "%.2f,%lu,%lu,%lu,%lu,%lu",
        workload_name(a.workload), bytes_per_call, a.universe,
        a.dist.c_str(), a.theta,
        arrival_name(a.arrival), a.rate, a.cv2, a.clients, a.workers,
        a.queue_capacity,
        a.op_mix_str.c_str(), a.scan_len,
        a.traffic.sin_amp, a.traffic.sin_period_s,
        a.traffic.level_period_s, a.traffic.level_lo, a.traffic.level_hi,
        a.traffic.burst_prob,
        a.keys, a.queries, best.wall_ns_total, throughput_mops(best),
        (unsigned long)best.produced, (unsigned long)best.consumed,
        (unsigned long)best.producer_lag_ns_total,
        best.lambda_mean, best.lambda_min, best.lambda_max,
        T.mean_ns(),
        (unsigned long)T.percentile(0.50),
        (unsigned long)T.percentile(0.99),
        (unsigned long)T.percentile(0.999),
        (unsigned long)T.percentile(0.9999),
        (unsigned long)T.max_ns(),
        Q.mean_ns(),
        (unsigned long)Q.percentile(0.50),
        (unsigned long)Q.percentile(0.99),
        (unsigned long)Q.percentile(0.999),
        (unsigned long)Q.percentile(0.9999),
        (unsigned long)Q.max_ns(),
        E.mean_ns(),
        (unsigned long)E.percentile(0.50),
        (unsigned long)E.percentile(0.99),
        (unsigned long)E.percentile(0.999),
        (unsigned long)E.percentile(0.9999),
        (unsigned long)E.max_ns(),
        L.mean_ns(),
        (unsigned long)L.percentile(0.50),
        (unsigned long)L.percentile(0.99),
        (unsigned long)L.percentile(0.999),
        (unsigned long)L.percentile(0.9999),
        (unsigned long)L.max_ns(),
        P.mean_ns(),
        (unsigned long)P.percentile(0.50),
        (unsigned long)P.percentile(0.99),
        (unsigned long)P.percentile(0.999),
        (unsigned long)P.percentile(0.9999),
        (unsigned long)P.max_ns());
    for (int i = 0; i < 5; ++i) {
        const Histogram& Hi = best.h.per_op[i];
        std::printf(",%.2f,%lu,%lu",
            Hi.mean_ns(),
            (unsigned long)Hi.percentile(0.99),
            (unsigned long)Hi.percentile(0.9999));
    }
    std::printf("\n");
    (void)mix;
}

// ---------- Main -------------------------------------------------------------

int main(int argc, char** argv) {
    Args a = parse_args(argc, argv);

    // ----- Key generation ----------------------------------------------------
    // For hash-style indexes (builtin-hash or any external one a user opts to
    // drive with synthetic skewed keys), generate via the bit-bias model.
    // For btree-style we use dense ascending keys. The heuristic: if the
    // index name contains "hash" the bias generator runs; otherwise keys are
    // 1..N. Override with --hash-sigma 0 to disable the bias entirely.
    const bool use_skew_gen =
        (a.index_name == "hash" || a.index_name == "builtin-hash");

    std::vector<idx_key_t> keys(a.keys);
    std::vector<idx_val_t> vals(a.keys);
    if (use_skew_gen) {
        HashKeyGen hgen(a.hash_intervals, a.hash_sigma,
                        a.seed ^ 0xBEEFCAFEULL, a.hash_dom_fixed);
        for (size_t i = 0; i < a.keys; ++i) {
            keys[i] = (idx_key_t)hgen.next();
            vals[i] = (idx_val_t)(i + 1);
        }
    } else {
        for (size_t i = 0; i < a.keys; ++i) {
            keys[i] = (idx_key_t)(i + 1);
            vals[i] = (idx_val_t)(i + 1);
        }
    }

    // ----- Build the index via factory --------------------------------------
    IndexConfig icfg;
    icfg.hash_buckets = a.hash_buckets;
    std::unique_ptr<IIndex> idx_owned = make_index(a.index_name, icfg);
    IIndex* idx = idx_owned.get();
    idx->bulk_load(keys, vals);
    if (!a.human_output && !a.csv_output)
        std::fprintf(stderr, "[setup] index=%s %s\n",
                     idx->name(), idx->diag().c_str());

    // Workload.
    size_t bytes_per_call = a.bytes;
    if (a.workload == WorkloadKind::ObjectAccess && bytes_per_call == 0)
        bytes_per_call = 4096;
    if (a.workload == WorkloadKind::StorageStack && bytes_per_call == 0)
        bytes_per_call = 4096;
    if (a.workers < 1) a.workers = 1;
    // One Workload instance per batch worker: sharing a single buffer
    // across workers would add coherence traffic on the polluter buffer
    // itself, confounding the index coherence effects E12 isolates.
    std::vector<Workload> works;
    works.reserve((size_t)a.workers);
    for (int w = 0; w < a.workers; ++w)
        works.emplace_back(a.workload, bytes_per_call, a.universe,
                           a.seed ^ (0xABCDEFULL + 0x9E3779B9ULL * (uint64_t)w));
    if (!a.human_output && !a.csv_output)
        std::fprintf(stderr,
            "[setup] workload=%s per_call=%zu B universe=%.2f MB x%d worker(s)\n",
            workload_name(a.workload), bytes_per_call,
            a.universe / (1024.0 * 1024.0), a.workers);

    // Op mix.
    OpMix mix = parse_opmix(a.op_mix_str);
    OpMixSampler mix_sampler(mix);
    // Lock policy: the global shared_mutex is a fallback for indexes with
    // no internal concurrency control. Engaged only when the mix contains
    // writes, more than one thread touches the index, and the index does
    // not declare thread_safe().
    bool has_writes = (mix.update + mix.insert + mix.del) > 0;
    int  index_threads = (a.arrival == Arrival::Batch) ? a.workers : a.clients;
    bool needs_lock = has_writes && index_threads > 1 && !idx->thread_safe();
    if (!a.human_output && !a.csv_output)
        std::fprintf(stderr,
            "[setup] op_mix s=%.2f u=%.2f i=%.2f d=%.2f sc=%.2f  scan_len=%d  needs_lock=%d\n",
            mix.search, mix.update, mix.insert, mix.del, mix.scan, a.scan_len, (int)needs_lock);

    // Query stream.
    std::vector<idx_key_t> qkeys(a.queries);
    if (a.dist == "uniform") {
        std::mt19937_64 rng(a.seed);
        std::uniform_int_distribution<size_t> di(0, a.keys - 1);
        for (size_t i = 0; i < a.queries; ++i) qkeys[i] = keys[di(rng)];
    } else if (a.dist == "zipf") {
        ZipfGen zg(a.keys, a.theta, a.seed);
        for (size_t i = 0; i < a.queries; ++i) qkeys[i] = keys[zg.next()];
    } else {
        std::fprintf(stderr, "unknown dist: %s\n", a.dist.c_str()); return 2;
    }

    if (!a.human_output && !a.csv_output)
        std::fprintf(stderr,
            "[setup] arrival=%s rate=%.0f cv2=%.3f clients=%d queue=%zu dist=%s theta=%.3f\n",
            arrival_name(a.arrival), a.rate, a.cv2, a.clients,
            a.queue_capacity, a.dist.c_str(), a.theta);
    if (!a.human_output && !a.csv_output && a.arrival == Arrival::Poisson) {
        std::fprintf(stderr,
            "[setup] traffic sin_amp=%.2f period=%.2fs level_period=%.2fs "
            "[%.2f..%.2f] burst_prob=%.4f tick=%.2fs dur=%.3fs pareto=%.2f\n",
            a.traffic.sin_amp, a.traffic.sin_period_s,
            a.traffic.level_period_s, a.traffic.level_lo, a.traffic.level_hi,
            a.traffic.burst_prob, a.traffic.burst_tick_s,
            a.traffic.burst_dur_mean_s, a.traffic.burst_pareto_alpha);
    }
    if (a.arrival == Arrival::Poisson && a.rate <= 0) {
        std::fprintf(stderr, "[error] --rate > 0 required in poisson mode\n");
        return 2;
    }
    // Resolve auto queue capacity (0): sized to hold every query so the
    // producer can never hit a full queue mid-run. Capped at 2^24 entries.
    if (a.arrival == Arrival::Poisson && a.queue_capacity == 0) {
        constexpr size_t CAP = (size_t)1 << 24;
        size_t want = pow2_ceil(a.queries);
        a.queue_capacity = std::min(want, CAP);
        if (a.queue_capacity < a.queries) {
            std::fprintf(stderr,
                "[queue] WARNING: auto capacity capped at %zu entries "
                "(< %zu queries); producer backpressure is possible.\n",
                a.queue_capacity, a.queries);
        }
    } else if (a.queue_capacity == 0) {
        a.queue_capacity = 1 << 16;  // batch mode never uses it; keep sane
    }

    if (a.human_output) emit_human_setup(a, bytes_per_call, idx, mix, needs_lock);

    // Warm-up.
    {
        volatile uint64_t s = 0;
        for (size_t i = 0; i < std::min<size_t>(a.queries, 200000); ++i)
            s ^= idx->lookup(qkeys[i]);
        (void)s;
    }

    // Repeats policy:
    //   * Scalar stats (wall time, throughput, producer lag, lambda) come
    //     from the FASTEST run — best-of-N minimises interference noise
    //     for throughput claims.
    //   * Latency histograms are MERGED across all repeats: percentiles,
    //     especially p99.9+, need samples, and in Poisson mode wall time
    //     is fixed by the arrival schedule so "best" is a coin flip.
    RunResult best; best.wall_ns_total = 1e30;
    OpHists merged;
    for (int r = 0; r < a.repeats; ++r) {
        RunResult cur = (a.arrival == Arrival::Batch)
            ? run_batch(idx, works, qkeys, mix_sampler, needs_lock,
                        a.scan_len, a.workers, a.seed)
            : run_poisson(idx, works[0], qkeys, a);
        if (a.human_output) emit_human_run(r, cur);
        merged.merge(cur.h);
        if (cur.wall_ns_total < best.wall_ns_total) best = std::move(cur);
    }
    best.h = std::move(merged);

    // Backpressure / lateness sanity check.
    if (a.arrival == Arrival::Poisson && best.produced > 0 && a.rate > 0) {
        double lag_per_op_ns = (double)best.producer_lag_ns_total / best.produced;
        double inter_arrival_ns = 1e9 / a.rate;
        if (lag_per_op_ns > 2.0 * inter_arrival_ns) {
            std::fprintf(stderr,
                "[producer] WARNING: mean producer lateness %.0f ns/op vs "
                "mean inter-arrival %.0f ns; the offered rate was not "
                "realised (queue backpressure or scheduler). Treat "
                "queue/e2e latencies with suspicion.\n",
                lag_per_op_ns, inter_arrival_ns);
        }
    }

    if (a.human_output) emit_human_best(best, a.repeats);
    if (a.csv_output) {
        if (a.human_output) std::printf("\nCSV output\n");
        emit_csv(a, bytes_per_call, best, mix);
    }
    return 0;
}
