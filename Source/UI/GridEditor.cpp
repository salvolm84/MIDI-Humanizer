// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "GridEditor.h"

#include <cmath>

namespace mh
{

namespace
{
    /** Width divided by height. A drum diamond is a marker, not a duration, so its
        shape has to stay constant: tying only the height to the row height is what made
        vertical zoom stretch it into a spike.
    */
    constexpr float diamondAspectRatio = 1.2f;

    /** Stops diamonds growing without limit on very tall rows. */
    constexpr float maxDiamondHalfHeight = 12.0f;
    constexpr float minDiamondHalfHeight = 2.5f;
}

GridEditor::GridEditor (TimelineView& timelineToUse, LaneModel& lanesToUse)
    : timeline (timelineToUse), lanes (lanesToUse)
{
    timeline.addChangeListener (this);
    lanes.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

GridEditor::~GridEditor()
{
    timeline.removeChangeListener (this);
    lanes.removeChangeListener (this);
}

void GridEditor::setClips (const NoteSequence* source, const NoteSequence* humanized)
{
    sourceClip = source;
    humanizedClip = humanized;

    sourceIndexById.clear();

    if (sourceClip != nullptr)
        for (size_t i = 0; i < sourceClip->notes.size(); ++i)
            sourceIndexById[sourceClip->notes[i].sourceId] = (int) i;

    hoveredNote = -1;
    repaint();
}

void GridEditor::setShowSource (bool shouldShow)
{
    if (showSource == shouldShow)
        return;

    showSource = shouldShow;
    repaint();
}

void GridEditor::setColourMode (theme::NoteColourMode mode)
{
    if (colourMode == mode)
        return;

    colourMode = mode;
    repaint();
}

void GridEditor::setReferenceBpm (double bpm)
{
    const auto clamped = juce::jlimit (20.0, 999.0, bpm);

    if (std::abs (clamped - referenceBpm) < 1.0e-6)
        return;

    referenceBpm = clamped;
    repaint();
}

void GridEditor::setPlayhead (double ppq, bool playing)
{
    if (std::abs (ppq - playheadPpq) < 1.0e-6 && playing == playheadVisible)
        return;

    playheadPpq = ppq;
    playheadVisible = playing;
    repaint();
}

void GridEditor::setDropActive (bool active)
{
    if (dropActive == active)
        return;

    dropActive = active;
    repaint();
}

const Note* GridEditor::sourceNoteFor (const Note& humanizedNote) const
{
    if (sourceClip == nullptr || humanizedNote.isGhost)
        return nullptr;

    const auto found = sourceIndexById.find (humanizedNote.sourceId);

    if (found == sourceIndexById.end())
        return nullptr;

    return &sourceClip->notes[(size_t) found->second];
}

juce::Point<float> GridEditor::diamondHalfSize() const
{
    const auto halfHeight = juce::jlimit (minDiamondHalfHeight, maxDiamondHalfHeight,
                                          (float) lanes.rowHeight * 0.42f);
    return { halfHeight * diamondAspectRatio, halfHeight };
}

void GridEditor::addDiamond (juce::Path& path, float centreX, float centreY,
                             float halfWidth, float halfHeight)
{
    path.startNewSubPath (centreX - halfWidth, centreY);
    path.lineTo (centreX, centreY - halfHeight);
    path.lineTo (centreX + halfWidth, centreY);
    path.lineTo (centreX, centreY + halfHeight);
    path.closeSubPath();
}

//==============================================================================
void GridEditor::paint (juce::Graphics& g)
{
    drawBackground (g);
    drawGridLines (g);
    drawNotes (g);
    drawPlayhead (g);
    drawHoverInfo (g);

    if (dropActive)
    {
        g.setColour (theme::dropHighlight);
        g.fillAll();
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }
}

void GridEditor::drawBackground (juce::Graphics& g) const
{
    g.fillAll (theme::gridBg);

    const auto& rows = lanes.getRows();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto y = lanes.yForRow ((int) i);
        const auto height = (float) lanes.rowHeight;

        if (y + height < 0.0f)
            continue;

        if (y > (float) getHeight())
            break;

        if ((i % 2) != 0)
        {
            g.setColour (theme::gridBgAltRow);
            g.fillRect (0.0f, y, (float) getWidth(), height);
        }

        g.setColour (theme::rowLine);
        g.drawHorizontalLine ((int) std::round (y + height) - 1, 0.0f, (float) getWidth());
    }

    // Everything past the clip length is outside the loop, so shade it back.
    const auto endX = timeline.xForPpq (timeline.lengthPpq);

    if (endX < (float) getWidth())
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (endX, 0.0f, (float) getWidth() - endX, (float) getHeight());
    }
}

