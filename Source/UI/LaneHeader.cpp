// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "LaneHeader.h"

#include <algorithm>
#include <cmath>

namespace mh
{

namespace
{
    void addUnique (std::vector<int>& target, int value)
    {
        if (std::find (target.begin(), target.end(), value) == target.end())
            target.push_back (value);
    }
}

void LaneModel::rebuild (const NoteSequence& source, const NoteSequence& humanized,
                         const DrumMapping& mapping, Visibility visibility, Order order,
                         const std::function<LaneSettings (int)>& laneProvider)
{
    std::vector<int> used;

    for (auto& n : source.notes)
        addUnique (used, n.noteNumber);

    // Ghost notes can introduce lanes the source never touched, and they must not
    // appear out of nowhere with no row to sit on.
    for (auto& n : humanized.notes)
        addUnique (used, n.noteNumber);

    // Everything that could be shown, before the visibility mode narrows it.
    std::vector<int> candidates;

    if (! mapping.isEmpty())
    {
        candidates = mapping.mappedNotes();

        for (auto note : used)
            addUnique (candidates, note);
    }
    else if (! used.empty())
    {
        const auto lowest  = *std::min_element (used.begin(), used.end());
        const auto highest = *std::max_element (used.begin(), used.end());

        for (int note = lowest; note <= highest; ++note)
            candidates.push_back (note);
    }

    if (candidates.empty())
        for (int note = 35; note <= 81; ++note)   // GM percussion range as a starting point
            candidates.push_back (note);

    const auto isUsed = [&used] (int note)
    {
        return std::find (used.begin(), used.end(), note) != used.end();
    };

    std::vector<int> visible;
    hiddenUsedRows = 0;

    for (auto note : candidates)
    {
        auto keep = true;

        switch (visibility)
        {
            case Visibility::allRows:        break;
            case Visibility::hideUnused:     keep = isUsed (note); break;
            case Visibility::hideUnlabelled: keep = mapping.hasSpecificName (note); break;
        }

        if (keep)
            visible.push_back (note);
        else if (isUsed (note))
            ++hiddenUsedRows;   // a row with notes in it is being hidden; the UI says so
    }

    // Never end up with an empty list: a blank grid looks like a broken plugin.
    if (visible.empty())
        visible = candidates;

    const auto effectiveOrder = (order == Order::mapOrder && mapping.isEmpty())
                                    ? Order::pitchDescending
                                    : order;

    std::sort (visible.begin(), visible.end(), [&mapping, effectiveOrder] (int a, int b)
    {
        switch (effectiveOrder)
        {
            case Order::mapOrder:        return mapping.sortKeyFor (a) < mapping.sortKeyFor (b);
            case Order::kitOrder:        return laneSortKey (a) < laneSortKey (b);
            case Order::pitchDescending: break;
        }

        return a > b;
    });

    rows.clear();
    rows.reserve (visible.size());

    for (auto note : visible)
    {
        LaneRow row;
        row.note  = note;
        row.label = mapping.labelFor (note);
        row.used  = isUsed (note);

        if (laneProvider != nullptr)
        {
            const auto settings = laneProvider (note);
            row.humanized      = settings.enabled;
            row.muted          = settings.muted;
            row.soloed         = settings.soloed;
            row.hasOverride    = settings.hasOverride;
            row.groupId        = settings.groupId;
            row.timingBiasMs   = settings.timingBiasMs;
            row.velocityTarget = settings.velocityTarget;
        }

        rows.push_back (row);
    }

    sendChangeMessage();
}

void LaneModel::setSelection (std::vector<int> notes)
{
    if (notes == selection)
        return;

    selection = std::move (notes);
    sendChangeMessage();
}

bool LaneModel::isSelected (int noteNumber) const
{
    return std::find (selection.begin(), selection.end(), noteNumber) != selection.end();
}

void LaneModel::setSelectedNote (int noteNumber)
{
    if (noteNumber < 0)
        setSelection ({});
    else
        setSelection ({ noteNumber });
}

int LaneModel::indexOfNote (int noteNumber) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].note == noteNumber)
            return (int) i;

    return -1;
}

