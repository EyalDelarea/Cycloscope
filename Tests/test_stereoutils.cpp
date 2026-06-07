#include "StereoUtils.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <limits>
#include <cstdio>

int main()
{
    const int n = 2048;
    const double tau = 6.283185307179586;
    std::vector<float> a (n), b (n), c (n), z (n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        a[(size_t) i] = (float) std::sin (tau * 5.0 * i / n);   // L
        b[(size_t) i] = (float) std::sin (tau * 9.0 * i / n);   // different freq -> uncorrelated
        c[(size_t) i] = -a[(size_t) i];                          // inverted
    }

    assert (std::fabs (stereoCorrelation (a.data(), a.data(), n) - 1.0f) < 1e-3f);  // identical -> +1
    assert (std::fabs (stereoCorrelation (a.data(), c.data(), n) + 1.0f) < 1e-3f);  // inverted -> -1
    assert (std::fabs (stereoCorrelation (a.data(), b.data(), n)) < 0.2f);          // uncorrelated -> ~0
    assert (stereoCorrelation (z.data(), z.data(), n) == 0.0f);                     // silence -> 0

    // full-scale sine RMS ~ -3.01 dBFS
    assert (std::fabs (rmsDb (a.data(), n) - (-3.01f)) < 0.1f);
    assert (rmsDb (z.data(), n) <= -99.0f);

    // NaN/Inf scrubbing: a bad sample must not leak NaN to the readout
    std::vector<float> bad (n, 0.5f);
    bad[(size_t) 10] = std::nanf ("");
    bad[(size_t) 11] = std::numeric_limits<float>::infinity();
    assert (std::isfinite (rmsDb (bad.data(), n)));
    assert (std::isfinite (stereoCorrelation (bad.data(), bad.data(), n)));

    std::puts ("stereoutils OK");
    return 0;
}
