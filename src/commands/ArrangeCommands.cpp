#include "ArrangeCommands.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sampler::commands
{
namespace
{
engine::Arrangement& arrangementOf(ModelContext& context)
{
    jassert(context.arrangement != nullptr); // arrangement commands need the engine-backed context
    return *context.arrangement;
}

/** The clip spec of a sound of the project (empty source id if it is not there). */
engine::ClipSpec specForSource(const ModelContext& context, const juce::String& sourceId)
{
    engine::ClipSpec spec;
    const auto source = model::SamplerState(context.sampler).findSourceById(sourceId);
    if (! source.isValid())
        return spec;
    spec.sourceId = source.id();
    spec.name = source.name();
    spec.file = context.bundleDir.getChildFile(source.decoded());
    spec.lengthSeconds = source.lengthSeconds();
    return spec;
}

/** Performs the plan's primitive edits; returns the ids of created clips by their tag (pieces have none). */
std::map<int, juce::String> execute(ModelContext& context, const Plan& plan, const std::vector<engine::ClipSpec>& specs = {})
{
    auto& a = arrangementOf(context);
    const auto time = timeMapOf(a);
    const auto tracks = a.tracks();
    std::map<int, juce::String> tagged;
    const auto trackId = [&tracks](int index) { return tracks[static_cast<size_t>(juce::jlimit(0, static_cast<int>(tracks.size()) - 1, index))].id; };

    // The clips that pieces are cut from, as they are before the first piece is made (their fades change meanwhile).
    std::map<juce::String, engine::ClipInfo> originals;
    for (const auto& s : plan.created)
        if (s.cutPiece && s.copyOf.isNotEmpty() && ! originals.count(s.copyOf))
            if (const auto info = a.clip(s.copyOf))
                originals.emplace(s.copyOf, *info);

    // New clips first: a piece may copy a clip that this plan shortens or removes.
    for (const auto& s : plan.created)
    {
        const auto seconds = time.secondsBetween(s.start, s.end());
        juce::String id;
        if (s.copyOf.isNotEmpty())
        {
            const auto found = s.cutPiece ? originals.find(s.copyOf) : originals.end();
            id = a.copyClip(s.copyOf, trackId(s.track), s.start, seconds, s.offset);
            if (found != originals.end() && id.isNotEmpty())
            {
                // Like a split: the fade in stays on the left part and the fade out moves to the piece that holds the end.
                const auto& original = found->second;
                const bool holdsEnd = std::abs(s.end() - original.end()) < EditPlanner::epsilon;
                a.setClipFades(s.copyOf, original.fadeIn, 0.0);
                a.setClipFades(id, 0.0, holdsEnd ? original.fadeOut : 0.0);
            }
        }
        else if (juce::isPositiveAndBelow(s.specIndex, static_cast<int>(specs.size())))
        {
            auto spec = specs[static_cast<size_t>(s.specIndex)];
            spec.start = s.start;
            spec.lengthSeconds = seconds;
            spec.lengthBeats = spec.warp.active() ? s.length : 0.0; // a warped clip is placed in bars
            spec.offsetSeconds = s.offset;
            id = a.createClip(trackId(s.track), spec);
        }
        if (s.tag >= 0 && id.isNotEmpty())
            tagged[s.tag] = id;
    }
    for (const auto& s : plan.changed)
        a.setClipBounds(s.id, trackId(s.track), s.start, time.secondsBetween(s.start, s.end()), s.offset);
    for (const auto& id : plan.removed)
        a.removeClip(id);
    return tagged;
}

std::optional<Span> findSpan(const std::vector<Span>& spans, const juce::String& id)
{
    for (const auto& s : spans)
        if (s.id == id)
            return s;
    return std::nullopt;
}

std::vector<juce::String> values(const std::map<int, juce::String>& tagged)
{
    std::vector<juce::String> ids;
    for (const auto& [tag, id] : tagged)
        ids.push_back(id);
    return ids;
}

/** Clip starts (beats) and lengths (seconds) and the loop range, to put back after a tempo or time signature change. */
struct Snapshot
{
    std::vector<engine::ClipInfo> clips;
    engine::LoopRange loop;

    explicit Snapshot(const engine::Arrangement& a) : clips(a.clips()), loop(a.loopRange()) {}
    Snapshot(std::vector<engine::ClipInfo> c, engine::LoopRange l) : clips(std::move(c)), loop(l) {}

    void restore(engine::Arrangement& a) const
    {
        for (const auto& before : clips)
            if (before.warp.active())
                continue; // a warped clip keeps its start and its bars by itself (Arrangement::setTempo)
            else if (const auto now = a.clip(before.id))
                if (std::abs(now->start - before.start) > EditPlanner::epsilon
                    || std::abs(now->lengthSeconds - before.lengthSeconds) > 1.0e-6)
                    a.setClipBounds(before.id, before.trackId, before.start, before.lengthSeconds, before.offsetSeconds);
        if (! loop.isEmpty())
        {
            const auto now = a.loopRange();
            if (std::abs(now.start - loop.start) > EditPlanner::epsilon || std::abs(now.end - loop.end) > EditPlanner::epsilon)
                a.setLoopRange(loop);
        }
    }
};
} // namespace

//==============================================================================
Span spanFor(const engine::ClipInfo& clip, int trackIndex)
{
    Span s;
    s.id = clip.id;
    s.track = trackIndex;
    s.start = clip.start;
    s.length = clip.length;
    s.offset = clip.offsetSeconds;
    s.warpBpm = clip.warp.active() ? clip.warp.sourceBpm : 0.0;
    if (clip.clampedSpeed > 0.0 && clip.length > 0.0)
        s.warpBpm = clip.length * 60.0 / clip.coveredSeconds(); // Tracktion plays a clamped speed: the beats say how much sound plays at that speed
    if (clip.loop)
    {
        s.loopStart = clip.loopStart;
        s.loopLength = clip.loopLength;
    }
    return s;
}

std::vector<Span> spansOf(const engine::Arrangement& a)
{
    std::map<juce::String, int> indexOf;
    const auto tracks = a.tracks();
    for (size_t i = 0; i < tracks.size(); ++i)
        indexOf[tracks[i].id] = static_cast<int>(i);
    std::vector<Span> spans;
    for (const auto& clip : a.clips())
        spans.push_back(spanFor(clip, indexOf[clip.trackId]));
    return spans;
}

TimeMap timeMapOf(const engine::Arrangement& a)
{
    return {[&a](double beats) { return a.beatsToSeconds(beats); }, [&a](double seconds) { return a.secondsToBeats(seconds); }};
}

juce::String nextTrackName(const std::vector<engine::TrackInfo>& tracks)
{
    std::set<int> used;
    for (const auto& t : tracks)
        if (t.name.startsWith("Audio ") && t.name.substring(6).containsOnly("0123456789") && t.name.length() > 6)
            used.insert(t.name.substring(6).getIntValue());
    int n = 1;
    while (used.count(n))
        ++n;
    return "Audio " + juce::String(n);
}

juce::Colour nextTrackColour(const std::vector<engine::TrackInfo>& tracks)
{
    for (int i = 0; i < engine::trackPaletteSize; ++i)
    {
        const auto colour = engine::trackPaletteColour(i);
        if (std::none_of(tracks.begin(), tracks.end(), [colour](const engine::TrackInfo& t) { return t.colour == colour; }))
            return colour;
    }
    return engine::trackPaletteColour(static_cast<int>(tracks.size()));
}

juce::String clipCountName(const juce::String& verb, size_t count)
{
    return count == 1 ? verb + " clip" : verb + " " + juce::String(static_cast<int>(count)) + " clips";
}

//==============================================================================
std::vector<juce::String> CreateClipsFromSources::place(ModelContext& context, const std::vector<juce::String>& sourceIds,
                                                        PlacementTarget target, const std::vector<engine::WarpSettings>& warps)
{
    auto& a = arrangementOf(context);
    std::vector<engine::ClipSpec> specs;
    for (size_t i = 0; i < sourceIds.size(); ++i)
        if (auto spec = specForSource(context, sourceIds[i]); spec.sourceId.isNotEmpty() && spec.lengthSeconds > 0.0)
        {
            if (i < warps.size() && warps[i].active())
                spec.warp = warps[i];
            specs.push_back(spec);
        }
    if (specs.empty())
        return {};

    int track = target.trackIndex;
    if (target.trackId.isNotEmpty() && ! target.newTrack)
    {
        track = a.trackIndex(target.trackId);
        target.newTrack = track < 0; // the track is gone: the clips get a new one
    }
    if (target.newTrack || a.numTracks() == 0)
    {
        const auto tracks = a.tracks();
        a.addTrack(static_cast<int>(tracks.size()), nextTrackName(tracks), nextTrackColour(tracks));
        track = a.numTracks() - 1;
    }
    track = juce::jlimit(0, a.numTracks() - 1, track);

    const auto time = timeMapOf(a);
    std::vector<Span> placed;
    auto beat = std::max(0.0, target.beat);
    for (size_t i = 0; i < specs.size(); ++i)
    {
        Span s;
        s.specIndex = static_cast<int>(i);
        s.tag = static_cast<int>(i);
        s.track = track;
        s.start = beat;
        s.warpBpm = specs[i].warp.active() ? specs[i].warp.sourceBpm : 0.0;
        s.length = beatAfterSoundSeconds(s, beat, specs[i].lengthSeconds, time) - beat;
        placed.push_back(s);
        beat = s.end();
    }
    return values(execute(context, EditPlanner::place(spansOf(a), placed, time), specs));
}

void CreateClipsFromSources::perform(ModelContext& context) { results = place(context, sources, where, warps); }

void SlicesToTrack::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto source = model::SamplerState(context.sampler).findSourceById(id);
    if (! source.isValid() || source.frames() <= 0 || source.sampleRate() <= 0.0)
        return;
    const auto data = source.sliceSet();
    const auto tracks = a.tracks();
    newTrackId = a.addTrack(static_cast<int>(tracks.size()), source.name(), nextTrackColour(tracks));
    if (newTrackId.isEmpty())
        return;

    const auto time = timeMapOf(a);
    const double rate = source.sampleRate();
    const double soundSeconds = source.lengthSeconds();
    auto beat = std::max(0.0, startBeat);
    juce::int64 start = 0;
    for (int i = 0; i < data.numSlices(); ++i)
    {
        const auto end = i < static_cast<int>(data.markers.size()) ? data.markers[static_cast<size_t>(i)] : source.frames();
        const auto& settings = data.slices[static_cast<size_t>(i)];
        if (end <= start)
            continue; // a slice without length (a damaged slice set) has nothing to play
        const double lengthSeconds = static_cast<double>(end - start) / rate;

        engine::ClipSpec spec;
        spec.sourceId = source.id();
        spec.name = source.name() + " " + juce::String(i + 1);
        spec.file = context.bundleDir.getChildFile(source.decoded());
        spec.start = beat;
        spec.lengthSeconds = lengthSeconds;
        // A reversed clip plays the reversed file, in which the slice [s, e) sits at [length - e, length - s).
        spec.offsetSeconds = settings.reverse ? soundSeconds - static_cast<double>(end) / rate : static_cast<double>(start) / rate;
        spec.gainDb = settings.gainDb;
        spec.reversed = settings.reverse;
        spec.originSourceId = source.id();
        spec.originSlice = i;
        spec.pitch = settings.pitch;
        double lengthBeats = 0.0;
        if (warp.active())
        {
            spec.warp = warp;
            spec.warp.transpose = juce::jlimit(-engine::WarpSettings::maxTranspose, engine::WarpSettings::maxTranspose, settings.pitch);
            lengthBeats = lengthSeconds * warp.sourceBpm / 60.0;
            spec.lengthBeats = lengthBeats;
        }
        if (const auto clip = a.createClip(newTrackId, spec); clip.isNotEmpty())
            results.push_back(clip);

        beat = warp.active() ? beat + lengthBeats : time.beatAfter(beat, lengthSeconds);
        start = end;
    }
}

