# Audio fixtures for demos and tests: synthesised from fixed formulas (design D-9), also linked by the tests.
add_library(sampler_fixtures STATIC FixtureGenerator.cpp FixtureGenerator.h)
target_include_directories(sampler_fixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sampler_fixtures PUBLIC sampler_juce PRIVATE sampler_lame)
sampler_target_debuggable(sampler_fixtures)
sampler_target_warnings(sampler_fixtures)

juce_add_console_app(MakeFixtures PRODUCT_NAME "make-fixtures")
target_sources(MakeFixtures PRIVATE Main.cpp)
target_link_libraries(MakeFixtures PRIVATE sampler_fixtures sampler_juce)
sampler_target_debuggable(MakeFixtures)
sampler_target_warnings(MakeFixtures)
