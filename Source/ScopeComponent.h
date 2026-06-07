#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "ScopeReduce.h"
#include <vector>

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
    void buildLive (int width, float midY, float halfH, float ampZoom);
    bool buildBaseShape (int width, float midY, float halfH);
    void copyLatestMono (float* dest, int numSamples);

    CycloscopeProcessor& proc;
    std::vector<float> capture;
    std::vector<float> capL, capR;
    std::vector<float> heldCycle;
    std::vector<float> capA, capB; // captured single cycles for A/B compare
    // Per-pixel trace envelope (pixel-space Y): yHi = upper (max), yLo = lower (min).
    // A line collapses to yHi == yLo; a dense waveform fills the band between them.
    std::vector<float> yHi, yLo;     // primary trace (mono / left)
    std::vector<float> yHiR, yLoR;   // right-channel overlay for the Stereo source
    bool stereoTrace = false;
    bool hasHeld = false;
    bool unstable = false;
    float lastFreq = 0.0f;
    float lastClarity = 0.0f;
    float liveVpp = 0.0f;   // Live measurement: peak-to-peak
    float liveRms = -100.0f; // Live measurement: RMS dBFS
    std::vector<float> heldHi, heldLo; // Normal/Single sweep: last triggered frame (envelope)
    bool liveArmed = true;
    int prevSweep = -1;

    // Spectrum (FFT) mode
    static constexpr int kFftOrder = 11;        // 2048-point FFT
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { 1 << kFftOrder, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData;                 // 2*fftSize working buffer
    std::vector<float> specMag;                 // smoothed magnitude (dB) per bin

    juce::Image gridCache;                      // cached static grid layer (perf)
    bool gridBaseCached = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
