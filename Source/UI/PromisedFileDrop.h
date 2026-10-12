// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mh
{

/** Lets the native window holding `component` accept macOS *promised-file* drags.

    Some hosts only write the dragged file once the drop target says where it should go.
    Reaper's Cmd+Option item drag works this way. JUCE only reads plain file URLs, so on
    its own it sees an empty drag and ignores it. This asks the sender for the file, then
    hands the real path to JUCE's normal drop routing, so FileDragAndDropTargets need no
    changes.

    Cheap and idempotent: call it whenever the component may have gained a new peer (a
    timer is fine). Does nothing off macOS, or while the component is not on screen.
*/
#if JUCE_MAC
void acceptPromisedFileDrops (juce::Component& component);
#else
inline void acceptPromisedFileDrops (juce::Component&) {}
#endif

} // namespace mh
