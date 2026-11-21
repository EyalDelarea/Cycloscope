#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A caption label above a control. Reparents the passed control as a child so the
// pair lays out and shows/hides as one unit. The control must outlive this object.
class LabeledControl : public juce::Component
{
public:
    LabeledControl (const juce::String& name, juce::Component& controlToWrap)
        : control (controlToWrap)
    {
        // Engraved-caption look: uppercase + wide tracking at a small size, the
        // standard premium treatment for control captions (matches the STEREO cap).
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (9.0f)).withExtraKerningFactor (0.14f));
        label.setColour (juce::Label::textColourId, juce::Colour (0xff8a8f98));
        addAndMakeVisible (label);
        addAndMakeVisible (control);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));

        // Every control in the bar shares one visual centre line: the knob's centre (the
        // rotary area above its 16 px value readout). Drop-downs get a fixed, button-like
        // height and a capped width instead of stretching into tall empty boxes on wide
        // windows; the switch lines up with the knobs too.
        const auto knobArea = r.withTrimmedBottom (16);
        if (dynamic_cast<juce::ComboBox*> (&control) != nullptr)
        {
            const int h = juce::jmin (knobArea.getHeight(), getTopLevelComponent()->getWidth() >= 1800 ? 32 : 28);
            const int w = juce::jmin (r.getWidth(), juce::jlimit (168, 232, r.getWidth() / 2)); // grows a little on big windows
            control.setBounds (juce::Rectangle<int> (w, h).withCentre (knobArea.getCentre()));
        }
        else if (dynamic_cast<juce::ToggleButton*> (&control) != nullptr)
        {
            control.setBounds (knobArea);
        }
        else
        {
            control.setBounds (r);
        }
    }

private:
    juce::Label label;
    juce::Component& control;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabeledControl)
};
