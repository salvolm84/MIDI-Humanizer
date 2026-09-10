// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

#include <cmath>

namespace mh
{

namespace
{
    using Attr  = juce::AudioParameterFloatAttributes;
    using Range = juce::NormalisableRange<float>;

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const juce::String& name,
                                                           Range range, float defaultValue,
                                                           const juce::String& label)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            range, defaultValue,
                                                            Attr().withLabel (label));
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterInt> intParam (const char* id, const juce::String& name,
                                                       int minValue, int maxValue, int defaultValue)
    {
        return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name,
                                                          minValue, maxValue, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const juce::String& name,
                                                             juce::StringArray choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name,
                                                             std::move (choices), defaultIndex);
    }

    Range percent()  { return { 0.0f,   100.0f, 0.1f }; }
    Range bipolar100() { return { -100.0f, 100.0f, 0.1f }; }

}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MidiHumanizerProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- global ------------------------------------------------------------------
    layout.add (boolParam  (pid::enabled,  "Humanize", true),
                intParam   (pid::seed,     "Seed", 1, 9999, 1),
                boolParam  (pid::loopClip, "Loop Clip", true),
                boolParam  (pid::audition, "Audition", false),
                floatParam (pid::auditionBpm, "Audition Tempo", { 40.0f, 240.0f, 0.1f }, 120.0f, "BPM"),
                boolParam  (pid::midiThru, "Pass Input Through", false),
                boolParam  (pid::testNote, "Send Test Note", false),
                boolParam  (pid::mapOutputNotes, "Map Output Notes", true),
                intParam   (pid::transpose, "Transpose", -24, 24, 0));

    juce::StringArray channels { "As-is" };

    for (int ch = 1; ch <= 16; ++ch)
        channels.add (juce::String (ch));

    layout.add (choiceParam (pid::outChannel, "Output Channel", channels, 0));

    // ---- master lane values -------------------------------------------------------
    // Every lane inherits these until it is given an override. Timing and velocity are
    // both bias-plus-spread: where the lane sits on average, and how far each individual
    // hit wanders either side of it.
    layout.add (floatParam (pid::timeBias,    "Timing Bias",       { -60.0f, 60.0f, 0.1f }, 0.0f, "ms"),
                floatParam (pid::timeSpread,  "Timing Spread",     { 0.0f, 60.0f, 0.1f }, 8.0f, "ms"),
                floatParam (pid::swing,       "Swing",             bipolar100(), 0.0f, "%"),
                intParam   (pid::velTarget,   "Velocity Target",   0, 127, 0),
                floatParam (pid::velDynamics, "Velocity Dynamics", percent(), 0.0f, "%"),
                floatParam (pid::velSpread,   "Velocity Spread",   { 0.0f, 63.0f, 0.5f }, 12.0f, ""),
                floatParam (pid::accent,      "Accents",           percent(), 0.0f, "%"),
                floatParam (pid::minGap,      "Min Hit Gap",       { 0.0f, 80.0f, 0.5f }, 20.0f, "ms"));

    // ---- global timing ------------------------------------------------------------
    layout.add (floatParam  (pid::tighten,    "Tighten",      percent(), 0.0f, "%"),
                choiceParam (pid::tightenDiv, "Tighten Grid", gridDivisionLabels(), defaultGridDivisionIndex),
                choiceParam (pid::swingDiv,   "Swing Grid",   gridDivisionLabels(), defaultSwingDivisionIndex));

    // ---- drummer realism ----------------------------------------------------------
    layout.add (floatParam (pid::flam,       "Flams",          percent(), 0.0f, "%"),
                floatParam (pid::flamSpread, "Flam Spread",    { 1.0f, 60.0f, 0.1f }, 18.0f, "ms"),
                floatParam (pid::ghost,      "Ghost Notes",    percent(), 0.0f, "%"),
                intParam   (pid::ghostVel,   "Ghost Velocity", 1, 80, 28),
                floatParam (pid::variation,  "Variation",      percent(), 0.0f, "%"));

    // ---- playability limits -------------------------------------------------------
    layout.add (boolParam  (pid::limits, "Playability Limits", true),
                boolParam  (pid::choke,  "Choke Groups", true),
                floatParam (pid::hands,  "Hand Alternation", percent(), 0.0f, "%"));

    return layout;
}