void GridEditor::drawGridLines (juce::Graphics& g) const
{
    const auto width = (float) getWidth();
    const auto height = (float) getHeight();
    const auto bar = timeline.barLengthPpq();
    const auto beat = 4.0 / (double) juce::jmax (1, timeline.timeSigDenominator);
    const auto step = timeline.stepPpq();
    const auto lastPpq = timeline.ppqForX (width);

    // Subdivisions first, then beats, then bars: later passes overwrite earlier ones so
    // the strongest line always wins where they coincide.
    const auto drawLines = [&] (double spacing, juce::Colour colour, float minimumSpacingPx)
    {
        if (spacing <= 0.0 || spacing * timeline.pixelsPerBeat < (double) minimumSpacingPx)
            return;

        g.setColour (colour);
        const auto firstIndex = (juce::int64) std::floor (timeline.scrollBeats / spacing);

        for (auto index = juce::jmax ((juce::int64) 0, firstIndex); ; ++index)
        {
            const auto ppq = (double) index * spacing;

            if (ppq > lastPpq)
                break;

            g.drawVerticalLine ((int) std::round (timeline.xForPpq (ppq)), 0.0f, height);
        }
    };

    drawLines (step, theme::subLine,  4.0f);
    drawLines (beat, theme::beatLine, 3.0f);
    drawLines (bar,  theme::barLine,  2.0f);
}

void GridEditor::drawNotes (juce::Graphics& g) const
{
    if (humanizedClip == nullptr || humanizedClip->notes.empty())
        return;

    const auto width = (float) getWidth();
    const auto height = (float) getHeight();
    const auto halfSize = diamondHalfSize();
    const auto halfWidth = halfSize.x;
    const auto halfHeight = halfSize.y;

    for (size_t i = 0; i < humanizedClip->notes.size(); ++i)
    {
        const auto& note = humanizedClip->notes[i];
        const auto row = lanes.indexOfNote (note.noteNumber);

        if (row < 0)
            continue;

        const auto centreY = lanes.yForRow (row) + (float) lanes.rowHeight * 0.5f;

        if (centreY < -halfHeight || centreY > height + halfHeight)
            continue;

        const auto centreX = timeline.xForPpq (note.startPpq);

        if (centreX < -halfWidth - 40.0f || centreX > width + halfWidth)
            continue;

        // The source position, joined by a hairline, is what makes the humanization
        // legible: you can see how far each hit moved and in which direction.
        if (showSource)
        {
            if (const auto* original = sourceNoteFor (note))
            {
                const auto sourceX = timeline.xForPpq (original->startPpq);

                if (std::abs (sourceX - centreX) > 0.75f)
                {
                    g.setColour (theme::sourceMarker);
                    g.drawLine (sourceX, centreY, centreX, centreY, 1.0f);

                    juce::Path marker;
                    addDiamond (marker, sourceX, centreY, halfWidth * 0.6f, halfHeight * 0.6f);
                    g.strokePath (marker, juce::PathStrokeType (1.0f));
                }
            }
        }

        const auto groupId = lanes.getRows()[(size_t) row].groupId;
        const auto colour = theme::noteColour (note.noteNumber, note.velocity, colourMode, groupId);

        juce::Path diamond;
        addDiamond (diamond, centreX, centreY, halfWidth, halfHeight);

        if (! note.audible)
        {
            // Muted, or another lane is soloed. Drawn hollow and faint so it is clearly
            // still in the clip rather than appearing to have been deleted.
            g.setColour (colour.withAlpha (0.13f));
            g.fillPath (diamond);
            g.setColour (colour.withAlpha (0.5f));
            g.strokePath (diamond, juce::PathStrokeType (1.0f));
        }
        else if (note.isGhost)
        {
            // Generated notes read as outlines so they are never mistaken for source
            // material in a visual reference view.
            g.setColour (colour.withAlpha (0.30f));
            g.fillPath (diamond);
            g.setColour (colour);
            g.strokePath (diamond, juce::PathStrokeType (1.4f));
        }
        else
        {
            g.setColour (colour);
            g.fillPath (diamond);
            g.setColour (theme::noteOutline.withAlpha (0.85f));
            g.strokePath (diamond, juce::PathStrokeType (1.0f));
        }

        if ((int) i == hoveredNote)
        {
            g.setColour (juce::Colours::white);
            g.strokePath (diamond, juce::PathStrokeType (1.6f));
        }
    }
}

void GridEditor::drawPlayhead (juce::Graphics& g) const
{
    if (! playheadVisible)
        return;

    const auto x = timeline.xForPpq (playheadPpq);

    if (x < 0.0f || x > (float) getWidth())
        return;

    g.setColour (theme::playhead.withAlpha (0.9f));
    g.drawVerticalLine ((int) std::round (x), 0.0f, (float) getHeight());
}

