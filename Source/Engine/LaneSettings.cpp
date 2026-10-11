// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "LaneSettings.h"

namespace mh
{

namespace
{
    constexpr auto laneTreeType = "LaneSettings";
    constexpr auto laneRowType  = "Lane";
}

void LaneSettingsTable::copyToAll (int fromNote, bool includeUntouched)
{
    const auto source = get (fromNote);

    for (int note = 0; note < numLanes; ++note)
    {
        if (note == fromNote)
            continue;

        auto& target = lanes[(size_t) note];

        if (! includeUntouched && ! target.hasOverride)
            continue;

        // The lane's own enablement is a separate decision from its numbers, so it is
        // deliberately not carried across.
        const auto own = target;
        target = source;
        target.enabled    = own.enabled;
        target.groupId    = own.groupId;
        target.muted      = own.muted;
        target.soloed     = own.soloed;
        target.seedOffset = own.seedOffset;
        target.hasOverride = true;
    }
}

std::vector<int> LaneSettingsTable::notesInGroup (int groupId) const
{
    std::vector<int> notes;

    if (groupId == 0)
        return notes;

    for (int note = 0; note < numLanes; ++note)
        if (lanes[(size_t) note].groupId == groupId)
            notes.push_back (note);

    return notes;
}

int LaneSettingsTable::nextFreeGroupId() const
{
    for (int candidate = 1; candidate < numLanes; ++candidate)
        if (notesInGroup (candidate).empty())
            return candidate;

    return 1;
}

int LaneSettingsTable::linkNotes (const std::vector<int>& notes)
{
    if (notes.size() < 2)
        return 0;

    // Absorb any group the incoming notes already belong to, so linking a tom to a
    // group of toms extends that group instead of orphaning its other members.
    std::vector<int> members;

    const auto addMember = [&members] (int note)
    {
        if (std::find (members.begin(), members.end(), note) == members.end())
            members.push_back (note);
    };

    for (auto note : notes)
    {
        const auto clamped = juce::jlimit (0, numLanes - 1, note);
        addMember (clamped);

        for (auto existing : notesInGroup (lanes[(size_t) clamped].groupId))
            addMember (existing);
    }

    const auto groupId = nextFreeGroupId();

    for (auto note : members)
        lanes[(size_t) note].groupId = groupId;

    // A group speaks with one voice, so the members start from the same values.
    syncGroupFrom (members.front());
    return groupId;
}

void LaneSettingsTable::unlinkNotes (const std::vector<int>& notes)
{
    std::vector<int> touchedGroups;

    for (auto note : notes)
    {
        const auto clamped = juce::jlimit (0, numLanes - 1, note);
        const auto group = lanes[(size_t) clamped].groupId;

        if (group == 0)
            continue;

        if (std::find (touchedGroups.begin(), touchedGroups.end(), group) == touchedGroups.end())
            touchedGroups.push_back (group);

        lanes[(size_t) clamped].groupId = 0;
    }

    // A group of one is not a group.
    for (auto group : touchedGroups)
    {
        const auto remaining = notesInGroup (group);

        if (remaining.size() == 1)
            lanes[(size_t) remaining.front()].groupId = 0;
    }
}

void LaneSettingsTable::syncGroupFrom (int noteNumber)
{
    const auto source = get (noteNumber);

    if (source.groupId == 0)
        return;

    for (auto note : notesInGroup (source.groupId))
    {
        if (note == noteNumber)
            continue;

        // Only the humanization numbers are shared. Enablement, playback state and the
        // per-lane seed stay each lane's own business.
        const auto own = lanes[(size_t) note];
        lanes[(size_t) note] = source;
        lanes[(size_t) note].enabled    = own.enabled;
        lanes[(size_t) note].muted      = own.muted;
        lanes[(size_t) note].soloed     = own.soloed;
        lanes[(size_t) note].seedOffset = own.seedOffset;
    }
}

bool LaneSettingsTable::anySoloed() const
{
    return std::any_of (lanes.begin(), lanes.end(), [] (const LaneSettings& l) { return l.soloed; });
}

void LaneSettingsTable::rerollLanes (const std::vector<int>& notes, int salt)
{
    for (auto note : notes)
    {
        auto& lane = lanes[(size_t) juce::jlimit (0, numLanes - 1, note)];

        // Any change of offset re-deals this lane's hand; the salt just keeps successive
        // presses from landing back on a value already seen.
        lane.seedOffset = (lane.seedOffset + 1 + salt) & 0xffff;
    }
}

juce::ValueTree LaneSettingsTable::toValueTree() const
{
    juce::ValueTree tree (laneTreeType);

    for (int note = 0; note < numLanes; ++note)
    {
        const auto& lane = lanes[(size_t) note];

        // Only lanes that say something are stored; the rest are the defaults.
        if (lane.isDefault())
            continue;

        juce::ValueTree row (laneRowType);
        row.setProperty ("note", note, nullptr);
        row.setProperty ("ovr", lane.hasOverride, nullptr);
        row.setProperty ("on", lane.enabled, nullptr);
        row.setProperty ("group", lane.groupId, nullptr);
        row.setProperty ("mute", lane.muted, nullptr);
        row.setProperty ("solo", lane.soloed, nullptr);
        row.setProperty ("seedOff", lane.seedOffset, nullptr);
        row.setProperty ("tBias", lane.timingBiasMs, nullptr);
        row.setProperty ("tSpread", lane.timingSpreadMs, nullptr);
        row.setProperty ("swing", lane.swingPercent, nullptr);
        row.setProperty ("vTarget", lane.velocityTarget, nullptr);
        row.setProperty ("vDyn", lane.velocityDynamics, nullptr);
        row.setProperty ("vSpread", lane.velocitySpread, nullptr);
        row.setProperty ("vSpreadUp", lane.velocitySpreadUpOnly, nullptr);
        row.setProperty ("accent", lane.accentAmount, nullptr);
        row.setProperty ("minGap", lane.minGapMs, nullptr);
        tree.appendChild (row, nullptr);
    }

    return tree;
}

void LaneSettingsTable::restoreFromValueTree (const juce::ValueTree& tree)
{
    resetAll();

    if (! tree.hasType (laneTreeType))
        return;

    for (auto row : tree)
    {
        if (! row.hasType (laneRowType))
            continue;

        const auto note = juce::jlimit (0, numLanes - 1, (int) row.getProperty ("note", 0));

        LaneSettings lane;
        lane.hasOverride      = (bool)  row.getProperty ("ovr", false);
        lane.enabled          = (bool)  row.getProperty ("on", true);
        lane.groupId          = juce::jmax (0, (int) row.getProperty ("group", 0));
        lane.muted            = (bool)  row.getProperty ("mute", false);
        lane.soloed           = (bool)  row.getProperty ("solo", false);
        lane.seedOffset       = juce::jmax (0, (int) row.getProperty ("seedOff", 0));
        lane.timingBiasMs     = (float) row.getProperty ("tBias", 0.0);
        lane.timingSpreadMs   = (float) row.getProperty ("tSpread", 0.0);
        lane.swingPercent     = (float) row.getProperty ("swing", 0.0);
        lane.velocityTarget   = juce::jlimit (0, 127, (int) row.getProperty ("vTarget", 0));
        lane.velocityDynamics = (float) row.getProperty ("vDyn", 0.0);
        lane.velocitySpread   = (float) row.getProperty ("vSpread", 0.0);
        lane.velocitySpreadUpOnly = (bool) row.getProperty ("vSpreadUp", false);
        lane.accentAmount     = (float) row.getProperty ("accent", 0.0);
        lane.minGapMs         = (float) row.getProperty ("minGap", 20.0);

        lanes[(size_t) note] = lane;
    }
}

} // namespace mh
