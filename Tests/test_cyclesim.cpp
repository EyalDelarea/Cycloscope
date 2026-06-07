#include "PeriodDetector.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdio>

// cycleSelfSimilarity distinguishes a single repeating oscillator (~1.0) from a detuned
// ensemble (supersaw/unison, well below) -- the basis for Base Shape choosing "average" vs
// "animate the latest cycle". Pitch clarity cannot make this distinction.
int main()
{
    const int N = 16384;
    const double TWO_PI = 6.283185307179586;

    // 1. Pure periodic sine (period 100) -> still matches itself 8 periods later -> ~1.0
    {
        std::vector<float> s ((size_t) N);
        for (int i = 0; i < N; ++i) s[(size_t) i] = (float) std::sin (TWO_PI * i / 100.0);
        const float r = cycleSelfSimilarity (s.data(), N, 100.0f);
        std::printf ("sine        selfSim=%.3f\n", r);
        assert (r > 0.95f);
    }

    // 2. A single saw (period 100) -> also truly repeating -> ~1.0
    {
        std::vector<float> s ((size_t) N);
        double p = 0.0;
        for (int i = 0; i < N; ++i) { s[(size_t) i] = (float) (2.0 * p - 1.0); p += 1.0 / 100.0; if (p >= 1.0) p -= 1.0; }
        const float r = cycleSelfSimilarity (s.data(), N, 100.0f);
        std::printf ("saw         selfSim=%.3f\n", r);
        assert (r > 0.95f);
    }

    // 3. Two detuned sines summed (periods 100 and 103) -> cycles drift apart -> well below 0.9
    {
        std::vector<float> s ((size_t) N);
        for (int i = 0; i < N; ++i)
            s[(size_t) i] = (float) (0.5 * std::sin (TWO_PI * i / 100.0) + 0.5 * std::sin (TWO_PI * i / 103.0));
        const float r = cycleSelfSimilarity (s.data(), N, 100.0f);
        std::printf ("detuned x2  selfSim=%.3f\n", r);
        assert (r < 0.85f);          // clearly "morphing"
    }

    // 4. Degenerate inputs are safe (assume periodic -> 1.0)
    {
        std::vector<float> s (8, 0.0f);
        assert (cycleSelfSimilarity (s.data(), 8, 100.0f) == 1.0f); // < 2 periods fit
        assert (cycleSelfSimilarity (nullptr, N, 100.0f) == 1.0f);
        assert (cycleSelfSimilarity (s.data(), 8, 0.0f)   == 1.0f); // bad period
    }

    std::puts ("cyclesim OK");
    return 0;
}
