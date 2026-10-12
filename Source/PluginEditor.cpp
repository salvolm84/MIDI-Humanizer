// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "PluginEditor.h"
#include "ParamIDs.h"
#include "UI/PromisedFileDrop.h"

#include <cmath>

namespace mh
{

namespace
{
    constexpr int headerTopHeight    = 36;
    constexpr int headerBottomHeight = 34;
    constexpr int presetRowHeight    = 38;
    constexpr int rulerHeight        = 20;
    constexpr int laneHeaderWidth    = 252;
    constexpr int velocityHeight     = 96;
    constexpr int scrollBarThickness = 12;
    constexpr int statusHeight       = 20;

    /** The plugin version, which only the plugin targets define; the console tools that
        link this file for testing do not.
    */
    juce::String versionString()
    {
       #if defined (JucePlugin_VersionString)
        return JucePlugin_VersionString;
       #else
        return "dev";
       #endif
    }

    bool isMidiPath (const juce::File& file)
    {
        return file.hasFileExtension ("mid;midi;smf;rmi");
    }

    bool isDrumMapPath (const juce::File& file)
    {
        return file.hasFileExtension ("drm;txt;xml");
    }
}

//==============================================================================
DragOutHandle::DragOutHandle()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DragOutHandle::setEnabledLook (bool shouldBeEnabled)
{
    if (enabledLook == shouldBeEnabled)
        return;

    enabledLook = shouldBeEnabled;
    repaint();
}

void DragOutHandle::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto active = enabledLook;

    g.setColour (active ? (hovered ? theme::controlBgOver : theme::controlBg)
                        : theme::controlBg.darker (0.25f));
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (active && hovered ? theme::accent : theme::panelBorder);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

    g.setColour (active ? theme::textPrimary : theme::textDim);
    g.setFont (theme::labelFont (12.0f));
    g.drawText ("Drag out MIDI", getLocalBounds(), juce::Justification::centred, false);
}

void DragOutHandle::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void DragOutHandle::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void DragOutHandle::mouseDrag (const juce::MouseEvent&)
{
    if (dragging || createFileToDrag == nullptr)
        return;

    const auto file = createFileToDrag();

    if (! file.existsAsFile())
        return;

    dragging = true;

    // canMoveFiles stays false: the temp file is ours, the host must copy it.
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() }, false, this, [this] { dragging = false; });
}

//==============================================================================
GrooveDropZone::GrooveDropZone()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool GrooveDropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    // Only MIDI. Anything else is left to fall through to the editor, which knows what to
    // do with a dropped drum map.
    for (auto& path : files)
        if (juce::File (path).hasFileExtension ("mid;midi;smf;rmi"))
            return true;

    return false;
}

void GrooveDropZone::fileDragEnter (const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void GrooveDropZone::fileDragExit (const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void GrooveDropZone::filesDropped (const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();

    for (auto& path : files)
    {
        const juce::File file (path.unquoted().trim());

        if (file.existsAsFile() && file.hasFileExtension ("mid;midi;smf;rmi"))
        {
            if (onFileDropped != nullptr)
                onFileDropped (file);

            return;
        }
    }
}

void GrooveDropZone::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void GrooveDropZone::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void GrooveDropZone::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClicked != nullptr)
        onClicked();
}

void GrooveDropZone::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (dragActive ? theme::accentDim.withAlpha (0.55f)
                            : (hovered ? theme::controlBgOver : theme::controlBg));
    g.fillRoundedRectangle (bounds, 3.0f);

    // A dashed edge says "drop here" without needing a caption to explain it.
    juce::Path outline;
    outline.addRoundedRectangle (bounds, 3.0f);

    const float dashes[] = { 4.0f, 3.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes,
                                                    juce::numElementsInArray (dashes));

    g.setColour (dragActive ? theme::accent : theme::textDim);
    g.fillPath (dashed);

    auto text = getLocalBounds().reduced (6, 3);

    g.setColour (dragActive ? juce::Colours::white : theme::textPrimary);
    g.setFont (theme::labelFont (11.0f, true));
    g.drawText ("LEARN GROOVE", text.removeFromTop (13), juce::Justification::centred, false);

    g.setColour (dragActive ? juce::Colours::white : theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText (dragActive ? "release to measure it" : "drop a played take, or click",
                text, juce::Justification::centred, false);
}

