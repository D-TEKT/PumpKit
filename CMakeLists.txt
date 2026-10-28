cmake_minimum_required(VERSION 3.22)

project(PumpKit VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically the first time you build
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.16-security
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PumpKit
    COMPANY_NAME            "DIY"
    PLUGIN_MANUFACTURER_CODE Diyy
    PLUGIN_CODE             Pmpk
    FORMATS                 VST3
    PRODUCT_NAME            "PumpKit"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    VST3_CATEGORIES         Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PumpKit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PumpKit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PumpKit
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
