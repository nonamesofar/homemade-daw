// M2b tests that need a real playback device (label hardware: CI leaves them out): the real WASAPI source behind the
// recording controller, with a tone played by JUCE on the default device.
#include "arrange/ArrangeFixture.h"

#include "app/AppController.h"
#include "commands/ArrangeCommands.h"
#include "app/RecordingController.h"
#include "engine/Transport.h"
#include "platform/ICaptureSource.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cmath>

using sampler::app::RecordingController;
using sampler::test::ArrangeFixture;

namespace
{
class ToneCallback : public juce::AudioIODeviceCallback
{
public:
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = 0.2f * static_cast<float>(std::sin(phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / rate;
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr)
                    out[c][i] = s;
        }
    }

private:
    double rate = 44100.0;
    double phase = 0.0;
};

void runFor(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

/** Highest peak found in any 0.25 s window of the first channel. */
float loudestWindow(const juce::File& wav)
{
    double rate = 0.0;
    const auto b = sampler::test::readWav(wav, &rate);
    float loudest = 0.0f;
    const auto step = static_cast<int>(rate / 4.0);
    for (int start = 0; start + step <= b.getNumSamples(); start += step)
        loudest = std::max(loudest, b.getMagnitude(0, start, step));
    return loudest;
}

struct HardwareRig
{
    explicit HardwareRig(const juce::String& name) : f(name)
    {
        start = juce::Time::getMillisecondCounterHiRes();
        sampler::app::RecordingEnv env;
        env.arrangement = [this] { return &f.arrangement(); };
        env.commands = [this] { return f.commands.get(); };
        env.bundleDir = [this] { return f.project->bundleDir(); };
        env.isPlaying = [] { return true; };
        env.isPlayPending = [] { return false; };
        env.positionBeats = [this] { return (juce::Time::getMillisecondCounterHiRes() - start) / 500.0; };
        env.play = [] {};
        env.setOutputMuted = [](bool) {};
        env.outputDeviceName = [] { return juce::String(); };
        env.toast = [this](const juce::String& text, bool) { toasts.add(text); };
        env.takeRecorder = [] { return static_cast<sampler::engine::TakeRecorder*>(nullptr); };
        env.simulateNoExclusion = true; // the tone comes from this process: only the whole device hears it
        rc = std::make_unique<RecordingController>(env);
    }

    bool playTone()
    {
        if (! dm.initialiseWithDefaultDevices(0, 2).isEmpty())
            return false;
        if (dm.getCurrentAudioDevice() == nullptr)
            return false;
        dm.addAudioCallback(&tone);
        return true;
    }

    ~HardwareRig()
    {
        rc.reset();
        dm.removeAudioCallback(&tone);
    }

    ArrangeFixture f;
    juce::AudioDeviceManager dm;
    ToneCallback tone;
    juce::StringArray toasts;
    double start = 0.0;
    std::unique_ptr<RecordingController> rc;
};
} // namespace

TEST_CASE("the real source records a tone played on the default device onto an armed track", "[app][record][hardware]")
{
    HardwareRig r("rec-hw-default");
    if (sampler::platform::listPlaybackDevices().empty() || ! r.playTone())
        SKIP("no playback device");

    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    runFor(3500);
    r.rc->stop();
    for (int i = 0; i < 300 && r.rc->isActive(); ++i)
        runFor(20);

    INFO("messages: " << r.toasts.joinIntoString(" | "));
    REQUIRE(r.f.clipsOn(0).size() == 1);
    const auto clip = r.f.clipsOn(0)[0];
    const auto source = sampler::model::SamplerState(r.f.project->samplerState()).findSourceById(clip.sourceId);
    const auto file = r.f.project->bundleDir().getChildFile(source.decoded());
    CHECK(clip.lengthSeconds > 2.5);
    CHECK(clip.lengthSeconds < 4.0);
    CHECK(loudestWindow(file) > 0.05f); // the tone is in the take
}

