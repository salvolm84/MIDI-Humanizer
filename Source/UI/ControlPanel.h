// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "../Engine/LaneSettings.h"
#include "Widgets.h"

#include <memory>
#include <vector>

namespace mh
{

class MidiHumanizerProcessor;

/** All the humanization controls.

    The timing, velocity and playability controls are **scoped**: they edit either the
    master values that every lane inherits, or one lane's own override. A scope bar at
    the top picks which, and each section heading names the current scope so it is never
    ambiguous whether a knob is about to change one drum or the whole kit.

    Controls that are genuinely global — the grids, flams, ghosts, variation, hand
    alternation — are always bound straight to their host parameters.
*/
class ControlPanel final : public juce::Component
{
public:
    explicit ControlPanel (MidiHumanizerProcessor& processor);

    /** An empty list selects the master values; otherwise these lanes are edited as a
        unit, with the knobs showing the last one's values.
    */
    void setScope (std::vector<int> noteNumbers, const juce::String& laneLabel);
    int  getScope() const noexcept { return scopeNote; }

    /** Pulls current values into every scoped knob. Called from the editor's timer so
        the knobs follow host automation and any change made elsewhere.
    */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Fired when a lane's settings change, so the lane list can repaint its badges. */
    std::function<void()> onLaneEdited;

    /** Fired when the scope changes from inside the panel (the MASTER button). */
    std::function<void (std::vector<int>)> onScopeChanged;

    /** Fired when lanes are linked or unlinked, so the list can rebuild its badges. */
    std::function<void()> onLinkChanged;

    static constexpr int preferredHeight = 266;

private:
    struct Column
    {
        std::unique_ptr<SectionPanel> panel;
        std::vector<juce::Component*> knobs;      ///< laid out in a row
        std::vector<juce::Component*> stacked;    ///< one per row underneath
        int weight = 1;
        juce::String baseTitle;
        bool scoped = false;                      ///< heading shows the current scope
    };

    Column& addColumn (const juce::String& title, int weight, bool scoped);

    void buildScopeBar();
    void notifyLaneEdited();
    void updateStatsLabel();
    void rewireScopedKnobs();
    void updateScopeUi();

    /** Reads a scoped value: from the host parameter at master scope, otherwise from the
        selected lane's resolved settings.
    */
    double readScoped (LaneField field) const;

    /** Writes a scoped value. Touching a lane control makes that lane a manual lane,
        because otherwise the edit would be silently discarded on the next recompute.
    */
    void writeScoped (LaneField field, double value);

    MidiHumanizerProcessor& plugin;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Column> columns;
    std::array<ScopedKnob*, (size_t) numLaneFields> scopedKnobs {};   ///< null for non-knob fields
    ScopedToggle* spreadUpToggle = nullptr;

    // Scope bar
    juce::TextButton masterButton { "MASTER" };
    juce::Label scopeLabel;
    juce::ToggleButton overrideButton { "Override" }, laneOnButton { "Humanize" },
                       muteButton { "Mute" }, soloButton { "Solo" };
    juce::TextButton copyFromMasterButton { "From master" },
                     copyToAllButton { "To all lanes" },
                     resetLaneButton { "Reset" },
                     linkButton { "Link" },
                     unlinkButton { "Unlink" },
                     rerollLaneButton { "Re-roll" };

    /** Measured result for the current scope, so the Velocity Target promise is visible
        rather than taken on trust.
    */
    juce::Label statsLabel;

    /** Every lane an edit is written to. Empty means master scope. */
    std::vector<int> scopeLanes;

    /** The lane the knobs display, which is the last one selected. */
    int scopeNote = -1;
    juce::String scopeName;

    template <typename ControlType, typename... Args>
    ControlType* create (Args&&... args)
    {
        auto control = std::make_unique<ControlType> (std::forward<Args> (args)...);
        auto* raw = control.get();
        addAndMakeVisible (*raw);
        owned.push_back (std::move (control));
        return raw;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanel)
};

} // namespace mh
