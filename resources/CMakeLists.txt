# Fonts compiled into the program (no runtime downloads). IBM Plex is SIL OFL 1.1: LICENSE-IBM-Plex.txt travels with it.
juce_add_binary_data(sampler_resources
    HEADER_NAME "SamplerBinaryData.h"
    NAMESPACE sampler_resources
    SOURCES
        fonts/IBMPlexSans-Regular.ttf
        fonts/IBMPlexSans-Medium.ttf
        fonts/IBMPlexSans-SemiBold.ttf
        fonts/IBMPlexMono-Regular.ttf
        fonts/IBMPlexMono-Medium.ttf)
set_target_properties(sampler_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)
