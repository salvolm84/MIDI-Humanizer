// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

/*
    Console checks for the parts of the plugin that are pure logic: the humanizer
    transform, the MIDI file round trip, the drum map parsers, and the clip player's
    note bookkeeping. Run with:  cmake --build build --target MidiHumanizerTests
                                 ./build/MidiHumanizerTests_artefacts/Debug/MidiHumanizerTests
*/

#include "../Source/Engine/ClipPlayer.h"
#include "../Source/Engine/EditHistory.h"
#include "../Source/Engine/GrooveExtractor.h"
#include "../Source/Engine/Humanizer.h"
#include "../Source/Model/DrumMapping.h"
#include "../Source/Model/MidiClipIO.h"

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

    /** A two-bar rock beat: kick on 1 and 3, snare on 2 and 4, straight sixteenth hats. */
    mh::NoteSequence makeTestBeat()
    {
        mh::NoteSequence clip;
        clip.name = "test-beat";
        clip.lengthPpq = 8.0;
        clip.sourceBpm = 120.0;

        const auto add = [&clip] (double ppq, int note, int velocity)
        {
            mh::Note n;
            n.startPpq = ppq;
            n.lengthPpq = 0.1;
            n.noteNumber = note;
            n.velocity = velocity;
            n.channel = 10;
            clip.notes.push_back (n);
        };

        for (int bar = 0; bar < 2; ++bar)
        {
            const auto barStart = (double) bar * 4.0;

            add (barStart + 0.0, 36, 110);
            add (barStart + 2.0, 36, 104);
            add (barStart + 1.0, 38, 100);
            add (barStart + 3.0, 38, 102);

            for (int step = 0; step < 16; ++step)
                add (barStart + (double) step * 0.25, 42, (step % 4) == 0 ? 96 : 74);
        }

        clip.sortByTime();
        clip.assignSourceIds();
        return clip;
    }

    /** Applies one set of lane values to every lane, standing in for the master. */
    void setAllLanes (mh::HumanizeParams& p, const mh::LaneSettings& lane)
    {
        p.lane.fill (lane);
    }

    mh::LaneSettings busyLane()
    {
        mh::LaneSettings lane;
        lane.timingSpreadMs = 18.0f;
        lane.velocitySpread = 24.0f;
        lane.swingPercent = 40.0f;
        lane.accentAmount = 50.0f;
        lane.minGapMs = 20.0f;
        return lane;
    }

    mh::HumanizeParams makeBusyParams()
    {
        mh::HumanizeParams p;
        p.seed = 7;
        p.referenceBpm = 120.0;
        p.flamAmount = 40.0f;
        p.ghostAmount = 60.0f;
        p.variationAmount = 50.0f;
        p.handAlternation = 45.0f;
        p.limitsEnabled = true;
        p.chokeEnabled = true;
        setAllLanes (p, busyLane());
        return p;
    }

    /** Mean start position of one lane, for checking timing bias. */
    double meanStart (const mh::NoteSequence& clip, int noteNumber)
    {
        auto total = 0.0;
        auto count = 0;

        for (auto& n : clip.notes)
            if (n.noteNumber == noteNumber)
            {
                total += n.startPpq;
                ++count;
            }

        return count > 0 ? total / (double) count : 0.0;
    }

    double meanVelocity (const mh::NoteSequence& clip, int noteNumber)
    {
        auto total = 0.0;
        auto count = 0;

        for (auto& n : clip.notes)
            if (n.noteNumber == noteNumber)
            {
                total += (double) n.velocity;
                ++count;
            }

        return count > 0 ? total / (double) count : 0.0;
    }
}

//==============================================================================
static void testDeterminism()
{
    section ("Humanizer is deterministic");

    const auto source = makeTestBeat();
    const auto params = makeBusyParams();

    const auto first = mh::Humanizer::process (source, params);
    const auto second = mh::Humanizer::process (source, params);

    check (first.notes.size() == second.notes.size(), "same note count across runs");

    auto identical = first.notes.size() == second.notes.size();

    for (size_t i = 0; identical && i < first.notes.size(); ++i)
        identical = std::abs (first.notes[i].startPpq - second.notes[i].startPpq) < 1.0e-12
                 && first.notes[i].velocity == second.notes[i].velocity
                 && first.notes[i].noteNumber == second.notes[i].noteNumber;

    check (identical, "identical params produce a bit-identical clip");

    auto rerolled = params;
    rerolled.seed = 8;
    const auto different = mh::Humanizer::process (source, rerolled);

    auto anyDifference = false;

    for (size_t i = 0; i < juce::jmin (different.notes.size(), first.notes.size()); ++i)
        if (std::abs (different.notes[i].startPpq - first.notes[i].startPpq) > 1.0e-9)
            anyDifference = true;

    check (anyDifference, "a new seed deals a different performance");
}

