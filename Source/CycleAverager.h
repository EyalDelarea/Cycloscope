#pragma once
#include <vector>
#include <cstddef>

// Catmull-Rom cubic interpolation at fractional position pos in buf[0,n).
inline float cubicAt (const float* buf, int n, double pos) noexcept
{
    const int i1 = (int) pos; const double f = pos - (double) i1;
    auto at = [&] (int i) { return (double) buf[i < 0 ? 0 : (i >= n ? n - 1 : i)]; };
    const double y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
    return (float) (0.5 * ((2.0 * y1)
                 + (-y0 + y2) * f
                 + (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3) * f * f
                 + (-y0 + 3.0 * y1 - 3.0 * y2 + y3) * f * f * f));
}

// Nearest rising zero-crossing (buf[s-1] < 0 <= buf[s]) to `around` within +/- radius,
// or -1 if none. Used to phase-lock each averaged cycle.
inline int nearestRisingZero (const float* buf, int n, int around, int radius) noexcept
{
    for (int d = 0; d <= radius; ++d)
        for (int s : { around + d, around - d })
            if (s >= 1 && s < n && buf[s - 1] < 0.0f && buf[s] >= 0.0f) return s;
    return -1;
}

// Average every whole period of length `period` that fits in buf[0..numSamples)
// into a single cycle of `outLen` points. With phaseLock, each cycle re-anchors to
// the nearest rising zero-crossing (coherent even when the period estimate is
// slightly off) and resamples with cubic interpolation; otherwise it uses fixed
// spacing + linear interpolation. Returns zeros if fewer than one period fits.
// Pure C++ — no JUCE dependency.
inline std::vector<float> averageCycle (const float* buf, int numSamples,
                                        float period, int outLen, int startOffset = 0,
                                        bool phaseLock = false, int maxCycles = 0)
{
    std::vector<float> out ((size_t) (outLen > 0 ? outLen : 0), 0.0f);
    if (buf == nullptr || period <= 1.0f || outLen <= 0 || numSamples <= 0)
        return out;

    int numCycles = (int) (((double) numSamples - (double) startOffset) / (double) period);
    if (numCycles < 1) numCycles = 1;
    const int radius = (int) (period / 6.0) > 1 ? (int) (period / 6.0) : 1;
    // recent-N: average only the most recent maxCycles whole periods
    int firstCycle = 0;
    if (maxCycles > 0 && numCycles > maxCycles) firstCycle = numCycles - maxCycles;

    int counted = 0;
    for (int c = firstCycle; c < numCycles; ++c)
    {
        double start = (double) startOffset + (double) c * (double) period;
        if (phaseLock)
        {
            const int z = nearestRisingZero (buf, numSamples, (int) (start + 0.5), radius);
            if (z >= 0) start = (double) z;
        }
        bool ok = true;
        std::vector<float> tmp ((size_t) outLen);
        for (int j = 0; j < outLen; ++j)
        {
            const double pos = start + ((double) j / outLen) * (double) period;
            const int i0 = (int) pos;
            if (i0 < 0 || i0 + 1 >= numSamples) { ok = false; break; }
            const double frac = pos - (double) i0;
            tmp[(size_t) j] = phaseLock
                ? cubicAt (buf, numSamples, pos)
                : (float) ((double) buf[i0] * (1.0 - frac) + (double) buf[i0 + 1] * frac);
        }
        if (! ok) break;
        for (int j = 0; j < outLen; ++j) out[(size_t) j] += tmp[(size_t) j];
        ++counted;
    }

    if (counted > 0)
        for (auto& v : out) v /= (float) counted;
    return out;
}
