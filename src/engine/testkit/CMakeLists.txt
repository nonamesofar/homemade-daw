# Test-only helpers that run the real engine (edit and plugin labs). Linked by test executables, never by the app.
add_library(sampler_engine_testkit STATIC
    EditLab.cpp EditLab.h
    FilePlayer.cpp FilePlayer.h
    PluginLab.cpp PluginLab.h
    WarpLab.cpp WarpLab.h)
target_link_libraries(sampler_engine_testkit
    PUBLIC sampler_engine
    PRIVATE sampler_tracktion)
sampler_target_optimised(sampler_engine_testkit)
sampler_target_warnings(sampler_engine_testkit)
