// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Engine/LaneSettings.h"
#include "../Model/DrumMapping.h"
#include "../Model/NoteSequence.h"
#include "Theme.h"

#include <functional>
#include <vector>

namespace mh
{

/** One drum lane as laid out on screen. */
struct LaneRow
{
    int          note = 36;
    juce::String label;
    bool         used = false;   ///< the clip actually contains hits on this lane

    // Mirrored from the lane's resolved settings, so the list can show at a glance
    // which drums have been given their own treatment.
    bool         humanized = true;
    bool         muted = false;
    bool         soloed = false;
    bool         hasOverride = false;
    int          groupId = 0;      ///< non-zero when linked to other lanes
    float        timingBiasMs = 0.0f;
    int          velocityTarget = 0;
};

/** The shared vertical transform: which drum lanes are visible, in what order, at what
    height, and how far the pair of views is scrolled. The name list and the grid both
    read this so their rows always line up.
*/
class LaneModel final : public juce::ChangeBroadcaster
{
public:
    LaneModel() = default;

    /** Which rows the list shows.

        Note that `hideUnlabelled` can hide rows that contain notes, unlike `hideUnused`
        which cannot by definition. That is intentional — otherwise the mode would do
        nothing whenever a clip used notes outside the drum map — but it means the count
        of hidden-but-occupied rows has to be reported so music never goes silently
        invisible.
    */
    enum class Visibility
    {
        allRows = 0,
        hideUnused,       ///< only rows the clip actually plays
        hideUnlabelled    ///< only rows with a real instrument name
    };

    enum class Order
    {
        mapOrder = 0,        ///< the drum map's own row order (Cubase "Order" list)
        pitchDescending,     ///< high notes at the top, like a piano roll
        kitOrder             ///< cymbals, hats, toms, snare, kick: drum-notation stacking
    };

    /** `laneProvider` supplies each note number's resolved settings, keeping this model
        independent of the processor that owns them.
    */
    void rebuild (const NoteSequence& source, const NoteSequence& humanized,
                  const DrumMapping& mapping, Visibility visibility, Order order,
                  const std::function<LaneSettings (int)>& laneProvider);

    /** How many rows containing notes the current visibility mode is hiding. */
    int getHiddenUsedRowCount() const noexcept { return hiddenUsedRows; }

    const std::vector<LaneRow>& getRows() const noexcept { return rows; }
    int numRows() const noexcept                         { return (int) rows.size(); }

    /** Row index for a note number, or -1 when that lane is not shown. */
    int indexOfNote (int noteNumber) const;

    float  yForRow (int index) const noexcept   { return (float) ((double) index * rowHeight - scrollY); }
    int    rowAtY (float y) const noexcept;
    double contentHeight() const noexcept       { return (double) rows.size() * rowHeight; }

    void clampScroll (int viewHeight);
    void setScroll (double newScrollY, int viewHeight);
    void setRowHeight (double newHeight, int viewHeight);

    /** The lanes the control panel is editing. More than one is edited as a unit, which
        is what makes linked rows behave as a single control target.
    */
    void setSelection (std::vector<int> notes);
    const std::vector<int>& getSelection() const noexcept { return selection; }
    bool isSelected (int noteNumber) const;

    /** The lane the knobs read their displayed values from: the last one clicked. */
    int getSelectedNote() const noexcept { return selection.empty() ? -1 : selection.back(); }

    void setSelectedNote (int noteNumber);

    double rowHeight = 15.0;
    double scrollY   = 0.0;

    static constexpr double minRowHeight = 8.0;
    static constexpr double maxRowHeight = 40.0;

private:
    std::vector<LaneRow> rows;
    std::vector<int> selection;
    int hiddenUsedRows = 0;
};

//==============================================================================
/** The drum name list down the left-hand side. Read-only, like the grid. */
class LaneHeader final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::ChangeListener
{
public:
    explicit LaneHeader (LaneModel& model);
    ~LaneHeader() override;

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Reports the whole intended selection, so the modifier handling stays in one
        place rather than being spread between this list and its owner.
    */
    std::function<void (std::vector<int>)> onSelectionChanged;
    std::function<void (int)> onLaneEnableToggled;
    std::function<void (int)> onLaneMuteToggled;

    /** Width of the click target that toggles a lane on and off. */
    static constexpr int enableColumnWidth = 16;

    /** Width of the group-letter badge column. */
    static constexpr int groupColumnWidth = 14;

    /** Click target for muting a lane, immediately right of the humanize dot. */
    static constexpr int muteColumnWidth = 18;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    LaneModel& lanes;
    int hoveredRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneHeader)
};

} // namespace mh
