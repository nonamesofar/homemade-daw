// Import pipeline on a plain project (no engine): files, SOURCE nodes, duplicates and the user's files untouched.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <algorithm>

#include "commands/CommandService.h"
#include "io/ImportNaming.h"
#include "io/ImportService.h"
#include "model/SamplerState.h"

using namespace sampler;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

juce::String sha256(const juce::File& f) { return juce::SHA256(f).toHexString(); }

/** A throwaway project folder with the pieces the importer talks to. */
struct TestProject
{
    explicit TestProject(const juce::String& name)
        : bundle(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name + ".sdaw"))
    {
        bundle.deleteRecursively();
        bundle.getChildFile("audio/originals").createDirectory();
        service = std::make_unique<io::ImportService>(tree, commands, [this] { return bundle; }, jobs);
        service->onMessage = [this](const io::ImportMessage& m) { messages.push_back(m); };
    }

    ~TestProject()
    {
        service.reset();
        bundle.deleteRecursively();
    }

    void import(std::initializer_list<juce::File> files)
    {
        juce::Array<juce::File> list;
        for (const auto& f : files)
            list.add(f);
        service->importFiles(list);
        waitUntilIdle();
    }

    void waitUntilIdle(int timeoutMs = 20000)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (service->isBusy() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }

    int countMessages(io::ImportMessage::Kind kind) const
    {
        return static_cast<int>(
            std::count_if(messages.begin(), messages.end(), [kind](const auto& m) { return m.kind == kind; }));
    }

    juce::File bundle;
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    commands::CommandService commands{tree, undo, bundle};
    io::JobRegistry jobs;
    std::unique_ptr<io::ImportService> service;
    model::SamplerState state{tree};
    std::vector<io::ImportMessage> messages;
};
} // namespace

TEST_CASE("file names are made readable and safe", "[io][import]")
{
    CHECK(io::sanitiseName("Break 90 (final)") == "Break-90-final");
    CHECK(io::sanitiseName("  kick__01  ") == "kick-01");
    CHECK(io::sanitiseName("???") == "sound");
    CHECK(io::sanitiseName(juce::String::repeatedString("a", 100)).length() == 60);
    CHECK(io::originalRelativePath("Break 90 (final)", "sha256:0123456789abcdef", "WAV")
          == "audio/originals/Break-90-final-01234567.wav");
    CHECK(io::decodedRelativePath("Break 90 (final)", "sha256:0123456789abcdef") == "audio/Break-90-final-01234567.wav");
}

TEST_CASE("importing a WAV copies it, decodes it and records one source", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-wav");
    const auto user = fixture("drums-loop-120.wav");
    const auto hashBefore = sha256(user);

    project.import({user});

    REQUIRE(project.state.numSources() == 1);
    const auto source = project.state.sourceAt(0);
    const auto hash8 = hashBefore.substring(0, 8);
    CHECK(source.id() == "src_" + hash8);
    CHECK(source.name() == "drums-loop-120");
    CHECK(source.hash() == "sha256:" + hashBefore);
    CHECK(source.original() == "audio/originals/drums-loop-120-" + hash8 + ".wav");
    CHECK(source.decoded() == "audio/drums-loop-120-" + hash8 + ".wav");
    CHECK(source.originalPath() == user.getFullPathName());
    CHECK(source.sampleRate() == 44100.0);
    CHECK(source.channels() == 2);
    CHECK(source.frames() == 176400);
    CHECK(source.category() == model::Category::loop);

    // The paths are relative to the bundle and the files are there.
    CHECK_FALSE(juce::File::isAbsolutePath(source.original()));
    CHECK_FALSE(juce::File::isAbsolutePath(source.decoded()));
    const auto copy = project.bundle.getChildFile(source.original());
    const auto decoded = project.bundle.getChildFile(source.decoded());
    REQUIRE(copy.existsAsFile());
    REQUIRE(decoded.existsAsFile());
    CHECK(sha256(copy) == hashBefore); // an exact copy of the file the user chose
    CHECK(sha256(user) == hashBefore); // and that file is untouched

    // The working copy is 32-bit float at the original rate and holds the same audio.
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> in(wav.createReaderFor(user.createInputStream().release(), true));
    std::unique_ptr<juce::AudioFormatReader> out(wav.createReaderFor(decoded.createInputStream().release(), true));
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    CHECK(out->usesFloatingPointData);
    CHECK(out->bitsPerSample == 32);
    CHECK(out->lengthInSamples == in->lengthInSamples);
    juce::AudioBuffer<float> a(2, 176400), b(2, 176400);
    in->read(&a, 0, 176400, 0, true, true);
    out->read(&b, 0, 176400, 0, true, true);
    for (int c = 0; c < 2; ++c)
        CHECK(std::memcmp(a.getReadPointer(c), b.getReadPointer(c), sizeof(float) * 176400) == 0);

    CHECK(project.messages.empty());
    CHECK_FALSE(project.jobs.hasJobs());
    CHECK(project.commands.undoName() == "Import drums-loop-120.wav");
}

TEST_CASE("a short mono sound becomes a one-shot", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-kick");
    project.import({fixture("kick.wav")});
    REQUIRE(project.state.numSources() == 1);
    const auto source = project.state.sourceAt(0);
    CHECK(source.channels() == 1);
    CHECK(source.category() == model::Category::oneShot);
}

TEST_CASE("importing the same file twice adds one source and one copy", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-twice");
    project.import({fixture("drums-loop-120.wav")});
    REQUIRE(project.state.numSources() == 1);
    const auto id = project.state.sourceAt(0).id();

    project.import({fixture("drums-loop-120.wav")});
    CHECK(project.state.numSources() == 1);
    CHECK(project.bundle.getChildFile("audio/originals").getNumberOfChildFiles(juce::File::findFiles) == 1);
    REQUIRE(project.countMessages(io::ImportMessage::Kind::duplicate) == 1);
    CHECK(project.messages.back().sourceId == id); // the UI selects this row

    // One undo step only: the second import recorded nothing.
    project.commands.undo();
    CHECK(project.state.numSources() == 0);
}

