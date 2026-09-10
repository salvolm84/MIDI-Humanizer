#include "ClipPlayer.h"

#include <algorithm>
#include <cmath>

namespace mh
{

void ClipPlayer::prepare (double)
{
    reset();
}

void ClipPlayer::reset()
{
    active.fill ({});
    lastPpqEnd = 0.0;
    haveLastPosition = false;
    wasRunning = false;
    clipPositionPpq.store (0.0, std::memory_order_relaxed);
    playing.store (false, std::memory_order_relaxed);
}

void ClipPlayer::releaseAllNotes (juce::MidiBuffer& midiOut, int sampleOffset)
{
    for (auto& note : active)
    {
        if (note.channel == 0)
            continue;

        midiOut.addEvent (juce::MidiMessage::noteOff (note.channel, note.noteNumber), sampleOffset);
        note = {};
    }
}

void ClipPlayer::startNote (int channel, int noteNumber, int velocity, double endPpq,
                            int sampleOffset, juce::MidiBuffer& midiOut)
{
    // Retriggering the same drum: close the previous voice first so the host never sees
    // two note-ons without an intervening note-off.
    for (auto& note : active)
    {
        if (note.channel == channel && note.noteNumber == noteNumber)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (channel, noteNumber), sampleOffset);
            note = {};
        }
    }

    midiOut.addEvent (juce::MidiMessage::noteOn (channel, noteNumber,
                                                 (juce::uint8) juce::jlimit (1, 127, velocity)),
                      sampleOffset);

    for (auto& note : active)
    {
        if (note.channel == 0)
        {
            note = { channel, noteNumber, endPpq };
            return;
        }
    }

    // Table full, which needs 128 simultaneous drum hits. Steal whichever voice was
    // due to end first rather than leaking a note-off.
    auto* soonest = &active[0];

    for (auto& note : active)
        if (note.endPpq < soonest->endPpq)
            soonest = &note;

    midiOut.addEvent (juce::MidiMessage::noteOff (soonest->channel, soonest->noteNumber), sampleOffset);
    *soonest = { channel, noteNumber, endPpq };
}

void ClipPlayer::releaseDueNotes (double ppqStart, double ppqEnd, double samplesPerPpq,
                                  int numSamples, juce::MidiBuffer& midiOut)
{
    for (auto& note : active)
    {
        if (note.channel == 0 || note.endPpq >= ppqEnd)
            continue;

        const auto offset = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                          (int) std::floor ((note.endPpq - ppqStart) * samplesPerPpq));

        midiOut.addEvent (juce::MidiMessage::noteOff (note.channel, note.noteNumber), offset);
        note = {};
    }
}

void ClipPlayer::renderBlock (const NoteSequence& clip, const BlockContext& ctx, juce::MidiBuffer& midiOut)
{
    if (ctx.numSamples <= 0)
        return;

    const auto bpm           = juce::jlimit (1.0, 999.0, ctx.bpm);
    const auto samplesPerPpq = juce::jmax (1.0, ctx.sampleRate * 60.0 / bpm);
    const auto blockPpq      = (double) ctx.numSamples / samplesPerPpq;

    if (! ctx.transportRunning)
    {
        if (wasRunning)
            releaseAllNotes (midiOut, 0);

        wasRunning = false;
        haveLastPosition = false;
        playing.store (false, std::memory_order_relaxed);
        return;
    }

    // A locate, a loop jump or a punch-in leaves the previous block's tail notes
    // orphaned, so close them before scheduling anything new.
    if (! haveLastPosition || std::abs (ctx.ppqStart - lastPpqEnd) > blockPpq + 1.0e-6)
        releaseAllNotes (midiOut, 0);

    const auto ppqStart = ctx.ppqStart;
    const auto ppqEnd   = ppqStart + blockPpq;
    const auto loopLen  = juce::jmax (0.25, clip.lengthPpq);

    releaseDueNotes (ppqStart, ppqEnd, samplesPerPpq, ctx.numSamples, midiOut);

    if (! clip.notes.empty())
    {
        // The block can straddle a loop boundary, so walk it as one segment per pass
        // through the clip instead of assuming clip position advances monotonically.
        auto segmentStart = ppqStart;

        while (segmentStart < ppqEnd - 1.0e-12)
        {
            auto positionInClip = segmentStart;

            if (ctx.loopClip)
            {
                positionInClip = std::fmod (segmentStart, loopLen);

                if (positionInClip < 0.0)
                    positionInClip += loopLen;
            }
            else if (segmentStart >= loopLen)
            {
                break;
            }

            const auto segmentEnd = juce::jmin (ppqEnd, segmentStart + (loopLen - positionInClip));
            const auto segmentLen = segmentEnd - segmentStart;

            if (segmentLen <= 0.0)
                break;

            auto it = std::lower_bound (clip.notes.begin(), clip.notes.end(), positionInClip,
                                        [] (const Note& n, double value) { return n.startPpq < value; });

            for (; it != clip.notes.end() && it->startPpq < positionInClip + segmentLen; ++it)
            {
                if (! it->audible)
                    continue;   // muted lane, or another lane is soloed

                const auto offsetPpq = (segmentStart - ppqStart) + (it->startPpq - positionInClip);
                const auto sampleOffset = juce::jlimit (0, ctx.numSamples - 1,
                                                        (int) std::floor (offsetPpq * samplesPerPpq));

                const auto channel   = ctx.forcedChannel > 0 ? ctx.forcedChannel : it->channel;
                const auto noteNumber = juce::jlimit (0, 127, it->emittedNote() + ctx.transpose);

                startNote (juce::jlimit (1, 16, channel), noteNumber, it->velocity,
                           ppqStart + offsetPpq + it->lengthPpq, sampleOffset, midiOut);
            }

            segmentStart = segmentEnd;
        }
    }

    auto cursor = ctx.loopClip ? std::fmod (ppqStart, loopLen) : ppqStart;

    if (cursor < 0.0)
        cursor += loopLen;

    clipPositionPpq.store (cursor, std::memory_order_relaxed);
    playing.store (true, std::memory_order_relaxed);

    lastPpqEnd = ppqEnd;
    haveLastPosition = true;
    wasRunning = true;
}

} // namespace mh
