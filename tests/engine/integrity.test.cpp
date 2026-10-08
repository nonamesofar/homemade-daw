// M0 file-safety checks: atomic project save (task 3.5) and "audio files are never modified" (3.6).
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/EditLab.h"
#include "io/AtomicFile.h"

#ifndef ATOMIC_WRITER_PROBE_PATH
 #error "ATOMIC_WRITER_PROBE_PATH must be set by CMake"
#endif

namespace
{
juce::String sha256(const juce::File& f) { return juce::SHA256(f).toHexString(); }
} // namespace

TEST_CASE("atomic write replaces the file whole", "[io][atomic]")
{
    auto dir = sampler::test::tempDir("atomic-basic");
    const auto file = dir.getChildFile("project.bin");
    file.deleteFile();

    const juce::String first = "first version";
    const juce::String second = "second, longer version of the content";
    REQUIRE(sampler::io::writeFileAtomically(file, first.toRawUTF8(), first.getNumBytesAsUTF8()));
    CHECK(file.loadFileAsString() == first);
    REQUIRE(sampler::io::writeFileAtomically(file, second.toRawUTF8(), second.getNumBytesAsUTF8()));
    CHECK(file.loadFileAsString() == second);
    CHECK(dir.findChildFiles(juce::File::findFiles, false).size() == 1); // no temporary file left behind
}

TEST_CASE("a process killed in the middle of a save leaves the old project intact and loadable", "[io][atomic][A8]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("atomic-kill");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto audio = sampler::test::writeToneWav(dir.getChildFile("loop.wav"), 2.0);
    const auto editFile = dir.getChildFile("project.tracktionedit");

    {
        sampler::EditLab lab;
        lab.newEdit(editFile);
        REQUIRE(lab.addClip(audio, 0.0, 2.0, true) == 0);
        lab.setClipProperty(0, "sampler_sourceId", "src_1");
        REQUIRE(lab.save());
    }
    const auto hashBefore = sha256(editFile);
    const auto sizeBefore = editFile.getSize();

    // The writer saves 50 MB of garbage over the project file and is killed half way through.
    juce::ChildProcess writer;
    const juce::StringArray command{ATOMIC_WRITER_PROBE_PATH, editFile.getFullPathName(), "50000000", "60000"};
    REQUIRE(writer.start(command));

    // readProcessOutput waits until the buffer is full or the process ends, so ask for exactly the marker's length.
    const juce::String marker = "half-written\n";
    char buffer[64] = {};
    const auto got = writer.readProcessOutput(buffer, marker.length());
    const auto output = juce::String::fromUTF8(buffer, got);
    REQUIRE(output.contains("half-written"));

    {
        juce::String listing;
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false))
            listing << "\n  " << f.getFileName() << " (" << f.getSize() << " bytes)";
        INFO("writer running: " << (writer.isRunning() ? "yes" : "no") << ", output: " << output
             << ", size before: " << sizeBefore << ", files:" << listing);

        // While it is stalled: the real file is untouched and a partial temporary file sits next to it.
        CHECK(sha256(editFile) == hashBefore);
    }
    CHECK(dir.findChildFiles(juce::File::findFiles, false, "*.tracktionedit*").size() >= 2);

    REQUIRE(writer.kill());
    writer.waitForProcessToFinish(5000);

    CHECK(editFile.getSize() == sizeBefore);
    CHECK(sha256(editFile) == hashBefore);

    sampler::EditLab reopened;
    REQUIRE(reopened.open(editFile));
    REQUIRE(reopened.numClips() == 1);
    CHECK(reopened.clipProperty(0, "sampler_sourceId") == "src_1");

    // A leftover temporary file does not stop the next save from working.
    REQUIRE(reopened.save());
}

TEST_CASE("opening, editing, rendering and saving never modify the audio", "[engine][integrity][A9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("integrity");
    dir.deleteRecursively();
    dir.getChildFile("audio").createDirectory();
    const auto audio = sampler::test::writeToneWav(dir.getChildFile("audio/loop.wav"), 4.0);
    const auto editFile = dir.getChildFile("project.tracktionedit");
    const auto hashBefore = sha256(audio);
    const auto sizeBefore = audio.getSize();
    const auto timeBefore = audio.getLastModificationTime();

    {
        sampler::EditLab lab;
        lab.newEdit(editFile);
        REQUIRE(lab.addClip(audio, 0.0, 4.0, true) == 0);
        REQUIRE(lab.addClip(audio, 8.0, 2.0, true) == 1);
        lab.setTempo(90.0);
        lab.setClipProperty(0, "sampler_warpMode", "beats");
        CHECK(lab.buildThumbnail(0));
        CHECK(lab.buildReverseProxy(1));
        REQUIRE(lab.renderToWav(dir.getChildFile("mix.wav"), 8.0));
        REQUIRE(lab.save());
    }
    {
        sampler::EditLab reopened;
        REQUIRE(reopened.open(editFile));
        REQUIRE(reopened.save());
    }

    CHECK(audio.getSize() == sizeBefore);
    CHECK(sha256(audio) == hashBefore);
    CHECK(audio.getLastModificationTime() == timeBefore);
}
