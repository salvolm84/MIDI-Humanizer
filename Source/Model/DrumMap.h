#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace mh
{

/** Broad instrument groups. Humanization "feel" is dialled in per family rather than
    per note number, because that is how a drummer's timing actually varies: the whole
    hi-hat hand sits early, the whole snare hand sits late.
*/
enum class DrumFamily
{
    kick = 0,
    snare,
    hiHat,
    tom,
    cymbal,
    percussion,
    numFamilies
};

constexpr int numDrumFamilies = (int) DrumFamily::numFamilies;

/** GM-percussion name for a note number, or a plain note name (C#3) outside the GM range. */
juce::String drumName (int noteNumber);

/** True when the note is in the General MIDI percussion table, i.e. it has a real
    instrument name rather than falling back to a bare note name like "C#3".
*/
bool isGeneralMidiPercussion (int noteNumber);

/** Short label suitable for a narrow lane header. */
juce::String drumShortName (int noteNumber);

/** Which family a note number belongs to. Anything unrecognised lands in percussion. */
DrumFamily familyOf (int noteNumber);

juce::String familyName (DrumFamily family);

/** Notes that cut off this note's tail when they sound (open hi-hat closed by a pedal or
    closed hat, ride bell cutting a crash, ...). Returns an empty array when the note
    belongs to no choke group.
*/
juce::Array<int> chokedBy (int noteNumber);

/** Ordering used to lay lanes out top-to-bottom in the grid: cymbals up top, kick at the
    bottom, which is how most DAW drum editors and drum notation stack them. Lower sorts
    higher on screen.
*/
int laneSortKey (int noteNumber);

} // namespace mh
