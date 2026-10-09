juce_add_console_app(DeviceProbe PRODUCT_NAME "Device Probe")
target_sources(DeviceProbe PRIVATE DeviceProbe.cpp)
target_link_libraries(DeviceProbe PRIVATE sampler_juce)
sampler_target_debuggable(DeviceProbe)

juce_add_console_app(DecodeProbe PRODUCT_NAME "Decode Probe")
target_sources(DecodeProbe PRIVATE DecodeProbe.cpp)
target_link_libraries(DecodeProbe PRIVATE sampler_engine sampler_platform)
sampler_target_debuggable(DecodeProbe)

juce_add_console_app(CaptureProbe PRODUCT_NAME "Capture Probe")
target_sources(CaptureProbe PRIVATE CaptureProbe.cpp)
target_link_libraries(CaptureProbe PRIVATE sampler_engine sampler_platform)
sampler_target_debuggable(CaptureProbe)
