// M0 "verify at M0" assumptions about Tracktion, as headless tests (tasks 3.1 to 3.4).
// A failing test here is a finding: write the decision in docs/m0-findings.md.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/EditLab.h"

namespace
{
/** Fresh project folder with a source WAV in audio/. */
struct Bundle
{
    explicit Bundle(const juce::String& name)
    {
        root = sampler::test::tempDir(name).getChildFile("proj.sdaw");
        root.deleteRecursively();
        root.getChildFile("audio").createDirectory();
        audio = sampler::test::writeToneWav(root.getChildFile("audio/loop.wav"), 4.0);
        editFile = root.getChildFile("project.tracktionedit");
    }

    juce::File root, audio, editFile;
};

int countFiles(const juce::File& dir)
{
    return dir.exists() ? dir.getNumberOfChildFiles(juce::File::findFiles, "*") : 0;
}
} // namespace

TEST_CASE("clips keep their beat position when the tempo changes", "[engine][assumption][A1]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("a1");

    sampler::EditLab lab;
    lab.newEdit(b.editFile);
    const auto clip = lab.addClip(b.audio, 8.0, 2.0, true);
    REQUIRE(clip == 0);
    REQUIRE(lab.clipStartBeats(clip) == Catch::Approx(8.0));
    REQUIRE(lab.clipStartSeconds(clip) == Catch::Approx(4.0)); // beat 8 at 120 BPM

    lab.setTempo(90.0);

    INFO("after 120 -> 90 BPM: beat " << lab.clipStartBeats(clip) << ", seconds " << lab.clipStartSeconds(clip));
    CHECK(lab.clipStartBeats(clip) == Catch::Approx(8.0));
    CHECK(lab.clipStartSeconds(clip) == Catch::Approx(8.0 * 60.0 / 90.0));
}

TEST_CASE("a moved project folder still finds its audio", "[engine][assumption][A2]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("a2");

    {
        sampler::EditLab lab;
        lab.newEdit(b.editFile);
        REQUIRE(lab.addClip(b.audio, 0.0, 2.0, true) == 0);
        INFO("stored source reference: " << lab.clipSourceReference(0));
        REQUIRE(lab.save());
        CHECK(!juce::File::isAbsolutePath(lab.clipSourceReference(0)));
    }

    const auto movedRoot = b.root.getSiblingFile("moved-and-renamed.sdaw");
    movedRoot.deleteRecursively();
    REQUIRE(b.root.moveFileTo(movedRoot));

    sampler::EditLab reopened;
    REQUIRE(reopened.open(movedRoot.getChildFile("project.tracktionedit")));
    REQUIRE(reopened.numClips() == 1);
    INFO("stored source reference after move: " << reopened.clipSourceReference(0));
    CHECK(reopened.clipSourceResolves(0));
}

TEST_CASE("sampler_* clip properties survive save and reload", "[engine][assumption][A3]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("a3");

    {
        sampler::EditLab lab;
        lab.newEdit(b.editFile);
        REQUIRE(lab.addClip(b.audio, 0.0, 2.0, true) == 0);
        lab.setClipProperty(0, "sampler_sourceId", "src_3fa1c2d9");
        lab.setClipProperty(0, "sampler_warpMode", "beats");
        lab.setClipProperty(0, "sampler_sourceBpm", "90.0");
        REQUIRE(lab.save());
    }

    sampler::EditLab reopened;
    REQUIRE(reopened.open(b.editFile));
    REQUIRE(reopened.numClips() == 1);
    CHECK(reopened.clipProperty(0, "sampler_sourceId") == "src_3fa1c2d9");
    CHECK(reopened.clipProperty(0, "sampler_warpMode") == "beats");
    CHECK(reopened.clipProperty(0, "sampler_sourceBpm") == "90.0");
}

TEST_CASE("thumbnails and proxies can be redirected into the project cache folder", "[engine][assumption][A4]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("a4");
    const auto cache = b.root.getChildFile("cache");

    sampler::EditLab lab;
    REQUIRE(lab.setCacheDirectory(cache));
    lab.newEdit(b.editFile);
    REQUIRE(lab.addClip(b.audio, 0.0, 2.0, true) == 0);

    CHECK(lab.buildThumbnail(0));
    const auto proxyBuilt = lab.buildReverseProxy(0);
    {
        juce::String listing;
        for (const auto& f : cache.findChildFiles(juce::File::findFiles, true))
            listing << "\n  " << f.getRelativePathFrom(cache) << " (" << f.getSize() << " bytes)";
        INFO("files under cache:" << listing);
        CHECK(proxyBuilt);
    }

    INFO("edit temp dir: " << lab.editTempDirectory().getFullPathName());
    INFO("thumbnails dir: " << lab.thumbnailsDirectory().getFullPathName());
    CHECK(lab.editTempDirectory().isAChildOf(cache));
    CHECK(lab.thumbnailsDirectory().isAChildOf(cache));

    // Nothing derived may appear next to the user's audio.
    CHECK(countFiles(b.root.getChildFile("audio")) == 1);
    CHECK(countFiles(b.root) <= 1); // project.tracktionedit only if saved; nothing else at the top level
    CHECK(b.root.getChildFile("cache").isDirectory());
}

TEST_CASE("Rubber Band stretch through the engine: length follows the ratio and the proxy stays in the cache", "[engine][assumption][A4]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("a4-stretch");
    const auto cache = b.root.getChildFile("cache");

    sampler::EditLab lab;
    REQUIRE(lab.setCacheDirectory(cache));
    lab.newEdit(b.editFile);
    REQUIRE(lab.addClip(b.audio, 0.0, 2.0, true) == 0);

    bool usesProxy = false;
    const auto proxies = lab.buildStretchProxy(0, 0.5, usesProxy);
    INFO("uses proxy: " << usesProxy << ", proxy files: " << proxies);
    CHECK(proxies >= 0);

    // Whatever the engine derived stays under cache/, never beside the audio.
    CHECK(countFiles(b.root.getChildFile("audio")) == 1);
    CHECK(lab.editTempDirectory().isAChildOf(cache));
    {
        juce::String listing;
        for (const auto& f : cache.findChildFiles(juce::File::findFiles, true))
            listing << "\n  " << f.getRelativePathFrom(cache) << " (" << f.getSize() << " bytes)";
        std::printf("A4 stretch: uses proxy %d, files under cache:%s\n", usesProxy ? 1 : 0, listing.toRawUTF8());
    }
}