juce::String ImportAndPlace::name() const
{
    if (items.empty())
        return clipCountName("Add", toPlace.size());
    if (items.size() == 1)
    {
        const auto& origin = items.front().originalPath;
        const auto fileName = juce::File::isAbsolutePath(origin) ? juce::File(origin).getFileName() : juce::String();
        return "Import " + (fileName.isNotEmpty() ? fileName : items.front().name);
    }
    return "Import " + juce::String(static_cast<int>(items.size())) + " files";
}

void ImportAndPlace::perform(ModelContext& context)
{
    model::SamplerState state(context.sampler);
    for (const auto& item : items)
        state.addSource(model::Source::create(item), &context.undo);
    results = CreateClipsFromSources::place(context, toPlace, where, warps);
}

void MoveClips::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto spans = spansOf(a);
    std::vector<Span> placed;
    for (size_t i = 0; i < moves.size(); ++i)
        if (auto s = findSpan(spans, moves[i].clipId))
        {
            s->track = juce::jlimit(0, a.numTracks() - 1, moves[i].trackIndex);
            s->start = std::max(0.0, moves[i].start);
            s->tag = static_cast<int>(i);
            placed.push_back(*s);
            results.push_back(s->id);
        }
    execute(context, EditPlanner::place(spans, placed, timeMapOf(a)));
}

