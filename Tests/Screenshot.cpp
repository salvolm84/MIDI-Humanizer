// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Salvatore La Malfa

/*
    Renders the plugin editor to a PNG without needing a host or a visible window, so
    the layout can be inspected from a build script.

    Usage: MidiHumanizerShot <out.png> [clip.mid] [drummap.drm]
*/

#include "../Source/PluginEditor.h"

#include <iostream>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage: MidiHumanizerShot <out.png> [clip.mid] [drummap]" << std::endl;
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    mh::MidiHumanizerProcessor processor;
    juce::String error;

    if (argc > 2)
    {
        const juce::File clip { juce::String (argv[2]) };

        if (! processor.loadClipFromFile (clip, error))
            std::cout << "clip load failed: " << error << std::endl;
        else
            std::cout << "loaded clip: " << clip.getFileName() << " ("
                      << processor.getSourceClip().size() << " notes, "
                      << processor.getSourceClip().lengthPpq << " ppq)" << std::endl;
    }

    if (argc > 3)
    {
        const juce::File map { juce::String (argv[3]) };

        if (! processor.loadDrumMapFromFile (map, error))
            std::cout << "drum map load failed: " << error << std::endl;
        else
            std::cout << "loaded drum map: " << processor.getDrumMapping().getName() << " ("
                      << processor.getDrumMapping().getEntries().size() << " rows)" << std::endl;
    }

    // Remaining arguments configure the render without a host:
    //   paramID=value             a master/global host parameter
    //   laneNN.field=value        one lane's override (field names below)
    //   select=NN                 which lane the control panel should be scoped to
    std::vector<int> lanesToSelect;

    const auto parseNoteList = [] (const juce::String& text)
    {
        std::vector<int> notes;

        for (auto& token : juce::StringArray::fromTokens (text, ",", ""))
            notes.push_back (token.trim().getIntValue());

        return notes;
    };

    const auto laneFieldFromName = [] (const juce::String& name) -> int
    {
        static const char* names[] = { "timeBias", "timeSpread", "swing", "velTarget",
                                       "velDynamics", "velSpread", "accent", "minGap",
                                       "velSpreadUp" };

        for (int i = 0; i < (int) juce::numElementsInArray (names); ++i)
            if (name == names[i])
                return i;

        return -1;
    };

    for (int i = 4; i < argc; ++i)
    {
        const juce::String assignment { argv[i] };
        const auto key = assignment.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto text = assignment.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "select")
        {
            lanesToSelect = parseNoteList (text);
            continue;
        }

        if (key == "preset")
        {
            juce::String presetError;

            if (processor.loadPreset (text, presetError))
                std::cout << "preset: " << text << std::endl;
            else
                std::cout << "preset failed: " << presetError << std::endl;

            continue;
        }

        if (key == "mute" || key == "solo")
        {
            for (auto note : parseNoteList (text))
            {
                if (key == "mute")
                    processor.setLaneMuted (note, true);
                else
                    processor.setLaneSoloed (note, true);
            }

            std::cout << key << ": " << text << std::endl;
            continue;
        }

        if (key == "link")
        {
            const auto notes = parseNoteList (text);
            const auto groupId = processor.linkLanes (notes);
            std::cout << "linked " << notes.size() << " lanes into group " << groupId << std::endl;
            continue;
        }

        // ui.<key>=<value> writes a view option, e.g. ui.laneHeight=40
        if (key.startsWith ("ui."))
        {
            const auto property = key.fromFirstOccurrenceOf (".", false, false);
            processor.uiState.setProperty (property, text.getDoubleValue(), nullptr);
            std::cout << "ui " << property << " = " << text << std::endl;
            continue;
        }

        if (key.startsWith ("lane") && key.contains ("."))
        {
            const auto note = key.fromFirstOccurrenceOf ("lane", false, false)
                                 .upToFirstOccurrenceOf (".", false, false).getIntValue();
            const auto fieldName = key.fromFirstOccurrenceOf (".", false, false);

            auto lane = processor.getResolvedLaneSettings (note);
            lane.hasOverride = true;

            if (fieldName == "enabled")
            {
                lane.enabled = text.getIntValue() != 0;
            }
            else if (const auto field = laneFieldFromName (fieldName); field >= 0)
            {
                mh::setLaneField (lane, (mh::LaneField) field, text.getDoubleValue());
            }
            else
            {
                std::cout << "unknown lane field: " << fieldName << std::endl;
                continue;
            }

            processor.setLaneSettings (note, lane);
            std::cout << "lane " << note << " " << fieldName << " = " << text << std::endl;
            continue;
        }

        if (auto* parameter = processor.apvts.getParameter (key))
        {
            const auto value = (float) text.getDoubleValue();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            std::cout << "set " << key << " = " << value << std::endl;
        }
        else
        {
            std::cout << "unknown parameter: " << key << std::endl;
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cout << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (1320, 940);

    if (auto* humanizerEditor = dynamic_cast<mh::MidiHumanizerEditor*> (editor.get()))
        humanizerEditor->selectLanes (lanesToSelect);

    // Let the processor's recompute timer and any async updates run before drawing.
    for (int i = 0; i < 30; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    // Optional: show the groove drop zone mid-drag, so its highlighted state can be
    // checked without a real drag. Found by component ID rather than by adding test-only
    // API to the editor.
    if (juce::String (argv[1]).contains ("groovedrag"))
    {
        if (auto* zone = editor->findChildWithID ("grooveDrop"))
            if (auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (zone))
            {
                target->fileDragEnter ({ "/tmp/reference.mid" }, 4, 4);
                std::cout << "groove zone: simulated drag" << std::endl;
            }

        for (int i = 0; i < 4; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])));
    out.deleteFile();
    juce::FileOutputStream stream (out);

    if (! stream.openedOk())
    {
        std::cout << "cannot write " << out.getFullPathName() << std::endl;
        return 1;
    }

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, stream))
    {
        std::cout << "png encode failed" << std::endl;
        return 1;
    }

    std::cout << "wrote " << out.getFullPathName() << " (" << image.getWidth() << "x"
              << image.getHeight() << ")" << std::endl;

    editor = nullptr;
    return 0;
}
