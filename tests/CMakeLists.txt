include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)

add_executable(sampler_tests smoke.test.cpp)
target_link_libraries(sampler_tests PRIVATE Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_tests)
sampler_target_warnings(sampler_tests)
catch_discover_tests(sampler_tests)

# Pure UI logic: no GUI classes, so it runs without a window.
add_executable(sampler_ui_tests ui/timeline.test.cpp)
target_include_directories(sampler_ui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sampler_ui_tests PRIVATE Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_ui_tests)
sampler_target_warnings(sampler_ui_tests)
catch_discover_tests(sampler_ui_tests)

# The real timeline component driven by synthetic mouse events (headless, no window).
add_executable(sampler_ui_component_tests ui/timeline_component.test.cpp)
target_link_libraries(sampler_ui_component_tests PRIVATE sampler_ui Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_ui_component_tests)
sampler_target_warnings(sampler_ui_component_tests)
catch_discover_tests(sampler_ui_component_tests)

# Platform code (capture lifecycle); needs an output device, skips without one.
add_executable(sampler_platform_tests platform/capture.test.cpp)
target_link_libraries(sampler_platform_tests PRIVATE sampler_platform Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_platform_tests)
sampler_target_warnings(sampler_platform_tests)
catch_discover_tests(sampler_platform_tests ADD_TAGS_AS_LABELS)

# Tests that run the real engine headless.
add_executable(AtomicWriterProbe tools/AtomicWriterProbe.cpp)
target_link_libraries(AtomicWriterProbe PRIVATE sampler_io)
sampler_target_debuggable(AtomicWriterProbe)

add_executable(sampler_engine_tests engine/playback.test.cpp engine/assumptions.test.cpp engine/integrity.test.cpp
    engine/plugin_hosting.test.cpp)
add_dependencies(sampler_engine_tests AtomicWriterProbe SamplerTestPlugin_VST3)
target_compile_definitions(sampler_engine_tests PRIVATE
    ATOMIC_WRITER_PROBE_PATH="$<TARGET_FILE:AtomicWriterProbe>"
    TEST_PLUGIN_VST3_PATH="$<TARGET_FILE_DIR:SamplerTestPlugin_VST3>/../..")
target_link_libraries(sampler_engine_tests PRIVATE
    sampler_engine
    sampler_io
    sampler_platform
    sampler_juce
    Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_engine_tests)
sampler_target_warnings(sampler_engine_tests)
# Catch2 tags become ctest labels ([engine], [io], [plugin], ...). Every test runs by default; two labels let a run
# leave out the noisy ones with `ctest -LE "realtime|gui"`:
#   realtime: plays sound on the output device and records it through loopback.
#   hardware: needs a real audio device and its timing (so it can flake by a millisecond or two); CI leaves it out
#             with `-LE hardware`. Run it locally.
#   gui:      opens a window and posts mouse clicks to it.
catch_discover_tests(sampler_engine_tests ADD_TAGS_AS_LABELS)
