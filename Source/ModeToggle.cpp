#include "ModeToggle.h"

ModeToggle::ModeToggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramID,
                        juce::StringArray labels)
    : state (s), pid (paramID), segs (std::move (labels))
{
    gliderPos = (float) currentIndex();
    startTimerHz (60);
}

ModeToggle::~ModeToggle() { stopTimer(); }

int ModeToggle::currentIndex() const
{
    return (int) state.getRawParameterValue (pid)->load();
}

void ModeToggle::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmax (1, segs.size());
    const int idx = juce::jlimit (0, n - 1, (int) ((e.position.x / (float) getWidth()) * (float) n));
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid)))
        *p = idx;
}

void ModeToggle::timerCallback()
{
    const float target = (float) currentIndex();
    if (std::abs (target - gliderPos) > 0.001f)
    {
        gliderPos += (target - gliderPos) * 0.25f;
        repaint();
    }
    else if (gliderPos != target)
    {
        gliderPos = target;
        repaint();
    }
}

void ModeToggle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const int n = juce::jmax (1, segs.size());
    const float segW = r.getWidth() / (float) n;

    // Recessed track (matches the dropdown's inset concept).
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff141519), r.getY(),
                                                       juce::Colour (0xff1d1f24), r.getBottom()));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0xff31353b));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    // Active cell = a raised key (same dished material as the buttons) + a glowing accent
    // underline, so selection reads with the same accent-glow cue as the knobs.
    auto cell = juce::Rectangle<float> (r.getX() + gliderPos * segW, r.getY(), segW, r.getHeight());
    auto key = cell.reduced (1.5f);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff33363d), key.getY(),
                                                       juce::Colour (0xff232529), key.getBottom()));
    g.fillRoundedRectangle (key, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (key.getX() + 4.0f, key.getY() + 1.0f, key.getRight() - 4.0f, key.getY() + 1.0f, 1.0f);

    const juce::Colour acc (0xffff8a2b);
    auto under = juce::Rectangle<float> (cell.getX() + 6.0f, cell.getBottom() - 3.0f, segW - 12.0f, 2.2f);
    g.setColour (acc.withAlpha (0.30f));                                   // glow
    g.fillRoundedRectangle (under.expanded (1.5f, 1.2f), 2.0f);
    g.setColour (acc);                                                      // crisp
    g.fillRoundedRectangle (under, 1.1f);

    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    for (int i = 0; i < n; ++i)
    {
        const bool lit = std::abs (gliderPos - (float) i) < 0.5f;
        auto c = juce::Rectangle<float> (r.getX() + (float) i * segW, r.getY(), segW, r.getHeight());
        g.setColour (lit ? juce::Colour (0xffdfe2e6) : juce::Colour (0xff80858d));
        g.drawText (segs[i], c, juce::Justification::centred);
    }
}
