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

    std::puts ("phasealign OK");
    return 0;
}
