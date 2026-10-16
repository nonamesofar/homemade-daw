#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace sampler::ui
{
/**
    Decides who gets a key press (design D-9, D9 of M3): an open dialog first, then a focused text field, then the sample
    editor's pad keys (while the editor has focus), then the focused view, then the global shortcuts. Plain logic (the caller says what has focus), so it is unit tested.

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
        pads,           // a pad key in the sample editor
        shortcuts       // the view and global shortcut tables
    };

    struct Focus
    {
        bool dialogOpen = false;
        bool textFieldFocused = false;
        bool sampleEditorFocused = false;
    };

    /** The pad (0 = pad 1, bottom left, to 15) a key plays, or -1: 1 2 3 4 / Q W E R / A S D F / Z X C V, Z being pad 1. */
    static int padForKey(const juce::KeyPress&);
    /** The key that plays a pad (0..15) as a label ("Z"). */
    static juce::String keyLabelForPad(int pad);

    /**
        Pad keys are played on key-down and let go on key-up. `padKeyPressed` is true for the first press of a key and false
        for the auto-repeats while it is held. `padKeysReleased` (call from keyStateChanged) returns the pads whose key is no
        longer held; `releaseAllPads` returns every held pad (focus lost, editor hidden, app deactivated).
    */
    bool padKeyPressed(const juce::KeyPress&);
    std::vector<int> padKeysReleased();
    std::vector<int> releaseAllPads();
    bool anyPadHeld() const { return ! heldPads.empty(); }

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
    std::map<int, int> heldPads; // key code -> pad
};
} // namespace sampler::ui
