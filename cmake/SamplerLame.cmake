# libmp3lame 3.100 as a static library from external/lame (LGPL; see external/VERSIONS.md for the pinned commit).
# LAME has no CMake build and no official Git repository, so the sources are compiled here: the encoder only (no
# mpglib decoder, no assembler), config.h written from cmake/lame_config.h.in. Third-party code: no /WX, warnings off.
set(LAME_DIR ${CMAKE_SOURCE_DIR}/external/lame)
set(LAME_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/lame)
configure_file(${CMAKE_SOURCE_DIR}/cmake/lame_config.h.in ${LAME_GENERATED_DIR}/config.h COPYONLY)

add_library(sampler_lame STATIC
    ${LAME_DIR}/libmp3lame/VbrTag.c
    ${LAME_DIR}/libmp3lame/bitstream.c
    ${LAME_DIR}/libmp3lame/encoder.c
    ${LAME_DIR}/libmp3lame/fft.c
    ${LAME_DIR}/libmp3lame/gain_analysis.c
    ${LAME_DIR}/libmp3lame/id3tag.c
    ${LAME_DIR}/libmp3lame/lame.c
    ${LAME_DIR}/libmp3lame/newmdct.c
    ${LAME_DIR}/libmp3lame/presets.c
    ${LAME_DIR}/libmp3lame/psymodel.c
    ${LAME_DIR}/libmp3lame/quantize.c
    ${LAME_DIR}/libmp3lame/quantize_pvt.c
    ${LAME_DIR}/libmp3lame/reservoir.c
    ${LAME_DIR}/libmp3lame/set_get.c
    ${LAME_DIR}/libmp3lame/tables.c
    ${LAME_DIR}/libmp3lame/takehiro.c
    ${LAME_DIR}/libmp3lame/util.c
    ${LAME_DIR}/libmp3lame/vbrquantize.c
    ${LAME_DIR}/libmp3lame/version.c)
target_include_directories(sampler_lame
    PUBLIC ${LAME_DIR}/include
    PRIVATE ${LAME_DIR}/libmp3lame ${LAME_GENERATED_DIR})
target_compile_definitions(sampler_lame PRIVATE HAVE_CONFIG_H)
if(MSVC)
    target_compile_options(sampler_lame PRIVATE /W0)
endif()
sampler_target_optimised(sampler_lame)
set_target_properties(sampler_lame PROPERTIES FOLDER external)
