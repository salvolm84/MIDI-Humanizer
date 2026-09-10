#include "FactoryPresets.h"

namespace mh
{

namespace
{
    /** GM percussion notes the factory presets refer to. */
    enum GmNote
    {
        kick = 36,
        snare = 38,
        closedHat = 42,
        pedalHat = 44,
        openHat = 46,
        crash = 49,
        ride = 51
    };

    LaneSettings lane (float biasMs, float spreadMs, float velSpread = 0.0f, float swing = 0.0f)
    {
        LaneSettings settings;
        settings.hasOverride = true;
        settings.timingBiasMs = biasMs;
        settings.timingSpreadMs = spreadMs;
        settings.velocitySpread = velSpread;
        settings.swingPercent = swing;
        return settings;
    }
}

std::vector<FactoryPreset> factoryPresets()
{
    std::vector<FactoryPreset> presets;

    {
        FactoryPreset preset;
        preset.name = "Straight (no humanizing)";
        preset.description = "Everything neutral. A clean slate, and a way to hear the clip exactly "
                             "as programmed without switching Humanize off.";
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Tight Studio";
        preset.description = "A well-drilled session player. Small deviations, hats a shade early, "
                             "snare a shade late; the accenting you programmed is left alone.";
        preset.master = lane (0.0f, 4.0f, 7.0f);
        preset.master.accentAmount = 25.0f;
        preset.hands = 30.0f;
        preset.variation = 20.0f;
        // The hat hand runs slightly ahead of the snare hand: the single biggest cue that
        // a kit is being played rather than programmed.
        preset.lanes = { { closedHat, lane (-3.5f, 3.0f, 9.0f) },
                         { openHat,   lane (-3.0f, 3.5f, 9.0f) },
                         { pedalHat,  lane (-3.0f, 3.0f, 7.0f) },
                         { snare,     lane (2.5f, 4.5f, 8.0f) },
                         { kick,      lane (0.0f, 3.0f, 5.0f) } };
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Loose Jazz";
        preset.description = "Swung, quiet and elastic. Ride and hats swing hard, ghost notes fill "
                             "in around the snare, and nothing is quite where the grid says.";
        preset.master = lane (0.0f, 12.0f, 16.0f);
        preset.master.accentAmount = 45.0f;
        preset.ghost = 65.0f;
        preset.ghostVel = 24;
        preset.variation = 55.0f;
        preset.hands = 50.0f;
        preset.flam = 25.0f;
        preset.lanes = { { ride,      lane (-2.0f, 9.0f, 14.0f, 58.0f) },
                         { closedHat, lane (-2.0f, 9.0f, 14.0f, 58.0f) },
                         { snare,     lane (6.0f, 14.0f, 20.0f) },
                         { kick,      lane (1.0f, 8.0f, 10.0f) } };
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Behind The Beat";
        preset.description = "Laid back and heavy. The whole kit sits late, the snare latest of all, "
                             "which is what makes a slow groove feel weighty rather than sleepy.";
        preset.master = lane (9.0f, 7.0f, 12.0f);
        preset.master.accentAmount = 35.0f;
        preset.variation = 30.0f;
        preset.hands = 35.0f;
        preset.lanes = { { snare,     lane (16.0f, 8.0f, 12.0f) },
                         { kick,      lane (4.0f, 5.0f, 9.0f) },
                         { closedHat, lane (7.0f, 6.0f, 12.0f) } };
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Pushed / Urgent";
        preset.description = "Leaning forward. Everything slightly ahead of the grid, tightly played, "
                             "for driving fast material.";
        preset.master = lane (-7.0f, 4.0f, 10.0f);
        preset.master.accentAmount = 40.0f;
        preset.tighten = 35.0f;
        preset.hands = 25.0f;
        preset.variation = 25.0f;
        preset.lanes = { { closedHat, lane (-9.0f, 3.5f, 11.0f) },
                         { snare,     lane (-5.0f, 4.0f, 10.0f) },
                         { kick,      lane (-6.0f, 3.5f, 8.0f) } };
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Drunk Drummer";
        preset.description = "Deliberately too much. Useful as a reference for how far the controls "
                             "go, and for hearing what each one contributes.";
        preset.master = lane (0.0f, 38.0f, 34.0f);
        preset.master.accentAmount = 70.0f;
        preset.flam = 70.0f;
        preset.flamSpread = 34.0f;
        preset.ghost = 85.0f;
        preset.variation = 85.0f;
        preset.hands = 75.0f;
        presets.push_back (preset);
    }

    {
        FactoryPreset preset;
        preset.name = "Level The Kit";
        preset.description = "An example of the Velocity Target control rather than a feel: every "
                             "drum retargeted to a consistent level with its written dynamics kept, "
                             "for taming a clip programmed with wildly uneven velocities.";
        preset.master = lane (0.0f, 5.0f, 8.0f);
        preset.master.velocityTarget = 100;
        preset.master.velocityDynamics = 55.0f;

        auto quietHats = lane (-3.0f, 4.0f, 9.0f);
        quietHats.velocityTarget = 78;
        quietHats.velocityDynamics = 55.0f;

        auto loudCrash = lane (0.0f, 5.0f, 6.0f);
        loudCrash.velocityTarget = 115;
        loudCrash.velocityDynamics = 40.0f;

        preset.lanes = { { closedHat, quietHats }, { openHat, quietHats }, { crash, loudCrash } };
        presets.push_back (preset);
    }

    return presets;
}

const FactoryPreset* findFactoryPreset (const juce::String& name)
{
    static const auto presets = factoryPresets();

    for (auto& preset : presets)
        if (preset.name == name)
            return &preset;

    return nullptr;
}

} // namespace mh
