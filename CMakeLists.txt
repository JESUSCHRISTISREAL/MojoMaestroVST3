cmake_minimum_required(VERSION 3.25)
project(MojoMaestro VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static MSVC runtime so the .vst3 has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MOJO_COPY_AFTER_BUILD "Copy the VST3 to the system VST3 folder after build (needs admin on Windows)" OFF)

# JUCE source: use an existing checkout if MOJO_JUCE_DIR (or env JUCE_DIR) is set,
# otherwise download JUCE 9.0.5.
set(MOJO_JUCE_DIR "$ENV{JUCE_DIR}" CACHE PATH "Path to an existing JUCE checkout (optional)")
if(MOJO_JUCE_DIR)
    message(STATUS "Using local JUCE at ${MOJO_JUCE_DIR}")
    add_subdirectory("${MOJO_JUCE_DIR}" juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MojoMaestro
    COMPANY_NAME                "SFOG7"
    PRODUCT_NAME                "Mojo Maestro"
    PLUGIN_MANUFACTURER_CODE    Mmdp
    PLUGIN_CODE                 Mjm1
    FORMATS                     VST3
    VST3_CATEGORIES             Fx Distortion
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE
    COPY_PLUGIN_AFTER_BUILD     ${MOJO_COPY_AFTER_BUILD})

target_sources(MojoMaestro PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/VfdGeometry.h
    Source/MojoMaestro.hpp)

juce_add_binary_data(MojoAssets SOURCES
    Assets/plate_3x.png
    Assets/knob_big_3x.png
    Assets/knob_small_3x.png
    Assets/cap_3x.png Assets/ptr_big_3x.png Assets/ptr_small_3x.png)

target_include_directories(MojoMaestro PRIVATE Source)

target_compile_definitions(MojoMaestro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MojoMaestro
    PRIVATE
        MojoAssets
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional offline tests (not part of the plugin): cmake -DMOJO_BUILD_TESTS=ON
option(MOJO_BUILD_TESTS "Build the offline click test and the editor snapshot tool" OFF)
if(MOJO_BUILD_TESTS)
    foreach(t click_test snapshot_test interaction_test)
        juce_add_console_app(Mojo_${t} PRODUCT_NAME "Mojo_${t}")
        target_sources(Mojo_${t} PRIVATE Tests/${t}.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_include_directories(Mojo_${t} PRIVATE Source)
        target_compile_definitions(Mojo_${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(Mojo_${t} PRIVATE MojoAssets juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
endif()

# Optional regression check: run the same click test against an older Source folder (cmake -DMOJO_OLD_SRC=path).
set(MOJO_OLD_SRC "" CACHE PATH "Older Source folder to run the click test against")
if(MOJO_BUILD_TESTS AND MOJO_OLD_SRC)
    juce_add_console_app(Mojo_click_test_old PRODUCT_NAME "Mojo_click_test_old")
    target_sources(Mojo_click_test_old PRIVATE Tests/click_test.cpp ${MOJO_OLD_SRC}/PluginProcessor.cpp ${MOJO_OLD_SRC}/PluginEditor.cpp)
    target_include_directories(Mojo_click_test_old PRIVATE ${MOJO_OLD_SRC})
    target_compile_definitions(Mojo_click_test_old PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(Mojo_click_test_old PRIVATE MojoAssets juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
endif()
