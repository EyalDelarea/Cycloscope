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
    if (frozen) return; // hold the current frame

    // Spectrum: run the FFT + ballistics here (off the paint path) so analysis cadence
    // is decoupled from rendering and a dropped paint can't corrupt the smoothing.
    if ((int) proc.apvts.getRawParameterValue ("displayMode")->load() == 2)
        analyse();

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

    std::vector<float> ys ((size_t) width, midY);
    bool haveTrace = false;
    stereoTrace = false; // buildLive sets it true for the Stereo source

    if (modeIdx == 1) // Base Shape
        haveTrace = buildBaseShape (ys, width, midY, halfH, ampZoom);

    if (! haveTrace) // Live, or Base Shape fallback
        buildLive (ys, width, midY, halfH, ampZoom);

    juce::Path path;
    path.startNewSubPath (0.0f, ys[0]);
    for (int x = 1; x < width; ++x) path.lineTo ((float) x, ys[(size_t) x]);

    // low-clarity sources read as "uncertain": soften the trace (calm, not alarming)
    const bool soft = (modeIdx == 1 && haveTrace && unstable);

    // gradient fill under the trace
    juce::Path fill = path;
    fill.lineTo ((float) (width - 1), midY);
    fill.lineTo (0.0f, midY);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (soft ? 0x14ff8a2b : 0x39ff8a2b), 0.0f, bounds.getY(),
        juce::Colour (0x00ff8a2b), 0.0f, midY, false));
    g.fillPath (fill);

    // crisp orange stroke with a subtle glow underlay (calm)
    g.setColour (juce::Colour (soft ? 0x18ff8a2b : 0x33ff8a2b));
    g.strokePath (path, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colour (soft ? 0x66ff8a2b : 0xffff8a2b));
    g.strokePath (path, juce::PathStrokeType (1.8f));

    // Stereo source: overlay the right channel (blue) on top of left (orange)
    if (stereoTrace && (int) traceR.size() >= width)
    {
        juce::Path pR;
        pR.startNewSubPath (0.0f, traceR[0]);
        for (int x = 1; x < width; ++x) pR.lineTo ((float) x, traceR[(size_t) x]);
        g.setColour (juce::Colour (0xcc5fb0ff));
        g.strokePath (pR, juce::PathStrokeType (1.4f));
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

void ScopeComponent::buildLive (std::vector<float>& ys, int width, float midY,
                                float halfH, float ampZoom)
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
    capture.assign ((size_t) captureSize, 0.0f);
    copyLatestMono (capture.data(), captureSize); // also fills capL/capR members
    bool triggered = false;
    const float start = findTriggerIndex (capture.data(), captureSize, window, mode, threshold, 0.05f, &triggered);

    // measurement (cheap, O(N)): peak-to-peak + RMS over the captured window
    float mn = 1.0e9f, mx = -1.0e9f;
    for (int i = 0; i < captureSize; ++i) { const float v = capture[(size_t) i]; mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
    liveVpp = (mx > mn) ? (mx - mn) : 0.0f;
    liveRms = rmsDb (capture.data(), captureSize);

    stereoTrace = (src == 4); // Stereo source: overlay L (primary) + R
    if (stereoTrace) traceR.assign ((size_t) width, midY);

    auto interp = [] (const std::vector<float>& b, float pos) -> float
    {
        const int i0 = (int) pos;
        if (i0 >= 0 && i0 + 1 < (int) b.size())
        {
            const float f = pos - (float) i0;
            return b[(size_t) i0] * (1.0f - f) + b[(size_t) (i0 + 1)] * f;
        }
        if (i0 >= 0 && i0 < (int) b.size()) return b[(size_t) i0];
        return 0.0f;
    };

    for (int x = 0; x < width; ++x)
    {
        const float pos = start + (float) x * samplesPerPixel; // sub-sample aligned
        if (stereoTrace)
        {
            ys[(size_t) x]     = midY - interp (capL, pos) * ampZoom * halfH;
            traceR[(size_t) x] = midY - interp (capR, pos) * ampZoom * halfH;
        }
        else
        {
            ys[(size_t) x] = midY - interp (capture, pos) * ampZoom * halfH;
        }
    }

    // Sweep: Auto (free-run, default), Normal (hold last triggered frame), Single
    // (capture one triggered frame then hold until re-armed). Applies to the mono trace.
    const int sweep = (int) proc.apvts.getRawParameterValue ("triggerSweep")->load();
    if (sweep == 2 && prevSweep != 2) liveArmed = true; // re-arm on (re)selecting Single
    prevSweep = sweep;
    if (! stereoTrace && sweep != 0)
    {
        const bool sizeOk = ((int) heldLiveFrame.size() == width);
        bool hold = false;
        if (sweep == 1)            hold = (! triggered && sizeOk);          // Normal
        else if (triggered && liveArmed) liveArmed = false;                  // Single: accept this frame
        else                       hold = sizeOk;                            // Single: hold
        if (hold) for (int x = 0; x < width; ++x) ys[(size_t) x] = heldLiveFrame[(size_t) x];
        else      heldLiveFrame.assign (ys.begin(), ys.end());
    }
}

bool ScopeComponent::buildBaseShape (std::vector<float>& ys, int width, float midY,
                                     float halfH, float /*ampZoom*/)
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

    const int hn = (int) heldCycle.size();
    for (int x = 0; x < width; ++x)
    {
        const int idx = juce::jlimit (0, hn - 1, x % hn);
        ys[(size_t) x] = midY - heldCycle[(size_t) idx] * halfH; // Base Shape ignores Amplitude (auto-normalized)
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
    capL.assign ((size_t) numSamples, 0.0f);
    capR.assign ((size_t) numSamples, 0.0f);
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

void ScopeComponent::analyse()
{
    const int fftSize = 1 << kFftOrder;
    const int numBins = fftSize / 2;

    if ((int) specBuf[0].size() != numBins)
    {
        specBuf[0].assign ((size_t) numBins, -120.0f);
        specBuf[1].assign ((size_t) numBins, -120.0f);
        specFront.store (-1, std::memory_order_release);
        specBack = 0;
        lastAnalyseMs = 0.0;
    }

    // windowed mono FFT
    fftData.assign ((size_t) (2 * fftSize), 0.0f);
    copyLatestMono (fftData.data(), fftSize);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data()); // magnitudes [0..numBins]

    // Per-bin ballistics: instant attack (peaks register immediately), slow time-based
    // release (smooth, readable decay). releaseCoeff derives from the *actual* elapsed
    // time so the feel is identical regardless of frame rate / CPU load.
    const int front = specFront.load (std::memory_order_acquire);
    const float* prev = (front >= 0) ? specBuf[(size_t) front].data() : nullptr;
    float* out = specBuf[(size_t) specBack].data();

    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt  = (lastAnalyseMs > 0.0) ? (now - lastAnalyseMs) * 0.001 : 0.0;
    lastAnalyseMs = now;
    constexpr double kReleaseTau = 0.35; // seconds to ~63% — the "good spot", not user-exposed
    const float rel = (prev != nullptr && dt > 0.0) ? (float) (1.0 - std::exp (-dt / kReleaseTau)) : 1.0f;

    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[(size_t) i] / (float) fftSize;
        const float ndb = juce::Decibels::gainToDecibels (mag, -120.0f);
        if (prev == nullptr || ndb >= prev[(size_t) i]) out[(size_t) i] = ndb;            // instant attack
        else out[(size_t) i] = prev[(size_t) i] + (ndb - prev[(size_t) i]) * rel;          // slow release
    }

    specFront.store (specBack, std::memory_order_release); // publish
    specBack = 1 - specBack;
}

