// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#include "ControlPanel.h"

#include "../ParamIDs.h"
#include "../PluginProcessor.h"

namespace mh
{

namespace
{
    /** One table describing every scoped control: which lane field it edits, which host
        parameter holds the master value, and how it reads on screen.
    */
    struct ScopedControlInfo
    {
        LaneField   field;
        const char* caption;
        const char* parameterId;
        double      minValue, maxValue, step;
        const char* suffix;
        const char* help;
    };

    const ScopedControlInfo& scopedControl (LaneField field)
    {
        static const ScopedControlInfo table[] =
        {
            { LaneField::timingBias,       "Bias",     pid::timeBias,    -60.0,  60.0, 0.1, " ms",
              "Where this drum sits on average. Negative plays ahead of the beat, positive "
              "behind it. This is the feel control: a drummer's hi-hat hand usually sits "
              "slightly early while the snare hand sits late." },
            { LaneField::timingSpread,     "Spread",   pid::timeSpread,    0.0,  60.0, 0.1, " ms",
              "How far individual hits wander either side of the bias. Deviations cluster "
              "near the bias rather than spreading evenly, which is how real playing behaves. "
              "Set this to zero for a perfectly tight lane." },
            { LaneField::swing,            "Swing",    pid::swing,      -100.0, 100.0, 0.1, " %",
              "Delays the off-beats of the swing grid. At +100% the off-beat lands on a "
              "triplet; negative values pull it early instead. Per lane, so the hats can "
              "swing while the kick stays straight." },
            { LaneField::velocityTarget,   "Target",   pid::velTarget,     0.0, 127.0, 1.0, "",
              "Off leaves the clip's own levels alone. Any other value retargets this drum "
              "so its AVERAGE velocity becomes that number. Use Dynamics to decide how much "
              "of your written accenting survives." },
            { LaneField::velocityDynamics, "Dynamics", pid::velDynamics,   0.0, 100.0, 0.1, " %",
              "How much of the programmed accenting survives a retarget. At 0% every hit "
              "lands on the Target. At 100% your dynamics are kept intact and merely "
              "re-centred on it. Only does anything when Target is set." },
            { LaneField::velocitySpread,   "Spread",   pid::velSpread,     0.0,  63.0, 0.5, "",
              "How far each hit's velocity wanders either side of its level, in velocity "
              "units. Zero makes every hit exactly as loud as its level says." },
            { LaneField::accent,           "Accents",  pid::accent,        0.0, 100.0, 0.1, " %",
              "Lifts hits that fall on strong grid positions and drops the weak ones. "
              "Measured from where a note was written, before swing and spread move it, so "
              "the accent always reflects the intended beat." },
            { LaneField::minGap,           "Min gap",  pid::minGap,        0.0,  80.0, 0.5, " ms",
              "This drum cannot be struck twice inside this time. Hits that break the rule "
              "are dropped, keeping the quieter one out. Flam pairs are exempt. Applies even "
              "when the lane's humanization is switched off." },
        };

        static_assert (juce::numElementsInArray (table) == (size_t) numLaneFields,
                       "every lane field needs a scoped control");

        return table[(size_t) juce::jlimit (0, numLaneFields - 1, (int) field)];
    }

    /** A velocity target of zero means "leave the recorded levels alone", which needs
        saying in words rather than showing as a misleading 0.
    */
    juce::String velocityTargetText (double value)
    {
        const auto target = (int) std::lround (value);
        return target <= 0 ? "Off" : juce::String (target);
    }

    void styleSmallButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::controlBg);
    }
}

//==============================================================================
ControlPanel::Column& ControlPanel::addColumn (const juce::String& title, int weight, bool scoped)
{
    Column column;
    column.panel = std::make_unique<SectionPanel> (title);
    column.weight = weight;
    column.baseTitle = title;
    column.scoped = scoped;
    addAndMakeVisible (*column.panel);
    columns.push_back (std::move (column));
    return columns.back();
}

