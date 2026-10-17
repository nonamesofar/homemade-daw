#include "Arrangement.h"

#include "EngineSetup.h"
#include "model/SamplerState.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace te = tracktion::engine;

namespace sampler::engine
{
namespace
{
const juce::Identifier sourceIdProperty("sampler_sourceId");
const juce::Identifier originSourceProperty("sampler_originSource");
const juce::Identifier originSliceProperty("sampler_originSlice");
const juce::Identifier pitchProperty("sampler_pitch");
const juce::Identifier loopOnProperty("sampler_loopOn");
const juce::Identifier inputProperty("sampler_input");
// Per-clip warp, loop and fade properties (design D1). A missing sampler_warp means WARP off.
const juce::Identifier warpProperty("sampler_warp");
const juce::Identifier warpModeProperty("sampler_warpMode");
const juce::Identifier sourceBpmProperty("sampler_sourceBpm");
const juce::Identifier transposeProperty("sampler_transpose");
const juce::Identifier loopProperty("sampler_loop");
const juce::Identifier loopStartProperty("sampler_loopStart");
const juce::Identifier loopLengthProperty("sampler_loopLength");
const juce::Identifier fadeInProperty("sampler_fadeIn");
const juce::Identifier fadeOutProperty("sampler_fadeOut");
/** A Beats clip plays a render of the reversed sound, so Tracktion's own reverse flag stays off and this one says "reversed". */
const juce::Identifier reversedProperty("sampler_reversed");

constexpr const char* beatsFolder = "cache/beats/";

/** Changes that are not edits: the playhead and Tracktion's own time stamp. */
bool isVolatile(const juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType(te::IDs::TRANSPORT))
        return property != te::IDs::loopPoint1 && property != te::IDs::loopPoint2 && property != te::IDs::looping
               && property != loopOnProperty;
    return property == te::IDs::lastSignificantChange;
}
} // namespace

juce::String toString(WarpMode mode)
{
    switch (mode)
    {
        case WarpMode::beats: return "beats";
        case WarpMode::tones: return "tones";
        case WarpMode::repitch: return "repitch";
    }
    return "beats";
}

WarpMode warpModeFromString(const juce::String& text, WarpMode fallback)
{
    if (text == "beats")
        return WarpMode::beats;
    if (text == "tones")
        return WarpMode::tones;
    if (text == "repitch")
        return WarpMode::repitch;
    return fallback;
}

juce::Colour trackPaletteColour(int index)
{
    static const juce::uint32 colours[trackPaletteSize] = {0xffff8f5a, 0xff4fd1c5, 0xffb9a2ff, 0xfff0d65f, 0xff6aa8ff, 0xffff6b9a,
                                                           0xff8fd16a, 0xffffb84d, 0xff5ad1ff, 0xffe58fff, 0xffc8c8a0, 0xffff7070};
    return juce::Colour(colours[((index % trackPaletteSize) + trackPaletteSize) % trackPaletteSize]);
}

