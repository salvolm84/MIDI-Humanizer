#pragma once

#include "LaneHeader.h"
#include "TimelineView.h"

#include <functional>
#include <map>

namespace mh
{

/** Velocity viewer under the grid.

    Stems are drawn at each humanized note's position, coloured with the same velocity
    ramp as the diamonds above so the two views read as one picture. Like the grid it is
    read-only; the source velocity is shown as a faint tick so the change is visible.
*/
class VelocityLane final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::ChangeListener
{
public:
    VelocityLane (TimelineView& timeline, LaneModel& lanes);
    ~VelocityLane() override;

    void setClips (const NoteSequence* source, const NoteSequence* humanized);
    void setShowSource (bool shouldShow);
    void setColourMode (theme::NoteColourMode mode);
    void setPlayhead (double ppq, bool playing);

    /** Restricts the view to these note numbers; an empty list shows every lane.

        Selecting a drum narrows the lane to it, which is the only way to read one drum's
        dynamics when several share the same stretch of bar.
    */
    void setLaneFilter (std::vector<int> notes);

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void()> onViewChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    const Note* sourceNoteFor (const Note& humanizedNote) const;

    TimelineView& timeline;
    LaneModel& lanes;

    const NoteSequence* sourceClip = nullptr;
    const NoteSequence* humanizedClip = nullptr;
    std::map<int, int> sourceIndexById;

    bool showSource = true;
    std::vector<int> laneFilter;
    theme::NoteColourMode colourMode = theme::NoteColourMode::byLane;
    double playheadPpq = 0.0;
    bool playheadVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityLane)
};

} // namespace mh
