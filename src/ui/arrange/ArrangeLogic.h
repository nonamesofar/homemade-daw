#pragma once

// Plain arrangement logic for the UI (design D-7): no painting and no engine, so Catch2 tests it directly.
// Positions are in beats (Tracktion beats: the time signature's denominator note), pixels are doubles.
#include "commands/ArrangeCommands.h"
#include "engine/Arrangement.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace sampler::ui
{
//==============================================================================
/** bar . beat . sixteenth, counted from 1 (design: transport spec, "Position display"). */
namespace PositionText
{
/** Subdivisions shown per beat: sixteenth notes (4 per quarter-note beat, 2 per eighth, 1 per sixteenth). */
int subdivisionsPerBeat(engine::TimeSig);
/** "5 . 3 . 1". */
juce::String format(double beats, engine::TimeSig);
/** "5.2" -> beats (missing parts count as 1). Empty for text that is not a position in this time signature. */
std::optional<double> parse(const juce::String& text, engine::TimeSig);
} // namespace PositionText

//==============================================================================
/** Stop-twice, Home/Enter and Shift+Space (transport spec). */
namespace TransportLogic
{
/** Where a second Stop, Home or Enter goes: the loop start when looping is on, else the start of the song. */
double returnPosition(bool looping, engine::LoopRange loop);
/** Shift+Space starts from the selection's start, or from the playhead with nothing selected. */
double playFromSelection(std::optional<double> selectionStart, double playhead);
} // namespace TransportLogic

//==============================================================================
enum class SnapValue
{
    off,
    bar,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    quarterTriplet,
    eighthTriplet,
    sixteenthTriplet
};

/** The grid (design D-7, arrangement spec "Snap"). A new project snaps to 1 bar. */
namespace Snap
{
/** In menu order: Off, 1 bar, 1/2 ... 1/32, 1/4T, 1/8T, 1/16T. */
const std::vector<SnapValue>& all();
juce::String name(SnapValue);
/** Stored form (the same as the name). Unknown text gives 1 bar. */
SnapValue fromName(const juce::String&);
/** Grid step in beats; 0 for Off. */
double gridBeats(SnapValue, engine::TimeSig);
/** Nearest grid line, or the beat itself with snap off or bypassed (Alt). */
double snap(double beat, SnapValue, engine::TimeSig, bool bypass = false);
/** Grid line at or before the beat. */
double snapDown(double beat, SnapValue, engine::TimeSig, bool bypass = false);
/** The shortest clip or loop: one grid step, or `fallbackBeats` with snap off. */
double minimumLength(SnapValue, engine::TimeSig, double fallbackBeats);
} // namespace Snap

//==============================================================================
/**
    Which clips are selected, and the selected track (design D-7). Not undoable. Holds ids only, so it survives undo,
    redo and reopening the project.
*/
class SelectionModel
{
public:
    const std::vector<juce::String>& clips() const { return selected; }
    bool isSelected(const juce::String& clipId) const;
    bool isEmpty() const { return selected.empty(); }

    /** Plain click selects only it; Shift adds it; Mod adds or removes it. */
    void click(const juce::String& clipId, bool shift, bool mod);
    void setClips(std::vector<juce::String>);
    void clearClips();
    /** A marquee touched these clips; with `keepExisting` (Shift) the earlier selection stays. */
    void marquee(const std::vector<juce::String>& touched, const std::vector<juce::String>& before, bool keepExisting);
    /** Drops ids that no longer exist. */
    void retainOnly(const std::set<juce::String>& existing);
    /** After undo or redo: keep the selected clips that still exist and select those that came back. */
    void afterHistoryChange(const std::set<juce::String>& before, const std::set<juce::String>& after);

    const juce::String& track() const { return selectedTrack; }
    /** The track that gets new tracks and pastes; also what a lane click picks. Clears the "header selected" state. */
    void setTrack(const juce::String& trackId)
    {
        selectedTrack = trackId;
        headerSelected = false;
    }
    /** A click on a track header: the track is selected as a track, so Delete removes it (when no clip is selected). */
    void selectHeader(const juce::String& trackId)
    {
        selectedTrack = trackId;
        headerSelected = true;
    }
    /** After tracks changed: a selected track that is gone moves to the first remaining one (none if empty), not as a header. */
    void retainTrack(const std::vector<juce::String>& trackIds)
    {
        if (std::find(trackIds.begin(), trackIds.end(), selectedTrack) != trackIds.end())
            return;
        selectedTrack = trackIds.empty() ? juce::String() : trackIds.front();
        headerSelected = false;
    }
    bool isHeaderSelected() const { return headerSelected && selectedTrack.isNotEmpty(); }

    std::function<void()> onChange;

private:
    void changed()
    {
        if (onChange)
            onChange();
    }

    std::vector<juce::String> selected;
    juce::String selectedTrack;
    bool headerSelected = false;
};

//==============================================================================
/** Copied clips relative to the earliest clip and the topmost track (arrangement spec, "Copy, cut and paste"). */
namespace Clipboard
{
std::vector<commands::ClipRecord> recordsFor(const std::vector<engine::ClipInfo>& clips,
                                             const std::function<int(const juce::String& trackId)>& trackIndex);
} // namespace Clipboard

//==============================================================================
/**
    Meter ballistics (track-mixer spec): the bar falls at 20 dB per second, the highest recent peak is held for 1.5 s,
    the average is the RMS of the readings of the last 300 ms, and the over flag stays on until cleared.
*/
class MeterBallistics
{
public:
    static constexpr double floorDb = -60.0;
    static constexpr double fallDbPerSecond = 20.0;
    static constexpr double holdMs = 1500.0;
    static constexpr double rmsWindowMs = 300.0;

    /** One reading: the peak since the previous one, the time since then, and whether it went over 0 dBFS. */
    void push(double peakDb, double elapsedMs, bool over = false);

    double levelDb() const { return level; }
    double heldDb() const { return held; }
    double rmsDb() const;
    bool isOver() const { return overLatched; }
    void clearOver() { overLatched = false; }

private:
    double level = floorDb, held = floorDb, sinceHeld = 0.0;
    bool overLatched = false;
    struct Reading
    {
        double amplitude, ms;
    };
    std::vector<Reading> recent;
};

/** dB readouts: "-inf" at or below -100 dB, else one decimal ("-4.5"). */
juce::String formatDb(double db);
} // namespace sampler::ui
