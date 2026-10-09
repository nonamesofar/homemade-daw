# One build of the third-party modules for the whole project.
#
# JUCE modules are CMake INTERFACE libraries: linking juce::juce_core compiles juce_core.cpp into the linking target.
# Linked from several targets, that compiles each module many times, with whatever flags and JUCE_* settings each
# target happens to have (an ODR hazard). Instead:
#   sampler_juce       STATIC: every JUCE module we use, compiled once, one set of JUCE_* settings, /O2 even in Debug,
#                      no /WX. Consumers get its include folders as SYSTEM (MSVC /external:I) and its definitions.
#   sampler_tracktion  STATIC: the Tracktion modules, compiled once. Only sampler_engine links it (PRIVATE), so the
#                      "Tracktion only in src/engine" rule is enforced by CMake.
# Every target of ours links these instead of juce::juce_*. The test plugin (juce_add_plugin) is its own DLL and keeps
# its own JUCE copy, which is correct.

#-------------------------------------------------------------------------------------------------------------------
add_library(sampler_juce STATIC)
target_link_libraries(sampler_juce PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_osc
    juce::juce_cryptography)

# The single set of JUCE settings for the project. Built without _CONSOLE (the GUI flavour of
# JUCEApplicationBase::getCommandLineParameters, which works for console apps too); the console apps use a plain
# main() with ScopedJuceInitialiser_GUI, so nothing in them depends on the module being built with _CONSOLE.
target_compile_definitions(sampler_juce PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Re-export what the modules need (include folders, JUCE_MODULE_AVAILABLE_*, DEBUG/NDEBUG, the settings above).
# The PRIVATE links reach consumers only as $<LINK_ONLY:...> (system libraries), never as sources.
target_include_directories(sampler_juce SYSTEM INTERFACE $<TARGET_PROPERTY:sampler_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(sampler_juce INTERFACE $<TARGET_PROPERTY:sampler_juce,COMPILE_DEFINITIONS>)
sampler_target_optimised(sampler_juce)
set_target_properties(sampler_juce PROPERTIES FOLDER external)

#-------------------------------------------------------------------------------------------------------------------
# Tracktion modules link the JUCE modules they depend on as INTERFACE libraries, so linking tracktion::* would compile
# JUCE again. Take only each Tracktion module's own sources, include folders and definitions; JUCE comes from
# sampler_juce.
add_library(sampler_tracktion STATIC)
foreach(module IN ITEMS tracktion_core tracktion_graph tracktion_engine)
    foreach(property IN ITEMS INTERFACE_SOURCES INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_DEFINITIONS
                              INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${module} ${property})
        if(NOT value)
            continue()
        endif()
        if(property STREQUAL "INTERFACE_SOURCES")
            target_sources(sampler_tracktion PRIVATE ${value})
        elseif(property STREQUAL "INTERFACE_INCLUDE_DIRECTORIES")
            target_include_directories(sampler_tracktion SYSTEM PUBLIC ${value})
        elseif(property STREQUAL "INTERFACE_COMPILE_DEFINITIONS")
            target_compile_definitions(sampler_tracktion PUBLIC ${value})
        elseif(property STREQUAL "INTERFACE_COMPILE_OPTIONS")
            target_compile_options(sampler_tracktion PRIVATE ${value})
        else()
            # Module dependencies (juce_*, tracktion_*) come from sampler_juce / this target; keep anything else.
            list(FILTER value EXCLUDE REGEX "^(juce::)?juce_|^(tracktion::)?tracktion_")
            if(value)
                target_link_libraries(sampler_tracktion PUBLIC ${value})
            endif()
        endif()
    endforeach()
endforeach()
target_link_libraries(sampler_tracktion PUBLIC sampler_juce)
if(SAMPLER_RUBBERBAND)
    # Rubber Band is compiled into the Tracktion module from external/rubberband (single-file build), for the
    # rubberbandMelodic / rubberbandPercussive stretch modes.
    target_compile_definitions(sampler_tracktion PUBLIC
        TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1
        TRACKTION_BUILD_RUBBERBAND=1)
    target_include_directories(sampler_tracktion SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external)
endif()
if(MSVC)
    target_compile_options(sampler_tracktion PRIVATE /bigobj)
endif()
sampler_target_optimised(sampler_tracktion)
set_target_properties(sampler_tracktion PROPERTIES FOLDER external)