void TrimClip::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto spans = spansOf(a);
    const auto span = findSpan(spans, clipId);
    const auto info = a.clip(clipId);
    if (! span || ! info)
        return;
    const auto time = timeMapOf(a);
    const auto shortest = std::max(minLength, time.beatAfter(span->start, engine::Arrangement::minClipSeconds) - span->start);
    auto trimmed = EditPlanner::withStart(*span, EditPlanner::clampTrimStart(*span, start, shortest, time), time);
    trimmed.length = EditPlanner::clampTrimEnd(trimmed, end, info->sourceSeconds, shortest, time) - trimmed.start;
    trimmed.tag = 0;
    execute(context, EditPlanner::place(spans, {trimmed}, time));
    results = {clipId};
}

void SplitClips::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto plan = EditPlanner::split(spansOf(a), clipIds, beat, timeMapOf(a));
    auto created = plan.created;
    for (size_t i = 0; i < created.size(); ++i)
        created[i].tag = static_cast<int>(i);
    Plan tagged = plan;
    tagged.created = created;
    std::map<juce::String, engine::ClipInfo> before; // the fades of the clips that are split
    for (const auto& s : created)
        if (const auto info = a.clip(s.copyOf))
            before.emplace(s.copyOf, *info);
    for (const auto& s : plan.changed)
        results.push_back(s.id);
    const auto tags = execute(context, tagged);
    for (const auto& id : values(tags))
        results.push_back(id);

    // The fade in stays on the first part and the fade out on the second (the cut itself keeps the short declick).
    for (size_t i = 0; i < created.size(); ++i)
    {
        const auto found = before.find(created[i].copyOf);
        const auto right = tags.find(static_cast<int>(i));
        if (found == before.end() || right == tags.end())
            continue;
        a.setClipFades(created[i].copyOf, found->second.fadeIn, 0.0);
        a.setClipFades(right->second, 0.0, found->second.fadeOut);
    }
}

