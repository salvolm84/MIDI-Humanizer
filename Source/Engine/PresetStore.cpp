#include "PresetStore.h"

#include <algorithm>

namespace mh
{

juce::File PresetStore::getPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("SLM Audio")
               .getChildFile ("MIDI Humanizer")
               .getChildFile ("Presets");
}

juce::File PresetStore::fileForName (const juce::String& name) const
{
    return getPresetDirectory().getChildFile (juce::File::createLegalFileName (name) + fileExtension);
}

std::vector<PresetInfo> PresetStore::getUserPresets() const
{
    std::vector<PresetInfo> presets;
    const auto directory = getPresetDirectory();

    if (! directory.isDirectory())
        return presets;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, juce::String ("*") + fileExtension))
    {
        PresetInfo info;
        info.name = entry.getFile().getFileNameWithoutExtension();
        info.file = entry.getFile();
        info.isFactory = false;
        presets.push_back (info);
    }

    std::sort (presets.begin(), presets.end(), [] (const PresetInfo& a, const PresetInfo& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return presets;
}

juce::ValueTree PresetStore::loadFromFile (const juce::File& file, juce::String& errorMessage) const
{
    if (! file.existsAsFile())
    {
        errorMessage = "Preset not found: " + file.getFileName();
        return {};
    }

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
    {
        errorMessage = file.getFileName() + " is not a readable preset";
        return {};
    }

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.hasType (presetTreeType))
    {
        errorMessage = file.getFileName() + " is not a MIDI Humanizer preset";
        return {};
    }

    return tree;
}

bool PresetStore::save (const juce::String& name, const juce::ValueTree& state,
                        juce::String& errorMessage) const
{
    if (name.trim().isEmpty())
    {
        errorMessage = "Give the preset a name";
        return false;
    }

    auto directory = getPresetDirectory();

    if (! directory.createDirectory())
    {
        errorMessage = "Could not create " + directory.getFullPathName();
        return false;
    }

    const auto xml = state.createXml();

    if (xml == nullptr)
    {
        errorMessage = "Nothing to save";
        return false;
    }

    const auto file = fileForName (name);

    if (! file.replaceWithText (xml->toString()))
    {
        errorMessage = "Could not write " + file.getFileName();
        return false;
    }

    return true;
}

bool PresetStore::deletePreset (const juce::String& name, juce::String& errorMessage) const
{
    const auto file = fileForName (name);

    if (! file.existsAsFile())
    {
        errorMessage = "No such preset: " + name;
        return false;
    }

    if (! file.deleteFile())
    {
        errorMessage = "Could not delete " + file.getFileName();
        return false;
    }

    return true;
}

} // namespace mh
