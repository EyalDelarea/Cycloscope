#include "PluginProcessor.h"
#include "PluginEditor.h"

CycloscopeProcessor::CycloscopeProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMS", createLayout())
{
    // Allocate once for the max supported rate so the GUI never races a realloc and
    // never dereferences a null buffer (editor can open before prepareToPlay).
    scopeBufferL = std::make_unique<RingBuffer> (192000);
    scopeBufferR = std::make_unique<RingBuffer> (192000);
}

bool CycloscopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false; // analyzer is a passthrough: input must equal output
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorValueTreeState::ParameterLayout CycloscopeProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "triggerMode", 1 }, "Trigger",
        StringArray { "Free", "Rising", "Falling" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold",
        NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "timeZoom", 1 }, "Time",
        NormalisableRange<float> { 1.0f, 64.0f, 0.1f }, 4.0f)); // samples/pixel
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ampZoom", 1 }, "Amplitude",
        NormalisableRange<float> { 0.1f, 8.0f, 0.01f }, 1.0f));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "freeze", 1 }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "displayMode", 1 }, "Mode",
        StringArray { "Live", "Base Shape", "Spectrum" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "cycles", 1 }, "Cycles", 1, 8, 1));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "channelSource", 1 }, "Source",
        StringArray { "Mono", "Left", "Right", "Side", "Stereo" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "syncDiv", 1 }, "Sync",
        StringArray { "Off", "1/4", "1/2", "1 Bar" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "triggerSweep", 1 }, "Sweep",
        StringArray { "Auto", "Normal", "Single" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "stereoDecay", 1 }, "Decay",
        NormalisableRange<float> { 0.0f, 0.995f, 0.005f }, 0.92f)); // up to ~2 s trails for reading a delay's field
    return layout;
}

void CycloscopeProcessor::prepareToPlay (double sampleRate, int)
{
    // Buffers are allocated once in the constructor (no realloc -> no GUI race, no
    // history loss on benign host re-prepares). Just record the sample rate.
    currentSampleRate.store (sampleRate);
}

void CycloscopeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (scopeBufferL != nullptr && scopeBufferR != nullptr && numCh > 0)
    {
        const float* inL = buffer.getReadPointer (0);
        const float* inR = buffer.getReadPointer (numCh > 1 ? 1 : 0);
        for (int n = 0; n < numSamples; ++n)
        {
            scopeBufferL->write (inL[n]);
            scopeBufferR->write (inR[n]); // mono input -> R mirrors L
        }
    }
    // passthrough: output already equals input (visualize only)
}

juce::AudioProcessorEditor* CycloscopeProcessor::createEditor()
{
    return new CycloscopeEditor (*this);
}

void CycloscopeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("editorWidth",  editorWidth.load());
        xml->setAttribute ("editorHeight", editorHeight.load());
        xml->setAttribute ("gonioWidth",   gonioWidth.load());
        xml->setAttribute ("gonioShown",   gonioShown.load() ? 1 : 0);
        copyXmlToBinary (*xml, destData);
    }
}

void CycloscopeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        editorWidth.store  (xml->getIntAttribute ("editorWidth",  editorWidth.load()));
        editorHeight.store (xml->getIntAttribute ("editorHeight", editorHeight.load()));
        gonioWidth.store   (xml->getIntAttribute ("gonioWidth",   gonioWidth.load()));
        gonioShown.store   (xml->getIntAttribute ("gonioShown",   gonioShown.load() ? 1 : 0) != 0);
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CycloscopeProcessor();
}
