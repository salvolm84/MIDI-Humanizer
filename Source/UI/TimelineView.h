// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Model/GridDivision.h"
#include "Theme.h"

namespace mh
{

/** The shared horizontal transform.

    The ruler, the drum grid and the velocity lane all convert between quarter notes and
    pixels through this one object, so they cannot drift out of alignment when the user
    zooms or scrolls.
*/
class TimelineView final : public juce::ChangeBroadcaster
{
public:
    TimelineView() = default;

    static constexpr double minPixelsPerBeat = 12.0;
    static constexpr double maxPixelsPerBeat = 900.0;

    float  xForPpq (double ppq) const noexcept   { return (float) ((ppq - scrollBeats) * pixelsPerBeat); }
    double ppqForX (float x)    const noexcept   { return scrollBeats + (double) x / pixelsPerBeat; }

    double barLengthPpq() const noexcept
    {
        return 4.0 * (double) juce::jmax (1, timeSigNumerator) / (double) juce::jmax (1, timeSigDenominator);
    }

    double stepPpq() const noexcept              { return gridDivision (gridDivisionIndex).ppq; }
    double visibleBeats (int width) const noexcept { return (double) width / pixelsPerBeat; }

    /** Total scrollable extent: the clip plus a bar of run-out so the last hit is not
        jammed against the right edge.
    */
    double contentBeats() const noexcept         { return juce::jmax (barLengthPpq(), lengthPpq) + barLengthPpq(); }

    void setScroll (double beats, int width);
    void clampScroll (int width);

    /** Zooms about a pixel position, keeping the quarter note under the cursor put. */
    void zoomAbout (double factor, float anchorX, int width);

    void zoomToFit (int width);

    void setGridDivision (int index);
    void setLength (double newLengthPpq, int numerator, int denominator);

    double pixelsPerBeat = 96.0;
    double scrollBeats   = 0.0;
    double lengthPpq     = 4.0;
    int    gridDivisionIndex   = defaultGridDivisionIndex;
    int    timeSigNumerator    = 4;
    int    timeSigDenominator  = 4;
};

//==============================================================================
/** Bar and beat ruler above the grid, with the playhead marker. */
class TimeRuler final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit TimeRuler (TimelineView& view);
    ~TimeRuler() override;

    void paint (juce::Graphics&) override;

    void setPlayhead (double ppq, bool playing);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    TimelineView& timeline;
    double playheadPpq = 0.0;
    bool   playheadVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeRuler)
};

} // namespace mh
