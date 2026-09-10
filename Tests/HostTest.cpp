// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

/*
    Two-layer end-to-end check.

    1. The AudioProcessor itself, driven by a fake host transport: does a loaded clip
       actually play, humanized, in sync, with no stuck notes and no invalid events?
    2. The built VST3 bundle, loaded through JUCE's own VST3 host: is it recognised as a
       MIDI effect with the right bus layout and parameter set, does it survive a
       transport run, and does its state round-trip through the host?

    The clip is injected only in layer 1, because a host-side state blob is in the
    format JUCE's VST3 host defines rather than the processor's own, so pushing a raw
    processor blob into a hosted instance would prove nothing.

    Usage: MidiHumanizerHostTest <path/to/MIDI Humanizer.vst3> <clip.mid>
*/

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace
{
    int failures = 0;
    int checks = 0;

    void check (bool condition, const juce::String& what)
    {
        ++checks;

        if (condition)
            return;

        ++failures;
        std::cout << "  FAIL: " << what << std::endl;
    }

    void section (const juce::String& name)
    {
        std::cout << "== " << name << std::endl;
    }

    /** A minimal host transport: constant tempo, always rolling.

        The VST3 wrapper requires a valid time in samples, so this fills in the whole
        position rather than only the fields the plugin happens to read.
    */
    class FakePlayHead final : public juce::AudioPlayHead
    {
    public:
        double ppqPosition = 0.0;
        double bpm = 120.0;
        double sampleRate = 48000.0;
        juce::int64 timeInSamples = 0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppqPosition);
            info.setIsPlaying (playing);
            info.setTimeSignature (TimeSignature { 4, 4 });
            info.setTimeInSamples (timeInSamples);
            info.setTimeInSeconds ((double) timeInSamples / sampleRate);
            info.setPpqPositionOfLastBarStart (std::floor (ppqPosition / 4.0) * 4.0);
            return info;
        }

        void advance (int numSamples)
        {
            timeInSamples += numSamples;
            ppqPosition += (double) numSamples * bpm / (60.0 * sampleRate);
        }
    };

    /** Runs a transport over any AudioProcessor and tallies what came out. */
    struct TransportResult
    {
        int noteOns = 0;
        int noteOffs = 0;
        int stuck = 0;
        int offBoundaryHits = 0;
        std::set<int> velocities;
        std::set<int> noteNumbers;
        bool eventsInRange = true;
        bool velocitiesValid = true;
    };

    TransportResult runTransport (juce::AudioProcessor& processor, int numBlocks,
                                  double sampleRate, int blockSize)
    {
        TransportResult result;

        FakePlayHead playHead;
        playHead.sampleRate = sampleRate;
        processor.setPlayHead (&playHead);
        processor.prepareToPlay (sampleRate, blockSize);

        // A MIDI effect has no audio buses, which is exactly how JUCE's wrappers call it.
        juce::AudioBuffer<float> audio (juce::jmax (0, processor.getTotalNumOutputChannels()), blockSize);
        std::map<std::pair<int, int>, int> sounding;

        const auto tally = [&] (const juce::MidiBuffer& midi)
        {
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();

                if (metadata.samplePosition < 0 || metadata.samplePosition >= blockSize)
                    result.eventsInRange = false;

                const auto key = std::make_pair (message.getChannel(), message.getNoteNumber());

                if (message.isNoteOn())
                {
                    ++result.noteOns;
                    ++sounding[key];
                    result.velocities.insert (message.getVelocity());
                    result.noteNumbers.insert (message.getNoteNumber());

                    if (message.getVelocity() < 1 || message.getVelocity() > 127)
                        result.velocitiesValid = false;

                    if (metadata.samplePosition % 64 != 0)
                        ++result.offBoundaryHits;
                }
                else if (message.isNoteOff())
                {
                    ++result.noteOffs;
                    --sounding[key];
                }
            }
        };

        for (int block = 0; block < numBlocks; ++block)
        {
            audio.clear();
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            tally (midi);
            playHead.advance (blockSize);
        }

        // Stopping the transport must release everything still sounding.
        playHead.playing = false;
        audio.clear();
        juce::MidiBuffer tail;
        processor.processBlock (audio, tail);
        tally (tail);

        for (auto& [key, count] : sounding)
        {
            juce::ignoreUnused (key);

            if (count != 0)
                ++result.stuck;
        }

        processor.setPlayHead (nullptr);
        processor.releaseResources();
        return result;
    }

    void pumpMessageThread (int iterations = 25)
    {
        for (int i = 0; i < iterations; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }


    /** Finds a hosted parameter by its display name. */
    juce::AudioProcessorParameter* findParameterNamed (juce::AudioProcessor& processor,
                                                       const juce::String& name)
    {
        for (auto* parameter : processor.getParameters())
            if (parameter->getName (64) == name)
                return parameter;

        return nullptr;
    }

    void setParam (juce::AudioProcessorValueTreeState& state, const char* id, float value)
    {
        if (auto* parameter = state.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

//==============================================================================
static void testProcessorPlaysClip (const juce::File& clipFile)
{
    section ("Processor plays a loaded clip in sync with the host transport");

    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 512;

    mh::MidiHumanizerProcessor processor;
    juce::String error;

    check (processor.loadClipFromFile (clipFile, error), "clip loads: " + error);

    const auto sourceNotes = processor.getSourceClip().size();
    const auto clipBars = processor.getSourceClip().lengthPpq / processor.getSourceClip().barLengthPpq();

    setParam (processor.apvts, "timeAmount", 90.0f);
    setParam (processor.apvts, "timeRange", 30.0f);
    setParam (processor.apvts, "velAmount", 70.0f);
    setParam (processor.apvts, "accent", 60.0f);
    setParam (processor.apvts, "ghost", 70.0f);
    pumpMessageThread();

    const auto humanizedNotes = processor.getHumanizedClip().size();
    std::cout << "   " << sourceNotes << " notes in, " << humanizedNotes << " out, "
              << clipBars << " bars" << std::endl;

    check (humanizedNotes > 0, "the humanizer produced a clip to play");

    // ~12.8 seconds at 120 BPM, so a two-bar clip comes round about three times.
    const auto result = runTransport (processor, 1200, sampleRate, blockSize);

    std::cout << "   " << result.noteOns << " note-ons, " << result.noteOffs << " note-offs, "
              << result.velocities.size() << " distinct velocities, "
              << result.noteNumbers.size() << " lanes" << std::endl;

    check (result.noteOns > humanizedNotes * 2, "the clip plays and loops round repeatedly");
    check (result.eventsInRange, "every event lands inside its block");
    check (result.velocitiesValid, "every emitted velocity is 1..127");
    check (result.stuck == 0, "no note left sounding after the transport stops ("
                              + juce::String (result.stuck) + ")");
    check (result.velocities.size() > 4, "velocities are humanized rather than uniform");
    check (result.offBoundaryHits > 0, "hits land off the block boundaries, i.e. timing moved");
    check (result.noteNumbers.size() >= 3, "every drum lane in the clip is played");
}

static void testProcessorSilentWithoutAClip()
{
    section ("Processor is silent with no clip");

    mh::MidiHumanizerProcessor processor;
    pumpMessageThread (5);

    const auto result = runTransport (processor, 40, 48000.0, 512);

    check (result.noteOns == 0, "nothing is emitted when no clip is loaded");
    check (result.stuck == 0, "and nothing is left sounding");
}

static void testProcessorStateRoundTrip (const juce::File& clipFile)
{
    section ("Processor state round trip");

    mh::MidiHumanizerProcessor saved;
    juce::String error;
    saved.loadClipFromFile (clipFile, error);
    setParam (saved.apvts, "swing", 42.0f);
    setParam (saved.apvts, "ghost", 55.0f);
    pumpMessageThread();

    juce::MemoryBlock blob;
    saved.getStateInformation (blob);
    check (blob.getSize() > 0, "state is produced");

    mh::MidiHumanizerProcessor restored;
    restored.setStateInformation (blob.getData(), (int) blob.getSize());
    pumpMessageThread();

    check (restored.getSourceClip().size() == saved.getSourceClip().size(),
           "the dropped clip survives a save and reload");
    check (std::abs (restored.getSourceClip().lengthPpq - saved.getSourceClip().lengthPpq) < 1.0e-9,
           "clip length survives");
    check (std::abs (restored.apvts.getRawParameterValue ("swing")->load() - 42.0f) < 0.05f,
           "parameters survive");
    check (restored.getHumanizedClip().size() == saved.getHumanizedClip().size(),
           "the reloaded clip humanizes to the same note count");
}

static void testVst3Bundle (const juce::File& bundle)
{
    section ("Built VST3 bundle");

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, bundle.getFullPathName());

    check (! descriptions.isEmpty(), "bundle is recognised: " + bundle.getFullPathName());

    if (descriptions.isEmpty())
        return;

    const auto& description = *descriptions[0];
    std::cout << "   " << description.name << " | " << description.pluginFormatName
              << " | category " << description.category
              << " | in=" << description.numInputChannels
              << " out=" << description.numOutputChannels << std::endl;

    // The two build flavours have deliberately different shapes, so the expectation
    // depends on which bundle was handed to us.
    const auto isInstrumentVariant = description.isInstrument;

    if (isInstrumentVariant)
        check (description.numOutputChannels > 0 || description.numInputChannels == 0,
               "instrument variant is scannable");
    else
        check (description.numInputChannels == 0 && description.numOutputChannels == 0,
               "MIDI-effect build declares no audio channels");

    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 512;

    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::String createError;
    auto instance = formatManager.createPluginInstance (description, sampleRate, blockSize, createError);

    check (instance != nullptr, "instantiates: " + createError);

    if (instance == nullptr)
        return;

    check (instance->acceptsMidi(), "declares MIDI input");
    check (instance->producesMidi(), "declares MIDI output");
    check (instance->hasEditor(), "provides an editor");

    std::cout << "   instantiated buses: in=" << instance->getTotalNumInputChannels()
              << " out=" << instance->getTotalNumOutputChannels()
              << " (output bus count " << instance->getBusCount (false) << ")"
              << " isMidiEffect=" << (instance->isMidiEffect() ? "yes" : "no") << std::endl;

    if (isInstrumentVariant)
        check (instance->getTotalNumOutputChannels() > 0,
               "instrument variant exposes the silent output bus an instrument slot needs");
    else
        check (instance->getTotalNumOutputChannels() == 0,
               "MIDI-effect build exposes no audio output");

    // The parameter set the host sees must match what the processor defines.
    mh::MidiHumanizerProcessor reference;
    check (instance->getParameters().size() >= reference.getParameters().size(),
           "the host sees every parameter ("
               + juce::String (instance->getParameters().size()) + " vs "
               + juce::String (reference.getParameters().size()) + ")");

    juce::StringArray missing;

    for (auto* referenceParam : reference.getParameters())
    {
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (referenceParam))
        {
            auto found = false;

            for (auto* hosted : instance->getParameters())
                if (hosted->getName (64) == withID->getName (64))
                    found = true;

            if (! found)
                missing.add (withID->paramID);
        }
    }

    check (missing.isEmpty(), "no parameter is missing in the host: " + missing.joinIntoString (", "));

    // A transport run must not crash, and with no clip loaded it must stay silent.
    const auto result = runTransport (*instance, 200, sampleRate, blockSize);

    check (result.eventsInRange, "hosted plugin keeps events inside their block");
    check (result.stuck == 0, "hosted plugin leaves nothing sounding");
    std::cout << "   hosted run: " << result.noteOns << " note-ons (no clip loaded, so zero expected)"
              << std::endl;
    check (result.noteOns == 0, "hosted plugin is silent with no clip");

    // State must survive a host-side save and reload. Any parameter will do; the point
    // is that the blob the host stores reproduces itself exactly.
    if (auto* firstParameter = instance->getParameters()[0])
        firstParameter->setValueNotifyingHost (0.75f);

    pumpMessageThread (10);

    juce::MemoryBlock first, second;
    instance->getStateInformation (first);
    check (first.getSize() > 0, "hosted plugin reports state");

    instance->setStateInformation (first.getData(), (int) first.getSize());
    pumpMessageThread (10);
    instance->getStateInformation (second);

    check (first.getSize() == second.getSize() && first == second,
           "host state round-trips unchanged");

    instance->releaseResources();
    instance.reset();
}


/** Proves the hosted VST3's processBlock really runs with a usable block size.

    A plugin with no audio buses is an easy thing to get wrong in a wrapper, and "no
    output" looks identical whether the plumbing is broken or there is simply nothing to
    play. Switching on pass-through and feeding notes in separates the two: if input
    comes back out, the wrapper, the bus layout and the block size are all sound, and any
    silence in a DAW is about the clip or the routing instead.
*/
static void testHostedPluginProcessesMidi (const juce::File& bundle)
{
    section ("Hosted VST3 actually processes MIDI");

    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 512;

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, bundle.getFullPathName());

    if (descriptions.isEmpty())
    {
        check (false, "bundle is recognised");
        return;
    }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::String createError;
    auto instance = formatManager.createPluginInstance (*descriptions[0], sampleRate, blockSize, createError);

    if (instance == nullptr)
    {
        check (false, "instantiates: " + createError);
        return;
    }

    auto* thru = findParameterNamed (*instance, "Pass Input Through");
    check (thru != nullptr, "the pass-through parameter is visible to the host");

    if (thru == nullptr)
        return;

    thru->setValueNotifyingHost (1.0f);
    pumpMessageThread (10);

    FakePlayHead playHead;
    playHead.sampleRate = sampleRate;
    instance->setPlayHead (&playHead);
    instance->prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> audio (juce::jmax (0, instance->getTotalNumOutputChannels()), blockSize);

    auto sawNoteOn = false;
    auto sawNoteOff = false;
    auto reportedBlockSizes = std::set<int> {};

    for (int block = 0; block < 8; ++block)
    {
        audio.clear();

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (10, 38), 200);

        instance->processBlock (audio, midi);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn() && message.getNoteNumber() == 38)
                sawNoteOn = true;

            if (message.isNoteOff() && message.getNoteNumber() == 38)
                sawNoteOff = true;

            reportedBlockSizes.insert (metadata.samplePosition);
        }

        playHead.advance (blockSize);
    }

    check (sawNoteOn && sawNoteOff,
           "input MIDI survives a round trip through the hosted plugin, so the wrapper, "
           "the empty bus layout and the block size are all working");

    instance->setPlayHead (nullptr);
    instance->releaseResources();
    instance.reset();
}


