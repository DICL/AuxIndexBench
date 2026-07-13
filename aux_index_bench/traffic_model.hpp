// traffic_model.hpp - Time-varying arrival rate λ(t), rescaled so that
// the *time-averaged* λ(t) equals base_rate regardless of shape.
//
// Raw signal:
//   raw(t) = level(t) * (1 + amp*sin(2π t/period + phase)) * burst(t)
// Corrected:
//   λ(t)   = base_rate * raw(t) / E[raw]
//
// where E[raw] = E[level] * E[sinmod] * E[burst] (independent factors,
// so the product's expectation is the product of expectations). Each
// factor's expected time-average is computed analytically at
// construction time, using `total_duration_s` for the sine to handle
// partial-cycle experiments correctly.
//
// This fix resolves the E8 confounder we found: previously an
// experiment with `--sin-amp 0.5 --sin-period 0.5` and only 133 ms of
// wall time saw an effective mean of 1.99*base_rate instead of
// base_rate, because the sine only completed 27% of a cycle. Now the
// producer is fed λ(t) whose measured mean matches the target within
// sampling noise.

#pragma once
#include <cstdint>
#include <cmath>
#include <random>
#include <algorithm>

namespace aib {

struct TrafficParams {
    double base_rate     = 1e6;
    double sin_amp       = 0.0;
    double sin_period_s  = 10.0;
    double sin_phase     = 0.0;
    double level_period_s = 0.0;
    double level_lo       = 1.0;
    double level_hi       = 1.0;
    double burst_prob          = 0.0;
    double burst_tick_s        = 0.1;
    double burst_dur_mean_s    = 0.05;
    double burst_pareto_alpha  = 1.5;

    // Estimated total experiment duration in seconds. Used for the
    // sine mean correction. If 0, the sine correction assumes a full
    // period (safe when the experiment covers many cycles).
    double total_duration_s = 0.0;
};

class TrafficModel {
public:
    TrafficModel(TrafficParams p, uint64_t seed)
        : p_(p), rng_(seed), uni01_(0.0, 1.0),
          exp_burst_(1.0 / std::max(1e-6, p.burst_dur_mean_s)) {
        cur_level_       = sample_level();
        next_level_at_s_ = p_.level_period_s > 0 ? p_.level_period_s : 1e18;
        burst_until_s_   = -1.0;
        burst_mult_      = 1.0;
        next_burst_at_s_ = p_.burst_prob > 0 ? p_.burst_tick_s : 1e18;

        mean_correction_ = mean_level() * mean_sinmod() * mean_burst();
        if (mean_correction_ < 1e-9) mean_correction_ = 1.0; // paranoia

        // Warn if the experiment duration is too short for any of the
        // enabled shapes to sample its distribution. Analytical mean
        // correction cannot fix single-realisation variance.
        double T = p_.total_duration_s;
        if (T > 0.0) {
            if (p_.sin_amp != 0.0 && T < p_.sin_period_s) {
                std::fprintf(stderr,
                    "[traffic] WARNING: experiment duration %.3fs < sine period %.3fs;"
                    " the realised sine mean will vary run-to-run even after correction.\n",
                    T, p_.sin_period_s);
            }
            if (p_.level_lo != p_.level_hi && T < 3.0 * p_.level_period_s) {
                std::fprintf(stderr,
                    "[traffic] WARNING: experiment duration %.3fs < 3x level period %.3fs;"
                    " level shifts will be sparse and lambda_mean will vary run-to-run.\n",
                    T, p_.level_period_s);
            }
            if (p_.burst_prob > 0.0 && T < 20.0 * p_.burst_tick_s) {
                std::fprintf(stderr,
                    "[traffic] WARNING: experiment duration %.3fs covers fewer than 20"
                    " burst ticks; the burst mean will be dominated by single events.\n",
                    T);
            }
        }
    }

    // Producer-facing rate at time t (seconds since experiment start).
    double lambda_at(double t_s) {
        // Advance state up to t.
        while (t_s >= next_level_at_s_) {
            cur_level_        = sample_level();
            next_level_at_s_ += p_.level_period_s;
        }
        while (t_s >= next_burst_at_s_) {
            if (uni01_(rng_) < p_.burst_prob) {
                double dur  = exp_burst_(rng_);
                double u    = uni01_(rng_);
                double mult = std::pow(1.0 - u, -1.0 / p_.burst_pareto_alpha);
                burst_until_s_ = t_s + dur;
                burst_mult_    = mult;
            }
            next_burst_at_s_ += p_.burst_tick_s;
        }
        double burst  = (t_s < burst_until_s_) ? burst_mult_ : 1.0;
        double sinmod = 1.0 + p_.sin_amp *
            std::sin(2 * M_PI * t_s / p_.sin_period_s + p_.sin_phase);
        double raw = cur_level_ * sinmod * burst;
        double lam = p_.base_rate * raw / mean_correction_;
        if (lam < 1.0) lam = 1.0;
        return lam;
    }

    // Expose the correction factor for diagnostics / paper text.
    double mean_correction() const { return mean_correction_; }

private:
    TrafficParams p_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uni01_;
    std::exponential_distribution<double>  exp_burst_;
    double cur_level_;
    double next_level_at_s_;
    double burst_until_s_;
    double burst_mult_;
    double next_burst_at_s_;
    double mean_correction_ = 1.0;

    double sample_level() {
        if (p_.level_lo >= p_.level_hi) return p_.level_lo;
        return p_.level_lo + uni01_(rng_) * (p_.level_hi - p_.level_lo);
    }

    // --- Analytical mean-of-time-average of each factor ---------------
    //
    // level(t): uniform in [lo, hi] resampled every level_period_s.
    // Time-average expectation = (lo + hi) / 2.
    double mean_level() const {
        return 0.5 * (p_.level_lo + p_.level_hi);
    }

    // sinmod(t) = 1 + amp * sin(2π t/P + φ).
    // Over t ∈ [0, T]:
    //   mean = 1 + amp * P/(2π T) * (cos(φ) - cos(2π T/P + φ))
    // If T >= P (many full cycles) the second term is small; if T < P
    // (partial cycle) it can bias the mean well away from 1.
    double mean_sinmod() const {
        if (p_.sin_amp == 0.0) return 1.0;
        double T = p_.total_duration_s;
        double P = p_.sin_period_s;
        if (P <= 1e-9) return 1.0;
        if (T <= 1e-9) return 1.0;      // caller didn't supply T; assume full cycles
        double phi = p_.sin_phase;
        double w = 2.0 * M_PI * T / P;
        return 1.0 + p_.sin_amp * (P / (2.0 * M_PI * T)) *
                     (std::cos(phi) - std::cos(w + phi));
    }

    // burst(t): equals 1 outside bursts, equals a Pareto(α) sample during
    // bursts. Fraction of time inside a burst:
    //   f = burst_prob * burst_dur_mean_s / burst_tick_s   (approx.)
    // E[Pareto multiplier] with shape α (min=1) = α/(α - 1) for α > 1,
    // or undefined otherwise; we clamp α to (1.001, ∞) to keep the
    // correction finite.
    double mean_burst() const {
        if (p_.burst_prob <= 0.0 || p_.burst_tick_s <= 0.0) return 1.0;
        double f = p_.burst_prob * p_.burst_dur_mean_s / p_.burst_tick_s;
        if (f > 1.0) f = 1.0;            // saturated
        double alpha = std::max(1.001, p_.burst_pareto_alpha);
        double pareto_mean = alpha / (alpha - 1.0);
        return (1.0 - f) * 1.0 + f * pareto_mean;
    }
};

} // namespace aib
