// Offline verification harness: renders the oscilloscope trace exactly the way
// ScopeComponent::paint draws it (gradient fill + min/max band + dual-edge stroke),
// for a known synthetic signal at several Time-zoom settings, and writes PNGs. Lets us
// eyeball the envelope rendering without needing a live audio signal in the standalone.
#include <juce_graphics/juce_graphics.h>
#include "ScopeReduce.h"
#include <vector>
#include <cmath>

static void renderTrace (juce::Graphics& g, const std::vector<float>& sig,
                         double spp, int width, float midY, float halfH, float ampZoom)
{
    std::vector<float> yHi ((size_t) width), yLo ((size_t) width);
    decimateMinMax (sig.data(), (int) sig.size(), 0.0, spp, width, yLo.data(), yHi.data());
    for (int x = 0; x < width; ++x)
    {
        yHi[(size_t) x] = midY - yHi[(size_t) x] * ampZoom * halfH;
        yLo[(size_t) x] = midY - yLo[(size_t) x] * ampZoom * halfH;
    }

    auto edge = [width] (const std::vector<float>& e)
    {
        juce::Path p; p.startNewSubPath (0.0f, e[0]);
        for (int x = 1; x < width; ++x) p.lineTo ((float) x, e[(size_t) x]);
        return p;
    };

    const juce::Path top = edge (yHi);
    juce::Path fill = top;
    fill.lineTo ((float) (width - 1), midY); fill.lineTo (0.0f, midY); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x39ff8a2b), 0.0f, 0.0f,
                                             juce::Colour (0x00ff8a2b), 0.0f, midY, false));
    g.fillPath (fill);

    juce::Path band = top;
    for (int x = width - 1; x >= 0; --x) band.lineTo ((float) x, yLo[(size_t) x]);
    band.closeSubPath();
    g.setColour (juce::Colour (0x59ff8a2b));
    g.fillPath (band);

    const juce::Path bot = edge (yLo);
    g.setColour (juce::Colour (0x33ff8a2b)); g.strokePath (top, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colour (0xffff8a2b));
    g.strokePath (top, juce::PathStrokeType (1.8f));
    g.strokePath (bot, juce::PathStrokeType (1.8f));
}

int main()
{
    const int W = 520, H = 200, SR = 48000;
    // Two-tone signal (fundamental + a higher partial) -> a real min/max envelope at high zoom.
    std::vector<float> sig (1 << 17);
    for (size_t i = 0; i < sig.size(); ++i)
    {
        const double t = (double) i / SR;
        sig[i] = 0.8f * (float) (0.7 * std::sin (2.0 * 3.14159265 * 220.0 * t)
                               + 0.3 * std::sin (2.0 * 3.14159265 * 1760.0 * t));
    }

    const double zooms[] = { 1.0, 16.0, 64.0 };
    const char* names[] = { "/tmp/scope_zoom01.png", "/tmp/scope_zoom16.png", "/tmp/scope_zoom64.png" };
    for (int k = 0; k < 3; ++k)
    {
        juce::Image img (juce::Image::ARGB, W, H, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0e1114));
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawHorizontalLine (H / 2, 0.0f, (float) W);
        renderTrace (g, sig, zooms[k], W, (float) H * 0.5f, (float) H * 0.5f, 1.0f);

        juce::File out (names[k]);
        juce::FileOutputStream os (out);
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat fmt; fmt.writeImageToStream (img, os); }
    }
    return 0;
}
