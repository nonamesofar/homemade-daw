// M2b: the recording controller with a fake System source and a scripted transport (no audio device).
#include "FakeCapture.h"
#include "arrange/ArrangeFixture.h"

#include "app/RecordingController.h"
#include "app/RecordingPolicy.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::app::RecordingController;
using sampler::test::ArrangeFixture;
using sampler::test::FakeCaptureSource;
namespace cmd = sampler::commands;

namespace
{
struct Rig
{
    explicit Rig(const juce::String& name) : f(name)
    {
        script.clockMs = [this] { return clock; };
        sampler::app::RecordingEnv env;
        env.arrangement = [this] { return &f.arrangement(); };
        env.commands = [this] { return f.commands.get(); };
        env.bundleDir = [this] { return f.project->bundleDir(); };
        env.isPlaying = [this] { return playing; };
        env.isPlayPending = [] { return false; };
        env.positionBeats = [this] { return beat; };
        env.play = [this]
        {
            ++playCalls;
            mutedAtPlay = muted;
            playing = true;
        };
        env.setOutputMuted = [this](bool m)
        {
            muted = m;
            mutedHistory.add(m ? "mute" : "unmute");
        };
        env.outputDeviceName = [this] { return outputName; };
        env.outputDeviceType = [this] { return outputType; };
        env.setLocateLocked = [this](bool locked)
        {
            locateLocked = locked;
            locateHistory.add(locked ? "lock" : "unlock");
        };
        env.toast = [this](const juce::String& text, bool) { toasts.add(text); };
        env.takeRecorder = [this] { return static_cast<sampler::engine::TakeRecorder*>(&recorder); };
        env.makeCapture = [this]
        {
            auto fake = std::make_unique<FakeCaptureSource>(script);
            made.push_back(fake.get());
            return std::unique_ptr<sampler::platform::ICaptureSource>(std::move(fake));
        };
        env.listDevices = []
        { return std::vector<sampler::platform::PlaybackDevice>{{"dev-a", "Fake Speakers", true}, {"dev-b", "Fake Headset", false}}; };
        env.clockMs = [this] { return clock; };
        env.wallClock = [this] { return wall; };
        envCopy = env;
        f.commands->onRefused = [this](const juce::String& m) { toasts.add(m); };
    }

    void make()
    {
        envCopy.maxSeconds = maxSeconds;
        rc = std::make_unique<RecordingController>(envCopy);
    }

    /** One step: audio goes into the fake source, time and the playhead move on, the controller ticks. */
    void advance(double seconds, bool tone)
    {
        for (int i = 0; i < static_cast<int>(seconds * 10.0 + 0.5); ++i)
        {
            if (! made.empty() && alive)
                made.back()->push(0.1, tone);
            for (auto* tap : recorder.taps)
                if (alive && ! tap->detached)
                    tap->push(0.1, tone, firstTapSeconds >= 0.0 ? firstTapSeconds : beat * 0.5);
            clock += 100.0;
            if (playing)
                beat += 0.2; // 120 BPM
            rc->tick();
        }
    }

    void settle()
    {
        for (int i = 0; i < 400 && rc->state() != RecordingController::State::idle; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        alive = false;
    }

    ArrangeFixture f;
    sampler::test::FakeTakeRecorder recorder;
    FakeCaptureSource::Script script;
    sampler::app::RecordingEnv envCopy;
    std::unique_ptr<RecordingController> rc;
    std::vector<FakeCaptureSource*> made;
    juce::StringArray toasts, mutedHistory, locateHistory;
    juce::Time wall{2026, 9, 12, 10, 15, 0};
    bool mutedAtPlay = false, locateLocked = false;
    juce::String outputType = "Windows Audio";
    double clock = 1000.0, beat = 0.0, maxSeconds = 1800.0, firstTapSeconds = -1.0;
    bool playing = false, muted = false, alive = true;
    int playCalls = 0;
    juce::String outputName = "Other Device";
};

bool anyToastContains(const juce::StringArray& toasts, const juce::String& text)
{
    for (const auto& t : toasts)
        if (t.contains(text))
            return true;
    return false;
}
} // namespace

TEST_CASE("a take from bar 5 with 2 s of leading silence is placed 2 s later, minus the latency", "[app][record]")
{
    Rig r("rc-placement");
    r.make();
    r.script.latencySeconds = 0.05;
    r.beat = 16.0; // bar 5
    r.rc->armState().arm(r.f.trackId(0));

    REQUIRE(r.rc->start());
    CHECK(r.playCalls == 1); // stopped: starting a recording starts the song
    r.advance(2.0, false);
    r.advance(3.0, true);
    r.rc->stop();
    r.settle();

    auto clips = r.f.clipsOn(0);
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == Approx(16.0 + (2.0 - 0.005 - 0.05) * 2.0).margin(0.02));
    CHECK(clips[0].lengthSeconds == Approx(3.01).margin(0.02));
    CHECK(r.f.commands->undoName() == "Record take");

