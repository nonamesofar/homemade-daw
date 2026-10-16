# Offline slice rendering (Signalsmith pitch, r8brain resampling) and its scheduler. No Tracktion, no GUI.
# Signalsmith Stretch/Linear and r8brain-free-src are header-only (r8brain uses its built-in Ooura FFT).
add_library(sampler_render STATIC
    SliceBuffer.h SliceBank.h
    SliceRenderer.cpp SliceRenderer.h
    RenderScheduler.cpp RenderScheduler.h)
target_include_directories(sampler_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(sampler_render SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external/signalsmith-stretch/include
    ${CMAKE_SOURCE_DIR}/external/signalsmith-linear/include
    ${CMAKE_SOURCE_DIR}/external/r8brain-free-src)
target_compile_definitions(sampler_render PRIVATE NOMINMAX)
target_link_libraries(sampler_render PUBLIC sampler_analysis sampler_juce)
sampler_target_optimised(sampler_render)
sampler_target_warnings(sampler_render)
sampler_assert_no_tracktion(sampler_render)
