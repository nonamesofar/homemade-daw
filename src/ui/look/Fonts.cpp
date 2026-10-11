#include "Fonts.h"

#include <SamplerBinaryData.h>

namespace sampler::ui
{
namespace
{
juce::Typeface::Ptr load(const char* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)); }
} // namespace

juce::Typeface::Ptr Fonts::sansTypeface(Weight weight)
{
    namespace data = sampler_resources;
    static const juce::Typeface::Ptr regular = load(data::IBMPlexSansRegular_ttf, data::IBMPlexSansRegular_ttfSize);
    static const juce::Typeface::Ptr medium = load(data::IBMPlexSansMedium_ttf, data::IBMPlexSansMedium_ttfSize);
    static const juce::Typeface::Ptr semiBold = load(data::IBMPlexSansSemiBold_ttf, data::IBMPlexSansSemiBold_ttfSize);
    return weight == Weight::medium ? medium : weight == Weight::semiBold ? semiBold : regular;
}

juce::Typeface::Ptr Fonts::monoTypeface(bool medium)
{
    namespace data = sampler_resources;
    static const juce::Typeface::Ptr regular = load(data::IBMPlexMonoRegular_ttf, data::IBMPlexMonoRegular_ttfSize);
    static const juce::Typeface::Ptr mediumFace = load(data::IBMPlexMonoMedium_ttf, data::IBMPlexMonoMedium_ttfSize);
    return medium ? mediumFace : regular;
}

juce::Font Fonts::sans(float height, Weight weight) { return juce::Font(juce::FontOptions(sansTypeface(weight)).withPointHeight(height)); }
juce::Font Fonts::mono(float height, bool medium) { return juce::Font(juce::FontOptions(monoTypeface(medium)).withPointHeight(height)); }
} // namespace sampler::ui