void DuplicateClips::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto spans = spansOf(a);
    std::vector<Span> chosen;
    for (const auto& id : clipIds)
        if (auto s = findSpan(spans, id))
            chosen.push_back(*s);
    if (chosen.empty())
        return;
    double first = chosen.front().start, last = chosen.front().end();
    for (const auto& s : chosen)
    {
        first = std::min(first, s.start);
        last = std::max(last, s.end());
    }
    std::vector<Span> placed;
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        Span copy = chosen[i];
        copy.copyOf = copy.id;
        copy.id = {};
        copy.start += last - first;
        copy.tag = static_cast<int>(i);
        placed.push_back(copy);
    }
    results = values(execute(context, EditPlanner::place(spans, placed, timeMapOf(a))));
}

void DeleteClips::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    for (const auto& id : clipIds)
        a.removeClip(id);
}

void PasteClips::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (a.numTracks() == 0)
        return;
    const auto time = timeMapOf(a);
    std::vector<engine::ClipSpec> specs;
    std::vector<Span> placed;
    for (const auto& record : records)
    {
        auto spec = specForSource(context, record.sourceId);
        if (spec.sourceId.isEmpty())
            continue; // the sound is gone from the project
        spec.name = record.name;
        spec.gainDb = record.gainDb;
        spec.reversed = record.reversed;
        if (record.warp.active() || record.warp.sourceBpm > 0.0 || record.warp.transpose != 0.0) // keeps the remembered tempo when WARP is off
            spec.warp = record.warp;
        spec.loopStart = record.loopStart;
        spec.loopLength = record.loopLength;
        spec.fadeIn = record.fadeIn;
        spec.fadeOut = record.fadeOut;
        Span s;
        s.specIndex = static_cast<int>(specs.size());
        s.tag = s.specIndex;
        s.track = juce::jlimit(0, a.numTracks() - 1, track + record.trackOffset);
        s.start = std::max(0.0, beat + record.startOffset);
        s.warpBpm = spec.warp.active() ? spec.warp.sourceBpm : 0.0;
        s.length = spec.warp.active() && record.lengthBeats > 0.0
                       ? record.lengthBeats
                       : time.beatAfter(s.start, record.lengthSeconds) - s.start; // lengthSeconds is timeline time
        s.offset = record.offsetSeconds;
        s.loopStart = record.loopStart;
        s.loopLength = record.loopLength;
        specs.push_back(spec);
        placed.push_back(s);
    }
    results = values(execute(context, EditPlanner::place(spansOf(a), placed, time), specs));
}