static void testAmountScalesAFixedField()
{
    section ("Spread scales one random field rather than re-dealing it");

    const auto source = makeTestBeat();

    mh::HumanizeParams low;
    low.seed = 3;
    {
        mh::LaneSettings lane;
        lane.timingSpreadMs = 5.0f;
        setAllLanes (low, lane);
    }

    auto high = low;
    {
        mh::LaneSettings lane;
        lane.timingSpreadMs = 20.0f;
        setAllLanes (high, lane);
    }

    const auto lowResult = mh::Humanizer::process (source, low);
    const auto highResult = mh::Humanizer::process (source, high);

    check (lowResult.notes.size() == highResult.notes.size(), "note count unchanged by spread");

    // Every note must move in the same direction, just further: that is the property
    // which lets the grid preview morph smoothly instead of reshuffling.
    std::map<int, double> lowShift, highShift;

    for (auto& n : lowResult.notes)
        lowShift[n.sourceId] = n.startPpq;

    for (auto& n : highResult.notes)
        highShift[n.sourceId] = n.startPpq;

    auto sameDirection = true;
    auto strictlyDeeper = 0;

    for (auto& n : source.notes)
    {
        const auto lowDelta = lowShift[n.sourceId] - n.startPpq;
        const auto highDelta = highShift[n.sourceId] - n.startPpq;

        if (std::abs (lowDelta) < 1.0e-9)
            continue;

        if (lowDelta * highDelta < 0.0)
            sameDirection = false;

        if (std::abs (highDelta) > std::abs (lowDelta) + 1.0e-9)
            ++strictlyDeeper;
    }

    check (sameDirection, "no note flips direction when the spread is widened");
    check (strictlyDeeper > 0, "widening the spread deepens the deviations");
}

static void testInvariants()
{
    section ("Output invariants");

    const auto source = makeTestBeat();
    const auto params = makeBusyParams();
    const auto result = mh::Humanizer::process (source, params);

    auto velocitiesInRange = true;
    auto insideLoop = true;
    auto sorted = true;

    for (size_t i = 0; i < result.notes.size(); ++i)
    {
        const auto& n = result.notes[i];

        if (n.velocity < 1 || n.velocity > 127)
            velocitiesInRange = false;

        if (n.startPpq < 0.0 || n.startPpq >= result.lengthPpq)
            insideLoop = false;

        if (i > 0 && n.startPpq < result.notes[i - 1].startPpq - 1.0e-12)
            sorted = false;
    }

    check (velocitiesInRange, "every velocity stays within 1..127");
    check (insideLoop, "every note wraps into [0, clip length)");
    check (sorted, "output is sorted by time");

    // Playability: two hits on one drum can never be closer than the minimum gap,
    // except for the flam pairs that are deliberately exempt.
    const auto minGapPpq = (double) params.forNote (38).minGapMs * params.referenceBpm / 60000.0;
    std::map<int, double> lastOnLane;
    auto gapRespected = true;

    for (auto& n : result.notes)
    {
        if (n.isFlam)
            continue;

        const auto found = lastOnLane.find (n.noteNumber);

        if (found != lastOnLane.end() && n.startPpq - found->second < minGapPpq - 1.0e-9)
            gapRespected = false;

        lastOnLane[n.noteNumber] = n.startPpq;
    }

    check (gapRespected, "minimum hit gap is enforced per lane");

    mh::HumanizeParams disabled;
    disabled.enabled = false;
    const auto bypassed = mh::Humanizer::process (source, disabled);
    check (bypassed.notes.size() == source.notes.size(), "disabled humanizer passes the clip through");
}

static void testGhostNotes()
{
    section ("Ghost notes");

    const auto source = makeTestBeat();
    auto params = makeBusyParams();
    params.ghostAmount = 100.0f;
    params.ghostVelocity = 30;

    mh::HumanizeReport report;
    const auto result = mh::Humanizer::process (source, params, &report);

    check (report.ghostsAdded > 0, "ghosts are generated at full amount");
    check (result.notes.size() == source.notes.size() + (size_t) report.ghostsAdded,
           "reported ghost count matches the notes added");

    auto ghostsAreQuietAndOnSnare = true;

    for (auto& n : result.notes)
        if (n.isGhost && (mh::familyOf (n.noteNumber) != mh::DrumFamily::snare || n.velocity > 60))
            ghostsAreQuietAndOnSnare = false;

    check (ghostsAreQuietAndOnSnare, "ghosts land quietly on a snare lane");

    auto none = params;
    none.ghostAmount = 0.0f;
    mh::HumanizeReport quietReport;
    mh::Humanizer::process (source, none, &quietReport);
    check (quietReport.ghostsAdded == 0, "no ghosts at zero amount");
}

static void testMidiRoundTrip()
{
    section ("MIDI file round trip");

    const auto source = makeTestBeat();
    auto temp = juce::File::createTempFile (".mid");

    check (mh::MidiClipIO::save (source, temp, 120.0), "clip writes to disk");

    mh::NoteSequence reloaded;
    juce::String error;
    const auto loaded = mh::MidiClipIO::load (temp, reloaded, error);

    check (loaded, "clip reads back: " + error);

    if (loaded)
    {
        check (reloaded.notes.size() == source.notes.size(), "note count survives the round trip");
        check (std::abs (reloaded.lengthPpq - source.lengthPpq) < 1.0e-6, "clip length survives");

        auto positionsMatch = reloaded.notes.size() == source.notes.size();

        for (size_t i = 0; positionsMatch && i < source.notes.size(); ++i)
            positionsMatch = std::abs (reloaded.notes[i].startPpq - source.notes[i].startPpq) < 1.0e-3
                          && reloaded.notes[i].noteNumber == source.notes[i].noteNumber
                          && reloaded.notes[i].velocity == source.notes[i].velocity;

        check (positionsMatch, "note positions, pitches and velocities survive");
    }

    temp.deleteFile();
}

