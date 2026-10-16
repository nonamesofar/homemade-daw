#include "SliceCommands.h"

#include <algorithm>
#include <cmath>

namespace sampler::commands
{
namespace
{
model::Source findSource(ModelContext& context, const juce::String& id)
{
    return model::SamplerState(context.sampler).findSourceById(id);
}

/** The sensitivity as saved: 0..100 with two decimals (so it reads back exactly from the project file). */
double savedSensitivity(double sensitivity)
{
    return std::round(std::clamp(sensitivity, 0.0, 100.0) * 100.0) / 100.0;
}

/** Resets every slice to the defaults, as Beat, Transient and Auto-slice do. */
void resetSettings(model::SliceSetData& data)
{
    data.slices.assign(static_cast<size_t>(data.numSlices()), model::SliceSettings{});
}

/** Writes `after` if it differs from `before` and is valid; a command that cannot apply changes nothing. */
bool writeIfChanged(ModelContext& context, model::Source source, const model::SliceSetData& before, const model::SliceSetData& after)
{
    if (after == before)
        return false;
    if (model::findSliceSetViolation(after, source.frames(), source.sampleRate()))
        return false;
    // The existing node is changed in place where only values differ: small, coalescing undo records.
    auto node = source.node();
    auto existing = node.getChildWithName(model::sliceIds::SLICESET);
    if (existing.isValid() && after.slices == before.slices && after.markers.size() == before.markers.size())
    {
        existing.setProperty(model::sliceIds::mode, model::toString(after.mode), &context.undo);
        existing.setProperty(model::sliceIds::division, model::toString(after.division), &context.undo);
        existing.setProperty(model::sliceIds::sensitivity, after.sensitivity, &context.undo);
        juce::StringArray points;
        for (const auto marker : after.markers)
            points.add(juce::String(marker));
        existing.setProperty(model::sliceIds::markers, points.joinIntoString(" "), &context.undo);
        return true;
    }
    source.setSliceSet(after, &context.undo);
    return true;
}
} // namespace

juce::String SetSliceMode::name() const
{
    switch (mode)
    {
        case model::SliceMode::beat: return "Slice by beat";
        case model::SliceMode::transient: return "Slice by transient";
        case model::SliceMode::manual: return "Slice manually";
    }
    return "Slice manually";
}

void SetSliceMode::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    auto after = before;
    after.mode = mode;
    after.division = division;
    after.sensitivity = savedSensitivity(sensitivity);
    if (mode != model::SliceMode::manual)
    {
        after.markers = markers;
        resetSettings(after);
    }
    writeIfChanged(context, source, before, after);
}

void AutoSlice::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    auto after = before;
    after.mode = model::SliceMode::transient;
    after.sensitivity = savedSensitivity(sensitivity);
    after.markers = markers;
    resetSettings(after);
    writeIfChanged(context, source, before, after);
}

void SetSliceDivision::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    auto after = before;
    after.mode = model::SliceMode::beat;
    after.division = division;
    after.markers = markers;
    resetSettings(after);
    writeIfChanged(context, source, before, after);
}

void AddSlicePoint::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    auto after = before;
    if (! model::insertSliceMarker(after, frame, source.frames(), source.sampleRate()))
        return;
    after.mode = model::SliceMode::manual;
    writeIfChanged(context, source, before, after);
}

void MoveSlicePoint::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    if (index < 0 || index >= static_cast<int>(before.markers.size()))
        return;
    const auto minFrames = model::minSliceFrames(source.sampleRate());
    const auto i = static_cast<size_t>(index);
    const auto previous = i == 0 ? juce::int64{0} : before.markers[i - 1];
    const auto next = i + 1 == before.markers.size() ? source.frames() : before.markers[i + 1];
    if (next - previous < 2 * minFrames)
        return; // no room for the point at all
    auto after = before;
    after.markers[i] = std::clamp(frame, previous + minFrames, next - minFrames);
    after.mode = model::SliceMode::manual;
    writeIfChanged(context, source, before, after);
}