//==============================================================================
juce::AudioProcessor::BusesProperties MidiHumanizerProcessor::busesForThisBuild()
{
   #if MH_INSTRUMENT_VARIANT
    // The bus exists only so instrument-slot hosts will load us; it stays silent.
    return BusesProperties().withOutput ("Silent Out", juce::AudioChannelSet::stereo(), true);
   #else
    return {};
   #endif
}

MidiHumanizerProcessor::MidiHumanizerProcessor()
    : juce::AudioProcessor (busesForThisBuild()),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (auto* parameter : getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.addParameterListener (withID->paramID, this);

    uiState.setProperty ("gridDivision", defaultGridDivisionIndex, nullptr);
    uiState.setProperty ("pixelsPerBeat", 96.0, nullptr);
    uiState.setProperty ("rowVisibility", 1, nullptr);   // 0 = all, 1 = hide unused, 2 = hide unlabelled
    uiState.setProperty ("rowOrder", 0, nullptr);          // 0 = map, 1 = pitch, 2 = kit
    uiState.setProperty ("followPlayhead", true, nullptr);
    uiState.setProperty ("laneHeight", 15.0, nullptr);
    uiState.setProperty ("showSource", true, nullptr);
    uiState.setProperty ("colourMode", 0, nullptr);      // 0 = by lane, 1 = by velocity
    uiState.setProperty ("tipsEnabled", true, nullptr);

    invalidateHumanizedClip();

    // Seed the history with the opening state, so the stack's current position always
    // corresponds to the live state.
    history.push (captureEditState());

    startTimerHz (30);
}

MidiHumanizerProcessor::~MidiHumanizerProcessor()
{
    stopTimer();

    for (auto* parameter : getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.removeParameterListener (withID->paramID, this);
}

//==============================================================================
void MidiHumanizerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    player.prepare (currentSampleRate);
    internalPpq = 0.0;
    lastTransportSource = TransportSource::stopped;
    diagBlocks.store (0, std::memory_order_relaxed);
    diagNoteOns.store (0, std::memory_order_relaxed);
    invalidateHumanizedClip();
}

void MidiHumanizerProcessor::releaseResources()
{
    player.reset();
}

bool MidiHumanizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if MH_INSTRUMENT_VARIANT
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
   #else
    // A MIDI effect has no audio buses, so there is nothing to reject.
    juce::ignoreUnused (layouts);
    return true;
   #endif
}

void MidiHumanizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto blockSize = buffer.getNumSamples();

    if (blockSize <= 0)
        return;

    double bpm = 0.0;
    double hostPpq = 0.0;
    bool hostPlaying = false;
    bool haveHostPpq = false;

    auto havePlayHead = false;

    if (auto* playHead = getPlayHead())
    {
        havePlayHead = true;

        if (const auto position = playHead->getPosition())
        {
            if (const auto tempo = position->getBpm())
                bpm = *tempo;

            hostPlaying = position->getIsPlaying();

            // Prefer the musical position, which is what keeps the clip locked to bars.
            // Hosts do not all fill in the whole position struct on every processing
            // path, so fall back to wall-clock time and convert it, rather than going
            // silent because one field was missing.
            if (const auto ppq = position->getPpqPosition())
            {
                hostPpq = *ppq;
                haveHostPpq = true;
            }
            else if (bpm > 0.0)
            {
                if (const auto seconds = position->getTimeInSeconds())
                {
                    hostPpq = *seconds * bpm / 60.0;
                    haveHostPpq = true;
                }
                else if (const auto samples = position->getTimeInSamples())
                {
                    hostPpq = ((double) *samples / currentSampleRate) * bpm / 60.0;
                    haveHostPpq = true;
                }
            }
        }
    }

    if (bpm > 0.0)
        hostBpm.store (bpm, std::memory_order_relaxed);

    auto effectiveTempo = bpm;

    if (effectiveTempo <= 0.0)
        effectiveTempo = (double) apvts.getRawParameterValue (pid::auditionBpm)->load();

    const auto auditioning = apvts.getRawParameterValue (pid::audition)->load() > 0.5f;
    const auto passThrough = apvts.getRawParameterValue (pid::midiThru)->load() > 0.5f;

    ClipPlayer::BlockContext context;
    context.bpm           = effectiveTempo;
    context.sampleRate    = currentSampleRate;
    context.numSamples    = blockSize;
    context.loopClip      = apvts.getRawParameterValue (pid::loopClip)->load() > 0.5f;
    context.forcedChannel = (int) apvts.getRawParameterValue (pid::outChannel)->load();
    context.transpose     = (int) apvts.getRawParameterValue (pid::transpose)->load();

    auto source = TransportSource::stopped;

    if (hostPlaying && haveHostPpq)
        source = TransportSource::hostSynced;
    else if (hostPlaying)
        source = TransportSource::hostInferred;   // rolling, but we have to keep time
    else if (auditioning)
        source = TransportSource::audition;

    const auto blockPpq = (double) blockSize * effectiveTempo / (60.0 * currentSampleRate);

    // Restart our own clock only when we newly take over from the host, so that toggling
    // Audition starts from the top instead of resuming mid-bar.
    if (source != lastTransportSource
        && (source == TransportSource::audition || source == TransportSource::hostInferred))
        internalPpq = 0.0;

    switch (source)
    {
        case TransportSource::hostSynced:
            context.ppqStart = hostPpq;
            context.transportRunning = true;
            internalPpq = hostPpq;   // keeps a later audition hand-over seamless
            break;

        case TransportSource::hostInferred:
        case TransportSource::audition:
            context.ppqStart = internalPpq;
            context.transportRunning = true;
            internalPpq += blockPpq;
            break;

        case TransportSource::stopped:
            break;
    }

    lastTransportSource = source;

    juce::MidiBuffer generated;
    player.renderBlock (clipStore.live(), context, generated);

    // Routing check: a metronomic note that ignores the clip and the transport entirely.
    // If this is inaudible in a DAW then the host is not accepting the plugin's MIDI
    // output at all, which is a different problem from an empty or mis-routed clip.
    const auto testNoteWanted = apvts.getRawParameterValue (pid::testNote)->load() > 0.5f;
    const auto testChannel = context.forcedChannel > 0 ? context.forcedChannel : 10;

    if (testNoteWanted)
    {
        const auto period  = juce::jmax (2, (int) (currentSampleRate * 0.5));
        const auto noteLen = juce::jmax (1, (int) (currentSampleRate * 0.08));
        auto phase = testNotePhase;

        for (int offset = 0; offset < blockSize; ++offset)
        {
            if (phase == 0)
            {
                generated.addEvent (juce::MidiMessage::noteOn (testChannel, testNoteNumber,
                                                               (juce::uint8) 100), offset);
                testNoteSounding = true;
            }
            else if (phase == noteLen && testNoteSounding)
            {
                generated.addEvent (juce::MidiMessage::noteOff (testChannel, testNoteNumber), offset);
                testNoteSounding = false;
            }

            if (++phase >= period)
                phase = 0;
        }

        testNotePhase = phase;
    }
    else
    {
        testNotePhase = 0;

        if (testNoteSounding)
        {
            generated.addEvent (juce::MidiMessage::noteOff (testChannel, testNoteNumber), 0);
            testNoteSounding = false;
        }
    }

    if (! passThrough)
        midiMessages.clear();

    midiMessages.addEvents (generated, 0, blockSize, 0);

    // ---- diagnostics -----------------------------------------------------------
    juce::int64 noteOns = 0;

    for (const auto metadata : generated)
        if (metadata.getMessage().isNoteOn())
            ++noteOns;

    diagHavePlayHead.store (havePlayHead, std::memory_order_relaxed);
    diagHostPlaying.store (hostPlaying, std::memory_order_relaxed);
    diagHaveMusicalPosition.store (haveHostPpq, std::memory_order_relaxed);
    diagSource.store ((int) source, std::memory_order_relaxed);
    diagPpq.store (context.ppqStart, std::memory_order_relaxed);
    diagBlocks.fetch_add (1, std::memory_order_relaxed);
    diagNoteOns.fetch_add (noteOns, std::memory_order_relaxed);

    for (const auto metadata : generated)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            diagLastChannel.store (message.getChannel(), std::memory_order_relaxed);
            diagLastNote.store (message.getNoteNumber(), std::memory_order_relaxed);
        }
    }
}

