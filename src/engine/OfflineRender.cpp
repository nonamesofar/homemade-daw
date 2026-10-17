#include "OfflineRender.h"

#include "Arrangement.h"
#include "EngineSetup.h"
#include "ProjectEdit.h"
#include "WarpService.h"

namespace te = tracktion::engine;

namespace sampler::engine
{
bool renderToWav(ProjectEdit& project, const juce::File& destination, const RenderOptions& options, juce::String& error)
{
    auto& edit = project.tracktionEdit();
    auto& arrangement = project.arrangement();

    const auto endBeat = arrangement.endBeat();
    if (endBeat <= 0.0)
    {
        error = "The project has no clips to render.";
        return false;
    }

    // Reverse, Tones and Beats renders play silence until their file is complete: make the missing Beats renders and wait.
    auto& warp = project.warpService();
    warp.scan();
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(options.proxyTimeoutMs);
    while (! (warp.isIdle() && arrangement.allRendersReady()))
    {
        if (warp.isIdle() && warp.numFailures() > 0)
        {
            error = "A stretched version of a clip could not be made: " + warp.lastError();
            return false;
        }
        if (juce::Time::getMillisecondCounter() > deadline)
        {
            error = "A clip did not finish rendering (reverse, Tones or Beats).";
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    destination.deleteFile();
    if (! destination.getParentDirectory().createDirectory())
    {
        error = "Cannot create " + destination.getParentDirectory().getFullPathName();
        return false;
    }

    te::Renderer::Parameters params(edit);
    params.destFile = destination;
    params.tracksToDo = te::toBitSet(te::getAllTracks(edit));
    params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 32;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = options.blockSize;
    params.canRenderInMono = false;
    params.useMasterPlugins = true; // includes the master volume
    params.ditheringEnabled = false;
    params.time = tracktion::TimeRange(tracktion::TimePosition(),
                                       tracktion::TimePosition::fromSeconds(arrangement.beatsToSeconds(endBeat)));

    // A clip whose sound file is missing stays silent: it is left out (the offline renderer would wait for its data).
    juce::Array<te::Clip*> playable;
    bool anyMissing = false;
    for (auto* track : te::getAudioTracks(edit))
        for (auto* clip : track->getClips())
        {
            auto* wave = dynamic_cast<te::WaveAudioClip*>(clip);
            if (wave != nullptr && ! wave->getSourceFileReference().getFile().existsAsFile())
                anyMissing = true;
            else
                playable.add(clip);
        }
    if (anyMissing)
    {
        if (playable.isEmpty())
        {
            error = "Every clip's sound file is missing.";
            return false;
        }
        params.allowedClips = playable;
    }

    // The metronome is never part of a render.
    const bool clickWasOn = edit.clickTrackEnabled.get();
    edit.clickTrackEnabled = false;
    const auto written = te::Renderer::renderToFile("Sampler render", params);
    edit.clickTrackEnabled = clickWasOn;

    if (! written.existsAsFile())
    {
        destination.deleteFile();
        error = "The render failed.";
        return false;
    }
    return true;
}
} // namespace sampler::engine