struct Arrangement::Impl : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
    Impl(Arrangement& o, te::Edit& e) : owner(o), edit(e), state(e.state), inhibitor(e)
    {
        state.addListener(this);
        // Only CommandService opens undo steps (gestures, coalescing): Tracktion's own 350 ms timer is held off by the
        // inhibitor. The history is effectively unbounded, so the history token never loses a step.
        edit.getUndoManager().setMaxNumberOfStoredUnits(100000000, 1000);
        lastTrackOrder = trackOrder();
    }
    ~Impl() override
    {
        state.removeListener(this);
        cancelPendingUpdate();
        stopTimer();
    }

    /** Several renders finishing together rebuild the playback graph once (a few ms later), not once each. */
    void scheduleRestartPlayback()
    {
        if (! isTimerRunning())
            startTimer(40);
    }

    void timerCallback() override
    {
        stopTimer();
        // The playback graph of a clip whose file was missing is built again. Only while the transport plays: otherwise the
        // graph is made from the clips' state when playback starts, and an offline render makes its own (restarting playback
        // from inside a render, which pumps messages, hangs it).
        if (edit.getTransport().isPlaying() && ! edit.isRendering())
            edit.restartPlayback();
        else if (edit.isRendering())
            startTimer(100);
    }

    juce::UndoManager* um() { return &edit.getUndoManager(); }

    juce::Array<te::AudioTrack*> audioTracks() const { return te::getAudioTracks(edit); }

    te::AudioTrack* findTrack(const juce::String& id) const
    {
        for (auto* t : audioTracks())
            if (t->itemID.toString() == id)
                return t;
        return nullptr;
    }

    te::WaveAudioClip* findClip(const juce::String& id) const
    {
        for (auto* t : audioTracks())
            for (auto* c : t->getClips())
                if (c->itemID.toString() == id)
                    return dynamic_cast<te::WaveAudioClip*>(c);
        return nullptr;
    }

    double toBeats(tracktion::TimePosition t) const { return edit.tempoSequence.toBeats(t).inBeats(); }
    tracktion::TimePosition toTime(double beats) const { return edit.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)); }

    TrackInfo infoFor(te::AudioTrack& t) const
    {
        TrackInfo info;
        info.id = t.itemID.toString();
        info.name = t.getName();
        info.colour = t.getColour();
        if (auto* vp = t.getVolumePlugin())
        {
            // The saved values: right after an undo the plugin's parameters only catch up in the next message.
            info.volumeDb = te::volumeFaderPositionToDB(vp->volume.get());
            info.pan = vp->pan.get();
        }
        info.mute = t.state[te::IDs::mute];
        info.solo = t.state[te::IDs::solo];
        if (const auto input = t.state[inputProperty].toString(); input.isNotEmpty())
            info.input = input;
        return info;
    }

    double tempoBpm() const { return edit.tempoSequence.getTempo(0)->getBpm(); }

    /** The project folder: where the edit file is (Save As moves it, so this is asked each time). */
    juce::File bundleDir() const
    {
        const auto editFile = edit.editFileRetriever ? edit.editFileRetriever() : juce::File();
        return editFile.getParentDirectory();
    }

    model::Source sourceById(const juce::String& id) const
    {
        return model::SamplerState(state.getChildWithName(model::ids::SAMPLER)).findSourceById(id);
    }

    //==============================================================================
    // Warp (design D2): the clip's properties plus the tempo decide what Tracktion is told.

    WarpSettings warpOf(te::WaveAudioClip& c) const
    {
        WarpSettings w;
        w.on = static_cast<int>(c.state[warpProperty]) != 0;
        w.mode = warpModeFromString(c.state[warpModeProperty].toString());
        w.sourceBpm = c.state.hasProperty(sourceBpmProperty) ? static_cast<double>(c.state[sourceBpmProperty]) : 0.0;
        // An M3 slice clip keeps the pitch of its slice in sampler_pitch: it is the transpose until the user sets one.
        w.transpose = c.state.hasProperty(transposeProperty) ? static_cast<double>(c.state[transposeProperty])
                      : c.state.hasProperty(pitchProperty)   ? static_cast<double>(c.state[pitchProperty])
                                                             : 0.0;
        return w;
    }

    bool reversedOf(te::WaveAudioClip& c) const { return c.getIsReversed() || static_cast<int>(c.state[reversedProperty]) != 0; }

    static bool isBeats(const WarpSettings& w) { return w.active() && w.mode == WarpMode::beats; }

    void writeWarp(te::WaveAudioClip& c, const WarpSettings& w)
    {
        if (w.on || c.state.hasProperty(warpProperty))
            c.state.setProperty(warpProperty, w.on ? 1 : 0, um());
        if (w.on || w.mode != WarpMode::beats || c.state.hasProperty(warpModeProperty))
            c.state.setProperty(warpModeProperty, toString(w.mode), um());
        if (w.sourceBpm > 0.0)
            c.state.setProperty(sourceBpmProperty, w.sourceBpm, um());
        if (w.transpose != 0.0 || c.state.hasProperty(transposeProperty))
            c.state.setProperty(transposeProperty, w.transpose, um());
    }

    render::BeatsParams beatsParamsOf(te::WaveAudioClip& c, const WarpSettings& w) const
    {
        render::BeatsParams p;
        p.sourceBpm = w.sourceBpm;
        p.projectBpm = tempoBpm();
        p.reverse = reversedOf(c);
        p.transposeSemitones = static_cast<float>(w.transpose);
        return p;
    }

    juce::File beatsFileFor(const juce::String& key) const { return bundleDir().getChildFile(juce::String(beatsFolder) + key + ".wav"); }

    static bool pointsAtBeatsFile(te::WaveAudioClip& c)
    {
        return c.getSourceFileReference().source.get().replaceCharacter('\\', '/').contains(beatsFolder);
    }

    /** What a clip plays, independent of the tempo: where it starts, where in the sound, and how much of the sound. */
    struct Content
    {
        double startBeats = 0.0;
        double offset = 0.0;  // seconds of the sound (of the reversed sound when reversed)
        double covered = 0.0; // seconds of the sound
    };

    /** The loop of a clip (design D1, D6): the part of the sound it repeats, in seconds of the sound. */
    struct Loop
    {
        bool on = false;
        double start = 0.0;
        double length = 0.0;
    };

    Loop loopOf(te::WaveAudioClip& c) const
    {
        Loop l;
        l.start = c.state.hasProperty(loopStartProperty) ? static_cast<double>(c.state[loopStartProperty]) : 0.0;
        l.length = c.state.hasProperty(loopLengthProperty) ? static_cast<double>(c.state[loopLengthProperty]) : 0.0;
        l.on = static_cast<int>(c.state[loopProperty]) != 0 && l.length > 0.0;
        return l.on ? l : Loop{};
    }

    void writeLoop(te::WaveAudioClip& c, const Loop& l)
    {
        if (l.on)
        {
            c.state.setProperty(loopProperty, 1, um());
            c.state.setProperty(loopStartProperty, l.start, um());
            c.state.setProperty(loopLengthProperty, l.length, um());
        }
        else if (c.state.hasProperty(loopProperty))
        {
            // Switched off, not removed: an undo puts a removed property back at the end of the node, which would change the
            // saved text of the edit (the start and length are ignored while the flag is 0).
            c.state.setProperty(loopProperty, 0, um());
        }
    }

    /** Material seconds the clip's position offset stands for: timeline seconds scaled by the tempo ratio. */
    double offsetFactor(const WarpSettings& w) const { return w.active() ? tempoBpm() / w.sourceBpm : 1.0; }

    /** Reads the content from Tracktion's state; valid while that state belongs to the current tempo. */
    Content contentOf(te::WaveAudioClip& c) const
    {
        const auto pos = c.getPosition();
        const auto w = warpOf(c);
        const auto loop = loopOf(c);
        Content content;
        content.startBeats = toBeats(pos.getStart());
        // A looped clip plays from the loop's start; its position offset is how far into the loop it begins.
        content.offset = pos.getOffset().inSeconds() * offsetFactor(w) + (loop.on ? loop.start : 0.0);
        content.covered = w.active() ? (toBeats(pos.getEnd()) - content.startBeats) * 60.0 / w.sourceBpm : pos.getLength().inSeconds();
        return content;
    }

    /** Fades as they play: the user's values cut to the clip's length, then at least the 3 ms declick (design D6). */
    struct Fades
    {
        double in = 0.0;
        double out = 0.0;
    };

    Fades userFadesOf(te::WaveAudioClip& c) const
    {
        const auto length = c.getPosition().getLength().inSeconds();
        Fades f;
        f.in = c.state.hasProperty(fadeInProperty) ? std::max(0.0, static_cast<double>(c.state[fadeInProperty])) : 0.0;
        f.out = c.state.hasProperty(fadeOutProperty) ? std::max(0.0, static_cast<double>(c.state[fadeOutProperty])) : 0.0;
        f.in = std::min(f.in, length);
        f.out = std::min(f.out, std::max(0.0, length - f.in));
        return f;
    }

    void applyFades(te::WaveAudioClip& c)
    {
        constexpr double declick = 0.003;
        const auto f = userFadesOf(c);
        const auto length = c.getPosition().getLength().inSeconds();
        const auto in = tracktion::TimeDuration::fromSeconds(std::min(std::max(f.in, declick), length));
        const auto out = tracktion::TimeDuration::fromSeconds(std::min(std::max(f.out, declick), length));
        if (c.getFadeIn() != in)
            c.setFadeIn(in);
        if (c.getFadeOut() != out)
            c.setFadeOut(out);
        if (c.getFadeInType() != te::AudioFadeCurve::linear)
            c.setFadeInType(te::AudioFadeCurve::linear);
        if (c.getFadeOutType() != te::AudioFadeCurve::linear)
            c.setFadeOutType(te::AudioFadeCurve::linear);
    }

    /** The content of a clip given bounds the way the commands give them: length in timeline seconds at this tempo. */
    Content contentFor(te::WaveAudioClip& c, double startBeats, double lengthSeconds, double offsetSeconds) const
    {
        const auto w = warpOf(c);
        Content content;
        content.startBeats = startBeats;
        content.offset = offsetSeconds;
        if (! w.active())
            content.covered = lengthSeconds;
        else
        {
            const auto endBeats = toBeats(toTime(startBeats) + tracktion::TimeDuration::fromSeconds(lengthSeconds));
            content.covered = (endBeats - startBeats) * 60.0 / w.sourceBpm;
        }
        return content;
    }

    /**
        Sets everything Tracktion needs for the clip's warp settings at the current tempo (design D2): auto-tempo, stretch
        mode, speed, pitch, the file it plays (the sound or its Beats render) and its position. Writes go through the
        undo manager, so an undo restores the Tracktion state with the properties. `rewriteBeats` false keeps a Beats
        clip on the render it already plays (a tempo drag in progress).
    */
    void applyWarp(te::WaveAudioClip& c, const Content& content, bool rewriteBeats = true)
    {
        auto w = warpOf(c);
        const auto source = sourceById(c.state[sourceIdProperty].toString());
        if (! source.isValid())
            w.on = false;
        const auto decoded = source.isValid() ? bundleDir().getChildFile(source.decoded()) : juce::File();
        const auto tempo = tempoBpm();
        const bool reversed = reversedOf(c);
        const bool beatsMode = isBeats(w);

        // A Beats render is made from the reversed sound, so Tracktion's own reverse flag is off for it.
        if (c.getIsReversed() != (reversed && ! beatsMode))
            c.setIsReversed(reversed && ! beatsMode);
        if (reversed && beatsMode)
            c.state.setProperty(reversedProperty, 1, um());
        else if (c.state.hasProperty(reversedProperty))
            c.state.removeProperty(reversedProperty, um());

        const auto leaveBeatsFile = [&] {
            if (decoded != juce::File() && pointsAtBeatsFile(c))
                c.getSourceFileReference().setToDirectFileReference(decoded, true);
        };
        const auto setStretch = [&](te::TimeStretcher::Mode mode) {
            if (c.getTimeStretchMode() != mode)
                c.setTimeStretchMode(mode);
        };
        const auto setPitch = [&](float semitones) {
            if (c.getPitchChange() != semitones)
                c.setPitchChange(semitones);
        };
        const auto setSpeed = [&](double ratio) {
            if (c.getSpeedRatio() != ratio)
                c.setSpeedRatio(ratio);
        };

        double timelineLength = content.covered;
        double timelineOffset = content.offset;
        double toTimeline = 1.0; // timeline seconds per second of the sound
        if (! w.active())
        {
            leaveBeatsFile();
            if (c.getAutoTempo())
                c.setAutoTempo(false);
            setStretch(te::TimeStretcher::disabled);
            setPitch(0.0f);
            setSpeed(1.0);
        }
        else if (w.mode == WarpMode::repitch)
        {
            leaveBeatsFile();
            if (c.getAutoTempo())
                c.setAutoTempo(false);
            setStretch(te::TimeStretcher::disabled);
            setPitch(0.0f);
            const auto ratio = juce::jlimit(te::ClipConstants::speedRatioMin, te::ClipConstants::speedRatioMax, tempo / w.sourceBpm);
            setSpeed(ratio);
            timelineLength = content.covered / ratio;
            timelineOffset = content.offset / ratio;
            toTimeline = 1.0 / ratio;
        }
        else if (w.mode == WarpMode::beats)
        {
            if (c.getAutoTempo())
                c.setAutoTempo(false);
            setStretch(te::TimeStretcher::disabled);
            setPitch(0.0f);
            setSpeed(1.0);
            if (rewriteBeats || ! pointsAtBeatsFile(c))
            {
                const auto key = render::beatsRenderKey(source.hash(), beatsParamsOf(c, w));
                c.getSourceFileReference().setToDirectFileReference(beatsFileFor(key), true);
            }
            const auto ratio = w.sourceBpm / tempo;
            timelineLength = content.covered * ratio;
            timelineOffset = content.offset * ratio;
            toTimeline = ratio;
        }
        else // tones
        {
            leaveBeatsFile();
            setSpeed(1.0);
            auto info = c.getLoopInfo();
            if (const auto wave = c.getAudioFile().getInfo(); wave.sampleRate > 0.0)
            {
                info.setBpm(w.sourceBpm, wave);
                info.setNumBeats(wave.getLengthInSeconds() * w.sourceBpm / 60.0);
                c.setLoopInfo(info);
            }
            setStretch(te::TimeStretcher::rubberbandMelodic);
            if (! c.getAutoTempo())
                c.setAutoTempo(true);
            setPitch(static_cast<float>(w.transpose));
            c.setUsesProxy(true);
            const auto ratio = w.sourceBpm / tempo;
            timelineLength = content.covered * ratio;
            timelineOffset = content.offset * ratio;
            toTimeline = ratio;
        }

        // Loop (D2): Tracktion's loop range is in timeline seconds (beats for auto-tempo clips) and its position offset is
        // how far into the loop the clip starts. Written as properties, as the setters clamp to the length of a file that a
        // Beats clip may not have yet.
        const auto loop = loopOf(c);
        const bool beatLoop = w.active() && w.mode == WarpMode::tones;
        double loopStartT = 0.0, loopLengthT = 0.0;
        if (loop.on)
        {
            loopStartT = loop.start * toTimeline;
            loopLengthT = loop.length * toTimeline;
            timelineOffset = std::fmod(std::max(0.0, timelineOffset - loopStartT), loopLengthT);
        }
        const auto setLoopProperty = [&](const juce::Identifier& id, double value) {
            if (value != 0.0 || c.state.hasProperty(id))
                if (! c.state.hasProperty(id) || static_cast<double>(c.state[id]) != value)
                    c.state.setProperty(id, value, um());
        };
        const auto beatsPerSecond = tempo / 60.0;
        setLoopProperty(te::IDs::loopStart, beatLoop ? 0.0 : loopStartT);
        setLoopProperty(te::IDs::loopLength, beatLoop ? 0.0 : loopLengthT);
        setLoopProperty(te::IDs::loopStartBeats, beatLoop ? loopStartT * beatsPerSecond : 0.0);
        setLoopProperty(te::IDs::loopLengthBeats, beatLoop ? loopLengthT * beatsPerSecond : 0.0);

        c.setPosition({{toTime(content.startBeats), tracktion::TimeDuration::fromSeconds(timelineLength)},
                       tracktion::TimeDuration::fromSeconds(timelineOffset)});
        applyFades(c);
        if (c.getIsReversed() || c.usesTimeStretchedProxy())
            c.beginRenderingNewProxyIfNeeded();
    }

    ClipInfo infoFor(te::WaveAudioClip& c, const juce::String& trackId) const
    {
        ClipInfo info;
        info.id = c.itemID.toString();
        info.trackId = trackId;
        info.sourceId = c.state[sourceIdProperty].toString();
        info.originSourceId = c.state[originSourceProperty].toString();
        info.originSlice = c.state.hasProperty(originSliceProperty) ? static_cast<int>(c.state[originSliceProperty]) : -1;
        info.pitch = c.state.hasProperty(pitchProperty) ? static_cast<double>(c.state[pitchProperty]) : 0.0;
        info.name = c.getName();
        info.warp = warpOf(c);
        const auto loop = loopOf(c);
        info.loop = loop.on;
        info.loopStart = loop.start;
        info.loopLength = loop.length;
        const auto fades = userFadesOf(c);
        info.fadeIn = fades.in;
        info.fadeOut = fades.out;
        const auto pos = c.getPosition();
        info.startSeconds = pos.getStart().inSeconds();
        info.lengthSeconds = pos.getLength().inSeconds();
        info.offsetSeconds = pos.getOffset().inSeconds();
        info.start = toBeats(pos.getStart());
        info.length = toBeats(pos.getEnd()) - info.start;
        // Seconds of the sound, not of the timeline; a looped clip starts that far past its loop's start.
        info.offsetSeconds = info.offsetSeconds * offsetFactor(info.warp) + (loop.on ? loop.start : 0.0);
        info.gainDb = c.getGainDB();
        info.reversed = reversedOf(c);

        const auto source = sourceById(info.sourceId);
        info.sourceSeconds = source.isValid() ? source.lengthSeconds() : c.getSourceLength().inSeconds();
        const bool beatsClip = isBeats(info.warp) && source.isValid();
        // A Beats clip plays a render in the cache: it is missing when the sound is, not when the render is.
        info.missing = beatsClip ? ! bundleDir().getChildFile(source.decoded()).existsAsFile()
                                 : ! c.getSourceFileReference().getFile().existsAsFile();
        if (! info.missing)
        {
            if (beatsClip)
                info.rendering = ! c.getSourceFileReference().getFile().existsAsFile();
            else if (c.getIsReversed() || c.usesTimeStretchedProxy())
            {
                const auto playback = c.getPlaybackFile();
                const auto file = playback.getFile();
                info.rendering = edit.engine.getRenderManager().isProxyBeingGenerated(playback) || ! file.existsAsFile()
                                 || file.getSize() < 100;
            }
        }
        return info;
    }

    std::vector<ClipInfo> clipsOf(te::AudioTrack& t) const
    {
        std::vector<ClipInfo> result;
        const auto trackId = t.itemID.toString();
        for (auto* c : t.getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*>(c))
                result.push_back(infoFor(*wave, trackId));
        std::sort(result.begin(), result.end(), [](const ClipInfo& a, const ClipInfo& b) { return a.start < b.start; });
        return result;
    }

    /** Where a track inserted at audio-track index `index` goes in Tracktion's list. */
    te::TrackInsertPoint insertPointFor(int index, te::Track* excluding = nullptr) const
    {
        juce::Array<te::AudioTrack*> list;
        for (auto* t : audioTracks())
            if (t != excluding)
                list.add(t);
        index = juce::jlimit(0, list.size(), index);
        if (list.isEmpty())
            return te::TrackInsertPoint::getEndOfTracks(edit);
        if (index > 0)
            return te::TrackInsertPoint(nullptr, list[index - 1]);
        // Before the first audio track: after whatever precedes it at the top level.
        te::Track* preceding = nullptr;
        for (auto* t : te::getTopLevelTracks(edit))
        {
            if (t == list.getFirst())
                break;
            if (t != excluding)
                preceding = t;
        }
        return te::TrackInsertPoint(nullptr, preceding);
    }

    /** Writes volume and pan explicitly, so an undo restores a value instead of removing the property (see syncMixerParameters). */
    static void ensureMixerProperties(te::VolumeAndPanPlugin& vp, juce::UndoManager* undo)
    {
        if (! vp.state.hasProperty(te::IDs::volume))
            vp.state.setProperty(te::IDs::volume, vp.volume.get(), undo);
        if (! vp.state.hasProperty(te::IDs::pan))
            vp.state.setProperty(te::IDs::pan, vp.pan.get(), undo);
    }

    void prepareNewClip(te::WaveAudioClip& clip)
    {
        clip.setAutoTempo(false);
        clip.setUsesProxy(true);
    }

    //==============================================================================
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override
    {
        if (isVolatile(tree, property))
            return;
        if (tree.hasType(te::IDs::PLUGIN) && (property == te::IDs::volume || property == te::IDs::pan))
            mixerValuesChanged = true;
        triggerAsyncUpdate();
    }

    /**
        Tracktion does not push a changed property (an undo or redo) back into a plugin's parameter, so the audio would
        keep the old volume or pan: bring every volume plugin's parameters back to their saved values.
    */
    void syncMixerParameters()
    {
        // Only where they differ: the update writes the value back through the undo manager when the property is
        // missing (at its default), which would be an undo step nobody asked for. ensureMixerProperties keeps them set.
        const auto sync = [](te::VolumeAndPanPlugin& vp) {
            if (std::abs(vp.volParam->getCurrentValue() - vp.volume.get()) > 1.0e-6f)
                vp.volParam->updateFromAttachedValue();
            if (std::abs(vp.panParam->getCurrentValue() - vp.pan.get()) > 1.0e-6f)
                vp.panParam->updateFromAttachedValue();
        };
        for (auto* t : audioTracks())
            if (auto* vp = t->getVolumePlugin())
                sync(*vp);
        if (auto master = edit.getMasterVolumePlugin())
            sync(*master);
    }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }

    /** Tracktion re-sorts nodes on its own after edits; only a new order of the audio tracks is a change. */
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override
    {
        if (parent != state)
            return;
        if (const auto order = trackOrder(); order != lastTrackOrder)
        {
            lastTrackOrder = order;
            triggerAsyncUpdate();
        }
    }

    juce::String trackOrder() const
    {
        juce::String order;
        for (const auto& child : state)
            if (child.hasType(te::IDs::TRACK))
                order << child[te::IDs::id].toString() << ",";
        return order;
    }

    void notifyChanged() { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override
    {
        if (std::exchange(mixerValuesChanged, false))
            syncMixerParameters();
        lastTrackOrder = trackOrder();
        ++owner.changeGeneration;
        owner.sendSynchronousChangeMessage();
    }

    Arrangement& owner;
    te::Edit& edit;
    juce::ValueTree state;
    te::Edit::UndoTransactionInhibitor inhibitor;
    juce::String lastTrackOrder;
    bool mixerValuesChanged = false;
};