static void testDrumMapParsers()
{
    section ("Drum map parsers");

    const juce::String cubase = R"(<?xml version="1.0" encoding="utf-8"?>
<InstrumentMap>
  <string name="Name" value="Ferrari Kit"/>
  <list name="Map" type="list">
    <item>
      <string name="Name" value="Kick In"/>
      <int name="INote" value="36"/>
      <int name="ONote" value="36"/>
      <int name="Channel" value="-1"/>
    </item>
    <item>
      <string name="Name" value="Snare Top"/>
      <int name="INote" value="38"/>
      <int name="ONote" value="40"/>
      <int name="Channel" value="-1"/>
    </item>
    <item>
      <string name="Name" value="Hat Tip"/>
      <int name="INote" value="42"/>
      <int name="ONote" value="42"/>
      <int name="Channel" value="-1"/>
    </item>
  </list>
  <list name="Order" type="list">
    <item><int name="Item" value="2"/></item>
    <item><int name="Item" value="1"/></item>
    <item><int name="Item" value="0"/></item>
  </list>
</InstrumentMap>)";

    mh::DrumMapping cubaseMap;
    juce::String error;
    check (cubaseMap.loadFromText (cubase, "Ferrari Kit", error), "Cubase .drm parses: " + error);
    check (cubaseMap.getFormat() == mh::DrumMapping::Format::cubaseDrumMap, "detected as a Cubase map");
    check (cubaseMap.getEntries().size() == 3, "three rows parsed");
    check (cubaseMap.labelFor (38) == "Snare Top", "INote 38 is labelled from the map");
    check (cubaseMap.labelFor (49) == mh::drumName (49), "unmapped notes fall back to General MIDI");

    // The Order list puts the hat first, so it must sort above the kick.
    check (cubaseMap.sortKeyFor (42) < cubaseMap.sortKeyFor (36), "Cubase Order list drives row order");
    check (cubaseMap.sortKeyFor (36) < cubaseMap.sortKeyFor (49), "mapped rows sort above unmapped ones");

    const juce::String reaper = "# Reaper MIDI note names\n"
                                "36 Kick Drum\n"
                                "38 Snare 1\n"
                                "42 Hi-Hat Closed\n"
                                "\n"
                                "not a note line\n";

    mh::DrumMapping reaperMap;
    check (reaperMap.loadFromText (reaper, "reaper-kit", error), "Reaper note names parse: " + error);
    check (reaperMap.getFormat() == mh::DrumMapping::Format::reaperNoteNames, "detected as Reaper names");
    check (reaperMap.getEntries().size() == 3, "comments and junk lines are skipped");
    check (reaperMap.labelFor (42) == "Hi-Hat Closed", "multi-word names are kept intact");

    // State round trip, so a loaded map survives saving the project.
    mh::DrumMapping restored;
    restored.restoreFromValueTree (cubaseMap.toValueTree());
    check (restored.getEntries().size() == cubaseMap.getEntries().size(), "map survives plugin state");
    check (restored.labelFor (38) == "Snare Top", "labels survive plugin state");

    mh::DrumMapping rejected;
    check (! rejected.loadFromText ("<InstrumentMap></InstrumentMap>", "empty", error),
           "an empty map is rejected rather than silently accepted");
}

static void testClipPlayerNeverLeavesNotesStuck()
{
    section ("Clip player note bookkeeping");

    const auto source = makeTestBeat();
    const auto clip = mh::Humanizer::process (source, makeBusyParams());

    mh::ClipPlayer player;
    player.prepare (48000.0);

    mh::ClipPlayer::BlockContext context;
    context.sampleRate = 48000.0;
    context.numSamples = 256;
    context.bpm = 120.0;
    context.loopClip = true;
    context.transportRunning = true;

    std::map<std::pair<int, int>, int> sounding;
    auto noteOns = 0;
    auto negativeCount = false;
    auto badOffsets = false;

    const auto pump = [&] (int numBlocks, double startPpq)
    {
        auto ppq = startPpq;

        for (int block = 0; block < numBlocks; ++block)
        {
            context.ppqStart = ppq;
            juce::MidiBuffer out;
            player.renderBlock (clip, context, out);

            for (const auto metadata : out)
            {
                const auto message = metadata.getMessage();

                if (metadata.samplePosition < 0 || metadata.samplePosition >= context.numSamples)
                    badOffsets = true;

                const auto key = std::make_pair (message.getChannel(), message.getNoteNumber());

                if (message.isNoteOn())
                {
                    ++sounding[key];
                    ++noteOns;
                }
                else if (message.isNoteOff())
                {
                    if (--sounding[key] < 0)
                        negativeCount = true;
                }
            }

            ppq += (double) context.numSamples * context.bpm / (60.0 * context.sampleRate);
        }

        return ppq;
    };

    // Three passes through a two-bar loop, which forces several loop wraps.
    auto ppq = pump (900, 0.0);

    check (noteOns > 0, "notes are emitted while the transport runs");
    check (! badOffsets, "every event lands inside its block");
    check (! negativeCount, "no note-off is sent without a matching note-on");

    // Jump the timeline, as a locate would.
    ppq = pump (20, ppq + 37.5);

    // Stopping the transport must release everything.
    context.transportRunning = false;
    context.ppqStart = ppq;
    juce::MidiBuffer tail;
    player.renderBlock (clip, context, tail);

    for (const auto metadata : tail)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOff())
            --sounding[std::make_pair (message.getChannel(), message.getNoteNumber())];
    }

    auto stuck = 0;

    for (auto& [key, count] : sounding)
    {
        juce::ignoreUnused (key);

        if (count != 0)
            ++stuck;
    }

    check (stuck == 0, "stopping the transport leaves no note sounding (" + juce::String (stuck) + " stuck)");

    // A clip whose notes all vanish must still not leave anything hanging.
    mh::NoteSequence empty;
    context.transportRunning = true;
    context.ppqStart = 0.0;
    juce::MidiBuffer none;
    player.renderBlock (empty, context, none);
    check (true, "an empty clip renders without incident");
}