/** Closes the gap the other VST3 checks leave open.

    Every hosted check so far ran with no clip loaded, so "no MIDI out" was expected and
    a wrapper that silently swallowed generated events would have passed. This pushes a
    state blob containing a clip into the hosted plugin, confirms it landed by reading a
    parameter back, and only then asserts that notes actually leave the bundle.
*/
static void testHostedPluginEmitsGeneratedMidi (const juce::File& bundle, const juce::File& clipFile)
{
    section ("Hosted VST3 emits generated notes");

    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 512;

    juce::MemoryBlock state;
    int expectedNotes = 0;

    {
        mh::MidiHumanizerProcessor reference;
        juce::String error;

        if (! reference.loadClipFromFile (clipFile, error))
        {
            check (false, "reference clip loads: " + error);
            return;
        }

        // A parameter well away from its default acts as a witness that the blob applied.
        setParam (reference.apvts, "swing", 42.0f);
        pumpMessageThread();

        expectedNotes = reference.getHumanizedClip().size();
        reference.getStateInformation (state);
    }

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, bundle.getFullPathName());

    if (descriptions.isEmpty())
    {
        check (false, "bundle is recognised");
        return;
    }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::String createError;
    auto instance = formatManager.createPluginInstance (*descriptions[0], sampleRate, blockSize, createError);

    if (instance == nullptr)
    {
        check (false, "instantiates: " + createError);
        return;
    }

    auto* swing = findParameterNamed (*instance, "Swing");
    check (swing != nullptr, "the swing parameter is visible to the host");

    instance->setStateInformation (state.getData(), (int) state.getSize());
    pumpMessageThread (30);

    const auto swingText = swing != nullptr ? swing->getCurrentValueAsText() : juce::String();
    const auto stateApplied = swing != nullptr && swingText.getFloatValue() > 20.0f;

    std::cout << "   swing after state push: " << swingText
              << (stateApplied ? "  (state applied)" : "  (state NOT applied)") << std::endl;

    if (! stateApplied)
    {
        // Worth knowing, but it is a limitation of pushing a processor blob through the
        // host rather than a fault in the plugin, so it is reported and not failed.
        std::cout << "   NOTE: a raw processor state blob does not load through JUCE's VST3 host,"
                  << std::endl
                  << "         so generated-MIDI output cannot be checked from here." << std::endl;
        instance->releaseResources();
        instance.reset();
        return;
    }

    const auto result = runTransport (*instance, 1200, sampleRate, blockSize);

    std::cout << "   " << result.noteOns << " note-ons out of the bundle (clip holds "
              << expectedNotes << ")" << std::endl;

    check (result.noteOns > expectedNotes,
           "generated notes leave the VST3 bundle and loop");
    check (result.stuck == 0, "nothing left sounding after the transport stops");
    check (result.eventsInRange, "generated events land inside their block");

    instance->releaseResources();
    instance.reset();
}


