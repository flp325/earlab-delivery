cmake_minimum_required(VERSION 3.22)
project(EarLab VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码是 UTF-8（含中文界面文案），MSVC 默认按本地代码页解析会报 C2001
if(MSVC)
    add_compile_options(/utf-8)
endif()

# JUCE 由 scripts/fetch_juce.py 下载到 third_party/JUCE
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/JUCE")
if(NOT EXISTS "${JUCE_DIR}/modules/juce_core")
    message(FATAL_ERROR "JUCE 不存在，请先运行: python ../scripts/fetch_juce.py")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- 应用
juce_add_gui_app(EarLabEngine
    PRODUCT_NAME "EarLab Engine"
    COMPANY_NAME "EarLab"
    BUNDLE_ID "org.earlab.engine"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/EarLab.png"
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

juce_add_binary_data(EarLabArtwork HEADER_NAME EarLabArtwork.h NAMESPACE EarLabArtwork
    SOURCES assets/EarLab.png)
target_link_libraries(EarLabEngine PRIVATE EarLabArtwork)

target_sources(EarLabEngine PRIVATE
    src/Main.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentLayout.cpp
    src/ui/MainComponentPlugin.cpp
    src/ui/HistoryChart.cpp
    src/ui/PluginBrowser.cpp
    src/ui/EqCurvePanel.cpp
    src/ui/EarLabLookAndFeel.cpp
    src/ui/AbilityBars.cpp
    src/ui/AssessmentCard.cpp
    src/ui/AssessmentHistory.cpp
    src/engine/MultiBandEq.cpp
    src/engine/AudioEngine.cpp    src/engine/CompressorProcessor.cpp
    src/engine/CompressorCalibration.cpp
    src/engine/DualChain.cpp
    src/engine/MaterialPlayer.cpp
    src/engine/LoudnessMatcher.cpp
    src/engine/AbxSession.cpp
    src/engine/TrainingTask.cpp
    src/engine/ProfileStore.cpp
    src/engine/Assessment.cpp
    src/dsp/LoudnessMeter.cpp
    src/plugin/PluginCatalog.cpp
    src/plugin/PluginHost.cpp
    src/plugin/PluginProbe.cpp
    src/plugin/PluginScanRunner.cpp
    src/plugin/PluginTask.cpp
    src/plugin/PluginTrainableProcessor.cpp
    src/plugin/PluginMemory.cpp)

target_include_directories(EarLabEngine PRIVATE src)

target_compile_definitions(EarLabEngine PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EarLabEngine,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EarLabEngine,JUCE_VERSION>")

target_link_libraries(EarLabEngine
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(WIN32)
    target_link_libraries(EarLabEngine PRIVATE dwmapi)
endif()

# ---------------------------------------------------------------- 测试：响度计
juce_add_console_app(EarLabDspTests PRODUCT_NAME "EarLabDspTests")

target_sources(EarLabDspTests PRIVATE
    tests/LoudnessTest.cpp
    src/dsp/LoudnessMeter.cpp
    src/engine/MultiBandEq.cpp)

target_include_directories(EarLabDspTests PRIVATE src)

target_compile_definitions(EarLabDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(EarLabDspTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- 测试：引擎
juce_add_console_app(EarLabEngineTests PRODUCT_NAME "EarLabEngineTests")

target_sources(EarLabEngineTests PRIVATE
    tests/EngineTest.cpp
    src/dsp/LoudnessMeter.cpp
    src/engine/MultiBandEq.cpp
    src/engine/DualChain.cpp
    src/engine/LoudnessMatcher.cpp
    src/engine/AbxSession.cpp
    src/engine/MaterialPlayer.cpp
    src/engine/CompressorProcessor.cpp
    src/engine/CompressorCalibration.cpp
    src/engine/TrainingTask.cpp
    src/engine/ProfileStore.cpp
    src/engine/Assessment.cpp)

target_include_directories(EarLabEngineTests PRIVATE src)

target_compile_definitions(EarLabEngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(EarLabEngineTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- 测试：第三方插件
juce_add_console_app(EarLabPluginTests PRODUCT_NAME "EarLabPluginTests")

target_sources(EarLabPluginTests PRIVATE
    tests/PluginTest.cpp
    src/plugin/PluginCatalog.cpp
    src/plugin/PluginHost.cpp
    src/plugin/PluginProbe.cpp
    src/plugin/PluginScanRunner.cpp
    src/plugin/PluginTask.cpp
    src/plugin/PluginTrainableProcessor.cpp
    src/plugin/PluginMemory.cpp)

target_include_directories(EarLabPluginTests PRIVATE src)

target_compile_definitions(EarLabPluginTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1)

target_link_libraries(EarLabPluginTests
    PRIVATE
        juce::juce_audio_processors_headless
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- CTest
enable_testing()

add_test(NAME dsp    COMMAND $<TARGET_FILE:EarLabDspTests>)
add_test(NAME engine COMMAND $<TARGET_FILE:EarLabEngineTests>)
add_test(NAME plugin COMMAND $<TARGET_FILE:EarLabPluginTests> --unit-only)

# Opt-in microbenchmarks against source extracted from the unchanged release tag.
option(EARLAB_BUILD_BENCHMARKS "Build the release-baseline microbenchmarks" OFF)
set(EARLAB_PERF_BASELINE_DIR "" CACHE PATH "Directory containing renamed baseline sources")
if(EARLAB_BUILD_BENCHMARKS)
    juce_add_console_app(EarLabPerformance PRODUCT_NAME "EarLabPerformance")
    set_target_properties(EarLabPerformance PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_sources(EarLabPerformance PRIVATE
        tests/Performance.cpp
        src/engine/MultiBandEq.cpp src/engine/MaterialPlayer.cpp src/engine/CompressorProcessor.cpp
        "${EARLAB_PERF_BASELINE_DIR}/BaselineMultiBandEq.cpp"
        "${EARLAB_PERF_BASELINE_DIR}/BaselineMaterialPlayer.cpp"
        "${EARLAB_PERF_BASELINE_DIR}/BaselineCompressorProcessor.cpp")
    target_include_directories(EarLabPerformance PRIVATE src src/engine "${EARLAB_PERF_BASELINE_DIR}")
    target_compile_definitions(EarLabPerformance PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EarLabPerformance PRIVATE juce::juce_audio_formats juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
