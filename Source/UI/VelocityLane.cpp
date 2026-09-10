// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "VelocityLane.h"

#include <algorithm>
#include <cmath>

namespace mh
{

VelocityLane::VelocityLane (TimelineView& timelineToUse, LaneModel& lanesToUse)
    : timeline (timelineToUse), lanes (lanesToUse)
{
    timeline.addChangeListener (this);
}

VelocityLane::~VelocityLane()
{
    timeline.removeChangeListener (this);
}

void VelocityLane::setClips (const NoteSequence* source, const NoteSequence* humanized)
{
    sourceClip = source;
    humanizedClip = humanized;

    sourceIndexById.clear();

    if (sourceClip != nullptr)
        for (size_t i = 0; i < sourceClip->notes.size(); ++i)
            sourceIndexById[sourceClip->notes[i].sourceId] = (int) i;

    repaint();
}

void VelocityLane::setShowSource (bool shouldShow)
{
    if (showSource == shouldShow)
        return;

    showSource = shouldShow;
    repaint();
}

void VelocityLane::setPlayhead (double ppq, bool playing)
{
    if (std::abs (ppq - playheadPpq) < 1.0e-6 && playing == playheadVisible)
        return;

    playheadPpq = ppq;
    playheadVisible = playing;
    repaint();
}

void VelocityLane::setColourMode (theme::NoteColourMode mode)
{
    if (colourMode == mode)
        return;

    colourMode = mode;
    repaint();
}

void VelocityLane::setLaneFilter (std::vector<int> notes)
{
    if (laneFilter == notes)
        return;

    laneFilter = std::move (notes);
    repaint();
}

const Note* VelocityLane::sourceNoteFor (const Note& humanizedNote) const
{
    if (sourceClip == nullptr || humanizedNote.isGhost)
        return nullptr;

    const auto found = sourceIndexById.find (humanizedNote.sourceId);

    if (found == sourceIndexById.end())
        return nullptr;

    return &sourceClip->notes[(size_t) found->second];
}

void VelocityLane::paint (juce::Graphics& g)
{
    g.fillAll (theme::velLaneBg);

    const auto width = (float) getWidth();
    const auto height = (float) getHeight();
    const auto baseline = height - 1.0f;
    const auto usableHeight = height - 8.0f;

    // Reference lines at 32 / 64 / 96 give the eye something to judge depth against.
    g.setColour (theme::velLaneLine);

    for (auto reference : { 32, 64, 96 })
    {
        const auto y = baseline - usableHeight * (float) reference / 127.0f;
        g.drawHorizontalLine ((int) std::round (y), 0.0f, width);
    }

    // Bar lines, so the velocity lane stays legible on its own.
    const auto bar = timeline.barLengthPpq();
    const auto lastPpq = timeline.ppqForX (width);
    g.setColour (theme::barLine.withAlpha (0.55f));

    for (auto index = (juce::int64) juce::jmax (0.0, std::floor (timeline.scrollBeats / bar)); ; ++index)
    {
        const auto ppq = (double) index * bar;

        if (ppq > lastPpq)
            break;

        g.drawVerticalLine ((int) std::round (timeline.xForPpq (ppq)), 0.0f, height);
    }

    if (humanizedClip != nullptr)
    {
        for (auto& note : humanizedClip->notes)
        {
            if (! laneFilter.empty()
                && std::find (laneFilter.begin(), laneFilter.end(), note.noteNumber) == laneFilter.end())
                continue;

            const auto x = timeline.xForPpq (note.startPpq);

            if (x < -6.0f || x > width + 6.0f)
                continue;

            const auto stemHeight = usableHeight * (float) note.velocity / 127.0f;
            const auto top = baseline - stemHeight;
            const auto row = lanes.indexOfNote (note.noteNumber);
            const auto groupId = row >= 0 ? lanes.getRows()[(size_t) row].groupId : 0;
            const auto colour = theme::noteColour (note.noteNumber, note.velocity, colourMode, groupId);

            if (showSource)
            {
                if (const auto* original = sourceNoteFor (note))
                {
                    const auto sourceY = baseline - usableHeight * (float) original->velocity / 127.0f;
                    g.setColour (theme::sourceMarker);
                    g.drawLine (x - 4.0f, sourceY, x + 4.0f, sourceY, 1.0f);
                }
            }

            g.setColour (! note.audible ? colour.withAlpha (0.25f)
                                        : (note.isGhost ? colour.withAlpha (0.55f) : colour));
            g.fillRect (x - 1.0f, top, 2.5f, stemHeight);

            // Cubase caps each velocity stem with a small handle.
            g.fillRect (x - 3.0f, top - 1.5f, 6.5f, 3.0f);
        }
    }

    if (playheadVisible)
    {
        const auto x = timeline.xForPpq (playheadPpq);

        if (x >= 0.0f && x <= width)
        {
            g.setColour (theme::playhead.withAlpha (0.9f));
            g.drawVerticalLine ((int) std::round (x), 0.0f, height);
        }
    }

    g.setColour (theme::panelBorder);
    g.drawHorizontalLine (0, 0.0f, width);
}

void VelocityLane::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
        timeline.zoomAbout (std::pow (1.35, (double) wheel.deltaY * 3.0), (float) event.x, getWidth());
    else
        timeline.setScroll (timeline.scrollBeats - (double) wheel.deltaY * timeline.visibleBeats (getWidth()) * 0.5,
                            getWidth());

    if (onViewChanged != nullptr)
        onViewChanged();
}

} // namespace mh
