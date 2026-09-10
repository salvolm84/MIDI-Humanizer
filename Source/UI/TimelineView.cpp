#include "TimelineView.h"

#include <cmath>

namespace mh
{

void TimelineView::clampScroll (int width)
{
    const auto maxScroll = juce::jmax (0.0, contentBeats() - visibleBeats (width));
    scrollBeats = juce::jlimit (0.0, maxScroll, scrollBeats);
}

void TimelineView::setScroll (double beats, int width)
{
    scrollBeats = beats;
    clampScroll (width);
    sendChangeMessage();
}

void TimelineView::zoomAbout (double factor, float anchorX, int width)
{
    const auto anchorPpq = ppqForX (anchorX);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
    scrollBeats = anchorPpq - (double) anchorX / pixelsPerBeat;
    clampScroll (width);
    sendChangeMessage();
}

void TimelineView::zoomToFit (int width)
{
    const auto beats = juce::jmax (barLengthPpq(), lengthPpq);

    if (width > 0 && beats > 0.0)
        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, (double) width / beats);

    scrollBeats = 0.0;
    sendChangeMessage();
}

void TimelineView::setGridDivision (int index)
{
    const auto clamped = juce::jlimit (0, numGridDivisions() - 1, index);

    if (clamped == gridDivisionIndex)
        return;

    gridDivisionIndex = clamped;
    sendChangeMessage();
}

void TimelineView::setLength (double newLengthPpq, int numerator, int denominator)
{
    const auto changed = std::abs (newLengthPpq - lengthPpq) > 1.0e-9
                      || numerator != timeSigNumerator
                      || denominator != timeSigDenominator;

    if (! changed)
        return;

    lengthPpq = juce::jmax (0.25, newLengthPpq);
    timeSigNumerator = juce::jmax (1, numerator);
    timeSigDenominator = juce::jmax (1, denominator);
    sendChangeMessage();
}

//==============================================================================
TimeRuler::TimeRuler (TimelineView& view)
    : timeline (view)
{
    timeline.addChangeListener (this);
    setInterceptsMouseClicks (false, false);
}

TimeRuler::~TimeRuler()
{
    timeline.removeChangeListener (this);
}

void TimeRuler::setPlayhead (double ppq, bool playing)
{
    if (std::abs (ppq - playheadPpq) < 1.0e-6 && playing == playheadVisible)
        return;

    playheadPpq = ppq;
    playheadVisible = playing;
    repaint();
}

void TimeRuler::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (theme::headerBg);

    const auto bar = timeline.barLengthPpq();
    const auto firstBarIndex = (int) std::floor (timeline.scrollBeats / bar);
    const auto lastPpq = timeline.ppqForX ((float) bounds.getWidth());

    g.setFont (theme::labelFont (11.0f));

    // Beat ticks are only useful once there is room for them.
    const auto drawBeats = timeline.pixelsPerBeat > 26.0;

    for (int barIndex = juce::jmax (0, firstBarIndex); ; ++barIndex)
    {
        const auto barPpq = (double) barIndex * bar;

        if (barPpq > lastPpq)
            break;

        const auto x = timeline.xForPpq (barPpq);

        g.setColour (theme::textSecondary);
        g.drawVerticalLine ((int) std::round (x), 4.0f, (float) bounds.getHeight());

        g.setColour (theme::textPrimary);
        g.drawText (juce::String (barIndex + 1), (int) x + 4, 1, 44, bounds.getHeight() - 2,
                    juce::Justification::centredLeft, false);

        if (! drawBeats)
            continue;

        for (int beat = 1; beat < timeline.timeSigNumerator; ++beat)
        {
            const auto beatPpq = barPpq + (double) beat * 4.0 / (double) timeline.timeSigDenominator;
            const auto beatX = timeline.xForPpq (beatPpq);

            g.setColour (theme::textDim);
            g.drawVerticalLine ((int) std::round (beatX),
                                (float) bounds.getHeight() * 0.55f, (float) bounds.getHeight());
        }
    }

    g.setColour (theme::panelBorder);
    g.drawHorizontalLine (bounds.getBottom() - 1, 0.0f, (float) bounds.getWidth());

    if (playheadVisible)
    {
        const auto x = timeline.xForPpq (playheadPpq);

        if (bounds.toFloat().expanded (2.0f, 0.0f).contains (x, 1.0f))
        {
            juce::Path marker;
            marker.addTriangle (x - 5.0f, 1.0f, x + 5.0f, 1.0f, x, 9.0f);
            g.setColour (theme::playhead);
            g.fillPath (marker);
            g.drawVerticalLine ((int) std::round (x), 1.0f, (float) bounds.getHeight());
        }
    }
}

} // namespace mh
