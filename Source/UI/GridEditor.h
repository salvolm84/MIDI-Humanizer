#pragma once

#include "LaneHeader.h"
#include "TimelineView.h"

#include <functional>
#include <map>

namespace mh
{

/** The drum grid.

    Deliberately read-only: it is a reference view of what the humanizer produced, not an
    editor. Notes are drawn as Cubase-style diamonds because a drum hit's length carries
    no useful information, and each diamond is tinted by velocity.

    With "show source" on, the original position of every note is drawn as a hollow
    marker joined to the humanized one, which makes the timing shift directly readable.
*/
class GridEditor final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::ChangeListener
{
public:
    GridEditor (TimelineView& timeline, LaneModel& lanes);
    ~GridEditor() override;

    /** The clips are owned by the processor, which outlives the editor. */
    void setClips (const NoteSequence* source, const NoteSequence* humanized);

    void setShowSource (bool shouldShow);
    void setColourMode (theme::NoteColourMode mode);

    /** Tempo used to report timing shifts in milliseconds in the hover read-out. */
    void setReferenceBpm (double bpm);
    void setPlayhead (double ppq, bool playing);
    void setDropActive (bool active);

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Called whenever the view scrolls or zooms, so the host editor can update its
        scrollbars without the grid needing to know they exist.
    */
    std::function<void()> onViewChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    void drawBackground (juce::Graphics&) const;
    void drawGridLines (juce::Graphics&) const;
    void drawNotes (juce::Graphics&) const;
    void drawPlayhead (juce::Graphics&) const;
    void drawHoverInfo (juce::Graphics&) const;

    /** Half-width and half-height of a note diamond, in pixels.

        Drawing and hit-testing both go through this, so a note can never be drawn at one
        size and clicked at another.
    */
    juce::Point<float> diamondHalfSize() const;

    static void addDiamond (juce::Path&, float centreX, float centreY, float halfWidth, float halfHeight);

    /** Index into the humanized clip's notes, or -1. */
    int noteIndexAt (juce::Point<float> position) const;

    const Note* sourceNoteFor (const Note& humanizedNote) const;

    TimelineView& timeline;
    LaneModel& lanes;

    const NoteSequence* sourceClip = nullptr;
    const NoteSequence* humanizedClip = nullptr;

    /** sourceId to index in `sourceClip->notes`, rebuilt whenever the clips change. */
    std::map<int, int> sourceIndexById;

    bool showSource = true;
    bool dropActive = false;
    theme::NoteColourMode colourMode = theme::NoteColourMode::byLane;
    double referenceBpm = 120.0;

    double playheadPpq = 0.0;
    bool   playheadVisible = false;

    int hoveredNote = -1;
    juce::Point<float> hoverPosition;

    juce::Point<int> dragStart;
    double dragStartScrollBeats = 0.0;
    double dragStartScrollY = 0.0;
    bool   panning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridEditor)
};

} // namespace mh
