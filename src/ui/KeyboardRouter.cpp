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
    if (key.getModifiers().isAltDown())
        return false;
    const auto code = key.getKeyCode();
    if (key.getModifiers().isCommandDown())
        return code == 'R' || code == 'r'; // Mod+R: Record

    return code == juce::KeyPress::spaceKey || code == juce::KeyPress::homeKey || code == juce::KeyPress::returnKey
           || code == 'L' || code == 'l';
}

namespace
{
// Pad 1 is bottom left: the bottom row Z X C V is pads 1 to 4, then A S D F, Q W E R and 1 2 3 4 on top.
constexpr char padKeys[16] = {'Z', 'X', 'C', 'V', 'A', 'S', 'D', 'F', 'Q', 'W', 'E', 'R', '1', '2', '3', '4'};
} // namespace

int KeyboardRouter::padForKey(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isAltDown() || mods.isShiftDown() || mods.isCtrlDown())
        return -1;
    const auto code = static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode())));
    for (int pad = 0; pad < 16; ++pad)
        if (code == padKeys[pad])
            return pad;
    return -1;
}

juce::String KeyboardRouter::keyLabelForPad(int pad)
{
    return juce::isPositiveAndBelow(pad, 16) ? juce::String::charToString(static_cast<juce::juce_wchar>(padKeys[pad])) : juce::String();
}

KeyboardRouter::Route KeyboardRouter::route(const juce::KeyPress& key, Focus focus)
{
    if (focus.dialogOpen)
        return Route::leaveToFocused;
    if (focus.textFieldFocused && ! isGlobalFileShortcut(key))
        return Route::leaveToFocused;
    if (focus.sampleEditorFocused && ! focus.textFieldFocused && padForKey(key) >= 0)
        return Route::pads;
    return Route::shortcuts;
}

bool KeyboardRouter::padKeyPressed(const juce::KeyPress& key)
{
    const auto pad = padForKey(key);
    if (pad < 0)
        return false;
    const auto code = static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(key.getKeyCode())));
    if (heldPads.count(code) != 0)
        return false; // an auto-repeat of a key that is still held
    heldPads[code] = pad;
    return true;
}

std::vector<int> KeyboardRouter::padKeysReleased()
{
    std::vector<int> released;
    for (auto it = heldPads.begin(); it != heldPads.end();)
    {
        if (isKeyDown(it->first))
            ++it;
        else
        {
            released.push_back(it->second);
            it = heldPads.erase(it);
        }
    }
    return released;
}

std::vector<int> KeyboardRouter::releaseAllPads()
{
    std::vector<int> released;
    for (const auto& [code, pad] : heldPads)
        released.push_back(pad);
    heldPads.clear();
    return released;
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
