#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Orange-accent dark theme. Custom rotary slider with a glowing pointer.
class ScopeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr juce::uint32 accent = 0xffff8a2b;

    ScopeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // Popup menus (dropdown lists + the Presets menu): tight rows, Inter, accent-filled
    // selection instead of JUCE's oversized default checkmark.
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    // Route all sans-serif UI text through the embedded Inter weights (monospaced
    // readouts are left untouched so the technical numerals keep their fixed pitch).
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    juce::Typeface::Ptr interRegular, interMedium, interSemiBold, interBold;
};
