// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cmath>
#include <vector>

namespace mh
{

/** Humanization settings for one drum lane.

    The same struct carries the master values and each lane's overrides, so resolving
    inheritance is a straight copy rather than a field-by-field merge.

    Timing and velocity are both expressed as **bias plus spread** rather than as an
    amount scaling a range: the bias is where the lane sits on average, the spread is how
    far individual hits wander either side of it. That maps directly onto how a drummer
    is described — "the snare is a few milliseconds late and fairly loose" — and it means
    the two controls are independent instead of multiplying together.
*/
struct LaneSettings
{
    /** Numeric values come from the master unless this is set. Independent of `enabled`,
        so a lane can be excluded from humanization without also having to own a full set
        of values.
    */
    bool  hasOverride = false;

    /** Lanes sharing a non-zero group id are edited as one: a change to any member is
        written to all of them. Zero means the lane stands alone.

        Grouping is deliberately an edit-time and display-time relationship only. The
        members still each hold their own copy of the values, so the humanizer resolves
        lanes exactly as it did before and knows nothing about groups.
    */
    int   groupId = 0;

    /** When false the lane passes through with its timing and velocity untouched.
        Structural rules that protect the instrument (minimum hit gap, choke groups) still
        apply, because those describe what a kit can physically do.
    */
    bool  enabled = true;

    /** Playback state, kept separate from `enabled` because they answer different
        questions: `enabled` is "humanize this drum", while these are "let me hear it".
        Muted and soloed lanes still appear in the grid, drawn hollow, so notes never seem
        to vanish.
    */
    bool  muted = false;
    bool  soloed = false;

    /** Shifts this lane's random field without touching any other lane, so one drum can
        be re-rolled while the rest of the kit keeps the performance you already liked.
        Zero leaves the lane on the global seed.
    */
    int   seedOffset = 0;

    // ---- timing ------------------------------------------------------------------
    float timingBiasMs   = 0.0f;    ///< -60..+60; negative plays ahead of the beat
    float timingSpreadMs = 0.0f;    ///< 0..60; peak wander either side of the bias
    float swingPercent   = 0.0f;    ///< -100..100; +100 puts off-beats on a triplet

    // ---- velocity ----------------------------------------------------------------
    /** 0 leaves the lane's recorded levels alone. 1..127 retargets the lane so its
        average velocity becomes this value.
    */
    int   velocityTarget = 0;

    /** 0..100. How much of the source's own accenting survives a retarget. At 0 every
        hit sits on the target; at 100 the written dynamics are kept intact and merely
        re-centred on it. Irrelevant when velocityTarget is 0.
    */
    float velocityDynamics = 0.0f;

    float velocitySpread = 0.0f;    ///< 0..63; peak wander either side of the level
    float accentAmount   = 0.0f;    ///< 0..100; lifts hits on strong grid positions

    // ---- playability --------------------------------------------------------------
    float minGapMs = 20.0f;         ///< this drum cannot be struck twice inside this

    /** True when the lane would leave every note exactly where it is. Used to skip work
        and to decide whether a lane is worth flagging in the UI.
    */
    bool isNeutral() const noexcept
    {
        return ! enabled
            || (timingBiasMs == 0.0f && timingSpreadMs == 0.0f && swingPercent == 0.0f
                && velocityTarget == 0 && velocitySpread == 0.0f && accentAmount == 0.0f);
    }

