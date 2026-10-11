# The only code that edits the project model. Links the engine's Tracktion-free facade (engine <- commands); Tracktion
# itself stays private to sampler_engine. The model-only commands (sources) still run without an arrangement.
add_library(sampler_commands STATIC
    CommandService.cpp CommandService.h
    Commands.cpp Commands.h
    EditPlanner.cpp EditPlanner.h
    ArrangeCommands.cpp ArrangeCommands.h)
target_include_directories(sampler_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sampler_commands PUBLIC sampler_engine sampler_model sampler_juce)
sampler_target_debuggable(sampler_commands)
sampler_target_warnings(sampler_commands)