void SetClipGain::perform(ModelContext& context) { arrangementOf(context).setClipGainDb(clipId, gainDb); }

void SetClipStart::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto spans = spansOf(a);
    auto span = findSpan(spans, clipId);
    if (! span)
        return;
    span->start = std::max(0.0, beat);
    span->tag = 0;
    execute(context, EditPlanner::place(spans, {*span}, timeMapOf(a)));
    results = {clipId};
}

void SetClipLoop::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (! a.clip(clipId))
        return;
    a.setClipLoop(clipId, on);
    results = {clipId};
}

void SetClipLength::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto spans = spansOf(a);
    auto span = findSpan(spans, clipId);
    const auto info = a.clip(clipId);
    if (! span || ! info)
        return;
    const auto time = timeMapOf(a);
    const auto shortest = std::max(1.0e-3, time.beatAfter(span->start, engine::Arrangement::minClipSeconds) - span->start);
    // Without loop the sound is the limit (clampTrimEnd); a looped clip may repeat its loop.
    span->length = EditPlanner::clampTrimEnd(*span, span->start + std::max(beats, 0.0), info->sourceSeconds, shortest, time) - span->start;
    span->tag = 0;
    execute(context, EditPlanner::place(spans, {*span}, time));
    results = {clipId};
}

void SetClipFades::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (! a.clip(clipId))
        return;
    a.setClipFades(clipId, std::max(0.0, fadeIn), std::max(0.0, fadeOut));
    results = {clipId};
}

void SetClipReversed::perform(ModelContext& context) { arrangementOf(context).setClipReversed(clipId, reversed); }

void SetClipWarp::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto info = a.clip(clipId);
    if (! info)
        return;
    auto settings = info->warp;
    settings.on = on;
    settings.mode = mode;
    if (sourceBpm > 0.0)
        settings.sourceBpm = juce::jlimit(engine::WarpSettings::minBpm, engine::WarpSettings::maxBpm, sourceBpm);
    if (settings.on && ! settings.active())
        return; // WARP needs a tempo
    if (settings == info->warp)
        return;

    // The clip keeps its start and the part of its sound; its length follows (bars when warped, seconds when not).
    a.setClipWarp(clipId, settings);
    const auto spans = spansOf(a);
    if (auto span = findSpan(spans, clipId))
    {
        span->tag = 0;
        execute(context, EditPlanner::place(spans, {*span}, timeMapOf(a)));
    }
    results = {clipId};
}

void SetClipTranspose::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (const auto info = a.clip(clipId))
    {
        const auto clamped = juce::jlimit(-engine::WarpSettings::maxTranspose, engine::WarpSettings::maxTranspose, std::round(st * 100.0) / 100.0);
        if (clamped != info->warp.transpose)
            a.setClipTranspose(clipId, clamped);
        results = {clipId};
    }
}

//==============================================================================
void AddTrack::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const auto tracks = a.tracks();
    const auto afterIndex = after.isEmpty() ? -1 : a.trackIndex(after);
    const auto index = afterIndex < 0 ? static_cast<int>(tracks.size()) : afterIndex + 1;
    if (const auto id = a.addTrack(index, nextTrackName(tracks), nextTrackColour(tracks)); id.isNotEmpty())
        results = {id};
}

void DeleteTracks::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    // Tracks that recorded a deleted track go back to System audio, in the same step.
    for (const auto& track : a.tracks())
        if (std::find(trackIds.begin(), trackIds.end(), track.id) == trackIds.end()
            && track.input.startsWith("track:") && std::find(trackIds.begin(), trackIds.end(), track.input.fromFirstOccurrenceOf(":", false, false)) != trackIds.end())
            a.setTrackInput(track.id, "system:default");
    for (const auto& id : trackIds)
        a.removeTrack(id);
}

void SetTrackInput::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (a.trackIndex(trackId) < 0 || input == "track:" + trackId)
        return;
    if (input.startsWith("track:") && a.trackIndex(input.fromFirstOccurrenceOf(":", false, false)) < 0)
        return;
    a.setTrackInput(trackId, input);
}