ControlPanel::ControlPanel (MidiHumanizerProcessor& processor)
    : plugin (processor)
{
    buildScopeBar();

    const auto makeScopedKnob = [this] (LaneField field)
    {
        const auto& info = scopedControl (field);

        auto* knob = field == LaneField::velocityTarget
            ? create<ScopedKnob> (info.caption, info.minValue, info.maxValue, info.step,
                                  juce::String (info.suffix), velocityTargetText)
            : create<ScopedKnob> (info.caption, info.minValue, info.maxValue, info.step,
                                  juce::String (info.suffix));

        knob->setHelp (info.help);
        knob->onGestureStart = [this] { plugin.setUndoSuppressed (true); };
        knob->onGestureEnd   = [this] { plugin.setUndoSuppressed (false); };
        scopedKnobs[(size_t) (int) field] = knob;
        return static_cast<juce::Component*> (knob);
    };

    // Scoped and global controls never share a box: a heading that names one drum must
    // not sit above a knob that changes the whole kit.
    {
        auto& column = addColumn ("Timing", 4, true);
        column.knobs = { makeScopedKnob (LaneField::timingBias),
                         makeScopedKnob (LaneField::timingSpread),
                         makeScopedKnob (LaneField::swing),
                         makeScopedKnob (LaneField::minGap) };
    }

    {
        auto& column = addColumn ("Velocity", 4, true);
        column.knobs = { makeScopedKnob (LaneField::velocityTarget),
                         makeScopedKnob (LaneField::velocityDynamics),
                         makeScopedKnob (LaneField::velocitySpread),
                         makeScopedKnob (LaneField::accent) };
    }

    {
        auto& column = addColumn ("Grid + limits  (all lanes)", 3, false);

        auto* tighten = create<Knob> (plugin.apvts, pid::tighten, "Tighten");
        tighten->setHelp ("Pulls every note toward the grid before any deviation is added, so a "
                          "loosely played source clip can be made tight first and then humanized "
                          "deliberately. At 100% the clip is fully quantized.");

        auto* tightenGrid = create<ChoiceControl> (plugin.apvts, pid::tightenDiv, "Tighten grid");
        tightenGrid->setHelp ("Which subdivision Tighten quantizes to.");

        auto* swingGrid = create<ChoiceControl> (plugin.apvts, pid::swingDiv, "Swing grid");
        swingGrid->setHelp ("Which subdivision the per-lane Swing control works against. Notes that "
                            "are not really on this grid, such as triplet fills, are left alone.");

        auto* limits = create<ToggleControl> (plugin.apvts, pid::limits, "Enforce limits");
        limits->setHelp ("Master switch for the physical rules: the per-lane Min gap and the choke "
                         "groups. Turn it off to allow results a real kit could not play.");

        auto* choke = create<ToggleControl> (plugin.apvts, pid::choke, "Choke groups");
        choke->setHelp ("A closed or pedal hi-hat cuts short a ringing open hat, the way a real "
                        "hi-hat does.");

        column.knobs = { tighten };
        column.stacked = { tightenGrid, swingGrid, limits, choke };
    }

    {
        auto& column = addColumn ("Realism  (all lanes)", 6, false);

        struct GlobalKnob { const char* id; const char* caption; const char* help; };

        const GlobalKnob globals[] =
        {
            { pid::flam, "Flams",
              "Two hits written on the same drum at the same instant are physically a flam, so "
              "one stick is moved earlier and made quieter instead of both landing together." },
            { pid::flamSpread, "Spread",
              "How far apart the two halves of a flam sit, in milliseconds." },
            { pid::ghost, "Ghosts",
              "Adds quiet snare notes on empty sixteenths near existing snare hits. Never on a "
              "beat, and never in a bar with no snare nearby, so they read as fills rather than "
              "random extra notes. Shown as hollow diamonds in the grid." },
            { pid::ghostVel, "Ghost vel", "How loud generated ghost notes are." },
            { pid::variation, "Variation",
              "Breaks up runs of identical repeated hits by varying their velocity, so a "
              "programmed sixteenth line stops sounding machine-gunned." },
            { pid::hands, "Hands",
              "Models alternating hands in fast runs: the weaker hand lands slightly late and "
              "slightly softer. The other cure for machine-gun hi-hats." },
        };

        for (auto& global : globals)
        {
            auto* knob = create<Knob> (plugin.apvts, global.id, global.caption);
            knob->setHelp (global.help);
            column.knobs.push_back (knob);
        }
    }

    rewireScopedKnobs();
    updateScopeUi();
}

void ControlPanel::notifyLaneEdited()
{
    refresh();
    updateScopeUi();

    if (onLaneEdited != nullptr)
        onLaneEdited();
}

