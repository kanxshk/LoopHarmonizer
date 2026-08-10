cmake_minimum_required(VERSION 3.22)

project(LoopHarmonizer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static-link the MSVC runtime so the built VST3 loads in any DAW without
# the end user needing the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Off by default: the system VST3 folder lives under Program Files, so the
# post-build copy fails unless the build is run elevated. Turn on with
#   cmake -B build -DLOOPHARMONIZER_COPY_PLUGIN=ON
option(LOOPHARMONIZER_COPY_PLUGIN "Copy the built VST3 into the system VST3 folder after building" OFF)
option(LOOPHARMONIZER_BUILD_TOOLS "Build the standalone DSP test harnesses" ON)

#==============================================================================
# Analysis DSP, deliberately free of any JUCE dependency so it can be built and
# tested by a plain console executable before it goes anywhere near the plugin's
# audio callback. The plugin and the test harnesses both link this.
add_library(LoopHarmonizerDSP STATIC
    Source/DSP/YinPitchDetector.cpp
    Source/DSP/YinPitchDetector.h
    Source/DSP/NoteSegmenter.cpp
    Source/DSP/NoteSegmenter.h
    Source/DSP/KeyDetector.cpp
    Source/DSP/KeyDetector.h
    Source/DSP/NoteUtils.cpp
    Source/DSP/NoteUtils.h)

target_include_directories(LoopHarmonizerDSP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(LoopHarmonizerDSP PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(LoopHarmonizerDSP PRIVATE /W4)
else()
    target_compile_options(LoopHarmonizerDSP PRIVATE -Wall -Wextra)
endif()

#==============================================================================
add_subdirectory(JUCE)

# LoopHarmonizer is an audio-in / MIDI-out generator: it analyses an incoming
# monophonic loop and emits MIDI for the producer to route to their own
# instruments. Hence IS_SYNTH FALSE (it has an audio input and no internal
# synth engine) combined with NEEDS_MIDI_OUTPUT TRUE.
juce_add_plugin(LoopHarmonizer
    COMPANY_NAME                "LoopHarmonizer"
    BUNDLE_ID                   "com.loopharmonizer.loopharmonizer"
    PLUGIN_MANUFACTURER_CODE    Lhrm
    PLUGIN_CODE                 Lph1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "LoopHarmonizer"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${LOOPHARMONIZER_COPY_PLUGIN}
    VST3_CATEGORIES             "Fx" "Analyzer")

juce_generate_juce_header(LoopHarmonizer)

target_sources(LoopHarmonizer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)

target_compile_definitions(LoopHarmonizer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LoopHarmonizer
    PRIVATE
        LoopHarmonizerDSP
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Console harness for validating pitch detection against real loops, built
# against LoopHarmonizerDSP only - no JUCE, so it compiles in seconds.
if(LOOPHARMONIZER_BUILD_TOOLS)
    add_executable(PitchTest
        Tools/PitchTest/main.cpp
        Tools/PitchTest/WavFile.cpp
        Tools/PitchTest/WavFile.h)

    target_include_directories(PitchTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Tools/PitchTest")
    target_link_libraries(PitchTest PRIVATE LoopHarmonizerDSP)

    if(MSVC)
        target_compile_options(PitchTest PRIVATE /W4)
    else()
        target_compile_options(PitchTest PRIVATE -Wall -Wextra)
    endif()
endif()
