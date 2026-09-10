#pragma once

#include "PluginProcessor.h"
#include "UI/ControlPanel.h"
#include "UI/GridEditor.h"
#include "UI/GridToolbar.h"
#include "UI/VelocityLane.h"
#include "UI/Widgets.h"

namespace mh
{

/** Drag handle that exports the humanized clip as a temporary .mid and starts a system
    file drag, so the result can be dropped straight back onto a DAW track.
*/
class DragOutHandle final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    DragOutHandle();

    /** Returns the file to drag, or an invalid File to cancel. */
    std::function<juce::File()> createFileToDrag;

    void setEnabledLook (bool shouldBeEnabled);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    bool hovered = false;
    bool enabledLook = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragOutHandle)
};

//==============================================================================
/** Drop target for a reference performance to learn a groove from.

    A dedicated zone rather than another whole-window drop, because a dropped MIDI file is
    already unambiguous elsewhere in this editor: it becomes the clip. There is no way to
    tell "play this" from "measure this" apart from where it lands, so the two meanings get
    two places. JUCE resolves a file drag to the deepest interested component and walks
    upward from there, so this intercepts before the editor's clip handling.
*/
class GrooveDropZone final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::SettableTooltipClient
{
public:
    GrooveDropZone();

    std::function<void (const juce::File&)> onFileDropped;
    std::function<void()> onClicked;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    bool hovered = false;
    bool dragActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveDropZone)
};

//==============================================================================
class MidiHumanizerEditor final : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  public juce::TextDragAndDropTarget,
                                  private juce::ChangeListener,
                                  private juce::ScrollBar::Listener,
                                  private juce::Timer
{
public:
    explicit MidiHumanizerEditor (MidiHumanizerProcessor&);
    ~MidiHumanizerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Points the control panel at one drum lane, or at the master values when passed a
        negative note number. Same effect as clicking the lane in the name list.
    */
    void selectLane (int noteNumber);

    /** Selects several lanes at once, which the control panel then edits as a unit. */
    void selectLanes (std::vector<int> noteNumbers);

    // ---- drag and drop from the host or Finder ----------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInTextDrag (const juce::String& text) override;
    void textDragEnter (const juce::String& text, int x, int y) override;
    void textDragExit (const juce::String& text) override;
    void textDropped (const juce::String& text, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;

    void buildHeader();
    void wireToolbar();
    void refreshFromProcessor();
    void rebuildLanes();
    void updateScrollBars();
    void updateClipInfo();
    void setStatus (const juce::String& message, bool isError = false);
    void updateHostStatus();
    void setTipsEnabled (bool shouldShow);
    void buildPresetBar();
    void refreshPresetList();
    void refreshAbAndUndo();
    void promptForPresetName();
    void chooseGrooveReferenceFile();
    void learnGrooveFrom (const juce::File& file);
    void applyColourMode();
    void applyVelocityFilter();

    void chooseMidiFile();
    void chooseDrumMapFile();
    void handleDroppedPaths (const juce::StringArray& paths);

    int    uiInt (const char* key, int fallback) const;
    bool   uiBool (const char* key, bool fallback) const;
    double uiDouble (const char* key, double fallback) const;
    void   setUi (const char* key, const juce::var& value);

    juce::Rectangle<int> gridArea() const { return gridBounds; }

    MidiHumanizerProcessor& plugin;
    theme::CubaseDarkLookAndFeel lookAndFeel;

    TimelineView timeline;
    LaneModel lanes;

    TimeRuler ruler { timeline };
    LaneHeader laneHeader { lanes };
    GridEditor grid { timeline, lanes };
    VelocityLane velocityLane { timeline, lanes };

    GridToolbar toolbar;
    ControlPanel controls;

    juce::Label brandLabel, clipInfoLabel, statusLabel, hostStatusLabel, velocityCaption, cornerCaption;
    juce::TextButton loadButton { "Load MIDI..." }, clearButton { "Clear" }, rerollButton { "Re-roll" };
    DragOutHandle dragOutHandle;
    GrooveDropZone grooveDropZone;

    std::unique_ptr<ToggleControl> humanizeToggle, auditionToggle, loopToggle, thruToggle, testNoteToggle, mapOutToggle;
    std::unique_ptr<MiniSlider> seedSlider, tempoSlider, transposeSlider;

    // Presets, A/B and undo
    juce::Label presetLabel, abLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save" }, deletePresetButton { "Delete" },

                     abAButton { "A" }, abBButton { "B" }, abCopyButton { "Copy to other" },
                     undoButton { "Undo" }, redoButton { "Redo" };

    juce::Label outChannelLabel;
    juce::ComboBox outChannelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outChannelAttachment;

    juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

    /** Held only while tooltips are switched on: destroying the window is the reliable
        way to turn them off, since JUCE shows them for any component under the mouse
        that carries tooltip text.
    */
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AlertWindow> presetNameWindow;
    bool updatingPresetBox = false;

    juce::Rectangle<int> gridBounds;
    bool updatingScrollBars = false;
    bool statusIsError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiHumanizerEditor)
};

} // namespace mh