MidiHumanizerProcessor::HostStatus MidiHumanizerProcessor::getHostStatus() const
{
    HostStatus status;
    status.havePlayHead        = diagHavePlayHead.load (std::memory_order_relaxed);
    status.hostReportsPlaying  = diagHostPlaying.load (std::memory_order_relaxed);
    status.haveMusicalPosition = diagHaveMusicalPosition.load (std::memory_order_relaxed);
    status.source              = (TransportSource) diagSource.load (std::memory_order_relaxed);
    status.ppq                 = diagPpq.load (std::memory_order_relaxed);
    status.bpm                 = hostBpm.load (std::memory_order_relaxed);
    status.blocksProcessed     = diagBlocks.load (std::memory_order_relaxed);
    status.noteOnsEmitted      = diagNoteOns.load (std::memory_order_relaxed);
    status.lastChannel         = diagLastChannel.load (std::memory_order_relaxed);
    status.lastNoteNumber      = diagLastNote.load (std::memory_order_relaxed);
    return status;
}

//==============================================================================
void MidiHumanizerProcessor::parameterChanged (const juce::String&, float)
{
    // Called on whichever thread moved the parameter, including the audio thread during
    // automation, so this must stay allocation-free and non-blocking.
    needsRecompute.store (true, std::memory_order_relaxed);
}

double MidiHumanizerProcessor::effectiveBpm() const
{
    auto bpm = hostBpm.load (std::memory_order_relaxed);

    if (bpm <= 0.0)
        bpm = (double) apvts.getRawParameterValue (pid::auditionBpm)->load();

    if (bpm <= 0.0)
        bpm = sourceClip.sourceBpm;

    return juce::jlimit (20.0, 999.0, bpm > 0.0 ? bpm : 120.0);
}

void MidiHumanizerProcessor::timerCallback()
{
    juce::ValueTree pending;

    {
        const juce::ScopedLock lock (pendingLock);
        pending = pendingState;
        pendingState = {};
    }

    if (pending.isValid())
        applyPendingState (pending);

    // Tempo feeds every millisecond-to-ppq conversion, so a tempo change has to
    // re-humanize just like a parameter change does.
    const auto bpm = effectiveBpm();
    const auto tempoMoved = std::abs (bpm - lastRecomputeBpm) > 0.01;

    if (needsRecompute.exchange (false, std::memory_order_relaxed) || tempoMoved)
        recomputeNow();
}

LaneSettings MidiHumanizerProcessor::getMasterLaneSettings() const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    LaneSettings master;
    master.hasOverride      = false;
    master.enabled          = true;
    master.timingBiasMs     = value (pid::timeBias);
    master.timingSpreadMs   = value (pid::timeSpread);
    master.swingPercent     = value (pid::swing);
    master.velocityTarget   = (int) value (pid::velTarget);
    master.velocityDynamics = value (pid::velDynamics);
    master.velocitySpread   = value (pid::velSpread);
    master.accentAmount     = value (pid::accent);
    master.minGapMs         = value (pid::minGap);
    return master;
}

LaneSettings MidiHumanizerProcessor::getResolvedLaneSettings (int noteNumber) const
{
    const auto& stored = laneSettings.get (noteNumber);
    auto resolved = stored.hasOverride ? stored : getMasterLaneSettings();

    copyLaneIdentity (resolved, stored);
    return resolved;
}