Arrangement::Arrangement(te::Edit& edit) : impl(std::make_unique<Impl>(*this, edit)) {}
Arrangement::~Arrangement() = default;

//==============================================================================
std::vector<TrackInfo> Arrangement::tracks() const
{
    std::vector<TrackInfo> result;
    for (auto* t : impl->audioTracks())
        result.push_back(impl->infoFor(*t));
    return result;
}

int Arrangement::numTracks() const { return impl->audioTracks().size(); }

int Arrangement::trackIndex(const juce::String& trackId) const
{
    const auto list = impl->audioTracks();
    for (int i = 0; i < list.size(); ++i)
        if (list[i]->itemID.toString() == trackId)
            return i;
    return -1;
}

std::optional<TrackInfo> Arrangement::track(const juce::String& trackId) const
{
    if (auto* t = impl->findTrack(trackId))
        return impl->infoFor(*t);
    return std::nullopt;
}

std::vector<ClipInfo> Arrangement::clips() const
{
    std::vector<ClipInfo> result;
    for (auto* t : impl->audioTracks())
    {
        auto list = impl->clipsOf(*t);
        result.insert(result.end(), list.begin(), list.end());
    }
    return result;
}

std::vector<ClipInfo> Arrangement::clipsOnTrack(const juce::String& trackId) const
{
    if (auto* t = impl->findTrack(trackId))
        return impl->clipsOf(*t);
    return {};
}

