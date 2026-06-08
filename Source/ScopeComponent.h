#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include <vector>
#include <atomic>

class ScopeComponent : public juce::Component, private juce::Timer
{
public:
    explicit ScopeComponent (CycloscopeProcessor&);
    ~ScopeComponent() override;

    void paint (juce::Graphics&) override;

    // Single-cycle workbench: capture the current Base Shape cycle into slot A/B for
    // overlay comparison, and export the held cycle as a single-cycle wavetable WAV.
    void captureSlot (int slot);   // 0 = A, 1 = B
    void clearCaptures();
    bool exportHeldCycle (const juce::File& file);

private:
    void drawGhost (juce::Graphics& g, const std::vector<float>& cyc,
                    float midY, float halfH, int width, juce::Colour col);
    void timerCallback() override;
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> b, bool baseShape);
    void ensureGrid (int w, int h, bool baseShape);
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> b);
    void analyse(); // FFT + per-bin ballistics, runs on the timer (off the paint path)
    void buildLive (std::vector<float>& ys, int width, float midY, float halfH, float ampZoom);
    bool buildBaseShape (std::vector<float>& ys, int width, float midY, float halfH, float ampZoom);
    void copyLatestMono (float* dest, int numSamples);

    CycloscopeProcessor& proc;
    std::vector<float> capture;
    std::vector<float> capL, capR;
    std::vector<float> heldCycle;
    std::vector<float> capA, capB; // captured single cycles for A/B compare
    std::vector<float> traceR;     // right-channel trace for the Stereo source
    bool stereoTrace = false;
    bool hasHeld = false;
    bool unstable = false;
    float lastFreq = 0.0f;
    float lastClarity = 0.0f;
    float liveVpp = 0.0f;   // Live measurement: peak-to-peak
    float liveRms = -100.0f; // Live measurement: RMS dBFS
    std::vector<float> heldLiveFrame; // Normal/Single sweep: last triggered frame
    bool liveArmed = true;
    int prevSweep = -1;

    // Spectrum (FFT) mode
    static constexpr int kFftOrder = 13;        // 8192-point FFT (Pro-Q "Maximum") — resolves harmonics
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { 1 << kFftOrder, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData;                 // 2*fftSize working buffer

    // Analysis output published to paint: a double buffer of per-bin dB written only by
    // the timer (message thread) and read only by paint (GL render thread). specFront is
    // the index of the readable buffer (-1 until the first analyse()); specBack is the
    // one the timer writes next. The previous buffer doubles as the ballistic history.
    std::vector<float> displayedDb;             // per-bin ballistic state (raw, timer thread only)
    std::vector<double> prefixSum;              // scratch for O(n) constant-Q smoothing
    std::vector<float> specBuf[2];              // published (tilted + smoothed) dB for paint
    std::atomic<int> specFront { -1 };
    int specBack = 0;
    double lastAnalyseMs = 0.0;                  // for frame-rate-independent release ballistics

    juce::Image gridCache;                      // cached static grid layer (perf)
    bool gridBaseCached = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