static void testClipPlayerHonoursOutputRouting()
{
    section ("Clip player output routing");

    mh::NoteSequence clip;
    clip.lengthPpq = 4.0;
    mh::Note n;
    n.startPpq = 0.0;
    n.lengthPpq = 0.5;
    n.noteNumber = 38;
    n.velocity = 100;
    n.channel = 10;
    clip.notes.push_back (n);

    mh::ClipPlayer player;
    player.prepare (48000.0);

    mh::ClipPlayer::BlockContext context;
    context.sampleRate = 48000.0;
    context.numSamples = 512;
    context.bpm = 120.0;
    context.transportRunning = true;
    context.loopClip = false;
    context.forcedChannel = 3;
    context.transpose = 2;
    context.ppqStart = 0.0;

    juce::MidiBuffer out;
    player.renderBlock (clip, context, out);

    auto sawRoutedNote = false;

    for (const auto metadata : out)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn() && message.getChannel() == 3 && message.getNoteNumber() == 40)
            sawRoutedNote = true;
    }

    check (sawRoutedNote, "forced channel and transpose are applied to the output");
}


static void testPerLaneIndependence()
{
    section ("Per-lane settings are independent");

    const auto source = makeTestBeat();

    mh::HumanizeParams p;
    p.seed = 11;
    p.referenceBpm = 120.0;

    // Only the snare is humanized; the kick and hats must come out untouched.
    mh::LaneSettings snare;
    snare.timingSpreadMs = 25.0f;
    snare.velocitySpread = 30.0f;
    p.lane[38] = snare;

    const auto result = mh::Humanizer::process (source, p);

    std::map<int, mh::Note> sourceById;

    for (auto& n : source.notes)
        sourceById[n.sourceId] = n;

    auto kickAndHatsUntouched = true;
    auto snareMoved = false;

    for (auto& n : result.notes)
    {
        const auto& original = sourceById[n.sourceId];

        if (n.noteNumber == 38)
        {
            if (std::abs (n.startPpq - original.startPpq) > 1.0e-9
                || n.velocity != original.velocity)
                snareMoved = true;
        }
        else if (std::abs (n.startPpq - original.startPpq) > 1.0e-9
                 || n.velocity != original.velocity)
        {
            kickAndHatsUntouched = false;
        }
    }

    check (snareMoved, "the lane that was given settings is humanized");
    check (kickAndHatsUntouched, "lanes with no settings are left exactly as written");
}

static void testDisabledLanePassesThrough()
{
    section ("A disabled lane passes through untouched");

    const auto source = makeTestBeat();
    auto p = makeBusyParams();

    // Everything is being humanized hard, except the kick.
    auto kick = p.forNote (36);
    kick.enabled = false;
    p.lane[36] = kick;

    const auto result = mh::Humanizer::process (source, p);

    std::map<int, mh::Note> sourceById;

    for (auto& n : source.notes)
        sourceById[n.sourceId] = n;

    auto kickUntouched = true;
    auto somethingElseMoved = false;

    for (auto& n : result.notes)
    {
        if (n.isGhost)
            continue;

        const auto& original = sourceById[n.sourceId];

        if (n.noteNumber == 36)
        {
            if (std::abs (n.startPpq - original.startPpq) > 1.0e-9 || n.velocity != original.velocity)
                kickUntouched = false;
        }
        else if (std::abs (n.startPpq - original.startPpq) > 1.0e-9)
        {
            somethingElseMoved = true;
        }
    }

    check (kickUntouched, "the disabled lane keeps its written timing and velocity");
    check (somethingElseMoved, "while the rest of the kit is still humanized");
}

