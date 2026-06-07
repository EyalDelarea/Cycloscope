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

    // waveform body: fill the min/max band. Zero-area (invisible) when zoomed in,
    // a solid silhouette when zoomed out -> reads like a real scope, not an aliased line.
    juce::Path band = top;
    for (int x = width - 1; x >= 0; --x) band.lineTo ((float) x, yLo[(size_t) x]);
    band.closeSubPath();
    g.setColour (juce::Colour (soft ? 0x22ff8a2b : 0x59ff8a2b));
    g.fillPath (band);

    // crisp orange edges with a subtle glow underlay on the upper edge. The lower edge
    // strokes opaque-only, so when the band collapses to a line it overlays exactly.
    const juce::Path bot = edgePath (yLo);

    // The glow underlay (wide, soft stroke on the top edge) is the costliest draw at high
    // Time zoom -- and there the filled band already supplies the body/glow, so the halo
    // is invisible. Draw it only when the trace is line-like (thin/no band), where it
    // actually reads. Gate on max band thickness; this also covers Base Shape (a line).
    float maxBand = 0.0f;
    for (int x = 0; x < width; ++x) maxBand = juce::jmax (maxBand, std::abs (yHi[(size_t) x] - yLo[(size_t) x]));
    if (maxBand < 2.0f)
    {
        g.setColour (juce::Colour (soft ? 0x18ff8a2b : 0x33ff8a2b));
        g.strokePath (top, juce::PathStrokeType (3.0f));
    }
    g.setColour (juce::Colour (soft ? 0x66ff8a2b : 0xffff8a2b));
    g.strokePath (top, juce::PathStrokeType (1.8f));
    g.strokePath (bot, juce::PathStrokeType (1.8f));

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
                               + "   " + juce::String ((int) (lastClarity * 100.0f)) + "%";
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
        const bool stable = pr.clarity >= 0.7f;
        unstable = ! stable;

        const int anchor = findRisingZero (capture.data(), captureSize, (int) (period * 2.0f));
        // Stable (single oscillator/wavetable): average recent cycles into one clean cycle.
        // Unstable (supersaw/unison/detune is non-periodic by design): take the LATEST single
        // cycle as an animated snapshot so the detune texture is preserved, not averaged away.
        const int recentN = stable ? 6 : 1;
        auto avg = averageCycle (capture.data(), captureSize, period, perCycle, anchor, true, recentN);
        if (! avg.empty())
        {
            peakNormalize (avg.data(), (int) avg.size(), 0.9f);
            const int n = (int) avg.size();
            if (hasHeld && (int) heldCycle.size() == n)
            {
                // cross-correlation phase anchor: rotate into phase (both modes -> no horizontal jitter)
                const int s = bestCircularShift (avg.data(), heldCycle.data(), n);
                rotateInPlace (avg.data(), n, s);
                if (stable)
                {
                    const float a = 0.35f; // EMA blend -> clean, denoised, steady
                    for (int i = 0; i < n; ++i)
                        heldCycle[(size_t) i] = (1.0f - a) * heldCycle[(size_t) i] + a * avg[(size_t) i];
                    // Re-normalise: bestCircularShift aligns to whole samples only, so sub-sample
                    // phase drift between blended cycles otherwise erodes amplitude over time
                    // (the "breathing" seen in QA). Restoring peak keeps the cycle full-height.
                    peakNormalize (heldCycle.data(), n, 0.9f);
                }
                else
                {
                    heldCycle = std::move (avg); // phase-locked snapshot that animates in place
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

    // windowed mono FFT
    fftData.assign ((size_t) (2 * fftSize), 0.0f);
    copyLatestMono (fftData.data(), fftSize);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data()); // magnitudes [0..numBins]

    if ((int) specMag.size() != numBins) specMag.assign ((size_t) numBins, -120.0f);
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[(size_t) i] / (float) fftSize;
        const float db  = juce::Decibels::gainToDecibels (mag, -120.0f);
        specMag[(size_t) i] = frozen ? specMag[(size_t) i]
                                     : specMag[(size_t) i] * 0.7f + db * 0.3f; // temporal smoothing
    }

    const double sr   = proc.currentSampleRate.load();
    const double minF = 20.0, maxF = juce::jmax (1000.0, sr * 0.5);
    const float  topDb = 6.0f, botDb = -96.0f;
    auto freqToX = [&] (double f) -> float
    {
        const double t = (std::log10 (juce::jlimit (minF, maxF, f)) - std::log10 (minF))
                       / (std::log10 (maxF) - std::log10 (minF));
        return (float) (b.getX() + t * b.getWidth());
    };
    auto dbToY = [&] (float db) -> float
    {
        const float t = (db - topDb) / (botDb - topDb);
        return b.getY() + juce::jlimit (0.0f, 1.0f, t) * b.getHeight();
    };

    // grid + labels
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    const double fl[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
    for (double f : fl) if (f < maxF) g.drawVerticalLine ((int) freqToX (f), b.getY(), b.getBottom());
    for (float db = topDb; db >= botDb; db -= 24.0f) g.drawHorizontalLine ((int) dbToY (db), b.getX(), b.getRight());
    g.setColour (juce::Colour (0xff6a717a));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    const char* fLbl[] = { "100", "1k", "10k" }; const double fVal[] = { 100, 1000, 10000 };
    for (int k = 0; k < 3; ++k) if (fVal[k] < maxF)
        g.drawText (fLbl[k], (int) freqToX (fVal[k]) - 13, (int) b.getBottom() - 13, 26, 12, juce::Justification::centred);
    for (float db = topDb - 24.0f; db >= botDb; db -= 24.0f)
        g.drawText (juce::String ((int) db), 4, (int) dbToY (db) - 6, 30, 12, juce::Justification::left);

    // spectrum curve + fill
    juce::Path curve;
    bool started = false;
    for (int i = 1; i < numBins; ++i)
    {
        const double f = (double) i * sr / (double) fftSize;
        if (f < minF) continue;
        if (f > maxF) break;
        const float x = freqToX (f);
        const float y = dbToY (specMag[(size_t) i]);
        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else           curve.lineTo (x, y);
    }
    if (started)
    {
        juce::Path fillP = curve;
        fillP.lineTo (b.getRight(), b.getBottom());
        fillP.lineTo (freqToX (minF), b.getBottom());
        fillP.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x40ff8a2b), 0.0f, b.getY(),
                                                 juce::Colour (0x00ff8a2b), 0.0f, b.getBottom(), false));
        g.fillPath (fillP);
        g.setColour (juce::Colour (0xffff8a2b));
        g.strokePath (curve, juce::PathStrokeType (1.6f));
    }
}