std::optional<ClipInfo> Arrangement::clip(const juce::String& clipId) const
{
    for (auto* t : impl->audioTracks())
        for (auto* c : t->getClips())
            if (c->itemID.toString() == clipId)
                if (auto* wave = dynamic_cast<te::WaveAudioClip*>(c))
                    return impl->infoFor(*wave, t->itemID.toString());
    return std::nullopt;
}

int Arrangement::numClips() const
{
    int n = 0;
    for (auto* t : impl->audioTracks())
        n += t->getClips().size();
    return n;
}

double Arrangement::tempo() const { return impl->edit.tempoSequence.getTempo(0)->getBpm(); }

TimeSig Arrangement::timeSig() const
{
    auto* sig = impl->edit.tempoSequence.getTimeSig(0);
    return {sig->numerator.get(), sig->denominator.get()};
}

LoopRange Arrangement::loopRange() const
{
    const auto range = impl->edit.getTransport().getLoopRange();
    if (range.isEmpty())
        return {};
    return {impl->toBeats(range.getStart()), impl->toBeats(range.getEnd())};
}

double Arrangement::masterVolumeDb() const
{
    auto vp = impl->edit.getMasterVolumePlugin();
    return vp != nullptr ? te::volumeFaderPositionToDB(vp->volume.get()) : 0.0;
}

