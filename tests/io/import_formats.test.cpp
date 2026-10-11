// M1 D4: every supported format, MP3 without its gap, and files that cannot be imported.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include <algorithm>

#include "FixtureGenerator.h"
#include "commands/CommandService.h"
#include "io/ImportService.h"
#include "model/SamplerState.h"

using namespace sampler;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

struct Project
{
    explicit Project(const juce::String& name)
        : bundle(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name + ".sdaw"))
    {
        bundle.deleteRecursively();
        bundle.getChildFile("audio/originals").createDirectory();
        service = std::make_unique<io::ImportService>(tree, commands, [this] { return bundle; }, jobs);
        service->onMessage = [this](const io::ImportMessage& m) { messages.push_back(m); };
    }

    ~Project()
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
        const auto end = juce::Time::getMillisecondCounter() + 60000;
        while (service->isBusy() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }

    /** Every file and folder under the bundle, as relative paths, sorted. */
    juce::StringArray listing() const
    {
        juce::StringArray paths;
        for (const auto& f : bundle.findChildFiles(juce::File::findFilesAndDirectories, true))
            paths.add(f.getRelativePathFrom(bundle));
        paths.sort(false);
        return paths;
    }

    int errors() const
    {
        return static_cast<int>(std::count_if(messages.begin(), messages.end(),
                                              [](const auto& m) { return m.kind == io::ImportMessage::Kind::error; }));
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

juce::String sha256(const juce::File& f) { return juce::SHA256(f).toHexString(); }
} // namespace

TEST_CASE("each supported format imports with its own rate, channels and length", "[io][import][formats]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    struct Expected
    {
        const char* file;
        double rate;
        int channels;
        juce::int64 frames;
    };
    // A LAME tag gives the exact length; the tagless MP3 keeps whole frames minus 1105 at the start.
    const Expected cases[] = {{"drums-loop-120.wav", 44100.0, 2, 176400},   {"drums-loop-120.aiff", 44100.0, 2, 176400},
                              {"bass-phrase.flac", 48000.0, 1, 96000},     {"pad-chord.ogg", 44100.0, 2, 132300},
                              {"drums-loop-120.mp3", 44100.0, 2, 176400},  {"drums-loop-120-notag.mp3", 44100.0, 2, 178560 - 1105}};

    for (const auto& expected : cases)
    {
        INFO(expected.file);
        Project project("formats");
        const auto user = fixture(expected.file);
        const auto hashBefore = sha256(user);
        project.import({user});

        REQUIRE(project.state.numSources() == 1);
        const auto source = project.state.sourceAt(0);
        CHECK(source.sampleRate() == expected.rate);
        CHECK(source.channels() == expected.channels);
        CHECK(std::abs(source.frames() - expected.frames) <= 1);
        CHECK(project.errors() == 0);
        CHECK(sha256(user) == hashBefore);
        CHECK(project.bundle.getChildFile(source.original()).existsAsFile());
        CHECK(project.bundle.getChildFile(source.decoded()).existsAsFile());
    }
}

TEST_CASE("a tagged MP3 loop starts on the same sample as the WAV it came from", "[io][import][formats]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Project project("mp3-aligned");
    project.import({fixture("drums-loop-120.mp3")});
    REQUIRE(project.state.numSources() == 1);

    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    std::unique_ptr<juce::AudioFormatReader> imported(formats.createReaderFor(project.bundle.getChildFile(project.state.sourceAt(0).decoded())));
    std::unique_ptr<juce::AudioFormatReader> reference(formats.createReaderFor(fixture("drums-loop-120.wav")));
    REQUIRE(imported != nullptr);
    REQUIRE(reference != nullptr);

    const auto firstLoud = [](juce::AudioFormatReader& r) {
        juce::AudioBuffer<float> b(1, 20000);
        r.read(&b, 0, 20000, 0, true, false);
        for (int i = 0; i < 20000; ++i)
            if (std::abs(b.getSample(0, i)) > 0.2f)
                return i;
        return -1;
    };
    const auto a = firstLoud(*imported), b = firstLoud(*reference);
    REQUIRE(a >= 0);
    CHECK(std::abs(a - b) <= 1); // no gap at the start
}

