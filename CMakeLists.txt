cmake_minimum_required(VERSION 3.22)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(PARALLAX VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLX_BUILD_TESTS "Build engine tests and test plug-ins" ON)
set(PLX_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (otherwise fetched)")

if(PLX_JUCE_DIR AND EXISTS "${PLX_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${PLX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PLX_SOURCES
    Source/Host/HostedInstance.cpp
    Source/Host/PluginCatalog.cpp
    Source/Host/PluginWindows.cpp
    Source/Engine/ParallaxEngine.cpp
    Source/PluginProcessor.cpp
    Source/UI/PluginEditor.cpp)

set(PLX_DEFS
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
if(APPLE)
    list(APPEND PLX_DEFS JUCE_PLUGINHOST_AU=1)
endif()

set(PLX_LIBS
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra)

juce_add_plugin(Parallax
    COMPANY_NAME "Parallax Audio"
    BUNDLE_ID "com.parallaxaudio.parallax"
    PLUGIN_MANUFACTURER_CODE PlxA
    PLUGIN_CODE Plx1
    FORMATS VST3
    PRODUCT_NAME "Parallax"
    DESCRIPTION "Parallax - loudness-locked parallel processing"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Dynamics)

target_sources(Parallax PRIVATE ${PLX_SOURCES})
target_include_directories(Parallax PRIVATE Source)
target_compile_definitions(Parallax PUBLIC ${PLX_DEFS})
target_link_libraries(Parallax PRIVATE ${PLX_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(PLX_BUILD_TESTS)
    juce_add_console_app(ParallaxTests PRODUCT_NAME "ParallaxTests")
    target_sources(ParallaxTests PRIVATE ${PLX_SOURCES} Tests/EngineTests.cpp)
    target_include_directories(ParallaxTests PRIVATE Source)
    target_compile_definitions(ParallaxTests PRIVATE ${PLX_DEFS} PLX_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Parallax" JucePlugin_Manufacturer="Parallax Audio" JucePlugin_VersionString="0.1.0")
    target_link_libraries(ParallaxTests PRIVATE ${PLX_LIBS} juce::juce_recommended_config_flags)

    set(PLXTestDrive_CODE Tdrv)
    set(PLXTestLatency_CODE Tlat)
    set(PLXTestComp_CODE Tcmp)
    foreach(TP PLXTestDrive PLXTestLatency PLXTestComp)
        juce_add_plugin(${TP}
            COMPANY_NAME "PLXTest"
            BUNDLE_ID com.parallaxaudio.test.${TP}
            PLUGIN_MANUFACTURER_CODE PlxT
            PLUGIN_CODE ${${TP}_CODE}
            FORMATS VST3
            PRODUCT_NAME "${TP}"
            IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE)
        target_sources(${TP} PRIVATE Tests/TestPlugins/${TP}.cpp)
        target_compile_definitions(${TP} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        target_link_libraries(${TP} PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
    endforeach()
endif()
