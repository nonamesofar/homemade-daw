# The only code that edits the project model. Needs the model only (the Tracktion-touching parts of later commands go
# through helpers in src/engine), so commands are unit-testable without an engine.
add_library(sampler_commands STATIC
    CommandService.cpp CommandService.h
    Commands.cpp Commands.h)
target_include_directories(sampler_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_commands PUBLIC sampler_model sampler_juce)
sampler_target_debuggable(sampler_commands)
sampler_target_warnings(sampler_commands)
