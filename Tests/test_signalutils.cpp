#include "SignalUtils.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>

int main()
{
    std::vector<float> a = { 1.0f, 2.0f, 3.0f, 4.0f }; // mean 2.5
    removeMean (a.data(), (int) a.size());
    double m = 0.0; for (float v : a) m += v; m /= a.size();
    assert (std::fabs (m) < 1e-5);
    assert (std::fabs (a[0] - (-1.5f)) < 1e-5);

    std::vector<float> b = { 0.1f, -0.2f, 0.05f };
    peakNormalize (b.data(), (int) b.size(), 0.9f);
    float peak = 0.0f; for (float v : b) peak = std::max (peak, std::fabs (v));
    assert (std::fabs (peak - 0.9f) < 1e-4);

    std::vector<float> z = { 0.0f, 0.0f, 0.0f };
    peakNormalize (z.data(), (int) z.size(), 0.9f);
    assert (z[0] == 0.0f && z[1] == 0.0f);

    // findRisingZero: saw -1..1 period 100 crosses 0 upward at i=50
    std::vector<float> saw (300);
    for (int i = 0; i < 300; ++i) saw[(size_t) i] = (float) (2.0 * ((i % 100) / 100.0) - 1.0);
    assert (findRisingZero (saw.data(), 300, 200) == 50);

    // all-positive -> no rising zero -> 0
    std::vector<float> pos (50, 0.5f);
    assert (findRisingZero (pos.data(), 50, 50) == 0);

    // combineChannel: Mono / Left / Right / Side (exact-representable values)
    assert (combineChannel (0.5f, 0.25f, 0) == 0.375f); // mono = (L+R)/2
    assert (combineChannel (0.5f, 0.25f, 1) == 0.5f);   // left
    assert (combineChannel (0.5f, 0.25f, 2) == 0.25f);  // right
    assert (combineChannel (0.5f, 0.25f, 3) == 0.125f); // side = (L-R)/2

    std::puts ("signalutils OK");
    return 0;
}