void ControlPanel::buildScopeBar()
{
    masterButton.onClick = [this]
    {
        setScope ({}, {});

        if (onScopeChanged != nullptr)
            onScopeChanged ({});
    };
    styleSmallButton (masterButton);
    masterButton.setTooltip ("Edit the master values that every drum inherits. Click a drum in the "
                             "list on the left to give that one its own settings instead.");
    addAndMakeVisible (masterButton);

    scopeLabel.setFont (theme::labelFont (12.0f, true));
    scopeLabel.setColour (juce::Label::textColourId, theme::textPrimary);
    addAndMakeVisible (scopeLabel);

    statsLabel.setFont (theme::labelFont (11.0f));
    statsLabel.setColour (juce::Label::textColourId, theme::textDim);
    statsLabel.setTooltip ("What actually came out for the drums in scope, measured on the finished "
                           "clip. Use it to confirm a Velocity Target really landed where you asked.");
    addAndMakeVisible (statsLabel);

    overrideButton.onClick = [this]
    {
        if (scopeNote < 0)
            return;

        for (auto note : scopeLanes)
            plugin.setLaneOverride (note, overrideButton.getToggleState());

        refresh();

        if (onLaneEdited != nullptr)
            onLaneEdited();
    };
    overrideButton.setTooltip ("Off, this drum follows the master values. On, it uses its own. "
                               "Switching it on copies the master values across first, so nothing "
                               "changes until you actually move a control.");
    addAndMakeVisible (overrideButton);

    laneOnButton.onClick = [this]
    {
        if (scopeNote < 0)
            return;

        for (auto note : scopeLanes)
            plugin.setLaneEnabled (note, laneOnButton.getToggleState());

        updateScopeUi();

        if (onLaneEdited != nullptr)
            onLaneEdited();
    };
    laneOnButton.setTooltip ("Off, this drum passes through with exactly the timing and velocity you "
                             "programmed. The physical rules, Min gap and choke groups, still apply.");
    addAndMakeVisible (laneOnButton);

    muteButton.onClick = [this]
    {
        for (auto note : scopeLanes)
            plugin.setLaneMuted (note, muteButton.getToggleState());

        notifyLaneEdited();
    };
    muteButton.setTooltip ("Silences these drums without removing them. Muted notes still appear in "
                           "the grid, drawn hollow, so nothing seems to vanish. Separate from "
                           "Humanize, which is about varying the drum rather than hearing it.");
    addAndMakeVisible (muteButton);

    soloButton.onClick = [this]
    {
        for (auto note : scopeLanes)
            plugin.setLaneSoloed (note, soloButton.getToggleState());

        notifyLaneEdited();
    };
    soloButton.setTooltip ("Hear only these drums. The quickest way to judge one drum's feel is to "
                           "listen to it alone. Soloing anywhere silences everything not soloed.");
    addAndMakeVisible (soloButton);

    rerollLaneButton.onClick = [this]
    {
        plugin.rerollLanes (scopeLanes);
        notifyLaneEdited();
    };
    styleSmallButton (rerollLaneButton);
    rerollLaneButton.setTooltip ("Deal a new random hand for these drums only, leaving the rest of "
                                 "the kit's performance exactly as it is.");
    addAndMakeVisible (rerollLaneButton);

    copyFromMasterButton.onClick = [this]
    {
        if (scopeNote < 0)
            return;

        for (auto note : scopeLanes)
        {
            auto lane = plugin.getMasterLaneSettings();
            lane.enabled = plugin.getLaneSettings (note).enabled;
            lane.groupId = plugin.getLaneGroup (note);
            lane.hasOverride = true;
            plugin.setLaneSettings (note, lane);
        }

        refresh();
        updateScopeUi();

        if (onLaneEdited != nullptr)
            onLaneEdited();
    };
    styleSmallButton (copyFromMasterButton);
    copyFromMasterButton.setTooltip ("Copy the master values onto this drum.");
    addAndMakeVisible (copyFromMasterButton);

    copyToAllButton.onClick = [this]
    {
        if (scopeNote < 0)
            return;

        plugin.copyLaneToAllLanes (scopeNote, true);

        if (onLaneEdited != nullptr)
            onLaneEdited();
    };
    styleSmallButton (copyToAllButton);
    copyToAllButton.setTooltip ("Copy this drum's values onto every other drum. The quickest way to "
                                "set up a whole kit. Each lane keeps its own on/off state.");
    addAndMakeVisible (copyToAllButton);

    resetLaneButton.onClick = [this]
    {
        if (scopeNote < 0)
            return;

        for (auto note : scopeLanes)
            plugin.resetLane (note);

        refresh();
        updateScopeUi();

        if (onLaneEdited != nullptr)
            onLaneEdited();
    };
    styleSmallButton (resetLaneButton);
    resetLaneButton.setTooltip ("Drop the override on the selected drums and go back to inheriting "
                                "the master values.");
    addAndMakeVisible (resetLaneButton);

    linkButton.onClick = [this]
    {
        if (scopeLanes.size() < 2)
            return;

        const auto groupId = plugin.linkLanes (scopeLanes);
        juce::ignoreUnused (groupId);

        refresh();
        updateScopeUi();

        if (onLinkChanged != nullptr)
            onLinkChanged();
    };
    styleSmallButton (linkButton);
    linkButton.setTooltip ("Link the selected drums so they are edited as one. Afterwards, clicking "
                           "any of them selects the whole group and they share a colour in the grid. "
                           "Select several with Cmd-click, a range with Shift-click, or a whole drum "
                           "family with a double-click.");
    addAndMakeVisible (linkButton);

    unlinkButton.onClick = [this]
    {
        if (scopeLanes.empty())
            return;

        plugin.unlinkLanes (scopeLanes);
        refresh();
        updateScopeUi();

        if (onLinkChanged != nullptr)
            onLinkChanged();
    };
    styleSmallButton (unlinkButton);
    unlinkButton.setTooltip ("Break the link on the selected drums so each goes back to having its "
                             "own settings.");
    addAndMakeVisible (unlinkButton);
}

