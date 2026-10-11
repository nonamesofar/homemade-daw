# The SAMPLER ValueTree subtree: wrappers, ids, migrations. No GUI, no Tracktion, no audio thread.
add_library(sampler_model STATIC SamplerState.cpp SamplerState.h Migrations.cpp Migrations.h)
target_include_directories(sampler_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_model PUBLIC sampler_juce)
sampler_target_debuggable(sampler_model)
sampler_target_warnings(sampler_model)
