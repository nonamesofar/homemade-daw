# Everything that touches Tracktion lives here (linked PRIVATE: no Tracktion types in public headers).
add_library(sampler_engine STATIC
    EngineHost.cpp EngineHost.h
    EngineSetup.cpp EngineSetup.h
    EditLab.cpp EditLab.h
    PluginLab.cpp PluginLab.h)
target_include_directories(sampler_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(sampler_engine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)
if(SAMPLER_RUBBERBAND)
    # Rubber Band is compiled into the Tracktion module from external/rubberband (single-file build), for the
    # rubberbandMelodic / rubberbandPercussive stretch modes.
    target_compile_definitions(sampler_engine PUBLIC
        TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1
        TRACKTION_BUILD_RUBBERBAND=1)
    target_include_directories(sampler_engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external)
endif()
target_link_libraries(sampler_engine
    PUBLIC
        juce::juce_core
        juce::juce_audio_formats
        juce::juce_events
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils)
# Not linking juce_recommended_config_flags: it adds /Od in Debug, which would cancel /O2.
sampler_target_optimised(sampler_engine)
