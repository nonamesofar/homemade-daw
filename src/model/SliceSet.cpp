#include "model/SliceSet.h"

#include <algorithm>
#include <cmath>

namespace sampler::model
{
juce::String toString(SliceMode mode)
{
    switch (mode)
    {
        case SliceMode::beat: return "beat";
        case SliceMode::transient: return "transient";
        case SliceMode::manual: return "manual";
    }
    return "manual";
}

SliceMode sliceModeFromString(const juce::String& text, SliceMode fallback)
{
    if (text == "beat")
        return SliceMode::beat;
    if (text == "transient")
        return SliceMode::transient;
    if (text == "manual")
        return SliceMode::manual;
    return fallback;
}

juce::String toString(SliceDivision division)
{
    switch (division)
    {
        case SliceDivision::quarter: return "1/4";
        case SliceDivision::eighth: return "1/8";
        case SliceDivision::sixteenth: return "1/16";
        case SliceDivision::thirtySecond: return "1/32";
    }
    return "1/4";
}

SliceDivision sliceDivisionFromString(const juce::String& text, SliceDivision fallback)
{
    if (text == "1/4")
        return SliceDivision::quarter;
    if (text == "1/8")
        return SliceDivision::eighth;
    if (text == "1/16")
        return SliceDivision::sixteenth;
    if (text == "1/32")
        return SliceDivision::thirtySecond;
    return fallback;
}

double slicesPerBeat(SliceDivision division)
{
    switch (division)
    {
        case SliceDivision::quarter: return 1.0;
        case SliceDivision::eighth: return 2.0;
        case SliceDivision::sixteenth: return 4.0;
        case SliceDivision::thirtySecond: return 8.0;
    }
    return 1.0;
}

juce::String toString(PlayMode mode)
{
    switch (mode)
    {
        case PlayMode::oneShot: return "oneshot";
        case PlayMode::gate: return "gate";
        case PlayMode::loop: return "loop";
    }
    return "oneshot";
}

PlayMode playModeFromString(const juce::String& text, PlayMode fallback)
{
    if (text == "oneshot")
        return PlayMode::oneShot;
    if (text == "gate")
        return PlayMode::gate;
    if (text == "loop")
        return PlayMode::loop;
    return fallback;
}

juce::int64 minSliceFrames(double sampleRate)
{
    return std::max<juce::int64>(1, static_cast<juce::int64>(std::ceil(sampleRate * minSliceSeconds)));
}

void SliceSetData::normalise()
{
    slices.resize(static_cast<size_t>(numSlices()));
}

//==============================================================================
bool hasSliceSet(const juce::ValueTree& sourceNode) { return sourceNode.getChildWithName(sliceIds::SLICESET).isValid(); }

juce::ValueTree createSliceSetNode(const SliceSetData& data)
{
    juce::ValueTree node(sliceIds::SLICESET);
    node.setProperty(sliceIds::mode, toString(data.mode), nullptr);
    node.setProperty(sliceIds::division, toString(data.division), nullptr);
    node.setProperty(sliceIds::sensitivity, data.sensitivity, nullptr);
    juce::StringArray points;
    for (const auto marker : data.markers)
        points.add(juce::String(marker));
    node.setProperty(sliceIds::markers, points.joinIntoString(" "), nullptr);
    auto slices = data.slices;
    slices.resize(static_cast<size_t>(data.numSlices()));
    for (const auto& s : slices)
    {
        juce::ValueTree slice(sliceIds::SLICE);
        slice.setProperty(sliceIds::pitch, s.pitch, nullptr);
        slice.setProperty(sliceIds::gainDb, s.gainDb, nullptr);
        slice.setProperty(sliceIds::reverse, s.reverse ? 1 : 0, nullptr);
        slice.setProperty(sliceIds::playMode, toString(s.playMode), nullptr);
        node.appendChild(slice, nullptr);
    }
    return node;
}

SliceSetData readSliceSet(const juce::ValueTree& sourceNode, bool sanitise)
{
    SliceSetData data;
    const auto node = sourceNode.getChildWithName(sliceIds::SLICESET);
    if (! node.isValid())
        return data;
    data.mode = sliceModeFromString(node[sliceIds::mode].toString());
    data.division = sliceDivisionFromString(node[sliceIds::division].toString());
    data.sensitivity = node.hasProperty(sliceIds::sensitivity) ? static_cast<double>(node[sliceIds::sensitivity]) : defaultSensitivity;
    for (const auto& token : juce::StringArray::fromTokens(node[sliceIds::markers].toString(), " ", ""))
        if (token.isNotEmpty())
            data.markers.push_back(token.getLargeIntValue());
    data.slices.clear();
    for (const auto& child : node)
        if (child.hasType(sliceIds::SLICE))
        {
            SliceSettings s;
            s.pitch = child[sliceIds::pitch];
            s.gainDb = child.hasProperty(sliceIds::gainDb) ? static_cast<double>(child[sliceIds::gainDb]) : 0.0;
            s.reverse = static_cast<int>(child[sliceIds::reverse]) != 0;
            s.playMode = playModeFromString(child[sliceIds::playMode].toString());
            data.slices.push_back(s);
        }
    if (sanitise)
    {
        std::sort(data.markers.begin(), data.markers.end());
        data.markers.erase(std::unique(data.markers.begin(), data.markers.end()), data.markers.end());
        data.markers.erase(data.markers.begin(), std::upper_bound(data.markers.begin(), data.markers.end(), juce::int64{0}));
        if (static_cast<int>(data.markers.size()) > maxSliceCount - 1)
            data.markers.resize(static_cast<size_t>(maxSliceCount - 1));
    }
    data.normalise();
    return data;
}

void dropMarkersPastEnd(SliceSetData& data, juce::int64 frames)
{
    if (frames <= 0)
        return;
    data.markers.erase(std::lower_bound(data.markers.begin(), data.markers.end(), frames), data.markers.end());
    data.normalise();
}

void writeSliceSet(juce::ValueTree& sourceNode, const SliceSetData& data, juce::UndoManager* undo)
{
    auto old = sourceNode.getChildWithName(sliceIds::SLICESET);
    if (! old.isValid())
    {
        sourceNode.appendChild(createSliceSetNode(data), undo);
        return;
    }
    // Updated in place, so an observer never sees the slice set missing between a remove and an add.
    const auto fresh = createSliceSetNode(data);
    for (const auto& id : {sliceIds::mode, sliceIds::division, sliceIds::sensitivity, sliceIds::markers})
        old.setProperty(id, fresh[id], undo);
    int existing = 0;
    for (const auto& child : old)
        existing += child.hasType(sliceIds::SLICE) ? 1 : 0;
    int index = 0;
    for (const auto& slice : fresh)
    {
        if (index < existing)
        {
            int seen = 0;
            for (auto child : old)
                if (child.hasType(sliceIds::SLICE) && seen++ == index)
                {
                    for (const auto& id : {sliceIds::pitch, sliceIds::gainDb, sliceIds::reverse, sliceIds::playMode})
                        child.setProperty(id, slice[id], undo);
                    break;
                }
        }
        else
            old.appendChild(slice.createCopy(), undo);
        ++index;
    }
    for (int i = old.getNumChildren() - 1, kept = fresh.getNumChildren(); i >= 0 && existing > kept; --i)
        if (old.getChild(i).hasType(sliceIds::SLICE))
        {
            old.removeChild(i, undo);
            --existing;
        }
}

//==============================================================================
std::optional<juce::String> findSliceSetViolation(const SliceSetData& data, juce::int64 frames, double sampleRate)
{
    if (data.numSlices() > maxSliceCount)
        return "more than " + juce::String(maxSliceCount) + " slices";
    if (static_cast<int>(data.slices.size()) != data.numSlices())
        return juce::String("a slice has no settings, or one has too many");
    const auto minFrames = minSliceFrames(sampleRate);
    juce::int64 previous = 0;
    for (const auto marker : data.markers)
    {
        if (marker <= previous)
            return juce::String("the slice points are not increasing");
        if (marker - previous < minFrames)
            return juce::String("a slice is shorter than 5 ms");
        previous = marker;
    }
    if (frames > 0 && ! data.markers.empty() && frames - previous < minFrames)
        return juce::String("the last slice is shorter than 5 ms");
    for (const auto& s : data.slices)
    {
        if (s.pitch < minSlicePitch - 1.0e-9 || s.pitch > maxSlicePitch + 1.0e-9)
            return juce::String("a slice pitch is outside -24 to +24 semitones");
        if (s.gainDb < silenceGainDb - 1.0e-9 || s.gainDb > maxSliceGainDb + 1.0e-9)
            return juce::String("a slice gain is outside silence to +24 dB");
    }
    return std::nullopt;
}

bool insertSliceMarker(SliceSetData& data, juce::int64 frame, juce::int64 totalFrames, double sampleRate)
{
    if (frame <= 0 || frame >= totalFrames || data.numSlices() >= maxSliceCount)
        return false;
    const auto it = std::lower_bound(data.markers.begin(), data.markers.end(), frame);
    if (it != data.markers.end() && *it == frame)
        return false;
    const auto index = static_cast<size_t>(it - data.markers.begin());
    const auto left = index == 0 ? juce::int64{0} : data.markers[index - 1];
    const auto right = index == data.markers.size() ? totalFrames : data.markers[index];
    const auto minFrames = minSliceFrames(sampleRate);
    if (frame - left < minFrames || right - frame < minFrames)
        return false;
    data.normalise();
    data.markers.insert(it, frame);
    data.slices.insert(data.slices.begin() + static_cast<std::ptrdiff_t>(index), data.slices[index]); // both halves keep the settings
    return true;
}

void removeSliceMarker(SliceSetData& data, int index)
{
    if (index < 0 || index >= static_cast<int>(data.markers.size()))
        return;
    data.normalise();
    data.markers.erase(data.markers.begin() + index);
    data.slices.erase(data.slices.begin() + index + 1); // the right slice merges into the left one, which keeps its settings
}
} // namespace sampler::model
