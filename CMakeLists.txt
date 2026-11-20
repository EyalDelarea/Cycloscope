cmake_minimum_required(VERSION 3.22)
project(Cycloscope VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4          # bump to latest 8.x if needed
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Cycloscope
    PRODUCT_NAME "Cycloscope"
    COMPANY_NAME "EyalDelarea"
    PLUGIN_MANUFACTURER_CODE Eyal
    PLUGIN_CODE Cycl
    FORMATS VST3 AU Standalone     # if AU fails on CLT-only, remove AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Cycloscope PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ScopeComponent.cpp
    Source/ScopeLookAndFeel.cpp
    Source/ModeToggle.cpp
    Source/GoniometerComponent.cpp
)

# Embed the Inter typeface (SIL OFL) so the UI never falls back to the default
# system font. Generates BinaryData::InterRegular_ttf etc.
juce_add_binary_data(Cycloscope_fonts SOURCES
    Assets/Inter-Regular.ttf
    Assets/Inter-Medium.ttf
    Assets/Inter-SemiBold.ttf
    Assets/Inter-Bold.ttf
)

target_compile_definitions(Cycloscope PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Cycloscope
    PRIVATE
    Cycloscope_fonts
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_opengl
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---- Tests ----
enable_testing()
add_executable(test_ringbuffer Tests/test_ringbuffer.cpp)
target_include_directories(test_ringbuffer PRIVATE Source)
add_test(NAME ringbuffer COMMAND test_ringbuffer)

add_executable(test_trigger Tests/test_trigger.cpp)
target_include_directories(test_trigger PRIVATE Source)
add_test(NAME trigger COMMAND test_trigger)

add_executable(test_perioddetector Tests/test_perioddetector.cpp)
target_include_directories(test_perioddetector PRIVATE Source)
add_test(NAME perioddetector COMMAND test_perioddetector)

add_executable(test_cycleaverager Tests/test_cycleaverager.cpp)
target_include_directories(test_cycleaverager PRIVATE Source)
add_test(NAME cycleaverager COMMAND test_cycleaverager)

add_executable(test_signalutils Tests/test_signalutils.cpp)
target_include_directories(test_signalutils PRIVATE Source)
add_test(NAME signalutils COMMAND test_signalutils)

add_executable(test_phasealign Tests/test_phasealign.cpp)
target_include_directories(test_phasealign PRIVATE Source)
add_test(NAME phasealign COMMAND test_phasealign)

add_executable(test_notename Tests/test_notename.cpp)
target_include_directories(test_notename PRIVATE Source)
add_test(NAME notename COMMAND test_notename)

add_executable(test_stereoutils Tests/test_stereoutils.cpp)
target_include_directories(test_stereoutils PRIVATE Source)
add_test(NAME stereoutils COMMAND test_stereoutils)

# ---- Plugin-level tests + dev tooling (state, audio-callback contract, editor lifecycle,
# render-to-PNG). Compiled from the plugin's own sources so it runs headless on CI.
option(CYCLOSCOPE_PLUGIN_TESTS "Build the JUCE-level plugin tests / render harness" ON)
if(CYCLOSCOPE_PLUGIN_TESTS)
    juce_add_console_app(cycloscope_plugin_tests PRODUCT_NAME "cycloscope_plugin_tests")
    target_sources(cycloscope_plugin_tests PRIVATE
        Tests/plugin_tests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ScopeComponent.cpp
        Source/ScopeLookAndFeel.cpp
        Source/ModeToggle.cpp
        Source/GoniometerComponent.cpp
    )
    target_include_directories(cycloscope_plugin_tests PRIVATE Source)
    target_compile_definitions(cycloscope_plugin_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1   # lets the tests pump the message loop (timers)
    )
    target_link_libraries(cycloscope_plugin_tests PRIVATE
        Cycloscope_fonts
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    add_test(NAME plugin COMMAND cycloscope_plugin_tests)
endif()
