// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "DrumMap.h"

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace mh
{

/** One row of a user drum map. */
struct DrumMapEntry
{
    int          inNote  = 36;   ///< the note number this row displays (Cubase "INote")
    int          outNote = 36;   ///< note actually sent (Cubase "ONote"); kept for future routing
    int          channel = -1;   ///< -1 means "use the part's channel"
    juce::String name;
    int          order   = 0;    ///< display order, top of the list first
};

/** A drum name map loaded from a DAW's own format.

    Two formats are understood, picked by sniffing the contents rather than the
    extension, because both DAWs let you save under any name:

      - Cubase drum maps (.drm): XML, with a "Map" list of items carrying Name/INote/
        ONote/Channel, and an optional "Order" list giving the row order.
      - Reaper MIDI note-name files (.txt): one `<note number> <name>` per line, with
        `#` comments and an optional trailing channel column.

    When no map is loaded, lookups fall through to the General MIDI percussion names.
*/
class DrumMapping
{
public:
    DrumMapping() = default;

    enum class Format { none, cubaseDrumMap, reaperNoteNames };

    bool loadFromFile (const juce::File& file, juce::String& errorMessage);

    /** Parses map text already in memory (used when restoring plugin state). */
    bool loadFromText (const juce::String& text, const juce::String& sourceName, juce::String& errorMessage);

    void clear();

    bool isEmpty() const noexcept              { return entries.empty(); }
    Format getFormat() const noexcept          { return format; }
    const juce::String& getName() const noexcept { return mapName; }
    const std::vector<DrumMapEntry>& getEntries() const noexcept { return entries; }

    /** Row label for a note number: the map's name if present, else the GM name. */
    juce::String labelFor (int noteNumber) const;

    /** Sort key for laying rows out top-to-bottom. Uses the map's own order when the
        note is mapped, and falls back to the GM family ordering otherwise.
    */
    int sortKeyFor (int noteNumber) const;

    /** Every note number the map defines, in display order. Empty when no map is set. */
    std::vector<int> mappedNotes() const;

    /** True when this note number appears in the loaded map. */
    bool contains (int noteNumber) const;

    /** True when the note has a real instrument name rather than a bare note name.

        With a map loaded that means the map defines the row; without one it falls back to
        the General MIDI percussion table. Degrading to GM matters: keying this on the map
        alone would make "hide unlabelled" hide every row when no map is loaded.
    */
    bool hasSpecificName (int noteNumber) const;

    /** Round-trips the map through plugin state. */
    juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

    static bool looksLikeDrumMapFile (const juce::File& file);

private:
    bool parseCubaseDrumMap (const juce::String& text, juce::String& errorMessage);
    bool parseReaperNoteNames (const juce::String& text, juce::String& errorMessage);
    void finalise();

    std::vector<DrumMapEntry> entries;
    juce::String mapName;
    juce::String sourcePath;
    Format format = Format::none;
};

} // namespace mh