    const auto source = sampler::model::SamplerState(r.f.project->samplerState()).findSourceById(clips[0].sourceId);
    REQUIRE(source.isValid());
    CHECK(source.category() == sampler::model::Category::recording);
    CHECK(source.original().startsWith("audio/recordings/rec-"));
    CHECK(r.f.project->bundleDir().getChildFile(source.original()).existsAsFile());
    CHECK(r.playing); // stopping the recording leaves the song running
    CHECK_FALSE(r.f.commands->isRecordingLocked());
}

TEST_CASE("nothing armed: a message and no playback", "[app][record]")
{
    Rig r("rc-nothing-armed");
    r.make();
    CHECK_FALSE(r.rc->start());
    CHECK(r.playCalls == 0);
    CHECK(r.made.empty());
    CHECK(anyToastContains(r.toasts, "Arm a track"));
}

TEST_CASE("a track cannot record another armed track", "[app][record]")
{
    Rig r("rc-chained");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "track:" + r.f.trackId(0)));
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    CHECK_FALSE(r.rc->start());
    CHECK(r.playCalls == 0);
    CHECK(anyToastContains(r.toasts, "cannot record another armed track"));
}

TEST_CASE("during a recording a delete is refused and a volume change goes through", "[app][record]")
{
    Rig r("rc-lock");
    r.make();
    const auto sound = r.f.addSound("pad", 4.0);
    const auto clip = r.f.place(sound, 0, 0.0);
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(0.5, true);

    r.f.commands->apply(std::make_unique<cmd::DeleteClips>(std::vector<juce::String>{clip}));
    CHECK(r.f.arrangement().clip(clip).has_value());
    CHECK(anyToastContains(r.toasts, "Stop recording first"));

    r.f.commands->apply(std::make_unique<cmd::SetTrackParam>(r.f.trackId(0), cmd::SetTrackParam::Control::volume, -6.0));
    CHECK(r.f.arrangement().tracks()[0].volumeDb == Approx(-6.0).margin(0.1));

    r.toasts.clear();
    r.f.commands->undo();
    CHECK(anyToastContains(r.toasts, "Stop recording first"));

    r.rc->stop();
    r.settle();
    CHECK_FALSE(r.f.commands->isRecordingLocked());
}

TEST_CASE("a silent take adds nothing and names the track", "[app][record]")
{
    Rig r("rc-silent");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(2.0, false);
    r.rc->stop();
    r.settle();
    CHECK(r.f.arrangement().numClips() == 0);
    CHECK(anyToastContains(r.toasts, "Audio 1 was silent"));
    CHECK(r.f.project->bundleDir().getChildFile("audio/recordings").findChildFiles(juce::File::findFiles, false).isEmpty());
}

TEST_CASE("pressing Stop ends the recording with the playback", "[app][record]")
{
    Rig r("rc-stop");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(1.0, true);
    r.playing = false; // the user pressed Stop
    r.rc->tick();
    r.rc->tick();
    r.settle();
    CHECK(r.f.arrangement().numClips() == 1);
}

TEST_CASE("the 30-minute limit finishes the recording and playback continues", "[app][record]")
{
    Rig r("rc-limit");
    r.maxSeconds = 2.0;
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(3.0, true);
    r.settle();
    REQUIRE(r.f.clipsOn(0).size() == 1);
    CHECK(r.f.clipsOn(0)[0].lengthSeconds == Approx(2.0).margin(0.05));
    CHECK(anyToastContains(r.toasts, "limit"));
    CHECK(r.playing);
}

