#include "ArrangeLogic.h"

#include <algorithm>
#include <cmath>

namespace sampler::ui
{
namespace PositionText
{
int subdivisionsPerBeat(engine::TimeSig sig) { return std::max(1, 16 / std::max(1, sig.denominator)); }

juce::String format(double beats, engine::TimeSig sig)
{
    const auto perBar = static_cast<double>(std::max(1, sig.numerator));
    const auto subs = subdivisionsPerBeat(sig);
    const auto total = std::max(0.0, beats) + 1.0e-6; // 2.9999999 is beat 3
    const auto bar = static_cast<int>(std::floor(total / perBar));
    const auto inBar = total - bar * perBar;
    const auto beat = static_cast<int>(std::floor(inBar));
    const auto sub = static_cast<int>(std::floor((inBar - beat) * subs));
    return juce::String(bar + 1) + " . " + juce::String(beat + 1) + " . " + juce::String(std::min(sub, subs - 1) + 1);
}

std::optional<double> parse(const juce::String& text, engine::TimeSig sig)
{
    const auto parts = juce::StringArray::fromTokens(text.removeCharacters(" \t"), ".", "");
    if (parts.isEmpty() || parts.size() > 3)
        return std::nullopt;
    int values[3] = {1, 1, 1};
    for (int i = 0; i < parts.size(); ++i)
    {
        if (parts[i].isEmpty() || ! parts[i].containsOnly("0123456789") || parts[i].length() > 6)
            return std::nullopt;
        values[i] = parts[i].getIntValue();
    }
    const auto subs = subdivisionsPerBeat(sig);
    if (values[0] < 1 || values[1] < 1 || values[1] > sig.numerator || values[2] < 1 || values[2] > subs)
        return std::nullopt;
    return (values[0] - 1) * static_cast<double>(sig.numerator) + (values[1] - 1) + (values[2] - 1) / static_cast<double>(subs);
}
} // namespace PositionText

namespace TransportLogic
{
double returnPosition(bool looping, engine::LoopRange loop) { return looping && ! loop.isEmpty() ? loop.start : 0.0; }

double playFromSelection(std::optional<double> selectionStart, double playhead) { return selectionStart.value_or(playhead); }
} // namespace TransportLogic

namespace Snap
{
const std::vector<SnapValue>& all()
{
    static const std::vector<SnapValue> values{SnapValue::off,          SnapValue::bar,           SnapValue::half,
                                               SnapValue::quarter,      SnapValue::eighth,        SnapValue::sixteenth,
                                               SnapValue::thirtySecond, SnapValue::quarterTriplet, SnapValue::eighthTriplet,
                                               SnapValue::sixteenthTriplet};
    return values;
}

juce::String name(SnapValue v)
{
    switch (v)
    {
        case SnapValue::off: return "Off";
        case SnapValue::bar: return "1 bar";
        case SnapValue::half: return "1/2";
        case SnapValue::quarter: return "1/4";
        case SnapValue::eighth: return "1/8";
        case SnapValue::sixteenth: return "1/16";
        case SnapValue::thirtySecond: return "1/32";
        case SnapValue::quarterTriplet: return "1/4T";
        case SnapValue::eighthTriplet: return "1/8T";
        case SnapValue::sixteenthTriplet: return "1/16T";
    }
    return "1 bar";
}

SnapValue fromName(const juce::String& text)
{
    for (auto v : all())
        if (name(v) == text)
            return v;
    return SnapValue::bar;
}

static double noteGridBeats(SnapValue v, engine::TimeSig sig)
{
    // A note value 1/N lasts denominator/N beats (a beat is the denominator's note).
    const auto note = [&sig](double n) { return sig.denominator / n; };
    switch (v)
    {
        case SnapValue::off: return 0.0;
        case SnapValue::bar: return sig.numerator;
        case SnapValue::half: return note(2.0);
        case SnapValue::quarter: return note(4.0);
        case SnapValue::eighth: return note(8.0);
        case SnapValue::sixteenth: return note(16.0);
        case SnapValue::thirtySecond: return note(32.0);
        case SnapValue::quarterTriplet: return note(4.0) * 2.0 / 3.0;
        case SnapValue::eighthTriplet: return note(8.0) * 2.0 / 3.0;
        case SnapValue::sixteenthTriplet: return note(16.0) * 2.0 / 3.0;
    }
    return 0.0;
}

double gridBeats(SnapValue v, engine::TimeSig sig)
{
    // No step is longer than a bar (1/2 in 3/8 would be 4 beats): bar lines stay reachable and a loop can be one bar.
    return std::min(noteGridBeats(v, sig), static_cast<double>(sig.numerator));
}

double snap(double beat, SnapValue v, engine::TimeSig sig, bool bypass)
{
    const auto grid = gridBeats(v, sig);
    if (bypass || grid <= 0.0)
        return beat;
    // Grid lines count from the start of the bar that holds the beat, so bar lines are always targets (7/8 with 1/4).
    const auto bar = static_cast<double>(sig.numerator);
    const auto barStart = std::floor(beat / bar + 1.0e-9) * bar;
    const auto inBar = barStart + std::round((beat - barStart) / grid) * grid;
    const auto nextBar = barStart + bar;
    return std::abs(nextBar - beat) < std::abs(inBar - beat) ? nextBar : inBar;
}

double snapDown(double beat, SnapValue v, engine::TimeSig sig, bool bypass)
{
    const auto grid = gridBeats(v, sig);
    if (bypass || grid <= 0.0)
        return beat;
    const auto bar = static_cast<double>(sig.numerator);
    const auto barStart = std::floor(beat / bar + 1.0e-9) * bar;
    return barStart + std::floor((beat - barStart) / grid + 1.0e-9) * grid;
}

double minimumLength(SnapValue v, engine::TimeSig sig, double fallbackBeats)
{
    const auto grid = gridBeats(v, sig);
    return grid > 0.0 ? grid : fallbackBeats;
}
} // namespace Snap

//==============================================================================
bool SelectionModel::isSelected(const juce::String& clipId) const
{
    return std::find(selected.begin(), selected.end(), clipId) != selected.end();
}

void SelectionModel::click(const juce::String& clipId, bool shift, bool mod)
{
    headerSelected = false;
    if (mod)
    {
        if (isSelected(clipId))
            selected.erase(std::remove(selected.begin(), selected.end(), clipId), selected.end());
        else
            selected.push_back(clipId);
    }
    else if (shift)
    {
        if (! isSelected(clipId))
            selected.push_back(clipId);
    }
    else
    {
        selected = {clipId};
    }
    changed();
}

void SelectionModel::setClips(std::vector<juce::String> ids)
{
    headerSelected = false;
    selected.clear();
    for (auto& id : ids)
        if (! isSelected(id))
            selected.push_back(std::move(id));
    changed();
}

void SelectionModel::clearClips()
{
    headerSelected = false;
    if (selected.empty())
        return;
    selected.clear();
    changed();
}

void SelectionModel::marquee(const std::vector<juce::String>& touched, const std::vector<juce::String>& before, bool keepExisting)
{
    headerSelected = false;
    std::vector<juce::String> result = keepExisting ? before : std::vector<juce::String>{};
    for (const auto& id : touched)
        if (std::find(result.begin(), result.end(), id) == result.end())
            result.push_back(id);
    if (result != selected)
    {
        selected = std::move(result);
        changed();
    }
}

void SelectionModel::retainOnly(const std::set<juce::String>& existing)
{
    const auto oldSize = selected.size();
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const juce::String& id) { return ! existing.count(id); }),
                   selected.end());
    if (selected.size() != oldSize)
        changed();
}

