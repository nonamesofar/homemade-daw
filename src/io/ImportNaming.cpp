#include "ImportNaming.h"

namespace sampler::io
{
juce::String sanitiseName(const juce::String& name)
{
    juce::String out;
    bool pendingDash = false;
    for (auto c : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c))
        {
            if (pendingDash && out.isNotEmpty())
                out << '-';
            pendingDash = false;
            out << juce::String::charToString(c);
        }
        else
        {
            pendingDash = true;
        }
    }
    out = out.substring(0, 60);
    while (out.endsWithChar('-'))
        out = out.dropLastCharacters(1);
    return out.isEmpty() ? juce::String("sound") : out;
}

juce::String shortHash(const juce::String& hash)
{
    return hash.fromFirstOccurrenceOf(":", false, false).substring(0, 8);
}

juce::String originalRelativePath(const juce::String& displayName, const juce::String& hash, const juce::String& extension)
{
    return "audio/originals/" + sanitiseName(displayName) + "-" + shortHash(hash) + "." + extension.toLowerCase().trimCharactersAtStart(".");
}

juce::String decodedRelativePath(const juce::String& displayName, const juce::String& hash)
{
    return "audio/" + sanitiseName(displayName) + "-" + shortHash(hash) + ".wav";
}
} // namespace sampler::io
