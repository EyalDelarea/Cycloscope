#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ScopeComponent.h"
#include "GoniometerComponent.h"
#include "ScopeLookAndFeel.h"
#include "LabeledControl.h"
#include "ModeToggle.h"
#include <functional>

// Thin draggable bar between the waveform and the goniometer side panel.
struct DragHandle : juce::Component
{
    std::function<void (int dxScreen)> onDrag;
    int lastX = 0;
    void mouseDown (const juce::MouseEvent& e) override { lastX = e.getScreenPosition().x; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int x = e.getScreenPosition().x;
        if (onDrag) onDrag (x - lastX);
        lastX = x;
    }
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff2a2e33));
        g.fillRect (getWidth() / 2 - 1, 6, 2, getHeight() - 12);
    }
};

class CycloscopeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CycloscopeEditor (CycloscopeProcessor&);
    ~CycloscopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void applyMode (int modeIdx);
    juce::Slider& makeRotary (juce::Slider& s);

    CycloscopeProcessor& processorRef;
    ScopeLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    ScopeComponent scope;
    GoniometerComponent goniometer;
    DragHandle divider;
    juce::Label wordmark;
    juce::TextButton presetButton { "Presets" };
    juce::TextButton capAButton { "A" }, capBButton { "B" }, clearButton { "Clr" }, exportButton { "Export" };

    void showPresetMenu();
    void applyFactory (int idx);
    void savePreset();
    void loadPreset();
    static juce::File presetDir();

    ModeToggle modeToggle;

    juce::ComboBox triggerBox, sourceBox, syncBox, sweepBox;
    juce::Slider thresholdSlider, timeSlider, ampSlider, cyclesSlider, decaySlider;
    juce::ToggleButton freezeButton { "" };

    LabeledControl sourceLC { "Source",    sourceBox };
    LabeledControl decayLC { "Decay",     decaySlider };
    LabeledControl triggerLC { "Trigger",   triggerBox };
    LabeledControl syncLC { "Sync",      syncBox };
    LabeledControl sweepLC { "Sweep",     sweepBox };
    LabeledControl thresholdLC { "Threshold", thresholdSlider };
    LabeledControl timeLC { "Time",      timeSlider };
    LabeledControl ampLC { "Amplitude", ampSlider };
    LabeledControl cyclesLC { "Cycles",    cyclesSlider };
    LabeledControl freezeLC { "Freeze",    freezeButton };

    std::unique_ptr<ComboAttach>  triggerAttach, sourceAttach, syncAttach, sweepAttach;
    std::unique_ptr<SliderAttach> thresholdAttach, timeAttach, ampAttach, cyclesAttach, decayAttach;
    std::unique_ptr<ButtonAttach> freezeAttach;

    int groupOf (LabeledControl* lc) const;
    std::vector<int> dividerXs;
    int controlBarTop = 0, controlBarBottom = 0;

    int shownMode = -1;

    // GPU rendering: attaching a context moves rasterization of the (constantly redrawn,
    // path-heavy) analyzer off the CPU, so it stays smooth on large/ultrawide windows.
    juce::OpenGLContext openGLContext;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CycloscopeEditor)
};
