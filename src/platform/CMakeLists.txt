if(WIN32)
    add_library(sampler_platform STATIC
        ICaptureSource.h
        windows/MediaFoundationProbe.cpp windows/MediaFoundationProbe.h
        windows/WasapiLoopbackCapture.cpp)
    target_include_directories(sampler_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(sampler_platform
        PUBLIC juce::juce_core juce::juce_audio_formats
        PRIVATE mfplat mfreadwrite mfuuid propsys ole32 mmdevapi avrt)
    target_compile_definitions(sampler_platform PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    sampler_target_debuggable(sampler_platform)
endif()
