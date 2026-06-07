#pragma once
#include <cmath>

// Subtract the arithmetic mean in place (removes DC offset). Pure C++.
inline void removeMean (float* buf, int n) noexcept
{
    if (buf == nullptr || n <= 0) return;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += buf[i];
    const float mean = (float) (sum / n);
    for (int i = 0; i < n; ++i) buf[i] -= mean;
}

// Scale in place so the peak absolute value equals `target`. No-op on silence.
inline void peakNormalize (float* buf, int n, float target = 0.9f) noexcept
{
    if (buf == nullptr || n <= 0) return;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) { const float a = std::fabs (buf[i]); if (a > peak) peak = a; }
    if (peak <= 1e-6f) return;
    const float g = target / peak;
    for (int i = 0; i < n; ++i) buf[i] *= g;
}

// Combine an L/R sample pair into a single display stream per source index:
// 0=Mono (L+R)/2, 1=Left, 2=Right, 3=Side (L-R)/2. Pure C++.
inline float combineChannel (float L, float R, int source) noexcept
{
    switch (source)
    {
        case 1:  return L;
        case 2:  return R;
        case 3:  return (L - R) * 0.5f;
        default: return (L + R) * 0.5f;
    }
}

// Index of the first upward zero-crossing (buf[i-1] < 0 <= buf[i]) in [1, searchLimit),
// or 0 if none. Anchors a captured cycle to a canonical phase (rises from zero).
inline int findRisingZero (const float* buf, int numSamples, int searchLimit) noexcept
{
    if (buf == nullptr || numSamples < 2) return 0;
    const int limit = searchLimit < numSamples ? searchLimit : numSamples;
    for (int i = 1; i < limit; ++i)
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) return i;
    return 0;
}
