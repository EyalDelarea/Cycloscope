#pragma once
#include <cmath>

// Per-pixel min/max decimation for the oscilloscope trace. Pure C++ (no JUCE) so it
// is unit-testable headlessly.
//
// Pixel column x spans source positions [start + x*spp, start + (x+1)*spp). outMin[x]
// and outMax[x] are the min/max of the integer samples whose index falls in that span.
// When a column contains no integer sample (spp < 1, i.e. zoomed in past 1 sample/pixel)
// it collapses to a single interpolated value (min == max -> a line, not a band), so the
// trace stays smooth when zoomed in and becomes a true envelope when zoomed out. Reads
// each in-range window sample once. Out-of-range indices are skipped (never zero-injected).
inline void decimateMinMax (const float* src, int numSamples,
                            double start, double spp, int width,
                            float* outMin, float* outMax) noexcept
{
    if (src == nullptr || outMin == nullptr || outMax == nullptr || numSamples <= 0)
        return;

    // Clamped linear interpolation, for columns too narrow to contain an integer sample.
    auto interp = [src, numSamples] (double pos) -> float
    {
        if (pos <= 0.0)               return src[0];
        if (pos >= numSamples - 1)    return src[numSamples - 1];
        const int   i0 = (int) pos;
        const float f  = (float) (pos - (double) i0);
        return src[i0] * (1.0f - f) + src[i0 + 1] * f;
    };

    for (int x = 0; x < width; ++x)
    {
        const double lo = start + (double) x * spp;
        const double hi = lo + spp;

        int i = (int) std::ceil (lo);   // first integer index >= lo
        float mn =  1.0e30f, mx = -1.0e30f;
        bool any = false;
        for (; (double) i < hi; ++i)
        {
            if (i < 0 || i >= numSamples) continue; // skip the zero-pad region
            const float v = src[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            any = true;
        }

        if (! any)                       // span too narrow / fully out of range -> line
            mn = mx = interp (lo + spp * 0.5);

        outMin[x] = mn;
        outMax[x] = mx;
    }
}