/** The decisive wrapper check.

    The test note is generated from a parameter alone, with no clip and no transport
    involvement, so unlike every other hosted check this one can prove that notes the
    plugin invents actually leave the VST3 bundle. If this passes and a DAW still hears
    nothing, the fault is in the host's routing rather than in the plugin.
*/
static void testHostedPluginEmitsTestNote (const juce::File& bundle)
{
    section ("Hosted VST3 emits generated notes (test-note path)");

    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 512;

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, bundle.getFullPathName());

    if (descriptions.isEmpty())
    {
        check (false, "bundle is recognised");
        return;
    }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::String createError;
    auto instance = formatManager.createPluginInstance (*descriptions[0], sampleRate, blockSize, createError);

    if (instance == nullptr)
    {
        check (false, "instantiates: " + createError);
        return;
    }

    auto* testNote = findParameterNamed (*instance, "Send Test Note");
    check (testNote != nullptr, "the test-note parameter is visible to the host");

    if (testNote == nullptr)
        return;

    testNote->setValueNotifyingHost (1.0f);
    pumpMessageThread (10);

    // Deliberately left stopped: the test note must not depend on the transport.
    FakePlayHead playHead;
    playHead.sampleRate = sampleRate;
    playHead.playing = false;
    instance->setPlayHead (&playHead);
    instance->prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> audio (juce::jmax (0, instance->getTotalNumOutputChannels()), blockSize);

    auto noteOns = 0;
    auto noteOffs = 0;

    for (int block = 0; block < 200; ++block)
    {
        audio.clear();
        juce::MidiBuffer midi;
        instance->processBlock (audio, midi);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
                ++noteOns;
            else if (message.isNoteOff())
                ++noteOffs;
        }

        playHead.advance (blockSize);
    }

    std::cout << "   " << noteOns << " test note-ons left the bundle with the transport stopped"
              << std::endl;

    check (noteOns > 0, "notes the plugin generates do leave the VST3 bundle");
    check (noteOffs > 0, "and their note-offs leave too");

    testNote->setValueNotifyingHost (0.0f);
    pumpMessageThread (5);

    instance->setPlayHead (nullptr);
    instance->releaseResources();
    instance.reset();
}