TEST_CASE("a device that goes away keeps what was recorded", "[app][record]")
{
    Rig r("rc-device-lost");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(2.0, true);
    r.made.back()->loseDevice();
    r.rc->tick();
    r.settle();
    CHECK(r.f.arrangement().numClips() == 1);
    CHECK(anyToastContains(r.toasts, "went away"));
}

TEST_CASE("a write failure keeps what was recorded and says why", "[app][record]")
{
    Rig r("rc-write-failed");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(2.0, true);
    r.made.back()->breakWriting();
    r.rc->tick();
    r.settle();
    CHECK(r.f.arrangement().numClips() == 1);
    CHECK(anyToastContains(r.toasts, "disk"));
}

TEST_CASE("a System source that cannot start aborts everything", "[app][record]")
{
    Rig r("rc-start-failure");
    r.make();
    r.script.startFailure = "device busy";
    r.rc->armState().arm(r.f.trackId(0));
    CHECK_FALSE(r.rc->start());
    CHECK(r.playCalls == 0);
    CHECK(r.rc->state() == RecordingController::State::idle);
    CHECK(anyToastContains(r.toasts, "device busy"));
    CHECK(anyToastContains(r.toasts, "Fake Speakers"));
    CHECK(r.mutedHistory.isEmpty());
    CHECK_FALSE(r.f.commands->isRecordingLocked());
}

TEST_CASE("the output is muted only while an endpoint take records the device Sampler plays to", "[app][record]")
{
    SECTION("same device in endpoint mode: muted during, restored after")
    {
        Rig r("rc-mute-same");
        r.script.mode = "endpoint-loopback";
        r.outputName = "Fake Speakers";
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        CHECK(r.muted);
        r.advance(1.0, true);
        r.rc->stop();
        CHECK_FALSE(r.muted);
        r.settle();
    }
    SECTION("process loopback works: never muted")
    {
        Rig r("rc-mute-process");
        r.outputName = "Fake Speakers";
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        CHECK_FALSE(r.muted);
        r.rc->stop();
        r.settle();
        CHECK(r.mutedHistory.isEmpty());
    }
    SECTION("destroying the controller mid-recording restores the output")
    {
        Rig r("rc-mute-destroy");
        r.script.mode = "endpoint-loopback";
        r.outputName = "Fake Speakers";
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        CHECK(r.muted);
        r.rc.reset();
        CHECK_FALSE(r.muted);
        CHECK_FALSE(r.f.commands->isRecordingLocked());
    }
}

TEST_CASE("the mute decision table", "[app][record]")
{
    using sampler::app::RecordingPolicy;
    const juce::String wasapi = "Windows Audio";
    CHECK_FALSE(RecordingPolicy::shouldMuteOutput({}, "Speakers", wasapi));
    CHECK_FALSE(RecordingPolicy::shouldMuteOutput({{false, "Speakers"}}, "Speakers", wasapi)); // process loopback leaves us out
    CHECK(RecordingPolicy::shouldMuteOutput({{true, "Speakers"}}, "Speakers", wasapi));
    CHECK(RecordingPolicy::shouldMuteOutput({{true, "Speakers (Realtek Audio)"}}, "speakers", "DirectSound"));
    CHECK_FALSE(RecordingPolicy::shouldMuteOutput({{true, "Headset"}}, "Speakers", wasapi)); // clearly another device
    CHECK_FALSE(RecordingPolicy::shouldMuteOutput({{true, "Headset"}}, "Speakers", "DirectSound"));
    // On doubt it mutes: ASIO or an unknown driver type, an unknown name.
    CHECK(RecordingPolicy::shouldMuteOutput({{true, "Headset"}}, "Speakers", "ASIO"));
    CHECK(RecordingPolicy::shouldMuteOutput({{true, "Headset"}}, "Speakers", ""));
    CHECK(RecordingPolicy::shouldMuteOutput({{true, "Headset"}}, "", wasapi));
    CHECK(RecordingPolicy::shouldMuteOutput({{true, ""}}, "Speakers", wasapi));
}

