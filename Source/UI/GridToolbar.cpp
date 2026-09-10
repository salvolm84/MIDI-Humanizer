// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "GridToolbar.h"

#include "../Model/GridDivision.h"

namespace mh
{

namespace
{
    void styleCaption (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::labelFont (11.0f));
        label.setColour (juce::Label::textColourId, theme::textSecondary);
        label.setJustificationType (juce::Justification::centredRight);
    }
}

GridToolbar::GridToolbar()
{
    styleCaption (gridLabel, "Grid");
    styleCaption (rowLabel, "Order");
    styleCaption (heightLabel, "Row h");
    addAndMakeVisible (gridLabel);
    addAndMakeVisible (rowLabel);
    addAndMakeVisible (heightLabel);

    gridBox.setTooltip ("Spacing of the vertical grid lines in the editor. A display setting only: "
                        "it does not quantize anything.");
    rowOrderBox.setTooltip ("How the drum rows are stacked. Map order follows the loaded drum map, "
                            "Pitch puts high notes at the top, and Kit uses drum-notation order "
                            "with cymbals up top and the kick at the bottom.");
    colourBox.setTooltip ("By lane gives every drum its own hue with brightness showing velocity. "
                          "By velocity uses one blue-to-red ramp for every drum, which makes "
                          "loudness comparable between rows but drums look alike.");
    showBox.setTooltip ("Which drum rows the list shows. All rows shows every row a loaded drum map "
                        "defines, or the full pitch range the clip spans. Hide unused shows only the "
                        "drums the clip actually plays. Hide unlabelled shows only rows with a real "
                        "instrument name, which with a drum map loaded means the rows your kit "
                        "defines. Note that Hide unlabelled can hide a row that contains notes; the "
                        "status line says so when it does.");
    showSourceButton.setTooltip ("Draw each note's original position as a hollow marker joined to the "
                                 "humanized one, so you can see how far every hit moved.");
    followButton.setTooltip ("Scroll the view to keep the playhead visible during playback.");
    tipsButton.setTooltip ("Show these explanatory tooltips when hovering over a control.");
    rowHeightSlider.setTooltip ("Height of each drum row in pixels: the vertical zoom.");
    zoomOutButton.setTooltip ("Zoom out horizontally.");
    zoomInButton.setTooltip ("Zoom in horizontally.");
    zoomFitButton.setTooltip ("Fit the whole clip into the visible width.");
    loadMapButton.setTooltip ("Load a Cubase drum map (.drm) or a Reaper MIDI note-name file (.txt) "
                              "to get your kit's real instrument names on the rows. You can also "
                              "just drop the file onto the plugin.");
    clearMapButton.setTooltip ("Clear the drum map and go back to General MIDI percussion names.");

    gridBox.addItemList (gridDivisionLabels(), 1);
    gridBox.setSelectedItemIndex (defaultGridDivisionIndex, juce::dontSendNotification);
    gridBox.onChange = [this]
    {
        if (onGridDivisionChanged != nullptr)
            onGridDivisionChanged (gridBox.getSelectedItemIndex());
    };
    addAndMakeVisible (gridBox);

    rowOrderBox.addItemList ({ "Map order", "Pitch", "Kit" }, 1);
    rowOrderBox.setSelectedItemIndex (0, juce::dontSendNotification);
    rowOrderBox.onChange = [this]
    {
        if (onRowOrderChanged != nullptr)
            onRowOrderChanged (rowOrderBox.getSelectedItemIndex());
    };
    addAndMakeVisible (rowOrderBox);

    showSourceButton.setButtonText ("Show source");
    showSourceButton.onClick = [this]
    {
        if (onShowSourceChanged != nullptr)
            onShowSourceChanged (showSourceButton.getToggleState());
    };
    addAndMakeVisible (showSourceButton);

    followButton.setButtonText ("Follow");
    followButton.onClick = [this]
    {
        if (onFollowPlayheadChanged != nullptr)
            onFollowPlayheadChanged (followButton.getToggleState());
    };
    addAndMakeVisible (followButton);

    styleCaption (colourLabel, "Colour");
    styleCaption (showLabel, "Show");
    addAndMakeVisible (colourLabel);
    addAndMakeVisible (showLabel);

    showBox.addItemList ({ "All rows", "Hide unused", "Hide unlabelled" }, 1);
    showBox.setSelectedItemIndex (1, juce::dontSendNotification);
    showBox.onChange = [this]
    {
        if (onRowVisibilityChanged != nullptr)
            onRowVisibilityChanged (showBox.getSelectedItemIndex());
    };
    addAndMakeVisible (showBox);

    colourBox.addItemList ({ "By lane", "By velocity" }, 1);
    colourBox.setSelectedItemIndex (0, juce::dontSendNotification);
    colourBox.onChange = [this]
    {
        if (onColourModeChanged != nullptr)
            onColourModeChanged (colourBox.getSelectedItemIndex());
    };
    addAndMakeVisible (colourBox);

    tipsButton.setButtonText ("Tips");
    tipsButton.onClick = [this]
    {
        if (onTipsEnabledChanged != nullptr)
            onTipsEnabledChanged (tipsButton.getToggleState());
    };
    addAndMakeVisible (tipsButton);

    rowHeightSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rowHeightSlider.setRange (8.0, 40.0, 1.0);
    rowHeightSlider.setValue (15.0, juce::dontSendNotification);
    rowHeightSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    rowHeightSlider.onValueChange = [this]
    {
        if (onRowHeightChanged != nullptr)
            onRowHeightChanged (rowHeightSlider.getValue());
    };
    addAndMakeVisible (rowHeightSlider);

    const auto configureButton = [this] (juce::TextButton& button, const juce::String& text,
                                         std::function<void()> action)
    {
        button.setButtonText (text);
        button.onClick = std::move (action);
        addAndMakeVisible (button);
    };

    configureButton (zoomOutButton, "-",   [this] { if (onZoom != nullptr) onZoom (1.0 / 1.4); });
    configureButton (zoomInButton,  "+",   [this] { if (onZoom != nullptr) onZoom (1.4); });
    configureButton (zoomFitButton, "Fit", [this] { if (onZoomToFit != nullptr) onZoomToFit(); });
    configureButton (loadMapButton, "Load drum map...", [this] { if (onLoadDrumMap != nullptr) onLoadDrumMap(); });
    configureButton (clearMapButton, "x", [this] { if (onClearDrumMap != nullptr) onClearDrumMap(); });

    mapLabel.setFont (theme::labelFont (11.0f));
    mapLabel.setColour (juce::Label::textColourId, theme::textDim);
    mapLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (mapLabel);

    setDrumMapName ({});
}