HumanizeParams MidiHumanizerProcessor::collectParams (double bpm) const
{
    const auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    HumanizeParams p;
    p.enabled         = value (pid::enabled) > 0.5f;
    p.seed            = (int) value (pid::seed);
    p.referenceBpm    = bpm;

    p.tightenPercent  = value (pid::tighten);
    p.tightenDivision = (int) value (pid::tightenDiv);
    p.swingDivision   = (int) value (pid::swingDiv);

    p.flamAmount      = value (pid::flam);
    p.flamSpreadMs    = value (pid::flamSpread);
    p.ghostAmount     = value (pid::ghost);
    p.ghostVelocity   = (int) value (pid::ghostVel);
    p.variationAmount = value (pid::variation);
    p.handAlternation = value (pid::hands);

    p.limitsEnabled   = value (pid::limits) > 0.5f;
    p.chokeEnabled    = value (pid::choke) > 0.5f;
    p.outputNote      = buildOutputNoteMap();
    p.anyLaneSoloed   = laneSettings.anySoloed();

    // Inheritance is resolved once, here, so the engine never has to think about it.
    const auto master = getMasterLaneSettings();

    for (int note = 0; note < LaneSettingsTable::numLanes; ++note)
    {
        const auto& stored = laneSettings.get (note);
        auto& resolved = p.lane[(size_t) note];
        resolved = stored.hasOverride ? stored : master;
        copyLaneIdentity (resolved, stored);
    }

    return p;
}

void MidiHumanizerProcessor::setLaneSettings (int noteNumber, const LaneSettings& settings)
{
    laneSettings.set (noteNumber, settings);

    // Linked lanes speak with one voice, so an edit to one is an edit to all of them.
    laneSettings.syncGroupFrom (noteNumber);
    recomputeNow();
    commitUndoState();
}

int MidiHumanizerProcessor::linkLanes (const std::vector<int>& noteNumbers)
{
    const auto groupId = laneSettings.linkNotes (noteNumbers);
    recomputeNow();
    commitUndoState();
    return groupId;
}