TEST_CASE("tracks on one device share one capture and one sound", "[app][record]")
{
    Rig r("rc-shared-device");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    REQUIRE(r.rc->start());
    CHECK(r.made.size() == 1);
    r.advance(1.0, true);
    r.rc->stop();
    r.settle();
    REQUIRE(r.f.clipsOn(0).size() == 1);
    REQUIRE(r.f.clipsOn(1).size() == 1);
    CHECK(r.f.clipsOn(0)[0].sourceId == r.f.clipsOn(1)[0].sourceId);
    CHECK(r.f.commands->undoName() == "Record 2 takes");
}

TEST_CASE("System and Master takes of one recording land on the same start beat", "[app][record]")
{
    Rig r("rc-system-master");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "master"));
    r.beat = 8.0; // bar 3
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    REQUIRE(r.rc->start());
    r.advance(3.0, true);
    r.rc->stop();
    r.settle();

    REQUIRE(r.f.clipsOn(0).size() == 1);
    REQUIRE(r.f.clipsOn(1).size() == 1);
    CHECK(r.f.clipsOn(0)[0].start == Approx(8.0).margin(0.1));
    CHECK(r.f.clipsOn(1)[0].start == Approx(8.0).margin(0.1));
    CHECK(r.f.commands->undoName() == "Record 2 takes");
    const auto master = sampler::model::SamplerState(r.f.project->samplerState()).findSourceById(r.f.clipsOn(1)[0].sourceId);
    CHECK(master.recordedFrom() == "Master");

    r.f.commands->undo(); // both takes go in one step
    CHECK(r.f.arrangement().numClips() == 0);
}

TEST_CASE("a track recording another track tells where it came from", "[app][record]")
{
    Rig r("rc-track-input");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "track:" + r.f.trackId(0)));
    r.rc->armState().arm(r.f.trackId(1));
    REQUIRE(r.rc->start());
    REQUIRE(r.recorder.tracks.size() == 1);
    CHECK(r.recorder.tracks[0] == r.f.trackId(0));
    r.advance(1.0, true);
    r.rc->stop();
    r.settle();
    REQUIRE(r.f.clipsOn(1).size() == 1);
    const auto source = sampler::model::SamplerState(r.f.project->samplerState()).findSourceById(r.f.clipsOn(1)[0].sourceId);
    CHECK(source.recordedFrom() == r.f.arrangement().tracks()[0].name);
}

TEST_CASE("the song waits for the taps before it starts", "[app][record]")
{
    Rig r("rc-tap-wait");
    r.recorder.readyAtOnce = false;
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    CHECK(r.playCalls == 0);
    r.advance(0.3, false);
    CHECK(r.playCalls == 0);
    r.recorder.taps[0]->ready = true;
    r.advance(0.1, false);
    CHECK(r.playCalls == 1);
    r.rc->abortNow();
}

TEST_CASE("a tap that cannot be added stops the whole recording from starting", "[app][record]")
{
    Rig r("rc-tap-failure");
    r.recorder.failure = "no graph";
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    CHECK_FALSE(r.rc->start());
    CHECK(r.playCalls == 0);
    CHECK(anyToastContains(r.toasts, "no graph"));
    CHECK(r.f.project->bundleDir().getChildFile("audio/recordings").findChildFiles(juce::File::findFiles, false).isEmpty());
    CHECK_FALSE(r.f.commands->isRecordingLocked());
}

TEST_CASE("a take that runs across a loop is one continuous clip", "[app][record]")
{
    Rig r("rc-loop");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    for (int i = 0; i < 80; ++i) // 8 seconds; the playhead wraps every 2 s
    {
        r.recorder.taps[0]->push(0.1, true, 0.0);
        r.clock += 100.0;
        r.beat = std::fmod(r.beat + 0.2, 4.0);
        r.rc->tick();
    }
    r.rc->stop();
    r.settle();
    REQUIRE(r.f.clipsOn(0).size() == 1);
    CHECK(r.f.clipsOn(0)[0].lengthSeconds == Approx(8.0).margin(0.05));
    CHECK(r.f.clipsOn(0)[0].start == Approx(0.0).margin(0.05));
}