TEST_CASE("the real source records the default device when it is chosen by its id", "[app][record][hardware]")
{
    HardwareRig r("rec-hw-by-id");
    if (sampler::platform::listPlaybackDevices().empty() || ! r.playTone())
        SKIP("no playback device");

    juce::String id;
    for (const auto& d : sampler::platform::listPlaybackDevices())
        if (d.isDefault)
            id = d.id;
    REQUIRE(id.isNotEmpty());
    r.f.commands->apply(std::make_unique<sampler::commands::SetTrackInput>(r.f.trackId(0), "system:" + id));

    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    runFor(2500);
    r.rc->stop();
    for (int i = 0; i < 300 && r.rc->isActive(); ++i)
        runFor(20);

    REQUIRE(r.f.clipsOn(0).size() == 1);
    const auto source = sampler::model::SamplerState(r.f.project->samplerState()).findSourceById(r.f.clipsOn(0)[0].sourceId);
    CHECK(loudestWindow(r.f.project->bundleDir().getChildFile(source.decoded())) > 0.05f);
}

//==============================================================================
// Sampler plays its own song through the engine while a track records System audio of the same device.
namespace
{
struct EngineRig
{
    explicit EngineRig(const juce::String& name, bool simulateNoExclusion, bool keepOutputOpen)
        : root(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name)),
          controller(makeOptions(simulateNoExclusion, keepOutputOpen))
    {
    }

    sampler::app::AppOptions makeOptions(bool simulateNoExclusion, bool keepOutputOpen)
    {
        root.deleteRecursively();
        sampler::app::AppOptions o;
        o.untitledRoot = root.getChildFile("untitled");
        o.captureNoExclusion = simulateNoExclusion;
        o.adjustRecordingEnv = [this, keepOutputOpen](sampler::app::RecordingEnv& env) {
            auto original = env.setOutputMuted;
            env.setOutputMuted = [this, original, keepOutputOpen](bool m) {
                sawMuted = sawMuted || m;
                if (! keepOutputOpen)
                    original(m); // the real mute
            };
            env.toast = [this](const juce::String& text, bool) { toasts.add(text); };
        };
        return o;
    }