static void testTimingBias()
{
    section ("Timing bias moves a lane's average");

    const auto source = makeTestBeat();
    const auto biasMs = 20.0;
    const auto expectedShift = biasMs * 120.0 / 60000.0;   // ms to quarter notes at 120 BPM

    mh::HumanizeParams p;
    p.seed = 5;
    p.referenceBpm = 120.0;
    p.limitsEnabled = false;

    mh::LaneSettings snare;
    snare.timingBiasMs = (float) biasMs;
    p.lane[38] = snare;

    const auto result = mh::Humanizer::process (source, p);

    const auto before = meanStart (source, 38);
    const auto after  = meanStart (result, 38);

    check (std::abs ((after - before) - expectedShift) < 1.0e-6,
           "a positive bias puts the lane exactly that far behind the beat");

    // And a negative bias pulls it early by the same amount.
    mh::LaneSettings early;
    early.timingBiasMs = (float) -biasMs;
    p.lane[38] = early;

    const auto pulled = mh::Humanizer::process (source, p);
    check (meanStart (pulled, 38) < before, "a negative bias plays ahead of the beat");

    check (std::abs (meanStart (result, 36) - meanStart (source, 36)) < 1.0e-9,
           "biasing one lane leaves the others where they were");
}

static void testVelocityTargetAndDynamics()
{
    section ("Velocity target sets a lane's average, dynamics keeps its accenting");

    // Give the snare a deliberately uneven set of levels to retarget.
    mh::NoteSequence clip;
    clip.lengthPpq = 4.0;

    const int writtenVelocities[] = { 60, 100, 80, 120 };

    for (int i = 0; i < 4; ++i)
    {
        mh::Note n;
        n.startPpq = (double) i;
        n.lengthPpq = 0.1;
        n.noteNumber = 38;
        n.velocity = writtenVelocities[i];
        n.channel = 10;
        clip.notes.push_back (n);
    }

    clip.sortByTime();
    clip.assignSourceIds();

    mh::HumanizeParams p;
    p.seed = 21;
    p.referenceBpm = 120.0;
    p.limitsEnabled = false;

    // Dynamics at 0: every hit should sit on the target.
    mh::LaneSettings flat;
    flat.velocityTarget = 100;
    flat.velocityDynamics = 0.0f;
    p.lane[38] = flat;

    const auto flattened = mh::Humanizer::process (clip, p);
    auto allOnTarget = true;

    for (auto& n : flattened.notes)
        if (n.velocity != 100)
            allOnTarget = false;

    check (allOnTarget, "at 0% dynamics every hit lands on the target");

    // Dynamics at 100: the written accenting survives, re-centred on the target.
    mh::LaneSettings dynamic;
    dynamic.velocityTarget = 100;
    dynamic.velocityDynamics = 100.0f;
    p.lane[38] = dynamic;

    const auto preserved = mh::Humanizer::process (clip, p);

    check (std::abs (meanVelocity (preserved, 38) - 100.0) < 1.0,
           "the lane's average is moved onto the target");

    // Source mean is 90, so each hit keeps its distance from that mean once the lane is
    // re-centred on 100. The loudest hit lands on 130 and saturates at 127, which is the
    // correct and unavoidable consequence of asking for a high target with full dynamics.
    auto spacingPreserved = preserved.notes.size() == clip.notes.size();

    for (size_t i = 0; spacingPreserved && i < preserved.notes.size(); ++i)
    {
        const auto expected = juce::jlimit (1.0, 127.0, 100.0 + (double) writtenVelocities[i] - 90.0);
        spacingPreserved = std::abs ((double) preserved.notes[i].velocity - expected) < 1.5;
    }

    check (spacingPreserved, "at 100% dynamics the written accents are kept intact");

    // A target of zero must leave the recorded levels completely alone.
    mh::LaneSettings off;
    off.velocityTarget = 0;
    p.lane[38] = off;

    const auto untouched = mh::Humanizer::process (clip, p);
    auto levelsKept = true;

    for (size_t i = 0; i < untouched.notes.size(); ++i)
        if (untouched.notes[i].velocity != writtenVelocities[i])
            levelsKept = false;

    check (levelsKept, "a target of Off keeps the clip's own velocities");
}

static void testVelocitySpreadCentresOnTarget()
{
    section ("Velocity spread wanders around the target, not away from it");

    mh::NoteSequence clip;
    clip.lengthPpq = 8.0;

    for (int i = 0; i < 32; ++i)
    {
        mh::Note n;
        n.startPpq = (double) i * 0.25;
        n.lengthPpq = 0.1;
        n.noteNumber = 42;
        n.velocity = 30;          // deliberately far from the target we will ask for
        n.channel = 10;
        clip.notes.push_back (n);
    }

    clip.sortByTime();
    clip.assignSourceIds();

    mh::HumanizeParams p;
    p.seed = 33;
    p.referenceBpm = 120.0;
    p.limitsEnabled = false;

    mh::LaneSettings lane;
    lane.velocityTarget = 100;
    lane.velocityDynamics = 0.0f;
    lane.velocitySpread = 15.0f;
    p.lane[42] = lane;

    const auto result = mh::Humanizer::process (clip, p);

    const auto mean = meanVelocity (result, 42);
    check (std::abs (mean - 100.0) < 6.0,
           "the average stays on the target (got " + juce::String (mean, 1) + ")");

    auto low = 127, high = 1;

    for (auto& n : result.notes)
    {
        low = juce::jmin (low, n.velocity);
        high = juce::jmax (high, n.velocity);
    }

    check (high > low, "the spread actually varies the hits");
    check (low >= 100 - 16 && high <= 100 + 16,
           "and stays inside the requested spread (" + juce::String (low) + ".." + juce::String (high) + ")");
}