void ScopeComponent::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> b)
{
    const int fftSize = 1 << kFftOrder;
    const int numBins = fftSize / 2;
    g.fillAll (juce::Colour (0xff0e1114));

    const double sr   = proc.currentSampleRate.load();
    const double minF = 20.0, maxF = juce::jmax (1000.0, sr * 0.5);
    const float  topDb = 6.0f, botDb = -96.0f;
    auto freqToX = [&] (double f) -> float
    {
        const double t = (std::log10 (juce::jlimit (minF, maxF, f)) - std::log10 (minF))
                       / (std::log10 (maxF) - std::log10 (minF));
        return (float) (b.getX() + t * b.getWidth());
    };
    auto xToFreq = [&] (float x) -> double
    {
        const double t = juce::jlimit (0.0, 1.0, (double) (x - b.getX()) / (double) b.getWidth());
        return minF * std::pow (maxF / minF, t);
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

    const int front = specFront.load (std::memory_order_acquire);
    if (front < 0) return; // nothing analysed yet
    const float* mag = specBuf[(size_t) front].data();

    // Per-pixel log resampling: one vertex per pixel column, peak-aggregating the FFT bins
    // that fall in that column's frequency span. Bounded by pixel width (not bin count),
    // so the dense top end no longer overdraws/jaggies and the sparse low end no longer blocks.
    const int xL = juce::jmax ((int) b.getX(),        (int) std::ceil  (freqToX (minF)));
    const int xR = juce::jmin ((int) b.getRight() - 1, (int) std::floor (freqToX (maxF)));
    juce::Path curve;
    bool started = false;
    for (int x = xL; x <= xR; ++x)
    {
        const double fLo = xToFreq ((float) x - 0.5f);
        const double fHi = xToFreq ((float) x + 0.5f);
        int bLo = juce::jlimit (1, numBins - 1, (int) std::floor (fLo * (double) fftSize / sr));
        int bHi = juce::jlimit (1, numBins - 1, (int) std::ceil  (fHi * (double) fftSize / sr));
        if (bHi < bLo) bHi = bLo;
        float peak = -120.0f;
        for (int i = bLo; i <= bHi; ++i) peak = juce::jmax (peak, mag[(size_t) i]);
        const float y = dbToY (peak);
        if (! started) { curve.startNewSubPath ((float) x, y); started = true; }
        else           curve.lineTo ((float) x, y);
    }
    if (started)
    {
        juce::Path fillP = curve;
        fillP.lineTo ((float) xR, b.getBottom());
        fillP.lineTo ((float) xL, b.getBottom());
        fillP.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x40ff8a2b), 0.0f, b.getY(),
                                                 juce::Colour (0x00ff8a2b), 0.0f, b.getBottom(), false));
        g.fillPath (fillP);
        g.setColour (juce::Colour (0xffff8a2b));
        g.strokePath (curve, juce::PathStrokeType (1.6f));
    }
}
