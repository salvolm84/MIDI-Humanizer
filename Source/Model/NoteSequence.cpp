// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "NoteSequence.h"
#include <algorithm>

namespace mh
{

void NoteSequence::clear()
{
    notes.clear();
    lengthPpq = 4.0;
    sourceBpm = 120.0;
    timeSigNumerator = 4;
    timeSigDenominator = 4;
    name = {};
}

void NoteSequence::sortByTime()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        if (a.startPpq < b.startPpq)
            return true;

        if (b.startPpq < a.startPpq)
            return false;

        return a.noteNumber < b.noteNumber;
    });
}

std::vector<int> NoteSequence::lanesForDisplay() const
{
    std::vector<int> lanes;

    for (auto& n : notes)
        if (std::find (lanes.begin(), lanes.end(), n.noteNumber) == lanes.end())
            lanes.push_back (n.noteNumber);

    std::sort (lanes.begin(), lanes.end(), [] (int a, int b)
    {
        return laneSortKey (a) < laneSortKey (b);
    });

    return lanes;
}

double NoteSequence::barLengthPpq() const noexcept
{
    const auto denom = juce::jmax (1, timeSigDenominator);
    return 4.0 * (double) juce::jmax (1, timeSigNumerator) / (double) denom;
}

void NoteSequence::snapLengthToBars()
{
    // A few ticks of slack, so that file quantisation or an end-of-track marker sitting
    // a tick past the final bar line does not buy the clip a whole extra bar. The slack
    // is well below a 64th note, so genuine trailing content still rounds up.
    constexpr double tolerancePpq = 0.02;

    const auto bar = barLengthPpq();
    const auto bars = juce::jmax (1, (int) std::ceil ((lengthPpq - tolerancePpq) / bar));
    lengthPpq = bars * bar;
}

int NoteSequence::peakVelocity() const noexcept
{
    int peak = 0;

    for (auto& n : notes)
        peak = juce::jmax (peak, n.velocity);

    return peak;
}

void NoteSequence::assignSourceIds()
{
    for (size_t i = 0; i < notes.size(); ++i)
        notes[i].sourceId = (int) i;
}

juce::ValueTree NoteSequence::toValueTree() const
{
    juce::ValueTree tree ("Clip");
    tree.setProperty ("name", name, nullptr);
    tree.setProperty ("lengthPpq", lengthPpq, nullptr);
    tree.setProperty ("sourceBpm", sourceBpm, nullptr);
    tree.setProperty ("num", timeSigNumerator, nullptr);
    tree.setProperty ("den", timeSigDenominator, nullptr);

    juce::MemoryOutputStream stream;
    stream.writeInt ((int) notes.size());

    for (auto& n : notes)
    {
        stream.writeDouble (n.startPpq);
        stream.writeDouble (n.lengthPpq);
        stream.writeShort ((short) n.noteNumber);
        stream.writeShort ((short) n.velocity);
        stream.writeShort ((short) n.channel);
        stream.writeInt (n.sourceId);
    }

    tree.setProperty ("notes", stream.getMemoryBlock(), nullptr);
    return tree;
}

void NoteSequence::restoreFromValueTree (const juce::ValueTree& tree)
{
    clear();

    if (! tree.hasType ("Clip"))
        return;

    name               = tree.getProperty ("name", juce::String()).toString();
    lengthPpq          = (double) tree.getProperty ("lengthPpq", 4.0);
    sourceBpm          = (double) tree.getProperty ("sourceBpm", 120.0);
    timeSigNumerator   = juce::jlimit (1, 32, (int) tree.getProperty ("num", 4));
    timeSigDenominator = juce::jlimit (1, 32, (int) tree.getProperty ("den", 4));

    if (const auto* block = tree.getProperty ("notes").getBinaryData())
    {
        juce::MemoryInputStream stream (*block, false);
        const auto count = stream.readInt();

        for (int i = 0; i < count && ! stream.isExhausted(); ++i)
        {
            Note n;
            n.startPpq   = stream.readDouble();
            n.lengthPpq  = juce::jmax (0.02, stream.readDouble());
            n.noteNumber = juce::jlimit (0, 127, (int) stream.readShort());
            n.velocity   = juce::jlimit (1, 127, (int) stream.readShort());
            n.channel    = juce::jlimit (1, 16, (int) stream.readShort());
            n.sourceId   = stream.readInt();
            notes.push_back (n);
        }
    }

    sortByTime();
}

} // namespace mh
