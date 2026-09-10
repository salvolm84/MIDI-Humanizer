// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "Model/DrumMap.h"

namespace mh::pid
{

// Global -----------------------------------------------------------------------
inline constexpr auto enabled     = "enabled";
inline constexpr auto seed        = "seed";
inline constexpr auto loopClip    = "loopClip";
inline constexpr auto audition    = "audition";
inline constexpr auto auditionBpm = "auditionBpm";
inline constexpr auto outChannel  = "outChannel";
inline constexpr auto transpose   = "transpose";
inline constexpr auto midiThru    = "midiThru";

/** Diagnostic: emits a repeating note independently of the clip and the transport, so
    MIDI routing can be proved before blaming the humanizer.
*/
inline constexpr auto testNote     = "testNote";

/** Apply the loaded drum map's ONote column when sending, so a row can display one note
    and emit another.
*/
inline constexpr auto mapOutputNotes = "mapOutNotes";

// Master lane values -----------------------------------------------------------
// These are the defaults every lane inherits until it is given an override. They stay
// host parameters so the overall feel of the groove remains automatable; the per-lane
// overrides live in plugin state instead, because 128 lanes of parameters would swamp a
// host's automation list.
inline constexpr auto timeBias    = "timeBias";     ///< ms, negative plays early
inline constexpr auto timeSpread  = "timeSpread";   ///< ms of wander either side
inline constexpr auto swing       = "swing";
inline constexpr auto velTarget   = "velTarget";    ///< 0 = keep source levels
inline constexpr auto velDynamics = "velDynamics";  ///< % of source accenting kept
inline constexpr auto velSpread   = "velSpread";
inline constexpr auto accent      = "accent";
inline constexpr auto minGap      = "minGap";

// Global timing ----------------------------------------------------------------
inline constexpr auto tighten     = "tighten";
inline constexpr auto tightenDiv  = "tightenDiv";
inline constexpr auto swingDiv    = "swingDiv";

// Global drummer realism -------------------------------------------------------
inline constexpr auto flam        = "flam";
inline constexpr auto flamSpread  = "flamSpread";
inline constexpr auto ghost       = "ghost";
inline constexpr auto ghostVel    = "ghostVel";
inline constexpr auto variation   = "variation";
inline constexpr auto hands       = "hands";

// Global playability limits ----------------------------------------------------
inline constexpr auto limits      = "limits";
inline constexpr auto choke       = "choke";

} // namespace mh::pid