void GridEditor::drawHoverInfo (juce::Graphics& g) const
{
    if (hoveredNote < 0 || humanizedClip == nullptr
        || ! juce::isPositiveAndBelow (hoveredNote, (int) humanizedClip->notes.size()))
        return;

    const auto& note = humanizedClip->notes[(size_t) hoveredNote];
    const auto row = lanes.indexOfNote (note.noteNumber);
    const auto label = row >= 0 ? lanes.getRows()[(size_t) row].label : drumName (note.noteNumber);

    juce::StringArray lines;
    lines.add (label + "  (" + juce::String (note.noteNumber) + ")");
    lines.add ("Velocity  " + juce::String (note.velocity));

    if (const auto* original = sourceNoteFor (note))
    {
        const auto shiftPpq = note.startPpq - original->startPpq;
        const auto shiftMs = shiftPpq * 60000.0 / referenceBpm;
        const auto shiftTicks = shiftPpq * 960.0;

        lines.add ("Shift  " + juce::String (shiftMs, 1) + " ms  ("
                   + juce::String (shiftTicks >= 0.0 ? "+" : "") + juce::String (juce::roundToInt (shiftTicks)) + " ticks)");
        lines.add ("Velocity delta  " + juce::String (note.velocity - original->velocity));
    }
    else if (note.isGhost)
    {
        lines.add ("Generated ghost note");
    }

    const auto lineHeight = 15;
    const auto boxWidth = 178;
    const auto boxHeight = lineHeight * lines.size() + 8;

    auto box = juce::Rectangle<int> ((int) hoverPosition.x + 14, (int) hoverPosition.y + 12,
                                     boxWidth, boxHeight);

    if (box.getRight() > getWidth())
        box.setX ((int) hoverPosition.x - boxWidth - 14);

    if (box.getBottom() > getHeight())
        box.setY (juce::jmax (0, getHeight() - boxHeight));

    g.setColour (theme::panelBg.withAlpha (0.96f));
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (theme::panelBorder);
    g.drawRoundedRectangle (box.toFloat(), 3.0f, 1.0f);

    g.setFont (theme::labelFont (11.5f));

    for (int i = 0; i < lines.size(); ++i)
    {
        g.setColour (i == 0 ? theme::textPrimary : theme::textSecondary);
        g.drawText (lines[i], box.getX() + 7, box.getY() + 4 + i * lineHeight,
                    box.getWidth() - 14, lineHeight, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
int GridEditor::noteIndexAt (juce::Point<float> position) const
{
    if (humanizedClip == nullptr)
        return -1;

    const auto halfSize = diamondHalfSize();
    const auto halfWidth = halfSize.x;
    const auto row = lanes.rowAtY (position.y);

    if (row < 0)
        return -1;

    const auto note = lanes.getRows()[(size_t) row].note;
    auto best = -1;
    auto bestDistance = halfWidth + 1.0f;

    for (size_t i = 0; i < humanizedClip->notes.size(); ++i)
    {
        if (humanizedClip->notes[i].noteNumber != note)
            continue;

        const auto distance = std::abs (timeline.xForPpq (humanizedClip->notes[i].startPpq) - position.x);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = (int) i;
        }
    }

    return best;
}

void GridEditor::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto& modifiers = event.mods;

    if (modifiers.isCommandDown())
    {
        const auto factor = std::pow (1.35, (double) (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * 3.0);
        timeline.zoomAbout (factor, (float) event.x, getWidth());
    }
    else if (modifiers.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        timeline.setScroll (timeline.scrollBeats - (double) delta * timeline.visibleBeats (getWidth()) * 0.5,
                            getWidth());
    }
    else
    {
        lanes.setScroll (lanes.scrollY - (double) wheel.deltaY * lanes.rowHeight * 4.0, getHeight());
    }

    if (onViewChanged != nullptr)
        onViewChanged();
}

void GridEditor::mouseDown (const juce::MouseEvent& event)
{
    dragStart = event.getPosition();
    dragStartScrollBeats = timeline.scrollBeats;
    dragStartScrollY = lanes.scrollY;
    panning = false;
}

void GridEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (! panning && event.getDistanceFromDragStart() < 3)
        return;

    if (! panning)
    {
        panning = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    const auto deltaX = (float) (event.x - dragStart.x);
    const auto deltaY = (float) (event.y - dragStart.y);

    timeline.scrollBeats = dragStartScrollBeats - (double) deltaX / timeline.pixelsPerBeat;
    timeline.clampScroll (getWidth());
    lanes.scrollY = dragStartScrollY - (double) deltaY;
    lanes.clampScroll (getHeight());

    timeline.sendChangeMessage();
    lanes.sendChangeMessage();

    if (onViewChanged != nullptr)
        onViewChanged();
}

void GridEditor::mouseUp (const juce::MouseEvent&)
{
    if (! panning)
        return;

    panning = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void GridEditor::mouseMove (const juce::MouseEvent& event)
{
    hoverPosition = event.position;
    const auto found = noteIndexAt (event.position);

    if (found == hoveredNote)
        return;

    hoveredNote = found;
    repaint();
}

void GridEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoveredNote < 0)
        return;

    hoveredNote = -1;
    repaint();
}

} // namespace mh