double Arrangement::endBeat() const
{
    double end = 0.0;
    for (auto* t : impl->audioTracks())
        for (auto* c : t->getClips())
            end = std::max(end, impl->toBeats(c->getPosition().getEnd()));
    return end;
}

double Arrangement::beatsToSeconds(double beats) const { return impl->toTime(beats).inSeconds(); }
double Arrangement::secondsToBeats(double seconds) const { return impl->toBeats(tracktion::TimePosition::fromSeconds(seconds)); }

juce::String Arrangement::serialise() const
{
    impl->edit.flushState();
    auto copy = impl->state.createCopy();
    copy.removeProperty(te::IDs::lastSignificantChange, nullptr);
    if (auto transport = copy.getChildWithName(te::IDs::TRANSPORT); transport.isValid())
        for (int i = transport.getNumProperties(); --i >= 0;)
            if (const auto name = transport.getPropertyName(i); isVolatile(transport, name))
                transport.removeProperty(name, nullptr);
    return copy.toXmlString();
}

bool Arrangement::allRendersReady() const
{
    for (const auto& c : clips())
        if (c.rendering)
            return false;
    return true;
}

std::vector<BeatsRenderRequest> Arrangement::missingBeatsRenders() const
{
    std::vector<BeatsRenderRequest> requests;
    for (auto* t : impl->audioTracks())
        for (auto* base : t->getClips())
        {
            auto* c = dynamic_cast<te::WaveAudioClip*>(base);
            if (c == nullptr)
                continue;
            const auto w = impl->warpOf(*c);
            const auto source = impl->sourceById(c->state[sourceIdProperty].toString());
            if (! Arrangement::Impl::isBeats(w) || ! source.isValid())
                continue;
            const auto decoded = impl->bundleDir().getChildFile(source.decoded());
            const auto params = impl->beatsParamsOf(*c, w);
            const auto key = render::beatsRenderKey(source.hash(), params);
            const auto target = impl->beatsFileFor(key);
            // Only the file the clip points at counts (during a tempo drag the clip keeps the render of an older tempo).
            if (c->getSourceFileReference().getFile() != target || target.existsAsFile() || ! decoded.existsAsFile())
                continue;
            if (std::any_of(requests.begin(), requests.end(), [&](const BeatsRenderRequest& r) { return r.key == key; }))
                continue;
            BeatsRenderRequest request;
            request.clipId = c->itemID.toString();
            request.key = key;
            request.decodedWav = decoded;
            request.destination = target;
            request.params = params;
            request.startBeat = impl->toBeats(c->getPosition().getStart());
            requests.push_back(request);
        }
    return requests;
}

