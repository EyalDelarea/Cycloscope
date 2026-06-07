// Rasterization benchmark: times the actual JUCE software-path rendering of the scope
// trace, into a realistic-size Image, at low and high Time zoom. This is the cost the
// data-path benchmark (bench_buildlive) omits -- where the filled band + edge strokes
// actually get rasterized.
//
// Three render variants are compared in ONE process (so machine load cancels out -- only
// the relative numbers are meaningful; absolutes drift run-to-run):
//   OLD          - the original thin polyline: gradient fill + glow underlay + 1 crisp edge.
//   NEW band     - min/max envelope: gradient fill + filled band + 2 crisp edges, with the
//                  glow underlay drawn ONLY when the trace is line-like (the shipped path).
//   NEW ungated  - same, but always draws the glow underlay (pre-optimization), to show the
//                  saving from gating it out once a solid band already supplies the glow.
//
// Each number is the MIN per-frame time over several repeats after a warmup -- min is the
// cleanest microbenchmark signal (least contaminated by scheduler/thermal noise).
#include <juce_graphics/juce_graphics.h>
#include "ScopeReduce.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <algorithm>

using Clock = std::chrono::steady_clock;
static double ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); }

// Old        - classic single polyline: gradient + glow underlay + 1 crisp edge.
// NewPrev     - first envelope commit: band fill + 2 crisp edges@1.8, glow gated by thickness
//               (still fills the band + strokes a redundant 2nd edge when line-like).
// NewFinal    - adaptive (shipped): band regime = fill + thin@1.0 edges, no glow; line regime
//               = exactly Old (no band fill, no redundant 2nd edge). Thin edges make the
//               filled band cheaper than the old line (the stroke width is the cost lever).
enum class Variant { Old, NewPrev, NewFinal };

int main()
{
    const int W = 800, H = 420, FRAMES = 300, REPEATS = 7; // ~ real scope panel (editor 1080x520)
    std::vector<float> sig (1 << 17);
    for (size_t i = 0; i < sig.size(); ++i)
        sig[i] = 0.8f * (float) (0.7 * std::sin (2.0 * 3.14159 * 220.0 * i / 48000.0)
                               + 0.3 * std::sin (2.0 * 3.14159 * 1760.0 * i / 48000.0));

    const float midY = H * 0.5f, halfH = H * 0.5f;
    juce::Image img (juce::Image::ARGB, W, H, true);

    auto buildEnv = [&] (double spp, std::vector<float>& yHi, std::vector<float>& yLo)
    {
        decimateMinMax (sig.data(), (int) sig.size(), 0.0, spp, W, yLo.data(), yHi.data());
        for (int x = 0; x < W; ++x) { yHi[(size_t) x] = midY - yHi[(size_t) x] * halfH; yLo[(size_t) x] = midY - yLo[(size_t) x] * halfH; }
    };

    auto edge = [&] (const std::vector<float>& e) { juce::Path p; p.startNewSubPath (0.0f, e[0]); for (int x = 1; x < W; ++x) p.lineTo ((float) x, e[(size_t) x]); return p; };

    // Draws one frame in the requested variant. Mirrors ScopeComponent::paint exactly.
    auto drawFrame = [&] (const std::vector<float>& yHi, const std::vector<float>& yLo, Variant v)
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0e1114));
        const juce::Path top = edge (yHi);
        juce::Path fill = top; fill.lineTo ((float) (W - 1), midY); fill.lineTo (0.0f, midY); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x39ff8a2b), 0.0f, 0.0f, juce::Colour (0x00ff8a2b), 0.0f, midY, false));
        g.fillPath (fill);

        float maxBand = 0.0f; for (int x = 0; x < W; ++x) maxBand = std::max (maxBand, std::abs (yHi[(size_t) x] - yLo[(size_t) x]));
        const bool band = (maxBand >= 2.0f);

        if (v == Variant::Old || (v == Variant::NewFinal && ! band))
        {
            // classic line: glow underlay + 1 crisp edge
            g.setColour (juce::Colour (0x33ff8a2b)); g.strokePath (top, juce::PathStrokeType (3.0f));
            g.setColour (juce::Colour (0xffff8a2b)); g.strokePath (top, juce::PathStrokeType (1.8f));
            return;
        }

        juce::Path bandP = top; for (int x = W - 1; x >= 0; --x) bandP.lineTo ((float) x, yLo[(size_t) x]); bandP.closeSubPath();
        g.setColour (juce::Colour (0x59ff8a2b)); g.fillPath (bandP);

        if (v == Variant::NewFinal)
        {
            g.setColour (juce::Colour (0xffff8a2b));
            g.strokePath (top, juce::PathStrokeType (1.0f));
            g.strokePath (edge (yLo), juce::PathStrokeType (1.0f));
            return;
        }

        // NewPrev: glow gated by thickness, then 2 crisp edges@1.8 (+ a redundant 2nd edge when line-like)
        if (! band) { g.setColour (juce::Colour (0x33ff8a2b)); g.strokePath (top, juce::PathStrokeType (3.0f)); }
        g.setColour (juce::Colour (0xffff8a2b));
        g.strokePath (top, juce::PathStrokeType (1.8f));
        g.strokePath (edge (yLo), juce::PathStrokeType (1.8f));
    };

    std::printf ("render bench: panel %dx%d, %d frames x %d interleaved rounds (min per-frame ms)\n\n", W, H, FRAMES, REPEATS);

    // INTERLEAVE the variants: one timed sample of each per round, round-robin, tracking the
    // per-variant min. Measuring all rounds of one variant consecutively (then the next) is
    // biased when machine load drifts over the run -- the variant timed first sees a different
    // load epoch than the one timed last. Interleaving exposes every variant to the same epochs.
    const Variant variants[] = { Variant::Old, Variant::NewPrev, Variant::NewFinal };
    const double zooms[] = { 1.0, 16.0, 64.0 };
    for (double spp : zooms)
    {
        std::vector<float> yHi ((size_t) W), yLo ((size_t) W);
        buildEnv (spp, yHi, yLo);
        double best[3] = { 1e30, 1e30, 1e30 };
        for (int k = 0; k < 3; ++k) for (int f = 0; f < FRAMES; ++f) drawFrame (yHi, yLo, variants[k]); // warmup
        for (int r = 0; r < REPEATS; ++r)
            for (int k = 0; k < 3; ++k)
            {
                auto t0 = Clock::now();
                for (int f = 0; f < FRAMES; ++f) drawFrame (yHi, yLo, variants[k]);
                best[k] = std::min (best[k], ms (Clock::now() - t0) / FRAMES);
            }
        // acceptance: NewFinal must not regress vs Old. '+' = faster than Old, '-' = slower.
        std::printf ("zoom %4.0fx   OLD %6.3f   NEW-prev %6.3f   NEW-final %6.3f   | final vs Old %+6.3f ms   vs prev %.0f%% faster\n",
                     spp, best[0], best[1], best[2], best[0] - best[2], 100.0 * (best[1] - best[2]) / best[1]);
    }
    return 0;
}
