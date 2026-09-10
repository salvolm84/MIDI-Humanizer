// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "DrumMap.h"

namespace mh
{

namespace
{
    struct GmEntry { int note; const char* longName; const char* shortName; DrumFamily family; };

    // General MIDI percussion key map (notes 35-81).
    const GmEntry gmMap[] =
    {
        { 35, "Acoustic Bass Drum", "Kick A",   DrumFamily::kick },
        { 36, "Bass Drum 1",        "Kick",     DrumFamily::kick },
        { 37, "Side Stick",         "Stick",    DrumFamily::snare },
        { 38, "Acoustic Snare",     "Snare",    DrumFamily::snare },
        { 39, "Hand Clap",          "Clap",     DrumFamily::percussion },
        { 40, "Electric Snare",     "Snare E",  DrumFamily::snare },
        { 41, "Low Floor Tom",      "Tom LoF",  DrumFamily::tom },
        { 42, "Closed Hi-Hat",      "HH Cl",    DrumFamily::hiHat },
        { 43, "High Floor Tom",     "Tom HiF",  DrumFamily::tom },
        { 44, "Pedal Hi-Hat",       "HH Pdl",   DrumFamily::hiHat },
        { 45, "Low Tom",            "Tom Lo",   DrumFamily::tom },
        { 46, "Open Hi-Hat",        "HH Op",    DrumFamily::hiHat },
        { 47, "Low-Mid Tom",        "Tom LoMd", DrumFamily::tom },
        { 48, "Hi-Mid Tom",         "Tom HiMd", DrumFamily::tom },
        { 49, "Crash Cymbal 1",     "Crash 1",  DrumFamily::cymbal },
        { 50, "High Tom",           "Tom Hi",   DrumFamily::tom },
        { 51, "Ride Cymbal 1",      "Ride 1",   DrumFamily::cymbal },
        { 52, "Chinese Cymbal",     "China",    DrumFamily::cymbal },
        { 53, "Ride Bell",          "Bell",     DrumFamily::cymbal },
        { 54, "Tambourine",         "Tamb",     DrumFamily::percussion },
        { 55, "Splash Cymbal",      "Splash",   DrumFamily::cymbal },
        { 56, "Cowbell",            "Cowbell",  DrumFamily::percussion },
        { 57, "Crash Cymbal 2",     "Crash 2",  DrumFamily::cymbal },
        { 58, "Vibraslap",          "Vibrslp",  DrumFamily::percussion },
        { 59, "Ride Cymbal 2",      "Ride 2",   DrumFamily::cymbal },
        { 60, "Hi Bongo",           "Bongo Hi", DrumFamily::percussion },
        { 61, "Low Bongo",          "Bongo Lo", DrumFamily::percussion },
        { 62, "Mute Hi Conga",      "Conga Mt", DrumFamily::percussion },
        { 63, "Open Hi Conga",      "Conga Hi", DrumFamily::percussion },
        { 64, "Low Conga",          "Conga Lo", DrumFamily::percussion },
        { 65, "High Timbale",       "Timb Hi",  DrumFamily::percussion },
        { 66, "Low Timbale",        "Timb Lo",  DrumFamily::percussion },
        { 67, "High Agogo",         "Agogo Hi", DrumFamily::percussion },
        { 68, "Low Agogo",          "Agogo Lo", DrumFamily::percussion },
        { 69, "Cabasa",             "Cabasa",   DrumFamily::percussion },
        { 70, "Maracas",            "Maracas",  DrumFamily::percussion },
        { 71, "Short Whistle",      "Whis Sh",  DrumFamily::percussion },
        { 72, "Long Whistle",       "Whis Lg",  DrumFamily::percussion },
        { 73, "Short Guiro",        "Guiro Sh", DrumFamily::percussion },
        { 74, "Long Guiro",         "Guiro Lg", DrumFamily::percussion },
        { 75, "Claves",             "Claves",   DrumFamily::percussion },
        { 76, "Hi Wood Block",      "Wood Hi",  DrumFamily::percussion },
        { 77, "Low Wood Block",     "Wood Lo",  DrumFamily::percussion },
        { 78, "Mute Cuica",         "Cuica Mt", DrumFamily::percussion },
        { 79, "Open Cuica",         "Cuica Op", DrumFamily::percussion },
        { 80, "Mute Triangle",      "Tri Mt",   DrumFamily::percussion },
        { 81, "Open Triangle",      "Tri Op",   DrumFamily::percussion },
    };

    const GmEntry* findEntry (int noteNumber)
    {
        for (auto& e : gmMap)
            if (e.note == noteNumber)
                return &e;

        return nullptr;
    }
}

juce::String drumName (int noteNumber)
{
    if (auto* e = findEntry (noteNumber))
        return e->longName;

    return juce::MidiMessage::getMidiNoteName (noteNumber, true, true, 3);
}

bool isGeneralMidiPercussion (int noteNumber)
{
    return findEntry (noteNumber) != nullptr;
}

juce::String drumShortName (int noteNumber)
{
    if (auto* e = findEntry (noteNumber))
        return e->shortName;

    return juce::MidiMessage::getMidiNoteName (noteNumber, true, true, 3);
}

DrumFamily familyOf (int noteNumber)
{
    if (auto* e = findEntry (noteNumber))
        return e->family;

    return DrumFamily::percussion;
}

juce::String familyName (DrumFamily family)
{
    switch (family)
    {
        case DrumFamily::kick:        return "Kick";
        case DrumFamily::snare:       return "Snare";
        case DrumFamily::hiHat:       return "Hi-Hat";
        case DrumFamily::tom:         return "Toms";
        case DrumFamily::cymbal:      return "Cymbals";
        case DrumFamily::percussion:  return "Perc";
        case DrumFamily::numFamilies: break;
    }

    return "Perc";
}

juce::Array<int> chokedBy (int noteNumber)
{
    switch (noteNumber)
    {
        case 46: return { 42, 44 };   // open hat cut by closed hat / pedal
        case 26: return { 42, 44 };   // some maps put "open hat edge" here
        default: return {};
    }
}

int laneSortKey (int noteNumber)
{
    // Group first so families stay contiguous, then order by note number inside the
    // group. Cymbals at the top, kick at the bottom.
    // indexed by DrumFamily: kick, snare, hiHat, tom, cymbal, percussion
    const int groupOrder[] = { 4, 3, 1, 2, 0, 5 };
    static_assert (numDrumFamilies == 6, "groupOrder must cover every family");

    const auto group = groupOrder[(int) familyOf (noteNumber)];
    return group * 1000 + (127 - noteNumber);
}

} // namespace mh
