// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "DrumMapping.h"

#include <algorithm>

namespace mh
{

namespace
{
    /** Cubase writes typed leaf elements tagged <string>/<int>/<float> that carry the
        property name in a "name" attribute and the payload in "value".
    */
    const juce::XmlElement* childWithPropertyName (const juce::XmlElement& parent, juce::StringRef propertyName)
    {
        for (auto* child : parent.getChildIterator())
            if (child->getStringAttribute ("name") == propertyName)
                return child;

        return nullptr;
    }

    juce::String propertyString (const juce::XmlElement& parent, juce::StringRef propertyName,
                                 const juce::String& fallback = {})
    {
        if (auto* child = childWithPropertyName (parent, propertyName))
            return child->getStringAttribute ("value", fallback);

        return fallback;
    }

    int propertyInt (const juce::XmlElement& parent, juce::StringRef propertyName, int fallback)
    {
        if (auto* child = childWithPropertyName (parent, propertyName))
            return child->getIntAttribute ("value", fallback);

        return fallback;
    }

    /** Depth-first search for a <list name="..."> anywhere in the document, so we do not
        depend on the exact nesting Cubase happens to use.
    */
    const juce::XmlElement* findListNamed (const juce::XmlElement& element, juce::StringRef listName)
    {
        if (element.hasTagName ("list") && element.getStringAttribute ("name") == listName)
            return &element;

        for (auto* child : element.getChildIterator())
            if (auto* found = findListNamed (*child, listName))
                return found;

        return nullptr;
    }

    bool textLooksLikeXml (const juce::String& text)
    {
        const auto head = text.substring (0, 512);
        return head.trimStart().startsWith ("<")
            || head.contains ("<InstrumentMap")
            || head.contains ("<list");
    }
}

//==============================================================================
void DrumMapping::clear()
{
    entries.clear();
    mapName = {};
    sourcePath = {};
    format = Format::none;
}

bool DrumMapping::looksLikeDrumMapFile (const juce::File& file)
{
    return file.hasFileExtension ("drm;txt;xml");
}

bool DrumMapping::loadFromFile (const juce::File& file, juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = "File does not exist: " + file.getFullPathName();
        return false;
    }

    const auto text = file.loadFileAsString();

    if (text.isEmpty())
    {
        errorMessage = file.getFileName() + " is empty";
        return false;
    }

    if (! loadFromText (text, file.getFileNameWithoutExtension(), errorMessage))
        return false;

    sourcePath = file.getFullPathName();
    return true;
}

bool DrumMapping::loadFromText (const juce::String& text, const juce::String& sourceName,
                                juce::String& errorMessage)
{
    DrumMapping parsed;
    parsed.mapName = sourceName;

    const auto ok = textLooksLikeXml (text) ? parsed.parseCubaseDrumMap (text, errorMessage)
                                            : parsed.parseReaperNoteNames (text, errorMessage);

    if (! ok)
        return false;

    if (parsed.entries.empty())
    {
        errorMessage = sourceName + " contains no drum map rows";
        return false;
    }

    parsed.finalise();

    entries = std::move (parsed.entries);
    mapName = parsed.mapName;
    format  = parsed.format;
    sourcePath = {};
    return true;
}

bool DrumMapping::parseCubaseDrumMap (const juce::String& text, juce::String& errorMessage)
{
    auto root = juce::XmlDocument::parse (text);

    if (root == nullptr)
    {
        errorMessage = "Not a readable Cubase drum map (XML could not be parsed)";
        return false;
    }

    if (auto* nameElement = childWithPropertyName (*root, "Name"))
    {
        const auto declared = nameElement->getStringAttribute ("value");

        if (declared.isNotEmpty())
            mapName = declared;
    }

    auto* mapList = findListNamed (*root, "Map");

    if (mapList == nullptr)
    {
        errorMessage = "Cubase drum map has no \"Map\" list";
        return false;
    }

    for (auto* item : mapList->getChildIterator())
    {
        if (childWithPropertyName (*item, "INote") == nullptr)
            continue;

        DrumMapEntry entry;
        entry.inNote  = juce::jlimit (0, 127, propertyInt (*item, "INote", 36));
        entry.outNote = juce::jlimit (0, 127, propertyInt (*item, "ONote", entry.inNote));
        entry.channel = propertyInt (*item, "Channel", -1);
        entry.name    = propertyString (*item, "Name");
        entry.order   = (int) entries.size();

        if (entry.name.isEmpty())
            entry.name = drumName (entry.inNote);

        entries.push_back (entry);
    }

    if (entries.empty())
    {
        errorMessage = "Cubase drum map \"Map\" list has no usable rows";
        return false;
    }

    // The optional Order list holds indices into the Map list, top row first. Honouring
    // it is what makes the plugin's row layout match what the user sees in Cubase.
    if (auto* orderList = findListNamed (*root, "Order"))
    {
        std::vector<int> order;

        for (auto* item : orderList->getChildIterator())
        {
            const auto index = propertyInt (*item, "Item", -1);

            if (juce::isPositiveAndBelow (index, (int) entries.size()))
                order.push_back (index);
        }

        if (order.size() == entries.size())
            for (size_t position = 0; position < order.size(); ++position)
                entries[(size_t) order[position]].order = (int) position;
    }

    format = Format::cubaseDrumMap;
    return true;
}

