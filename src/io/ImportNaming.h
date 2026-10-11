#pragma once

#include <juce_core/juce_core.h>

namespace sampler::io
{
/**
    A readable, filesystem-safe form of a file name without extension: letters and digits are kept, every other run of
    characters becomes one '-', no leading or trailing '-', at most 60 characters. "Break 90 (final)" becomes
    "Break-90-final". An empty result becomes "sound".
*/
juce::String sanitiseName(const juce::String& nameWithoutExtension);

/** First 8 hex characters of "sha256:<hex>" (or of a bare hex string). */
juce::String shortHash(const juce::String& hash);

/** Path of the copy of the user's file, relative to the bundle: audio/originals/<name>-<hash8>.<ext> (extension lowercased). */
juce::String originalRelativePath(const juce::String& displayName, const juce::String& hash, const juce::String& extension);

/** Path of the decoded working copy, relative to the bundle: audio/<name>-<hash8>.wav. */
juce::String decodedRelativePath(const juce::String& displayName, const juce::String& hash);
} // namespace sampler::io
