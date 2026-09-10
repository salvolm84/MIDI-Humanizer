#pragma once

#include "LaneSettings.h"

#include <vector>

namespace mh
{

/** One lane override inside a factory preset, keyed by General MIDI note number.

    Factory presets can only speak GM, since they cannot know what kit the user has
    loaded. A preset's lane entries simply do nothing for a kit that maps its drums
    elsewhere, which is the right failure: the master values still apply.
*/
struct FactoryLane
{
    int          note = 36;
    LaneSettings settings;
};

/** A built-in setup, held as plain data and applied through the processor's own setters.

    These double as documentation. A table of control ranges tells you what Bias does
    arithmetically; "Behind The Beat" tells you what it is *for*.
*/
struct FactoryPreset
{
    juce::String name;
    juce::String description;

    // Master values, which every unlisted lane inherits.
    LaneSettings master;

    // Global controls.
    float tighten    = 0.0f;
    float flam       = 0.0f;
    float flamSpread = 18.0f;
    float ghost      = 0.0f;
    int   ghostVel   = 28;
    float variation  = 0.0f;
    float hands      = 0.0f;

    std::vector<FactoryLane> lanes;
};

/** The built-in presets, in the order they should be listed. */
std::vector<FactoryPreset> factoryPresets();

/** Looks one up by name, or returns nullptr. */
const FactoryPreset* findFactoryPreset (const juce::String& name);

} // namespace mh