TEST_CASE("the same bytes under another name are one sound", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-rename");
    const auto copyDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/import-rename-src");
    copyDir.createDirectory();
    const auto renamed = copyDir.getChildFile("other name.wav");
    REQUIRE(fixture("drums-loop-120.wav").copyFileTo(renamed));

    project.import({fixture("drums-loop-120.wav"), renamed}); // both in one drop
    CHECK(project.state.numSources() == 1);
    CHECK(project.countMessages(io::ImportMessage::Kind::duplicate) == 1);

    project.import({renamed}); // and later again
    CHECK(project.state.numSources() == 1);
    CHECK(project.countMessages(io::ImportMessage::Kind::duplicate) == 2);
    copyDir.deleteRecursively();
}

TEST_CASE("importing again after an undo reuses the files on disk", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-undo");
    project.import({fixture("kick.wav")});
    const auto decoded = project.bundle.getChildFile(project.state.sourceAt(0).decoded());
    REQUIRE(decoded.existsAsFile());
    const auto modified = decoded.getLastModificationTime();

    project.commands.undo();
    REQUIRE(project.state.numSources() == 0);
    CHECK(decoded.existsAsFile()); // files stay after undo (redo is instant)

    juce::Thread::sleep(20);
    project.import({fixture("kick.wav")});
    CHECK(project.state.numSources() == 1);
    CHECK(decoded.getLastModificationTime() == modified);
}

TEST_CASE("the job registry shows the batch while it runs", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-jobs");
    juce::Array<juce::File> files;
    files.add(fixture("not-audio.txt")); // fails fast, still part of the job
    files.add(fixture("kick.wav"));
    project.service->importFiles(files);
    REQUIRE(project.jobs.hasJobs());
    CHECK(project.jobs.jobs().front().label == "Importing 2 files");
    CHECK(project.jobs.allPendingNames().size() == 2);
    project.waitUntilIdle();
    CHECK_FALSE(project.jobs.hasJobs());
    CHECK(project.state.numSources() == 1);
    CHECK(project.countMessages(io::ImportMessage::Kind::error) == 1);
}

TEST_CASE("one pending name is removed per finished file, not every file with that stem", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    io::JobRegistry jobs;
    const int id = jobs.add("Importing 2 files", juce::StringArray{"kick", "kick"}); // kick.wav and kick.aiff
    jobs.nameDone(id, "kick");
    CHECK(jobs.allPendingNames().size() == 1);
    jobs.nameDone(id, "kick");
    CHECK(jobs.allPendingNames().isEmpty());
}

TEST_CASE("a damaged decoded copy left on disk is decoded again instead of reused", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-damaged-copy");
    project.import({fixture("kick.wav")});
    REQUIRE(project.state.numSources() == 1);
    const auto decoded = project.bundle.getChildFile(project.state.sourceAt(0).decoded());
    const auto goodSize = decoded.getSize();
    project.commands.undo();

    // Cut the copy short, as a crash during an earlier import could.
    juce::MemoryBlock bytes;
    REQUIRE(decoded.loadFileAsData(bytes));
    REQUIRE(decoded.replaceWithData(bytes.getData(), bytes.getSize() / 2));

    project.import({fixture("kick.wav")});
    REQUIRE(project.state.numSources() == 1);
    CHECK(decoded.getSize() == goodSize);
}

TEST_CASE("destroying the service during a long import cancels it quickly and leaves no files", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-cancel");

    // About 5 minutes of mono float audio: long enough that the copy and the decode are still running.
    const auto big = project.bundle.getSiblingFile("import-cancel-big.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(big.createOutputStream().release(), 44100.0, 1, 32, {}, 0));
        REQUIRE(writer != nullptr);
        juce::AudioBuffer<float> block(1, 44100);
        for (int i = 0; i < 44100; ++i)
            block.setSample(0, i, 0.25f * std::sin(0.05f * static_cast<float>(i)));
        for (int s = 0; s < 300; ++s)
            REQUIRE(writer->writeFromAudioSampleBuffer(block, 0, block.getNumSamples()));
    }

    juce::Array<juce::File> files;
    files.add(big);
    project.service->importFiles(files);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // let the worker get going

    const auto t0 = juce::Time::getMillisecondCounter();
    project.service.reset();
    CHECK(juce::Time::getMillisecondCounter() - t0 < 5000);

    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(project.bundle.getChildFile("audio/originals").getNumberOfChildFiles(juce::File::findFiles) == 0);
    CHECK(project.bundle.getChildFile("audio").getNumberOfChildFiles(juce::File::findFiles) == 0);
    CHECK(project.state.numSources() == 0);
    CHECK_FALSE(project.jobs.hasJobs());
    big.deleteFile();
}

TEST_CASE("an importer destroyed before its batch is recorded leaves no files behind", "[io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestProject project("import-destroyed");
    juce::Array<juce::File> list;
    list.add(fixture("drums-loop-120.wav"));
    project.service->importFiles(list);

    const auto audio = project.bundle.getChildFile("audio");
    const auto end = juce::Time::getMillisecondCounter() + 20000;
    while (audio.findChildFiles(juce::File::findFiles, true).isEmpty() && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep(1);

    project.service.reset(); // the worker may already have queued the batch result
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200); // the queued result finds no importer and cleans up
    CHECK(audio.findChildFiles(juce::File::findFiles, true).isEmpty());
    CHECK(project.state.numSources() == 0);
}
