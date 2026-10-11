// Golden renders (design D-11): small fixed plugin-free projects are built here through the real commands, rendered
// offline at 48 kHz / block 512, and compared with tests/golden/<case>.wav (same length, peak difference at or below
// -90 dBFS). SAMPLER_UPDATE_GOLDEN=1 rewrites the references: listen to them before committing (see README.md).
#include "../arrange/ArrangeFixture.h"

#include "engine/OfflineRender.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <limits>

using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;
using C = cmd::SetTrackParam::Control;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

/** Records a fixture file as a sound of the project (copied into audio/, as the import would leave it). */
juce::String addFixture(ArrangeFixture& f, const char* fileName, const juce::String& id)
{
    const auto source = fixture(fileName);
    const auto decoded = "audio/" + juce::String(fileName);
    REQUIRE(source.copyFileTo(f.project->bundleDir().getChildFile(decoded)));
    auto reader = f.host.openReader(f.project->bundleDir().getChildFile(decoded));
    REQUIRE(reader != nullptr);
    sampler::model::SourceData data;
    data.id = id;
    data.name = source.getFileNameWithoutExtension();
    data.original = decoded;
    data.decoded = decoded;
    data.hash = "sha256:" + id;
    data.sampleRate = reader->sampleRate;
    data.channels = static_cast<int>(reader->numChannels);
    data.frames = reader->lengthInSamples;
    f.commands->apply(std::make_unique<cmd::AddSources>(std::vector<sampler::model::SourceData>{data}));
    return id;
}

struct Sounds
{
    juce::String loop, kick, bass;
};

Sounds addSounds(ArrangeFixture& f)
{
    return {addFixture(f, "drums-loop-120.wav", "src_loop"), addFixture(f, "kick.wav", "src_kick"),
            addFixture(f, "bass-phrase.flac", "src_bass")};
}

void moreTracks(ArrangeFixture& f, int count)
{
    for (int i = 0; i < count; ++i)
        f.commands->apply(std::make_unique<cmd::AddTrack>());
}

struct Case
{
    const char* name;
    std::function<void(ArrangeFixture&, const Sounds&)> build;
};

const std::vector<Case>& cases()
{
    static const std::vector<Case> all{
        {"four-tracks",
         [](ArrangeFixture& f, const Sounds& s) {
             moreTracks(f, 3);
             f.place(s.loop, 0, 0.0);
             f.place(s.loop, 0, 8.0);
             f.place(s.bass, 1, 4.0);
             f.place(s.kick, 2, 2.0);
             f.place(s.kick, 2, 6.0);
             f.place(s.bass, 3, 12.0);
         }},
        {"overlap-trim",
         [](ArrangeFixture& f, const Sounds& s) {
             f.place(s.loop, 0, 0.0);
             f.place(s.kick, 0, 3.0); // lands inside the loop: it is split around the kick
         }},
        {"split",
         [](ArrangeFixture& f, const Sounds& s) {
             const auto loop = f.place(s.loop, 0, 0.0);
             f.commands->apply(std::make_unique<cmd::SplitClips>(std::vector<juce::String>{loop}, 4.0));
         }},
        {"left-trim",
         [](ArrangeFixture& f, const Sounds& s) {
             const auto loop = f.place(s.loop, 0, 0.0);
             f.commands->apply(std::make_unique<cmd::TrimClip>(loop, 4.0, 8.0, 0.25));
         }},
        {"clip-gain",
         [](ArrangeFixture& f, const Sounds& s) {
             const auto loop = f.place(s.loop, 0, 0.0);
             f.commands->apply(std::make_unique<cmd::SetClipGain>(loop, -6.0));
         }},
        {"reverse",
         [](ArrangeFixture& f, const Sounds& s) {
             const auto loop = f.place(s.loop, 0, 0.0);
             f.commands->apply(std::make_unique<cmd::SetClipReversed>(loop, true));
         }},
        {"volume-pan",
         [](ArrangeFixture& f, const Sounds& s) {
             moreTracks(f, 1);
             f.place(s.loop, 0, 0.0);
             f.place(s.bass, 1, 0.0);
             f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(0), C::volume, -4.5));
             f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), C::pan, -0.7));
         }},
        {"mute-solo",
         [](ArrangeFixture& f, const Sounds& s) {
             moreTracks(f, 2);
             f.place(s.loop, 0, 0.0);
             f.place(s.bass, 1, 0.0);
             f.place(s.kick, 2, 0.0);
             f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(0), C::mute, 1.0));
             f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), C::solo, 1.0));
         }},
        {"master-volume",
         [](ArrangeFixture& f, const Sounds& s) {
             f.place(s.loop, 0, 0.0);
             f.commands->apply(std::make_unique<cmd::SetTrackParam>(juce::String(), C::volume, -8.0));
         }},
        {"tempo-change",
         [](ArrangeFixture& f, const Sounds& s) {
             moreTracks(f, 1);
             f.place(s.loop, 0, 0.0);
             f.place(s.bass, 1, 8.0);
             f.commands->apply(std::make_unique<cmd::SetTempo>(90.0));
         }},
    };
    return all;
}

/** Peak of the difference between two renders in dBFS (+infinity when the shapes differ). */
double differenceDb(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
        return std::numeric_limits<double>::infinity();
    float peak = 0.0f;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(a.getSample(c, i) - b.getSample(c, i)));
    return juce::Decibels::gainToDecibels(peak, -200.0f);
}
} // namespace

