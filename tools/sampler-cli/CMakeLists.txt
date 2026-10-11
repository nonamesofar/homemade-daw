# Headless tools: no window, no audio device.
add_executable(sampler-cli Main.cpp)
target_link_libraries(sampler-cli PRIVATE sampler_engine sampler_juce)
sampler_target_debuggable(sampler-cli)
sampler_target_warnings(sampler-cli)
