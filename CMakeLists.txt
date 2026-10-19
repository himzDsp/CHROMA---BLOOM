cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime: the plugin then runs without the Visual C++ Redistributable installed.
# (Has to be set before project().)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(ChromaBloom VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHROMA_COPY_AFTER_BUILD "Copy plugin to the system plugin folder after build" ON)
option(CHROMA_BUILD_TESTS      "Also build the JUCE-free DSP unit tests (ctest)"    OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ChromaBloom
    COMPANY_NAME                "Him'z Audio"
    COMPANY_COPYRIGHT           "Him'z Audio ©2024"
    BUNDLE_ID                   com.himzaudio.chromabloom
    PLUGIN_MANUFACTURER_CODE    Hmzd
    PLUGIN_CODE                 Cinf
    PRODUCT_NAME                "Chroma Bloom"
    FORMATS                     VST3 AU Standalone
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     ${CHROMA_COPY_AFTER_BUILD})

target_sources(ChromaBloom PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ChromaBloom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ChromaBloom
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(CHROMA_BUILD_TESTS)
    enable_testing()
    add_executable(ChromaBloomDspTests Tests/dsp_test.cpp)
    target_include_directories(ChromaBloomDspTests PRIVATE Source)
    target_compile_features(ChromaBloomDspTests PRIVATE cxx_std_17)
    add_test(NAME dsp COMMAND ChromaBloomDspTests)
endif()