void Arrangement::reloadFile(const juce::File& file)
{
    impl->edit.engine.getAudioFileManager().forceFileUpdate(te::AudioFile(impl->edit.engine, file));
    impl->scheduleRestartPlayback();
    impl->notifyChanged();
}

//==============================================================================
juce::String Arrangement::addTrack(int index, const juce::String& name, juce::Colour colour)
{
    auto track = impl->edit.insertNewAudioTrack(impl->insertPointFor(index), nullptr, true);
    if (track == nullptr)
        return {};
    track->setName(name);
    track->setColour(colour);
    if (auto* vp = track->getVolumePlugin())
        Impl::ensureMixerProperties(*vp, impl->um());
    return track->itemID.toString();
}

void Arrangement::removeTrack(const juce::String& trackId)
{
    if (auto* t = impl->findTrack(trackId))
        impl->edit.deleteTrack(t);
}

void Arrangement::moveTrack(const juce::String& trackId, int newIndex)
{
    if (auto* t = impl->findTrack(trackId))
        impl->edit.moveTrack(t, impl->insertPointFor(newIndex, t));
}

void Arrangement::setTrackName(const juce::String& trackId, const juce::String& name)
{
    if (auto* t = impl->findTrack(trackId))
        t->setName(name);
}

void Arrangement::setTrackColour(const juce::String& trackId, juce::Colour colour)
{
    if (auto* t = impl->findTrack(trackId))
        t->setColour(colour);
}

void Arrangement::setTrackVolumeDb(const juce::String& trackId, double db)
{
    if (auto* t = impl->findTrack(trackId))
        if (auto* vp = t->getVolumePlugin())
            vp->setVolumeDb(static_cast<float>(juce::jlimit(silenceDb, 6.0, db)));
}

void Arrangement::setTrackPan(const juce::String& trackId, double pan)
{
    if (auto* t = impl->findTrack(trackId))
        if (auto* vp = t->getVolumePlugin())
            vp->setPan(static_cast<float>(juce::jlimit(-1.0, 1.0, pan)));
}

void Arrangement::setTrackMute(const juce::String& trackId, bool mute)
{
    // AudioTrack binds mute and solo without the undo manager, so the property is written here with it.
    if (auto* t = impl->findTrack(trackId))
        t->state.setProperty(te::IDs::mute, mute, impl->um());
}

void Arrangement::setTrackInput(const juce::String& trackId, const juce::String& input)
{
    if (auto* t = impl->findTrack(trackId))
    {
        if (input.isEmpty() || input == "system:default")
            t->state.removeProperty(inputProperty, impl->um());
        else
            t->state.setProperty(inputProperty, input, impl->um());
    }
}

void Arrangement::setTrackSolo(const juce::String& trackId, bool solo)
{
    if (auto* t = impl->findTrack(trackId))
        t->state.setProperty(te::IDs::solo, solo, impl->um());
}

void Arrangement::setMasterVolumeDb(double db)
{
    if (auto vp = impl->edit.getMasterVolumePlugin())
        vp->setVolumeDb(static_cast<float>(juce::jlimit(silenceDb, 6.0, db)));
}

juce::String Arrangement::createClip(const juce::String& trackId, const ClipSpec& spec)
{
    auto* t = impl->findTrack(trackId);
    if (t == nullptr)
        return {};
    const auto start = impl->toTime(spec.start);
    const te::ClipPosition position{{start, tracktion::TimeDuration::fromSeconds(spec.lengthSeconds)},
                                    tracktion::TimeDuration::fromSeconds(spec.offsetSeconds)};
    auto clip = t->insertWaveClip(spec.name, spec.file, position, false);
    if (clip == nullptr)
        return {};
    clip->getSourceFileReference().setToDirectFileReference(spec.file, true); // relative to the edit file
    clip->state.setProperty(sourceIdProperty, spec.sourceId, impl->um());
    if (spec.originSlice >= 0)
    {
        clip->state.setProperty(originSourceProperty, spec.originSourceId, impl->um());
        clip->state.setProperty(originSliceProperty, spec.originSlice, impl->um());
        clip->state.setProperty(pitchProperty, spec.pitch, impl->um());
    }
    impl->prepareNewClip(*clip);
    clip->setPosition(position);
    clip->setFadeIn(tracktion::TimeDuration::fromSeconds(0.003));
    clip->setFadeOut(tracktion::TimeDuration::fromSeconds(0.003));
    if (spec.gainDb != 0.0)
        clip->setGainDB(static_cast<float>(spec.gainDb));

    auto warp = spec.warp;
    warp.transpose = juce::jlimit(-WarpSettings::maxTranspose, WarpSettings::maxTranspose, warp.transpose);
    if (warp.on || warp.sourceBpm > 0.0 || warp.transpose != 0.0)
        impl->writeWarp(*clip, warp);
    if (spec.fadeIn > 0.0)
        clip->state.setProperty(fadeInProperty, spec.fadeIn, impl->um());
    if (spec.fadeOut > 0.0)
        clip->state.setProperty(fadeOutProperty, spec.fadeOut, impl->um());
    const bool looped = spec.loopLength > 0.0;
    if (looped)
        impl->writeLoop(*clip, {true, spec.loopStart, spec.loopLength});
    if (warp.active() || looped)
    {
        // Warped: the clip covers the part of the sound that the spec names, as bars at the clip's own tempo.
        Arrangement::Impl::Content content;
        content.startBeats = spec.start;
        content.offset = spec.offsetSeconds;
        content.covered = (warp.active() && spec.lengthBeats > 0.0 ? spec.lengthBeats * 60.0 / warp.sourceBpm : spec.lengthSeconds);
        if (spec.reversed)
        {
            if (Arrangement::Impl::isBeats(warp))
                clip->state.setProperty(reversedProperty, 1, impl->um());
            else
                clip->setIsReversed(true);
        }
        impl->applyWarp(*clip, content);
        return clip->itemID.toString();
    }

    if (spec.reversed)
    {
        clip->setIsReversed(true);
        clip->setPosition(position); // reversing rewrites the offset for the reversed file; the spec's offset is already that
        clip->beginRenderingNewProxyIfNeeded();
    }
    return clip->itemID.toString();
}

