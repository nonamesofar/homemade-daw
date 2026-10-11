# Everything that touches Tracktion lives here (linked PRIVATE: no Tracktion types in public headers).
add_library(sampler_engine STATIC
    EngineHost.cpp EngineHost.h
    EngineSetup.cpp EngineSetup.h
    ProjectEdit.cpp ProjectEdit.h
    Arrangement.cpp Arrangement.h
    Transport.cpp Transport.h
    LevelMeters.cpp LevelMeters.h
    OfflineRender.cpp OfflineRender.h
    Audition.cpp Audition.h AuditionRequestQueue.h)
target_include_directories(sampler_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_engine
    PUBLIC sampler_juce sampler_model
    PRIVATE sampler_tracktion)
sampler_target_optimised(sampler_engine)
sampler_target_warnings(sampler_engine)

add_subdirectory(testkit)