//==============================================================================
MidiHumanizerEditor::MidiHumanizerEditor (MidiHumanizerProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      plugin (processorToUse),
      controls (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    buildHeader();
    buildPresetBar();

    addAndMakeVisible (toolbar);
    addAndMakeVisible (controls);
    addAndMakeVisible (ruler);
    addAndMakeVisible (laneHeader);
    addAndMakeVisible (grid);
    addAndMakeVisible (velocityLane);
    addAndMakeVisible (horizontalScroll);
    addAndMakeVisible (verticalScroll);

    horizontalScroll.addListener (this);
    verticalScroll.addListener (this);

    grid.setClips (&plugin.getSourceClip(), &plugin.getHumanizedClip());
    velocityLane.setClips (&plugin.getSourceClip(), &plugin.getHumanizedClip());

    grid.setTooltip ("Read-only view of the humanized result. Drag to pan, wheel to scroll, "
                     "Shift+wheel to scroll sideways, Cmd+wheel to zoom. Hover a note for its exact "
                     "timing shift and velocity change.");
    laneHeader.setTooltip ("Click a drum to give it its own settings and to show only its velocities "
                           "below. Click the dot on the left to switch that drum's humanization on "
                           "and off.");
    velocityLane.setTooltip ("Velocity of each hit. Selecting a drum on the left narrows this to that "
                             "drum alone.");

    grid.onViewChanged         = [this] { updateScrollBars(); };
    velocityLane.onViewChanged = [this] { updateScrollBars(); };

    wireToolbar();

    // Selecting a lane in the name list points the control panel at that drum.
    laneHeader.onSelectionChanged = [this] (std::vector<int> notes) { selectLanes (std::move (notes)); };

    laneHeader.onLaneEnableToggled = [this] (int note)
    {
        plugin.setLaneEnabled (note, ! plugin.getLaneSettings (note).enabled);
    };

    laneHeader.onLaneMuteToggled = [this] (int note)
    {
        plugin.setLaneMuted (note, ! plugin.getLaneSettings (note).muted);
    };

    controls.onScopeChanged = [this] (std::vector<int> notes) { selectLanes (std::move (notes)); };
    controls.onLaneEdited = [this] { rebuildLanes(); };
    controls.onLinkChanged = [this] { rebuildLanes(); };

    // Restore the view options the processor is holding for us.
    timeline.gridDivisionIndex = uiInt ("gridDivision", defaultGridDivisionIndex);
    timeline.pixelsPerBeat     = uiDouble ("pixelsPerBeat", 96.0);
    lanes.rowHeight            = uiDouble ("laneHeight", 15.0);

    toolbar.setGridDivision (timeline.gridDivisionIndex);
    toolbar.setRowOrder (uiInt ("rowOrder", 0));
    toolbar.setRowVisibility (uiInt ("rowVisibility", 1));
    toolbar.setShowSource (uiBool ("showSource", true));
    toolbar.setFollowPlayhead (uiBool ("followPlayhead", true));
    toolbar.setRowHeight (lanes.rowHeight);

    grid.setShowSource (uiBool ("showSource", true));
    velocityLane.setShowSource (uiBool ("showSource", true));

    toolbar.setColourMode (uiInt ("colourMode", 0));
    toolbar.setTipsEnabled (uiBool ("tipsEnabled", true));
    applyColourMode();
    applyVelocityFilter();
    setTipsEnabled (uiBool ("tipsEnabled", true));

    plugin.addChangeListener (this);
    refreshFromProcessor();

    setWantsKeyboardFocus (true);

    setResizable (true, true);
    setResizeLimits (1120, 760, 3600, 2200);
    setSize (1320, 940);

    startTimerHz (30);
}

MidiHumanizerEditor::~MidiHumanizerEditor()
{
    stopTimer();
    plugin.removeChangeListener (this);
    horizontalScroll.removeListener (this);
    verticalScroll.removeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MidiHumanizerEditor::selectLane (int noteNumber)
{
    if (noteNumber < 0)
        selectLanes ({});
    else
        selectLanes ({ noteNumber });
}

void MidiHumanizerEditor::selectLanes (std::vector<int> noteNumbers)
{
    const auto primary = noteNumbers.empty() ? -1 : noteNumbers.back();

    lanes.setSelection (noteNumbers);
    controls.setScope (std::move (noteNumbers),
                       primary >= 0 ? plugin.getDrumMapping().labelFor (primary) : juce::String());
    applyVelocityFilter();
}

void MidiHumanizerEditor::applyVelocityFilter()
{
    // Selecting drums narrows the velocity lane to them, which is the only way to read
    // one drum's dynamics when several share the same stretch of bar.
    const auto& selection = lanes.getSelection();
    velocityLane.setLaneFilter (selection);

    juce::String caption ("VELOCITY  -  ");

    if (selection.empty())
        caption << "ALL LANES";
    else if (selection.size() == 1)
        caption << plugin.getDrumMapping().labelFor (selection.front()).toUpperCase();
    else
        caption << selection.size() << " DRUMS";

    velocityCaption.setText (caption, juce::dontSendNotification);
}

void MidiHumanizerEditor::applyColourMode()
{
    const auto mode = uiInt ("colourMode", 0) == 1 ? theme::NoteColourMode::byVelocity
                                                   : theme::NoteColourMode::byLane;
    grid.setColourMode (mode);
    velocityLane.setColourMode (mode);
}

void MidiHumanizerEditor::setTipsEnabled (bool shouldShow)
{
    if (shouldShow == (tooltipWindow != nullptr))
        return;

    if (shouldShow)
        tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 600);
    else
        tooltipWindow.reset();
}

void MidiHumanizerEditor::buildHeader()
{
    brandLabel.setText ("MIDI HUMANIZER", juce::dontSendNotification);
    brandLabel.setFont (theme::labelFont (15.0f, true));
    brandLabel.setColour (juce::Label::textColourId, theme::textPrimary);

    // The AGPL asks an interactive program to carry a copyright notice, a no-warranty
    // notice and a pointer to the licence. There is no menu bar in a plugin, so the
    // brand label holds it.
    brandLabel.setTooltip ("MIDI Humanizer " + versionString()
                           + "\nCopyright (c) 2026 Salvatore La Malfa"
                           + "\n\nFree software under the GNU AGPL v3 or later, and provided with "
                             "ABSOLUTELY NO WARRANTY."
                           + "\nSource and licence: https://github.com/salvolm84/MIDI-Humanizer"
                           + "\n\nBuilt with JUCE (AGPLv3) and the Steinberg VST3 SDK (MIT). "
                             "VST3 is a trademark of Steinberg Media Technologies GmbH.");
    addAndMakeVisible (brandLabel);

    clipInfoLabel.setFont (theme::labelFont (12.0f));
    clipInfoLabel.setColour (juce::Label::textColourId, theme::textSecondary);
    addAndMakeVisible (clipInfoLabel);

    statusLabel.setFont (theme::labelFont (11.5f));
    statusLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (statusLabel);

    // Live read-out of what the host is handing the plugin. In a DAW, silence has
    // several unrelated causes that look identical from the outside, so this says which
    // one you are looking at.
    hostStatusLabel.setFont (theme::labelFont (11.5f));
    hostStatusLabel.setColour (juce::Label::textColourId, theme::textDim);
    hostStatusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (hostStatusLabel);

    velocityCaption.setText ("VELOCITY", juce::dontSendNotification);
    velocityCaption.setFont (theme::labelFont (10.5f, true));
    velocityCaption.setColour (juce::Label::textColourId, theme::textSecondary);
    velocityCaption.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (velocityCaption);

    cornerCaption.setText ("DRUM MAP", juce::dontSendNotification);
    cornerCaption.setFont (theme::labelFont (10.0f, true));
    cornerCaption.setColour (juce::Label::textColourId, theme::textDim);
    cornerCaption.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (cornerCaption);

    loadButton.onClick = [this] { chooseMidiFile(); };
    loadButton.setTooltip ("Load a MIDI clip from disk. You can also drag a MIDI item straight from "
                           "your DAW or a .mid file from Finder onto the grid.");
    addAndMakeVisible (loadButton);

    clearButton.onClick = [this]
    {
        plugin.clearClip();
        setStatus ("Clip cleared. Drop a MIDI item onto the grid to load a new one.");
    };
    clearButton.setTooltip ("Remove the loaded clip.");
    addAndMakeVisible (clearButton);

    // Re-rolling the seed deals a completely new set of deviations, which is the quick
    // way to audition alternative takes at the same settings.
    rerollButton.onClick = [this]
    {
        if (auto* parameter = plugin.apvts.getParameter (pid::seed))
        {
            juce::Random random (juce::Time::getHighResolutionTicks());
            parameter->setValueNotifyingHost (random.nextFloat());
        }
    };
    rerollButton.setTooltip ("Deal a completely new set of random deviations at the same settings. "
                             "Everything else keeps the same performance, so this is how you audition "
                             "alternative takes.");
    addAndMakeVisible (rerollButton);

    dragOutHandle.createFileToDrag = [this]
    {
        const auto file = plugin.writeHumanizedClipToTempFile();

        if (! file.existsAsFile())
            setStatus ("Nothing to export yet.", true);

        return file;
    };
    dragOutHandle.setTooltip ("Drag from here onto a DAW track to drop the humanized clip in as real "
                              "MIDI notes. Use this to print the result instead of keeping the plugin "
                              "live.");
    addAndMakeVisible (dragOutHandle);

    humanizeToggle = std::make_unique<ToggleControl> (plugin.apvts, pid::enabled,  "Humanize");
    auditionToggle = std::make_unique<ToggleControl> (plugin.apvts, pid::audition, "Audition");
    loopToggle     = std::make_unique<ToggleControl> (plugin.apvts, pid::loopClip, "Loop");
    thruToggle     = std::make_unique<ToggleControl> (plugin.apvts, pid::midiThru, "Pass input thru");
    testNoteToggle = std::make_unique<ToggleControl> (plugin.apvts, pid::testNote, "Test note");
    mapOutToggle   = std::make_unique<ToggleControl> (plugin.apvts, pid::mapOutputNotes, "Map out notes");

    humanizeToggle->setHelp ("Master bypass. Off, the clip plays back exactly as programmed.");
    auditionToggle->setHelp ("Run an internal transport so the clip plays even when the host is "
                             "stopped, for dialling settings in. Uses the Tempo setting when the host "
                             "gives no tempo.");
    loopToggle->setHelp ("Loop the clip continuously. Off, it plays once from the start of the "
                         "timeline.");
    thruToggle->setHelp ("Also pass the incoming MIDI through unchanged, alongside the clip. Off by "
                         "default because this is a clip player, not an insert effect: it normally "
                         "replaces the track's MIDI rather than adding to it.");
    testNoteToggle->setHelp ("Diagnostic. Emits a repeating kick drum straight out, ignoring the clip, "
                             "the humanizer and the transport. If you cannot hear this, the problem is "
                             "MIDI routing rather than anything in the plugin.");

    mapOutToggle->setHelp ("Send the note each drum map row says to send, rather than the note the row "
                           "is displayed under. Only matters for maps whose output note differs from "
                           "its input note; for most maps the two are the same and this does nothing.");

    for (auto* toggle : { humanizeToggle.get(), auditionToggle.get(), loopToggle.get(),
                          thruToggle.get(), testNoteToggle.get(), mapOutToggle.get() })
        addAndMakeVisible (*toggle);

    seedSlider      = std::make_unique<MiniSlider> (plugin.apvts, pid::seed,        "Seed", 34);
    tempoSlider     = std::make_unique<MiniSlider> (plugin.apvts, pid::auditionBpm, "Tempo", 40);
    transposeSlider = std::make_unique<MiniSlider> (plugin.apvts, pid::transpose,   "Transp", 44);

    seedSlider->setHelp ("Which random performance you get. The same seed always produces exactly the "
                         "same result, so a saved project sounds the way you left it.");
    tempoSlider->setHelp ("Tempo used for Audition, and for converting the millisecond settings when "
                          "no host tempo is available.");
    transposeSlider->setHelp ("Shifts every output note by this many semitones. Useful when your drum "
                              "library's mapping differs from the clip's.");

    for (auto* slider : { seedSlider.get(), tempoSlider.get(), transposeSlider.get() })
        addAndMakeVisible (*slider);

    outChannelLabel.setText ("Out ch", juce::dontSendNotification);
    outChannelLabel.setFont (theme::labelFont (11.0f));
    outChannelLabel.setColour (juce::Label::textColourId, theme::textSecondary);
    outChannelLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (outChannelLabel);

    if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (
            plugin.apvts.getParameter (pid::outChannel)))
        outChannelBox.addItemList (choice->choices, 1);

    outChannelBox.setTooltip ("MIDI channel for the output. As-is keeps each note's original channel, "
                              "which for a drum clip is often channel 10. Set it explicitly if your "
                              "instrument only listens on one channel.");
    addAndMakeVisible (outChannelBox);
    outChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        plugin.apvts, pid::outChannel, outChannelBox);
}