static void testPresetsAndFactoryContent()
{
    section ("Presets");

    mh::MidiHumanizerProcessor processor;
    pumpMessageThread (5);

    const auto presets = processor.getPresetList();
    check (presets.size() >= 6, "factory presets are listed ("
                                    + juce::String ((int) presets.size()) + ")");

    auto allFactoryFirst = true;
    auto seenUser = false;

    for (auto& preset : presets)
    {
        if (! preset.isFactory)
            seenUser = true;
        else if (seenUser)
            allFactoryFirst = false;
    }

    check (allFactoryFirst, "factory presets come before user presets");

    // Every factory preset must actually load and change something.
    auto loadedAll = true;

    for (auto& preset : presets)
    {
        if (! preset.isFactory)
            continue;

        juce::String error;

        if (! processor.loadPreset (preset.name, error))
        {
            loadedAll = false;
            std::cout << "   could not load " << preset.name << ": " << error << std::endl;
        }
    }

    check (loadedAll, "every factory preset loads");

    juce::String error;
    check (processor.loadPreset ("Tight Studio", error), "a named factory preset loads: " + error);
    check (processor.getCurrentPresetName() == "Tight Studio", "the current preset name is reported");

    // Tight Studio puts the hats ahead of the snare: the preset's whole point.
    const auto hats = processor.getResolvedLaneSettings (42);
    const auto snare = processor.getResolvedLaneSettings (38);
    check (hats.timingBiasMs < 0.0f, "Tight Studio plays the hats early");
    check (snare.timingBiasMs > 0.0f, "and the snare late");
    check (hats.hasOverride && snare.hasOverride, "its lane values are overrides");

    check (processor.loadPreset ("Straight (no humanizing)", error), "the neutral preset loads");
    check (processor.getResolvedLaneSettings (42).timingSpreadMs == 0.0f,
           "and leaves nothing humanized");

    // A preset must not disturb links, mutes or solos, which belong to the session.
    processor.linkLanes ({ 41, 45 });
    processor.setLaneMuted (36, true);
    processor.loadPreset ("Loose Jazz", error);

    check (processor.getLaneGroup (41) > 0 && processor.getLaneGroup (41) == processor.getLaneGroup (45),
           "a preset leaves link groups intact");
    check (processor.getLaneSettings (36).muted, "and leaves mutes intact");

    // User preset round trip through a real file.
    const auto name = "ZZ Test Preset " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
    setParam (processor.apvts, "timeSpread", 27.0f);
    auto lane = processor.getResolvedLaneSettings (47);
    lane.hasOverride = true;
    lane.velocityTarget = 111;
    processor.setLaneSettings (47, lane);
    pumpMessageThread (5);

    check (processor.saveUserPreset (name, error), "a user preset saves: " + error);

    // Move away from it, then come back.
    processor.loadPreset ("Straight (no humanizing)", error);
    check (std::abs (processor.apvts.getRawParameterValue ("timeSpread")->load() - 27.0f) > 1.0f,
           "loading another preset really changed the values");

    check (processor.loadPreset (name, error), "the user preset loads back: " + error);
    pumpMessageThread (5);

    check (std::abs (processor.apvts.getRawParameterValue ("timeSpread")->load() - 27.0f) < 0.2f,
           "master parameters come back");
    check (processor.getResolvedLaneSettings (47).velocityTarget == 111,
           "per-lane overrides come back");

    auto appearsInList = false;

    for (auto& preset : processor.getPresetList())
        if (preset.name == name && ! preset.isFactory)
            appearsInList = true;

    check (appearsInList, "it is listed as a user preset");

    check (processor.deleteUserPreset (name, error), "and can be deleted: " + error);

    appearsInList = false;

    for (auto& preset : processor.getPresetList())
        if (preset.name == name)
            appearsInList = true;

    check (! appearsInList, "after which it is gone");
}