void SelectionModel::afterHistoryChange(const std::set<juce::String>& before, const std::set<juce::String>& after)
{
    std::vector<juce::String> result;
    for (const auto& id : selected)
        if (after.count(id))
            result.push_back(id);
    for (const auto& id : after)
        if (! before.count(id) && std::find(result.begin(), result.end(), id) == result.end())
            result.push_back(id);
    selected = std::move(result);
    changed();
}

//==============================================================================
namespace Clipboard
{
std::vector<commands::ClipRecord> recordsFor(const std::vector<engine::ClipInfo>& clips,
                                             const std::function<int(const juce::String&)>& trackIndex)
{
    std::vector<commands::ClipRecord> records;
    if (clips.empty())
        return records;
    double first = clips.front().start;
    int top = trackIndex(clips.front().trackId);
    for (const auto& c : clips)
    {
        first = std::min(first, c.start);
        top = std::min(top, trackIndex(c.trackId));
    }
    for (const auto& c : clips)
    {
        commands::ClipRecord r;
        r.sourceId = c.sourceId;
        r.name = c.name;
        r.trackOffset = trackIndex(c.trackId) - top;
        r.startOffset = c.start - first;
        r.lengthSeconds = c.lengthSeconds;
        r.offsetSeconds = c.offsetSeconds;
        r.gainDb = c.gainDb;
        r.reversed = c.reversed;
        records.push_back(r);
    }
    return records;
}
} // namespace Clipboard

//==============================================================================
void MeterBallistics::push(double peakDb, double elapsedMs, bool over)
{
    const auto input = std::max(floorDb, peakDb);
    level = std::max(input, level - fallDbPerSecond * elapsedMs / 1000.0);
    level = std::max(floorDb, level);
    if (input >= held)
    {
        held = input;
        sinceHeld = 0.0;
    }
    else if ((sinceHeld += elapsedMs) > holdMs)
    {
        held = level;
        sinceHeld = 0.0;
    }
    if (over || peakDb > 0.0)
        overLatched = true;

    recent.push_back({input <= floorDb ? 0.0 : std::pow(10.0, input / 20.0), elapsedMs});
    double total = 0.0;
    for (auto it = recent.rbegin(); it != recent.rend(); ++it)
    {
        total += it->ms;
        if (total > rmsWindowMs)
        {
            recent.erase(recent.begin(), it.base() - 1);
            break;
        }
    }
}

double MeterBallistics::rmsDb() const
{
    double sum = 0.0, ms = 0.0;
    for (const auto& r : recent)
    {
        sum += r.amplitude * r.amplitude * r.ms;
        ms += r.ms;
    }
    if (ms <= 0.0 || sum <= 0.0)
        return floorDb;
    return std::max(floorDb, 20.0 * std::log10(std::sqrt(sum / ms)));
}

juce::String formatDb(double db)
{
    if (db <= -99.9)
        return "-inf";
    return juce::String(db, 1);
}
} // namespace sampler::ui