void MidiHumanizerEditor::buildPresetBar()
{
    const auto caption = [] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (theme::labelFont (11.0f));
        label.setColour (juce::Label::textColourId, theme::textSecondary);
        label.setJustificationType (juce::Justification::centredRight);
    };

    caption (presetLabel, "Preset");
    caption (abLabel, "A/B");
    addAndMakeVisible (presetLabel);
    addAndMakeVisible (abLabel);

    presetBox.setTooltip ("A preset holds the master values and every per-lane override: the feel, "
                          "not the clip. Factory presets are listed first and double as worked "
                          "examples of what the controls do. Links, mutes and solos belong to the "
                          "session and are left alone by a preset.");
    presetBox.onChange = [this]
    {
        if (updatingPresetBox)
            return;

        const auto name = presetBox.getText();

        if (name.isEmpty())
            return;

        juce::String error;

        if (plugin.loadPreset (name, error))
            setStatus ("Loaded preset " + name);
        else
            setStatus (error, true);
    };
    addAndMakeVisible (presetBox);

    savePresetButton.onClick = [this] { promptForPresetName(); };
    savePresetButton.setTooltip ("Save the current settings as a named preset you can recall in any "
                                 "project.");
    addAndMakeVisible (savePresetButton);

    deletePresetButton.onClick = [this]
    {
        const auto name = presetBox.getText();
        juce::String error;

        if (plugin.deleteUserPreset (name, error))
        {
            setStatus ("Deleted preset " + name);
            refreshPresetList();
        }
        else
        {
            setStatus (error, true);
        }
    };
    deletePresetButton.setTooltip ("Delete the selected user preset. Factory presets cannot be "
                                   "deleted.");
    addAndMakeVisible (deletePresetButton);

    // A dropped MIDI file means "play this" everywhere else in the editor, so learning a
    // groove from one needs a place of its own.
    grooveDropZone.setComponentID ("grooveDrop");
    grooveDropZone.onClicked = [this] { chooseGrooveReferenceFile(); };
    grooveDropZone.onFileDropped = [this] (const juce::File& file) { learnGrooveFrom (file); };
    grooveDropZone.setTooltip ("Drop a played MIDI performance here, or click to browse, to measure "
                               "its feel and apply it. Each lane's average deviation from the grid "
                               "becomes its Bias and the spread of those deviations becomes its "
                               "Spread, so the settings are measured off a take that already feels "
                               "right rather than guessed at. Dropping a MIDI file anywhere else "
                               "loads it as the clip instead. Swing is not extracted; analyse "
                               "against a triplet grid if the reference swings.");
    addAndMakeVisible (grooveDropZone);

    abAButton.onClick = [this] { plugin.setAbSlot (0); refreshAbAndUndo(); };
    abBButton.onClick = [this] { plugin.setAbSlot (1); refreshAbAndUndo(); };
    abAButton.setTooltip ("Snapshot A. Switching stores what is on screen into the slot you are "
                          "leaving, so you can flip between two versions of a feel while deciding.");
    abBButton.setTooltip ("Snapshot B.");
    addAndMakeVisible (abAButton);
    addAndMakeVisible (abBButton);

    abCopyButton.onClick = [this] { plugin.copyAbSlotToOther(); setStatus ("Copied to the other slot."); };
    abCopyButton.setTooltip ("Copy the current settings into the other slot, so you can start "
                             "comparing from a shared point.");
    addAndMakeVisible (abCopyButton);

    undoButton.onClick = [this] { plugin.undo(); refreshAbAndUndo(); };
    redoButton.onClick = [this] { plugin.redo(); refreshAbAndUndo(); };
    undoButton.setTooltip ("Undo the last change to the settings (Cmd+Z). Covers per-lane values, "
                           "links, mute and solo, and preset loads. Does not cover loading a clip.");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z).");
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    refreshPresetList();
    refreshAbAndUndo();
}