//==============================================================================
double ControlPanel::readScoped (LaneField field) const
{
    if (scopeNote < 0)
    {
        if (auto* value = plugin.apvts.getRawParameterValue (scopedControl (field).parameterId))
            return (double) value->load();

        return 0.0;
    }

    return getLaneField (plugin.getResolvedLaneSettings (scopeNote), field);
}

void ControlPanel::writeScoped (LaneField field, double value)
{
    if (scopeNote < 0)
    {
        if (auto* parameter = plugin.apvts.getParameter (scopedControl (field).parameterId))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));

        return;
    }

    // Editing a lane control implies the lane wants its own values; without this the
    // edit would be overwritten by the master on the next recompute. Every lane in scope
    // gets the change, which is what makes a multi-selection or a link group behave as a
    // single control.
    for (auto note : scopeLanes)
    {
        auto lane = plugin.getResolvedLaneSettings (note);
        lane.hasOverride = true;
        setLaneField (lane, field, value);
        plugin.setLaneSettings (note, lane);
    }

    updateScopeUi();

    if (onLaneEdited != nullptr)
        onLaneEdited();
}

void ControlPanel::rewireScopedKnobs()
{
    for (int i = 0; i < numLaneFields; ++i)
    {
        const auto field = (LaneField) i;

        if (auto* knob = scopedKnobs[(size_t) i])
            knob->setCallbacks ([this, field] { return readScoped (field); },
                                [this, field] (double value) { writeScoped (field, value); });
    }
}

void ControlPanel::setScope (std::vector<int> noteNumbers, const juce::String& laneLabel)
{
    scopeLanes = std::move (noteNumbers);
    scopeNote = scopeLanes.empty() ? -1 : juce::jlimit (0, 127, scopeLanes.back());
    scopeName = laneLabel;

    rewireScopedKnobs();
    updateScopeUi();
}

