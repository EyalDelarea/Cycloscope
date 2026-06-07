#include "ScopeComponent.h"
#include "Trigger.h"
#include "PeriodDetector.h"
#include "CycleAverager.h"
#include "SignalUtils.h"
#include "PhaseAlign.h"
#include "NoteName.h"
#include "StereoUtils.h"
#include <juce_audio_formats/juce_audio_formats.h>

ScopeComponent::ScopeComponent (CycloscopeProcessor& p) : proc (p)
{
    // Reserve the capture buffers once up front so paint() never allocates on the
    // message thread. 2x the ring capacity covers the widest Time-zoom window (the
    // trigger search needs a 2x-window capture); anything beyond is zero-padded.
    const size_t cap = 2 * 192000;
    capture.reserve (cap);
    capL.reserve (cap);
    capR.reserve (cap);
    fftData.reserve ((size_t) (2 << kFftOrder)); // 2*fftSize, reserved so paint() never reallocs
    startTimerHz (60); // first-class feel
}

void ScopeComponent::ensureGrid (int w, int h, bool baseShape)
{
    if (gridCache.getWidth() == w && gridCache.getHeight() == h && gridBaseCached == baseShape)
        return;
    gridCache = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics ig (gridCache);
    drawGrid (ig, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), baseShape);
    gridBaseCached = baseShape;
}

ScopeComponent::~ScopeComponent() { stopTimer(); }

void ScopeComponent::timerCallback()
{
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    if (! frozen)
        repaint();
}

void ScopeComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.5f;
    const int width = juce::jmax (1, (int) bounds.getWidth());

    const float ampZoom = proc.apvts.getRawParameterValue ("ampZoom")->load();
    const int   modeIdx = (int) proc.apvts.getRawParameterValue ("displayMode")->load();

    if (modeIdx == 2) { paintSpectrum (g, bounds); return; }

    // background + labeled measurement grid (cached static layer, blitted each frame)
    g.fillAll (juce::Colour (0xff0e1114));
    ensureGrid (width, (int) bounds.getHeight(), modeIdx == 1);
    g.drawImageAt (gridCache, 0, 0);

    if (modeIdx == 1) // Base Shape: draw A/B captured-cycle ghosts behind the live trace
    {
        drawGhost (g, capA, midY, halfH, width, juce::Colour (0x663ba0ff)); // A = blue
        drawGhost (g, capB, midY, halfH, width, juce::Colour (0x6655e08a)); // B = green
    }

    bool haveTrace = false;
    stereoTrace = false; // buildLive sets it true for the Stereo source

    if (modeIdx == 1) // Base Shape
        haveTrace = buildBaseShape (width, midY, halfH);

    if (! haveTrace) // Live, or Base Shape fallback
        buildLive (width, midY, halfH, ampZoom);

    // low-clarity sources read as "uncertain": soften the trace (calm, not alarming)
    const bool soft = (modeIdx == 1 && haveTrace && unstable);

    // The trace is a min/max envelope (yHi = upper edge, yLo = lower edge). When zoomed
    // in it collapses to a single line (yHi == yLo); when zoomed out it fills into a
    // solid waveform band. Helper builds the upper-edge path shared by fill + stroke.
    auto edgePath = [width] (const std::vector<float>& e)
    {
        juce::Path p;
        p.startNewSubPath (0.0f, e[0]);
        for (int x = 1; x < width; ++x) p.lineTo ((float) x, e[(size_t) x]);
        return p;
    };

    const juce::Path top = edgePath (yHi);

    // gradient fill under the upper edge (the signature area-under-trace look)
    juce::Path fill = top;
    fill.lineTo ((float) (width - 1), midY);
    fill.lineTo (0.0f, midY);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (soft ? 0x14ff8a2b : 0x39ff8a2b), 0.0f, bounds.getY(),
        juce::Colour (0x00ff8a2b), 0.0f, midY, false));
    g.fillPath (fill);

    // Two render regimes, chosen by the envelope thickness (max |yHi-yLo| over the width):
    // a "band" when zoomed out (yHi != yLo), or a single "line" when zoomed in / Base Shape
    // (yHi == yLo). Each regime draws ONLY what it needs -- the cost here is dominated by
    // strokePath over the per-pixel zig-zag edges, so we minimise stroke work per regime.
    float maxBand = 0.0f;
    for (int x = 0; x < width; ++x) maxBand = juce::jmax (maxBand, std::abs (yHi[(size_t) x] - yLo[(size_t) x]));

    if (maxBand >= 2.0f)
    {
        // Zoomed out: a solid min/max silhouette. The smooth band FILL is what reads as a
        // real scope (it interpolates between adjacent column tops -- vertical lines would
        // comb/alias). Edges stay thin and the wide glow underlay is dropped: over a filled
        // band the halo is invisible, and it is the single costliest stroke.
        juce::Path band = top;
        for (int x = width - 1; x >= 0; --x) band.lineTo ((float) x, yLo[(size_t) x]);
        band.closeSubPath();
        g.setColour (juce::Colour (soft ? 0x22ff8a2b : 0x59ff8a2b));
        g.fillPath (band);

        // Edges at 1.0px (not the line regime's 1.8): over a jagged per-pixel envelope the
        // software rasterizer's stroke cost scales worse-than-linearly with width, so thin
        // edges are what make the filled band actually CHEAPER than the old single line
        // (measured: -15% at 64x zoom, -30% at 16x), while staying crisp.
        g.setColour (juce::Colour (soft ? 0x66ff8a2b : 0xffff8a2b));
        g.strokePath (top,             juce::PathStrokeType (1.0f));
        g.strokePath (edgePath (yLo),  juce::PathStrokeType (1.0f));
    }
    else
    {
        // Line-like: the classic single crisp trace + soft glow underlay. yLo == yHi here,
        // so there is no band to fill and no separate lower edge -- drawing them would be
        // pure redundant work (this was a real regression on the message thread at low zoom).
        g.setColour (juce::Colour (soft ? 0x18ff8a2b : 0x33ff8a2b));
        g.strokePath (top, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colour (soft ? 0x66ff8a2b : 0xffff8a2b));
        g.strokePath (top, juce::PathStrokeType (1.8f));
    }

    // Stereo source: overlay the right channel (blue) envelope on top of left (orange)
    if (stereoTrace && (int) yHiR.size() >= width && (int) yLoR.size() >= width)
    {
        const juce::Path topR = edgePath (yHiR);
        juce::Path bandR = topR;
        for (int x = width - 1; x >= 0; --x) bandR.lineTo ((float) x, yLoR[(size_t) x]);
        bandR.closeSubPath();
        g.setColour (juce::Colour (0x305fb0ff));
        g.fillPath (bandR);
        g.setColour (juce::Colour (0xcc5fb0ff));
        g.strokePath (topR, juce::PathStrokeType (1.4f));
        g.strokePath (edgePath (yLoR), juce::PathStrokeType (1.4f));
    }

    // pitch / clarity readout (Base Shape), calm muted text in the existing label style
    if (modeIdx == 1 && haveTrace)
    {
        g.setColour (juce::Colour (0xff6a717a));
        g.setFont (juce::Font (juce::FontOptions().withName (juce::Font::getDefaultMonospacedFontName()).withHeight (10.0f)));
        const juce::String txt = juce::String (lastFreq, 1) + " Hz   " + juce::String (noteNameForHz (lastFreq))
                               + "   " + juce::String ((int) (lastClarity * 100.0f)) + "%"
                               + (cyclesMorph ? "   \xc2\xb7 unison" : ""); // morphing detune/unison -> live cycle

        g.drawText (txt, bounds.toNearestInt().removeFromTop (18).reduced (10, 3), juce::Justification::right);
        if (soft)
        {
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("no single cycle - try Live", bounds.toNearestInt(), juce::Justification::centred);
        }
    }

    if (modeIdx == 1 && ! haveTrace)
    {
        g.setColour (juce::Colour (0xff71767f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("no stable pitch", bounds.toNearestInt(), juce::Justification::centred);
    }

    // Live measurement readout (Vpp + RMS), muted top-right
    if (modeIdx == 0)
    {
        g.setColour (juce::Colour (0xff6a717a));
        g.setFont (juce::Font (juce::FontOptions().withName (juce::Font::getDefaultMonospacedFontName()).withHeight (10.0f)));
        const juce::String txt = "Vpp " + juce::String (liveVpp, 2)
                               + "   RMS " + juce::String (liveRms, 1) + " dB";
        g.drawText (txt, bounds.toNearestInt().removeFromTop (18).reduced (10, 3), juce::Justification::right);
    }
}

void ScopeComponent::buildLive (int width, float midY, float halfH, float ampZoom)
{
    float samplesPerPixel = proc.apvts.getRawParameterValue ("timeZoom")->load();
    const float threshold = proc.apvts.getRawParameterValue ("threshold")->load();
    const auto mode = (TriggerMode) (int) proc.apvts.getRawParameterValue ("triggerMode")->load();
    const int src = (int) proc.apvts.getRawParameterValue ("channelSource")->load();

    // Tempo sync: lock the window to a musical division of the host tempo
    const int syncDiv = (int) proc.apvts.getRawParameterValue ("syncDiv")->load();
    if (syncDiv > 0)
    {
        static const double beats[] = { 0.0, 1.0, 2.0, 4.0 }; // Off, 1/4, 1/2, 1 Bar (4/4)
        const double bpm = juce::jmax (20.0, proc.hostBpm.load());
        const double sr  = proc.currentSampleRate.load();
        const double windowSamples = beats[syncDiv] * (60.0 / bpm) * sr;
        samplesPerPixel = (float) juce::jmax (1.0, windowSamples / (double) juce::jmax (1, width));
    }

    const int window = (int) (width * samplesPerPixel);
    const int captureSize = window * 2;
    capture.resize ((size_t) captureSize); // reserved in ctor -> no realloc, copyLatest overwrites
    copyLatestMono (capture.data(), captureSize); // also fills capL/capR members
    bool triggered = false;
    const float start = findTriggerIndex (capture.data(), captureSize, window, mode, threshold, 0.05f, &triggered);

    // measurement: peak-to-peak + RMS over the VISIBLE window (single pass), so the
    // readout matches what's drawn rather than the 2x-window trigger-search capture.
    {
        const int s0 = juce::jlimit (0, captureSize, (int) start);
        const int s1 = juce::jlimit (0, captureSize, s0 + window);
        float mn = 1.0e9f, mx = -1.0e9f;
        for (int i = s0; i < s1; ++i) { const float v = capture[(size_t) i]; mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
        liveVpp = (s1 > s0 && mx > mn) ? (mx - mn) : 0.0f;
        liveRms = (s1 > s0) ? rmsDb (capture.data() + s0, s1 - s0) : -100.0f;
    }

    stereoTrace = (src == 4); // Stereo source: overlay L (primary) + R
    yHi.resize ((size_t) width);
    yLo.resize ((size_t) width);

    // Per-pixel min/max decimation: one read of each window sample, into a true
    // envelope. Collapses to a single line when zoomed in (spp < 1), fills out into
    // a solid waveform when zoomed out -- correct AND cheap regardless of Time zoom.
    auto toPixels = [&] (std::vector<float>& hi, std::vector<float>& lo)
    {
        for (int x = 0; x < width; ++x)
        {
            hi[(size_t) x] = midY - hi[(size_t) x] * ampZoom * halfH; // sample max -> top (small y)
            lo[(size_t) x] = midY - lo[(size_t) x] * ampZoom * halfH; // sample min -> bottom
        }
    };

    if (stereoTrace)
    {
        yHiR.resize ((size_t) width);
        yLoR.resize ((size_t) width);
        decimateMinMax (capL.data(), captureSize, start, samplesPerPixel, width, yLo.data(),  yHi.data());
        decimateMinMax (capR.data(), captureSize, start, samplesPerPixel, width, yLoR.data(), yHiR.data());
        toPixels (yHi, yLo);
        toPixels (yHiR, yLoR);
        return; // sweep hold applies to the mono trace only (unchanged behavior)
    }

    decimateMinMax (capture.data(), captureSize, start, samplesPerPixel, width, yLo.data(), yHi.data());
    toPixels (yHi, yLo);

    // Sweep: Auto (free-run, default), Normal (hold last triggered frame), Single
    // (capture one triggered frame then hold until re-armed). Holds the full envelope.
    const int sweep = (int) proc.apvts.getRawParameterValue ("triggerSweep")->load();
    if (sweep == 2 && prevSweep != 2) liveArmed = true; // re-arm on (re)selecting Single
    prevSweep = sweep;
    if (sweep != 0)
    {
        const bool sizeOk = ((int) heldHi.size() == width && (int) heldLo.size() == width);
        bool hold = false;
        if (sweep == 1)            hold = (! triggered && sizeOk);          // Normal
        else if (triggered && liveArmed) liveArmed = false;                  // Single: accept this frame
        else                       hold = sizeOk;                            // Single: hold
        if (hold) { yHi = heldHi; yLo = heldLo; }
        else      { heldHi = yHi; heldLo = yLo; }
    }
}

bool ScopeComponent::buildBaseShape (int width, float midY, float halfH)
{
    const int cycles = juce::jlimit (1, 8, (int) proc.apvts.getRawParameterValue ("cycles")->load());

    const int captureSize = 8192;
    capture.assign ((size_t) captureSize, 0.0f);
    copyLatestMono (capture.data(), captureSize);
    removeMean (capture.data(), captureSize);

    const PitchResult pr = detectPitch (capture.data(), captureSize, proc.currentSampleRate.load());
    const float period = pr.period;
    const int perCycle = juce::jmax (2, width / cycles);

    if (period > 1.0f)
    {
        lastFreq = (float) (proc.currentSampleRate.load() / (double) period);
        lastClarity = pr.clarity;

        // Decide HOW to build the cycle from cycle self-similarity, not pitch clarity. A supersaw
        // is strongly pitched (clarity ~0.99) yet its detuned oscillators make every cycle
        // different -- averaging them collapses the texture into a static blur ("stuck"). Clarity
        // can't see this; selfSim (autocorr several periods out) can: ~1.0 = one repeating shape,
        // well below = morphing. See cycleSelfSimilarity() and Tests/test_cyclesim.cpp.
        const float selfSim  = cycleSelfSimilarity (capture.data(), captureSize, period);
        const bool  pitched  = pr.clarity >= 0.7f;
        const bool  repeating = pitched && selfSim >= 0.9f; // single osc/wavetable -> average
        cyclesMorph = pitched && ! repeating;               // supersaw/unison -> animate latest
        unstable    = ! pitched;                            // genuinely unpitched -> "no cycle"

        const int anchor = findRisingZero (capture.data(), captureSize, (int) (period * 2.0f));
        // Repeating: average recent cycles into one clean, denoised shape. Morphing/unpitched:
        // take the LATEST single cycle so the live texture is preserved, not averaged away.
        const int recentN = repeating ? 6 : 1;
        auto avg = averageCycle (capture.data(), captureSize, period, perCycle, anchor, true, recentN);
        if (! avg.empty())
        {
            peakNormalize (avg.data(), (int) avg.size(), 0.9f);
            const int n = (int) avg.size();
            if (hasHeld && (int) heldCycle.size() == n)
            {
                // cross-correlation phase anchor: rotate into phase (all modes -> no horizontal jitter)
                const int s = bestCircularShift (avg.data(), heldCycle.data(), n);
                rotateInPlace (avg.data(), n, s);
                if (repeating || cyclesMorph)
                {
                    // EMA blend. Repeating -> slow (0.35): clean, denoised, steady. Morphing ->
                    // fast (0.6): stays alive and follows the detune, only de-jittering frame noise
                    // (full averaging is what made the supersaw look stuck).
                    const float a = repeating ? 0.35f : 0.6f;
                    for (int i = 0; i < n; ++i)
                        heldCycle[(size_t) i] = (1.0f - a) * heldCycle[(size_t) i] + a * avg[(size_t) i];
                    // Re-normalise: bestCircularShift aligns to whole samples only, so sub-sample
                    // phase drift between blended cycles otherwise erodes amplitude over time
                    // (the "breathing" seen in QA). Restoring peak keeps the cycle full-height.
                    peakNormalize (heldCycle.data(), n, 0.9f);
                }
                else
                {
                    heldCycle = std::move (avg); // unpitched: phase-locked snapshot
                }
            }
            else
            {
                heldCycle = std::move (avg); // cold start / length change -> seed
                hasHeld = true;
            }
        }
    }

    if (! hasHeld) return false; // nothing captured yet -> Live fallback

    yHi.resize ((size_t) width);
    yLo.resize ((size_t) width);
    const int hn = (int) heldCycle.size();
    for (int x = 0; x < width; ++x)
    {
        const int idx = juce::jlimit (0, hn - 1, x % hn);
        const float y = midY - heldCycle[(size_t) idx] * halfH; // Base Shape ignores Amplitude (auto-normalized)
        yHi[(size_t) x] = y;
        yLo[(size_t) x] = y; // single line (no envelope) in Base Shape
    }
    return true;
}

void ScopeComponent::drawGrid (juce::Graphics& g, juce::Rectangle<float> b, bool baseShape)
{
    const float midY = b.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int k = 1; k < 16; ++k) g.drawVerticalLine ((int) (b.getWidth() * k / 16.0f), b.getY(), b.getBottom());
    for (int k = 1; k < 8;  ++k) g.drawHorizontalLine ((int) (b.getHeight() * k / 8.0f), b.getX(), b.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (b.getWidth() * k / 4.0f), b.getY(), b.getBottom());
    g.drawHorizontalLine ((int) (b.getHeight() * 0.25f), b.getX(), b.getRight());
    g.drawHorizontalLine ((int) (b.getHeight() * 0.75f), b.getX(), b.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawHorizontalLine ((int) midY, b.getX(), b.getRight());

    g.setColour (juce::Colour (0xff6a717a));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("+1.0", 5, (int) b.getY() + 2,                34, 12, juce::Justification::left);
    g.drawText ("+0.5", 5, (int) (b.getHeight() * 0.25f) - 6, 34, 12, juce::Justification::left);
    g.drawText ("0",    5, (int) midY - 6,                    34, 12, juce::Justification::left);
    g.drawText ("-0.5", 5, (int) (b.getHeight() * 0.75f) - 6, 34, 12, juce::Justification::left);
    g.drawText ("-1.0", 5, (int) b.getBottom() - 14,          34, 12, juce::Justification::left);

    if (baseShape)
    {
        const char* lbl[3] = { "1/4", "1/2", "3/4" };
        for (int k = 1; k < 4; ++k)
            g.drawText (lbl[k - 1], (int) (b.getWidth() * k / 4.0f) - 14, (int) b.getBottom() - 13,
                        28, 12, juce::Justification::centred);
    }
}

void ScopeComponent::copyLatestMono (float* dest, int numSamples)
{
    // resize (not assign): the buffers are reserved once in the ctor, so this never
    // reallocates and copyLatest overwrites every element (no need to pre-zero).
    capL.resize ((size_t) numSamples);
    capR.resize ((size_t) numSamples);
    proc.getScopeBufferL().copyLatest (capL.data(), numSamples);
    proc.getScopeBufferR().copyLatest (capR.data(), numSamples);
    const int src = (int) proc.apvts.getRawParameterValue ("channelSource")->load();
    for (int i = 0; i < numSamples; ++i)
        dest[i] = combineChannel (capL[(size_t) i], capR[(size_t) i], src);
}

void ScopeComponent::drawGhost (juce::Graphics& g, const std::vector<float>& cyc,
                                float midY, float halfH, int width, juce::Colour col)
{
    const int n = (int) cyc.size();
    if (n < 2 || width < 2) return;
    juce::Path p;
    for (int x = 0; x < width; ++x)
    {
        const double pos = (double) x / (double) width * (double) n; // one cycle across width
        const int i0 = (int) pos;
        const int i1 = (i0 + 1 < n) ? i0 + 1 : i0;
        const float frac = (float) (pos - (double) i0);
        const float v = cyc[(size_t) i0] * (1.0f - frac) + cyc[(size_t) i1] * frac;
        const float y = midY - v * halfH;
        if (x == 0) p.startNewSubPath (0.0f, y);
        else        p.lineTo ((float) x, y);
    }
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void ScopeComponent::captureSlot (int slot)
{
    if (! hasHeld || heldCycle.empty()) return;
    (slot == 0 ? capA : capB) = heldCycle;
    repaint();
}

void ScopeComponent::clearCaptures()
{
    capA.clear();
    capB.clear();
    repaint();
}

bool ScopeComponent::exportHeldCycle (const juce::File& file)
{
    if (heldCycle.empty()) return false;
    const int outLen = 2048; // single-cycle wavetable frame
    const int n = (int) heldCycle.size();
    juce::AudioBuffer<float> buf (1, outLen);
    auto* w = buf.getWritePointer (0);
    for (int i = 0; i < outLen; ++i)
        w[i] = cubicAt (heldCycle.data(), n, (double) i / (double) outLen * (double) n);

    juce::WavAudioFormat fmt;
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.get(), 44100.0, 1, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release(); // writer takes ownership of the stream
    return writer->writeFromAudioSampleBuffer (buf, 0, outLen);
}

void ScopeComponent::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> b)
{
    const int fftSize = 1 << kFftOrder;
    const int numBins = fftSize / 2;
    g.fillAll (juce::Colour (0xff0e1114));

    // ---- windowed mono FFT. resize()+fill (not assign): fftData is reserved in the ctor, so
    //      this never reallocates on the message thread. Zero the whole working buffer. ----
    fftData.resize ((size_t) (2 * fftSize));
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    copyLatestMono (fftData.data(), fftSize);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data()); // magnitudes [0..numBins]

    // ---- per-bin dB with asymmetric, frame-rate-INDEPENDENT ballistics (fast attack, slow
    //      release) -> a settled curve like Pro-Q's "Speed", using the real elapsed time. ----
    if ((int) specMag.size() != numBins) specMag.assign ((size_t) numBins, -120.0f);
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    double dt = (lastSpecMs > 0.0) ? (nowMs - lastSpecMs) * 0.001 : 1.0 / 60.0;
    lastSpecMs = nowMs;
    dt = juce::jlimit (0.001, 0.1, dt);
    const float aAtk = (float) (1.0 - std::exp (-dt / 0.02)); // ~20 ms attack
    const float aRel = (float) (1.0 - std::exp (-dt / 0.30)); // ~300 ms release
    if (! frozen)
        for (int i = 0; i < numBins; ++i)
        {
            const float mag = fftData[(size_t) i] / (float) fftSize;
            const float db  = juce::Decibels::gainToDecibels (mag, -120.0f);
            float& s = specMag[(size_t) i];
            s += (db >= s ? aAtk : aRel) * (db - s);
        }

    const double sr   = proc.currentSampleRate.load();
    const double minF = 20.0, maxF = juce::jmax (1000.0, sr * 0.5);
    const float  topDb = 0.0f, botDb = -90.0f; // Pro-Q-style framing
    const int    width = juce::jmax (2, (int) b.getWidth());
    const double logMin = std::log10 (minF), logMax = std::log10 (maxF);
    auto freqToX = [&] (double f) -> float
    {
        const double t = (std::log10 (juce::jlimit (minF, maxF, f)) - logMin) / (logMax - logMin);
        return (float) (b.getX() + t * b.getWidth());
    };
    auto dbToY = [&] (float db) -> float
    {
        const float t = (db - topDb) / (botDb - topDb);
        return b.getY() + juce::jlimit (0.0f, 1.0f, t) * b.getHeight();
    };

    // ---- rebuild per-column lookup tables only when width / sample-rate / FFT size change ----
    if (lutWidth != width || lutSr != sr || lutFftSize != fftSize)
    {
        lutWidth = width; lutSr = sr; lutFftSize = fftSize;
        binStart.assign ((size_t) (width + 1), 1);
        tiltLut.assign  ((size_t) width, 0.0f);
        for (int x = 0; x <= width; ++x)
        {
            const double f = std::pow (10.0, logMin + ((double) x / width) * (logMax - logMin));
            binStart[(size_t) x] = juce::jlimit (1, numBins - 1, (int) (f * (double) fftSize / sr));
            if (x < width) tiltLut[(size_t) x] = 4.5f * (float) std::log2 (juce::jmax (1.0, f) / 1000.0); // +4.5 dB/oct @ 1k
        }
        // Constant-Q smoothing: columns are log-spaced, so a fixed-pixel Gaussian == a fixed
        // fraction of an octave. sigma = 1/12 octave in pixels (single precomputed kernel).
        const double pxPerOct = (double) width / std::log2 (maxF / minF);
        const double sigma = juce::jmax (0.75, pxPerOct / 12.0);
        const int radius = juce::jlimit (1, 96, (int) std::ceil (3.0 * sigma));
        gaussKernel.assign ((size_t) (2 * radius + 1), 0.0f);
        double sum = 0.0;
        for (int k = -radius; k <= radius; ++k) { const double w = std::exp (-0.5 * (k / sigma) * (k / sigma)); gaussKernel[(size_t) (k + radius)] = (float) w; sum += w; }
        for (auto& w : gaussKernel) w = (float) ((double) w / sum);
        colDb.assign ((size_t) width, botDb);
        colSmooth.assign ((size_t) width, botDb);
    }

    // ---- aggregate FFT bins -> one dB per display column (MAX power keeps tonal peaks), then
    //      add the spectral tilt. Columns with < 1 bin (the low end) interpolate for smoothness. ----
    for (int x = 0; x < width; ++x)
    {
        const int b0 = binStart[(size_t) x];
        const int b1 = juce::jmax (b0 + 1, binStart[(size_t) (x + 1)]);
        float v;
        if (b1 - b0 <= 1)
        {
            const double f  = std::pow (10.0, logMin + ((double) x / width) * (logMax - logMin));
            const double fb = f * (double) fftSize / sr;
            const int    i0 = juce::jlimit (1, numBins - 2, (int) fb);
            const float  fr = (float) (fb - (double) i0);
            v = specMag[(size_t) i0] * (1.0f - fr) + specMag[(size_t) (i0 + 1)] * fr;
        }
        else
        {
            float mx = -200.0f;
            for (int i = b0; i < b1 && i < numBins; ++i) mx = juce::jmax (mx, specMag[(size_t) i]); // max dB == max power
            v = mx;
        }
        colDb[(size_t) x] = v + tiltLut[(size_t) x];
    }

    // ---- constant-Q smoothing: Gaussian over the log-spaced columns ----
    const int radius = ((int) gaussKernel.size() - 1) / 2;
    for (int x = 0; x < width; ++x)
    {
        float acc = 0.0f;
        for (int k = -radius; k <= radius; ++k)
            acc += colDb[(size_t) juce::jlimit (0, width - 1, x + k)] * gaussKernel[(size_t) (k + radius)];
        colSmooth[(size_t) x] = acc;
    }

    // ---- grid + labels (18 dB divisions over the 0..-90 window) ----
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    const double fl[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
    for (double f : fl) if (f < maxF) g.drawVerticalLine ((int) freqToX (f), b.getY(), b.getBottom());
    for (float db = topDb; db >= botDb; db -= 18.0f) g.drawHorizontalLine ((int) dbToY (db), b.getX(), b.getRight());
    g.setColour (juce::Colour (0xff6a717a));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    const char* fLbl[] = { "100", "1k", "10k" }; const double fVal[] = { 100, 1000, 10000 };
    for (int k = 0; k < 3; ++k) if (fVal[k] < maxF)
        g.drawText (fLbl[k], (int) freqToX (fVal[k]) - 13, (int) b.getBottom() - 13, 26, 12, juce::Justification::centred);

    // ---- curve (one point per column) + subtle fill. Both Paths are members reused via
    //      clearQuick() so paint() makes no per-frame heap allocation. ----
    specPath.clear();
    specFill.clear();
    specPath.startNewSubPath (b.getX(), dbToY (colSmooth[0]));
    for (int x = 1; x < width; ++x) specPath.lineTo (b.getX() + (float) x, dbToY (colSmooth[(size_t) x]));
    specFill.startNewSubPath (b.getX(), dbToY (colSmooth[0]));
    for (int x = 1; x < width; ++x) specFill.lineTo (b.getX() + (float) x, dbToY (colSmooth[(size_t) x]));
    specFill.lineTo (b.getX() + (float) (width - 1), b.getBottom());
    specFill.lineTo (b.getX(), b.getBottom());
    specFill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x20ff8a2b), 0.0f, b.getY(),
                                             juce::Colour (0x00ff8a2b), 0.0f, b.getBottom(), false));
    g.fillPath (specFill);
    g.setColour (juce::Colour (0xffff8a2b));
    g.strokePath (specPath, juce::PathStrokeType (1.5f));
}