void MidiHumanizerEditor::refreshPresetList()
{
    const juce::ScopedValueSetter<bool> guard (updatingPresetBox, true);

    presetBox.clear (juce::dontSendNotification);

    auto itemId = 1;
    auto addedFactoryHeader = false;
    auto addedUserHeader = false;

    for (auto& preset : plugin.getPresetList())
    {
        if (preset.isFactory && ! addedFactoryHeader)
        {
            presetBox.addSectionHeading ("Factory");
            addedFactoryHeader = true;
        }

        if (! preset.isFactory && ! addedUserHeader)
        {
            presetBox.addSectionHeading ("User");
            addedUserHeader = true;
        }

        presetBox.addItem (preset.name, itemId++);
    }

    const auto current = plugin.getCurrentPresetName();

    if (current.isNotEmpty())
        presetBox.setText (current, juce::dontSendNotification);
    else
        presetBox.setTextWhenNothingSelected ("(edited)");

    // Only the user's own presets can be removed.
    auto isUserPreset = false;

    for (auto& preset : plugin.getPresetList())
        if (! preset.isFactory && preset.name == current)
            isUserPreset = true;

    deletePresetButton.setEnabled (isUserPreset);
}

void MidiHumanizerEditor::refreshAbAndUndo()
{
    const auto slot = plugin.getAbSlot();
    abAButton.setToggleState (slot == 0, juce::dontSendNotification);
    abBButton.setToggleState (slot == 1, juce::dontSendNotification);
    undoButton.setEnabled (plugin.canUndo());
    redoButton.setEnabled (plugin.canRedo());
}