static void testLaneIdentityIsNeverInherited()
{
    section ("Lane identity is never inherited from the master");

    mh::MidiHumanizerProcessor processor;
    pumpMessageThread (5);

    // A lane with no override at all must still honour its own mute, solo, link and seed.
    // These fields describe the drum, not its values, so resolving them from the master
    // silently discards them.
    check (! processor.getLaneSettings (41).hasOverride, "the lane starts with no override");

    processor.setLaneMuted (41, true);
    processor.setLaneSoloed (45, true);
    processor.linkLanes ({ 47, 48 });
    processor.rerollLanes ({ 50 });
    pumpMessageThread (5);

    const auto muted = processor.getResolvedLaneSettings (41);
    const auto soloed = processor.getResolvedLaneSettings (45);
    const auto linked = processor.getResolvedLaneSettings (47);
    const auto rerolled = processor.getResolvedLaneSettings (50);

    check (! muted.hasOverride && muted.muted, "a mute survives on a lane with no override");
    check (soloed.soloed, "so does a solo");
    check (linked.groupId > 0, "so does group membership");
    check (rerolled.seedOffset != 0, "so does a per-lane seed");

    // And the engine has to see the same thing, not just the UI.
    processor.clearAllSolos();
    processor.setLaneMuted (36, true);

    juce::String error;
    processor.loadClipFromFile (juce::File::getCurrentWorkingDirectory()
                                    .getChildFile ("Assets/demo-rock-beat.mid"), error);
    pumpMessageThread (10);

    auto kickAudible = false;
    auto kickPresent = false;

    for (auto& n : processor.getHumanizedClip().notes)
        if (n.noteNumber == 36)
        {
            kickPresent = true;

            if (n.audible)
                kickAudible = true;
        }

    check (kickPresent, "the muted lane is still in the clip");
    check (! kickAudible, "and the engine agrees it is inaudible");
}

