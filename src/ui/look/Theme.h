#pragma once

#include <juce_graphics/juce_graphics.h>

/** Colour tokens of the mock (design §10.2). Everything the UI paints takes its colours from here. */
namespace sampler::ui::theme
{
inline const juce::Colour background{0xff17181b};
inline const juce::Colour panel{0xff202125};
inline const juce::Colour raised{0xff27282d};
inline const juce::Colour selected{0xff2f3036};
inline const juce::Colour border{0xff34353b};
inline const juce::Colour dashed{0xff4a4b53};
inline const juce::Colour text{0xffececef};
inline const juce::Colour muted{0xffa1a2ab};
inline const juce::Colour accent{0xffff7a3d};
inline const juce::Colour record{0xffff6b6b};
inline const juce::Colour ok{0xff4fd1c5};
inline const juce::Colour errorFill{0xff4a2a2d};
inline const juce::Colour infoFill{0xff2a2c33};

/** The track palette, in order. */
inline const juce::Colour trackColours[] = {juce::Colour(0xffff8f5a), juce::Colour(0xff4fd1c5), juce::Colour(0xffb9a2ff), juce::Colour(0xfff0d65f)};
} // namespace sampler::ui::theme
