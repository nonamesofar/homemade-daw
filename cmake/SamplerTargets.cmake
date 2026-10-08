# Warnings as errors on our own targets only (never on third-party code).
function(sampler_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    endif()
endfunction()

# Optimised even in the Debug preset (debug Tracktion is too slow for audio).
function(sampler_target_optimised target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<IF:$<CONFIG:Debug>,/O2,>")
    endif()
endfunction()

# Plain unoptimised debug code for everything else.
function(sampler_target_debuggable target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:/Od>")
    endif()
endfunction()
