// M0 file-safety checks: atomic project save (task 3.5) and "audio files are never modified" (3.6).
// The project file is saved by sampler::detail::saveEditAtomically (through EditLab::save), never by Tracktion's
// EditFileOperations::save, which deletes the edit file before moving the new one in.
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <thread>

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

TEST_CASE("a process killed in the middle of an atomic write leaves the old project intact and loadable", "[io][atomic][A8]")
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

TEST_CASE("the real project save keeps the old edit file whole until one swap replaces it", "[engine][atomic][A8]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("atomic-edit-save");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto audio = sampler::test::writeToneWav(dir.getChildFile("loop.wav"), 2.0);
    const auto editFile = dir.getChildFile("project.tracktionedit");

    sampler::EditLab lab;
    lab.newEdit(editFile);
    REQUIRE(editFile.existsAsFile());
    REQUIRE(lab.addClip(audio, 0.0, 2.0, true) == 0);
    lab.setClipProperty(0, "sampler_sourceId", "old_version");
    REQUIRE(lab.save());
    const auto hashBefore = sha256(editFile);

    // Deterministic: stop the save at the last moment before the swap and look at the disk.
    lab.setClipProperty(0, "sampler_sourceId", "new_version");
    bool hookRan = false;
    REQUIRE(lab.save(
        [&](const juce::File& temporary)
        {
            hookRan = true;
            // The new version is complete and flushed in a temporary file beside the edit file...
            CHECK(temporary.existsAsFile());
            CHECK(temporary.getParentDirectory() == editFile.getParentDirectory());
            CHECK(juce::parseXML(temporary) != nullptr);
            CHECK(temporary.loadFileAsString().contains("new_version"));
            // ...and the edit file is still there, whole and unchanged (it is never deleted before the swap).
            CHECK(editFile.existsAsFile());
            CHECK(sha256(editFile) == hashBefore);
        }));
    CHECK(hookRan);
    CHECK(editFile.loadFileAsString().contains("new_version"));
    CHECK(dir.findChildFiles(juce::File::findFiles, false).size() == 2); // edit + audio: no temporary file left

    // Probabilistic, on top: while saving many times, another thread never sees the edit file missing.
    std::atomic<bool> saving{true};
    std::atomic<int> polls{0}, missing{0};
    std::thread watcher(
        [&]
        {
            while (saving)
            {
                if (!editFile.existsAsFile())
                    ++missing;
                ++polls;
            }
        });
    bool allSaved = true;
    for (int i = 0; i < 50; ++i)
    {
        lab.setClipProperty(0, "sampler_sourceId", "v" + juce::String(i));
        allSaved = lab.save() && allSaved;
    }
    saving = false;
    watcher.join();
    INFO("polls " << polls.load() << ", edit file missing in " << missing.load());
    CHECK(allSaved);
    CHECK(polls > 0);
    CHECK(missing == 0);

    sampler::EditLab reopened;
    REQUIRE(reopened.open(editFile));
    CHECK(reopened.clipProperty(0, "sampler_sourceId") == "v49");
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
