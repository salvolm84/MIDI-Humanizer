#include "MidiClipIO.h"

namespace mh::MidiClipIO
{

namespace
{
    constexpr short exportTicksPerQuarter = 960;

    double firstTempoBpm (const juce::MidiFile& midiFile, double fallback)
    {
        juce::MidiMessageSequence tempos;
        midiFile.findAllTempoEvents (tempos);

        for (int i = 0; i < tempos.getNumEvents(); ++i)
        {
            const auto& m = tempos.getEventPointer (i)->message;

            if (m.isTempoMetaEvent())
            {
                const auto secondsPerQuarter = m.getTempoSecondsPerQuarterNote();

                if (secondsPerQuarter > 1.0e-6)
                    return 60.0 / secondsPerQuarter;
            }
        }

        return fallback;
    }

    void firstTimeSignature (const juce::MidiFile& midiFile, int& numerator, int& denominator)
    {
        juce::MidiMessageSequence sigs;
        midiFile.findAllTimeSigEvents (sigs);

        for (int i = 0; i < sigs.getNumEvents(); ++i)
        {
            const auto& m = sigs.getEventPointer (i)->message;

            if (m.isTimeSignatureMetaEvent())
            {
                m.getTimeSignatureInfo (numerator, denominator);
                return;
            }
        }
    }
}

bool looksLikeMidiFile (const juce::File& file)
{
    return file.hasFileExtension ("mid;midi;smf;rmi");
}

bool load (const juce::File& file, NoteSequence& dest, juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = "File does not exist: " + file.getFullPathName();
        return false;
    }

    juce::FileInputStream stream (file);

    if (! stream.openedOk())
    {
        errorMessage = "Could not open " + file.getFileName();
        return false;
    }

    juce::MidiFile midiFile;

    if (! midiFile.readFrom (stream, true))
    {
        errorMessage = file.getFileName() + " is not a readable MIDI file";
        return false;
    }

    const auto timeFormat = midiFile.getTimeFormat();

    NoteSequence loaded;
    loaded.name = file.getFileNameWithoutExtension();
    loaded.sourceBpm = firstTempoBpm (midiFile, 120.0);
    firstTimeSignature (midiFile, loaded.timeSigNumerator, loaded.timeSigDenominator);

    // Two timebases are possible. Tick-based files (timeFormat > 0) divide down exactly,
    // which keeps note positions on the musical grid. SMPTE files are in wall-clock
    // units, so they have to go through seconds and back out at the file's own tempo.
    const auto smpte = timeFormat <= 0;

    if (smpte)
        midiFile.convertTimestampTicksToSeconds();

    const auto ticksToPpq = [&] (double t)
    {
        if (smpte)
            return t * loaded.sourceBpm / 60.0;

        return t / (double) timeFormat;
    };

    double endOfDataPpq = 0.0;

    for (int track = 0; track < midiFile.getNumTracks(); ++track)
    {
        // Copying lets us match note pairs without mutating the parsed file.
        juce::MidiMessageSequence seq (*midiFile.getTrack (track));
        seq.updateMatchedPairs();

        endOfDataPpq = juce::jmax (endOfDataPpq, ticksToPpq (seq.getEndTime()));

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* event = seq.getEventPointer (i);
            const auto& m = event->message;

            if (! m.isNoteOn())
                continue;

            Note n;
            n.startPpq   = juce::jmax (0.0, ticksToPpq (m.getTimeStamp()));
            n.noteNumber = m.getNoteNumber();
            n.velocity   = juce::jlimit (1, 127, (int) m.getVelocity());
            n.channel    = m.getChannel();

            if (event->noteOffObject != nullptr)
                n.lengthPpq = ticksToPpq (event->noteOffObject->message.getTimeStamp()) - n.startPpq;
            else
                n.lengthPpq = 0.25;   // unmatched note-on: give it a sixteenth so it still sounds

            // Percussion note length carries almost no musical information, but a
            // zero-length note would never produce a note-off, so enforce a floor.
            n.lengthPpq = juce::jmax (0.02, n.lengthPpq);

            loaded.notes.push_back (n);
        }
    }

    if (loaded.notes.empty())
    {
        errorMessage = file.getFileName() + " contains no notes";
        return false;
    }

    loaded.sortByTime();
    loaded.assignSourceIds();

    double lastEnd = 0.0;

    for (auto& n : loaded.notes)
        lastEnd = juce::jmax (lastEnd, n.endPpq());

    // Honour trailing space the host encoded in the end-of-track marker, so a two-bar
    // item whose last hit is on bar 2 beat 1 still loops as two bars.
    loaded.lengthPpq = juce::jmax (lastEnd, endOfDataPpq, 0.25);
    loaded.snapLengthToBars();

    dest = std::move (loaded);
    return true;
}

bool save (const NoteSequence& seq, const juce::File& destFile, double bpm)
{
    juce::MidiMessageSequence track;

    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (1.0e6 * 60.0 / juce::jmax (1.0, bpm))));
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (seq.timeSigNumerator, seq.timeSigDenominator));

    const auto ppqToTicks = [] (double ppq) { return ppq * (double) exportTicksPerQuarter; };

    for (auto& n : seq.notes)
    {
        const auto onTick  = ppqToTicks (juce::jmax (0.0, n.startPpq));
        const auto offTick = juce::jmax (onTick + 1.0, ppqToTicks (n.endPpq()));

        track.addEvent (juce::MidiMessage::noteOn  (n.channel, n.noteNumber, (juce::uint8) juce::jlimit (1, 127, n.velocity)), onTick);
        track.addEvent (juce::MidiMessage::noteOff (n.channel, n.noteNumber), offTick);
    }

    track.updateMatchedPairs();
    track.sort();

    // End the track exactly on the clip's musical length so the length round-trips, but
    // never before the final note-off of a note that rings past the loop point.
    track.addEvent (juce::MidiMessage::endOfTrack(),
                    juce::jmax (track.getEndTime(), ppqToTicks (seq.lengthPpq)));

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (exportTicksPerQuarter);
    midiFile.addTrack (track);

    if (destFile.existsAsFile())
        destFile.deleteFile();

    juce::FileOutputStream out (destFile);

    if (! out.openedOk())
        return false;

    if (! midiFile.writeTo (out, 0))
        return false;

    out.flush();
    return true;
}

juce::File writeTempFileForDragOut (const NoteSequence& seq, double bpm, const juce::String& baseName)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("MIDIHumanizer");

    if (! dir.createDirectory())
        return {};

    const auto safeName = juce::File::createLegalFileName (baseName.isNotEmpty() ? baseName : "clip");
    auto target = dir.getChildFile (safeName + ".mid").getNonexistentSibling();

    if (! save (seq, target, bpm))
        return {};

    return target;
}

} // namespace mh::MidiClipIO
