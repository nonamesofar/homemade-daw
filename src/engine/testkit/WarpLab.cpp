#include "WarpLab.h"

#include "engine/EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
struct WarpLab::Impl
{
    Impl() : engine(detail::makeEngine("SamplerWarpLab", true)) {}

    te::WaveAudioClip* clip(int index) const
    {
        if (edit == nullptr)
            return nullptr;
        auto tracks = te::getAudioTracks(*edit);
        if (tracks.isEmpty())
            return nullptr;
        auto clips = tracks.getFirst()->getClips();
        return juce::isPositiveAndBelow(index, clips.size()) ? dynamic_cast<te::WaveAudioClip*>(clips[index]) : nullptr;
    }

    tracktion::TimePosition toTime(double beats) const
    {
        return edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats));
    }

    template <typename Pred>
    static bool waitFor(Pred done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (! done())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        return true;
    }

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
};

WarpLab::WarpLab() : impl(std::make_unique<Impl>()) {}
WarpLab::~WarpLab() = default;

void WarpLab::newEdit(const juce::File& editFile, double bpm)
{
    impl->edit = te::createEmptyEdit(*impl->engine, editFile);
    impl->edit->ensureNumberOfAudioTracks(1);
    impl->edit->tempoSequence.getTempo(0)->setBpm(bpm);
}

void WarpLab::setTempo(double bpm) { impl->edit->tempoSequence.getTempo(0)->setBpm(bpm); }
double WarpLab::tempo() const { return impl->edit->tempoSequence.getTempo(0)->getBpm(); }

int WarpLab::addClip(const juce::File& audio, double startBeats, double lengthSeconds, double offsetSeconds)
{
    auto* track = te::getAudioTracks(*impl->edit).getFirst();
    const te::ClipPosition position{{impl->toTime(startBeats), tracktion::TimeDuration::fromSeconds(lengthSeconds)},
                                    tracktion::TimeDuration::fromSeconds(offsetSeconds)};
    auto clip = track->insertWaveClip(audio.getFileNameWithoutExtension(), audio, position, false);
    if (clip == nullptr)
        return -1;
    clip->setAutoTempo(false);
    clip->setUsesProxy(true);
    clip->setPosition(position);
    clip->setFadeIn(tracktion::TimeDuration());
    clip->setFadeOut(tracktion::TimeDuration());
    return track->getClips().indexOf(clip.get());
}

void WarpLab::setRepitch(int index, double ratio, double startBeats, double lengthSeconds, double offsetSeconds)
{
    auto* c = impl->clip(index);
    c->setAutoTempo(false);
    c->setTimeStretchMode(te::TimeStretcher::disabled);
    c->setSpeedRatio(ratio);
    c->setPosition({{impl->toTime(startBeats), tracktion::TimeDuration::fromSeconds(lengthSeconds)},
                    tracktion::TimeDuration::fromSeconds(offsetSeconds)});
}

void WarpLab::setTones(int index, double sourceBpm, double semitones)
{
    auto* c = impl->clip(index);
    auto info = c->getLoopInfo();
    const auto wave = c->getAudioFile().getInfo();
    info.setBpm(sourceBpm, wave);
    info.setNumBeats(wave.getLengthInSeconds() * sourceBpm / 60.0);
    c->setLoopInfo(info);
    c->setAutoTempo(true);
    c->setTimeStretchMode(te::TimeStretcher::rubberbandMelodic);
    c->setPitchChange(static_cast<float>(semitones));
    c->setUsesProxy(true);
}

void WarpLab::setLoopSeconds(int index, double startSeconds, double lengthSeconds)
{
    impl->clip(index)->setLoopRange({tracktion::TimePosition::fromSeconds(startSeconds), tracktion::TimeDuration::fromSeconds(lengthSeconds)});
}

void WarpLab::setLoopBeats(int index, double startBeats, double lengthBeats)
{
    impl->clip(index)->setLoopRangeBeats({tracktion::BeatPosition::fromBeats(startBeats), tracktion::BeatDuration::fromBeats(lengthBeats)});
}

void WarpLab::setClipLengthSeconds(int index, double lengthSeconds)
{
    impl->clip(index)->setLength(tracktion::TimeDuration::fromSeconds(lengthSeconds), false);
}

double WarpLab::clipLengthBeats(int index) const
{
    const auto pos = impl->clip(index)->getPosition();
    const auto& ts = impl->edit->tempoSequence;
    return ts.toBeats(pos.getEnd()).inBeats() - ts.toBeats(pos.getStart()).inBeats();
}

double WarpLab::clipLengthSeconds(int index) const { return impl->clip(index)->getPosition().getLength().inSeconds(); }
bool WarpLab::clipIsLooping(int index) const { return impl->clip(index)->isLooping(); }

bool WarpLab::waitForProxies(int timeoutMs)
{
    auto& renders = impl->engine->getRenderManager();
    return Impl::waitFor(
        [&]
        {
            for (auto* track : te::getAudioTracks(*impl->edit))
                for (auto* base : track->getClips())
                    if (auto* c = dynamic_cast<te::WaveAudioClip*>(base))
                    {
                        c->beginRenderingNewProxyIfNeeded();
                        const auto playback = c->getPlaybackFile();
                        if (renders.isProxyBeingGenerated(playback))
                            return false;
                        if (c->usesTimeStretchedProxy() && playback.getFile().getSize() < 1000)
                            return false;
                    }
            return true;
        },
        timeoutMs);
}

bool WarpLab::renderToWav(const juce::File& dest, double seconds, double sampleRate, int blockSize)
{
    if (! waitForProxies())
        return false;
    dest.deleteFile();
    te::Renderer::Parameters params(*impl->edit);
    params.destFile = dest;
    params.tracksToDo = te::toBitSet(te::getAllTracks(*impl->edit));
    params.audioFormat = impl->engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 32;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = blockSize;
    params.canRenderInMono = false;
    params.useMasterPlugins = true;
    params.time = tracktion::TimeRange(tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds(seconds));
    return te::Renderer::renderToFile("Warp lab render", params).existsAsFile();
}

void WarpLab::reloadFile(const juce::File& file, bool alsoNotifyClips)
{
    impl->engine->getAudioFileManager().forceFileUpdate(te::AudioFile(*impl->engine, file));
    if (alsoNotifyClips)
        for (auto* track : te::getAudioTracks(*impl->edit))
            for (auto* base : track->getClips())
                if (auto* c = dynamic_cast<te::WaveAudioClip*>(base))
                    c->sourceMediaChanged();
}
} // namespace sampler