void RemoveSlicePoint::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    if (index < 0 || index >= static_cast<int>(before.markers.size()))
        return;
    auto after = before;
    model::removeSliceMarker(after, index);
    after.mode = model::SliceMode::manual;
    writeIfChanged(context, source, before, after);
}

void SetSliceBounds::perform(ModelContext& context)
{
    applied = false;
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    const auto before = source.sliceSet();
    const int markerIndex = end ? slice : slice - 1; // the point at the slice's start is the previous slice's end
    if (markerIndex < 0 || markerIndex >= static_cast<int>(before.markers.size()))
        return; // the ends of the sound
    const auto minFrames = model::minSliceFrames(source.sampleRate());
    const auto i = static_cast<size_t>(markerIndex);
    const auto previous = i == 0 ? juce::int64{0} : before.markers[i - 1];
    const auto next = i + 1 == before.markers.size() ? source.frames() : before.markers[i + 1];
    if (frame < previous + minFrames || frame > next - minFrames)
        return; // refused: outside what the neighbours allow
    auto after = before;
    after.markers[i] = frame;
    after.mode = model::SliceMode::manual;
    applied = writeIfChanged(context, source, before, after);
}

juce::String SetSliceParam::name() const
{
    switch (param)
    {
        case Param::pitch: return "Set slice pitch";
        case Param::gain: return "Set slice gain";
        case Param::reverse: return "Reverse slice";
        case Param::playMode: return "Set play mode";
    }
    return "Set slice";
}

juce::String SetSliceParam::coalesceKey() const
{
    return "slice:" + id + ":" + juce::String(slice) + ":" + juce::String(static_cast<int>(param));
}

void SetSliceParam::perform(ModelContext& context)
{
    auto source = findSource(context, id);
    if (! source.isValid())
        return;
    auto data = source.sliceSet();
    if (slice < 0 || slice >= data.numSlices())
        return;
    auto settings = data.slices[static_cast<size_t>(slice)];
    switch (param)
    {
        case Param::pitch: settings.pitch = std::clamp(std::round(value * 100.0) / 100.0, model::minSlicePitch, model::maxSlicePitch); break;
        case Param::gain:
        {
            const auto rounded = std::round(value * 100.0) / 100.0;
            settings.gainDb = rounded <= model::silenceGainDb ? model::silenceGainDb : std::min(rounded, model::maxSliceGainDb);
            break;
        }
        case Param::reverse: settings.reverse = value >= 0.5; break;
        case Param::playMode:
            settings.playMode = static_cast<model::PlayMode>(std::clamp(static_cast<int>(std::lround(value)), 0, 2));
            break;
    }
    if (settings == data.slices[static_cast<size_t>(slice)])
        return;

    auto node = source.node();
    auto existing = node.getChildWithName(model::sliceIds::SLICESET);
    juce::ValueTree child;
    if (existing.isValid())
    {
        int seen = 0;
        for (const auto& candidate : existing)
            if (candidate.hasType(model::sliceIds::SLICE) && seen++ == slice)
            {
                child = candidate;
                break;
            }
    }
    if (! child.isValid())
    {
        // The first thing done to a sound that was never chopped: it gets its (single-slice) set.
        data.slices[static_cast<size_t>(slice)] = settings;
        source.setSliceSet(data, &context.undo);
        return;
    }
    // In place: one property of one SLICE node, so repeated changes join into one small undo record.
    child.setProperty(model::sliceIds::pitch, settings.pitch, &context.undo);
    child.setProperty(model::sliceIds::gainDb, settings.gainDb, &context.undo);
    child.setProperty(model::sliceIds::reverse, settings.reverse ? 1 : 0, &context.undo);
    child.setProperty(model::sliceIds::playMode, model::toString(settings.playMode), &context.undo);
}
} // namespace sampler::commands
