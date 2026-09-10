#pragma once

#include "NoteSequence.h"

namespace mh
{

/** Reading and writing the standard MIDI files that DAWs hand over on a drag-and-drop. */
namespace MidiClipIO
{
    /** True when the extension looks like a standard MIDI file. */
    bool looksLikeMidiFile (const juce::File& file);

    /** Loads a .mid into `dest`, flattening every track into one lane set.

        Returns false and leaves `dest` untouched if the file cannot be parsed or holds
        no notes. `errorMessage` is filled in on failure.
    */
    bool load (const juce::File& file, NoteSequence& dest, juce::String& errorMessage);

    /** Writes a sequence out as a type-0 MIDI file at the given tempo. */
    bool save (const NoteSequence& seq, const juce::File& destFile, double bpm);

    /** Writes the sequence to a uniquely named temp file so it can be dragged into a
        host. Returns an empty File on failure. Caller does not need to clean up: the
        files live in the system temp directory.
    */
    juce::File writeTempFileForDragOut (const NoteSequence& seq, double bpm, const juce::String& baseName);
}

} // namespace mh
