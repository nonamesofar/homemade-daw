#include "Arrangement.h"

#include "EngineSetup.h"
#include "model/SamplerState.h"

#include <cmath>
#include <utility>

namespace te = tracktion::engine;

namespace sampler::engine
{
namespace
{
const juce::Identifier sourceIdProperty("sampler_sourceId");
const juce::Identifier loopOnProperty("sampler_loopOn");
const juce::Identifier inputProperty("sampler_input");

/** Changes that are not edits: the playhead and Tracktion's own time stamp. */
bool isVolatile(const juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType(te::IDs::TRANSPORT))
        return property != te::IDs::loopPoint1 && property != te::IDs::loopPoint2 && property != te::IDs::looping
               && property != loopOnProperty;
    return property == te::IDs::lastSignificantChange;
}
} // namespace

juce::Colour trackPaletteColour(int index)
{
    static const juce::uint32 colours[trackPaletteSize] = {0xffff8f5a, 0xff4fd1c5, 0xffb9a2ff, 0xfff0d65f, 0xff6aa8ff, 0xffff6b9a,
                                                           0xff8fd16a, 0xffffb84d, 0xff5ad1ff, 0xffe58fff, 0xffc8c8a0, 0xffff7070};
    return juce::Colour(colours[((index % trackPaletteSize) + trackPaletteSize) % trackPaletteSize]);
}

struct Arrangement::Impl : private juce::ValueTree::Listener, private juce::AsyncUpdater
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

    ClipInfo infoFor(te::WaveAudioClip& c, const juce::String& trackId) const
    {
        ClipInfo info;
        info.id = c.itemID.toString();
        info.trackId = trackId;
        info.sourceId = c.state[sourceIdProperty].toString();
        info.name = c.getName();
        const auto pos = c.getPosition();
        info.startSeconds = pos.getStart().inSeconds();
        info.lengthSeconds = pos.getLength().inSeconds();
        info.offsetSeconds = pos.getOffset().inSeconds();
        info.start = toBeats(pos.getStart());
        info.length = toBeats(pos.getEnd()) - info.start;
        info.gainDb = c.getGainDB();
        info.reversed = c.getIsReversed();

        const auto source = model::SamplerState(state.getChildWithName(model::ids::SAMPLER)).findSourceById(info.sourceId);
        info.sourceSeconds = source.isValid() ? source.lengthSeconds() : c.getSourceLength().inSeconds();
        info.missing = ! c.getSourceFileReference().getFile().existsAsFile();
        if (info.reversed && ! info.missing)
        {
            const auto playback = c.getPlaybackFile();
            const auto file = playback.getFile();
            info.rendering = edit.engine.getRenderManager().isProxyBeingGenerated(playback) || ! file.existsAsFile()
                             || file.getSize() < 100;
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
    impl->prepareNewClip(*clip);
    clip->setPosition(position);
    clip->setFadeIn(tracktion::TimeDuration::fromSeconds(0.003));
    clip->setFadeOut(tracktion::TimeDuration::fromSeconds(0.003));
    if (spec.gainDb != 0.0)
        clip->setGainDB(static_cast<float>(spec.gainDb));
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
    if (auto* wave = dynamic_cast<te::WaveAudioClip*>(clip); wave != nullptr && wave->getIsReversed())
        wave->beginRenderingNewProxyIfNeeded();
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
    c->setPosition({{impl->toTime(startBeats), tracktion::TimeDuration::fromSeconds(lengthSeconds)},
                    tracktion::TimeDuration::fromSeconds(offsetSeconds)});
}

void Arrangement::setClipGainDb(const juce::String& clipId, double db)
{
    if (auto* c = impl->findClip(clipId))
        c->setGainDB(static_cast<float>(juce::jlimit(silenceDb, 24.0, db)));
}

void Arrangement::setClipReversed(const juce::String& clipId, bool reversed)
{
    if (auto* c = impl->findClip(clipId); c != nullptr && c->getIsReversed() != reversed)
    {
        c->setIsReversed(reversed);
        if (reversed)
            c->beginRenderingNewProxyIfNeeded();
    }
}

void Arrangement::setTempo(double bpm)
{
    if (! std::isfinite(bpm))
        return;
    auto* tempo = impl->edit.tempoSequence.getTempo(0);
    const auto clamped = juce::jlimit(20.0, 999.0, bpm);
    if (clamped <= te::TempoSetting::maxBPM)
        tempo->setBpm(clamped);
    else // Tracktion's setter stops at 300 BPM; the tempo sequence itself takes any value (SetTempo keeps the clips on their beats)
        tempo->state.setProperty(te::IDs::bpm, clamped, impl->um());
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