    /** True when nothing about this lane differs from a freshly defaulted one. */
    bool isDefault() const noexcept
    {
        return ! hasOverride && enabled && ! muted && ! soloed
            && groupId == 0 && seedOffset == 0;
    }
};

/** Copies the fields that describe the lane *itself* rather than its humanization values.

    These must never be inherited from the master: whether a drum is linked, muted, soloed,
    excluded from humanization or sitting on its own seed is a property of that drum, not a
    value it borrows. Resolving inheritance field-by-field is what let mute and group
    membership go missing on lanes that had no override, so both resolution paths go
    through here instead.
*/
inline void copyLaneIdentity (LaneSettings& target, const LaneSettings& stored)
{
    target.hasOverride = stored.hasOverride;
    target.enabled     = stored.enabled;
    target.muted       = stored.muted;
    target.soloed      = stored.soloed;
    target.groupId     = stored.groupId;
    target.seedOffset  = stored.seedOffset;
}

//==============================================================================
/** The editable numeric fields of a lane, so the UI can drive them from one table
    instead of eight near-identical blocks of code.
*/
enum class LaneField
{
    timingBias = 0,
    timingSpread,
    swing,
    velocityTarget,
    velocityDynamics,
    velocitySpread,
    accent,
    minGap,
    numFields
};

constexpr int numLaneFields = (int) LaneField::numFields;

inline double getLaneField (const LaneSettings& lane, LaneField field)
{
    switch (field)
    {
        case LaneField::timingBias:       return lane.timingBiasMs;
        case LaneField::timingSpread:     return lane.timingSpreadMs;
        case LaneField::swing:            return lane.swingPercent;
        case LaneField::velocityTarget:   return lane.velocityTarget;
        case LaneField::velocityDynamics: return lane.velocityDynamics;
        case LaneField::velocitySpread:   return lane.velocitySpread;
        case LaneField::accent:           return lane.accentAmount;
        case LaneField::minGap:           return lane.minGapMs;
        case LaneField::numFields:        break;
    }

    return 0.0;
}

inline void setLaneField (LaneSettings& lane, LaneField field, double value)
{
    switch (field)
    {
        case LaneField::timingBias:       lane.timingBiasMs     = (float) value; return;
        case LaneField::timingSpread:     lane.timingSpreadMs   = (float) value; return;
        case LaneField::swing:            lane.swingPercent     = (float) value; return;
        case LaneField::velocityTarget:   lane.velocityTarget   = (int) std::lround (value); return;
        case LaneField::velocityDynamics: lane.velocityDynamics = (float) value; return;
        case LaneField::velocitySpread:   lane.velocitySpread   = (float) value; return;
        case LaneField::accent:           lane.accentAmount     = (float) value; return;
        case LaneField::minGap:           lane.minGapMs         = (float) value; return;
        case LaneField::numFields:        break;
    }
}

//==============================================================================
/** Per-lane overrides for all 128 note numbers.

    Only lanes that actually differ from the master are written to plugin state, so a
    project file does not carry 128 copies of the defaults.
*/
class LaneSettingsTable
{
public:
    LaneSettingsTable() = default;

    static constexpr int numLanes = 128;

    const LaneSettings& get (int noteNumber) const noexcept
    {
        return lanes[(size_t) juce::jlimit (0, numLanes - 1, noteNumber)];
    }

    void set (int noteNumber, const LaneSettings& settings)
    {
        lanes[(size_t) juce::jlimit (0, numLanes - 1, noteNumber)] = settings;
    }

    void reset (int noteNumber)
    {
        // Resetting a lane's values must not silently break a link the user set up.
        const auto group = get (noteNumber).groupId;
        LaneSettings fresh;
        fresh.groupId = group;
        set (noteNumber, fresh);
    }

    void resetAll() { lanes.fill ({}); }

    /** Copies one lane's numeric values onto every other lane that already overrides,
        or onto all lanes when `includeUntouched` is set.
    */
    void copyToAll (int fromNote, bool includeUntouched);

    // ---- linking ------------------------------------------------------------------
    /** Every note number in a group, in ascending order. Empty for group zero. */
    std::vector<int> notesInGroup (int groupId) const;

    /** Lowest group id not currently in use, for forming a new group. */
    int nextFreeGroupId() const;

    /** Puts all of `notes` into one new group and returns its id. Any group the notes
        already belonged to is absorbed, so linking a lane that is already linked extends
        the existing group rather than splitting it.
    */
    int linkNotes (const std::vector<int>& notes);

    /** Removes these notes from whatever group they are in. A group left with a single
        member is dissolved, since a group of one has no meaning.
    */
    void unlinkNotes (const std::vector<int>& notes);

    /** Copies one member's values onto every other member of its group. */
    void syncGroupFrom (int noteNumber);

    /** True when at least one lane is soloed, in which case only soloed lanes sound. */
    bool anySoloed() const;

    /** Gives these lanes a fresh seed offset, re-rolling only their randomness. */
    void rerollLanes (const std::vector<int>& notes, int salt);

    juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

private:
    std::array<LaneSettings, numLanes> lanes {};
};

} // namespace mh
