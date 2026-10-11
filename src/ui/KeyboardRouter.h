#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>

namespace sampler::ui
{
/**
    Decides who gets a key press (design D-9): an open dialog first, then a focused text field, then the focused view,
    then the global shortcuts. Plain logic (the caller says what has focus), so it is unit tested.

    While a dialog is open every key is left to it. While a text field has focus, every key is left to it except the
    global File shortcuts (Mod+S, Mod+Shift+S, Mod+N, Mod+O, Mod+I, Mod+Q), so typing "m", "s", "z", Space or Delete only
    edits the text and Mod+C/V/Z act on the text. Otherwise the key goes to the shortcut tables.

    Transport keys (Space, Home, Enter, L) are not repeated while held: `acceptsTransportKey` lets the first press through
    and ignores the auto-repeats until the key was released.
*/
class KeyboardRouter
{
public:
    enum class Route
    {
        leaveToFocused, // the dialog or text field handles it
        shortcuts       // the view and global shortcut tables
    };

    struct Focus
    {
        bool dialogOpen = false;
        bool textFieldFocused = false;
    };

    static Route route(const juce::KeyPress&, Focus);
    static bool isGlobalFileShortcut(const juce::KeyPress&);
    static bool isTransportKey(const juce::KeyPress&);

    /** True for the first press of a transport key; false for its auto-repeats. Other keys always pass. */
    bool acceptsTransportKey(const juce::KeyPress&);
    /** Call from keyStateChanged: forgets keys that are no longer held. */
    void keysChanged();

    /** Whether a key code is held now (replaced in tests). */
    std::function<bool(int keyCode)> isKeyDown = [](int code) { return juce::KeyPress::isKeyCurrentlyDown(code); };

private:
    std::set<int> held;
};
} // namespace sampler::ui
