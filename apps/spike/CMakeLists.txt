juce_add_gui_app(SamplerSpike PRODUCT_NAME "Sampler Spike")
target_sources(SamplerSpike PRIVATE Main.cpp)
juce_generate_juce_header(SamplerSpike)
target_compile_definitions(SamplerSpike PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SamplerSpike,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(SamplerSpike
    PRIVATE
        sampler_engine
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
sampler_target_debuggable(SamplerSpike)
sampler_target_warnings(SamplerSpike)
