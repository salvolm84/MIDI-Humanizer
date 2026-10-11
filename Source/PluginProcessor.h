// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "Engine/ClipPlayer.h"
#include "Engine/EditHistory.h"
#include "Engine/FactoryPresets.h"
#include "Engine/GrooveExtractor.h"
#include "Engine/Humanizer.h"
#include "Engine/PresetStore.h"
#include "Model/DrumMapping.h"
#include "Model/MidiClipIO.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Two plugin shapes are built from these sources.

    The default is a true MIDI effect: no audio buses at all, which is what Reaper,
    Bitwig, Studio One and FL expect in a MIDI FX slot. Cubase does not accept
    third-party VST3 MIDI effects in its MIDI Insert slots, so an instrument-flavoured
    variant is also built; it carries a silent stereo output so the host will load it in
    an instrument slot, from where its MIDI output can be routed to a drum track.
*/
#if defined (JucePlugin_IsMidiEffect) && JucePlugin_IsMidiEffect == 0
 #define MH_INSTRUMENT_VARIANT 1
#else
 #define MH_INSTRUMENT_VARIANT 0
#endif

namespace mh
{

/** MIDI FX plugin that plays a dropped drum clip back through a humanizer.

    Threading model, since it drives most of the design:
      - The source clip, the drum map and the UI state belong to the message thread.
      - Parameter edits set a dirty flag; a 30 Hz timer on the message thread re-runs the
        humanizer over the whole clip and publishes the result into a lock-free store.
      - processBlock only ever reads the published clip and the atomic parameter values.

    Recomputing the entire clip on every change (rather than humanizing note by note in
    the audio callback) is what lets the grid show exactly what will be played.
*/
class MidiHumanizerProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::Timer,
                                     public juce::ChangeBroadcaster
{
public:
    MidiHumanizerProcessor();
    ~MidiHumanizerProcessor() override;

    // ---- AudioProcessor ---------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "MIDI Humanizer"; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return true; }
    bool isMidiEffect() const override                       { return MH_INSTRUMENT_VARIANT == 0; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- clip management (message thread) ---------------------------------------
    /** Loads a .mid from disk. Returns false and fills `errorMessage` on failure. */
    bool loadClipFromFile (const juce::File& file, juce::String& errorMessage);

    void clearClip();

    const NoteSequence& getSourceClip()    const noexcept { return sourceClip; }
    const NoteSequence& getHumanizedClip() const noexcept { return humanizedForUi; }
    const HumanizeReport& getReport()      const noexcept { return report; }

    /** Writes the humanized clip to a temp .mid so it can be dragged back into a host. */
    juce::File writeHumanizedClipToTempFile() const;

    // ---- per-lane humanization (message thread) -----------------------------------
    /** The master values every lane inherits, read from the host parameters. */
    LaneSettings getMasterLaneSettings() const;

    /** A lane's stored settings, which may or may not be overriding the master. */
    const LaneSettings& getLaneSettings (int noteNumber) const { return laneSettings.get (noteNumber); }

    /** What the humanizer will actually use for this lane, master and override resolved. */
    LaneSettings getResolvedLaneSettings (int noteNumber) const;

    void setLaneSettings (int noteNumber, const LaneSettings& settings);

    /** Turning an override on seeds the lane from the master, so nothing jumps the moment
        it is enabled.
    */
    void setLaneOverride (int noteNumber, bool shouldOverride);
    void setLaneEnabled (int noteNumber, bool shouldBeEnabled);
    void resetLane (int noteNumber);
    void copyLaneToAllLanes (int noteNumber, bool includeUntouched);

    /** Note numbers that currently override the master, for the UI to badge. */
    bool laneHasOverride (int noteNumber) const { return laneSettings.get (noteNumber).hasOverride; }

    // ---- linked lanes -------------------------------------------------------------
    /** Links these lanes so that editing any one of them edits all of them. Returns the
        new group id, or 0 if fewer than two lanes were given.
    */
    int  linkLanes (const std::vector<int>& noteNumbers);
    void unlinkLanes (const std::vector<int>& noteNumbers);

    int  getLaneGroup (int noteNumber) const { return laneSettings.get (noteNumber).groupId; }
    std::vector<int> getLanesInGroup (int groupId) const { return laneSettings.notesInGroup (groupId); }

    void setLaneMuted (int noteNumber, bool shouldBeMuted);
    void setLaneSoloed (int noteNumber, bool shouldBeSoloed);
    void clearAllSolos();
    bool anyLaneSoloed() const { return laneSettings.anySoloed(); }

    /** Re-rolls only these lanes, leaving the rest of the kit's performance alone. */
    void rerollLanes (const std::vector<int>& noteNumbers);

    // ---- presets -------------------------------------------------------------------
    /** Factory presets first, then the user's own. */
    std::vector<PresetInfo> getPresetList() const;

    /** Applies a factory or user preset by name. */
    bool loadPreset (const juce::String& name, juce::String& errorMessage);

    bool saveUserPreset (const juce::String& name, juce::String& errorMessage);
    bool deleteUserPreset (const juce::String& name, juce::String& errorMessage);

    const juce::String& getCurrentPresetName() const noexcept { return currentPresetName; }

    static juce::File getPresetDirectory() { return PresetStore::getPresetDirectory(); }

    // ---- groove extraction ----------------------------------------------------------
    /** Measures a played reference clip and sets each matching lane's bias, spread and
        velocity from it. Returns the number of lanes updated, or -1 on failure.
    */
    int learnGrooveFromFile (const juce::File& file, juce::String& errorMessage);

    /** Description of the last analysis, for the UI to report. */
    const juce::String& getLastGrooveSummary() const noexcept { return lastGrooveSummary; }

    // ---- A/B snapshots --------------------------------------------------------------
    /** 0 = A, 1 = B. Switching stores the current state into the outgoing slot first, so
        both slots always hold what the user last had in them.
    */
    void setAbSlot (int slot);
    int  getAbSlot() const noexcept { return abSlot; }

    /** Copies the active slot's state over the other one. */
    void copyAbSlotToOther();

    // ---- undo -----------------------------------------------------------------------
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    /** Records the state *after* a change, which is what keeps the stack's current
        position and the live state in step. Called at the end of each mutating action.
    */
    void commitUndoState();

    /** Held true for the duration of a knob drag, so one gesture makes one undo step
        instead of one per mouse move.
    */
    void setUndoSuppressed (bool shouldSuppress);

    // ---- drum map (message thread) -----------------------------------------------
    bool loadDrumMapFromFile (const juce::File& file, juce::String& errorMessage);
    void clearDrumMap();
    const DrumMapping& getDrumMapping() const noexcept { return drumMapping; }

    // ---- host diagnostics ---------------------------------------------------------
    /** Where the playback position is coming from this block. */
    enum class TransportSource
    {
        stopped = 0,
        hostSynced,     ///< the host gave us a musical position; the clip starts where playback does
        hostInferred,   ///< the host is rolling but gave no usable position
        audition        ///< our own clock, driven by the Audition switch
    };

    /** What the host is actually handing the plugin. Shown in the editor, because in a
        DAW "no sound" has several very different causes and they are indistinguishable
        from the outside: the host may never call the plugin at all, may call it with no
        playhead, or may call it correctly while the clip is simply empty.
    */
    struct HostStatus
    {
        bool            havePlayHead = false;
        bool            hostReportsPlaying = false;
        bool            haveMusicalPosition = false;
        TransportSource source = TransportSource::stopped;
        double          ppq = 0.0;
        double          bpm = 0.0;
        juce::int64     blocksProcessed = 0;
        juce::int64     noteOnsEmitted = 0;
        int             lastChannel = 0;      ///< channel of the most recent note-on
        int             lastNoteNumber = -1;
    };

    HostStatus getHostStatus() const;

    // ---- transport read-out for the UI cursor -------------------------------------
    double getClipPositionPpq() const noexcept { return player.getClipPositionPpq(); }
    bool   isClipPlaying()      const noexcept { return player.isPlaying(); }

    /** Tempo the humanizer is currently converting milliseconds with. */
    double getReferenceBpm() const noexcept { return referenceBpm.load (std::memory_order_relaxed); }

    /** Forces a recompute, e.g. after restoring state. */
    void invalidateHumanizedClip() { needsRecompute.store (true, std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

    /** Grid settings, zoom and row-display options. Owned and mutated by the editor,
        persisted with the plugin state.
    */
    juce::ValueTree uiState { "UiState" };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** No audio buses for the MIDI-effect build; a silent stereo output for the
        instrument-slot build. See MH_INSTRUMENT_VARIANT above.
    */
    static BusesProperties busesForThisBuild();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    HumanizeParams collectParams (double bpm) const;

    /** Everything a preset, an A/B slot or an undo step needs: the master parameters and
        every per-lane override. Never the clip, the drum map or the view settings.
    */
    juce::ValueTree captureEditState();
    void applyEditState (const juce::ValueTree& state);

    /** Output note per lane, taken from the drum map's ONote column. */
    std::array<int, LaneSettingsTable::numLanes> buildOutputNoteMap() const;

    void applyFactoryPreset (const FactoryPreset& factory);
    void recomputeNow();
    double effectiveBpm() const;

    /** Applies a restored state tree. Called from the timer so the clip, drum map and UI
        state are only ever touched on the message thread, whatever thread the host chose
        to call setStateInformation from.
    */
    void applyPendingState (const juce::ValueTree& root);

    NoteSequence sourceClip;      ///< exactly what was dropped in
    NoteSequence humanizedForUi;  ///< message-thread copy of what the player is using
    HumanizeReport report;
    DrumMapping drumMapping;
    LaneSettingsTable laneSettings;

    ClipStore clipStore;
    ClipPlayer player;

    std::atomic<bool>   needsRecompute { false };
    std::atomic<double> referenceBpm { 120.0 };
    std::atomic<double> hostBpm { 0.0 };   ///< 0 = no host tempo seen yet

    double internalPpq = 0.0;
    double hostStartPpq = 0.0;      ///< host position the current run of playback began at
    double expectedHostPpq = 0.0;   ///< where the host should be next block if nothing jumped
    TransportSource lastTransportSource = TransportSource::stopped;

    static constexpr int testNoteNumber = 36;   // GM kick, audible on any drum kit
    int  testNotePhase = 0;
    bool testNoteSounding = false;
    double currentSampleRate = 48000.0;
    double lastRecomputeBpm = 0.0;

    PresetStore presetStore;
    juce::String currentPresetName;
    juce::String lastGrooveSummary;

    EditHistory history;
    bool applyingEditState = false;   ///< suppresses undo capture while restoring
    bool undoSuppressed = false;      ///< true mid-gesture
    juce::uint32 lastUndoCommitMs = 0;

    juce::ValueTree abSlots[2];
    int abSlot = 0;

    juce::CriticalSection pendingLock;
    juce::ValueTree pendingState;

    // Diagnostics, written on the audio thread and read by the editor.
    std::atomic<bool>        diagHavePlayHead { false };
    std::atomic<bool>        diagHostPlaying { false };
    std::atomic<bool>        diagHaveMusicalPosition { false };
    std::atomic<int>         diagSource { 0 };
    std::atomic<double>      diagPpq { 0.0 };
    std::atomic<juce::int64> diagBlocks { 0 };
    std::atomic<juce::int64> diagNoteOns { 0 };
    std::atomic<int>         diagLastChannel { 0 };
    std::atomic<int>         diagLastNote { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiHumanizerProcessor)
};

} // namespace mh
