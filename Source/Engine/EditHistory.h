// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace mh
{

/** Undo stack of whole-state snapshots.

    Hosts do not undo a plugin's internal state, so the per-lane work — values, links,
    mute and solo — would otherwise be unrecoverable. Snapshots rather than commands: the
    editable state is a few kilobytes of ValueTree, the actions are user-paced, and
    whole-state capture cannot drift out of step with the thing it is meant to reverse.
*/
class EditHistory
{
public:
    EditHistory() = default;

    static constexpr int maxEntries = 64;

    /** Records a state as the new present. Anything that had been undone past this point
        is discarded, which is the usual behaviour after editing following an undo.
    */
    void push (juce::ValueTree state);

    bool canUndo() const noexcept { return position > 0; }
    bool canRedo() const noexcept { return position >= 0 && position + 1 < (int) entries.size(); }

    /** Steps back and returns the state to apply, or an invalid tree if there is none. */
    juce::ValueTree undo();
    juce::ValueTree redo();

    void clear();

private:
    std::vector<juce::ValueTree> entries;
    int position = -1;   ///< index of the current state within `entries`
};

} // namespace mh