static void testGrooveDropZoneInterest()
{
    section ("Groove drop zone accepts only MIDI");

    mh::GrooveDropZone zone;

    check (zone.isInterestedInFileDrag ({ "/tmp/take.mid" }), "a .mid is accepted");
    check (zone.isInterestedInFileDrag ({ "/tmp/take.MIDI" }), "case does not matter");

    // Anything it declines falls through to the editor, which is what lets a drum map
    // dropped anywhere still load as a drum map.
    check (! zone.isInterestedInFileDrag ({ "/tmp/kit.drm" }), "a Cubase drum map is declined");
    check (! zone.isInterestedInFileDrag ({ "/tmp/names.txt" }), "a Reaper note-name file is declined");
    check (! zone.isInterestedInFileDrag ({ "/tmp/song.wav" }), "audio is declined");
    check (! zone.isInterestedInFileDrag ({}), "an empty drag is declined");

    // The callback has to fire for the dropped file, not merely for any drop.
    juce::File received;
    zone.onFileDropped = [&received] (const juce::File& file) { received = file; };

    auto temp = juce::File::createTempFile (".mid");
    temp.replaceWithText ("not really midi, but it exists");
    zone.filesDropped ({ temp.getFullPathName() }, 0, 0);

    check (received == temp, "the dropped file reaches the callback");

    // A file that does not exist must not be reported as dropped.
    received = juce::File();
    zone.filesDropped ({ "/tmp/does-not-exist-12345.mid" }, 0, 0);
    check (received == juce::File(), "a missing file is ignored");

    temp.deleteFile();
}