int LaneModel::rowAtY (float y) const noexcept
{
    if (rowHeight <= 0.0)
        return -1;

    const auto index = (int) std::floor (((double) y + scrollY) / rowHeight);
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? index : -1;
}

void LaneModel::clampScroll (int viewHeight)
{
    const auto maxScroll = juce::jmax (0.0, contentHeight() - (double) viewHeight);
    scrollY = juce::jlimit (0.0, maxScroll, scrollY);
}

void LaneModel::setScroll (double newScrollY, int viewHeight)
{
    scrollY = newScrollY;
    clampScroll (viewHeight);
    sendChangeMessage();
}

void LaneModel::setRowHeight (double newHeight, int viewHeight)
{
    const auto clamped = juce::jlimit (minRowHeight, maxRowHeight, newHeight);

    if (std::abs (clamped - rowHeight) < 1.0e-6)
        return;

    rowHeight = clamped;
    clampScroll (viewHeight);
    sendChangeMessage();
}

//==============================================================================
LaneHeader::LaneHeader (LaneModel& model)
    : lanes (model)
{
    lanes.addChangeListener (this);
}

LaneHeader::~LaneHeader()
{
    lanes.removeChangeListener (this);
}

void LaneHeader::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBg);

    const auto width = getWidth();
    const auto& rows = lanes.getRows();
    const auto showNames = lanes.rowHeight >= 9.0;

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto y = lanes.yForRow ((int) i);
        const auto height = (float) lanes.rowHeight;

        if (y + height < 0.0f)
            continue;

        if (y > (float) getHeight())
            break;

        const juce::Rectangle<float> rowBounds (0.0f, y, (float) width, height);
        const auto& row = rows[i];
        const auto isSelected = lanes.isSelected (row.note);
        const auto isPrimary = row.note == lanes.getSelectedNote();

        auto background = (i % 2) == 0 ? theme::panelBg : theme::panelBgAlt;

        if (isPrimary)
            background = theme::accentDim;
        else if (isSelected)
            background = theme::accentDim.withMultipliedBrightness (0.72f);
        else if ((int) i == hoveredRow)
            background = background.brighter (0.12f);

        g.setColour (background);
        g.fillRect (rowBounds);

        g.setColour (theme::rowLine);
        g.drawHorizontalLine ((int) std::round (y + height) - 1, 0.0f, (float) width);

        // Humanization on/off for this drum, as a click target on the left edge.
        const auto dotSize = juce::jmin (7.0f, height - 5.0f);
        const juce::Rectangle<float> dot (5.0f, y + (height - dotSize) * 0.5f, dotSize, dotSize);

        if (row.humanized)
        {
            g.setColour (row.hasOverride ? theme::accent : theme::textSecondary);
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (theme::textDim);
            g.drawEllipse (dot, 1.0f);
        }

        // Playback state, kept visually distinct from the humanize dot because they mean
        // different things: one is "vary this drum", the other is "let me hear it".
        if (row.soloed || row.muted)
        {
            g.setColour (row.soloed ? theme::soloMarker : theme::warning);
            g.setFont (theme::labelFont (juce::jmin (10.5f, (float) lanes.rowHeight - 4.0f), true));
            g.drawText (row.soloed ? "S" : "M", enableColumnWidth, (int) y,
                        muteColumnWidth, (int) height, juce::Justification::centred, false);
        }

        if (! showNames)
            continue;

        const auto summaryWidth = width >= 210 ? 68 : 0;
        const auto nameRight = width - 30 - summaryWidth - groupColumnWidth;

        // Linked lanes carry a letter, so a group is identifiable without having to
        // select it and read the panel.
        if (row.groupId > 0)
        {
            g.setColour (theme::groupColour (row.groupId));
            g.setFont (theme::labelFont (juce::jmin (10.5f, (float) lanes.rowHeight - 4.0f), true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + (row.groupId - 1) % 26)),
                        width - 30 - groupColumnWidth, (int) y, groupColumnWidth, (int) height,
                        juce::Justification::centred, false);
        }

        // Lanes with no hits are dimmed rather than hidden, so the layout does not jump
        // around while the user is auditioning different humanization settings.
        auto nameColour = row.used ? theme::textPrimary : theme::textDim;

        if (! row.humanized)
            nameColour = nameColour.withAlpha (0.55f);

        g.setColour (nameColour);
        g.setFont (theme::labelFont (juce::jmin (12.0f, (float) lanes.rowHeight - 2.0f)));
        const auto nameLeft = enableColumnWidth + muteColumnWidth;
        g.drawText (row.label, nameLeft, (int) y,
                    juce::jmax (10, nameRight - nameLeft), (int) height,
                    juce::Justification::centredLeft, false);

        // Only overriding lanes show numbers: the point is to spot at a glance which
        // drums have been given their own treatment.
        if (summaryWidth > 0 && row.hasOverride && row.humanized)
        {
            juce::String summary;
            summary << (row.timingBiasMs >= 0.0f ? "+" : "") << juce::roundToInt (row.timingBiasMs);

            if (row.velocityTarget > 0)
                summary << " / " << row.velocityTarget;

            g.setColour (theme::accent);
            g.setFont (theme::labelFont (juce::jmin (11.0f, (float) lanes.rowHeight - 3.0f)));
            g.drawText (summary, nameRight, (int) y, summaryWidth - 4, (int) height,
                        juce::Justification::centredRight, false);
        }

        g.setColour (isSelected ? theme::textPrimary : theme::textDim);
        g.drawText (juce::String (row.note), width - 30, (int) y, 26, (int) height,
                    juce::Justification::centredRight, false);
    }

    g.setColour (theme::panelBorder);
    g.drawVerticalLine (width - 1, 0.0f, (float) getHeight());
}