TEST_CASE("a named device is recorded, a missing one falls back to the default device", "[app][record]")
{
    const auto recordWith = [](const juce::String& input) {
        Rig r("rc-device-choice");
        r.make();
        r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), input));
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        REQUIRE(r.made.size() == 1);
        const auto options = r.made[0]->options_;
        r.advance(0.5, true);
        r.rc->stop();
        r.settle();
        return options;
    };

    const auto named = recordWith("system:dev-b");
    CHECK(named.deviceId == "dev-b");
    CHECK(named.trimSilence);
    CHECK(named.maxSeconds == Approx(1800.0));
    CHECK(recordWith("system:gone").deviceId.isEmpty());   // the headset is unplugged: the default device
    CHECK(recordWith("system:default").deviceId.isEmpty());
    CHECK(recordWith("system:default").excludeOwnProcess); // leaving Sampler out is asked for first
}

TEST_CASE("the developer flag makes the capture behave as if Sampler could not be left out", "[app][record]")
{
    Rig r("rc-no-exclusion");
    r.envCopy.simulateNoExclusion = true;
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    CHECK(r.made[0]->options_.simulateProcessLoopbackFailure);
    r.rc->stop();
    r.settle();
}

TEST_CASE("the no-signal warning shows after 3 seconds of silence and goes away with sound", "[app][record]")
{
    Rig r("rc-no-signal");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(2.8, false);
    REQUIRE(r.rc->liveTakes().size() == 1);
    CHECK_FALSE(r.rc->liveTakes()[0].noSignal);
    r.advance(0.4, false);
    CHECK(r.rc->liveTakes()[0].noSignal);
    CHECK(r.rc->liveTakes()[0].warning == "No sound is coming in (Fake Speakers)");
    r.advance(0.5, true);
    CHECK_FALSE(r.rc->liveTakes()[0].noSignal);
    CHECK(r.rc->liveTakes()[0].warning.isEmpty());
    CHECK(r.rc->liveTakes()[0].started);

    r.rc->stop();
    REQUIRE(r.rc->liveTakes().size() == 1); // still drawn while it is finished
    CHECK(r.rc->liveTakes()[0].finishing);
    CHECK(r.rc->liveTakes()[0].warning == "Finishing...");
    r.settle();
    CHECK(r.rc->liveTakes().empty());
}

TEST_CASE("lost parts are reported and the take is kept", "[app][record]")
{
    Rig r("rc-overrun");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.made.back()->holdWriter(true); // the disk cannot keep up
    r.advance(1.0, true);
    r.made.back()->push(41.0, true); // more than the ring holds
    r.made.back()->holdWriter(false);
    r.rc->stop();
    r.settle();
    CHECK(r.f.arrangement().numClips() == 1);
    CHECK(anyToastContains(r.toasts, "lost"));
}

//==============================================================================
// Review fixes.

TEST_CASE("a loopback device that sends no packets still gets the no-signal warning", "[app][record]")
{
    Rig r("rc-no-packets");
    r.script.deliverPackets = false;
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(2.8, false);
    REQUIRE(r.rc->liveTakes().size() == 1);
    CHECK_FALSE(r.rc->liveTakes()[0].noSignal);
    r.advance(0.4, false);
    CHECK(r.rc->liveTakes()[0].noSignal);
    CHECK(r.rc->liveTakes()[0].warning == "No sound is coming in (Fake Speakers)");
    r.rc->abortNow();
}

TEST_CASE("the limit also counts by the clock: a System take that hears nothing ends", "[app][record]")
{
    Rig r("rc-clock-limit");
    r.maxSeconds = 20.0;
    r.script.deliverPackets = false;
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(19.0, false);
    CHECK(r.rc->state() == RecordingController::State::recording);
    r.advance(1.5, false);
    CHECK(r.rc->state() != RecordingController::State::recording);
    r.settle();
    CHECK(anyToastContains(r.toasts, "limit"));
    CHECK(r.f.arrangement().numClips() == 0); // nothing was heard
    CHECK(r.playing);
}

