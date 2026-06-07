#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <vector>

// Always-on stereo goniometer (X-Y Lissajous of L vs R) + correlation meter and
// L/R level readout. Auto-scales so the field fills the panel at any level.
class GoniometerComponent : public juce::Component, private juce::Timer
{
public:
    explicit GoniometerComponent (CycloscopeProcessor&);
    ~GoniometerComponent() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CycloscopeProcessor& proc;
    std::vector<float> capL, capR;
    juce::Image persistImage;               // phosphor-persistence accumulation buffer
    float gonioGain = 1.0f;                  // smoothed display gain (stable, gated, capped)
    float lDbShown = -100.0f, rDbShown = -100.0f; // meter ballistics for the L/R readout
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoniometerComponent)
};
