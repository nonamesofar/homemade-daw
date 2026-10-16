add_library(sampler_io STATIC
    AtomicFile.cpp AtomicFile.h
    ImportNaming.cpp ImportNaming.h
    ImportService.cpp ImportService.h
    Mp3Gapless.cpp Mp3Gapless.h
    JobRegistry.cpp JobRegistry.h
    AnalysisService.cpp AnalysisService.h)
target_include_directories(sampler_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_io PUBLIC sampler_juce sampler_model sampler_commands)
sampler_target_debuggable(sampler_io)
sampler_target_warnings(sampler_io)