void GridToolbar::paint (juce::Graphics& g)
{
    g.fillAll (theme::headerBg);
    g.setColour (theme::panelBorder);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void GridToolbar::resized()
{
    auto bounds = getLocalBounds().reduced (6, 3);

    const auto place = [&bounds] (juce::Component& component, int width, int gapAfter = 4)
    {
        component.setBounds (bounds.removeFromLeft (width));
        bounds.removeFromLeft (gapAfter);
    };

    place (gridLabel, 28, 3);
    place (gridBox, 62, 8);
    place (rowLabel, 34, 3);
    place (rowOrderBox, 78, 8);
    place (showLabel, 34, 3);
    place (showBox, 108, 8);
    place (colourLabel, 40, 3);
    place (colourBox, 86, 10);
    place (showSourceButton, 96);
    place (followButton, 66);
    place (tipsButton, 52, 10);
    place (heightLabel, 36, 3);
    place (rowHeightSlider, 70, 8);
    place (zoomOutButton, 22, 2);
    place (zoomInButton, 22, 2);
    place (zoomFitButton, 32);

    // Drum map controls hug the right-hand edge.
    clearMapButton.setBounds (bounds.removeFromRight (22));
    bounds.removeFromRight (3);
    loadMapButton.setBounds (bounds.removeFromRight (114));
    bounds.removeFromRight (6);
    mapLabel.setBounds (bounds.removeFromRight (juce::jmax (0, juce::jmin (240, bounds.getWidth()))));
}

void GridToolbar::setGridDivision (int index)
{
    gridBox.setSelectedItemIndex (juce::jlimit (0, numGridDivisions() - 1, index), juce::dontSendNotification);
}

void GridToolbar::setRowOrder (int index)
{
    rowOrderBox.setSelectedItemIndex (juce::jlimit (0, 2, index), juce::dontSendNotification);
}

void GridToolbar::setRowVisibility (int index)
{
    showBox.setSelectedItemIndex (juce::jlimit (0, 2, index), juce::dontSendNotification);
}

void GridToolbar::setShowSource (bool shouldShow)
{
    showSourceButton.setToggleState (shouldShow, juce::dontSendNotification);
}

void GridToolbar::setFollowPlayhead (bool shouldFollow)
{
    followButton.setToggleState (shouldFollow, juce::dontSendNotification);
}

void GridToolbar::setRowHeight (double height)
{
    rowHeightSlider.setValue (height, juce::dontSendNotification);
}

void GridToolbar::setColourMode (int index)
{
    colourBox.setSelectedItemIndex (juce::jlimit (0, 1, index), juce::dontSendNotification);
}

void GridToolbar::setTipsEnabled (bool enabled)
{
    tipsButton.setToggleState (enabled, juce::dontSendNotification);
}

void GridToolbar::setDrumMapName (const juce::String& name)
{
    const auto hasMap = name.isNotEmpty();
    mapLabel.setText (hasMap ? "Map: " + name : "Map: General MIDI", juce::dontSendNotification);
    mapLabel.setColour (juce::Label::textColourId, hasMap ? theme::textPrimary : theme::textDim);
    clearMapButton.setEnabled (hasMap);
}

} // namespace mh
