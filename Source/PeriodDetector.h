#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <complex>

// In-place iterative radix-2 FFT (n must be a power of two). inverse=true computes the
// unscaled inverse transform. Pure C++, only used by the NSDF below.
inline void fftRadix2 (std::vector<std::complex<double>>& a, bool inverse) noexcept
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 6.283185307179586476925 / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Normalised Square Difference Function for lags [0, maxLag]:
//   nsdf(tau) = 2 * r(tau) / m(tau),  r = sum x[i] x[i+tau],  m = sum x[i]^2 + x[i+tau]^2
// over i in [0, n - tau). The autocorrelation r comes from one zero-padded FFT (no wrap:
// padded to >= 2n) and m from prefix sums of squares, so the whole function costs
// O(n log n) instead of the direct O(n * maxLag) double loop, which at 8192 samples ran to
// tens of millions of multiply-adds per frame on the GUI thread. Same values up to rounding.
inline std::vector<double> computeNsdf (const float* buf, int numSamples, int maxLag)
{
    std::vector<double> nsdf ((std::size_t) (maxLag + 1), 0.0);
    std::size_t m = 1;
    while (m < (std::size_t) numSamples * 2) m <<= 1;

    std::vector<std::complex<double>> spec (m);
    for (int i = 0; i < numSamples; ++i) spec[(std::size_t) i] = { (double) buf[i], 0.0 };
    fftRadix2 (spec, false);
    for (auto& c : spec) c = { std::norm (c), 0.0 };
    fftRadix2 (spec, true);

    std::vector<double> sq ((std::size_t) numSamples + 1, 0.0); // sq[k] = sum_{i<k} x[i]^2
    for (int i = 0; i < numSamples; ++i)
        sq[(std::size_t) i + 1] = sq[(std::size_t) i] + (double) buf[i] * (double) buf[i];

    const double invM = 1.0 / (double) m;
    for (int tau = 0; tau <= maxLag; ++tau)
    {
        const double r  = spec[(std::size_t) tau].real() * invM;
        const double mm = sq[(std::size_t) (numSamples - tau)] + (sq[(std::size_t) numSamples] - sq[(std::size_t) tau]);
        nsdf[(std::size_t) tau] = mm > 0.0 ? 2.0 * r / mm : 0.0;
    }
    return nsdf;
}

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

    const std::vector<double> nsdf = computeNsdf (buf, numSamples, maxLag);

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

// Period + clarity (NSDF peak height, 0..1) in one pass.
struct PitchResult { float period; float clarity; };
inline PitchResult detectPitch (const float* buf, int numSamples, double sampleRate,
                                float minHz = 30.0f, float maxHz = 4000.0f) noexcept
{
    float clarity = 0.0f;
    const float period = detectPeriodMPM (buf, numSamples, sampleRate, minHz, maxHz, 0.8f, &clarity);
    return { period, clarity };
}
