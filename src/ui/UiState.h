#pragma once

#include <juce_core/juce_core.h>

#include <map>

namespace sampler::ui
{
/**
    How the sample editor shows one sound. None of this is part of the project: zoom, scroll, the selected slice, the
    pad bank and the units of START / END are not undoable and not saved (design D8).
*/
struct EditorViewState
{
    double startFrame = 0.0;
    double framesPerPixel = 0.0; // 0 = the whole sound
    int selectedSlice = 0;
    int padBank = 0;
    int units = 0; // 0 bars, 1 seconds, 2 samples
};

/** View state that belongs to the window, not to the project. */
class UiState
{
public:
    /** The sound open in the sample editor, or empty while the arrangement is shown. */
    juce::String openSourceId;

    EditorViewState& editorState(const juce::String& sourceId) { return editors[sourceId]; }
    void clear()
    {
        openSourceId = {};
        editors.clear();
    }

private:
    std::map<juce::String, EditorViewState> editors;
};
} // namespace sampler::ui
