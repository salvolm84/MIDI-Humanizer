// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "Theme.h"

#include <functional>

namespace mh

{

/** The grid-settings strip: everything about how the viewer looks, mirroring the options
    a DAW's drum editor exposes. None of these are plugin parameters, so they report
    changes through callbacks and are pushed back in through the setters when state is
    restored.
*/
class GridToolbar final : public juce::Component
{
public:
    GridToolbar();

    void paint (juce::Graphics&) override;
    void resized() override;

    void setGridDivision (int index);
    void setRowOrder (int index);
    void setRowVisibility (int index);
    void setShowSource (bool shouldShow);
    void setFollowPlayhead (bool shouldFollow);
    void setRowHeight (double height);
    void setDrumMapName (const juce::String& name);
    void setColourMode (int index);
    void setTipsEnabled (bool enabled);

    std::function<void (int)>    onGridDivisionChanged;
    std::function<void (int)>    onRowOrderChanged;
    std::function<void (int)>    onRowVisibilityChanged;
    std::function<void (bool)>   onShowSourceChanged;
    std::function<void (bool)>   onFollowPlayheadChanged;
    std::function<void (double)> onRowHeightChanged;
    std::function<void (int)>    onColourModeChanged;
    std::function<void (bool)>   onTipsEnabledChanged;
    std::function<void (double)> onZoom;
    std::function<void()>        onZoomToFit;
    std::function<void()>        onLoadDrumMap;
    std::function<void()>        onClearDrumMap;

    static constexpr int preferredHeight = 28;

private:
    juce::Label gridLabel, rowLabel, heightLabel, mapLabel, colourLabel, showLabel;
    juce::ComboBox gridBox, rowOrderBox, colourBox, showBox;
    juce::ToggleButton showSourceButton, followButton, tipsButton;
    juce::Slider rowHeightSlider;
    juce::TextButton zoomOutButton, zoomInButton, zoomFitButton, loadMapButton, clearMapButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridToolbar)
};

} // namespace mh