TEST_CASE("the clock limit counts leading silence too", "[app][record]")
{
    Rig r("rc-clock-limit-leading");
    r.maxSeconds = 20.0;
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(10.0, false);
    for (int i = 0; i < 150 && r.rc->state() == RecordingController::State::recording; ++i)
        r.advance(0.1, true); // the recording ends at 20 s on the clock: 10 s of sound
    r.settle();
    CHECK(anyToastContains(r.toasts, "limit"));
    REQUIRE(r.f.clipsOn(0).size() == 1);
    CHECK(r.f.clipsOn(0)[0].lengthSeconds == Approx(10.0).margin(0.3));
}

TEST_CASE("a System take after the playhead went back is placed where the sound really is", "[app][record]")
{
    Rig r("rc-wrap-placement");
    r.make();
    r.beat = 8.0; // 4 s
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(1.0, false);
    r.beat = 0.0; // the loop wrapped
    r.advance(1.0, false);
    r.advance(1.0, true);
    r.rc->stop();
    r.settle();
    REQUIRE(r.f.clipsOn(0).size() == 1);
    CHECK(r.f.clipsOn(0)[0].start == Approx(1.9).margin(0.15)); // about 1 s after the wrap, not at the record start
}

TEST_CASE("Stop while the song waits for the taps ends the recording and the song never starts", "[app][record]")
{
    SECTION("the Stop button")
    {
        Rig r("rc-wait-stop");
        r.recorder.readyAtOnce = false;
        r.make();
        r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(0.3, false);
        r.rc->transportStopped();
        r.recorder.taps[0]->ready = true;
        r.advance(0.3, false);
        r.settle();
        CHECK(r.playCalls == 0);
        CHECK(r.rc->state() == RecordingController::State::idle);
        CHECK_FALSE(r.f.commands->isRecordingLocked());
    }
    SECTION("Stop gives a neutral message, no silent-take errors, and no files")
    {
        Rig r("rc-wait-cancel");
        r.recorder.readyAtOnce = false;
        r.make();
        r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(0.3, false);
        r.rc->transportStopped();
        r.settle();
        CHECK(anyToastContains(r.toasts, "Recording cancelled"));
        CHECK_FALSE(anyToastContains(r.toasts, "silent"));
        CHECK_FALSE(anyToastContains(r.toasts, "not added"));
        CHECK_FALSE(anyToastContains(r.toasts, "never started"));
        CHECK(r.f.arrangement().numClips() == 0);
        CHECK(r.f.project->bundleDir().getChildFile("audio/recordings").findChildFiles(juce::File::findFiles, false).isEmpty());
    }
}

TEST_CASE("takes land as their own step even while a mixer gesture is open", "[app][record]")
{
    Rig r("rc-gesture");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(0.5, true);
    r.f.commands->begin("Volume");
    r.f.commands->update(std::make_unique<cmd::SetTrackParam>(r.f.trackId(0), cmd::SetTrackParam::Control::volume, -6.0));
    r.rc->stop();
    r.settle();
    CHECK_FALSE(r.f.commands->isGestureOpen());
    REQUIRE(r.f.clipsOn(0).size() == 1);
    CHECK(r.f.commands->undoName() == "Record take");
    r.f.commands->undo();
    CHECK(r.f.arrangement().numClips() == 0);
    CHECK(r.f.arrangement().tracks()[0].volumeDb == Approx(-6.0).margin(0.1)); // the volume step is still there
}

