// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Model/DrumMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mh::theme
{

// Chrome ------------------------------------------------------------------------
inline const juce::Colour windowBg      { 0xff232628 };
inline const juce::Colour panelBg       { 0xff2b2e31 };
inline const juce::Colour panelBgAlt    { 0xff32363a };
inline const juce::Colour panelBorder   { 0xff1a1c1e };
inline const juce::Colour headerBg      { 0xff2e3235 };
inline const juce::Colour controlBg     { 0xff3a3e42 };
inline const juce::Colour controlBgOver { 0xff454a4e };
inline const juce::Colour textPrimary   { 0xffd3d7db };
inline const juce::Colour textSecondary { 0xff8f959a };
inline const juce::Colour textDim       { 0xff6a7075 };
inline const juce::Colour accent        { 0xff4a9fd8 };
inline const juce::Colour accentDim     { 0xff2f6d95 };
inline const juce::Colour warning       { 0xffd8813a };
inline const juce::Colour soloMarker    { 0xffe0c94a };

// Drum grid ---------------------------------------------------------------------
inline const juce::Colour gridBg        { 0xff43474a };
inline const juce::Colour gridBgAltRow  { 0xff3c4043 };
inline const juce::Colour barLine       { 0xff1e2022 };
inline const juce::Colour beatLine      { 0xff2c2f31 };
inline const juce::Colour subLine       { 0xff4e5255 };
inline const juce::Colour rowLine       { 0xff383c3f };
inline const juce::Colour playhead      { 0xffe9ebed };
inline const juce::Colour noteOutline   { 0xff1c1e20 };
inline const juce::Colour sourceMarker  { 0x66c9ced2 };
inline const juce::Colour dropHighlight { 0x334a9fd8 };

// Velocity lane -----------------------------------------------------------------
inline const juce::Colour velLaneBg     { 0xff34383b };
inline const juce::Colour velLaneLine   { 0xff43474a };
inline const juce::Colour velBaseline   { 0xff26292c };

/** How notes are tinted in the grid and the velocity lane. */
enum class NoteColourMode
{
    byLane = 0,   ///< a hue per drum, brightness carries velocity
    byVelocity    ///< one blue-to-red ramp shared by every drum
};

/** Cubase-style velocity colourisation: blue at the bottom of the range through
    cyan, green and yellow to red at 127. Velocity is comparable across drums, but every
    drum looks alike.
*/
juce::Colour velocityColour (int velocity);

/** A distinct, stable hue per drum, with velocity carried by brightness and saturation.

    The hue is a golden-ratio walk over the note number, so a drum keeps its colour
    however the rows are filtered or ordered, and adjacent note numbers land far apart on
    the wheel. The brightness floor keeps the quietest hits legible against the grid
    background.

    Linked lanes take their hue from the group instead of the note number, so a group
    reads as one instrument in the grid.
*/
juce::Colour laneColour (int noteNumber, int velocity, int groupId = 0);

/** Full-strength swatch for a link group, for badges and labels. */
juce::Colour groupColour (int groupId);

/** Dispatches to whichever of the two schemes is selected. */
juce::Colour noteColour (int noteNumber, int velocity, NoteColourMode mode, int groupId = 0);

juce::Font labelFont   (float height, bool bold = false);

/** Flat, dark look modelled on the Cubase 15 MIDI editor: no gradients, thin outlines,
    a single blue accent for values and active states.
*/
class CubaseDarkLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CubaseDarkLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace mh::theme
