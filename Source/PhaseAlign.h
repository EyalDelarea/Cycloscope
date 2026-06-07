#pragma once
#include <vector>

// Integer circular shift s in [0,n) that maximizes Sum a[(i+s)%n] * ref[i].
// Used to rotate a freshly-captured cycle into phase with the previously-held one,
// stabilizing the display frame-to-frame. Pure C++ — no JUCE dependency.
inline int bestCircularShift (const float* a, const float* ref, int n) noexcept
{
    if (a == nullptr || ref == nullptr || n <= 0) return 0;
    int best = 0; double bestCorr = -1e300;
    for (int s = 0; s < n; ++s)
    {
        double c = 0.0;
        for (int i = 0; i < n; ++i) c += (double) a[(i + s) % n] * (double) ref[i];
        if (c > bestCorr) { bestCorr = c; best = s; }
    }
    return best;
}

// Rotate a[0,n) in place by `shift` so new a[i] = old a[(i+shift)%n].
inline void rotateInPlace (float* a, int n, int shift) noexcept
{
    if (a == nullptr || n <= 0) return;
    shift = ((shift % n) + n) % n;
    if (shift == 0) return;
    std::vector<float> tmp ((size_t) n);
    for (int i = 0; i < n; ++i) tmp[(size_t) i] = a[(i + shift) % n];
    for (int i = 0; i < n; ++i) a[(size_t) i] = tmp[(size_t) i];
}