TEST_CASE("golden renders match their references", "[golden]")
{
    const juce::File goldenDir(SAMPLER_GOLDEN_DIR);
    const bool update = juce::SystemStats::getEnvironmentVariable("SAMPLER_UPDATE_GOLDEN", {}).isNotEmpty();
    // Used to see a failure on purpose: SAMPLER_GOLDEN_GAIN_DB adds clip gain to every clip of every case.
    const auto extraGain = juce::SystemStats::getEnvironmentVariable("SAMPLER_GOLDEN_GAIN_DB", {});

    for (const auto& golden : cases())
    {
        DYNAMIC_SECTION(golden.name)
        {
            ArrangeFixture f(juce::String("golden-") + golden.name);
            const auto sounds = addSounds(f);
            golden.build(f, sounds);
            if (extraGain.isNotEmpty())
                for (const auto& c : f.arrangement().clips())
                    f.commands->apply(std::make_unique<cmd::SetClipGain>(c.id, c.gainDb + extraGain.getDoubleValue()));
            REQUIRE(f.violations.isEmpty());
            ArrangeFixture::pump();

            const auto out = f.root.getChildFile("render.wav");
            juce::String error;
            sampler::engine::RenderOptions options; // 48 kHz, block 512
            REQUIRE(sampler::engine::renderToWav(*f.project, out, options, error));
            const auto rendered = sampler::test::readWav(out);
            REQUIRE(rendered.getNumSamples() > 0);

            const auto reference = goldenDir.getChildFile(juce::String(golden.name) + ".wav");
            if (update)
            {
                goldenDir.createDirectory();
                reference.deleteFile();
                REQUIRE(out.copyFileTo(reference));
                std::printf("golden: wrote %s\n", reference.getFullPathName().toRawUTF8());
                continue;
            }
            REQUIRE(reference.existsAsFile());
            const auto expected = sampler::test::readWav(reference);
            const auto diff = differenceDb(rendered, expected);
            INFO("length " << rendered.getNumSamples() << " vs reference " << expected.getNumSamples() << ", difference "
                           << diff << " dBFS (limit -90)");
            CHECK(rendered.getNumSamples() == expected.getNumSamples());
            CHECK(diff <= -90.0);
        }
    }
}

#ifdef SAMPLER_CLI_PATH
TEST_CASE("sampler-cli renders a generated project, and refuses a folder that is not a project", "[golden][cli]")
{
    const auto root = sampler::test::freshTempDir("golden-cli");
    juce::File bundle;
    {
        ArrangeFixture f("golden-cli-project");
        const auto sounds = addSounds(f);
        f.place(sounds.loop, 0, 0.0);
        juce::String error;
        bundle = f.root.getChildFile("Cli.sdaw");
        REQUIRE(f.project->saveAs(bundle, error));
        f.commands.reset();
        f.project.reset();

        // The in-process render of the same project, to compare with the tool's file.
        auto project = sampler::ProjectEdit::open(f.host, bundle, error);
        REQUIRE(project != nullptr);
        REQUIRE(sampler::engine::renderToWav(*project, root.getChildFile("inprocess.wav"), {}, error));
        project.reset();
        REQUIRE(bundle.copyDirectoryTo(root.getChildFile("Cli.sdaw")));
    }

    const auto run = [](const juce::StringArray& args, juce::String& output) {
        juce::ChildProcess process;
        juce::StringArray command{SAMPLER_CLI_PATH};
        command.addArray(args);
        REQUIRE(process.start(command));
        output = process.readAllProcessOutput();
        REQUIRE(process.waitForProcessToFinish(120000));
        return static_cast<int>(process.getExitCode());
    };

    juce::String output;
    const auto toolOut = root.getChildFile("tool.wav");
    CHECK(run({"render", root.getChildFile("Cli.sdaw").getFullPathName(), toolOut.getFullPathName()}, output) == 0);
    REQUIRE(toolOut.existsAsFile());
    CHECK(differenceDb(sampler::test::readWav(toolOut), sampler::test::readWav(root.getChildFile("inprocess.wav"))) <= -90.0);

    const auto notAProject = root.getChildFile("plain-folder");
    notAProject.createDirectory();
    const auto badOut = root.getChildFile("bad.wav");
    CHECK(run({"render", notAProject.getFullPathName(), badOut.getFullPathName()}, output) != 0);
    CHECK_FALSE(badOut.existsAsFile());
    CHECK(output.contains("not a Sampler project"));
    CHECK(run({"nonsense"}, output) == 2);
    const auto dangling = root.getChildFile("dangling.wav");
    CHECK(run({"render", root.getChildFile("Cli.sdaw").getFullPathName(), dangling.getFullPathName(), "--rate"}, output) == 2);
    CHECK(output.contains("needs a value"));
    CHECK(run({"render", root.getChildFile("Cli.sdaw").getFullPathName(), dangling.getFullPathName(), "--bogus", "1"}, output) == 2);
    CHECK_FALSE(dangling.existsAsFile());
}
#endif

// Not a test: with SAMPLER_MAKE_DEMO=<folder>.sdaw set, saves the "four-tracks" case as a project to open in the app
// (used by the demo scripts and the manual QA checklist).
TEST_CASE("save the four-tracks case as a demo project", "[golden][demo]")
{
    const auto target = juce::SystemStats::getEnvironmentVariable("SAMPLER_MAKE_DEMO", {});
    if (target.isEmpty())
        SKIP("set SAMPLER_MAKE_DEMO to a .sdaw folder to create it");
    ArrangeFixture f("golden-demo");
    const auto sounds = addSounds(f);
    cases().front().build(f, sounds);
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{4.0, 12.0}));
    juce::String error;
    REQUIRE(f.project->saveAs(juce::File(target), error));
    INFO(error);
}