juce::String Arrangement::copyClip(const juce::String& clipId, const juce::String& trackId, double startBeats,
                                   double lengthSeconds, double offsetSeconds)
{
    auto* source = impl->findClip(clipId);
    auto* t = impl->findTrack(trackId);
    if (source == nullptr || t == nullptr)
        return {};
    auto copy = source->state.createCopy();
    te::EditItemID::remapIDs(copy, nullptr, impl->edit);
    const te::ClipPosition position{{impl->toTime(startBeats), tracktion::TimeDuration::fromSeconds(lengthSeconds)},
                                    tracktion::TimeDuration::fromSeconds(offsetSeconds)};
    copy.setProperty(te::IDs::start, position.getStart().inSeconds(), nullptr);
    copy.setProperty(te::IDs::length, position.getLength().inSeconds(), nullptr);
    copy.setProperty(te::IDs::offset, position.getOffset().inSeconds(), nullptr);
    auto* clip = t->insertClipWithState(copy);
    if (clip == nullptr)
        return {};
    clip->setPosition(position);
    if (auto* wave = dynamic_cast<te::WaveAudioClip*>(clip))
    {
        if (impl->warpOf(*wave).active() || impl->loopOf(*wave).on)
            impl->applyWarp(*wave, impl->contentFor(*wave, startBeats, lengthSeconds, offsetSeconds));
        else
        {
            impl->applyFades(*wave);
            if (wave->getIsReversed())
                wave->beginRenderingNewProxyIfNeeded();
        }
    }
    return clip->itemID.toString();
}

void Arrangement::removeClip(const juce::String& clipId)
{
    if (auto* c = impl->findClip(clipId))
        c->removeFromParent();
}

void Arrangement::setClipBounds(const juce::String& clipId, const juce::String& trackId, double startBeats,
                                double lengthSeconds, double offsetSeconds)
{
    auto* c = impl->findClip(clipId);
    if (c == nullptr)
        return;
    if (auto* t = impl->findTrack(trackId); t != nullptr && c->getClipTrack() != t)
        c->moveTo(*t);
    if (impl->warpOf(*c).active() || impl->loopOf(*c).on)
    {
        impl->applyWarp(*c, impl->contentFor(*c, startBeats, lengthSeconds, offsetSeconds));
        return;
    }
    c->setPosition({{impl->toTime(startBeats), tracktion::TimeDuration::fromSeconds(lengthSeconds)},
                    tracktion::TimeDuration::fromSeconds(offsetSeconds)});
    impl->applyFades(*c);
}

void Arrangement::setClipLoop(const juce::String& clipId, bool on)
{
    auto* c = impl->findClip(clipId);
    if (c == nullptr)
        return;
    const auto before = impl->loopOf(*c);
    if (before.on == on)
        return;
    auto content = impl->contentOf(*c);
    const auto source = impl->sourceById(c->state[sourceIdProperty].toString());
    const auto soundEnd = source.isValid() ? source.lengthSeconds() : 0.0;
    if (on)
    {
        // The loop is what the clip plays now (one pass), cut to the sound.
        Arrangement::Impl::Loop loop{true, content.offset, content.covered};
        if (soundEnd > 0.0)
            loop.length = std::min(loop.length, soundEnd - loop.start);
        if (loop.length <= 0.0)
            return;
        impl->writeLoop(*c, loop);
        content.offset = loop.start; // the clip begins at the start of its loop; its length stays one pass
    }
    else
    {
        // One pass from where the clip starts: a clip longer than its loop becomes as long as the loop.
        const auto pass = std::min(content.covered, before.length);
        impl->writeLoop(*c, {});
        content.covered = pass;
        if (soundEnd > 0.0)
            content.covered = std::min(content.covered, std::max(0.0, soundEnd - content.offset));
    }
    impl->applyWarp(*c, content);
}

void Arrangement::setClipFades(const juce::String& clipId, double fadeIn, double fadeOut)
{
    auto* c = impl->findClip(clipId);
    if (c == nullptr)
        return;
    const auto length = c->getPosition().getLength().inSeconds();
    fadeIn = juce::jlimit(0.0, length, fadeIn);
    fadeOut = juce::jlimit(0.0, std::max(0.0, length - fadeIn), fadeOut);
    const auto write = [&](const juce::Identifier& id, double value) {
        // 0 is written over an existing value, not removed: an undo would put a removed property back at the end of the node.
        if (value > 0.0 || c->state.hasProperty(id))
            c->state.setProperty(id, value, impl->um());
    };
    write(fadeInProperty, fadeIn);
    write(fadeOutProperty, fadeOut);
    impl->applyFades(*c);
}

void Arrangement::setClipGainDb(const juce::String& clipId, double db)
{
    if (auto* c = impl->findClip(clipId))
        c->setGainDB(static_cast<float>(juce::jlimit(silenceDb, 24.0, db)));
}