TEST_CASE("unsupported files are rejected by name and change nothing", "[io][import][formats]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Project project("unsupported");
    const auto m4a = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/song.m4a");
    m4a.getParentDirectory().createDirectory();
    m4a.replaceWithText("not really an m4a");
    const auto before = project.listing();

    project.import({fixture("not-audio.txt"), m4a});

    CHECK(project.state.numSources() == 0);
    CHECK(project.errors() == 2);
    CHECK(project.messages[0].text.startsWith("not-audio.txt"));
    CHECK(project.messages[0].text.contains("not supported"));
    CHECK(project.messages[1].text.startsWith("song.m4a"));
    CHECK(project.listing() == before);
    CHECK_FALSE(project.commands.canUndo());
    m4a.deleteFile();
}

TEST_CASE("more than two channels are mixed down to stereo with a note", "[io][import][formats]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Project project("fivech");
    project.import({fixture("5ch-tone.wav")});
    REQUIRE(project.state.numSources() == 1);
    CHECK(project.state.sourceAt(0).channels() == 2);
    CHECK(project.state.sourceAt(0).frames() == 44100);
    REQUIRE(project.messages.size() == 1);
    CHECK(project.messages[0].kind == io::ImportMessage::Kind::info);
    CHECK(project.messages[0].text.contains("mixed down to stereo"));

    // The mix has sound in both channels.
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(project.bundle.getChildFile(project.state.sourceAt(0).decoded())));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> b(2, 44100);
    reader->read(&b, 0, 44100, 0, true, true);
    CHECK(b.getMagnitude(0, 0, 44100) > 0.1f);
    CHECK(b.getMagnitude(1, 0, 44100) > 0.1f);
}

TEST_CASE("a corrupt file leaves no sound, no step and no files", "[io][import][failures]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Project project("corrupt");
    const auto before = project.listing();

    project.import({fixture("corrupt.wav")});

    CHECK(project.state.numSources() == 0);
    REQUIRE(project.errors() == 1);
    CHECK(project.messages[0].text.startsWith("corrupt.wav"));
    CHECK(project.listing() == before);
    CHECK_FALSE(project.commands.canUndo());
}

TEST_CASE("one bad file among good ones: the good ones are one step", "[io][import][failures]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Project project("partial");
    project.import({fixture("kick.wav"), fixture("corrupt.wav"), fixture("drums-loop-120.wav")});

    CHECK(project.state.numSources() == 2);
    REQUIRE(project.errors() == 1);
    CHECK(project.messages[0].text.startsWith("corrupt.wav"));
    CHECK(project.commands.undoName() == "Import 2 files");
    project.commands.undo();
    CHECK(project.state.numSources() == 0);
    CHECK_FALSE(project.commands.canUndo());
}

TEST_CASE("a file longer than 30 minutes is refused before anything is copied", "[io][import][failures]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/long-fixture");
    dir.deleteRecursively();
    REQUIRE(fixtures::generateTooLongFixture(dir).isEmpty());
    const auto longFile = dir.getChildFile("toolong-31min.wav");
    REQUIRE(longFile.existsAsFile());

    Project project("toolong");
    const auto before = project.listing();
    project.import({longFile});

    CHECK(project.state.numSources() == 0);
    REQUIRE(project.errors() == 1);
    CHECK(project.messages[0].text.startsWith("toolong-31min.wav"));
    CHECK(project.messages[0].text.contains("longer than 30 minutes"));
    CHECK(project.listing() == before);
    dir.deleteRecursively();
}

TEST_CASE("a 10-minute file imports in the background and reports progress", "[.slow][io][import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/long-10min");
    dir.deleteRecursively();
    REQUIRE(fixtures::generateLongFixtures(dir).isEmpty());

    Project project("tenmin");
    double lastProgress = -1.0;
    int updates = 0;
    juce::Array<juce::File> files;
    files.add(dir.getChildFile("texture-10min.wav"));
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    project.service->importFiles(files);
    while (project.service->isBusy())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        if (project.jobs.hasJobs() && project.jobs.jobs().front().progress != lastProgress)
        {
            lastProgress = project.jobs.jobs().front().progress;
            ++updates;
        }
    }
    std::printf("10 min import took %.1f s, %d progress updates\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, updates);
    CHECK(project.state.numSources() == 1);
    CHECK(updates >= 3);
    dir.deleteRecursively();
}
