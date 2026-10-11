#pragma once

#include <juce_graphics/juce_graphics.h>

namespace sampler::ui
{
/** The embedded IBM Plex typefaces. Loaded from the program's own data on first use. */
struct Fonts
{
    enum class Weight
    {
        regular,
        medium,
        semiBold
    };

    static juce::Typeface::Ptr sansTypeface(Weight = Weight::regular);
    static juce::Typeface::Ptr monoTypeface(bool medium = false);

    static juce::Font sans(float height, Weight = Weight::regular);
    static juce::Font mono(float height, bool medium = false);
};
} // namespace sampler::ui
