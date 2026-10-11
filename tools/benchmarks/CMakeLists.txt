# Warp benchmark (M0 7.1, 7.2): Rubber Band (single-file build, built-in FFT), Signalsmith Stretch, beats prototype.
juce_add_console_app(StretchBench PRODUCT_NAME "Stretch Bench")
target_sources(StretchBench PRIVATE
    StretchBench.cpp
    ${CMAKE_SOURCE_DIR}/external/rubberband/single/RubberBandSingle.cpp)
target_include_directories(StretchBench SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external/rubberband
    ${CMAKE_SOURCE_DIR}/external/signalsmith-stretch/include
    ${CMAKE_SOURCE_DIR}/external/signalsmith-linear/include)
target_compile_definitions(StretchBench PRIVATE NOMINMAX)
target_link_libraries(StretchBench PRIVATE sampler_juce)
# A benchmark is meaningless unoptimised, so even the Debug preset builds it with /O2.
sampler_target_optimised(StretchBench)
