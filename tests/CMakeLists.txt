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
target_link_libraries(sampler_ui_component_tests PRIVATE sampler_ui Catch2::Catch2WithMain juce::juce_events)
sampler_target_debuggable(sampler_ui_component_tests)
sampler_target_warnings(sampler_ui_component_tests)
catch_discover_tests(sampler_ui_component_tests)

# Tests that run the real engine headless.
add_executable(AtomicWriterProbe tools/AtomicWriterProbe.cpp)
target_link_libraries(AtomicWriterProbe PRIVATE sampler_io)
sampler_target_debuggable(AtomicWriterProbe)

add_executable(sampler_engine_tests engine/playback.test.cpp engine/assumptions.test.cpp engine/integrity.test.cpp)
add_dependencies(sampler_engine_tests AtomicWriterProbe)
target_compile_definitions(sampler_engine_tests PRIVATE ATOMIC_WRITER_PROBE_PATH="$<TARGET_FILE:AtomicWriterProbe>")
target_link_libraries(sampler_engine_tests PRIVATE
    sampler_engine
    sampler_io
    Catch2::Catch2WithMain
    juce::juce_audio_formats
    juce::juce_cryptography
    juce::juce_events)
sampler_target_debuggable(sampler_engine_tests)
sampler_target_warnings(sampler_engine_tests)
catch_discover_tests(sampler_engine_tests PROPERTIES LABELS engine)
