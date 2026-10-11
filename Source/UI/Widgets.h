// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace mh
{

/** Rotary control with a caption above and an editable value box below. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
          const juce::String& caption)
        : attachment (state, parameterID, slider)
    {
        name.setText (caption, juce::dontSendNotification);
        name.setFont (theme::labelFont (11.0f));
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, theme::textSecondary);
        addAndMakeVisible (name);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        name.setBounds (bounds.removeFromTop (13));
        slider.setBounds (bounds);
    }

    /** Tooltips have to go on the control that actually receives the mouse, not on the
        wrapper, so each widget forwards the text inwards.
    */
    void setHelp (const juce::String& text)
    {
        slider.setTooltip (text);
        name.setTooltip (text);
    }

    static constexpr int preferredWidth  = 62;
    static constexpr int preferredHeight = 76;

private:
    juce::Label name;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
/** Caption plus drop-down, bound to a choice parameter. */
class ChoiceControl final : public juce::Component
{
public:
    ChoiceControl (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                   const juce::String& caption)
    {
        name.setText (caption, juce::dontSendNotification);
        name.setFont (theme::labelFont (11.0f));
        name.setColour (juce::Label::textColourId, theme::textSecondary);
        addAndMakeVisible (name);

        // The attachment maps the parameter's index onto the box's item list but does
        // not populate it, so the items have to be added before attaching.
        if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (
                state.getParameter (parameterID)))
            box.addItemList (choice->choices, 1);

        addAndMakeVisible (box);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            state, parameterID, box);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        name.setBounds (bounds.removeFromTop (13));
        box.setBounds (bounds.removeFromTop (21));
    }

    void setHelp (const juce::String& text)
    {
        box.setTooltip (text);
        name.setTooltip (text);
    }

    static constexpr int preferredHeight = 36;

private:
    juce::Label name;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceControl)
};

//==============================================================================
class ToggleControl final : public juce::Component
{
public:
    ToggleControl (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                   const juce::String& caption)
        : attachment (state, parameterID, button)
    {
        button.setButtonText (caption);
        addAndMakeVisible (button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    void setHelp (const juce::String& text) { button.setTooltip (text); }

    static constexpr int preferredHeight = 20;

private:
    juce::ToggleButton button;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleControl)
};

//==============================================================================
/** Compact horizontal slider with a caption on the left and the value on the right.
    Used where a whole row of knobs would waste space, such as the per-family feel grid.
*/
class MiniSlider final : public juce::Component
{
public:
    MiniSlider (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                const juce::String& caption, int captionWidth = 0)
        : attachment (state, parameterID, slider), labelWidth (captionWidth)
    {
        if (caption.isNotEmpty())
        {
            name.setText (caption, juce::dontSendNotification);
            name.setFont (theme::labelFont (11.0f));
            name.setColour (juce::Label::textColourId, theme::textSecondary);
            addAndMakeVisible (name);
        }

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        if (name.getText().isNotEmpty())
            name.setBounds (bounds.removeFromLeft (labelWidth > 0 ? labelWidth : 56));

        slider.setBounds (bounds);
    }

    void setHelp (const juce::String& text)
    {
        slider.setTooltip (text);
        name.setTooltip (text);
    }

    static constexpr int preferredHeight = 20;

private:
    juce::Label name;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    int labelWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniSlider)
};

//==============================================================================
/** A knob whose value lives somewhere else.

    The per-lane controls have to edit either a host parameter (the master values) or a
    row of the plugin's lane table, depending on which scope is selected. Rather than
    building and tearing down parameter attachments as the scope changes, the knob reads
    and writes through a pair of callbacks that the control panel re-points.
*/
class ScopedKnob final : public juce::Component
{
public:
    ScopedKnob (const juce::String& caption, double minValue, double maxValue, double step,
                const juce::String& suffix = {},
                std::function<juce::String (double)> textFromValue = {})
    {
        name.setText (caption, juce::dontSendNotification);
        name.setFont (theme::labelFont (11.0f));
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, theme::textSecondary);
        addAndMakeVisible (name);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRange (minValue, maxValue, step);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

        if (suffix.isNotEmpty())
            slider.setTextValueSuffix (suffix);

        if (textFromValue != nullptr)
            slider.textFromValueFunction = std::move (textFromValue);