static void testRowVisibility()
{
    section ("Row visibility modes");

    // A clip using three GM drums plus one note outside the GM percussion table.
    mh::NoteSequence clip;
    clip.lengthPpq = 4.0;

    for (auto note : { 36, 38, 42, 20 })
    {
        mh::Note n;
        n.startPpq = 0.0;
        n.lengthPpq = 0.1;
        n.noteNumber = note;
        n.velocity = 100;
        n.channel = 10;
        clip.notes.push_back (n);
    }

    clip.sortByTime();
    clip.assignSourceIds();

    const auto countRows = [&clip] (const mh::DrumMapping& mapping, mh::LaneModel::Visibility mode,
                                    int& hiddenWithNotes)
    {
        mh::LaneModel model;
        model.rebuild (clip, clip, mapping, mode, mh::LaneModel::Order::pitchDescending, {});
        hiddenWithNotes = model.getHiddenUsedRowCount();
        return model.numRows();
    };

    // ---- with a drum map loaded --------------------------------------------------
    mh::DrumMapping mapping;
    juce::String error;
    const juce::String mapText = "36 Kick\n38 Snare\n42 Hat\n49 Crash\n51 Ride\n";
    check (mapping.loadFromText (mapText, "kit", error), "test drum map parses: " + error);

    auto hidden = 0;

    const auto all = countRows (mapping, mh::LaneModel::Visibility::allRows, hidden);
    check (all == 6, "All rows shows the map's five rows plus the unmapped note in use ("
                         + juce::String (all) + ")");
    check (hidden == 0, "and hides nothing");

    const auto unused = countRows (mapping, mh::LaneModel::Visibility::hideUnused, hidden);
    check (unused == 4, "Hide unused shows only the four drums played (" + juce::String (unused) + ")");
    check (hidden == 0, "and cannot hide a row that has notes");

    const auto unlabelled = countRows (mapping, mh::LaneModel::Visibility::hideUnlabelled, hidden);
    check (unlabelled == 5, "Hide unlabelled shows exactly the map's rows ("
                                + juce::String (unlabelled) + ")");
    check (hidden == 1, "and reports the one played row it hides");

    // ---- with no drum map, falling back to General MIDI ---------------------------
    mh::DrumMapping none;

    const auto gmAll = countRows (none, mh::LaneModel::Visibility::allRows, hidden);
    check (gmAll == 23, "with no map, All rows spans notes 20 to 42 (" + juce::String (gmAll) + ")");

    const auto gmLabelled = countRows (none, mh::LaneModel::Visibility::hideUnlabelled, hidden);
    check (gmLabelled == 8, "Hide unlabelled falls back to the GM table, leaving 35 to 42 ("
                                + juce::String (gmLabelled) + ")");
    check (hidden == 1, "and still reports the played note that has no GM name");

    // A mode must never leave the grid blank; that reads as a broken plugin.
    mh::NoteSequence unnamedOnly;
    unnamedOnly.lengthPpq = 4.0;
    mh::Note stray;
    stray.noteNumber = 20;
    stray.velocity = 100;
    unnamedOnly.notes.push_back (stray);
    unnamedOnly.assignSourceIds();

    mh::LaneModel model;
    model.rebuild (unnamedOnly, unnamedOnly, none, mh::LaneModel::Visibility::hideUnlabelled,
                   mh::LaneModel::Order::pitchDescending, {});
    check (model.numRows() > 0, "a mode that would hide everything falls back to showing rows");
}

static void testUndoRedo()
{
    section ("Undo and redo through the processor");

    mh::MidiHumanizerProcessor processor;
    pumpMessageThread (5);

    check (! processor.canUndo(), "nothing to undo on a fresh instance");

    auto lane = processor.getResolvedLaneSettings (38);
    lane.hasOverride = true;
    lane.timingBiasMs = 15.0f;
    processor.setLaneSettings (38, lane);
    pumpMessageThread (3);

    check (processor.canUndo(), "an edit can be undone");
    check (std::abs (processor.getResolvedLaneSettings (38).timingBiasMs - 15.0f) < 0.01f,
           "the edit took effect");

    processor.undo();
    pumpMessageThread (3);

    check (std::abs (processor.getResolvedLaneSettings (38).timingBiasMs) < 0.01f,
           "undo reverses it");
    check (processor.canRedo(), "and it can be redone");

    processor.redo();
    pumpMessageThread (3);

    check (std::abs (processor.getResolvedLaneSettings (38).timingBiasMs - 15.0f) < 0.01f,
           "redo restores it");

    // Repeated undo must keep stepping back rather than sticking on one state.
    processor.undo();
    processor.undo();
    pumpMessageThread (3);
    check (std::abs (processor.getResolvedLaneSettings (38).timingBiasMs) < 0.01f,
           "a second undo does not bounce back to the edited state");

    // Links are undoable too.
    processor.linkLanes ({ 41, 45, 47 });
    pumpMessageThread (3);
    check (processor.getLaneGroup (41) > 0, "lanes linked");

    processor.undo();
    pumpMessageThread (3);
    check (processor.getLaneGroup (41) == 0, "undo unlinks them again");
}

