#pragma once
#include <cmath>

// Pearson correlation between L and R over n samples: Sum(L*R)/sqrt(SumL2*SumR2),
// clamped to [-1,1]. Returns 0 when either channel is silent. +1 = mono/correlated,
// 0 = uncorrelated, -1 = anti-phase. Pure C++ — no JUCE dependency.
inline float stereoCorrelation (const float* L, const float* R, int n) noexcept
{
    if (L == nullptr || R == nullptr || n <= 0) return 0.0f;
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        lr += (double) L[i] * R[i];
        ll += (double) L[i] * L[i];
        rr += (double) R[i] * R[i];
    }
    const double denom = std::sqrt (ll * rr);
    if (! (denom > 1e-12)) return 0.0f;          // also catches NaN denom
    double c = lr / denom;
    if (! std::isfinite (c)) return 0.0f;         // scrub NaN/Inf from bad samples
    if (c > 1.0) c = 1.0; if (c < -1.0) c = -1.0;
    return (float) c;
}

// RMS level of x in dBFS, floored at -100 dB for silence. Pure C++.
inline float rmsDb (const float* x, int n) noexcept
{
    if (x == nullptr || n <= 0) return -100.0f;
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    const double rms = std::sqrt (s / (double) n);
    if (! (rms >= 1e-5)) return -100.0f;          // also catches NaN (silence floor)
    const double db = 20.0 * std::log10 (rms);
    return std::isfinite (db) ? (float) db : -100.0f;
}
