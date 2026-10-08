#pragma once

#include <juce_graphics/juce_graphics.h>

namespace sampler::platform
{
/** Test and diagnostics helpers for native windows (a plugin editor is a native child view, so JUCE cannot snapshot it). */
struct NativeWindowTools
{
    /** Draws the whole window, native children included, into an image. Empty image on failure. */
    static juce::Image snapshot(void* nativeWindowHandle);

    /**
        Sends a left click (press and release) to the deepest child window under `clientPosition` (a point in the
        client area of the given top-level window), without moving the mouse cursor. False if no window was found.
    */
    static bool postClick(void* nativeWindowHandle, juce::Point<int> clientPosition);
};
} // namespace sampler::platform