static void testLaneSettingsState()
{
    section ("Lane settings survive plugin state");

    mh::LaneSettingsTable table;

    mh::LaneSettings snare;
    snare.hasOverride = true;
    snare.timingBiasMs = 6.5f;
    snare.velocityTarget = 104;
    snare.velocitySpread = 9.0f;
    table.set (38, snare);

    auto kick = table.get (36);
    kick.enabled = false;
    table.set (36, kick);

    mh::LaneSettingsTable restored;
    restored.restoreFromValueTree (table.toValueTree());

    check (restored.get (38).hasOverride, "an overriding lane is stored");
    check (std::abs (restored.get (38).timingBiasMs - 6.5f) < 1.0e-4, "its bias survives");
    check (restored.get (38).velocityTarget == 104, "its velocity target survives");
    check (! restored.get (36).enabled, "a disabled lane is stored even without an override");
    check (! restored.get (42).hasOverride, "untouched lanes stay on the master");

    // Copying one lane over the rest is how a whole kit gets set up quickly.
    table.copyToAll (38, true);
    check (table.get (42).hasOverride && table.get (42).velocityTarget == 104,
           "copy-to-all carries the numbers across");
    check (! table.get (36).enabled, "copy-to-all leaves each lane's own on/off alone");
}


static void testLaneLinking()
{
    section ("Linked lanes are edited as one");

    mh::LaneSettingsTable table;

    const std::vector<int> toms { 41, 45, 47, 48 };
    const auto group = table.linkNotes (toms);

    check (group > 0, "linking returns a group id");

    auto allInGroup = true;

    for (auto note : toms)
        if (table.get (note).groupId != group)
            allInGroup = false;

    check (allInGroup, "every linked lane carries the group id");
    check (table.notesInGroup (group).size() == toms.size(), "the group reports all its members");
    check (table.get (38).groupId == 0, "unrelated lanes are untouched");

    // Editing one member and syncing must carry the values to the whole group.
    auto lead = table.get (47);
    lead.hasOverride = true;
    lead.timingBiasMs = 9.0f;
    lead.velocityTarget = 96;
    table.set (47, lead);
    table.syncGroupFrom (47);

    auto sharedValues = true;

    for (auto note : toms)
    {
        const auto lane = table.get (note);

        if (std::abs (lane.timingBiasMs - 9.0f) > 1.0e-4 || lane.velocityTarget != 96
            || ! lane.hasOverride)
            sharedValues = false;
    }

    check (sharedValues, "an edit to one member reaches every member");

    // Each lane keeps its own on/off state even inside a group.
    auto quietTom = table.get (45);
    quietTom.enabled = false;
    table.set (45, quietTom);
    table.syncGroupFrom (47);

    check (! table.get (45).enabled, "a group does not overwrite a member's own on/off state");
    check (table.get (47).enabled, "and the others stay enabled");

    // Resetting values must not silently break the link.
    table.reset (41);
    check (table.get (41).groupId == group, "resetting a lane's values keeps it linked");

    // Linking a lane that is already grouped extends that group rather than splitting it.
    const auto extended = table.linkNotes ({ 43, 47 });
    check (table.notesInGroup (extended).size() == toms.size() + 1,
           "linking into an existing group absorbs its members");

    // Unlinking, and the rule that a group of one is no group.
    table.unlinkNotes ({ 41, 45, 47 });
    const auto remaining = table.notesInGroup (extended);
    check (remaining.size() != 1, "a group left with one member is dissolved");

    // Group membership has to survive a project save.
    mh::LaneSettingsTable fresh;
    fresh.linkNotes ({ 49, 51, 57 });

    mh::LaneSettingsTable restored;
    restored.restoreFromValueTree (fresh.toValueTree());

    check (restored.get (49).groupId == fresh.get (49).groupId, "group ids survive plugin state");
    check (restored.notesInGroup (restored.get (49).groupId).size() == 3,
           "and the whole group comes back");

    // Copy-to-all must not smear one group id across the entire kit.
    mh::LaneSettingsTable copied;
    copied.linkNotes ({ 41, 45 });
    auto source = copied.get (38);
    source.hasOverride = true;
    source.timingBiasMs = 4.0f;
    copied.set (38, source);
    copied.copyToAll (38, true);

    check (copied.get (41).groupId == copied.get (45).groupId && copied.get (41).groupId > 0,
           "copy-to-all leaves existing links intact");
    check (copied.get (36).groupId == 0, "and does not invent new ones");
}


