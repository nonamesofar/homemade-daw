include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)

add_subdirectory(testplugin)

add_executable(sampler_tests smoke.test.cpp)
target_link_libraries(sampler_tests PRIVATE Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_tests)
sampler_target_warnings(sampler_tests)
catch_discover_tests(sampler_tests)

# Pure UI logic: geometry, position text, snap, selection, meters, key routing (no window).
add_executable(sampler_ui_tests ui/arrange_geometry.test.cpp ui/arrange_logic.test.cpp)
target_include_directories(sampler_ui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sampler_ui_tests PRIVATE sampler_ui Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_ui_tests)
sampler_target_warnings(sampler_ui_tests)
catch_discover_tests(sampler_ui_tests)

# UI components driven by synthetic events (headless, no window).
add_executable(sampler_ui_component_tests ui/browser_panel.test.cpp ui/browser_model.test.cpp ui/widgets.test.cpp)
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
    engine/plugin_hosting.test.cpp engine/project_edit.test.cpp engine/project_open.test.cpp engine/thumbnails.test.cpp engine/audition.test.cpp)
add_dependencies(sampler_engine_tests AtomicWriterProbe SamplerTestPlugin_VST3)
target_compile_definitions(sampler_engine_tests PRIVATE
    ATOMIC_WRITER_PROBE_PATH="$<TARGET_FILE:AtomicWriterProbe>"
    TEST_PLUGIN_VST3_PATH="$<TARGET_FILE_DIR:SamplerTestPlugin_VST3>/../..")
target_link_libraries(sampler_engine_tests PRIVATE
    sampler_engine_testkit
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

# Import, decoding and encoding code that runs without the audio engine.
add_executable(sampler_io_tests io/lame.test.cpp io/fixtures.test.cpp io/import.test.cpp io/mp3.test.cpp io/import_formats.test.cpp)
target_compile_definitions(sampler_io_tests PRIVATE SAMPLER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio-fixtures")
target_link_libraries(sampler_io_tests PRIVATE sampler_io sampler_fixtures sampler_lame sampler_juce Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_io_tests)
sampler_target_warnings(sampler_io_tests)
catch_discover_tests(sampler_io_tests ADD_TAGS_AS_LABELS)

# The SAMPLER subtree wrappers (no engine, no GUI).
add_executable(sampler_model_tests model/sampler_state.test.cpp model/migrations.test.cpp)
target_link_libraries(sampler_model_tests PRIVATE sampler_model Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_model_tests)
sampler_target_warnings(sampler_model_tests)
catch_discover_tests(sampler_model_tests ADD_TAGS_AS_LABELS)

# Commands and undo on a plain model, and the plain edit planner (no engine).
add_executable(sampler_commands_tests commands/commands.test.cpp commands/edit_planner.test.cpp)
target_link_libraries(sampler_commands_tests PRIVATE sampler_commands Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_commands_tests)
sampler_target_warnings(sampler_commands_tests)
catch_discover_tests(sampler_commands_tests ADD_TAGS_AS_LABELS)

# The main window content and its controller (headless: no top-level window is created).
add_executable(sampler_app_tests app/arrangement_app.test.cpp app/main_component.test.cpp app/project_flow.test.cpp app/progress.test.cpp)
target_compile_definitions(sampler_app_tests PRIVATE SAMPLER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio-fixtures")
target_link_libraries(sampler_app_tests PRIVATE sampler_app_core Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_app_tests)
sampler_target_warnings(sampler_app_tests)
catch_discover_tests(sampler_app_tests ADD_TAGS_AS_LABELS)

# The arrangement (facade, commands, transport, renders) on a real headless engine.
add_executable(sampler_arrange_tests arrange/arrangement.test.cpp arrange/arrange_commands.test.cpp arrange/arrangement_view.test.cpp
    arrange/arrange_more.test.cpp arrange/arrange_property.test.cpp arrange/arrange_perf.test.cpp arrange/arrange_hardware.test.cpp)
target_include_directories(sampler_arrange_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sampler_arrange_tests PRIVATE SAMPLER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio-fixtures")
target_link_libraries(sampler_arrange_tests PRIVATE sampler_ui sampler_commands sampler_engine sampler_juce Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_arrange_tests)
sampler_target_warnings(sampler_arrange_tests)
catch_discover_tests(sampler_arrange_tests ADD_TAGS_AS_LABELS)

# Golden renders: fixed plugin-free projects rendered offline and compared with stored WAVs (also runs sampler-cli).
add_executable(sampler_golden_tests golden/GoldenProjects.cpp)
add_dependencies(sampler_golden_tests sampler-cli)
target_include_directories(sampler_golden_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sampler_golden_tests PRIVATE
    SAMPLER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio-fixtures"
    SAMPLER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    SAMPLER_CLI_PATH="$<TARGET_FILE:sampler-cli>")
target_link_libraries(sampler_golden_tests PRIVATE sampler_commands sampler_engine sampler_juce Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_golden_tests)
sampler_target_warnings(sampler_golden_tests)
catch_discover_tests(sampler_golden_tests ADD_TAGS_AS_LABELS)