bool DrumMapping::parseReaperNoteNames (const juce::String& text, juce::String& errorMessage)
{
    auto lines = juce::StringArray::fromLines (text);

    for (auto rawLine : lines)
    {
        const auto line = rawLine.trim();

        if (line.isEmpty() || line.startsWith ("#") || line.startsWith ("//"))
            continue;

        const auto firstToken = line.upToFirstOccurrenceOf (" ", false, false)
                                    .upToFirstOccurrenceOf ("\t", false, false);

        if (! firstToken.containsOnly ("0123456789"))
            continue;

        const auto noteNumber = firstToken.getIntValue();

        if (! juce::isPositiveAndBelow (noteNumber, 128))
            continue;

        // Everything after the note number is the name. Reaper allows a trailing
        // channel column, but names like "Crash 2" make that ambiguous, so the whole
        // remainder is treated as the label.
        const auto name = line.substring (firstToken.length()).trim();

        if (name.isEmpty())
            continue;

        DrumMapEntry entry;
        entry.inNote  = noteNumber;
        entry.outNote = noteNumber;
        entry.name    = name;
        entry.order   = (int) entries.size();
        entries.push_back (entry);
    }

    if (entries.empty())
    {
        errorMessage = "No `<note number> <name>` lines found";
        return false;
    }

    format = Format::reaperNoteNames;
    return true;
}

void DrumMapping::finalise()
{
    std::stable_sort (entries.begin(), entries.end(),
                      [] (const DrumMapEntry& a, const DrumMapEntry& b) { return a.order < b.order; });

    // Drop duplicate rows for the same note, keeping the first, then re-index densely.
    std::vector<DrumMapEntry> unique;
    unique.reserve (entries.size());

    for (auto& e : entries)
    {
        const auto alreadyPresent = std::any_of (unique.begin(), unique.end(),
                                                 [&e] (const DrumMapEntry& u) { return u.inNote == e.inNote; });

        if (! alreadyPresent)
            unique.push_back (e);
    }

    entries = std::move (unique);

    for (size_t i = 0; i < entries.size(); ++i)
        entries[i].order = (int) i;
}

juce::String DrumMapping::labelFor (int noteNumber) const
{
    for (auto& e : entries)
        if (e.inNote == noteNumber)
            return e.name;

    return drumName (noteNumber);
}

int DrumMapping::sortKeyFor (int noteNumber) const
{
    for (auto& e : entries)
        if (e.inNote == noteNumber)
            return e.order;

    // Unmapped notes sit below every mapped row, in kit order among themselves.
    return 10000 + laneSortKey (noteNumber);
}

std::vector<int> DrumMapping::mappedNotes() const
{
    std::vector<int> notes;
    notes.reserve (entries.size());

    for (auto& e : entries)
        notes.push_back (e.inNote);

    return notes;
}

bool DrumMapping::contains (int noteNumber) const
{
    return std::any_of (entries.begin(), entries.end(),
                        [noteNumber] (const DrumMapEntry& e) { return e.inNote == noteNumber; });
}

bool DrumMapping::hasSpecificName (int noteNumber) const
{
    if (! entries.empty())
        return contains (noteNumber);

    return isGeneralMidiPercussion (noteNumber);
}

juce::ValueTree DrumMapping::toValueTree() const
{
    juce::ValueTree tree ("DrumMapping");
    tree.setProperty ("name", mapName, nullptr);
    tree.setProperty ("format", (int) format, nullptr);
    tree.setProperty ("source", sourcePath, nullptr);

    for (auto& e : entries)
    {
        juce::ValueTree row ("Row");
        row.setProperty ("in", e.inNote, nullptr);
        row.setProperty ("out", e.outNote, nullptr);
        row.setProperty ("ch", e.channel, nullptr);
        row.setProperty ("name", e.name, nullptr);
        row.setProperty ("order", e.order, nullptr);
        tree.appendChild (row, nullptr);
    }

    return tree;
}

void DrumMapping::restoreFromValueTree (const juce::ValueTree& tree)
{
    clear();

    if (! tree.hasType ("DrumMapping"))
        return;

    mapName    = tree.getProperty ("name", juce::String()).toString();
    sourcePath = tree.getProperty ("source", juce::String()).toString();
    format     = (Format) (int) tree.getProperty ("format", (int) Format::none);

    for (auto row : tree)
    {
        if (! row.hasType ("Row"))
            continue;

        DrumMapEntry e;
        e.inNote  = juce::jlimit (0, 127, (int) row.getProperty ("in", 36));
        e.outNote = juce::jlimit (0, 127, (int) row.getProperty ("out", e.inNote));
        e.channel = (int) row.getProperty ("ch", -1);
        e.name    = row.getProperty ("name", juce::String()).toString();
        e.order   = (int) row.getProperty ("order", (int) entries.size());
        entries.push_back (e);
    }

    if (! entries.empty())
        finalise();
    else
        format = Format::none;
}

} // namespace mh