static void testGrooveExtraction()
{
    section ("Groove extraction measures a performance");

    // A synthetic "played" take: the snare sits a known 12 ms late with a known spread,
    // so what the extractor reports can be checked against the truth rather than eyeballed.
    constexpr auto bpm = 120.0;
    constexpr auto msToPpq = bpm / 60000.0;
    constexpr auto biasMs = 12.0;

    const double offsetsMs[] = { 8.0, 16.0, 10.0, 14.0, 6.0, 18.0, 12.0, 12.0 };

    mh::NoteSequence played;
    played.lengthPpq = 8.0;
    played.sourceBpm = bpm;

    for (int i = 0; i < (int) juce::numElementsInArray (offsetsMs); ++i)
    {
        mh::Note n;
        n.startPpq = (double) i * 1.0 + offsetsMs[i] * msToPpq;   // on beats, but late
        n.lengthPpq = 0.1;
        n.noteNumber = 38;
        n.velocity = 90 + (i % 2 == 0 ? -6 : 6);
        n.channel = 10;
        played.notes.push_back (n);
    }

    played.sortByTime();
    played.assignSourceIds();

    // Grid index 2 is 1/4, which is where these hits nominally sit.
    const auto analysis = mh::GrooveExtractor::analyse (played, 2, bpm);

    check (analysis.lanes.size() == 1, "one lane measured");
    check (analysis.totalHits == 8, "every hit counted");

    const auto* lane = analysis.find (38);
    check (lane != nullptr, "the snare lane is found");

    if (lane != nullptr)
    {
        check (std::abs (lane->meanDeviationMs - biasMs) < 0.5,
               "mean deviation matches the bias that was built in ("
                   + juce::String (lane->meanDeviationMs, 2) + " ms)");
        check (lane->deviationSpreadMs > 3.0 && lane->deviationSpreadMs < 5.0,
               "spread reflects the scatter (" + juce::String (lane->deviationSpreadMs, 2) + " ms)");
        check (std::abs (lane->meanVelocity - 90.0) < 0.5, "mean velocity measured");

        // Applying the measurement must reproduce the bias it measured.
        mh::LaneSettings settings;
        mh::GrooveExtractor::applyTo (settings, *lane);

        check (settings.hasOverride, "applying makes the lane override the master");
        check (std::abs (settings.timingBiasMs - (float) biasMs) < 0.5f,
               "the lane's bias is set from the measurement");
        check (settings.velocityTarget == 90, "the lane's velocity target is the measured average");

        // Spread is a triangular peak, so it must exceed the standard deviation it came
        // from, otherwise the humanized result would be noticeably tighter than the take.
        check (settings.timingSpreadMs > (float) lane->deviationSpreadMs,
               "standard deviation is converted to a peak, not used raw");
    }

    // A perfectly quantized clip has nothing to teach: bias and spread should be ~zero.
    mh::NoteSequence machine;
    machine.lengthPpq = 4.0;

    for (int i = 0; i < 4; ++i)
    {
        mh::Note n;
        n.startPpq = (double) i;
        n.lengthPpq = 0.1;
        n.noteNumber = 36;
        n.velocity = 100;
        machine.notes.push_back (n);
    }

    machine.assignSourceIds();
    const auto flat = mh::GrooveExtractor::analyse (machine, 2, bpm);
    const auto* kick = flat.find (36);

    check (kick != nullptr && std::abs (kick->meanDeviationMs) < 1.0e-6,
           "a quantized clip measures as zero bias");
    check (kick != nullptr && kick->deviationSpreadMs < 1.0e-6,
           "and as zero spread");
}

static void testEditHistory()
{
    section ("Undo history");

    mh::EditHistory history;

    check (! history.canUndo(), "nothing to undo when empty");
    check (! history.canRedo(), "nothing to redo when empty");

    const auto state = [] (int tag)
    {
        juce::ValueTree tree ("State");
        tree.setProperty ("tag", tag, nullptr);
        return tree;
    };

    // The stack records the state after each change, seeded with the opening state.
    history.push (state (0));
    check (! history.canUndo(), "the seed state alone is not an undo step");

    history.push (state (1));
    history.push (state (2));

    check (history.canUndo(), "changes can be undone");

    auto back = history.undo();
    check ((int) back.getProperty ("tag") == 1, "undo steps back one state");

    back = history.undo();
    check ((int) back.getProperty ("tag") == 0, "and again");
    check (! history.canUndo(), "stopping at the opening state");

    auto forward = history.redo();
    check ((int) forward.getProperty ("tag") == 1, "redo steps forward");

    // Editing after an undo discards the branch that had been undone.
    history.push (state (99));
    check (! history.canRedo(), "editing after undo drops the redo branch");

    back = history.undo();
    check ((int) back.getProperty ("tag") == 1, "and the new edit sits on the older state");

    history.clear();
    check (! history.canUndo() && ! history.canRedo(), "clearing empties the history");
}

static void testPerLaneSeed()
{
    section ("Per-lane re-roll");

    const auto source = makeTestBeat();

    mh::HumanizeParams base;
    base.seed = 4;
    base.referenceBpm = 120.0;
    base.limitsEnabled = false;
    {
        mh::LaneSettings lane;
        lane.timingSpreadMs = 20.0f;
        setAllLanes (base, lane);
    }

    const auto before = mh::Humanizer::process (source, base);

    // Re-rolling one lane must leave every other lane's performance untouched.
    auto rerolled = base;
    rerolled.lane[38].seedOffset = 1;
    const auto after = mh::Humanizer::process (source, rerolled);

    std::map<int, double> beforeById, afterById;

    for (auto& n : before.notes)
        beforeById[n.sourceId] = n.startPpq;

    for (auto& n : after.notes)
        afterById[n.sourceId] = n.startPpq;

    auto snareChanged = false;
    auto othersIdentical = true;

    for (auto& n : source.notes)
    {
        const auto moved = std::abs (afterById[n.sourceId] - beforeById[n.sourceId]) > 1.0e-9;

        if (n.noteNumber == 38)
        {
            if (moved)
                snareChanged = true;
        }
        else if (moved)
        {
            othersIdentical = false;
        }
    }

    check (snareChanged, "the re-rolled lane gets a different performance");
    check (othersIdentical, "every other lane is left exactly as it was");

    // A zero offset must be indistinguishable from no offset at all.
    auto zeroed = base;
    zeroed.lane[38].seedOffset = 0;
    const auto unchanged = mh::Humanizer::process (source, zeroed);

    auto identical = unchanged.notes.size() == before.notes.size();

    for (size_t i = 0; identical && i < unchanged.notes.size(); ++i)
        identical = std::abs (unchanged.notes[i].startPpq - before.notes[i].startPpq) < 1.0e-12;

    check (identical, "a zero seed offset changes nothing");
}

