#include "KeyboardRouter.h"

namespace sampler::ui
{
bool KeyboardRouter::isGlobalFileShortcut(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isAltDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    return c == 's' || c == 'n' || c == 'o' || c == 'i' || c == 'q';
}

bool KeyboardRouter::isTransportKey(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
        return false;
    const auto code = key.getKeyCode();
    return code == juce::KeyPress::spaceKey || code == juce::KeyPress::homeKey || code == juce::KeyPress::returnKey
           || code == 'L' || code == 'l';
}

KeyboardRouter::Route KeyboardRouter::route(const juce::KeyPress& key, Focus focus)
{
    if (focus.dialogOpen)
        return Route::leaveToFocused;
    if (focus.textFieldFocused && ! isGlobalFileShortcut(key))
        return Route::leaveToFocused;
    return Route::shortcuts;
}

bool KeyboardRouter::acceptsTransportKey(const juce::KeyPress& key)
{
    if (! isTransportKey(key))
        return true;
    const auto code = key.getKeyCode();
    if (held.count(code) != 0 && isKeyDown(code))
        return false; // an auto-repeat of a key that is still held
    held.insert(code);
    return true;
}

void KeyboardRouter::keysChanged()
{
    for (auto it = held.begin(); it != held.end();)
        it = isKeyDown(*it) ? std::next(it) : held.erase(it);
}
} // namespace sampler::ui