void MidiHumanizerEditor::promptForPresetName()
{
    presetNameWindow = std::make_unique<juce::AlertWindow> ("Save preset",
                                                            "Name this humanization setup:",
                                                            juce::MessageBoxIconType::NoIcon);
    presetNameWindow->addTextEditor ("name", plugin.getCurrentPresetName(), "Name:");
    presetNameWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetNameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    presetNameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (presetNameWindow == nullptr)
            return;

        const auto name = presetNameWindow->getTextEditorContents ("name").trim();
        presetNameWindow.reset();

        if (result == 0 || name.isEmpty())
            return;

        juce::String error;

        if (plugin.saveUserPreset (name, error))
        {
            setStatus ("Saved preset " + name);
            refreshPresetList();
        }
        else
        {
            setStatus (error, true);
        }
    }));
}

void MidiHumanizerEditor::learnGrooveFrom (const juce::File& file)
{
    juce::String error;
    const auto lanesLearned = plugin.learnGrooveFromFile (file, error);

    if (lanesLearned < 0)
        setStatus (error, true);
    else
        setStatus (plugin.getLastGrooveSummary());

    refreshPresetList();
    refreshAbAndUndo();
}

void MidiHumanizerEditor::chooseGrooveReferenceFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a played MIDI performance to learn from",
                                                       juce::File(), "*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        learnGrooveFrom (file);
    });
}

void MidiHumanizerEditor::wireToolbar()
{
    toolbar.onGridDivisionChanged = [this] (int index)
    {
        timeline.setGridDivision (index);
        setUi ("gridDivision", index);
    };

    toolbar.onRowOrderChanged = [this] (int index)
    {
        setUi ("rowOrder", index);
        rebuildLanes();
    };

    toolbar.onRowVisibilityChanged = [this] (int index)
    {
        setUi ("rowVisibility", index);
        rebuildLanes();
    };

    toolbar.onShowSourceChanged = [this] (bool shouldShow)
    {
        setUi ("showSource", shouldShow);
        grid.setShowSource (shouldShow);
        velocityLane.setShowSource (shouldShow);
    };

    toolbar.onFollowPlayheadChanged = [this] (bool shouldFollow)
    {
        setUi ("followPlayhead", shouldFollow);
    };

    toolbar.onRowHeightChanged = [this] (double height)
    {
        lanes.setRowHeight (height, gridBounds.getHeight());
        setUi ("laneHeight", lanes.rowHeight);
        updateScrollBars();
    };

    toolbar.onZoom = [this] (double factor)
    {
        timeline.zoomAbout (factor, (float) gridBounds.getWidth() * 0.5f, gridBounds.getWidth());
        setUi ("pixelsPerBeat", timeline.pixelsPerBeat);
        updateScrollBars();
    };

    toolbar.onZoomToFit = [this]
    {
        timeline.zoomToFit (gridBounds.getWidth());
        setUi ("pixelsPerBeat", timeline.pixelsPerBeat);
        updateScrollBars();
    };

    toolbar.onColourModeChanged = [this] (int index)
    {
        setUi ("colourMode", index);
        applyColourMode();
    };

    toolbar.onTipsEnabledChanged = [this] (bool enabled)
    {
        setUi ("tipsEnabled", enabled);
        setTipsEnabled (enabled);
    };

    toolbar.onLoadDrumMap = [this] { chooseDrumMapFile(); };

    toolbar.onClearDrumMap = [this]
    {
        plugin.clearDrumMap();
        setStatus ("Drum map cleared: showing General MIDI names.");
    };
}

//==============================================================================
void MidiHumanizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBg);

    const auto headerBottom = headerTopHeight + headerBottomHeight + presetRowHeight;

    g.setColour (theme::headerBg);
    g.fillRect (0, 0, getWidth(), headerBottom);

    g.setColour (theme::panelBorder);
    g.drawHorizontalLine (headerTopHeight, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerTopHeight + headerBottomHeight, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerBottom - 1, 0.0f, (float) getWidth());

    // The lane-name gutter beside the velocity lane and under the ruler.
    g.setColour (theme::panelBg);
    g.fillRect (0, gridBounds.getY() - rulerHeight, laneHeaderWidth, rulerHeight);
    g.fillRect (0, gridBounds.getBottom() + scrollBarThickness, laneHeaderWidth, velocityHeight);

    g.setColour (theme::panelBorder);
    g.drawVerticalLine (laneHeaderWidth - 1, (float) (gridBounds.getY() - rulerHeight),
                        (float) (gridBounds.getBottom() + scrollBarThickness + velocityHeight));
}

