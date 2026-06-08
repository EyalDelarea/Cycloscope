#include "GoniometerComponent.h"
#include "StereoUtils.h"

GoniometerComponent::GoniometerComponent (CycloscopeProcessor& p) : proc (p)
{
    startTimerHz (30); // persistence trails don't need 60 Hz; halves this panel's render load
}

GoniometerComponent::~GoniometerComponent() { stopTimer(); }

void GoniometerComponent::timerCallback()
{
    // When the panel is collapsed it is set invisible -> paint() never runs, so its full
    // per-frame cost (4k-sample path + persistence image) disappears entirely.
    if (! isVisible()) return;
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    if (! frozen) repaint();
}

void GoniometerComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff08090a));

    // header
    g.setColour (juce::Colour (0xff6a717a));
    g.setFont (juce::Font (juce::FontOptions (9.5f)).boldened());
    g.drawText ("STEREO", area.removeFromTop (16).reduced (8, 2), juce::Justification::left);

    // reserve bottom strip for meter + two-row readout
    auto bottom = area.removeFromBottom (48);
    const float pad = 10.0f;
    const float side = juce::jmin (area.getWidth() - pad * 2.0f, area.getHeight() - pad * 2.0f);
    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    const float radius = side * 0.5f;
    const float d = radius * 0.70710678f;

    // axes
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (cx, cy - radius, cx, cy + radius);
    g.drawLine (cx - radius, cy, cx + radius, cy);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (cx - d, cy - d, cx + d, cy + d);
    g.drawLine (cx + d, cy - d, cx - d, cy + d);
    g.setColour (juce::Colour (0xff4a5059));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("M", (int) (cx - 6), (int) (cy - radius - 12), 12, 11, juce::Justification::centred);
    g.drawText ("L", (int) (cx - d - 12), (int) (cy - d - 12), 12, 11, juce::Justification::centred);
    g.drawText ("R", (int) (cx + d), (int) (cy - d - 12), 12, 11, juce::Justification::centred);

    // snapshot L/R
    const int N = 4096;
    capL.assign ((size_t) N, 0.0f);
    capR.assign ((size_t) N, 0.0f);
    proc.getScopeBufferL().copyLatest (capL.data(), N);
    proc.getScopeBufferR().copyLatest (capR.data(), N);

    // Scale by the PEAK radial extent of the Lissajous (mid/side), not by RMS. RMS-scaling
    // normalised by overall loudness, which let the centred (mid) axis dominate the gain and
    // visually crushed a delay/widener's real side spread into the vertical line. Peak-scaling
    // makes the trace fill the scope consistently and shows true width. RMS is still computed
    // for the silence gate and correlation gating.
    double e = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float mid = (capL[(size_t) i] + capR[(size_t) i]) * 0.5f;
        const float sd  = (capR[(size_t) i] - capL[(size_t) i]) * 0.5f;
        e += (double) mid * mid + (double) sd * sd;
        peak = juce::jmax (peak, std::abs (mid), std::abs (sd));
    }
    const float rms  = (float) std::sqrt (e / (double) N);
    const float GATE = 0.0015f; // ~-56 dBFS: below this = silence, draw nothing

    // Phosphor persistence: fade the prior frames and draw the new trace on top, so an
    // evolving stereo field (delay/reverb/widening) builds a readable envelope rather
    // than a single flickering frame. Decay 0 = no persistence (cleared each frame).
    const float decay = juce::jlimit (0.0f, 0.995f, proc.apvts.getRawParameterValue ("stereoDecay")->load());
    if (persistImage.getWidth() != getWidth() || persistImage.getHeight() != getHeight())
        persistImage = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    persistImage.multiplyAllAlphas (decay * decay); // running at 30 (not 60) Hz: square to keep the same wall-clock trail length

    if (rms >= GATE)
    {
        float target = (peak > 1.0e-6f) ? (radius * 0.92f / peak) // peaks fill ~92% of the radius
                                        : radius;
        target = juce::jmin (target, radius * 40.0f); // cap amplification for very quiet signals
        gonioGain += (target - gonioGain) * 0.18f;    // smooth across frames -> no size jitter
        const float s = gonioGain;

        // Decimate the drawn trace: ~1024 points is plenty on this small panel and cuts
        // the (software-rendered) path cost ~4x. Meter/correlation math above still uses
        // all N samples, so accuracy is unchanged.
        const int step = juce::jmax (1, N / 1024);
        juce::Path p;
        p.preallocateSpace ((N / step) * 3);
        bool first = true;
        for (int i = 0; i < N; i += step)
        {
            const float L = capL[(size_t) i], R = capR[(size_t) i];
            const float x = cx + ((R - L) * 0.5f) * s;
            const float y = cy - ((L + R) * 0.5f) * s;
            if (first) { p.startNewSubPath (x, y); first = false; }
            else       p.lineTo (x, y);
        }
        juce::Graphics ig (persistImage);
        ig.reduceClipRegion (juce::Rectangle<int> ((int) (cx - radius), (int) (cy - radius),
                                                   (int) (radius * 2.0f), (int) (radius * 2.0f)));
        ig.setColour (juce::Colour (0x40ff8a2b));           // soft glow underlay (a touch brighter)
        ig.strokePath (p, juce::PathStrokeType (3.0f));
        ig.setColour (juce::Colour (0xffff8a2b));           // bright opaque trace (persists clearly)
        ig.strokePath (p, juce::PathStrokeType (1.3f));
    }
    g.drawImageAt (persistImage, 0, 0);

    // correlation meter (-1 .. +1) + L/R readout. Gate the correlation on the same
    // silence floor as the trace, so noise-floor dither doesn't show a phantom corr.
    const float corr = (rms >= GATE) ? stereoCorrelation (capL.data(), capR.data(), N) : 0.0f;

    // Meter ballistics: instant attack, slow release. RMS over an 85 ms window collapses
    // into a ping-pong delay's gaps and reads wildly low/jumpy; peak-holding the louder
    // value makes the L/R readout track what you actually hear and sit still.
    const float lDb = rmsDb (capL.data(), N);
    const float rDb = rmsDb (capR.data(), N);
    lDbShown = lDb > lDbShown ? lDb : lDbShown + (lDb - lDbShown) * 0.08f;
    rDbShown = rDb > rDbShown ? rDb : rDbShown + (rDb - rDbShown) * 0.08f;

    auto meter = bottom.removeFromTop (10).reduced (pad, 0);
    g.setColour (juce::Colour (0xff14171a));
    g.fillRoundedRectangle (meter, 4.0f);
    const float midX = meter.getCentreX();
    const float mk = midX + corr * (meter.getWidth() * 0.5f - 2.0f);
    g.setColour (corr >= 0.0f ? juce::Colour (0xff2bd47a) : juce::Colour (0xffd4502b));
    if (mk >= midX) g.fillRect (juce::Rectangle<float> (midX, meter.getY(), mk - midX, meter.getHeight()));
    else            g.fillRect (juce::Rectangle<float> (mk, meter.getY(), midX - mk, meter.getHeight()));
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawVerticalLine ((int) midX, meter.getY(), meter.getBottom());

    // Two compact rows so the readout never truncates on a narrow panel. Integer dB
    // (no false 4-decimal precision); correlation carries an explicit sign.
    g.setFont (juce::Font (juce::FontOptions().withName (juce::Font::getDefaultMonospacedFontName()).withHeight (11.0f)));
    auto dbStr = [] (float v) { return v <= -99.5f ? juce::String ("-inf")
                                                   : juce::String (juce::roundToInt (v)); };
    const juce::String corrStr = (corr >= 0.0f ? "+" : "") + juce::String (corr, 2);

    auto rowC  = bottom.removeFromTop (14);
    g.setColour (juce::Colour (0xff9aa0a8));
    g.drawText ("corr " + corrStr, rowC.reduced (pad, 0), juce::Justification::centred);

    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("L " + dbStr (lDbShown) + "   R " + dbStr (rDbShown) + " dB",
                bottom.reduced (pad, 0), juce::Justification::centred);
}
