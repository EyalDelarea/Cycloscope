#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Sliding pill toggle bound to a 2+-choice AudioParameterChoice. Animates a glowing
// glider between segments and writes the parameter on click.
class ModeToggle : public juce::Component, private juce::Timer
{
public:
    ModeToggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramID,
                juce::StringArray labels);
    ~ModeToggle() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int currentIndex() const;

    juce::AudioProcessorValueTreeState& state;
    juce::String pid;
    juce::StringArray segs;
    float gliderPos = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeToggle)
};