void MidiHumanizerEditor::resized()
{
    auto bounds = getLocalBounds();

    // ---- header, top row ---------------------------------------------------------
    {
        auto row = bounds.removeFromTop (headerTopHeight).reduced (8, 5);
        brandLabel.setBounds (row.removeFromLeft (150));

        dragOutHandle.setBounds (row.removeFromRight (112));
        row.removeFromRight (5);
        clearButton.setBounds (row.removeFromRight (58));
        row.removeFromRight (5);
        loadButton.setBounds (row.removeFromRight (94));
        row.removeFromRight (12);

        clipInfoLabel.setBounds (row);
    }

    // ---- header, second row ------------------------------------------------------
    {
        auto row = bounds.removeFromTop (headerBottomHeight).reduced (8, 7);

        const auto place = [&row] (juce::Component& component, int width, int gapAfter = 8)
        {
            component.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (gapAfter);
        };

        place (*humanizeToggle, 84, 4);
        place (*auditionToggle, 82, 4);
        place (*loopToggle, 58, 4);
        place (*thruToggle, 118, 4);
        place (*testNoteToggle, 86, 4);
        place (*mapOutToggle, 118, 12);
        place (*seedSlider, 136, 4);
        place (rerollButton, 58, 12);
        place (*tempoSlider, 146, 12);

        outChannelBox.setBounds (row.removeFromRight (74));
        row.removeFromRight (4);
        outChannelLabel.setBounds (row.removeFromRight (44));
        row.removeFromRight (12);
        transposeSlider->setBounds (row.removeFromRight (juce::jmin (168, juce::jmax (0, row.getWidth()))));
    }

    // ---- presets, A/B and undo -----------------------------------------------------
    {
        auto row = bounds.removeFromTop (presetRowHeight).reduced (8, 4);

        const auto place = [&row] (juce::Component& component, int width, int gapAfter = 6)
        {
            component.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (gapAfter);
        };

        place (presetLabel, 40, 3);
        place (presetBox, 210, 5);
        place (savePresetButton, 50, 3);
        place (deletePresetButton, 58, 14);
        place (grooveDropZone, 176, 16);
        place (abLabel, 30, 4);
        place (abAButton, 34, 3);
        place (abBButton, 34, 4);
        place (abCopyButton, 100, 16);
        place (undoButton, 56, 3);
        place (redoButton, 56);
    }

    toolbar.setBounds (bounds.removeFromTop (GridToolbar::preferredHeight));
    controls.setBounds (bounds.removeFromBottom (ControlPanel::preferredHeight));
    {
        auto statusRow = bounds.removeFromBottom (statusHeight).reduced (10, 2);
        hostStatusLabel.setBounds (statusRow.removeFromRight (juce::jmin (420, statusRow.getWidth() / 2)));
        statusLabel.setBounds (statusRow);
    }

    // ---- velocity lane, then the horizontal scrollbar above it -------------------
    {
        auto velocityRow = bounds.removeFromBottom (velocityHeight);
        velocityCaption.setBounds (velocityRow.removeFromLeft (laneHeaderWidth).reduced (8, 4));
        velocityLane.setBounds (velocityRow.withTrimmedRight (scrollBarThickness));
    }

    {
        auto scrollRow = bounds.removeFromBottom (scrollBarThickness);
        scrollRow.removeFromLeft (laneHeaderWidth);
        horizontalScroll.setBounds (scrollRow.withTrimmedRight (scrollBarThickness));
    }

    // ---- ruler and grid ----------------------------------------------------------
    {
        auto rulerRow = bounds.removeFromTop (rulerHeight);
        cornerCaption.setBounds (rulerRow.removeFromLeft (laneHeaderWidth).reduced (8, 2));
        ruler.setBounds (rulerRow.withTrimmedRight (scrollBarThickness));
    }

    laneHeader.setBounds (bounds.removeFromLeft (laneHeaderWidth));
    verticalScroll.setBounds (bounds.removeFromRight (scrollBarThickness));
    grid.setBounds (bounds);
    gridBounds = bounds;

    timeline.clampScroll (gridBounds.getWidth());
    lanes.clampScroll (gridBounds.getHeight());
    updateScrollBars();

    // The hidden-row count is only known once the rows have been rebuilt, so the info
    // line has to follow it on every path, not just the full refresh.
    updateClipInfo();
}

//==============================================================================
void MidiHumanizerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void MidiHumanizerEditor::refreshFromProcessor()
{
    const auto& source = plugin.getSourceClip();
    const auto& humanized = plugin.getHumanizedClip();

    // The clip objects are stable members of the processor, but their contents change,
    // so the note-id lookups inside the views have to be rebuilt.
    grid.setClips (&source, &humanized);
    velocityLane.setClips (&source, &humanized);
    grid.setReferenceBpm (plugin.getReferenceBpm());

    timeline.setLength (juce::jmax (source.lengthPpq, humanized.lengthPpq),
                        source.timeSigNumerator, source.timeSigDenominator);

    rebuildLanes();

    toolbar.setDrumMapName (plugin.getDrumMapping().isEmpty() ? juce::String()
                                                                 : plugin.getDrumMapping().getName());
    applyVelocityFilter();
    updateScrollBars();

    dragOutHandle.setEnabledLook (! humanized.isEmpty());
    refreshPresetList();
    refreshAbAndUndo();
}

void MidiHumanizerEditor::rebuildLanes()
{
    const auto order = (LaneModel::Order) juce::jlimit (0, 2, uiInt ("rowOrder", 0));

    const auto visibility = (LaneModel::Visibility) juce::jlimit (0, 2, uiInt ("rowVisibility", 1));

    lanes.rebuild (plugin.getSourceClip(), plugin.getHumanizedClip(),
                   plugin.getDrumMapping(), visibility, order,
                   [this] (int note) { return plugin.getResolvedLaneSettings (note); });

    lanes.clampScroll (gridBounds.getHeight());
    updateScrollBars();
}