TEST_CASE("an aborted recording leaves no take files behind", "[app][record]")
{
    const auto recordingFiles = [](Rig& r)
    { return r.f.project->bundleDir().getChildFile("audio/recordings").findChildFiles(juce::File::findFiles, false); };

    SECTION("abortNow while recording")
    {
        Rig r("rc-abort-files");
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(0.5, true);
        r.rc->abortNow();
        CHECK(recordingFiles(r).isEmpty());
    }
    SECTION("destroying the controller while recording")
    {
        Rig r("rc-destroy-files");
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(0.5, true);
        r.alive = false;
        r.rc.reset();
        CHECK(recordingFiles(r).isEmpty());
    }
    SECTION("abortNow while the takes are being finished")
    {
        Rig r("rc-abort-finishing");
        r.make();
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(0.5, true);
        r.rc->stop();
        r.rc->abortNow();
        for (int i = 0; i < 20; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        CHECK(recordingFiles(r).isEmpty());
        CHECK(r.f.arrangement().numClips() == 0);
    }
}

TEST_CASE("abortNow while finishing destroys the takes at once, not in a later message", "[app][record]")
{
    Rig r("rc-abort-destroys");
    r.make();
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(0.5, true);
    const auto before = sampler::test::FakeTap::destroyedCount;
    r.rc->stop();
    r.rc->abortNow();
    CHECK(sampler::test::FakeTap::destroyedCount == before + 1); // while the edit is still alive
    CHECK(r.rc->state() == RecordingController::State::idle);
    for (int i = 0; i < 20; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    CHECK(sampler::test::FakeTap::destroyedCount == before + 1);
    CHECK(r.f.arrangement().numClips() == 0);
}

TEST_CASE("armed tracks are forgotten when the project goes away", "[app][record]")
{
    Rig r("rc-arms-cleared");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->armState().isAnyArmed());
    r.rc->abortNow(); // what closing a project does
    CHECK_FALSE(r.rc->armState().isAnyArmed());
}

TEST_CASE("system:default and the id of the default device are one capture", "[app][record]")
{
    Rig r("rc-default-device");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "system:default"));
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "system:dev-a")); // dev-a is the default device
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    REQUIRE(r.rc->start());
    CHECK(r.made.size() == 1);
    r.advance(1.0, true);
    r.rc->stop();
    r.settle();
    REQUIRE(r.f.clipsOn(0).size() == 1);
    REQUIRE(r.f.clipsOn(1).size() == 1);
    CHECK(r.f.clipsOn(0)[0].sourceId == r.f.clipsOn(1)[0].sourceId);
}

//==============================================================================
// Review fixes (W4, W5b, W5c, W7).

TEST_CASE("a System take does not keep what it heard while the song waited for the taps", "[app][record]")
{
    Rig r("rc-system-gate");
    r.recorder.readyAtOnce = false;
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    REQUIRE(r.rc->start());

    // One second of sound reaches the System device before the song can start; the Master tap hears nothing yet.
    for (int i = 0; i < 10; ++i)
    {
        r.made.back()->push(0.1, true);
        r.clock += 100.0;
        r.rc->tick();
    }
    CHECK(r.playCalls == 0);
    r.recorder.taps[0]->ready = true;
    r.advance(0.1, true); // the song starts on this tick
    CHECK(r.playCalls == 1);
    r.advance(2.0, true);
    r.rc->stop();
    r.settle();

    REQUIRE(r.f.clipsOn(0).size() == 1);
    REQUIRE(r.f.clipsOn(1).size() == 1);
    CHECK(r.f.clipsOn(0)[0].start == Approx(r.f.clipsOn(1)[0].start).margin(0.02)); // a few ms at 120 BPM
    CHECK(r.f.clipsOn(0)[0].start == Approx(0.0).margin(0.02));
    CHECK(r.f.clipsOn(0)[0].lengthSeconds == Approx(2.1).margin(0.15)); // not 3.1 s
}

TEST_CASE("a sample rate change during a Master take ends the recording and keeps the take", "[app][record]")
{
    Rig r("rc-rate-change");
    r.make();
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(1.0, true);
    r.recorder.taps[0]->rateChanged = true;
    r.rc->tick();
    CHECK(r.rc->state() == RecordingController::State::finishing);
    r.settle();
    CHECK(anyToastContains(r.toasts, "The audio device changed while recording; the take so far was kept"));
    CHECK(r.f.clipsOn(0).size() == 1);
}

TEST_CASE("the playhead is locked from the start of a recording until it ends", "[app][record]")
{
    Rig r("rc-locate-lock");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    CHECK(r.locateLocked);
    r.advance(1.0, true);
    CHECK(r.locateLocked);
    r.rc->stop();
    CHECK_FALSE(r.locateLocked); // nothing grows any more: Stop may return the playhead
    r.settle();
    CHECK_FALSE(r.locateLocked);
}