static void testMuteSoloAndOutputMapping()
{
    section ("Mute, solo and output note mapping");

    const auto source = makeTestBeat();

    mh::HumanizeParams p;
    p.seed = 6;
    p.referenceBpm = 120.0;

    // Muting a lane must silence it while leaving it in the clip to be drawn.
    p.lane[36].muted = true;
    auto result = mh::Humanizer::process (source, p);

    auto kickPresent = false;
    auto kickAudible = false;
    auto othersAudible = true;

    for (auto& n : result.notes)
    {
        if (n.noteNumber == 36)
        {
            kickPresent = true;

            if (n.audible)
                kickAudible = true;
        }
        else if (! n.audible)
        {
            othersAudible = false;
        }
    }

    check (kickPresent, "a muted lane stays in the clip so it can still be shown");
    check (! kickAudible, "but none of its notes are audible");
    check (othersAudible, "and nothing else is affected");

    // Soloing silences everything that is not soloed.
    mh::HumanizeParams solo;
    solo.seed = 6;
    solo.referenceBpm = 120.0;
    solo.lane[42].soloed = true;
    solo.anyLaneSoloed = true;

    result = mh::Humanizer::process (source, solo);

    auto hatAudible = false;
    auto anythingElseAudible = false;

    for (auto& n : result.notes)
    {
        if (n.noteNumber == 42)
        {
            if (n.audible)
                hatAudible = true;
        }
        else if (n.audible)
        {
            anythingElseAudible = true;
        }
    }

    check (hatAudible, "the soloed lane sounds");
    check (! anythingElseAudible, "and everything else is silenced");

    // A drum map's output note is emitted while the display note stays put.
    mh::HumanizeParams mapped;
    mapped.seed = 6;
    mapped.referenceBpm = 120.0;
    mapped.outputNote[38] = 40;

    result = mh::Humanizer::process (source, mapped);

    auto remapped = false;
    auto laneUnchanged = true;

    for (auto& n : result.notes)
        if (n.noteNumber == 38)
        {
            if (n.emittedNote() == 40)
                remapped = true;
            else
                laneUnchanged = false;
        }

    check (remapped, "the mapped lane emits its output note");
    check (laneUnchanged, "while every note keeps the lane it is drawn on");
}

static void testPerLaneStats()
{
    section ("Per-lane measurements");

    const auto source = makeTestBeat();

    mh::HumanizeParams p;
    p.seed = 9;
    p.referenceBpm = 120.0;
    p.limitsEnabled = false;

    // A known target with no dynamics and no spread has an exactly predictable outcome.
    mh::LaneSettings snare;
    snare.velocityTarget = 104;
    snare.velocityDynamics = 0.0f;
    snare.timingBiasMs = 10.0f;
    p.lane[38] = snare;

    mh::HumanizeReport report;
    mh::Humanizer::process (source, p, &report);

    const auto& stats = report.forNote (38);

    check (stats.hits == 4, "the lane's hits are counted");
    check (std::abs (stats.meanVelocity - 104.0) < 0.01,
           "the measured average matches the target that was asked for");
    check (stats.minVelocity == 104 && stats.maxVelocity == 104,
           "with no spread every hit is exactly on target");
    check (std::abs (stats.meanShiftMs - 10.0) < 0.01,
           "the measured shift matches the bias (" + juce::String (stats.meanShiftMs, 2) + " ms)");

    const auto& untouched = report.forNote (36);
    check (untouched.hits > 0, "other lanes are measured too");
    check (std::abs (untouched.meanShiftMs) < 1.0e-6, "and report no shift when unhumanized");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testDeterminism();
    testAmountScalesAFixedField();
    testPerLaneIndependence();
    testDisabledLanePassesThrough();
    testTimingBias();
    testVelocityTargetAndDynamics();
    testVelocitySpreadCentresOnTarget();
    testLaneSettingsState();
    testLaneLinking();
    testGrooveExtraction();
    testEditHistory();
    testPerLaneSeed();
    testMuteSoloAndOutputMapping();
    testPerLaneStats();
    testInvariants();
    testGhostNotes();
    testMidiRoundTrip();
    testDrumMapParsers();
    testClipPlayerNeverLeavesNotesStuck();
    testClipPlayerHonoursOutputRouting();

    std::cout << std::endl
              << (failures == 0 ? "ALL PASSED" : "FAILURES") << ": "
              << (checks - failures) << "/" << checks << " checks passed" << std::endl;

    return failures == 0 ? 0 : 1;
}
