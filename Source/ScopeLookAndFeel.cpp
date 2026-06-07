#include "ScopeLookAndFeel.h"
#include "BinaryData.h"

ScopeLookAndFeel::ScopeLookAndFeel()
{
    interRegular  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf,  (size_t) BinaryData::InterRegular_ttfSize);
    interMedium   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf,   (size_t) BinaryData::InterMedium_ttfSize);
    interSemiBold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, (size_t) BinaryData::InterSemiBold_ttfSize);
    interBold     = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_ttf,     (size_t) BinaryData::InterBold_ttfSize);
    setDefaultSansSerifTypeface (interRegular);

    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1a1c1f));
    // Value readouts: clean borderless text under the knob (modern-analyzer style), not a
    // boxed text field. Transparent box + bright value text; the edit field below
    // gets a subtle accent treatment only while you're typing.
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xffa6acb4));
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,    juce::Colour (accent).withAlpha (0.35f));
    setColour (juce::TextEditor::backgroundColourId,      juce::Colour (0xff14171a));
    setColour (juce::TextEditor::outlineColourId,         juce::Colour (accent).withAlpha (0.6f));
    setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colour (accent));
    setColour (juce::TextEditor::highlightColourId,       juce::Colour (accent).withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId,       juce::Colour (accent));
    setColour (juce::ComboBox::backgroundColourId,        juce::Colour (0xff0c0d0f));
    setColour (juce::ComboBox::outlineColourId,           juce::Colour (0xff34373d));
    setColour (juce::ComboBox::textColourId,              juce::Colour (0xffc0c5cc));
    setColour (juce::ComboBox::arrowColourId,             juce::Colour (accent));
    setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (0xff141517));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (accent));
    setColour (juce::TextButton::buttonColourId,          juce::Colour (0xff0c0d0f));
    setColour (juce::TextButton::buttonOnColourId,        juce::Colour (accent));
    setColour (juce::TextButton::textColourOnId,          juce::Colour (0xff231202));
    setColour (juce::TextButton::textColourOffId,         juce::Colour (0xff9aa0a8));
    setColour (juce::ToggleButton::tickColourId,          juce::Colour (accent));
}

juce::Typeface::Ptr ScopeLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Leave monospaced requests alone: the Vpp/RMS/corr numerals rely on fixed pitch.
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return juce::LookAndFeel_V4::getTypefaceForFont (f);

    if (interRegular == nullptr)
        return juce::LookAndFeel_V4::getTypefaceForFont (f);

    return (f.getTypefaceStyle() == "Bold" || f.isBold()) ? interBold : interRegular;
}

void ScopeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    // centre a square inside the (often wider) slot so the knob stays circular
    const float side = (float) juce::jmin (w, h);
    const auto bounds = juce::Rectangle<float> (
        (float) x + ((float) w - side) * 0.5f,
        (float) y + ((float) h - side) * 0.5f,
        side, side).reduced (3.0f);
    const auto c = bounds.getCentre();
    const float radius = bounds.getWidth() * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    using PS = juce::PathStrokeType;
    const auto arc = [&] (float r, float from, float to)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
        return p;
    };

    // --- value track + glowing value arc (modern-analyzer style), drawn OUTSIDE the body ---
    const float arcR  = radius - 2.5f;
    const float arcW  = juce::jmax (2.5f, radius * 0.11f);
    g.setColour (juce::Colour (0xff26292f));
    g.strokePath (arc (arcR, startAngle, endAngle), PS (arcW, PS::curved, PS::rounded));

    const juce::Colour acc (accent);
    g.setColour (acc.withAlpha (0.22f));                                   // soft outer glow
    g.strokePath (arc (arcR, startAngle, angle), PS (arcW * 2.2f, PS::curved, PS::rounded));
    g.setColour (acc);                                                      // crisp value arc
    g.strokePath (arc (arcR, startAngle, angle), PS (arcW, PS::curved, PS::rounded));

    // --- dished knob body: radial-ish vertical gradient, lighter at top ---
    const float bodyR = radius * 0.70f;
    const auto body = juce::Rectangle<float> (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff363941), c.x, c.y - bodyR,
                                             juce::Colour (0xff15161a), c.x, c.y + bodyR, false));
    g.fillEllipse (body);
    g.setColour (juce::Colour (0xff0d0e10));                                // grounding rim
    g.drawEllipse (body, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));                   // top sheen
    g.strokePath (arc (bodyR - 0.5f, startAngle, endAngle * 0.6f), PS (1.2f, PS::curved, PS::rounded));

    // --- indicator: bright glowing dot riding the arc end ---
    const float dx = c.x + arcR * std::sin (angle);
    const float dy = c.y - arcR * std::cos (angle);
    g.setColour (acc.withAlpha (0.40f));
    g.fillEllipse (dx - arcW, dy - arcW, arcW * 2.0f, arcW * 2.0f);
    g.setColour (acc.brighter (0.4f));
    g.fillEllipse (dx - arcW * 0.5f, dy - arcW * 0.5f, arcW, arcW);

    // --- short pointer tick on the body pointing at the value ---
    juce::Path tick;
    tick.addRoundedRectangle (-1.1f, -bodyR + 2.0f, 2.2f, bodyR * 0.42f, 1.1f);
    tick.applyTransform (juce::AffineTransform::rotation (angle).translated (c));
    g.setColour (juce::Colour (0xffb8bcc2));
    g.fillPath (tick);
}

void ScopeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin (22.0f, r.getHeight());
    const float w = h * 1.8f;
    const auto pill = juce::Rectangle<float> (r.getCentreX() - w * 0.5f,
                                              r.getCentreY() - h * 0.5f, w, h);
    const bool on = b.getToggleState();
    const juce::Colour acc (accent);

    // Concept: a glowing switch. ON = accent-lit track with a soft halo (the same accent-
    // glow cue as the knob arc); OFF = a recessed dark track.
    if (on)
    {
        g.setColour (acc.withAlpha (0.22f));                               // halo
        g.fillRoundedRectangle (pill.expanded (3.0f), (h + 6.0f) * 0.5f);
        g.setGradientFill (juce::ColourGradient::vertical (acc.brighter (0.10f), pill.getY(),
                                                           acc.darker (0.10f), pill.getBottom()));
        g.fillRoundedRectangle (pill, h * 0.5f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff17181c), pill.getY(),
                                                           juce::Colour (0xff24272d), pill.getBottom()));
        g.fillRoundedRectangle (pill, h * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));              // inset top shadow
        g.drawLine (pill.getX() + h * 0.5f, pill.getY() + 1.0f, pill.getRight() - h * 0.5f, pill.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colour (0xff34383f));
        g.drawRoundedRectangle (pill, h * 0.5f, 1.0f);
    }

    // Thumb: a dished disc (same material as the knob body) so it reads as a raised cap.
    const float kd = h - 6.0f;
    const float kx = on ? pill.getRight() - kd - 3.0f : pill.getX() + 3.0f;
    const float ky = pill.getCentreY() - kd * 0.5f;
    const auto thumb = juce::Rectangle<float> (kx, ky, kd, kd);
    g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xfffff1e2 : 0xffc4c8ce), thumb.getCentreX(), thumb.getY(),
                                             juce::Colour (on ? 0xfff2c594 : 0xff8b9097), thumb.getCentreX(), thumb.getBottom(), false));
    g.fillEllipse (thumb);
}

void ScopeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = 4.0f;
    const bool on = b.getToggleState();

    // Raised tactile surface: fill is a step lighter than the panel behind it (so the
    // shape reads without a hard outline), with a subtle top->bottom gradient. Outer
    // drop shadows get clipped to the button bounds in JUCE, so depth comes from a 1px
    // light top edge + 1px dark bottom edge (an inner bevel) instead. Accent fill is
    // reserved for the toggled-on / hover state — never the resting state.
    // Concept: a RAISED key. Same dished material as the knob body (top-light → bottom-
    // dark), grounded by an inner bevel. Accent is reserved for on/hover as a glow.
    const juce::Colour acc (accent);
    juce::Colour top, bot, border;
    if (on)        { top = acc.brighter (0.06f);   bot = acc;                    border = acc.darker (0.25f); }
    else if (down) { top = juce::Colour (0xff202227); bot = juce::Colour (0xff17181b); border = juce::Colour (0xff42454c); }
    else if (over) { top = juce::Colour (0xff3a3d44); bot = juce::Colour (0xff202227); border = acc.withAlpha (0.55f); }
    else           { top = juce::Colour (0xff32353c); bot = juce::Colour (0xff1c1d21); border = juce::Colour (0xff42454c); }

    g.setGradientFill (juce::ColourGradient::vertical (top, r.getY(), bot, r.getBottom()));
    g.fillRoundedRectangle (r, radius);

    // Inner bevel: light top edge (catches light), dark bottom edge (grounds it).
    // Dropped on press/active so the button reads as pushed-in / lit.
    if (! down && ! on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawLine (r.getX() + radius, r.getBottom() - 1.0f, r.getRight() - radius, r.getBottom() - 1.0f, 1.0f);
    }

    g.setColour (border);
    g.drawRoundedRectangle (r, radius, 1.0f);

    // Hover glow: a faint accent ring just inside the border — the same "accent = light"
    // cue the knob arc and combo chevron use, so hover feels consistent across controls.
    if (over && ! on && ! down)
    {
        g.setColour (acc.withAlpha (0.16f));
        g.drawRoundedRectangle (r.reduced (1.5f), radius - 1.0f, 1.4f);
    }
}

void ScopeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const juce::Colour c = b.getToggleState() ? juce::Colour (0xff231202)        // dark text on accent
                         : ! b.isEnabled()    ? juce::Colour (0xff62656b)
                         : over               ? juce::Colour (0xffd6dade)
                                              : juce::Colour (0xffbcc1c8);
    g.setColour (c);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 0),
                      juce::Justification::centred, 1);
}

juce::Font ScopeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.5f, buttonHeight * 0.5f)))
        .withExtraKerningFactor (0.015f);
}

void ScopeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    const float radius = 6.0f;
    const bool over = box.isMouseOver();
    const juce::Colour acc (accent);

    // Option F — framed minimal: a flat dark field, one clean hairline, generous air, a
    // small chevron and no divider. The hairline + chevron light to accent on hover.
    g.setColour (juce::Colour (0xff15161a));
    g.fillRoundedRectangle (r, radius);
    g.setColour (over ? acc.withAlpha (0.50f) : juce::Colour (0xff2c2f35));
    g.drawRoundedRectangle (r, radius, 1.0f);

    const float cx = r.getRight() - 13.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.8f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.8f);
    g.setColour (over ? acc : juce::Colour (0xff8a8f96));
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

juce::Font ScopeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    // Fixed, modest size: the default scales to box height and over-inflated the text
    // until the value ("Mono", "Off"…) collided with the arrow zone and ellipsised to "…".
    return juce::Font (juce::FontOptions (12.0f));
}

juce::Font ScopeLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (13.0f));
}

int ScopeLookAndFeel::getPopupMenuBorderSize() { return 5; }

void ScopeLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                  int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
    idealHeight = 27;                                                       // tight, even rows
    idealWidth  = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 52;
}

void ScopeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (juce::Colour (0xff1c1e22));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xff303339));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void ScopeLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
    bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
    const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        auto s = area.reduced (10, 0).toFloat();
        g.setColour (juce::Colour (0xff2c2f35));
        g.fillRect (s.getX(), s.getCentreY(), s.getWidth(), 1.0f);
        return;
    }

    const juce::Colour acc (accent);
    auto row = area.reduced (4, 1).toFloat();

    // Selection shows as an accent fill (no oversized checkmark); hover is a subtle lift.
    if (isTicked)
    {
        g.setColour (acc);
        g.fillRoundedRectangle (row, 4.0f);
    }
    else if (isHighlighted && isActive)
    {
        g.setColour (juce::Colour (0xff282b31));
        g.fillRoundedRectangle (row, 4.0f);
    }

    g.setColour (isTicked        ? juce::Colour (0xff231202)
                 : ! isActive    ? juce::Colour (0xff5a5e65)
                 : isHighlighted ? juce::Colour (0xfff0f2f4)
                                 : juce::Colour (0xffc6cad0));
    g.setFont (getPopupMenuFont());
    g.drawText (text, area.reduced (16, 0), juce::Justification::centredLeft, true);

    if (hasSubMenu)
    {
        const float x = (float) area.getRight() - 14.0f, y = (float) area.getCentreY();
        juce::Path p;
        p.startNewSubPath (x - 3.0f, y - 4.0f);
        p.lineTo (x + 1.0f, y);
        p.lineTo (x - 3.0f, y + 4.0f);
        g.setColour (isTicked ? juce::Colour (0xff231202) : juce::Colour (0xff8a8f96));
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ScopeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Generous left pad (framed-minimal look) + room for the small chevron. Allow a
    // little horizontal squish before ellipsising so "Mono"/"Auto" degrade gracefully.
    label.setBounds (11, 1, juce::jmax (8, box.getWidth() - 11 - 18), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setMinimumHorizontalScale (0.5f);
}
