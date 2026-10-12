#pragma once

#include "commands/CommandService.h"
#include "engine/Arrangement.h"
#include "engine/TakeRecorder.h"
#include "platform/ICaptureSource.h"
#include "ui/arrange/ArmState.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace sampler::app
{
/** What the controller needs from the rest of the app, as functions so that tests can replace every one of them. */
struct RecordingEnv
{
    std::function<engine::Arrangement*()> arrangement;
    std::function<commands::CommandService*()> commands;
    std::function<juce::File()> bundleDir;

    // The transport.
    std::function<bool()> isPlaying;
    std::function<bool()> isPlayPending;
    std::function<double()> positionBeats;
    std::function<void()> play;
    /** Gives the project its playback context, so that the taps are in the graph before the song starts (optional). */
    std::function<void()> prepareForPlayback;

    /** Mutes or restores Sampler's own output (below the edit; not an undo step). */
    std::function<void(bool)> setOutputMuted;
    /** Name and driver type ("Windows Audio", "DirectSound", "ASIO") of the device Sampler plays to (empty if unknown). */
    std::function<juce::String()> outputDeviceName;
    std::function<juce::String()> outputDeviceType;
    /** The user may not move the playhead while a recording runs (optional; loop wraps are not affected). */
    std::function<void(bool)> setLocateLocked;
    /** Non-empty (with the text for the user) when a recording may not start now, e.g. an import is running (optional). */
    std::function<juce::String()> startRefusal;
    std::function<void(const juce::String& text, bool isError)> toast;
    /** The taps of Master and track outputs (null while the engine side is not available). */
    std::function<engine::TakeRecorder*()> takeRecorder;

    std::function<std::unique_ptr<platform::ICaptureSource>()> makeCapture = [] { return platform::createCaptureSource(); };
    std::function<std::vector<platform::PlaybackDevice>()> listDevices = [] { return platform::listPlaybackDevices(); };
    /** A monotonic clock in milliseconds, in the domain of CaptureStats::startedAtMs. */
    std::function<double()> clockMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    std::function<juce::Time()> wallClock = [] { return juce::Time::getCurrentTime(); };

    double maxSeconds = 1800.0;
    /** Developer flag --capture-no-exclusion: behave as if process loopback were unavailable. */
    bool simulateNoExclusion = false;
    /** The clips that a finished recording added (for the selection). */
    std::function<void(const std::vector<juce::String>& clipIds)> onTakesAdded;
};

/**
    Records onto the armed tracks (design D8). Message thread only. States: idle -> recording -> finishing -> idle.
    A recording has one take per source: System audio per device, Master, or a track's output. When it ends the takes
    are finished on a worker and then added in one `AddTakes` command.

    While a recording runs or is being finished, the command service refuses everything except mixer commands.
*/
class RecordingController : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    enum class State
    {
        idle,
        recording,
        finishing
    };

    explicit RecordingController(RecordingEnv);
    ~RecordingController() override;

    State state() const { return currentState; }
    bool isRecording() const { return currentState == State::recording; }
    bool isActive() const { return currentState != State::idle; }

    ui::ArmState& armState() { return arms; }
    const ui::ArmState& armState() const { return arms; }

    /** The Record button / Mod+R: starts when idle, ends the recording while it runs. */
    void toggleRecord();
    /** Starts a recording on the armed tracks. False (with a message) if it did not start. */
    bool start();
    /** Ends the recording: the takes are finished and added; playback goes on. */
    void stop();
    /**
        Ends everything at once and adds nothing (the project is going away); the take files are deleted and the armed
        tracks forgotten (track ids repeat across projects). Blocks while the files are closed.
    */
    void abortNow();
    /** The Stop button was pressed: a recording that still waits to start the song ends (nothing plays). */
    void transportStopped();
    /** Runs `done` once nothing is recording (at once when idle): ends a running recording first. */
    void finishThen(std::function<void()> done);

    /** One frame of work (a timer calls it while active; tests call it directly). */
    void tick();

    using LiveTake = ui::LiveTake;
    /** A take that is growing, for the display (empty unless recording). */
    std::vector<LiveTake> liveTakes() const;
    /** The playhead where the recording began, in seconds. */
    double recordStartSeconds() const { return startSeconds; }

private:
    struct Slot;

    void timerCallback() override { tick(); }
    bool fail(const juce::String& text);
    void toast(const juce::String& text, bool isError) const;
    void beginFinishing(const juce::String& reasonMessage, bool reasonIsError);
    void finalise(std::vector<std::unique_ptr<Slot>> slots, juce::String reasonMessage, bool reasonIsError, bool cancelled);
    /** Joins the worker and destroys the takes of a finish that is on its way (files deleted), while the edit is alive. */
    void discardInFlight();
    void resolveStart(Slot&);
    double transportSecondsAt(double clockMsValue) const;
    void abortStartedSlots(std::vector<std::unique_ptr<Slot>>&);
    void releaseLocks();
    /** Starts the song (the recording asked for it) and drops what System takes heard before it. */
    void startSong();

    RecordingEnv env;
    ui::ArmState arms;
    State currentState = State::idle;
    std::vector<std::unique_ptr<Slot>> slots;
    std::vector<std::pair<double, double>> clockPairs; // (clock ms, timeline seconds) while the transport plays
    double startSeconds = 0.0;
    bool sawPlaying = false;
    std::vector<LiveTake> finishingTakes; // what was growing when the recording ended
    bool playWhenTapsReady = false; // the song waits until the engine taps are in its graph
    double waitingSinceMs = 0.0;
    double recordStartMs = 0.0;   // clock when the recording began, for the 30-minute limit
    juce::Time recordStartWall;   // wall clock when the recording began: names the takes
    double recordStartBeats = 0.0; // the playhead when the recording began
    int stoppedTicks = 0;
    int generation = 0; // bumped by abortNow so that a finish still on its way is dropped
    bool outputMutedByUs = false;
    std::thread worker;
    /** The takes a finishing worker is closing; owned here so that abortNow and the destructor can destroy them in time. */
    std::shared_ptr<std::vector<std::unique_ptr<Slot>>> inFlight;
    std::vector<std::function<void()>> afterFinish;

    JUCE_DECLARE_WEAK_REFERENCEABLE(RecordingController)
};
} // namespace sampler::app