TEST_CASE("the output is muted before the song starts", "[app][record]")
{
    Rig r("rc-mute-first");
    r.script.mode = "endpoint-loopback";
    r.outputName = "Fake Speakers";
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    CHECK(r.playCalls == 1);
    CHECK(r.mutedAtPlay);
    r.rc->abortNow();
}

TEST_CASE("takes are named and dated by the start of the recording, and told apart", "[app][record]")
{
    Rig r("rc-start-time");
    r.make();
    cmd::AddTrack add;
    r.f.commands->apply(add);
    r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(1), "master"));
    r.rc->armState().arm(r.f.trackId(0));
    r.rc->armState().toggle(r.f.trackId(1));
    REQUIRE(r.rc->start());
    r.advance(1.0, true);
    r.wall = juce::Time(2026, 9, 12, 10, 40, 30); // the recording ends much later
    r.rc->stop();
    r.settle();

    const auto a = r.f.clipsOn(0), b = r.f.clipsOn(1);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].name.contains("2026-10-12 10:15"));
    CHECK(b[0].name.contains("2026-10-12 10:15"));
    CHECK(a[0].name != b[0].name);
    const auto state = sampler::model::SamplerState(r.f.project->samplerState());
    CHECK(state.findSourceById(a[0].sourceId).recordedAt() == "2026-10-12T10:15:00");
}

TEST_CASE("a take that was not written says why instead of 'silent'", "[app][record]")
{
    SECTION("the disk failed")
    {
        Rig r("rc-reason-disk");
        r.make();
        r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.recorder.taps[0]->failWrites();
        r.advance(1.0, true);
        r.settle();
        CHECK(r.f.arrangement().numClips() == 0);
        CHECK(anyToastContains(r.toasts, "could not be written"));
        CHECK_FALSE(anyToastContains(r.toasts, "was silent"));
    }
    SECTION("the tap never got into the graph")
    {
        Rig r("rc-reason-tap");
        r.recorder.readyAtOnce = false;
        r.make();
        r.f.commands->apply(std::make_unique<cmd::SetTrackInput>(r.f.trackId(0), "master"));
        r.rc->armState().arm(r.f.trackId(0));
        REQUIRE(r.rc->start());
        r.advance(1.3, false); // the song starts after the wait runs out, though the tap never got in
        r.rc->stop();
        r.settle();
        CHECK(anyToastContains(r.toasts, "never started recording"));
        CHECK_FALSE(anyToastContains(r.toasts, "was silent"));
    }
}

TEST_CASE("Record pressed while the recording is finishing says so", "[app][record]")
{
    Rig r("rc-finishing-toast");
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    r.advance(1.0, true);
    r.rc->stop();
    REQUIRE(r.rc->state() == RecordingController::State::finishing);
    r.toasts.clear();
    r.rc->toggleRecord();
    CHECK(anyToastContains(r.toasts, "Finishing the recording"));
    CHECK_FALSE(r.rc->start());
    r.settle();
    CHECK(r.f.arrangement().numClips() == 1);
}

TEST_CASE("Record is refused while the app says it cannot start", "[app][record]")
{
    Rig r("rc-start-refusal");
    r.envCopy.startRefusal = [] { return juce::String("Wait for the import to finish"); };
    r.make();
    r.rc->armState().arm(r.f.trackId(0));
    CHECK_FALSE(r.rc->start());
    CHECK(anyToastContains(r.toasts, "Wait for the import to finish"));
    CHECK(r.made.empty());
    CHECK(r.playCalls == 0);
    CHECK_FALSE(r.f.commands->isRecordingLocked());
}

TEST_CASE("undo and redo are not available during a recording", "[app][record]")
{
    Rig r("rc-undo-lock");
    r.make();
    r.f.commands->apply(std::make_unique<cmd::SetTrackParam>(r.f.trackId(0), cmd::SetTrackParam::Control::volume, -6.0));
    CHECK(r.f.commands->canUndo());
    r.rc->armState().arm(r.f.trackId(0));
    REQUIRE(r.rc->start());
    CHECK_FALSE(r.f.commands->canUndo());
    CHECK_FALSE(r.f.commands->canRedo());
    r.rc->abortNow();
    CHECK(r.f.commands->canUndo());
}
