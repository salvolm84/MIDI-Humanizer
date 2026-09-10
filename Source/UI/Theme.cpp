// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "Theme.h"

#include <cmath>

namespace mh::theme
{

juce::Colour velocityColour (int velocity)
{
    struct Stop { int velocity; juce::uint32 argb; };

    static const Stop stops[] =
    {
        {   1, 0xff3f6fa8 },   // blue
        {  32, 0xff2f9cb2 },   // cyan
        {  64, 0xff5fae48 },   // green
        {  96, 0xffd0b032 },   // yellow
        { 127, 0xffcb4536 },   // red
    };

    const auto v = juce::jlimit (1, 127, velocity);

    for (size_t i = 1; i < juce::numElementsInArray (stops); ++i)
    {
        if (v > stops[i].velocity)
            continue;

        const auto& lo = stops[i - 1];
        const auto& hi = stops[i];
        const auto t = (float) (v - lo.velocity) / (float) (hi.velocity - lo.velocity);

        return juce::Colour (lo.argb).interpolatedWith (juce::Colour (hi.argb), t);
    }

    return juce::Colour (stops[juce::numElementsInArray (stops) - 1].argb);
}

namespace
{
    /** Golden-ratio hue walk: consecutive indices land almost opposite each other on the
        wheel, so neighbouring rows never look alike, and the mapping is stable because it
        depends only on the index.
    */
    float hueForIndex (int index)
    {
        constexpr auto goldenRatioConjugate = 0.6180339887f;
        const auto hue = std::fmod ((float) index * goldenRatioConjugate, 1.0f);
        return hue < 0.0f ? hue + 1.0f : hue;
    }

    /** Groups are salted so their hues do not line up with the note-number sequence. */
    float hueForGroup (int groupId) { return hueForIndex (groupId * 11 + 3); }
}

juce::Colour groupColour (int groupId)
{
    return juce::Colour::fromHSV (hueForGroup (juce::jmax (1, groupId)), 0.58f, 0.88f, 1.0f);
}

juce::Colour laneColour (int noteNumber, int velocity, int groupId)
{
    // Linked lanes share a hue: they are one instrument as far as the user is concerned.
    const auto hue = groupId > 0 ? hueForGroup (groupId)
                                 : hueForIndex (juce::jlimit (0, 127, noteNumber));

    const auto level = (float) (juce::jlimit (1, 127, velocity) - 1) / 126.0f;

    // Velocity is carried by brightness and saturation together, which reads as a much
    // clearer ramp than either alone. The floor matters: the grid background sits at
    // roughly 0.29 brightness, so even the quietest ghost note stays legible.
    const auto brightness = 0.42f + 0.56f * level;
    const auto saturation = 0.34f + 0.54f * level;

    return juce::Colour::fromHSV (hue, saturation, brightness, 1.0f);
}

juce::Colour noteColour (int noteNumber, int velocity, NoteColourMode mode, int groupId)
{
    return mode == NoteColourMode::byLane ? laneColour (noteNumber, velocity, groupId)
                                          : velocityColour (velocity);
}

juce::Font labelFont (float height, bool bold)
{
    auto options = juce::FontOptions().withHeight (height);

    if (bold)
        options = options.withStyle ("Bold");

    return juce::Font (options);
}

//==============================================================================
CubaseDarkLookAndFeel::CubaseDarkLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, windowBg);
    setColour (juce::Label::textColourId,                 textPrimary);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);

    setColour (juce::Slider::textBoxTextColourId,         textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId,   controlBg);
    setColour (juce::Slider::textBoxOutlineColourId,      panelBorder);
    setColour (juce::Slider::rotarySliderFillColourId,    accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff32363a));
    setColour (juce::Slider::thumbColourId,               accent);
    setColour (juce::Slider::trackColourId,               accent);
    setColour (juce::Slider::backgroundColourId,          juce::Colour (0xff32363a));

    setColour (juce::ComboBox::backgroundColourId,        controlBg);
    setColour (juce::ComboBox::textColourId,              textPrimary);
    setColour (juce::ComboBox::outlineColourId,           panelBorder);
    setColour (juce::ComboBox::arrowColourId,             textSecondary);
    setColour (juce::ComboBox::focusedOutlineColourId,    accentDim);

    setColour (juce::PopupMenu::backgroundColourId,           panelBgAlt);
    setColour (juce::PopupMenu::textColourId,                 textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId,      juce::Colours::white);

    setColour (juce::TextButton::buttonColourId,   controlBg);
    setColour (juce::TextButton::buttonOnColourId, accentDim);
    setColour (juce::TextButton::textColourOffId,  textPrimary);
    setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

    setColour (juce::ToggleButton::textColourId,        textPrimary);
    setColour (juce::ToggleButton::tickColourId,        accent);
    setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff53585c));

    setColour (juce::ScrollBar::thumbColourId,      juce::Colour (0xff5a6064));
    setColour (juce::ScrollBar::trackColourId,      juce::Colour (0xff2b2e31));
    setColour (juce::ScrollBar::backgroundColourId, juce::Colour (0xff2b2e31));

    setColour (juce::TooltipWindow::backgroundColourId, panelBgAlt);
    setColour (juce::TooltipWindow::textColourId,       textPrimary);
    setColour (juce::TooltipWindow::outlineColourId,    panelBorder);
}

void CubaseDarkLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.22f);
    const auto arcRadius = radius - thickness * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Bipolar controls read from the centre outwards, which is how Cubase shows a
    // pan-style value; unipolar ones fill from the left.
    const auto isBipolar = slider.getMinimum() < -0.001 && slider.getMaximum() > 0.001;
    const auto originAngle = isBipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Path pointer;
    const auto pointerLength = arcRadius * 0.62f;
    pointer.addRoundedRectangle (-1.0f, -arcRadius + thickness * 0.4f, 2.0f, pointerLength, 1.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (textPrimary);
    g.fillPath (pointer);
}

void CubaseDarkLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (box.isMouseOver (true) ? controlBgOver : box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 2.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

    juce::Path arrow;
    const auto cx = (float) width - 10.0f;
    const auto cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 3.5f, cy - 1.8f, cx + 3.5f, cy - 1.8f, cx, cy + 2.6f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void CubaseDarkLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void CubaseDarkLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool shouldDrawButtonAsHighlighted, bool)
{
    const auto height = (float) button.getHeight();
    const auto boxSize = juce::jmin (14.0f, height - 2.0f);
    const juce::Rectangle<float> box (1.0f, (height - boxSize) * 0.5f, boxSize, boxSize);

    g.setColour (shouldDrawButtonAsHighlighted ? controlBgOver : controlBg);
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (panelBorder);
    g.drawRoundedRectangle (box, 2.0f, 1.0f);

    if (button.getToggleState())
    {
        g.setColour (button.findColour (juce::ToggleButton::tickColourId));
        g.fillRoundedRectangle (box.reduced (3.0f), 1.5f);
    }

    g.setColour (button.findColour (juce::ToggleButton::textColourId)
                       .withAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (labelFont (12.0f));
    const juce::Rectangle<float> textArea (box.getRight() + 6.0f, 0.0f,
                                          (float) button.getWidth() - box.getRight() - 6.0f, height);
    g.drawText (button.getButtonText(), textArea, juce::Justification::centredLeft, false);
}

void CubaseDarkLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& backgroundColour,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.10f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (panelBorder);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
}

void CubaseDarkLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                       .withAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                juce::Justification::centred, false);
}

void CubaseDarkLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                0.0f, 0.0f, style, slider);
        return;
    }

    const auto centreY = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> track ((float) x, centreY - 2.0f, (float) width, 4.0f);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, 2.0f);

    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (track.withWidth (juce::jmax (0.0f, sliderPos - (float) x)), 2.0f);

    g.setColour (textPrimary);
    g.fillEllipse (sliderPos - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
}

void CubaseDarkLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar,
                                           int x, int y, int width, int height,
                                           bool isScrollbarVertical, int thumbStartPosition,
                                           int thumbSize, bool isMouseOver, bool isMouseDown)
{
    g.setColour (bar.findColour (juce::ScrollBar::trackColourId));
    g.fillRect (x, y, width, height);

    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int> (x + 2, thumbStartPosition, width - 4, thumbSize)
        : juce::Rectangle<int> (thumbStartPosition, y + 2, thumbSize, height - 4);

    auto colour = bar.findColour (juce::ScrollBar::thumbColourId);

    if (isMouseDown)
        colour = colour.brighter (0.4f);
    else if (isMouseOver)
        colour = colour.brighter (0.2f);

    g.setColour (colour);
    g.fillRoundedRectangle (thumb.toFloat(), 2.5f);
}

juce::Font CubaseDarkLookAndFeel::getComboBoxFont (juce::ComboBox&)      { return labelFont (12.0f); }
juce::Font CubaseDarkLookAndFeel::getLabelFont (juce::Label& label)      { return labelFont (label.getFont().getHeight()); }
juce::Font CubaseDarkLookAndFeel::getPopupMenuFont()                    { return labelFont (13.0f); }
juce::Font CubaseDarkLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return labelFont (12.0f); }

} // namespace mh::theme
