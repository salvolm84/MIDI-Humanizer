cmake_minimum_required(VERSION 3.22)

project(MidiHumanizer VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Point this at your JUCE checkout, or pass -DJUCE_DIR=/path/to/JUCE
set(JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE source tree")

option(MIDIHUMANIZER_BUILD_INSTRUMENT_VARIANT
       "Also build an instrument-flavoured VST3 for hosts that only route MIDI out of instrument slots (Cubase)"
       ON)
option(MIDIHUMANIZER_BUILD_TOOLS
       "Build the console test and offscreen-render tools"
       ON)

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at '${JUCE_DIR}'. Re-run cmake with -DJUCE_DIR=/path/to/JUCE")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

#===============================================================================
# Shared sources. Header-only helpers not listed here: Source/ParamIDs.h,
# Source/Model/GridDivision.h, Source/UI/Widgets.h.
set(MH_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Model/DrumMap.cpp
    Source/Model/NoteSequence.cpp
    Source/Model/MidiClipIO.cpp
    Source/Model/DrumMapping.cpp
    Source/Engine/Humanizer.cpp
    Source/Engine/ClipPlayer.cpp
    Source/Engine/LaneSettings.cpp
    Source/Engine/GrooveExtractor.cpp
    Source/Engine/PresetStore.cpp
    Source/Engine/FactoryPresets.cpp
    Source/Engine/EditHistory.cpp
    Source/UI/TimelineView.cpp
    Source/UI/GridEditor.cpp
    Source/UI/VelocityLane.cpp
    Source/UI/LaneHeader.cpp
    Source/UI/GridToolbar.cpp
    Source/UI/ControlPanel.cpp
    Source/UI/Theme.cpp)

# Lets Reaper's Cmd+Option item drag (a macOS promised-file drag) land on the editor.
# Deliberately no enable_language(OBJCXX): that would move JUCE's own .mm modules to a
# language that misses the hidden-visibility preset, so the plugin would export JUCE's
# symbols and clash with other JUCE binaries in the same host. As CXX sources, clang
# still compiles .mm files as Objective-C++.
if(APPLE)
    list(APPEND MH_SOURCES Source/UI/PromisedFileDrop_mac.mm)
endif()

set(MH_ENGINE_SOURCES
    Source/Model/DrumMap.cpp
    Source/Model/NoteSequence.cpp
    Source/Model/MidiClipIO.cpp
    Source/Model/DrumMapping.cpp
    Source/Engine/Humanizer.cpp
    Source/Engine/ClipPlayer.cpp
    Source/Engine/LaneSettings.cpp
    Source/Engine/GrooveExtractor.cpp
    Source/Engine/PresetStore.cpp
    Source/Engine/FactoryPresets.cpp
    Source/Engine/EditHistory.cpp)

set(MH_PLUGIN_MODULES
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra)

#===============================================================================
# Builds one flavour of the plugin. `midi_effect` picks between a true MIDI effect
# (no audio buses) and an instrument-slot build carrying a silent output bus.
function(mh_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_ID;MIDI_EFFECT;IS_SYNTH" "FORMATS" ${ARGN})

    juce_add_plugin(${target}
        PRODUCT_NAME             "${ARG_PRODUCT_NAME}"
        COMPANY_NAME             "SLM Audio"
        BUNDLE_ID                "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE Slma
        PLUGIN_CODE              "${ARG_PLUGIN_CODE}"
        FORMATS                  ${ARG_FORMATS}
        IS_SYNTH                 ${ARG_IS_SYNTH}
        IS_MIDI_EFFECT           ${ARG_MIDI_EFFECT}
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD  TRUE)

    target_sources(${target} PRIVATE ${MH_SOURCES})
    target_include_directories(${target} PRIVATE Source)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(${target}
        PRIVATE ${MH_PLUGIN_MODULES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# Primary build: a real MIDI effect. Reaper, Bitwig, Studio One and FL take this in a
# MIDI FX slot. Standalone is included for UI work without a host.
mh_add_plugin(MidiHumanizer
    PRODUCT_NAME "MIDI Humanizer"
    PLUGIN_CODE  Mhmz
    BUNDLE_ID    "com.slmaudio.midihumanizer"
    MIDI_EFFECT  TRUE
    IS_SYNTH     FALSE
    FORMATS      VST3 Standalone)

# Cubase will not load a third-party VST3 MIDI effect into a MIDI Insert slot, so this
# variant presents itself as an instrument with a silent output and routable MIDI out.
if(MIDIHUMANIZER_BUILD_INSTRUMENT_VARIANT)
    mh_add_plugin(MidiHumanizerInst
        PRODUCT_NAME "MIDI Humanizer Inst"
        PLUGIN_CODE  Mhmi
        BUNDLE_ID    "com.slmaudio.midihumanizer.inst"
        MIDI_EFFECT  FALSE
        IS_SYNTH     TRUE
        FORMATS      VST3)
endif()

#===============================================================================
if(MIDIHUMANIZER_BUILD_TOOLS)
    # Console checks for the pure-logic layers: no GUI, no plugin host needed.
    juce_add_console_app(MidiHumanizerTests PRODUCT_NAME "MidiHumanizerTests")
    target_sources(MidiHumanizerTests PRIVATE Tests/EngineTests.cpp ${MH_ENGINE_SOURCES})
    target_compile_definitions(MidiHumanizerTests PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(MidiHumanizerTests
        PRIVATE juce::juce_audio_basics juce::juce_data_structures juce::juce_events
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Offscreen renderer: draws the editor to a PNG so layout can be checked in CI.
    juce_add_console_app(MidiHumanizerShot PRODUCT_NAME "MidiHumanizerShot")
    target_sources(MidiHumanizerShot PRIVATE Tests/Screenshot.cpp ${MH_SOURCES})
    target_include_directories(MidiHumanizerShot PRIVATE Source)
    target_compile_definitions(MidiHumanizerShot PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MidiHumanizerShot
        PRIVATE ${MH_PLUGIN_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # End-to-end: drives the processor over a fake transport, then loads the built VST3
    # through JUCE's own host and checks its bus layout, parameters and state.
    juce_add_console_app(MidiHumanizerHostTest PRODUCT_NAME "MidiHumanizerHostTest")
    target_sources(MidiHumanizerHostTest PRIVATE Tests/HostTest.cpp ${MH_SOURCES})
    target_include_directories(MidiHumanizerHostTest PRIVATE Source)
    target_compile_definitions(MidiHumanizerHostTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MidiHumanizerHostTest
        PRIVATE ${MH_PLUGIN_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
