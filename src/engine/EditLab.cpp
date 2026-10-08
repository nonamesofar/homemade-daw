#include "EditLab.h"

#include "EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
struct EditLab::Impl
{
    explicit Impl(const juce::String& appName) : engine(detail::makeEngine(appName)) {}

    te::AudioTrack* track() const
    {
        if (edit == nullptr)
            return nullptr;
        auto tracks = te::getAudioTracks(*edit);
        return tracks.isEmpty() ? nullptr : tracks.getFirst();
    }

    te::WaveAudioClip* clip(int index) const
    {
        if (auto* t = track())
        {
            auto clips = t->getClips();
            if (juce::isPositiveAndBelow(index, clips.size()))
                return dynamic_cast<te::WaveAudioClip*>(clips[index]);
        }
        return nullptr;
    }

    /** Runs the message loop until `done()` or the timeout. */
    template <typename Pred>
    static bool waitFor(Pred done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (!done())
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

EditLab::EditLab(const juce::String& appName) : impl(std::make_unique<Impl>(appName)) {}
EditLab::~EditLab() = default;

void EditLab::newEdit(const juce::File& editFile)
{
    impl->edit = te::createEmptyEdit(*impl->engine, editFile);
    impl->edit->ensureNumberOfAudioTracks(1);
    te::EditFileOperations(*impl->edit).save(false, true, false);
}

bool EditLab::open(const juce::File& editFile)
{
    impl->edit = te::loadEditFromFile(*impl->engine, editFile);
    return impl->edit != nullptr;
}

bool EditLab::save()
{
    return impl->edit != nullptr && te::EditFileOperations(*impl->edit).save(false, true, false);
}

void EditLab::closeEdit() { impl->edit.reset(); }

int EditLab::addClip(const juce::File& audio, double startBeats, double lengthSeconds, bool useRelativePath)
{
    auto* track = impl->track();
    if (track == nullptr)
        return -1;

    const auto start = impl->edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(startBeats));
    auto clip = track->insertWaveClip(audio.getFileNameWithoutExtension(), audio,
                                      {{start, tracktion::TimeDuration::fromSeconds(lengthSeconds)}, {}}, false);
    if (clip == nullptr)
        return -1;

    if (useRelativePath)
        clip->getSourceFileReference().setToDirectFileReference(audio, true);

    return track->getClips().indexOf(clip.get());
}

int EditLab::numClips() const
{
    auto* t = impl->track();
    return t != nullptr ? t->getClips().size() : 0;
}

double EditLab::clipStartSeconds(int clip) const
{
    auto* c = impl->clip(clip);
    return c != nullptr ? c->getPosition().getStart().inSeconds() : -1.0;
}

double EditLab::clipStartBeats(int clip) const
{
    auto* c = impl->clip(clip);
    return c != nullptr ? impl->edit->tempoSequence.toBeats(c->getPosition().getStart()).inBeats() : -1.0;
}

void EditLab::setTempo(double bpm)
{
    if (impl->edit != nullptr)
        impl->edit->tempoSequence.getTempo(0)->setBpm(bpm);
}

void EditLab::setClipProperty(int clip, const juce::String& name, const juce::String& value)
{
    if (auto* c = impl->clip(clip))
        c->state.setProperty(juce::Identifier(name), value, nullptr);
}

juce::String EditLab::clipProperty(int clip, const juce::String& name) const
{
    auto* c = impl->clip(clip);
    return c != nullptr ? c->state[juce::Identifier(name)].toString() : juce::String();
}

bool EditLab::clipHasProperty(int clip, const juce::String& name) const
{
    auto* c = impl->clip(clip);
    return c != nullptr && c->state.hasProperty(juce::Identifier(name));
}

juce::String EditLab::clipSourceReference(int clip) const
{
    auto* c = impl->clip(clip);
    return c != nullptr ? c->getSourceFileReference().source.get() : juce::String();
}

bool EditLab::clipSourceResolves(int clip) const
{
    auto* c = impl->clip(clip);
    return c != nullptr && c->getSourceFileReference().getFile().existsAsFile();
}

bool EditLab::setCacheDirectory(const juce::File& dir)
{
    dir.createDirectory();
    return impl->engine->getTemporaryFileManager().setTempDirectory(dir);
}

juce::File EditLab::editTempDirectory() const
{
    return impl->edit != nullptr ? impl->edit->getTempDirectory(true) : juce::File();
}

juce::File EditLab::thumbnailsDirectory() const { return impl->engine->getTemporaryFileManager().getThumbnailsFolder(); }

bool EditLab::buildThumbnail(int clipIndex)
{
    auto* c = impl->clip(clipIndex);
    if (c == nullptr)
        return false;

    juce::Component host;
    te::SmartThumbnail thumbnail(*impl->engine, c->getAudioFile(), host, impl->edit.get());
    return Impl::waitFor([&] { return thumbnail.isFullyLoaded(); }, 15000);
}

bool EditLab::renderToWav(const juce::File& dest, double lengthSeconds)
{
    if (impl->edit == nullptr)
        return false;

    te::Renderer::Parameters params(*impl->edit);
    params.destFile = dest;
    params.tracksToDo = te::toBitSet(te::getAllTracks(*impl->edit));
    params.audioFormat = impl->engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 24;
    params.sampleRateForAudio = 44100.0;
    params.canRenderInMono = false;
    params.time = tracktion::TimeRange(tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds(lengthSeconds));
    return te::Renderer::renderToFile("Sampler render", params).existsAsFile();
}

bool EditLab::buildReverseProxy(int clipIndex)
{
    auto* c = impl->clip(clipIndex);
    if (c == nullptr)
        return false;

    c->setIsReversed(true);
    c->beginRenderingNewProxyIfNeeded();

    const auto dir = impl->edit->getTempDirectory(true);
    return Impl::waitFor([&] { return !dir.findChildFiles(juce::File::findFiles, false, "render_*.wav").isEmpty(); }, 20000);
}

int EditLab::buildStretchProxy(int clipIndex, double speedRatio, bool& usesProxy)
{
    auto* c = impl->clip(clipIndex);
    usesProxy = false;
    if (c == nullptr)
        return -1;

    c->setTimeStretchMode(te::TimeStretcher::rubberbandMelodic);
    c->setSpeedRatio(speedRatio);
    usesProxy = c->usesTimeStretchedProxy();
    c->beginRenderingNewProxyIfNeeded();

    const auto dir = impl->edit->getTempDirectory(true);
    if (usesProxy)
    {
        // The proxy file appears empty while it is being written; wait for it to have content, then let the job wind down.
        Impl::waitFor(
            [&]
            {
                const auto files = dir.findChildFiles(juce::File::findFiles, false, "*.wav");
                return !files.isEmpty() && files.getFirst().getSize() > 1000;
            },
            30000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    }
    return dir.findChildFiles(juce::File::findFiles, false, "*.wav").size();
}
} // namespace sampler
