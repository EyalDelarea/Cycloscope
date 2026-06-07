#include "CycleAverager.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdio>

int main()
{
    const int P = 100;          // integer period
    const int outLen = 128;

    std::vector<float> ref ((size_t) outLen);
    for (int j = 0; j < outLen; ++j)
        ref[(size_t) j] = (float) (2.0 * ((double) j / outLen) - 1.0);

    const int cycles = 30;
    const int n = P * cycles;
    std::vector<float> buf ((size_t) n);
    unsigned s = 777u;
    for (int i = 0; i < n; ++i)
    {
        double frac = (double) (i % P) / P;
        s = s * 1103515245u + 12345u;
        float noise = (((s >> 16) & 0x7fff) / 16384.0f - 1.0f) * 0.25f;
        buf[(size_t) i] = (float) (2.0 * frac - 1.0) + noise;
    }

    auto avg = averageCycle (buf.data(), n, (float) P, outLen);
    assert ((int) avg.size() == outLen);

    double sse = 0.0;
    for (int j = 0; j < outLen; ++j) { double d = avg[(size_t) j] - ref[(size_t) j]; sse += d * d; }
    double rms = std::sqrt (sse / outLen);
    assert (rms < 0.1);

    // a noiseless continuous waveform (sine, no discontinuity) reproduces faithfully
    const double tau = 6.283185307179586;
    std::vector<float> clean ((size_t) (P * 2));
    for (int i = 0; i < P * 2; ++i) clean[(size_t) i] = (float) std::sin (tau * (i % P) / P);
    auto rep = averageCycle (clean.data(), P * 2, (float) P, outLen);
    double sse2 = 0.0;
    for (int j = 0; j < outLen; ++j)
    {
        double refSin = std::sin (tau * (double) j / outLen);
        double d = rep[(size_t) j] - refSin; sse2 += d * d;
    }
    assert (std::sqrt (sse2 / outLen) < 0.02);

    // startOffset by one full period on a periodic signal == no offset
    std::vector<float> sine ((size_t) (P * 6));
    for (int i = 0; i < P * 6; ++i) sine[(size_t) i] = (float) std::sin (tau * (i % P) / P);
    auto a0 = averageCycle (sine.data(), P * 6, (float) P, outLen, 0);
    auto aP = averageCycle (sine.data(), P * 6, (float) P, outLen, P);
    double sseo = 0.0;
    for (int j = 0; j < outLen; ++j) { double d = a0[(size_t) j] - aP[(size_t) j]; sseo += d * d; }
    assert (std::sqrt (sseo / outLen) < 0.02);

    // phase-lock wins when the period estimate is slightly wrong (drift -> smear)
    {
        const double tau3 = 6.283185307179586;
        const int truP = 100, cyc = 18, nn = truP * cyc;
        std::vector<float> sig ((size_t) nn);
        for (int i = 0; i < nn; ++i) sig[(size_t) i] = (float) std::sin (tau3 * i / truP);
        std::vector<float> refc ((size_t) outLen);
        for (int j = 0; j < outLen; ++j) refc[(size_t) j] = (float) std::sin (tau3 * (double) j / outLen);
        const float wrongP = 100.4f;
        auto unlocked = averageCycle (sig.data(), nn, wrongP, outLen, 0, false);
        auto locked   = averageCycle (sig.data(), nn, wrongP, outLen, 0, true);
        auto rmsv = [&] (const std::vector<float>& v) { double s = 0; for (int j = 0; j < outLen; ++j) { double d = v[(size_t) j] - refc[(size_t) j]; s += d * d; } return std::sqrt (s / outLen); };
        assert (rmsv (locked) < rmsv (unlocked));
        assert (rmsv (locked) < 0.06);
    }

    // recent-N: on a signal whose amplitude grows each cycle, maxCycles=2 favors
    // the latest (louder) cycles vs maxCycles=0 (all)
    {
        const double tau4 = 6.283185307179586;
        const int Pp = 100, cyc = 10, nn = Pp * cyc + 4;
        std::vector<float> grow ((size_t) nn);
        for (int i = 0; i < nn; ++i)
        {
            const double amp = 1.0 + 0.1 * (i / Pp);
            grow[(size_t) i] = (float) (amp * std::sin (tau4 * (i % Pp) / Pp));
        }
        auto all2  = averageCycle (grow.data(), nn, (float) Pp, outLen, 0, false, 0);
        auto last2 = averageCycle (grow.data(), nn, (float) Pp, outLen, 0, false, 2);
        auto peak  = [&] (const std::vector<float>& v) { float p = 0; for (float x : v) p = std::max (p, std::fabs (x)); return p; };
        assert (peak (last2) > peak (all2));
    }

    std::puts ("cycleaverager OK");
    return 0;
}
