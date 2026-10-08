cmake_minimum_required(VERSION 3.28)

# Static MSVC runtime (must be set before any target is created).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Sampler VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SAMPLER_ASIO "Build with ASIO support (needs external/asiosdk)" OFF)
option(SAMPLER_RUBBERBAND "Build with Rubber Band" ON)

# Debug flags carry no /Od or /RTC1 by default: engine targets get /O2 (see sampler_target_*),
# the rest get /Od. /RTC1 cannot be combined with /O2.
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi")
    set(CMAKE_C_FLAGS_DEBUG "/Zi")
    add_compile_options(/utf-8)
endif()

add_subdirectory(external/JUCE)
add_subdirectory(external/tracktion_engine/modules)
add_subdirectory(external/Catch2)

include(cmake/SamplerTargets.cmake)

add_subdirectory(src)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)
