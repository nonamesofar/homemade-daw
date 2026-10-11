#include "FilePlayer.h"

#include "engine/EngineHost.h"
#include "engine/EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
struct FilePlayer::Impl
{
    explicit Impl(EngineHost& h) : engine(h.tracktionEngine()) {}

    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    double lengthSeconds = 0.0;
};

FilePlayer::FilePlayer(EngineHost& host) : impl(std::make_unique<Impl>(host)) {}

FilePlayer::~FilePlayer()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);
}

bool FilePlayer::loadFile(const juce::File& file)
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);

    impl->edit.reset();
    impl->lengthSeconds = 0.0;

    te::AudioFile audioFile(impl->engine, file);
    if (! audioFile.isValid())
        return false;

    auto edit = te::Edit::createSingleTrackEdit(impl->engine);
    auto tracks = te::getAudioTracks(*edit);
    if (tracks.isEmpty())
        return false;

    const auto length = audioFile.getLength();
    auto clip = tracks.getFirst()->insertWaveClip(
        file.getFileNameWithoutExtension(), file,
        {{tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds(length)}, {}}, false);
    if (clip == nullptr)
        return false;

    impl->lengthSeconds = length;
    impl->edit = std::move(edit);
    impl->edit->getTransport().ensureContextAllocated();
    return true;
}

double FilePlayer::loadedLengthSeconds() const { return impl->lengthSeconds; }

void FilePlayer::play()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().play(false);
}

void FilePlayer::stop()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);
}

bool FilePlayer::isPlaying() const { return impl->edit != nullptr && impl->edit->getTransport().isPlaying(); }

double FilePlayer::positionSeconds() const
{
    return impl->edit != nullptr ? impl->edit->getTransport().getPosition().inSeconds() : 0.0;
}

void FilePlayer::setTempo(double bpm)
{
    if (impl->edit != nullptr)
        impl->edit->tempoSequence.getTempo(0)->setBpm(bpm);
}

float FilePlayer::masterVolumeDb() const
{
    if (impl->edit != nullptr)
        if (auto master = impl->edit->getMasterVolumePlugin())
            return master->getVolumeDb();
    return 0.0f;
}

juce::String FilePlayer::editStateXml() const
{
    return impl->edit != nullptr ? impl->edit->state.toXmlString() : juce::String();
}

juce::String FilePlayer::undoHistoryDescription() const
{
    if (impl->edit == nullptr)
        return {};
    auto& undo = impl->edit->getUndoManager();
    return undo.getUndoDescriptions().joinIntoString("|") + " pending=" + juce::String(undo.getNumActionsInCurrentTransaction());
}

bool FilePlayer::renderToWav(const juce::File& dest, double startSeconds, double lengthSeconds, double sampleRate)
{
    if (impl->edit == nullptr)
        return false;

    dest.deleteFile();

    te::Renderer::Parameters params(*impl->edit);
    params.destFile = dest;
    params.tracksToDo = te::toBitSet(te::getAllTracks(*impl->edit));
    params.audioFormat = impl->engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = 512;
    params.canRenderInMono = false;
    params.time = tracktion::TimeRange(tracktion::TimePosition::fromSeconds(startSeconds),
                                       tracktion::TimeDuration::fromSeconds(lengthSeconds));

    return te::Renderer::renderToFile("Sampler render", params).existsAsFile();
}

juce::String FilePlayer::describeTransport() const
{
    if (impl->edit == nullptr)
        return "no edit";
    auto& t = impl->edit->getTransport();
    return juce::String("playing=") + (t.isPlaying() ? "1" : "0") + " stopping=" + (t.isStopping() ? "1" : "0")
           + " context=" + (t.isPlayContextActive() ? "1" : "0") + " pos=" + juce::String(t.getPosition().inSeconds(), 3)
           + " cpu=" + juce::String(impl->engine.getDeviceManager().getCpuUsage(), 3);
}
} // namespace sampler
