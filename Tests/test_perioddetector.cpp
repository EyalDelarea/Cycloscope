#include "PeriodDetector.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>

static std::vector<float> makeSine (double freq, double sr, int n)
{
    std::vector<float> b ((size_t) n);
    const double tau = 6.283185307179586;
    for (int i = 0; i < n; ++i) b[(size_t) i] = (float) std::sin (tau * freq * i / sr);
    return b;
}

static std::vector<float> makeSaw (double freq, double sr, int n)
{
    std::vector<float> b ((size_t) n);
    const double period = sr / freq;
    for (int i = 0; i < n; ++i)
    {
        double frac = std::fmod ((double) i, period) / period;
        b[(size_t) i] = (float) (2.0 * frac - 1.0);
    }
    return b;
}

static bool within (float a, float b, float tolPct)
{
    return std::fabs (a - b) <= tolPct * std::fabs (b); // fabs(b): correct for negative refs too
}


// The original direct O(n * maxLag) MPM, kept verbatim as the reference the FFT-based
// detector must reproduce (same pitched/unpitched decision, same period and clarity).
static float referenceMPM (const float* buf, int numSamples, double sampleRate,
                           float minHz, float maxHz, float clarity, float* clarityOut)
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
    if (globalMax < 0.5) return -1.0f;
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

static void checkMatchesReference()
{
    unsigned seed = 12345;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f; };
    const int n = 8192;
    int cases = 0, pitched = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    for (int kind = 0; kind < 8; ++kind)
    for (double hz : { 31.0, 55.0, 110.0, 261.6, 440.0, 1000.0, 3500.0 })
    {
        std::vector<float> b ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double ph  = std::fmod (i * hz / sr, 1.0);
            const double ph2 = std::fmod (i * hz * 1.007 / sr, 1.0);
            const double tw  = 6.283185307179586;
            float v = 0.0f;
            switch (kind)
            {
                case 0: v = (float) std::sin (tw * ph); break;                                 // sine
                case 1: v = (float) (2.0 * ph - 1.0); break;                                   // saw
                case 2: v = ph < 0.5 ? 0.8f : -0.8f; break;                                    // square
                case 3: v = (float) (0.5 * (2.0 * ph - 1.0) + 0.5 * (2.0 * ph2 - 1.0)); break; // detuned pair
                case 4: v = 0.3f * rnd(); break;                                               // noise
                case 5: v = 0.0f; break;                                                       // silence
                case 6: v = (float) std::sin (tw * ph) * 0.6f + 0.45f * rnd(); break;          // sine in noise (near thresholds)
                default: v = (float) (std::sin (tw * ph) + 0.9 * std::sin (tw * 2.0 * ph + 0.3)); break; // strong 2nd harmonic
            }
            b[(size_t) i] = v;
        }
        for (float clar : { 0.5f, 0.7f, 0.8f })
        {
            float cRef = 0.0f, cNew = 0.0f;
            const float pRef = referenceMPM (b.data(), n, sr, 30.0f, 4000.0f, clar, &cRef);
            const float pNew = detectPeriodMPM (b.data(), n, sr, 30.0f, 4000.0f, clar, &cNew);
            ++cases;
            assert ((pRef > 0.0f) == (pNew > 0.0f));
            if (pRef > 0.0f) { ++pitched; assert (std::fabs (pRef - pNew) < 1.0e-3f); }
            assert (std::fabs (cRef - cNew) < 1.0e-5f);
        }
    }
    std::printf ("perioddetector: FFT NSDF matches reference on %d cases (%d pitched)\n", cases, pitched);
}

int main()
{
    checkMatchesReference();

    const double sr = 44100.0;
    const int n = 8192;

    {
        auto b = makeSine (441.0, sr, n);
        float p = detectPeriod (b.data(), n, sr);
        assert (within (p, 100.0f, 0.02f));
    }
    {
        auto b = makeSaw (220.0, sr, n);
        float p = detectPeriod (b.data(), n, sr);
        assert (within (p, (float) (sr / 220.0), 0.02f));
    }
    {
        auto b = makeSine (110.0, sr, n);
        float p = detectPeriod (b.data(), n, sr);
        assert (within (p, (float) (sr / 110.0), 0.02f));
    }
    {
        std::vector<float> b ((size_t) n);
        unsigned s = 12345u;
        for (int i = 0; i < n; ++i) { s = s * 1103515245u + 12345u; b[(size_t) i] = ((s >> 16) & 0x7fff) / 16384.0f - 1.0f; }
        float p = detectPeriod (b.data(), n, sr);
        assert (p <= 0.0f);
    }
    {
        std::vector<float> b ((size_t) n, 0.0f);
        float p = detectPeriod (b.data(), n, sr);
        assert (p <= 0.0f);
    }

    // octave trap: fundamental 220 Hz + strong 2nd harmonic -> detect fundamental
    {
        const double tau = 6.283185307179586;
        std::vector<float> b ((size_t) n);
        for (int i = 0; i < n; ++i)
            b[(size_t) i] = (float) (std::sin (tau * 220.0 * i / sr) + 0.8 * std::sin (tau * 440.0 * i / sr));
        float p = detectPeriod (b.data(), n, sr);
        assert (within (p, (float) (sr / 220.0), 0.02f));
    }

    // detectPitch returns clarity: clean tone high, noise low
    {
        auto b = makeSine (441.0, sr, n);
        PitchResult pr = detectPitch (b.data(), n, sr);
        assert (pr.clarity >= 0.9f && within (pr.period, 100.0f, 0.02f));

        std::vector<float> nz ((size_t) n);
        unsigned s = 999u;
        for (int i = 0; i < n; ++i) { s = s * 1103515245u + 12345u; nz[(size_t) i] = ((s >> 16) & 0x7fff) / 16384.0f - 1.0f; }
        PitchResult pn = detectPitch (nz.data(), n, sr);
        assert (pn.period <= 0.0f && pn.clarity < 0.5f);
    }

    // high pitch must NOT be reported as a confident sub-octave (3 kHz -> 1500 Hz @ clarity 1.0)
    {
        auto b = makeSine (3000.0, sr, n);
        PitchResult pr = detectPitch (b.data(), n, sr);
        const bool confidentlyHalf = (pr.period > 0.0f)
            && within (pr.period, (float) (sr / 1500.0), 0.05f) && pr.clarity >= 0.9f;
        assert (! confidentlyHalf); // never confidently wrong by an octave
        const bool correct = (pr.period > 0.0f) && within (pr.period, (float) (sr / 3000.0), 0.05f);
        const bool honest  = (pr.period <= 0.0f) || pr.clarity < 0.7f;
        assert (correct || honest);
    }

    std::puts ("perioddetector OK");
    return 0;
}