void MidiHumanizerEditor::updateClipInfo()
{
    const auto& source = plugin.getSourceClip();

    if (source.isEmpty())
    {
        clipInfoLabel.setText ("Drop a MIDI item from your DAW onto the grid, or use Load MIDI...",
                               juce::dontSendNotification);
        setStatus ("No clip loaded.");
        return;
    }

    const auto bars = source.lengthPpq / source.barLengthPpq();
    const auto& humanized = plugin.getHumanizedClip();

    clipInfoLabel.setText (source.name + "   |   " + juce::String (source.size()) + " notes   |   "
                               + juce::String (bars, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                               + " bars   |   " + juce::String (source.timeSigNumerator) + "/"
                               + juce::String (source.timeSigDenominator) + "   |   "
                               + juce::String (plugin.getReferenceBpm(), 1) + " BPM",
                           juce::dontSendNotification);

    const auto& report = plugin.getReport();
    juce::StringArray parts;
    parts.add (juce::String (humanized.size()) + " notes out");

    if (report.ghostsAdded > 0)
        parts.add (juce::String (report.ghostsAdded) + " ghosts added");

    if (report.flamsCreated > 0)
        parts.add (juce::String (report.flamsCreated) + " flams");

    if (report.notesDroppedByGap > 0)
        parts.add (juce::String (report.notesDroppedByGap) + " dropped (min gap)");

    if (report.tailsChoked > 0)
        parts.add (juce::String (report.tailsChoked) + " tails choked");

    if (report.peakTimingShiftMs > 0.01)
        parts.add ("peak shift " + juce::String (report.peakTimingShiftMs, 1) + " ms");

    // "Hide unlabelled" can hide a row that has notes on it. Saying so is what stops
    // that reading as notes having disappeared.
    if (const auto hidden = lanes.getHiddenUsedRowCount(); hidden > 0)
        parts.add (juce::String (hidden) + (hidden == 1 ? " row with notes hidden"
                                                        : " rows with notes hidden"));

    if (! statusIsError)
        setStatus (parts.joinIntoString ("   |   "));
}

void MidiHumanizerEditor::setStatus (const juce::String& message, bool isError)
{
    statusIsError = isError;
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? theme::warning : theme::textDim);
}

//==============================================================================
void MidiHumanizerEditor::updateScrollBars()
{
    if (updatingScrollBars || gridBounds.isEmpty())
        return;

    const juce::ScopedValueSetter<bool> guard (updatingScrollBars, true);

    horizontalScroll.setRangeLimits (0.0, juce::jmax (1.0, timeline.contentBeats()),
                                     juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollBeats,
                                      timeline.visibleBeats (gridBounds.getWidth()),
                                      juce::dontSendNotification);

    verticalScroll.setRangeLimits (0.0, juce::jmax (1.0, lanes.contentHeight()),
                                   juce::dontSendNotification);
    verticalScroll.setCurrentRange (lanes.scrollY, (double) gridBounds.getHeight(),
                                    juce::dontSendNotification);
}

void MidiHumanizerEditor::scrollBarMoved (juce::ScrollBar* scrollBarThatMoved, double newRangeStart)
{
    if (updatingScrollBars)
        return;

    if (scrollBarThatMoved == &horizontalScroll)
        timeline.setScroll (newRangeStart, gridBounds.getWidth());
    else if (scrollBarThatMoved == &verticalScroll)
        lanes.setScroll (newRangeStart, gridBounds.getHeight());
}

void MidiHumanizerEditor::updateHostStatus()
{
    const auto status = plugin.getHostStatus();

    juce::String text;
    auto colour = theme::textDim;

    if (status.blocksProcessed == 0)
    {
        // The host has never called processBlock, so nothing the plugin does can matter.
        // In Cubase this usually means the MIDI track has no part for the insert to
        // process; put an empty part over the bars you want to hear.
        text = "HOST: never called this plugin - check the track has a MIDI part";
        colour = theme::warning;
    }
    else if (! status.havePlayHead)
    {
        text = "HOST: no playhead - use Audition";
        colour = theme::warning;
    }
    else
    {
        switch (status.source)
        {
            case MidiHumanizerProcessor::TransportSource::hostSynced:
                text = "HOST: playing, synced";
                colour = theme::accent;
                break;

            case MidiHumanizerProcessor::TransportSource::hostInferred:
                text = "HOST: playing, position inferred";
                colour = theme::warning;
                break;

            case MidiHumanizerProcessor::TransportSource::audition:
                text = "AUDITION";
                colour = theme::accent;
                break;

            case MidiHumanizerProcessor::TransportSource::stopped:
                text = "HOST: stopped";
                break;
        }

        text << "  |  " << juce::String (status.bpm > 0.0 ? status.bpm : plugin.getReferenceBpm(), 1)
             << " BPM  |  " << juce::String (status.noteOnsEmitted) << " notes out";

        // The channel is the usual reason notes leave the plugin and are never heard.
        if (status.lastNoteNumber >= 0)
            text << " on ch " << juce::String (status.lastChannel)
                 << " (note " << juce::String (status.lastNoteNumber) << ")";
    }

    hostStatusLabel.setText (text, juce::dontSendNotification);
    hostStatusLabel.setColour (juce::Label::textColourId, colour);
}

void MidiHumanizerEditor::timerCallback()
{
    // The host may give the editor a new native window at any time, so re-check here.
    acceptPromisedFileDrops (*this);
    updateHostStatus();
    controls.refresh();

    const auto playing = plugin.isClipPlaying();
    const auto position = plugin.getClipPositionPpq();

    grid.setPlayhead (position, playing);
    velocityLane.setPlayhead (position, playing);
    ruler.setPlayhead (position, playing);

    if (playing && uiBool ("followPlayhead", true) && ! gridBounds.isEmpty())
    {
        const auto visible = timeline.visibleBeats (gridBounds.getWidth());
        const auto relative = (position - timeline.scrollBeats) / juce::jmax (1.0e-6, visible);

        // Only re-centre once the cursor is genuinely off-screen, otherwise the view
        // creeps continuously and becomes hard to read.
        if (relative < 0.02 || relative > 0.94)
        {
            timeline.setScroll (position - visible * 0.2, gridBounds.getWidth());
            updateScrollBars();
        }
    }
}

//==============================================================================
void MidiHumanizerEditor::chooseMidiFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a MIDI clip", juce::File(), "*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        juce::String error;

        if (plugin.loadClipFromFile (file, error))
        {
            timeline.zoomToFit (gridBounds.getWidth());
            setUi ("pixelsPerBeat", timeline.pixelsPerBeat);
            setStatus ("Loaded " + file.getFileName());
        }
        else
        {
            setStatus (error, true);
        }
    });
}

