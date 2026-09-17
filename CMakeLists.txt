cmake_minimum_required(VERSION 3.22)

project(AdbSynth VERSION 0.1.0)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.venv/lib/python3.12/site-packages/torch/share/cmake/Torch")
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

# Shared, JUCE-free DSP + parameter schema, linked by both the plugin and the offline renderer.
add_library(adbsynth_core STATIC
    Source/SynthEngine.cpp
    Source/SynthEngine.h
    Source/ParameterSchema.h
)

target_include_directories(adbsynth_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(adbsynth_core PUBLIC ${TORCH_LIBRARIES})
target_compile_features(adbsynth_core PUBLIC cxx_std_20)
set_target_properties(adbsynth_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(AdbSynthRender Tools/RenderCLI.cpp)
target_link_libraries(AdbSynthRender PRIVATE adbsynth_core)

juce_add_plugin(AdbSynth
    COMPANY_NAME "AdbSynth"
    BUNDLE_ID com.adbsynth.oscillator
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Adbs
    PLUGIN_CODE Adbs
    FORMATS Standalone VST3
    PRODUCT_NAME "AdbSynth"
)

target_sources(AdbSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)
target_compile_features(AdbSynth PRIVATE cxx_std_20)

juce_generate_juce_header(AdbSynth)

target_compile_definitions(AdbSynth
    PUBLIC
        ADBSYNTH_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ADBSYNTH_TORCH_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/.tmp/ml/model_scripted.pt"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AdbSynth
    PRIVATE
        adbsynth_core
        ${TORCH_LIBRARIES}
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)