// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Model/NoteSequence.h"
#include <array>
#include <atomic>

namespace mh
{

/** Hands a humanized clip from the message thread to the audio thread without locking.

    A small ring of slots plus one atomic index. The writer only ever fills a slot that
    is not currently published, and the reader latches the index once per block, so the
    audio thread never allocates, never blocks and never reads a half-written clip.
    Publishes are coalesced by a timer on the processor side, which keeps the publish
    rate far below the block rate and means the reader cannot be lapped.
*/
class ClipStore
{
public:
    ClipStore() = default;

    /** Message thread only. */
    void publish (NoteSequence&& sequence)
    {
        const auto next = (liveIndex.load (std::memory_order_relaxed) + 1) % numSlots;
        slots[(size_t) next] = std::move (sequence);
        liveIndex.store (next, std::memory_order_release);
    }

    /** Safe from any thread. The returned reference stays valid for the duration of one
        audio block.
    */
    const NoteSequence& live() const noexcept
    {
        return slots[(size_t) liveIndex.load (std::memory_order_acquire)];
    }

private:
    static constexpr int numSlots = 4;

    std::array<NoteSequence, numSlots> slots;
    std::atomic<int> liveIndex { 0 };
};

//==============================================================================
/** Plays a clip in sync with the host transport.

    Positions are derived from the host's PPQ position every block rather than from an
    internal sample counter, so looping, tempo changes and jumps in the timeline all
    stay locked to the arrangement. Sounding notes are tracked separately from the clip,
    which is what guarantees a note-off even if the clip is replaced, the transport
    stops, or the note vanishes because the user changed a humanization setting.
*/
class ClipPlayer
{
public:
    ClipPlayer() = default;

    void prepare (double sampleRate);
    void reset();

    struct BlockContext
    {
        double ppqStart      = 0.0;    ///< host PPQ at the first sample of this block
        double bpm           = 120.0;
        double sampleRate    = 48000.0;
        int    numSamples    = 0;
        bool   transportRunning = false;
        bool   loopClip      = true;
        int    forcedChannel = 0;      ///< 0 = keep each note's own channel, 1..16 = force
        int    transpose     = 0;      ///< semitone offset applied to output note numbers
    };

    /** Emits note-ons and note-offs for one audio block into `midiOut`. */
    void renderBlock (const NoteSequence& clip, const BlockContext& ctx, juce::MidiBuffer& midiOut);

    /** Queues an all-notes-off for everything currently sounding. Call when the
        transport stops or the plugin is bypassed.
    */
    void releaseAllNotes (juce::MidiBuffer& midiOut, int sampleOffset);

    /** Where the playhead sits inside the clip, in quarter notes; for the UI cursor. */
    double getClipPositionPpq() const noexcept { return clipPositionPpq.load (std::memory_order_relaxed); }
    bool   isPlaying()          const noexcept { return playing.load (std::memory_order_relaxed); }

private:
    struct ActiveNote
    {
        int    channel    = 0;   ///< 0 means the slot is free
        int    noteNumber = 0;
        double endPpq     = 0.0; ///< absolute host PPQ at which to release
    };

    static constexpr int maxActiveNotes = 128;

    void startNote (int channel, int noteNumber, int velocity, double endPpq,
                    int sampleOffset, juce::MidiBuffer& midiOut);
    void releaseDueNotes (double ppqStart, double ppqEnd, double samplesPerPpq,
                          int numSamples, juce::MidiBuffer& midiOut);

    std::array<ActiveNote, maxActiveNotes> active {};

    double lastPpqEnd = 0.0;
    bool   haveLastPosition = false;
    bool   wasRunning = false;

    std::atomic<double> clipPositionPpq { 0.0 };
    std::atomic<bool>   playing { false };
};

} // namespace mh