void MidiHumanizerEditor::chooseDrumMapFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a drum map (Cubase .drm or Reaper note names)",
                                                       juce::File(), "*.drm;*.txt;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        juce::String error;

        if (plugin.loadDrumMapFromFile (file, error))
            setStatus ("Loaded drum map " + file.getFileName() + " ("
                       + juce::String (plugin.getDrumMapping().getEntries().size()) + " rows)");
        else
            setStatus (error, true);
    });
}

void MidiHumanizerEditor::handleDroppedPaths (const juce::StringArray& paths)
{
    juce::File midiFile, drumMapFile;

    for (auto& path : paths)
    {
        const juce::File file (path.unquoted().trim());

        if (! file.existsAsFile())
            continue;

        if (midiFile == juce::File() && isMidiPath (file))
            midiFile = file;
        else if (drumMapFile == juce::File() && isDrumMapPath (file))
            drumMapFile = file;
    }

    juce::StringArray messages;
    auto failed = false;

    if (drumMapFile != juce::File())
    {
        juce::String error;

        if (plugin.loadDrumMapFromFile (drumMapFile, error))
            messages.add ("Drum map: " + drumMapFile.getFileName());
        else
        {
            messages.add (error);
            failed = true;
        }
    }

    if (midiFile != juce::File())
    {
        juce::String error;

        if (plugin.loadClipFromFile (midiFile, error))
        {
            timeline.zoomToFit (gridBounds.getWidth());
            setUi ("pixelsPerBeat", timeline.pixelsPerBeat);
            messages.add ("Loaded " + midiFile.getFileName());
        }
        else
        {
            messages.add (error);
            failed = true;
        }
    }

    if (messages.isEmpty())
        messages.add ("Nothing usable in that drop: expected a .mid clip or a .drm / .txt drum map.");

    setStatus (messages.joinIntoString ("   |   "), failed || midiFile == juce::File());
}

bool MidiHumanizerEditor::keyPressed (const juce::KeyPress& key)
{
    const auto& rows = lanes.getRows();

    // Walking the lane list from the keyboard is much faster than aiming at 15-pixel rows.
    const auto moveSelection = [this, &rows] (int delta, bool extend)
    {
        if (rows.empty())
            return true;

        const auto current = lanes.indexOfNote (lanes.getSelectedNote());
        const auto next = juce::jlimit (0, (int) rows.size() - 1,
                                        current < 0 ? 0 : current + delta);

        auto selection = extend ? lanes.getSelection() : std::vector<int>{};

        if (std::find (selection.begin(), selection.end(), rows[(size_t) next].note) == selection.end())
            selection.push_back (rows[(size_t) next].note);

        selectLanes (std::move (selection));
        return true;
    };

    if (key.isKeyCode (juce::KeyPress::upKey))
        return moveSelection (-1, key.getModifiers().isShiftDown());

    if (key.isKeyCode (juce::KeyPress::downKey))
        return moveSelection (1, key.getModifiers().isShiftDown());

    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        selectLanes ({});
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        plugin.undo();
        refreshAbAndUndo();
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                        | juce::ModifierKeys::shiftModifier, 0))
    {
        plugin.redo();
        refreshAbAndUndo();
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        if (auto* parameter = plugin.apvts.getParameter (pid::audition))
            parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);

        return true;
    }

    const auto selection = lanes.getSelection();

    if (! selection.empty())
    {
        if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
        {
            const auto muted = plugin.getLaneSettings (selection.back()).muted;

            for (auto note : selection)
                plugin.setLaneMuted (note, ! muted);

            rebuildLanes();
            return true;
        }

        if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
        {
            const auto soloed = plugin.getLaneSettings (selection.back()).soloed;

            for (auto note : selection)
                plugin.setLaneSoloed (note, ! soloed);

            rebuildLanes();
            return true;
        }
    }

    return false;
}

//==============================================================================
bool MidiHumanizerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& path : files)
    {
        const juce::File file (path);

        if (isMidiPath (file) || isDrumMapPath (file))
            return true;
    }

    return false;
}

void MidiHumanizerEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    grid.setDropActive (true);
}

void MidiHumanizerEditor::fileDragExit (const juce::StringArray&)
{
    grid.setDropActive (false);
}

void MidiHumanizerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    grid.setDropActive (false);
    handleDroppedPaths (files);
}

bool MidiHumanizerEditor::isInterestedInTextDrag (const juce::String& text)
{
    // Some hosts hand over a path (or a newline-separated list of them) as plain text
    // rather than as a file drag.
    return isInterestedInFileDrag (juce::StringArray::fromLines (text));
}

void MidiHumanizerEditor::textDragEnter (const juce::String&, int, int)
{
    grid.setDropActive (true);
}

void MidiHumanizerEditor::textDragExit (const juce::String&)
{
    grid.setDropActive (false);
}

void MidiHumanizerEditor::textDropped (const juce::String& text, int, int)
{
    grid.setDropActive (false);
    handleDroppedPaths (juce::StringArray::fromLines (text));
}

//==============================================================================
int MidiHumanizerEditor::uiInt (const char* key, int fallback) const
{
    return (int) plugin.uiState.getProperty (key, fallback);
}

bool MidiHumanizerEditor::uiBool (const char* key, bool fallback) const
{
    return (bool) plugin.uiState.getProperty (key, fallback);
}

double MidiHumanizerEditor::uiDouble (const char* key, double fallback) const
{
    return (double) plugin.uiState.getProperty (key, fallback);
}

void MidiHumanizerEditor::setUi (const char* key, const juce::var& value)
{
    plugin.uiState.setProperty (key, value, nullptr);
}

} // namespace mh
