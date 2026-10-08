add_executable(sampler_tests smoke.test.cpp)
target_link_libraries(sampler_tests PRIVATE Catch2::Catch2WithMain)
sampler_target_debuggable(sampler_tests)
sampler_target_warnings(sampler_tests)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(sampler_tests)