    bool waitForAudio()
    {
        const auto end = juce::Time::getMillisecondCounter() + 15000;
        while (! controller.engine().isAudioReady() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        return controller.engine().isAudioReady();
    }

    void placeSong()
    {
        juce::Array<juce::File> files;
        files.add(juce::File(SAMPLER_FIXTURES_DIR).getChildFile("drums-loop-120.wav"));
        controller.importAndPlace(files, {0, false, 0.0});
        for (int i = 0; i < 400 && (controller.importer().isBusy() || controller.project().arrangement().numClips() == 0); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    /** Loudest 0.25 s window of the first take in the project, or -1 if none was added. */
    float loudestTake()
    {
        const auto clips = controller.project().arrangement().clips();
        float loudest = -1.0f;
        for (const auto& c : clips)
        {
            const auto source = sampler::model::SamplerState(controller.project().samplerState()).findSourceById(c.sourceId);
            if (source.category() == sampler::model::Category::recording)
                loudest = std::max(loudest, loudestWindow(controller.project().bundleDir().getChildFile(source.decoded())));
        }
        return loudest;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::File root;
    juce::StringArray toasts;
    bool sawMuted = false;
    sampler::app::AppController controller;
};

void recordTheSong(EngineRig& r, int milliseconds)
{
    r.controller.recording().armState().arm(r.controller.project().arrangement().tracks()[0].id);
    REQUIRE(r.controller.recording().start());
    runFor(milliseconds);
    r.controller.recording().stop();
    for (int i = 0; i < 400 && r.controller.recording().isActive(); ++i)
        runFor(20);
}
} // namespace

TEST_CASE("the song Sampler plays is left out of a System audio take", "[app][record][hardware]")
{
    EngineRig r("rec-hw-exclusion", false, false);
    if (sampler::platform::listPlaybackDevices().empty() || ! r.waitForAudio())
        SKIP("no playback device");
    r.placeSong();
    // Not a clip yet at the record position: the take goes after the song on the same track.
    r.controller.project().transport().locateBeats(0.0);
    recordTheSong(r, 3000);
    INFO("messages: " << r.toasts.joinIntoString(" | "));
    CHECK(r.controller.project().transport().positionBeats() > 2.0); // the song really played
    CHECK(r.loudestTake() < 0.0f);                                     // nothing of it is in a take: the take was silent and dropped
    CHECK_FALSE(r.sawMuted);                                          // and the song stayed audible
}

TEST_CASE("without exclusion the output is muted and the take still has no song", "[app][record][hardware]")
{
    EngineRig r("rec-hw-mute", true, false);
    if (sampler::platform::listPlaybackDevices().empty() || ! r.waitForAudio())
        SKIP("no playback device");
    r.placeSong();
    r.controller.project().transport().locateBeats(0.0);
    recordTheSong(r, 3000);
    INFO("messages: " << r.toasts.joinIntoString(" | "));
    CHECK(r.sawMuted);
    CHECK_FALSE(r.controller.engine().isOutputMuted()); // restored
    CHECK(r.loudestTake() < 0.0f);
}

TEST_CASE("without exclusion and without the mute the take does hold the song (so the checks above mean something)", "[app][record][hardware]")
{
    EngineRig r("rec-hw-nomute", true, true);
    if (sampler::platform::listPlaybackDevices().empty() || ! r.waitForAudio())
        SKIP("no playback device");
    r.placeSong();
    r.controller.project().transport().locateBeats(0.0);
    recordTheSong(r, 3000);
    INFO("messages: " << r.toasts.joinIntoString(" | "));
    CHECK(r.loudestTake() > 0.01f);
}

TEST_CASE("a Master take recorded live lines up with the grid and holds what the song played", "[app][record][hardware]")
{
    EngineRig r("rec-hw-grid", false, false);
    if (! r.waitForAudio())
        SKIP("no audio output");

    // A sound with a click at every half second (every beat at 120 BPM), imported at bar 1 of track 1.
    const auto sound = r.root.getChildFile("clicks.wav");
    sampler::test::writeTestWav(sound, 12.0);
    juce::Array<juce::File> files;
    files.add(sound);
    r.controller.importAndPlace(files, {0, false, 0.0});
    for (int i = 0; i < 400 && (r.controller.importer().isBusy() || r.controller.project().arrangement().numClips() == 0); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(r.controller.project().arrangement().numClips() == 1);

    auto& commands = r.controller.commandService();
    commands.apply(std::make_unique<sampler::commands::AddTrack>());
    const auto resample = r.controller.project().arrangement().tracks()[1].id;
    commands.apply(std::make_unique<sampler::commands::SetTrackInput>(resample, "master"));

    r.controller.project().transport().locateBeats(0.0);
    r.controller.recording().armState().arm(resample);
    REQUIRE(r.controller.recording().start());
    runFor(4300);
    r.controller.recording().stop();
    for (int i = 0; i < 400 && r.controller.recording().isActive(); ++i)
        runFor(20);

    const auto clips = r.controller.project().arrangement().clipsOnTrack(resample);
    INFO("messages: " << r.toasts.joinIntoString(" | "));
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].lengthSeconds > 3.5);
    const auto source = sampler::model::SamplerState(r.controller.project().samplerState()).findSourceById(clips[0].sourceId);
    double rate = 0.0;
    const auto take = sampler::test::readWav(r.controller.project().bundleDir().getChildFile(source.decoded()), &rate);
    REQUIRE(take.getNumSamples() > static_cast<int>(rate * 3.5));

    float peak = 0.0f;
    for (int i = 0; i < take.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(take.getSample(0, i)));
    REQUIRE(peak > 0.1f);

    // Where each click lies on the timeline: the clip start plus the click's frame in the take.
    int quiet = static_cast<int>(rate);
    std::vector<double> errors;
    for (int i = 0; i < take.getNumSamples(); ++i)
    {
        const bool loud = std::abs(take.getSample(0, i)) > 0.5f * peak;
        if (loud && quiet >= static_cast<int>(rate * 0.3))
        {
            const auto seconds = clips[0].startSeconds + i / rate;
            const auto nearest = std::round(seconds / 0.5) * 0.5;
            errors.push_back(std::abs(seconds - nearest));
        }
        quiet = loud ? 0 : quiet + 1;
    }
    REQUIRE(errors.size() >= 6);
    for (const auto e : errors)
        CHECK(e < 0.003); // within 3 ms of a beat
}
