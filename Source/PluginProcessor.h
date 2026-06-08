#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RingBuffer.h"
#include <memory>

class CycloscopeProcessor : public juce::AudioProcessor
{
public:
    CycloscopeProcessor();
    ~CycloscopeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cycloscope"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    RingBuffer& getScopeBufferL() noexcept { return *scopeBufferL; }
    RingBuffer& getScopeBufferR() noexcept { return *scopeBufferR; }
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> editorWidth  { 1080 };
    std::atomic<int> editorHeight { 520 };
    std::atomic<int> gonioWidth   { 200 };
    std::atomic<bool> gonioShown  { true };   // collapsible stereo panel: hidden = renders nothing

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    std::unique_ptr<RingBuffer> scopeBufferL, scopeBufferR;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CycloscopeProcessor)
};
