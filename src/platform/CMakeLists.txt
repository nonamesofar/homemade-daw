if(WIN32)
    add_library(sampler_platform STATIC
        ICaptureSource.h NativeWindowTools.h
        windows/NativeWindowTools.cpp
        windows/MediaFoundationProbe.cpp windows/MediaFoundationProbe.h
        windows/WasapiLoopbackCapture.cpp)
    target_include_directories(sampler_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(sampler_platform
        PUBLIC sampler_juce
        PRIVATE mfplat mfreadwrite mfuuid propsys ole32 mmdevapi avrt)
    sampler_target_debuggable(sampler_platform)
    sampler_target_warnings(sampler_platform)
endif()
