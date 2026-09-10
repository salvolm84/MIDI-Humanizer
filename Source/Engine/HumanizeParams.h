// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Model/DrumMap.h"
#include "../Model/GridDivision.h"
#include "LaneSettings.h"

namespace mh
{

/** Identity note mapping, the default when no drum map is loaded. */
inline std::array<int, LaneSettingsTable::numLanes> makeIdentityNoteMap()
{
    std::array<int, LaneSettingsTable::numLanes> map {};

    for (int note = 0; note < LaneSettingsTable::numLanes; ++note)
        map[(size_t) note] = note;

    return map;
}

/** A plain snapshot of every humanization control, read once per recompute.

    The processor copies its APVTS values into one of these on the message thread; the
    humanizer never touches parameter objects itself. That keeps the transform a pure
    function of (source clip, params), which is what makes the on-screen preview and the
    audible output provably identical.

    Per-lane inheritance is resolved by the processor before the humanizer ever sees it:
    `lane[n]` is always the final answer for note number `n`, so the engine indexes
    straight into it with no notion of masters or overrides.
*/
struct HumanizeParams
{
    bool   enabled        = true;

    /** Changing the seed re-rolls every random decision. Note that spread is applied as
        a scale on a fixed random field, so widening it deepens the same performance
        rather than shuffling it into a different one.
    */
    int    seed           = 1;

    /** Tempo used for every ms-to-ppq conversion. Comes from the host when one is
        running, otherwise from the clip's own tempo map.
    */
    double referenceBpm   = 120.0;

    // ---- resolved per-lane settings ----------------------------------------------
    std::array<LaneSettings, LaneSettingsTable::numLanes> lane {};

    const LaneSettings& forNote (int noteNumber) const noexcept
    {
        return lane[(size_t) juce::jlimit (0, LaneSettingsTable::numLanes - 1, noteNumber)];
    }

    /** Output note per lane, from the loaded drum map's ONote column. Identity by
        default. Applied here rather than at playback so the mapping travels with the
        published clip instead of needing its own lock-free hand-off.
    */
    std::array<int, LaneSettingsTable::numLanes> outputNote = makeIdentityNoteMap();

    /** True when any lane is soloed, in which case only soloed lanes are audible. */
    bool   anyLaneSoloed = false;

    // ---- global timing -----------------------------------------------------------
    float  tightenPercent  = 0.0f;   ///< 0..100, pulls notes toward the grid first
    int    tightenDivision = defaultGridDivisionIndex;

    /** Which subdivision swings. The amount is per lane, but the grid it swings against
        is a property of the groove as a whole.
    */
    int    swingDivision   = defaultSwingDivisionIndex;

    // ---- global drummer realism ---------------------------------------------------
    float  flamAmount      = 0.0f;   ///< 0..100, splits doubled hits into flams
    float  flamSpreadMs    = 18.0f;
    float  ghostAmount     = 0.0f;   ///< 0..100, snare ghost note density
    int    ghostVelocity   = 28;
    float  variationAmount = 0.0f;   ///< 0..100, breaks up identical repeated hits
    float  handAlternation = 0.0f;   ///< 0..100, strong/weak alternation in fast runs

    // ---- global playability limits ------------------------------------------------
    bool   limitsEnabled   = true;
    bool   chokeEnabled    = true;   ///< closed hat cuts the open hat's tail
};

} // namespace mh