void MidiHumanizerProcessor::unlinkLanes (const std::vector<int>& noteNumbers)
{
    laneSettings.unlinkNotes (noteNumbers);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::setLaneOverride (int noteNumber, bool shouldOverride)
{
    auto lane = laneSettings.get (noteNumber);

    if (lane.hasOverride == shouldOverride)
        return;

    if (shouldOverride)
    {
        // Seed from the master, so switching a lane to manual changes nothing audible
        // until the user actually moves one of its controls.
        const auto wasEnabled = lane.enabled;
        lane = getMasterLaneSettings();
        lane.enabled = wasEnabled;
    }

    lane.hasOverride = shouldOverride;
    laneSettings.set (noteNumber, lane);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::setLaneEnabled (int noteNumber, bool shouldBeEnabled)
{
    auto lane = laneSettings.get (noteNumber);

    if (lane.enabled == shouldBeEnabled)
        return;

    lane.enabled = shouldBeEnabled;
    laneSettings.set (noteNumber, lane);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::resetLane (int noteNumber)
{
    laneSettings.reset (noteNumber);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::copyLaneToAllLanes (int noteNumber, bool includeUntouched)
{
    laneSettings.copyToAll (noteNumber, includeUntouched);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::recomputeNow()
{
    const auto bpm = effectiveBpm();
    referenceBpm.store (bpm, std::memory_order_relaxed);
    lastRecomputeBpm = bpm;

    humanizedForUi = Humanizer::process (sourceClip, collectParams (bpm), &report);
    clipStore.publish (NoteSequence (humanizedForUi));

    sendChangeMessage();
}

//==============================================================================
bool MidiHumanizerProcessor::loadClipFromFile (const juce::File& file, juce::String& errorMessage)
{
    NoteSequence loaded;

    if (! MidiClipIO::load (file, loaded, errorMessage))
        return false;

    sourceClip = std::move (loaded);
    recomputeNow();
    return true;
}

void MidiHumanizerProcessor::clearClip()
{
    sourceClip.clear();
    recomputeNow();
}

juce::File MidiHumanizerProcessor::writeHumanizedClipToTempFile() const
{
    if (humanizedForUi.isEmpty())
        return {};

    const auto baseName = (humanizedForUi.name.isNotEmpty() ? humanizedForUi.name : juce::String ("clip"))
                          + "_humanized";

    return MidiClipIO::writeTempFileForDragOut (humanizedForUi, getReferenceBpm(), baseName);
}

std::array<int, LaneSettingsTable::numLanes> MidiHumanizerProcessor::buildOutputNoteMap() const
{
    auto map = makeIdentityNoteMap();

    const auto applyMap = apvts.getRawParameterValue (pid::mapOutputNotes)->load() > 0.5f;

    if (! applyMap)
        return map;

    // A Cubase drum map's ONote column says which note the row should actually send. For
    // most maps it equals INote, so this is usually the identity anyway.
    for (auto& entry : drumMapping.getEntries())
        if (juce::isPositiveAndBelow (entry.inNote, LaneSettingsTable::numLanes))
            map[(size_t) entry.inNote] = juce::jlimit (0, 127, entry.outNote);

    return map;
}

void MidiHumanizerProcessor::setLaneMuted (int noteNumber, bool shouldBeMuted)
{
    auto lane = laneSettings.get (noteNumber);

    if (lane.muted == shouldBeMuted)
        return;

    lane.muted = shouldBeMuted;
    laneSettings.set (noteNumber, lane);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::setLaneSoloed (int noteNumber, bool shouldBeSoloed)
{
    auto lane = laneSettings.get (noteNumber);

    if (lane.soloed == shouldBeSoloed)
        return;

    lane.soloed = shouldBeSoloed;
    laneSettings.set (noteNumber, lane);
    recomputeNow();
    commitUndoState();
}

void MidiHumanizerProcessor::clearAllSolos()
{
    if (! laneSettings.anySoloed())
        return;


    for (int note = 0; note < LaneSettingsTable::numLanes; ++note)
    {
        auto lane = laneSettings.get (note);

        if (lane.soloed)
        {
            lane.soloed = false;
            laneSettings.set (note, lane);
        }
    }

    recomputeNow();
}

void MidiHumanizerProcessor::rerollLanes (const std::vector<int>& noteNumbers)
{
    if (noteNumbers.empty())
        return;

    laneSettings.rerollLanes (noteNumbers, juce::Random::getSystemRandom().nextInt (97));
    recomputeNow();
    commitUndoState();
}

//==============================================================================
juce::ValueTree MidiHumanizerProcessor::captureEditState()
{
    juce::ValueTree state (PresetStore::presetTreeType);
    state.setProperty ("version", 1, nullptr);
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (laneSettings.toValueTree(), nullptr);
    return state;
}

void MidiHumanizerProcessor::applyEditState (const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    // Undo capture is suppressed while restoring, otherwise applying a state would push
    // the state we are leaving and the stack would grow on every undo.
    const juce::ScopedValueSetter<bool> guard (applyingEditState, true);

    if (const auto params = state.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    if (const auto lanes = state.getChildWithName ("LaneSettings"); lanes.isValid())
        laneSettings.restoreFromValueTree (lanes);
    else
        laneSettings.resetAll();

    recomputeNow();
}

//==============================================================================
std::vector<PresetInfo> MidiHumanizerProcessor::getPresetList() const
{
    std::vector<PresetInfo> presets;

    for (auto& factory : factoryPresets())
    {
        PresetInfo info;
        info.name = factory.name;
        info.isFactory = true;
        presets.push_back (info);
    }

    for (auto& user : presetStore.getUserPresets())
        presets.push_back (user);

    return presets;
}

bool MidiHumanizerProcessor::loadPreset (const juce::String& name, juce::String& errorMessage)
{
    if (const auto* factory = findFactoryPreset (name))
    {
        applyFactoryPreset (*factory);
        currentPresetName = name;
        commitUndoState();
        return true;
    }

    const auto state = presetStore.loadFromFile (presetStore.fileForName (name), errorMessage);

    if (! state.isValid())
        return false;

    applyEditState (state);
    currentPresetName = name;
    commitUndoState();
    return true;
}

void MidiHumanizerProcessor::applyFactoryPreset (const FactoryPreset& factory)
{
    // Suppress undo capture while the many individual writes go in; the single step is
    // committed by the caller once the whole preset has landed.
    const juce::ScopedValueSetter<bool> guard (applyingEditState, true);

    // Applied through the normal setters rather than as a state blob, so a factory preset
    // cannot go stale if the parameter layout changes.
    const auto setParam = [this] (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    setParam (pid::timeBias,    factory.master.timingBiasMs);
    setParam (pid::timeSpread,  factory.master.timingSpreadMs);
    setParam (pid::swing,       factory.master.swingPercent);
    setParam (pid::velTarget,   (float) factory.master.velocityTarget);
    setParam (pid::velDynamics, factory.master.velocityDynamics);
    setParam (pid::velSpread,   factory.master.velocitySpread);
    setParam (pid::accent,      factory.master.accentAmount);
    setParam (pid::minGap,      factory.master.minGapMs);

    setParam (pid::tighten,    factory.tighten);
    setParam (pid::flam,       factory.flam);
    setParam (pid::flamSpread, factory.flamSpread);
    setParam (pid::ghost,      factory.ghost);
    setParam (pid::ghostVel,   (float) factory.ghostVel);
    setParam (pid::variation,  factory.variation);
    setParam (pid::hands,      factory.hands);

    // Links, mutes, solos and per-lane seeds belong to the session rather than to the
    // feel, so a preset replaces the humanization values and leaves those alone.
    const auto keepSessionState = [] (LaneSettings& target, const LaneSettings& existing)
    {
        target.enabled    = existing.enabled;
        target.groupId    = existing.groupId;
        target.muted      = existing.muted;
        target.soloed     = existing.soloed;
        target.seedOffset = existing.seedOffset;
    };

    for (int note = 0; note < LaneSettingsTable::numLanes; ++note)
    {
        LaneSettings fresh;
        keepSessionState (fresh, laneSettings.get (note));
        laneSettings.set (note, fresh);
    }

    for (auto& laneEntry : factory.lanes)
    {
        auto lane = laneEntry.settings;
        keepSessionState (lane, laneSettings.get (laneEntry.note));
        laneSettings.set (laneEntry.note, lane);
    }

    recomputeNow();
}

bool MidiHumanizerProcessor::saveUserPreset (const juce::String& name, juce::String& errorMessage)
{
    if (! presetStore.save (name, captureEditState(), errorMessage))
        return false;

    currentPresetName = name;
    return true;
}

bool MidiHumanizerProcessor::deleteUserPreset (const juce::String& name, juce::String& errorMessage)
{
    if (! presetStore.deletePreset (name, errorMessage))
        return false;

    if (currentPresetName == name)
        currentPresetName = {};

    return true;
}

//==============================================================================
int MidiHumanizerProcessor::learnGrooveFromFile (const juce::File& file, juce::String& errorMessage)
{
    NoteSequence reference;

    if (! MidiClipIO::load (file, reference, errorMessage))
        return -1;

    const auto gridIndex = (int) apvts.getRawParameterValue (pid::tightenDiv)->load();
    const auto analysis = GrooveExtractor::analyse (reference, gridIndex, effectiveBpm());

    if (analysis.lanes.empty())
    {
        errorMessage = file.getFileName() + " has no notes to measure";
        return -1;
    }


    auto lanesUpdated = 0;

    for (auto& measured : analysis.lanes)
    {
        // A single hit says nothing about spread, so it is measured but not applied.
        if (measured.hits < 2)
            continue;

        auto lane = getResolvedLaneSettings (measured.note);
        GrooveExtractor::applyTo (lane, measured);
        laneSettings.set (measured.note, lane);
        laneSettings.syncGroupFrom (measured.note);
        ++lanesUpdated;
    }

    lastGrooveSummary = "Learned " + juce::String (lanesUpdated) + " lanes from "
                        + file.getFileName() + " (" + juce::String (analysis.totalHits) + " hits, grid "
                        + gridDivision (gridIndex).label + ")";

    currentPresetName = {};
    recomputeNow();
    commitUndoState();
    return lanesUpdated;
}

//==============================================================================
void MidiHumanizerProcessor::setAbSlot (int slot)
{
    const auto target = juce::jlimit (0, 1, slot);

    if (target == abSlot)
        return;

    // Store what is on screen into the slot being left, so both slots always hold what
    // the user last had in them.
    abSlots[abSlot] = captureEditState();
    abSlot = target;

    if (abSlots[abSlot].isValid())
        applyEditState (abSlots[abSlot]);
    else
        abSlots[abSlot] = captureEditState();   // first visit: seed it from the current state

    sendChangeMessage();
}

void MidiHumanizerProcessor::copyAbSlotToOther()
{
    abSlots[abSlot] = captureEditState();
    abSlots[1 - abSlot] = abSlots[abSlot].createCopy();
    sendChangeMessage();
}

//==============================================================================
void MidiHumanizerProcessor::setUndoSuppressed (bool shouldSuppress)
{
    undoSuppressed = shouldSuppress;

    // The end of a gesture is the point at which it becomes one undo step.
    if (! shouldSuppress)
        commitUndoState();
}

void MidiHumanizerProcessor::commitUndoState()
{
    if (applyingEditState || undoSuppressed)
        return;

    // Belt and braces for anything that changes several lanes in a loop: a single user
    // action should not leave a run of near-identical undo steps behind it.
    const auto now = juce::Time::getMillisecondCounter();

    if (now - lastUndoCommitMs < 200)
        return;

    lastUndoCommitMs = now;
    history.push (captureEditState());
}

void MidiHumanizerProcessor::undo()
{
    const auto state = history.undo();

    if (state.isValid())
        applyEditState (state);
}

void MidiHumanizerProcessor::redo()
{
    const auto state = history.redo();

    if (state.isValid())
        applyEditState (state);
}

bool MidiHumanizerProcessor::loadDrumMapFromFile (const juce::File& file, juce::String& errorMessage)
{
    if (! drumMapping.loadFromFile (file, errorMessage))
        return false;

    sendChangeMessage();
    return true;
}

void MidiHumanizerProcessor::clearDrumMap()
{
    drumMapping.clear();
    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* MidiHumanizerProcessor::createEditor()
{
    return new MidiHumanizerEditor (*this);
}

void MidiHumanizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("MidiHumanizerState");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (sourceClip.toValueTree(), nullptr);
    root.appendChild (drumMapping.toValueTree(), nullptr);
    root.appendChild (laneSettings.toValueTree(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);

    if (const auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void MidiHumanizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);

    if (! root.hasType ("MidiHumanizerState"))
        return;

    // Parameters are safe to swap in from any thread; everything else is handed to the
    // timer so it lands on the message thread the UI reads from.
    if (const auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    {
        const juce::ScopedLock lock (pendingLock);
        pendingState = root;
    }

    invalidateHumanizedClip();
}

void MidiHumanizerProcessor::applyPendingState (const juce::ValueTree& root)
{
    if (const auto clip = root.getChildWithName ("Clip"); clip.isValid())
        sourceClip.restoreFromValueTree (clip);

    drumMapping.restoreFromValueTree (root.getChildWithName ("DrumMapping"));
    laneSettings.restoreFromValueTree (root.getChildWithName ("LaneSettings"));

    // A freshly loaded project starts a fresh history: undoing into the previous
    // project's state would be nonsense.
    history.clear();
    history.push (captureEditState());

    if (const auto ui = root.getChildWithName ("UiState"); ui.isValid())
        uiState.copyPropertiesFrom (ui, nullptr);

    recomputeNow();
}

} // namespace mh

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mh::MidiHumanizerProcessor();
}