void AddTakes::perform(ModelContext& context)
{
    model::SamplerState state(context.sampler);
    auto& a = arrangementOf(context);
    std::map<juce::String, juce::String> sourceOfFile; // tracks on one device share one file and one sound
    skipped.clear();
    for (size_t index = 0; index < takes.size(); ++index)
    {
        const auto& take = takes[index];
        if (a.trackIndex(take.trackId) < 0 || take.frames <= 0 || take.sampleRate <= 0.0)
        {
            skipped.push_back(index);
            continue;
        }
        PlacementTarget target;
        target.trackId = take.trackId;
        target.beat = take.startBeat;
        if (const auto known = sourceOfFile.find(take.relativePath); known != sourceOfFile.end())
        {
            const auto ids = CreateClipsFromSources::place(context, {known->second}, target);
            if (ids.empty())
                skipped.push_back(index); // no clip came of it: the take is not added
            for (const auto& id : ids)
                results.push_back(id);
            continue;
        }
        model::SourceData data;
        data.hash = "rec:" + juce::File(take.relativePath).getFileNameWithoutExtension(); // the whole name: it is unique per take
        data.id = state.makeSourceId(data.hash);
        data.name = take.name;
        data.original = data.decoded = take.relativePath;
        data.originalPath = {};
        data.sampleRate = take.sampleRate;
        data.channels = take.channels;
        data.frames = take.frames;
        data.category = model::Category::recording;
        data.recordedFrom = take.recordedFrom;
        data.recordedAt = take.recordedAt;
        if (! state.addSource(model::Source::create(data), &context.undo))
        {
            skipped.push_back(index);
            continue;
        }
        const auto ids = CreateClipsFromSources::place(context, {data.id}, target);
        if (ids.empty())
        {
            // No clip came of it: no sound is left behind without a clip.
            if (const auto added = state.findSourceById(data.id); added.isValid())
                added.node().getParent().removeChild(added.node(), &context.undo);
            skipped.push_back(index);
            continue;
        }
        sourceOfFile[take.relativePath] = data.id;
        for (const auto& id : ids)
            results.push_back(id);
    }
}

void ReorderTrack::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (a.trackIndex(trackId) != newIndex)
        a.moveTrack(trackId, newIndex);
}

void RenameTrack::perform(ModelContext& context)
{
    const auto trimmed = text.trim();
    if (trimmed.isNotEmpty())
        arrangementOf(context).setTrackName(trackId, trimmed);
}

void SetTrackColour::perform(ModelContext& context) { arrangementOf(context).setTrackColour(trackId, colour); }

juce::String SetTrackParam::name() const
{
    switch (control)
    {
        case Control::volume: return "Set volume";
        case Control::pan: return "Set pan";
        case Control::mute: return "Mute";
        case Control::solo:
        case Control::exclusiveSolo: return "Solo";
    }
    return {};
}

juce::String SetTrackParam::coalesceKey() const
{
    if (control == Control::exclusiveSolo)
        return {};
    return (trackId.isEmpty() ? juce::String("master") : trackId) + ":" + juce::String(static_cast<int>(control));
}

void SetTrackParam::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    if (trackId.isEmpty())
    {
        if (control == Control::volume)
            a.setMasterVolumeDb(value);
        return;
    }
    switch (control)
    {
        case Control::volume: a.setTrackVolumeDb(trackId, value); break;
        case Control::pan: a.setTrackPan(trackId, value); break;
        case Control::mute: a.setTrackMute(trackId, value > 0.5); break;
        case Control::solo: a.setTrackSolo(trackId, value > 0.5); break;
        case Control::exclusiveSolo:
            for (const auto& t : a.tracks())
                a.setTrackSolo(t.id, t.id == trackId);
            break;
    }
}

void SetLoop::perform(ModelContext& context)
{
    auto r = range;
    if (r.end < r.start)
        std::swap(r.start, r.end);
    r.start = std::max(0.0, r.start);
    arrangementOf(context).setLoopRange(r);
}

void SetTempo::perform(ModelContext& context)
{
    if (! std::isfinite(bpm))
        return;
    auto& a = arrangementOf(context);
    if (! baseline)
        baseline = a.clips(); // before any change of this command; later updates of a drag restore from it
    const Snapshot before(*baseline, a.loopRange());
    a.setTempo(bpm, ! deferRenders);
    before.restore(a);
    execute(context, EditPlanner::shortenOverlaps(spansOf(a)));
}

void SetTimeSig::perform(ModelContext& context)
{
    auto& a = arrangementOf(context);
    const Snapshot before(a);
    a.setTimeSig(juce::jlimit(1, 32, numerator), denominator == 2 || denominator == 8 || denominator == 16 ? denominator : 4);
    before.restore(a);
    execute(context, EditPlanner::shortenOverlaps(spansOf(a)));
}
} // namespace sampler::commands
