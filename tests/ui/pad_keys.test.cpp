// M3 3.6: the sample editor's pad keys in the KeyboardRouter, as plain logic.
#include <catch2/catch_test_macros.hpp>

#include "ui/KeyboardRouter.h"

using sampler::ui::KeyboardRouter;

namespace
{
juce::KeyPress key(char c, juce::ModifierKeys mods = {}) { return juce::KeyPress(static_cast<int>(c), mods, c); }

KeyboardRouter::Focus editorFocus()
{
    KeyboardRouter::Focus f;
    f.sampleEditorFocused = true;
    return f;
}
} // namespace

TEST_CASE("the pad keys are laid out like an MPC with pad 1 at the bottom left", "[ui][keys][pads]")
{
    // Bottom row Z X C V = pads 1 to 4, then A S D F, Q W E R, and 1 2 3 4 on top.
    const char* rows[] = {"ZXCV", "ASDF", "QWER", "1234"};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
        {
            const char c = rows[row][col];
            INFO("key " << c);
            CHECK(KeyboardRouter::padForKey(key(c)) == row * 4 + col);
            CHECK(KeyboardRouter::padForKey(key(static_cast<char>(std::tolower(c)))) == row * 4 + col);
            CHECK(KeyboardRouter::keyLabelForPad(row * 4 + col) == juce::String::charToString(static_cast<juce::juce_wchar>(c)));
        }
    CHECK(KeyboardRouter::padForKey(key('L')) == -1);
    CHECK(KeyboardRouter::padForKey(key('M')) == -1);
    CHECK(KeyboardRouter::padForKey(key('5')) == -1);
    CHECK(KeyboardRouter::keyLabelForPad(16).isEmpty());
}

TEST_CASE("pad keys only play in the sample editor scope and without a modifier", "[ui][keys][pads]")
{
    // Outside the editor the same key keeps its job (S solos a track).
    CHECK(KeyboardRouter::route(key('S'), {}) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(key('S'), editorFocus()) == KeyboardRouter::Route::pads);

    // Modifier keys keep their shortcuts: Mod+R records, Mod+Z undoes, Mod+S saves.
    const juce::ModifierKeys mod(juce::ModifierKeys::commandModifier);
    CHECK(KeyboardRouter::route(key('R', mod), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(key('Z', mod), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(key('S', mod), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(key('Z', juce::ModifierKeys(juce::ModifierKeys::shiftModifier)), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(key('Z', juce::ModifierKeys(juce::ModifierKeys::altModifier)), editorFocus()) == KeyboardRouter::Route::shortcuts);

    // L is not a pad key: it keeps toggling the loop, and so do Space and Home.
    CHECK(KeyboardRouter::route(key('L'), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(juce::KeyPress(juce::KeyPress::spaceKey), editorFocus()) == KeyboardRouter::Route::shortcuts);
    CHECK(KeyboardRouter::route(juce::KeyPress(juce::KeyPress::deleteKey), editorFocus()) == KeyboardRouter::Route::shortcuts);
}

TEST_CASE("a text field or a dialog wins over the pad keys", "[ui][keys][pads]")
{
    auto focus = editorFocus();
    focus.textFieldFocused = true;
    CHECK(KeyboardRouter::route(key('Q'), focus) == KeyboardRouter::Route::leaveToFocused);
    CHECK(KeyboardRouter::route(key('S', juce::ModifierKeys(juce::ModifierKeys::commandModifier)), focus) == KeyboardRouter::Route::shortcuts); // File shortcut
    focus.textFieldFocused = false;
    focus.dialogOpen = true;
    CHECK(KeyboardRouter::route(key('Q'), focus) == KeyboardRouter::Route::leaveToFocused);
}

TEST_CASE("a held pad key plays once and is let go on key-up", "[ui][keys][pads]")
{
    KeyboardRouter router;
    std::set<int> down;
    router.isKeyDown = [&down](int code) { return down.count(code) != 0; };

    down.insert('Z');
    CHECK(router.padKeyPressed(key('Z')));
    CHECK(router.anyPadHeld());
    // Auto-repeat: the OS sends the key again while it stays down.
    CHECK_FALSE(router.padKeyPressed(key('Z')));
    CHECK_FALSE(router.padKeyPressed(key('z')));
    CHECK(router.padKeysReleased().empty());

    // A second key joins; the first is let go on its own.
    down.insert('X');
    CHECK(router.padKeyPressed(key('X')));
    down.erase('Z');
    const auto released = router.padKeysReleased();
    REQUIRE(released.size() == 1);
    CHECK(released[0] == 0); // pad 1
    CHECK(router.anyPadHeld());

    // The same key can be played again after it was let go.
    down.insert('Z');
    CHECK(router.padKeyPressed(key('Z')));

    // Keys that are no pad keys do not count.
    CHECK_FALSE(router.padKeyPressed(key('L')));
}

TEST_CASE("losing focus lets go of every held pad", "[ui][keys][pads]")
{
    KeyboardRouter router;
    std::set<int> down{'Z', 'S', '3'};
    router.isKeyDown = [&down](int code) { return down.count(code) != 0; };
    for (const char c : {'Z', 'S', '3'})
        REQUIRE(router.padKeyPressed(key(c)));

    auto released = router.releaseAllPads();
    std::sort(released.begin(), released.end());
    CHECK(released == std::vector<int>{0, 5, 14});
    CHECK_FALSE(router.anyPadHeld());
    CHECK(router.releaseAllPads().empty());
    // Still down on the keyboard, pressed again after the focus came back: plays again (a new press).
    CHECK(router.padKeyPressed(key('Z')));
}
