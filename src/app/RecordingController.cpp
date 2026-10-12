#include "RecordingController.h"

#include "RecordingPolicy.h"
#include "commands/ArrangeCommands.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sampler::app
{
struct RecordingController::Slot
{
    enum class Kind
    {
        system,
        master,
        track
    };

    Kind kind = Kind::system;
    juce::String deviceId;       // System: the endpoint id, empty for the Windows default device
    juce::String sourceTrackId;  // Track
    juce::File file;
    std::unique_ptr<platform::ICaptureSource> capture;
    std::unique_ptr<engine::TakeTap> tap;
    std::vector<juce::String> trackIds; // the armed tracks that get this take
    double startSeconds = -1.0;
    double lastLoudMs = -1.0;        // System: clock time when sound was last seen arriving
    juce::int64 loudMark = -1;       // System: the writer's last-loud position then
    juce::String deviceName;
    int number = 0;                  // the n in the file name: unique within the recording
    bool tapWasReady = false;        // Master/track: the tap was in the graph and its file open when the take ended
    platform::CaptureStats stats;
    platform::TakeResult result;
    bool detached = false;

    const platform::TakeWriter& writer() const { return capture != nullptr ? capture->take() : tap->take(); }
};

namespace
{
juce::String isoTime(const juce::Time& t) { return t.formatted("%Y-%m-%dT%H:%M:%S"); }
} // namespace

RecordingController::RecordingController(RecordingEnv e) : env(std::move(e)) {}

RecordingController::~RecordingController()
{
    stopTimer();
    if (worker.joinable())
        worker.join();
    for (auto& slot : slots)
    {
        if (slot->tap != nullptr)
            slot->tap->detach();
        if (slot->capture != nullptr)
            slot->capture->stop();
        if (slot->tap != nullptr)
            slot->tap->finish();
        slot->file.deleteFile(); // an unfinished take is not kept
    }
    slots.clear();
    discardInFlight();
    releaseLocks();
}

void RecordingController::discardInFlight()
{
    if (worker.joinable())
        worker.join();
    if (inFlight == nullptr)
        return;
    for (auto& slot : *inFlight)
        slot->file.deleteFile();
    inFlight->clear(); // the takes (and the plugins inside them) die here, not in a message that arrives after the edit is gone
    inFlight.reset();
}

void RecordingController::toast(const juce::String& text, bool isError) const
{
    if (env.toast)
        env.toast(text, isError);
}

bool RecordingController::fail(const juce::String& text)
{
    toast(text, true);
    return false;
}

void RecordingController::releaseLocks()
{
    if (outputMutedByUs && env.setOutputMuted)
        env.setOutputMuted(false);
    outputMutedByUs = false;
    if (env.setLocateLocked)
        env.setLocateLocked(false);
    if (env.commands)
        if (auto* cs = env.commands())
            cs->setRecordingLock(false);
}

void RecordingController::toggleRecord()
{
    if (currentState == State::idle)
        start();
    else if (currentState == State::recording)
        stop();
    else
        toast("Finishing the recording...", false);
}

//==============================================================================
bool RecordingController::start()
{
    if (currentState == State::finishing)
    {
        toast("Finishing the recording...", false);
        return false;
    }
    if (currentState != State::idle)
        return false;
    if (env.startRefusal)
        if (const auto refusal = env.startRefusal(); refusal.isNotEmpty())
            return fail(refusal);
    auto* a = env.arrangement ? env.arrangement() : nullptr;
    auto* cs = env.commands ? env.commands() : nullptr;
    if (a == nullptr || cs == nullptr)
        return false;

    const auto tracks = a->tracks();
    std::vector<juce::String> order;
    for (const auto& t : tracks)
        order.push_back(t.id);
    arms.prune(order);
    const auto armed = arms.armedInOrder(order);
    if (armed.empty())
        return fail("Arm a track first: click R on the track you want to record onto");

    const auto trackById = [&tracks](const juce::String& id) -> const engine::TrackInfo*
    {
        for (const auto& t : tracks)
            if (t.id == id)
                return &t;
        return nullptr;
    };

    // Which source each armed track records; a track that records another armed track is refused.
    const auto devices = env.listDevices ? env.listDevices() : std::vector<platform::PlaybackDevice>{};
    std::vector<std::unique_ptr<Slot>> planned;
    for (const auto& id : armed)
    {
        const auto input = trackById(id)->input;
        Slot::Kind kind = Slot::Kind::system;
        juce::String deviceId, sourceTrack;
        if (input == "master")
            kind = Slot::Kind::master;
        else if (input.startsWith("track:") && trackById(input.fromFirstOccurrenceOf(":", false, false)) != nullptr)
        {
            kind = Slot::Kind::track;
            sourceTrack = input.fromFirstOccurrenceOf(":", false, false);
            if (arms.isArmed(sourceTrack))
                return fail("A track cannot record another armed track: disarm " + trackById(sourceTrack)->name + " or " + trackById(id)->name);
        }
        else if (input.startsWith("system:") && input != "system:default")
        {
            const auto wanted = input.fromFirstOccurrenceOf(":", false, false);
            // A device that is gone falls back to the default one; the default device is the same take as "system:default".
            if (std::any_of(devices.begin(), devices.end(), [&wanted](const auto& d) { return d.id == wanted && ! d.isDefault; }))
                deviceId = wanted;
        }

        Slot* slot = nullptr;
        for (auto& s : planned)
            if (s->kind == kind && s->deviceId == deviceId && s->sourceTrackId == sourceTrack)
                slot = s.get();
        if (slot == nullptr)
        {
            planned.push_back(std::make_unique<Slot>());
            slot = planned.back().get();
            slot->kind = kind;
            slot->deviceId = deviceId;
            slot->sourceTrackId = sourceTrack;
        }
        slot->trackIds.push_back(id);
    }

    // Files.
    const auto dir = env.bundleDir().getChildFile("audio").getChildFile("recordings");
    recordStartWall = env.wallClock();
    const auto stamp = recordStartWall.formatted("%Y%m%d-%H%M%S");
    int n = 1;
    for (auto& slot : planned)
    {
        juce::File file;
        do
        {
            slot->number = n;
            file = dir.getChildFile("rec-" + stamp + "-" + juce::String(n++) + ".wav");
        } while (file.exists());
        slot->file = file;
    }

    if (env.prepareForPlayback)
        env.prepareForPlayback();

    // System captures first: they can fail, and a failure must leave nothing behind.
    const auto deviceLabel = [&devices](const juce::String& id)
    {
        for (const auto& d : devices)
            if (id.isEmpty() ? d.isDefault : d.id == id)
                return d.name;
        return juce::String(id.isEmpty() ? "the default playback device" : "the playback device");
    };

    auto abortWith = [this, &planned](const juce::String& message)
    {
        abortStartedSlots(planned);
        return fail(message);
    };

    for (auto& slot : planned)
    {
        if (slot->kind != Slot::Kind::system)
            continue;
        slot->capture = env.makeCapture();
        platform::CaptureOptions options;
        options.excludeOwnProcess = true;
        options.simulateProcessLoopbackFailure = env.simulateNoExclusion;
        options.deviceId = slot->deviceId;
        options.trimSilence = true;
        options.maxSeconds = env.maxSeconds;
        const auto started = slot->capture->start(slot->file, options);
        if (started.failed())
        {
            slot->capture.reset();
            return abortWith("Cannot record " + deviceLabel(slot->deviceId) + ": " + started.getErrorMessage());
        }
        slot->stats = slot->capture->stats();
        slot->deviceName = slot->stats.deviceName.isNotEmpty() ? slot->stats.deviceName : deviceLabel(slot->deviceId);
    }

    for (auto& slot : planned)
    {
        if (slot->kind == Slot::Kind::system)
            continue;
        auto* recorder = env.takeRecorder ? env.takeRecorder() : nullptr;
        if (recorder == nullptr)
            return abortWith("Recording from the engine is not available");
        platform::TakeOptions options;
        options.file = slot->file;
        options.maxSeconds = env.maxSeconds;
        juce::String error;
        slot->tap = slot->kind == Slot::Kind::master ? recorder->tapMaster(options, error) : recorder->tapTrack(slot->sourceTrackId, options, error);
        if (slot->tap == nullptr)
            return abortWith("Cannot record " + juce::String(slot->kind == Slot::Kind::master ? "Master" : trackById(slot->sourceTrackId)->name) + ": " + error);
    }

    slots = std::move(planned);
    recordStartBeats = env.positionBeats ? env.positionBeats() : 0.0;
    startSeconds = a->beatsToSeconds(recordStartBeats);
    recordStartMs = env.clockMs();
    clockPairs.clear();
    sawPlaying = false;
    playWhenTapsReady = false;
    stoppedTicks = 0;
    currentState = State::recording;
    cs->setRecordingLock(true);

    // Sampler's own output is muted before the song can start, so that no block of it leaks into an endpoint take.
    std::vector<SystemRecording> systemTakes;
    for (const auto& slot : slots)
        if (slot->kind == Slot::Kind::system)
            systemTakes.push_back({slot->stats.mode == "endpoint-loopback", slot->deviceName});
    if (env.setOutputMuted && RecordingPolicy::shouldMuteOutput(systemTakes, env.outputDeviceName ? env.outputDeviceName() : juce::String(),
                                                               env.outputDeviceType ? env.outputDeviceType() : juce::String()))
    {
        env.setOutputMuted(true);
        outputMutedByUs = true;
    }

    if (env.setLocateLocked)
        env.setLocateLocked(true);

    // A song that is stopped starts with the recording; it waits for the engine taps to be in the graph (a moment),
    // so that the take begins on the playhead.
    const bool hasTaps = std::any_of(slots.begin(), slots.end(), [](const auto& s) { return s->tap != nullptr; });
    if (env.isPlaying && ! env.isPlaying() && env.play)
    {
        if (hasTaps)
        {
            playWhenTapsReady = true;
            waitingSinceMs = env.clockMs();
            for (auto& slot : slots) // what System audio hears until the song starts is not kept
                if (slot->capture != nullptr)
                    slot->capture->discardBefore(std::numeric_limits<double>::infinity());
        }
        else
        {
            startSong();
        }
    }

    startTimerHz(30);
    sendChangeMessage();
    return true;
}

void RecordingController::startSong()
{
    if (env.play)
        env.play();
    // System audio heard before the song began is not part of the take: it would sit before the first Master or
    // track block on the timeline, with nothing to place it by.
    const auto now = env.clockMs();
    for (auto& slot : slots)
        if (slot->capture != nullptr)
            slot->capture->discardBefore(now);
}

void RecordingController::abortStartedSlots(std::vector<std::unique_ptr<Slot>>& list)
{
    for (auto& slot : list)
    {
        if (slot->capture != nullptr)
            slot->capture->stop();
        if (slot->tap != nullptr)
        {
            slot->tap->detach();
            slot->tap->finish();
        }
        slot->file.deleteFile();
    }
    list.clear();
}

//==============================================================================
double RecordingController::transportSecondsAt(double t) const
{
    if (clockPairs.empty())
        return startSeconds;
    auto it = std::upper_bound(clockPairs.begin(), clockPairs.end(), t, [](double value, const auto& pair) { return value < pair.first; });
    if (it != clockPairs.begin())
        --it;
    return it->second + (t - it->first) / 1000.0;
}

void RecordingController::resolveStart(Slot& slot)
{
    if (slot.startSeconds >= 0.0)
        return;
    if (slot.tap != nullptr)
    {
        slot.startSeconds = slot.tap->firstFrameSeconds();
        return;
    }
    const auto lead = slot.writer().leadingTrimFrames();
    if (lead < 0 || clockPairs.empty())
        return;
    const auto stats = slot.capture->stats();
    const auto rate = stats.sampleRate > 0.0 ? stats.sampleRate : 48000.0;
    const auto firstKeptMs = stats.startedAtMs + 1000.0 * static_cast<double>(lead) / rate;
    // The device may report no latency: then the measured delay between a sound being played and being captured is used.
    const auto latency = stats.streamLatencySeconds > 0.0 ? stats.streamLatencySeconds : RecordingPolicy::fallbackCaptureLatencySeconds;
    auto at = transportSecondsAt(firstKeptMs) - latency;
    if (firstKeptMs < clockPairs.front().first)
        at = std::max(startSeconds, at); // before the first clock pair the playhead is only known to be where it began
    slot.startSeconds = std::max(0.0, at);
}

void RecordingController::tick()
{
    if (currentState != State::recording)
        return;
    auto* a = env.arrangement ? env.arrangement() : nullptr;
    if (a == nullptr)
        return;

    if (playWhenTapsReady)
    {
        const bool ready = std::all_of(slots.begin(), slots.end(), [](const auto& s) { return s->tap == nullptr || s->tap->isReady(); });
        if (ready || env.clockMs() - waitingSinceMs > 1000.0)
        {
            playWhenTapsReady = false;
            startSong();
        }
    }

    const bool playing = env.isPlaying && env.isPlaying();
    if (playing)
    {
        sawPlaying = true;
        const auto now = env.clockMs();
        if (clockPairs.empty() || now - clockPairs.back().first >= 10.0)
            clockPairs.emplace_back(now, a->beatsToSeconds(env.positionBeats()));
    }
    const auto nowMs = env.clockMs();
    for (auto& slot : slots)
    {
        resolveStart(*slot);
        if (slot->capture != nullptr && slot->writer().everLoud())
        {
            const auto& w = slot->writer();
            const auto mark = w.framesReceived() - w.framesSinceLoud();
            if (mark != slot->loudMark)
            {
                slot->loudMark = mark;
                slot->lastLoudMs = nowMs;
            }
        }
    }

    // The limit also counts by the clock: a loopback device that hears nothing sends no frames to count.
    if (nowMs - recordStartMs >= env.maxSeconds * 1000.0)
    {
        beginFinishing("Recording stopped at the 30-minute limit", false);
        return;
    }

    // Stop pressed (or the song ended): the recording ends with the playback.
    const bool pending = env.isPlayPending && env.isPlayPending();
    if (sawPlaying && ! playing && ! pending && ! playWhenTapsReady)
    {
        if (++stoppedTicks >= 2)
        {
            beginFinishing({}, false);
            return;
        }
    }
    else
    {
        stoppedTicks = 0;
    }

    for (auto& slot : slots)
    {
        const auto& writer = slot->writer();
        if (slot->capture != nullptr && ! slot->capture->isRecording())
        {
            const auto stats = slot->capture->stats();
            if (stats.deviceLost)
                beginFinishing(slot->deviceName + " went away. What was recorded so far was kept.", true);
            else if (stats.writeFailed)
                beginFinishing("The take could not be written (is the disk full?). What was recorded so far was kept.", true);
            else
                beginFinishing(stats.error.isNotEmpty() ? "Recording stopped: " + stats.error : juce::String("Recording stopped"), true);
            return;
        }
        if (slot->tap != nullptr && slot->tap->endedByRateChange())
        {
            beginFinishing("The audio device changed while recording; the take so far was kept", true);
            return;
        }
        if (writer.writeFailed())
        {
            beginFinishing("The take could not be written (is the disk full?). What was recorded so far was kept.", true);
            return;
        }
        if (writer.limitReached())
        {
            beginFinishing("Recording stopped at the 30-minute limit", false);
            return;
        }
    }
}

std::vector<RecordingController::LiveTake> RecordingController::liveTakes() const
{
    if (currentState == State::finishing)
        return finishingTakes;

    std::vector<LiveTake> result;
    if (currentState != State::recording)
        return result;
    auto* a = env.arrangement ? env.arrangement() : nullptr;
    const auto playhead = a != nullptr && env.positionBeats ? a->beatsToSeconds(env.positionBeats()) : startSeconds;
    for (const auto& slot : slots)
    {
        LiveTake live;
        live.trackIds = slot->trackIds;
        live.writer = &slot->writer();
        live.isSystem = slot->kind == Slot::Kind::system;
        live.deviceName = slot->deviceName;
        const auto rate = live.writer->options().sampleRate;
        live.started = live.writer->framesKept() > 0;
        live.startSeconds = slot->startSeconds >= 0.0 ? slot->startSeconds : startSeconds;
        live.lengthSeconds = live.started ? static_cast<double>(live.writer->framesKept()) / rate : std::max(0.0, playhead - live.startSeconds);
        if (live.isSystem)
        {
            // By the clock: a real loopback device sends nothing at all while nothing plays.
            live.noSignal = env.clockMs() - std::max(slot->stats.startedAtMs, slot->lastLoudMs) >= RecordingPolicy::noSignalSeconds * 1000.0;
            if (live.noSignal)
                live.warning = RecordingPolicy::noSignalText(live.deviceName);
        }
        result.push_back(live);
    }
    return result;
}

//==============================================================================
void RecordingController::stop()
{
    if (currentState == State::recording)
        beginFinishing({}, false);
}

void RecordingController::transportStopped()
{
    if (currentState == State::recording && playWhenTapsReady)
        beginFinishing({}, false);
}

void RecordingController::abortNow()
{
    arms.clear();
    if (currentState == State::idle)
        return;
    stopTimer();
    ++generation;
    discardInFlight(); // a finish that is already running: wait for it, then destroy its takes now
    for (auto& slot : slots)
    {
        if (slot->tap != nullptr)
            slot->tap->detach();
        if (slot->capture != nullptr)
            slot->capture->stop();
        if (slot->tap != nullptr)
            slot->tap->finish();
        slot->file.deleteFile();
    }
    slots.clear();
    finishingTakes.clear();
    releaseLocks();
    clockPairs.clear();
    currentState = State::idle;
    afterFinish.clear();
    sendChangeMessage();
}

void RecordingController::finishThen(std::function<void()> done)
{
    if (currentState == State::idle)
    {
        if (done)
            done();
        return;
    }
    if (done)
        afterFinish.push_back(std::move(done));
    if (currentState == State::recording)
        beginFinishing({}, false);
}

void RecordingController::beginFinishing(const juce::String& reasonMessage, bool reasonIsError)
{
    if (currentState != State::recording)
        return;
    const bool cancelled = playWhenTapsReady && ! reasonIsError; // Stop pressed before the song ever started
    finishingTakes = liveTakes(); // the growing clips stay on screen, labelled, until the takes land
    for (auto& take : finishingTakes)
    {
        take.writer = nullptr;
        take.finishing = true;
        take.warning = "Finishing...";
    }
    currentState = State::finishing;
    playWhenTapsReady = false; // a song that was waiting for the taps does not start now
    stopTimer();
    if (outputMutedByUs && env.setOutputMuted)
        env.setOutputMuted(false);
    outputMutedByUs = false;
    if (env.setLocateLocked)
        env.setLocateLocked(false); // nothing grows any more; Stop may return the playhead

    for (auto& slot : slots)
    {
        resolveStart(*slot);
        if (slot->tap != nullptr)
        {
            slot->tapWasReady = slot->tap->isReady();
            slot->tap->detach();
        }
    }
    sendChangeMessage();

    if (worker.joinable())
        worker.join();
    inFlight = std::make_shared<std::vector<std::unique_ptr<Slot>>>(std::move(slots));
    slots.clear();
    const auto shared = inFlight;
    juce::WeakReference<RecordingController> weak(this);
    const int gen = generation;
    worker = std::thread([shared, weak, gen, reasonMessage, reasonIsError, cancelled]() mutable {
        for (auto& slot : *shared)
        {
            if (slot->capture != nullptr)
            {
                slot->capture->stop();
                slot->stats = slot->capture->stats();
                slot->result = slot->capture->takeResult();
            }
            else
            {
                slot->result = slot->tap->finish();
            }
        }
        juce::MessageManager::callAsync([shared, weak, gen, reasonMessage, reasonIsError, cancelled]() mutable {
            // After an abort the takes were already destroyed (inFlight emptied) and this does nothing.
            if (auto* self = weak.get(); self != nullptr && self->generation == gen && self->inFlight == shared)
            {
                auto finished = std::move(*shared);
                self->inFlight.reset();
                self->finalise(std::move(finished), reasonMessage, reasonIsError, cancelled);
            }
        });
    });
}

void RecordingController::finalise(std::vector<std::unique_ptr<Slot>> finished, juce::String reasonMessage, bool reasonIsError, bool cancelled)
{
    if (worker.joinable())
        worker.join();
    if (cancelled) // the user gave up before the song started: nothing was recorded that they want
        for (auto& slot : finished)
            slot->file.deleteFile();

    auto* a = env.arrangement ? env.arrangement() : nullptr;
    auto* cs = env.commands ? env.commands() : nullptr;
    if (cs != nullptr)
        cs->setRecordingLock(false);

    std::vector<commands::Take> takes;
    juce::StringArray silentTracks, failedTakes;
    bool lostParts = false;
    const auto when = recordStartWall; // names and times are those of the start of the recording
    const auto bundle = env.bundleDir();

    if (a != nullptr && ! cancelled)
    {
        for (auto& slot : finished)
        {
            lostParts = lostParts || slot->result.overruns > 0;
            const bool usable = slot->result.everLoud && slot->result.frames > 0 && slot->file.existsAsFile();
            if (! usable)
            {
                juce::String reason; // empty: the take was only silent
                if (slot->result.writeFailed)
                    reason = "it could not be written (is the disk full?)";
                else if (slot->tap != nullptr && ! slot->tapWasReady)
                    reason = "the audio engine never started recording it";
                else if (! slot->file.existsAsFile())
                    reason = "its file is missing";
                for (const auto& id : slot->trackIds)
                    if (const auto info = a->track(id))
                    {
                        if (reason.isEmpty())
                            silentTracks.add(info->name);
                        else
                            failedTakes.add("The take on " + info->name + " was not added: " + reason);
                    }
                slot->file.deleteFile();
                continue;
            }

            juce::String from = "Master";
            if (slot->kind == Slot::Kind::system)
                from = slot->deviceName.isNotEmpty() ? "System audio (" + slot->deviceName + ")" : juce::String("System audio");
            else if (slot->kind == Slot::Kind::track)
                from = a->track(slot->sourceTrackId) ? a->track(slot->sourceTrackId)->name : juce::String("a track");

            const auto start = slot->startSeconds >= 0.0 ? slot->startSeconds : startSeconds;
            for (const auto& id : slot->trackIds)
            {
                const auto info = a->track(id);
                if (! info)
                    continue;
                commands::Take take;
                take.trackId = id;
                take.name = info->name + " " + when.formatted("%Y-%m-%d %H:%M") + (finished.size() > 1 ? " #" + juce::String(slot->number) : juce::String());
                take.relativePath = slot->file.getRelativePathFrom(bundle).replaceCharacter('\\', '/');
                take.sampleRate = slot->result.sampleRate;
                take.channels = slot->result.channels;
                take.frames = slot->result.frames;
                take.startBeat = a->secondsToBeats(start);
                take.recordedFrom = from;
                take.recordedAt = isoTime(when);
                takes.push_back(take);
            }
        }
    }

    juce::StringArray notAdded;
    if (! takes.empty() && cs != nullptr)
    {
        if (cs->isGestureOpen())
            cs->commit(); // a mixer gesture still open would swallow the takes into its own step
        commands::AddTakes command(takes);
        cs->apply(command);
        const auto& skipped = command.skippedTakes();
        for (const auto index : skipped)
            notAdded.add(takes[index].name);
        for (const auto index : skipped)
        {
            const auto path = takes[index].relativePath;
            bool used = false; // tracks on one device share a file: it stays while any of them got a clip
            for (size_t i = 0; i < takes.size(); ++i)
                used = used || (takes[i].relativePath == path && std::find(skipped.begin(), skipped.end(), i) == skipped.end());
            if (! used)
                bundle.getChildFile(path).deleteFile();
        }
        if (env.onTakesAdded)
            env.onTakesAdded(command.resultIds());
    }

    if (cancelled)
    {
        toast("Recording cancelled", false);
    }
    else if (reasonMessage.isNotEmpty())
        toast(reasonMessage, reasonIsError);
    if (! silentTracks.isEmpty())
    {
        silentTracks.removeDuplicates(false);
        toast(silentTracks.size() == 1 ? "The take on " + silentTracks[0] + " was silent and was not added"
                                       : "The takes on " + silentTracks.joinIntoString(", ") + " were silent and were not added",
              true);
    }
    for (const auto& text : failedTakes)
        toast(text, true);
    if (! notAdded.isEmpty())
        toast(notAdded.size() == 1 ? "The take \"" + notAdded[0] + "\" could not be added to the project"
                                   : juce::String(notAdded.size()) + " takes could not be added to the project: " + notAdded.joinIntoString(", "),
              true);
    if (lostParts && ! cancelled)
        toast("Parts of the recording were lost because the computer could not keep up", true);

    finished.clear();
    finishingTakes.clear();
    clockPairs.clear();
    currentState = State::idle;
    sendChangeMessage();

    auto callbacks = std::move(afterFinish);
    afterFinish.clear();
    for (auto& done : callbacks)
        done();
}
} // namespace sampler::app