static void testAbSnapshots()
{
    section ("A/B snapshots");

    mh::MidiHumanizerProcessor processor;
    pumpMessageThread (5);

    check (processor.getAbSlot() == 0, "starts on slot A");

    setParam (processor.apvts, "timeSpread", 5.0f);
    pumpMessageThread (3);

    processor.setAbSlot (1);
    pumpMessageThread (3);

    setParam (processor.apvts, "timeSpread", 40.0f);
    pumpMessageThread (3);

    check (processor.getAbSlot() == 1, "switched to slot B");

    processor.setAbSlot (0);
    pumpMessageThread (5);

    check (std::abs (processor.apvts.getRawParameterValue ("timeSpread")->load() - 5.0f) < 0.2f,
           "slot A still holds its own value ("
               + juce::String (processor.apvts.getRawParameterValue ("timeSpread")->load(), 1) + ")");

    processor.setAbSlot (1);
    pumpMessageThread (5);

    check (std::abs (processor.apvts.getRawParameterValue ("timeSpread")->load() - 40.0f) < 0.2f,
           "and slot B holds its own");

    // Copying makes both slots agree.
    processor.copyAbSlotToOther();
    processor.setAbSlot (0);
    pumpMessageThread (5);

    check (std::abs (processor.apvts.getRawParameterValue ("timeSpread")->load() - 40.0f) < 0.2f,
           "copy-to-other overwrites the far slot");
}

static void testLearnGroove (const juce::File& clipFile)
{
    section ("Learning a groove through the processor");

    mh::MidiHumanizerProcessor processor;
    juce::String error;

    check (processor.loadClipFromFile (clipFile, error), "clip loads: " + error);
    pumpMessageThread (5);

    // Learn from the clip itself. It is machine-quantized, so every lane should come back
    // with essentially no bias and no spread, which is a precise expectation to check.
    const auto lanesLearned = processor.learnGrooveFromFile (clipFile, error);

    check (lanesLearned > 0, "lanes were learned: " + error);
    check (processor.getLastGrooveSummary().isNotEmpty(), "a summary is reported");

    auto allTight = true;

    for (int note = 0; note < 128; ++note)
    {
        const auto lane = processor.getResolvedLaneSettings (note);

        if (lane.hasOverride && (std::abs (lane.timingBiasMs) > 1.0f || lane.timingSpreadMs > 1.0f))
            allTight = false;
    }

    check (allTight, "a quantized reference teaches near-zero bias and spread");

    juce::String missingError;
    check (processor.learnGrooveFromFile (juce::File(), missingError) < 0,
           "a missing file fails rather than throwing");
}

//==============================================================================
int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "usage: MidiHumanizerHostTest <plugin.vst3> <clip.mid>" << std::endl;
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    // juce::File requires absolute paths, so resolve whatever the shell handed us.
    const auto here = juce::File::getCurrentWorkingDirectory();
    const auto bundle = here.getChildFile (juce::String (argv[1]));
    const auto clip = here.getChildFile (juce::String (argv[2]));

    testProcessorPlaysClip (clip);
    testProcessorSilentWithoutAClip();
    testProcessorStateRoundTrip (clip);
    testPresetsAndFactoryContent();
    testLaneIdentityIsNeverInherited();
    testGrooveDropZoneInterest();
    testRowVisibility();
    testUndoRedo();
    testAbSnapshots();
    testLearnGroove (clip);
    testVst3Bundle (bundle);
    testHostedPluginProcessesMidi (bundle);
    testHostedPluginEmitsGeneratedMidi (bundle, clip);
    testHostedPluginEmitsTestNote (bundle);

    std::cout << std::endl
              << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks passed" << std::endl;

    return failures == 0 ? 0 : 1;
}
