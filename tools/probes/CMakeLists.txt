juce_add_console_app(DeviceProbe PRODUCT_NAME "Device Probe")
target_sources(DeviceProbe PRIVATE DeviceProbe.cpp)
target_link_libraries(DeviceProbe PRIVATE sampler_juce)
sampler_target_debuggable(DeviceProbe)

juce_add_console_app(DecodeProbe PRODUCT_NAME "Decode Probe")
target_sources(DecodeProbe PRIVATE DecodeProbe.cpp MediaFoundationProbe.cpp MediaFoundationProbe.h)
target_link_libraries(DecodeProbe PRIVATE sampler_engine mfplat mfreadwrite mfuuid propsys ole32)
sampler_target_debuggable(DecodeProbe)

juce_add_console_app(CaptureProbe PRODUCT_NAME "Capture Probe")
target_sources(CaptureProbe PRIVATE CaptureProbe.cpp)
target_link_libraries(CaptureProbe PRIVATE sampler_engine_testkit sampler_engine sampler_platform)
sampler_target_debuggable(CaptureProbe)

juce_add_console_app(PadProbe PRODUCT_NAME "Pad Probe")
target_sources(PadProbe PRIVATE PadProbe.cpp)
target_link_libraries(PadProbe PRIVATE sampler_engine_testkit sampler_engine sampler_platform sampler_render)
sampler_target_debuggable(PadProbe)

juce_add_console_app(WarpProbe PRODUCT_NAME "Warp Probe")
target_sources(WarpProbe PRIVATE WarpProbe.cpp)
target_link_libraries(WarpProbe PRIVATE sampler_engine_testkit sampler_engine)
sampler_target_debuggable(WarpProbe)
