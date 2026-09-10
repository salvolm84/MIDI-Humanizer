// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace mh
{

/** A named humanization setup: master values plus every per-lane override.

    Deliberately excludes the clip, the drum map and the view settings. A preset describes
    a *feel*, so it has to be applicable to any clip; baking the clip into it would make it
    useless for the thing presets are for.
*/
struct PresetInfo
{
    juce::String name;
    bool         isFactory = false;
    juce::File   file;        ///< invalid for factory presets
};

/** Finds, reads and writes user presets on disk.

    Factory presets are plain data rather than files (see FactoryPresets.h) and are applied
    through the processor's own setters, so they cannot drift out of step with whatever
    format the parameter state happens to use.
*/
class PresetStore
{
public:
    PresetStore() = default;

    static juce::File getPresetDirectory();

    /** User presets on disk, alphabetically. */
    std::vector<PresetInfo> getUserPresets() const;

    juce::ValueTree loadFromFile (const juce::File& file, juce::String& errorMessage) const;
    bool save (const juce::String& name, const juce::ValueTree& state, juce::String& errorMessage) const;
    bool deletePreset (const juce::String& name, juce::String& errorMessage) const;

    juce::File fileForName (const juce::String& name) const;

    static constexpr auto presetTreeType = "MidiHumanizerPreset";
    static constexpr auto fileExtension = ".mhpreset";
};

} // namespace mh
