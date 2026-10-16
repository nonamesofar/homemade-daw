# Pure DSP on buffers: onset detector, tempo estimator, slicer. No Tracktion, no GUI (juce_dsp's FFT only).
add_library(sampler_analysis STATIC
    OnsetDetector.cpp OnsetDetector.h
    TempoEstimator.cpp TempoEstimator.h
    Slicer.cpp Slicer.h)
target_include_directories(sampler_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_analysis PUBLIC sampler_juce)
sampler_target_optimised(sampler_analysis)
sampler_target_warnings(sampler_analysis)
sampler_assert_no_tracktion(sampler_analysis)