        // One drag should make one undo step, so the gesture boundaries are reported.
        slider.onDragStart = [this]
        {
            dragging = true;

            if (onGestureStart != nullptr)
                onGestureStart();
        };

        slider.onDragEnd = [this]
        {
            dragging = false;

            if (onGestureEnd != nullptr)
                onGestureEnd();
        };

        slider.onValueChange = [this]
        {
            if (updating)
                return;

            if (setter != nullptr)
                setter (slider.getValue());
        };

        addAndMakeVisible (slider);
    }

    std::function<void()> onGestureStart, onGestureEnd;

    void resized() override
    {
        auto bounds = getLocalBounds();
        name.setBounds (bounds.removeFromTop (13));
        slider.setBounds (bounds);
    }

    void setCallbacks (std::function<double()> newGetter, std::function<void (double)> newSetter)
    {
        getter = std::move (newGetter);
        setter = std::move (newSetter);
        refresh();
    }

    /** Pulls the current value in without firing the setter, so the knob follows host
        automation and scope changes without feeding edits back.
    */
    void refresh()
    {
        if (getter == nullptr)
            return;

        const juce::ScopedValueSetter<bool> guard (updating, true);
        slider.setValue (getter(), juce::dontSendNotification);
    }

    void setDimmed (bool shouldDim)
    {
        slider.setAlpha (shouldDim ? 0.45f : 1.0f);
        name.setAlpha (shouldDim ? 0.45f : 1.0f);
    }

    void setHelp (const juce::String& text)
    {
        slider.setTooltip (text);
        name.setTooltip (text);
    }

    static constexpr int preferredWidth  = 62;
    static constexpr int preferredHeight = 76;

private:
    juce::Label name;
    juce::Slider slider;
    std::function<double()> getter;
    std::function<void (double)> setter;
    bool updating = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopedKnob)
};

//==============================================================================
/** The on/off counterpart of ScopedKnob: a toggle reading and writing through
    callbacks, so it can follow the master or a lane as the scope changes.
*/
class ScopedToggle final : public juce::Component
{
public:
    explicit ScopedToggle (const juce::String& caption)
    {
        button.setButtonText (caption);
        button.onClick = [this]
        {
            if (! updating && setter != nullptr)
                setter (button.getToggleState() ? 1.0 : 0.0);
        };
        addAndMakeVisible (button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    void setCallbacks (std::function<double()> newGetter, std::function<void (double)> newSetter)
    {
        getter = std::move (newGetter);
        setter = std::move (newSetter);
        refresh();
    }

    void refresh()
    {
        if (getter == nullptr)
            return;

        const juce::ScopedValueSetter<bool> guard (updating, true);
        button.setToggleState (getter() > 0.5, juce::dontSendNotification);
    }

    void setDimmed (bool shouldDim) { button.setAlpha (shouldDim ? 0.45f : 1.0f); }
    void setHelp (const juce::String& text) { button.setTooltip (text); }

private:
    juce::ToggleButton button;
    std::function<double()> getter;
    std::function<void (double)> setter;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopedToggle)
};

//==============================================================================
/** Titled container: a dark rounded box with a small caps heading. */
class SectionPanel final : public juce::Component
{
public:
    explicit SectionPanel (juce::String sectionTitle) : title (std::move (sectionTitle)) {}

    /** Section headings name the scope they are editing, so it is never ambiguous
        whether a knob is about to change one lane or every lane.
    */
    void setTitle (const juce::String& newTitle)
    {
        if (title == newTitle)
            return;

        title = newTitle;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (theme::panelBg);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (theme::panelBorder);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        g.setColour (theme::accent);
        g.fillRect (bounds.getX() + 1.0f, bounds.getY() + 1.0f, 2.5f, headerHeight - 2.0f);

        g.setColour (theme::textSecondary);
        g.setFont (theme::labelFont (10.5f, true));
        g.drawText (title.toUpperCase(), 9, 0, getWidth() - 12, (int) headerHeight,
                    juce::Justification::centredLeft, false);
    }

    /** Area below the heading, where the owner should place controls. */
    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().withTrimmedTop ((int) headerHeight).reduced (6, 2);
    }

    static constexpr float headerHeight = 18.0f;

private:
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPanel)
};

} // namespace mh
