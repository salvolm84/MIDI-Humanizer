#include "EditHistory.h"

namespace mh
{

void EditHistory::push (juce::ValueTree state)
{
    if (! state.isValid())
        return;

    // Editing after an undo throws away the branch that was undone.
    if (position >= 0 && position + 1 < (int) entries.size())
        entries.erase (entries.begin() + position + 1, entries.end());

    entries.push_back (std::move (state));

    if ((int) entries.size() > maxEntries)
        entries.erase (entries.begin());

    position = (int) entries.size() - 1;
}

juce::ValueTree EditHistory::undo()
{
    if (! canUndo())
        return {};

    --position;
    return entries[(size_t) position];
}

juce::ValueTree EditHistory::redo()
{
    if (! canRedo())
        return {};

    ++position;
    return entries[(size_t) position];
}

void EditHistory::clear()
{
    entries.clear();
    position = -1;
}

} // namespace mh