void ControlPanel::updateScopeUi()
{
    const auto isMaster = scopeLanes.empty();
    const auto groupId = isMaster ? 0 : plugin.getLaneGroup (scopeNote);
    const auto multiple = scopeLanes.size() > 1;

    juce::String scopeText;

    if (isMaster)
    {
        scopeText = "MASTER  -  every lane inherits these";
    }
    else if (groupId > 0)
    {
        scopeText << "LINKED GROUP "
                  << juce::String::charToString ((juce::juce_wchar) ('A' + (groupId - 1) % 26))
                  << "  -  " << juce::String ((int) plugin.getLanesInGroup (groupId).size())
                  << " drums, edited as one";
    }
    else if (multiple)
    {
        scopeText << juce::String ((int) scopeLanes.size()) << " drums selected  -  edited together";
    }
    else
    {
        scopeText << (scopeName.isNotEmpty() ? scopeName : drumName (scopeNote))
                  << "  (note " << juce::String (scopeNote) << ")";
    }

    scopeLabel.setText (scopeText, juce::dontSendNotification);
    scopeLabel.setColour (juce::Label::textColourId,
                          groupId > 0 ? theme::groupColour (groupId) : theme::textPrimary);
    masterButton.setToggleState (isMaster, juce::dontSendNotification);

    overrideButton.setEnabled (! isMaster);
    laneOnButton.setEnabled (! isMaster);
    copyFromMasterButton.setEnabled (! isMaster);
    copyToAllButton.setEnabled (! isMaster);
    resetLaneButton.setEnabled (! isMaster);

    // Linking needs at least two lanes; unlinking needs an existing group.
    linkButton.setEnabled (multiple && groupId == 0);

    auto anyLinked = false;

    for (auto note : scopeLanes)
        if (plugin.getLaneGroup (note) > 0)
            anyLinked = true;

    unlinkButton.setEnabled (anyLinked);
    rerollLaneButton.setEnabled (! isMaster);
    muteButton.setEnabled (! isMaster);
    soloButton.setEnabled (! isMaster);

    if (! isMaster)
    {
        const auto lead = plugin.getLaneSettings (scopeNote);
        muteButton.setToggleState (lead.muted, juce::dontSendNotification);
        soloButton.setToggleState (lead.soloed, juce::dontSendNotification);
    }
    else
    {
        muteButton.setToggleState (false, juce::dontSendNotification);
        soloButton.setToggleState (false, juce::dontSendNotification);
    }

    updateStatsLabel();

    auto laneEnabled = true;

    if (! isMaster)
    {
        const auto stored = plugin.getLaneSettings (scopeNote);
        overrideButton.setToggleState (stored.hasOverride, juce::dontSendNotification);
        laneOnButton.setToggleState (stored.enabled, juce::dontSendNotification);
        laneEnabled = stored.enabled;
    }
    else
    {
        overrideButton.setToggleState (false, juce::dontSendNotification);
        laneOnButton.setToggleState (true, juce::dontSendNotification);
    }

    // A lane that is switched off is not going to be humanized, so its knobs say so.
    for (auto* knob : scopedKnobs)
        if (knob != nullptr)
            knob->setDimmed (! laneEnabled);

    juce::String suffix;

    if (isMaster)
        suffix = " -  master";
    else if (groupId > 0)
        suffix = "  -  GROUP " + juce::String::charToString ((juce::juce_wchar) ('A' + (groupId - 1) % 26));
    else if (multiple)
        suffix = "  -  " + juce::String ((int) scopeLanes.size()) + " DRUMS";
    else
        suffix = "  -  " + (scopeName.isNotEmpty() ? scopeName : drumName (scopeNote));

    for (auto& column : columns)
        if (column.scoped)
            column.panel->setTitle (column.baseTitle + suffix);
}

void ControlPanel::updateStatsLabel()
{
    if (scopeLanes.empty())
    {
        const auto& report = plugin.getReport();
        juce::String text;
        text << juce::String (plugin.getHumanizedClip().size()) << " notes";

        if (report.peakTimingShiftMs > 0.01)
            text << "   peak shift " << juce::String (report.peakTimingShiftMs, 1) << " ms";

        statsLabel.setText (text, juce::dontSendNotification);
        return;
    }

    // Aggregate across the scope, so a linked group reports as the single thing it is.
    const auto& report = plugin.getReport();
    auto hits = 0;
    auto shiftTotal = 0.0;
    auto velocityTotal = 0.0;
    auto lowest = 128;
    auto highest = 0;

    for (auto note : scopeLanes)
    {
        const auto& stats = report.forNote (note);

        if (stats.hits == 0)
            continue;

        hits += stats.hits;
        shiftTotal += stats.meanShiftMs * (double) stats.hits;
        velocityTotal += stats.meanVelocity * (double) stats.hits;
        lowest = juce::jmin (lowest, stats.minVelocity);
        highest = juce::jmax (highest, stats.maxVelocity);
    }

    if (hits == 0)
    {
        statsLabel.setText ("no hits on this lane", juce::dontSendNotification);
        return;
    }

    juce::String text;
    text << hits << (hits == 1 ? " hit" : " hits")
         << "   shift " << (shiftTotal >= 0.0 ? "+" : "")
         << juce::String (shiftTotal / (double) hits, 1) << " ms"
         << "   vel " << lowest << "-" << highest
         << " (avg " << juce::roundToInt (velocityTotal / (double) hits) << ")";

    statsLabel.setText (text, juce::dontSendNotification);
}