void LaneHeader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    lanes.setScroll (lanes.scrollY - (double) wheel.deltaY * lanes.rowHeight * 4.0, getHeight());
}

void LaneHeader::mouseMove (const juce::MouseEvent& event)
{
    const auto row = lanes.rowAtY ((float) event.y);

    if (row == hoveredRow)
        return;

    hoveredRow = row;
    repaint();
}

void LaneHeader::mouseDown (const juce::MouseEvent& event)
{
    const auto rowIndex = lanes.rowAtY ((float) event.y);

    if (rowIndex < 0)
        return;

    const auto& rows = lanes.getRows();
    const auto note = rows[(size_t) rowIndex].note;

    if (event.x < enableColumnWidth)
    {
        if (onLaneEnableToggled != nullptr)
            onLaneEnableToggled (note);

        return;
    }

    if (event.x < enableColumnWidth + muteColumnWidth)
    {
        if (onLaneMuteToggled != nullptr)
            onLaneMuteToggled (note);

        return;
    }

    if (onSelectionChanged == nullptr)
        return;

    auto selection = lanes.getSelection();

    // A double-click grabs the whole drum family, which is the quick route to linking
    // all the toms or all the cymbals.
    if (event.getNumberOfClicks() > 1)
    {
        selection.clear();

        for (auto& row : rows)
            if (familyOf (row.note) == familyOf (note))
                selection.push_back (row.note);
    }
    else if (event.mods.isCommandDown())
    {
        // Add or remove one lane.
        const auto existing = std::find (selection.begin(), selection.end(), note);

        if (existing != selection.end())
            selection.erase (existing);
        else
            selection.push_back (note);
    }
    else if (event.mods.isShiftDown() && ! selection.empty())
    {
        // Extend from the last click to here, in the order the rows are displayed.
        const auto anchorIndex = lanes.indexOfNote (lanes.getSelectedNote());

        if (anchorIndex >= 0)
        {
            selection.clear();

            for (auto i = juce::jmin (anchorIndex, rowIndex); i <= juce::jmax (anchorIndex, rowIndex); ++i)
                selection.push_back (rows[(size_t) i].note);
        }
        else
        {
            selection = { note };
        }
    }
    else
    {
        // A plain click on a linked lane selects the whole group: that is what makes a
        // group behave as one row rather than as several that happen to match.
        selection = { note };

        if (rows[(size_t) rowIndex].groupId > 0)
        {
            selection.clear();

            for (auto& row : rows)
                if (row.groupId == rows[(size_t) rowIndex].groupId)
                    selection.push_back (row.note);
        }
    }

    onSelectionChanged (std::move (selection));
}

void LaneHeader::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRow < 0)
        return;

    hoveredRow = -1;
    repaint();
}

} // namespace mh
