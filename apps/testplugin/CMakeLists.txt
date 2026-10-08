# JUCE test plugin for the M0 hosting checks: a VST3 instrument with 4 stereo outputs (stand-in for Maschine 3).
juce_add_plugin(SamplerTestPlugin
    PRODUCT_NAME "Sampler Test Plugin"
    COMPANY_NAME "Sampler"
    PLUGIN_MANUFACTURER_CODE Smpl
    PLUGIN_CODE Tp01
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(SamplerTestPlugin PRIVATE TestPlugin.cpp TestPlugin.h)
target_compile_definitions(SamplerTestPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SamplerTestPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
sampler_target_debuggable(SamplerTestPlugin)