void Arrangement::setClipReversed(const juce::String& clipId, bool reversed)
{
    auto* c = impl->findClip(clipId);
    if (c == nullptr || impl->reversedOf(*c) == reversed)
        return;
    if (const auto loop = impl->loopOf(*c); loop.on)
    {
        // The loop region and the start inside it move to the other end of the sound, as the offset of a reversed clip does.
        auto content = impl->contentOf(*c);
        const auto source = impl->sourceById(c->state[sourceIdProperty].toString());
        const auto soundEnd = source.isValid() ? source.lengthSeconds() : loop.start + loop.length;
        const auto phase = content.offset - loop.start;
        const Arrangement::Impl::Loop mirrored{true, std::max(0.0, soundEnd - loop.start - loop.length), loop.length};
        impl->writeLoop(*c, mirrored);
        content.offset = mirrored.start + std::fmod(loop.length - phase, loop.length);
        if (Arrangement::Impl::isBeats(impl->warpOf(*c)))
        {
            if (reversed)
                c->state.setProperty(reversedProperty, 1, impl->um());
            else
                c->state.removeProperty(reversedProperty, impl->um());
        }
        else
            c->setIsReversed(reversed);
        impl->applyWarp(*c, content);
        return;
    }
    if (Arrangement::Impl::isBeats(impl->warpOf(*c)))
    {
        // A Beats clip plays a render of the reversed sound: the offset moves to the other end of the sound, as Tracktion
        // does for its own reverse, and the render is another file.
        auto content = impl->contentOf(*c);
        const auto source = impl->sourceById(c->state[sourceIdProperty].toString());
        content.offset = std::max(0.0, source.lengthSeconds() - content.covered - content.offset);
        if (reversed)
            c->state.setProperty(reversedProperty, 1, impl->um());
        else
            c->state.removeProperty(reversedProperty, impl->um());
        impl->applyWarp(*c, content);
        return;
    }
    c->setIsReversed(reversed);
    if (reversed)
        c->beginRenderingNewProxyIfNeeded();
}

void Arrangement::setClipWarp(const juce::String& clipId, const WarpSettings& settings)
{
    auto* c = impl->findClip(clipId);
    if (c == nullptr)
        return;
    auto w = settings;
    w.transpose = juce::jlimit(-WarpSettings::maxTranspose, WarpSettings::maxTranspose, w.transpose);
    if (w.sourceBpm > 0.0)
        w.sourceBpm = juce::jlimit(WarpSettings::minBpm, WarpSettings::maxBpm, w.sourceBpm);
    const auto content = impl->contentOf(*c); // under the old settings: the same part of the sound stays
    impl->writeWarp(*c, w);
    impl->applyWarp(*c, content);
}

void Arrangement::setClipTranspose(const juce::String& clipId, double semitones)
{
    if (auto* c = impl->findClip(clipId))
    {
        auto w = impl->warpOf(*c);
        w.transpose = juce::jlimit(-WarpSettings::maxTranspose, WarpSettings::maxTranspose, semitones);
        setClipWarp(clipId, w);
    }
}

void Arrangement::setTempo(double bpm, bool rewriteBeats)
{
    if (! std::isfinite(bpm))
        return;
    // Warped clips keep their start and their bars: note what each plays now, change the tempo, and set Tracktion up again.
    std::vector<std::pair<te::WaveAudioClip*, Arrangement::Impl::Content>> warped;
    for (auto* t : impl->audioTracks())
        for (auto* base : t->getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*>(base); wave != nullptr && impl->warpOf(*wave).active())
                warped.emplace_back(wave, impl->contentOf(*wave));

    auto* tempo = impl->edit.tempoSequence.getTempo(0);
    const auto clamped = juce::jlimit(20.0, 999.0, bpm);
    if (clamped <= te::TempoSetting::maxBPM)
        tempo->setBpm(clamped);
    else // Tracktion's setter stops at 300 BPM; the tempo sequence itself takes any value (SetTempo keeps the clips on their beats)
        tempo->state.setProperty(te::IDs::bpm, clamped, impl->um());

    for (auto& [clip, content] : warped)
        impl->applyWarp(*clip, content, rewriteBeats);
}

void Arrangement::setTimeSig(int numerator, int denominator)
{
    auto* sig = impl->edit.tempoSequence.getTimeSig(0);
    sig->numerator = juce::jlimit(1, 32, numerator);
    sig->denominator = denominator;
}

void Arrangement::setLoopRange(LoopRange range)
{
    // TransportControl binds the loop points without the undo manager, so they are written here with it.
    auto& transport = impl->edit.getTransport();
    const auto start = range.isEmpty() ? 0.0 : impl->toTime(range.start).inSeconds();
    const auto end = range.isEmpty() ? 0.0 : impl->toTime(range.end).inSeconds();
    transport.state.setProperty(te::IDs::loopPoint1, start, impl->um());
    transport.state.setProperty(te::IDs::loopPoint2, end, impl->um());
}

void Arrangement::addDefaultTrackIfEmpty()
{
    // Tracktion's new edits (and so the projects M1 saved) hold one unnamed audio track: it becomes "Audio 1". Written
    // without the undo manager, so it is neither an undo step nor an unsaved change; the file changes on the next save.
    // The same goes for writing out the mixer values that are still at their defaults.
    const auto list = impl->audioTracks();
    for (int i = 0; i < list.size(); ++i)
    {
        auto& node = list[i]->state;
        if (! node.hasProperty(te::IDs::name) || node[te::IDs::name].toString().isEmpty())
            node.setProperty(te::IDs::name, "Audio " + juce::String(i + 1), nullptr);
        if (! node.hasProperty(te::IDs::colour))
            node.setProperty(te::IDs::colour, trackPaletteColour(i).toString(), nullptr);
        if (auto* vp = list[i]->getVolumePlugin())
            Impl::ensureMixerProperties(*vp, nullptr);
    }
    if (auto master = impl->edit.getMasterVolumePlugin())
        Impl::ensureMixerProperties(*master, nullptr);
    if (! list.isEmpty())
        return;
    auto& undo = impl->edit.getUndoManager();
    const bool hadHistory = undo.canUndo() || undo.canRedo();
    addTrack(0, "Audio 1", trackPaletteColour(0));
    if (! hadHistory)
        undo.clearUndoHistory();
}
} // namespace sampler::engine
