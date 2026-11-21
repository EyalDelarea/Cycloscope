#include "PhaseAlign.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdio>

int main()
{
    const int n = 128;
    const double tau = 6.283185307179586;
    std::vector<float> ref (n), a (n);
    for (int i = 0; i < n; ++i) ref[(size_t) i] = (float) std::sin (tau * i / n);

    // a = ref rotated forward by k; bestCircularShift should recover an alignment
    const int k = 30;
    for (int i = 0; i < n; ++i) a[(size_t) i] = ref[(size_t) ((i + k) % n)];

    const int s = bestCircularShift (a.data(), ref.data(), n);
    // rotating a by s should reproduce ref closely
    std::vector<float> aligned (n);
    for (int i = 0; i < n; ++i) aligned[(size_t) i] = a[(size_t) i];
    rotateInPlace (aligned.data(), n, s);
    double sse = 0.0;
    for (int i = 0; i < n; ++i) { double d = aligned[(size_t) i] - ref[(size_t) i]; sse += d * d; }
    assert (std::sqrt (sse / n) < 1e-3);

    // identical inputs -> shift 0
    assert (bestCircularShift (ref.data(), ref.data(), n) == 0);

    // the split (modulo-free) loop must pick exactly the shift the plain modulo sum picks
    unsigned seed = 7;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f; };
    for (int len : { 1, 2, 3, 17, 128, 1000, 1080 })
        for (int trial = 0; trial < 4; ++trial)
        {
            std::vector<float> x ((size_t) len), y ((size_t) len);
            for (int i = 0; i < len; ++i) { x[(size_t) i] = rnd(); y[(size_t) i] = rnd(); }
            int best = 0; double bestCorr = -1e300;
            for (int sh = 0; sh < len; ++sh)
            {
                double c = 0.0;
                for (int i = 0; i < len; ++i) c += (double) x[(size_t) ((i + sh) % len)] * (double) y[(size_t) i];
                if (c > bestCorr) { bestCorr = c; best = sh; }
            }
            assert (bestCircularShift (x.data(), y.data(), len) == best);
        }

    std::puts ("phasealign OK");
    return 0;
}
