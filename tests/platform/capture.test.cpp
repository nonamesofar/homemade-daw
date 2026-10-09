// Capture lifecycle: failures while starting leave nothing running and nothing on disk.
// Needs an output device to get past device activation; without one the tests skip.
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "platform/ICaptureSource.h"

namespace
{
juce::File testDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests").getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

bool noOutputDevice(const juce::Result& r) { return r.getErrorMessage().contains("cannot open the default output device"); }
} // namespace

TEST_CASE("stop without start is harmless", "[platform][capture]")
{
    auto capture = sampler::platform::createCaptureSource();
    CHECK(capture->stop().wasOk());
    CHECK_FALSE(capture->isRecording());
}

TEST_CASE("a stream that fails to start stops its writer and leaves no file", "[platform][capture]")
{
    const auto dir = testDir("capture-start-failure");
    const auto out = dir.getChildFile("rec.wav");

    {
        auto capture = sampler::platform::createCaptureSource();
        sampler::platform::CaptureOptions options;
        options.excludeOwnProcess = false;
        options.simulateStreamStartFailure = true; // fails after the file and its writer thread exist

        const auto started = capture->start(out, options);
        if (noOutputDevice(started))
            SKIP("no output device: " << started.getErrorMessage());
        INFO("start: " << started.getErrorMessage());
        REQUIRE(started.failed());
        CHECK(started.getErrorMessage().contains("Start failed"));
        CHECK_FALSE(capture->isRecording());
        CHECK_FALSE(out.exists());
        CHECK(capture->stop().wasOk()); // nothing left to stop
    } // the destructor must return (it used to wait forever for the writer thread)

    CHECK(dir.findChildFiles(juce::File::findFiles, false).isEmpty());
}

TEST_CASE("a destination that cannot be created fails the start cleanly", "[platform][capture]")
{
    const auto dir = testDir("capture-bad-destination");
    const auto blocker = dir.getChildFile("not-a-folder");
    REQUIRE(blocker.replaceWithText("a file where the folder should be"));
    const auto out = blocker.getChildFile("rec.wav");

    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false;
    const auto started = capture->start(out, options);
    if (noOutputDevice(started))
        SKIP("no output device: " << started.getErrorMessage());
    INFO("start: " << started.getErrorMessage());
    REQUIRE(started.failed());
    CHECK(started.getErrorMessage().contains("cannot create"));
    CHECK_FALSE(capture->isRecording());
    CHECK(capture->stop().wasOk());
}

TEST_CASE("a short capture starts, stops and writes a file", "[platform][capture]")
{
    const auto dir = testDir("capture-short");
    const auto out = dir.getChildFile("rec.wav");

    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false;
    const auto started = capture->start(out, options);
    if (noOutputDevice(started))
        SKIP("no output device: " << started.getErrorMessage());
    REQUIRE(started.wasOk());
    CHECK(capture->isRecording());
    juce::Thread::sleep(500);
    const auto stopped = capture->stop();
    INFO("stop: " << stopped.getErrorMessage());
    CHECK(stopped.wasOk());
    CHECK_FALSE(capture->isRecording());

    const auto stats = capture->stats();
    CHECK(stats.error.isEmpty());
    CHECK_FALSE(stats.deviceLost);
    CHECK_FALSE(stats.writeFailed);
    CHECK(stats.framesWritten > 0); // silence counts as recorded time
    CHECK(out.getSize() > 44);
}
