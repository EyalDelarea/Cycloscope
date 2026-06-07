#include "PeriodDetector.h"
#include <cassert>
#include <vector>
#include <cmath>
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

int main()
{
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
