#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace sampler::ui
{
/**
    Short messages that appear at the bottom of the window and go away by themselves (errors, notes). The component
    does not take mouse clicks. Plain look for now; the final look comes with the style pass (task 7.3).
*/
class Toast : public juce::Component, private juce::Timer
{
public:
    enum class Kind { info, error };

    struct Message
    {
        juce::String text;
        Kind kind = Kind::info;
        juce::uint32 expiresAtMs = 0;
    };

    static constexpr int visibleMs = 5000;

    Toast();

    /** Adds a message; it disappears after `visibleMs`. At most 4 stay on screen, the oldest goes first. */
    void show(const juce::String& text, Kind kind = Kind::info);

    const std::vector<Message>& messages() const { return active; }

    void paint(juce::Graphics&) override;

    /** The size needed for the current messages, anchored at the bottom centre of `area`. */
    juce::Rectangle<int> boundsFor(juce::Rectangle<int> area) const;

private:
    void timerCallback() override;

    std::vector<Message> active;
};
} // namespace sampler::ui
