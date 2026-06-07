#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

// McLeod Pitch Method via the Normalised Square Difference Function. Returns the
// fundamental period in samples (sub-sample via parabolic interp), or <= 0 if not
// clearly pitched. More accurate and octave-robust than plain autocorrelation.
inline float detectPeriodMPM (const float* buf, int numSamples, double sampleRate,
                              float minHz = 30.0f, float maxHz = 4000.0f,
                              float clarity = 0.8f, float* clarityOut = nullptr) noexcept
{
    if (clarityOut != nullptr) *clarityOut = 0.0f;
    if (buf == nullptr || numSamples < 64 || sampleRate <= 0.0) return -1.0f;
    int minLag = (int) (sampleRate / maxHz); if (minLag < 2) minLag = 2;
    int maxLag = (int) (sampleRate / minHz); if (maxLag >= numSamples) maxLag = numSamples - 1;
    if (maxLag <= minLag + 2) return -1.0f;

    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i) energy += (double) buf[i] * buf[i];
    if (energy <= 1e-7) return -1.0f;

    std::vector<double> nsdf ((size_t) (maxLag + 1), 0.0);
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        double acf = 0.0, m = 0.0; const int lim = numSamples - tau;
        for (int i = 0; i < lim; ++i)
        {
            acf += (double) buf[i] * buf[i + tau];
            m   += (double) buf[i] * buf[i] + (double) buf[i + tau] * buf[i + tau];
        }
        nsdf[(size_t) tau] = m > 0.0 ? 2.0 * acf / m : 0.0;
    }

    double globalMax = 0.0;
    for (int tau = minLag; tau <= maxLag; ++tau) globalMax = std::max (globalMax, nsdf[(size_t) tau]);
    if (clarityOut != nullptr) *clarityOut = (float) globalMax;
    if (globalMax < 0.5) return -1.0f; // not periodic enough (noise/silence)

    const double thresh = clarity * globalMax;
    int chosen = -1;
    for (int tau = minLag + 1; tau < maxLag; ++tau)
        if (nsdf[(size_t) tau] > nsdf[(size_t) (tau - 1)] && nsdf[(size_t) tau] >= nsdf[(size_t) (tau + 1)]
            && nsdf[(size_t) tau] >= thresh) { chosen = tau; break; }
    if (chosen < 0) return -1.0f;

    const double a = nsdf[(size_t) (chosen - 1)], b = nsdf[(size_t) chosen], c = nsdf[(size_t) (chosen + 1)];
    const double denom = (a - 2.0 * b + c);
    const double shift = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
    return (float) ((double) chosen + shift);
}

// Public entry point (stable signature). Delegates to the MPM detector.
inline float detectPeriod (const float* buf, int numSamples, double sampleRate,
                           float minHz = 30.0f, float maxHz = 4000.0f,
                           float /*confidence*/ = 0.5f) noexcept
{
    return detectPeriodMPM (buf, numSamples, sampleRate, minHz, maxHz);
}

// Cycle self-similarity: normalised autocorrelation at a lag of k whole periods (k chosen to
// fit the buffer, up to maxPeriods). ~1.0 for a TRULY periodic signal (single oscillator /
// wavetable -- its waveform still matches itself many cycles later); drops well below for a
// detuned ensemble (supersaw / unison) whose oscillators drift apart so cycles decorrelate
// over time. This is what distinguishes "one repeating shape" from "morphing" -- pitch
// clarity cannot (a supersaw is strongly pitched). Returns 1.0 when the buffer holds < 2
// periods (not enough evidence -> assume periodic). Pure C++, headlessly testable.
inline float cycleSelfSimilarity (const float* buf, int n, float period, int maxPeriods = 8) noexcept
{
    if (buf == nullptr || period <= 1.0f || n <= 0) return 1.0f;
    int k = (int) ((double) n / (double) period) - 1;
    if (k > maxPeriods) k = maxPeriods;
    if (k < 2) return 1.0f; // can't judge with < 2 periods of separation
    const int lag = (int) ((double) k * (double) period + 0.5);
    if (lag <= 0 || lag >= n) return 1.0f;
    double num = 0.0, e0 = 0.0, e1 = 0.0; const int lim = n - lag;
    for (int i = 0; i < lim; ++i)
    {
        const double a = (double) buf[i], b = (double) buf[i + lag];
        num += a * b; e0 += a * a; e1 += b * b;
    }
    const double d = std::sqrt (e0 * e1);
    return d > 1e-12 ? (float) (num / d) : 1.0f;
}

// Period + clarity (NSDF peak height, 0..1) in one pass.
struct PitchResult { float period; float clarity; };
inline PitchResult detectPitch (const float* buf, int numSamples, double sampleRate,
                                float minHz = 30.0f, float maxHz = 4000.0f) noexcept
{
    float clarity = 0.0f;
    const float period = detectPeriodMPM (buf, numSamples, sampleRate, minHz, maxHz, 0.8f, &clarity);
    return { period, clarity };
}