void ControlPanel::refresh()
{
    for (auto* knob : scopedKnobs)
        if (knob != nullptr)
            knob->refresh();

    updateStatsLabel();
}

//==============================================================================
void ControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::windowBg);
}

void ControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced (6, 5);

    // ---- scope bar ----------------------------------------------------------------
    {
        auto row = bounds.removeFromTop (22);

        masterButton.setBounds (row.removeFromLeft (68));
        row.removeFromLeft (8);

        resetLaneButton.setBounds (row.removeFromRight (52));
        row.removeFromRight (3);
        copyToAllButton.setBounds (row.removeFromRight (78));
        row.removeFromRight (3);
        copyFromMasterButton.setBounds (row.removeFromRight (82));
        row.removeFromRight (3);
        rerollLaneButton.setBounds (row.removeFromRight (62));
        row.removeFromRight (8);
        unlinkButton.setBounds (row.removeFromRight (56));
        row.removeFromRight (3);
        linkButton.setBounds (row.removeFromRight (44));
        row.removeFromRight (8);
        soloButton.setBounds (row.removeFromRight (52));
        row.removeFromRight (3);
        muteButton.setBounds (row.removeFromRight (56));
        row.removeFromRight (3);
        laneOnButton.setBounds (row.removeFromRight (82));
        row.removeFromRight (3);
        overrideButton.setBounds (row.removeFromRight (78));
        row.removeFromRight (8);

        scopeLabel.setBounds (row);

        // Measured results sit under the scope name, where the eye already is.
        statsLabel.setBounds (bounds.removeFromTop (15).withTrimmedLeft (76));
        bounds.removeFromTop (3);
    }

    auto totalWeight = 0;

    for (auto& column : columns)
        totalWeight += column.weight;

    if (totalWeight <= 0)
        return;

    const auto gap = 6;
    const auto available = bounds.getWidth() - gap * ((int) columns.size() - 1);

    for (size_t i = 0; i < columns.size(); ++i)
    {
        auto& column = columns[i];

        const auto width = i + 1 == columns.size()
            ? bounds.getWidth()
            : available * column.weight / totalWeight;

        auto columnBounds = bounds.removeFromLeft (width);

        if (i + 1 < columns.size())
            bounds.removeFromLeft (gap);

        column.panel->setBounds (columnBounds);

        // The controls are children of the ControlPanel, not of the section box, so the
        // box's content rectangle has to be translated out of its local space.
        auto content = column.panel->getContentBounds()
                           .translated (columnBounds.getX(), columnBounds.getY());

        // Measure the stack for real: drop-downs are nearly twice a toggle's height, and
        // assuming otherwise silently clips the last control off the bottom.
        const auto heightOf = [] (juce::Component* control)
        {
            return dynamic_cast<ChoiceControl*> (control) != nullptr ? ChoiceControl::preferredHeight
                                                                    : ToggleControl::preferredHeight;
        };

        auto stackedHeight = 0;

        for (auto* control : column.stacked)
            stackedHeight += heightOf (control) + 2;

        if (! column.knobs.empty())
        {
            const auto knobHeight = juce::jmin (Knob::preferredHeight,
                                                content.getHeight() - stackedHeight - 4);
            auto knobRow = content.removeFromTop (juce::jmax (48, knobHeight));
            const auto knobWidth = knobRow.getWidth() / (int) column.knobs.size();

            for (auto* knob : column.knobs)
                knob->setBounds (knobRow.removeFromLeft (knobWidth));

            content.removeFromTop (4);
        }

        for (auto* control : column.stacked)
        {
            const auto height = heightOf (control);
            control->setBounds (content.removeFromTop (juce::jmin (height, juce::jmax (0, content.getHeight()))));
            content.removeFromTop (2);
        }
    }
}

} // namespace mh
