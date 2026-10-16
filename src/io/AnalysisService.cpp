#include "io/AnalysisService.h"

#include "io/AtomicFile.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

namespace sampler::io
{
namespace
{
constexpr int readBlock = 65536;

/** Decodes the sound's WAV to mono at its own rate. Empty if it cannot be read (or `cancel` is set). */
std::vector<float> readMono(const juce::File& wav, double& rate, const std::atomic<bool>& cancel)
{
    juce::WavAudioFormat format;
    auto stream = wav.createInputStream();
    if (stream == nullptr)
        return {};
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};
    rate = reader->sampleRate;
    const auto channels = static_cast<int>(reader->numChannels);
    std::vector<float> mono(static_cast<size_t>(reader->lengthInSamples), 0.0f);
    juce::AudioBuffer<float> block(channels, readBlock);
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += readBlock)
    {
        if (cancel.load())
            return {};
        const auto n = static_cast<int>(std::min<juce::int64>(readBlock, reader->lengthInSamples - position));
        if (! reader->read(&block, 0, n, position, true, true))
            return {};
        for (int c = 0; c < channels; ++c)
        {
            const float* in = block.getReadPointer(c);
            float* out = mono.data() + position;
            for (int i = 0; i < n; ++i)
                out[i] += in[i] / static_cast<float>(channels);
        }
    }
    return mono;
}

std::shared_ptr<const SoundAnalysis> analyseFile(const juce::File& wav, const std::atomic<bool>& cancel)
{
    double rate = 0.0;
    const auto mono = readMono(wav, rate, cancel);
    if (mono.empty() || rate <= 0.0)
        return nullptr;

    auto result = std::make_shared<SoundAnalysis>();
    result->sampleRate = rate;
    result->frames = static_cast<juce::int64>(mono.size());
    auto onsets = analysis::detectOnsets(mono.data(), result->frames, rate);
    const auto tempo = analysis::estimateTempo(onsets.onsets, result->frames, rate);
    result->onsets = std::move(onsets.onsets);
    result->tempo = tempo.candidates;
    result->firstBeatFrame = tempo.firstBeatFrame;
    return result;
}
} // namespace

//==============================================================================
juce::String analysisToJson(const SoundAnalysis& a)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("algorithmVersion", a.algorithmVersion);
    root->setProperty("sampleRate", a.sampleRate);
    root->setProperty("frames", a.frames);
    root->setProperty("firstBeatFrame", a.firstBeatFrame);
    juce::Array<juce::var> tempo;
    for (const auto& t : a.tempo)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("bpm", t.bpm);
        o->setProperty("confidence", static_cast<double>(t.confidence));
        tempo.add(juce::var(o));
    }
    root->setProperty("tempo", tempo);
    juce::Array<juce::var> onsets;
    for (const auto& o : a.onsets)
    {
        juce::Array<juce::var> pair;
        pair.add(o.frame);
        pair.add(static_cast<double>(o.strength));
        onsets.add(juce::var(pair));
    }
    root->setProperty("onsets", onsets);
    return juce::JSON::toString(juce::var(root), true);
}

bool analysisFromJson(const juce::String& text, SoundAnalysis& out)
{
    const auto root = juce::JSON::parse(text);
    if (! root.isObject() || ! root.hasProperty("algorithmVersion"))
        return false;
    SoundAnalysis a;
    a.algorithmVersion = root["algorithmVersion"];
    a.sampleRate = root["sampleRate"];
    a.frames = static_cast<juce::int64>(root["frames"]);
    a.firstBeatFrame = static_cast<juce::int64>(root["firstBeatFrame"]);
    if (const auto* tempo = root["tempo"].getArray())
        for (const auto& t : *tempo)
            a.tempo.push_back({t["bpm"], static_cast<float>(static_cast<double>(t["confidence"]))});
    if (const auto* onsets = root["onsets"].getArray())
        for (const auto& o : *onsets)
            if (const auto* pair = o.getArray(); pair != nullptr && pair->size() == 2)
                a.onsets.push_back({static_cast<juce::int64>((*pair)[0]), static_cast<float>(static_cast<double>((*pair)[1]))});
    out = std::move(a);
    return true;
}

//==============================================================================
AnalysisService::AnalysisService(std::function<juce::File()> dir)
    : bundleDir(std::move(dir)), cancelled(std::make_shared<std::atomic<bool>>(false)), pool(1)
{
}

AnalysisService::~AnalysisService()
{
    cancelled->store(true);
    pool.removeAllJobs(true, 10000);
}

juce::String AnalysisService::cacheKey(const juce::File& bundle, const model::Source& source)
{
    const auto hash = source.hash();
    if (hash.startsWith("sha256:") && hash.length() >= 7 + 16)
        return hash.substring(7, 7 + 16);
    // A take has no content hash: the decoded file's path, size and date stand in for it.
    const auto file = bundle.getChildFile(source.decoded());
    const auto text = source.decoded() + "|" + juce::String(file.getSize()) + "|" + juce::String(file.getLastModificationTime().toMilliseconds());
    return juce::SHA256(text.toUTF8()).toHexString().substring(0, 16);
}

juce::File AnalysisService::cacheFile(const juce::File& bundle, const model::Source& source)
{
    return bundle.getChildFile("cache/analysis/" + cacheKey(bundle, source) + ".json");
}

std::shared_ptr<const SoundAnalysis> AnalysisService::find(const model::Source& source)
{
    if (! source.isValid())
        return nullptr;
    const auto bundle = bundleDir();
    const auto key = cacheKey(bundle, source);
    const auto fresh = [&source](const SoundAnalysis& a) {
        return a.algorithmVersion == analysisAlgorithmVersion && std::abs(a.sampleRate - source.sampleRate()) < 0.5
               && a.frames == source.frames();
    };

    if (const auto it = memory.find(key); it != memory.end())
    {
        if (fresh(*it->second))
            return it->second;
        memory.erase(it);
    }
    const auto file = cacheFile(bundle, source);
    if (! file.existsAsFile())
        return nullptr;
    auto loaded = std::make_shared<SoundAnalysis>();
    if (! analysisFromJson(file.loadFileAsString(), *loaded) || ! fresh(*loaded))
        return nullptr;
    memory[key] = loaded;
    return loaded;
}

juce::String AnalysisService::failureSignature(const model::Source& source) const
{
    const auto file = bundleDir().getChildFile(source.decoded());
    return source.decoded() + "|" + juce::String(file.getSize()) + "|" + juce::String(file.getLastModificationTime().toMilliseconds()) + "|"
           + juce::String(source.frames()) + "|" + juce::String(source.sampleRate());
}

bool AnalysisService::hasFailed(const model::Source& source) const
{
    if (! source.isValid())
        return false;
    const auto it = failed.find(cacheKey(bundleDir(), source));
    return it != failed.end() && it->second == failureSignature(source);
}

bool AnalysisService::isAnalysing(const juce::String& sourceId) const
{
    for (const auto& job : running)
        if (job.second == sourceId)
            return true;
    return false;
}

void AnalysisService::request(const model::Source& source)
{
    if (! source.isValid() || find(source) != nullptr || hasFailed(source))
        return;
    const auto bundle = bundleDir();
    const auto key = cacheKey(bundle, source);
    if (running.count(key) > 0)
        return;
    failed.erase(key);
    const auto signature = failureSignature(source);
    const auto frames = source.frames();
    const auto rate = source.sampleRate();
    running[key] = source.id();

    const auto wav = bundle.getChildFile(source.decoded());
    const auto cache = cacheFile(bundle, source);
    const auto sourceId = source.id();
    const auto cancel = cancelled;
    juce::WeakReference<AnalysisService> weak(this);
    pool.addJob([weak, cancel, wav, cache, key, sourceId, signature, frames, rate, this]() mutable {
        auto result = analyseFile(wav, *cancel);
        if (result != nullptr && ! cancel->load())
        {
            computed.fetch_add(1);
            cache.getParentDirectory().createDirectory();
            const auto json = analysisToJson(*result);
            writeFileAtomically(cache, json.toRawUTF8(), json.getNumBytesAsUTF8());
        }
        if (cancel->load())
            return;
        juce::MessageManager::callAsync([weak, key, sourceId, signature, frames, rate, result] {
            if (weak != nullptr)
                weak->finish(key, sourceId, signature, frames, rate, result);
        });
    });
}

void AnalysisService::finish(const juce::String& key, const juce::String& sourceId, const juce::String& signature, juce::int64 frames,
                             double sampleRate, std::shared_ptr<const SoundAnalysis> result)
{
    running.erase(key);
    // Unreadable, empty, or not the sound the project describes (`find` would turn it down and the editor ask again forever).
    if (result == nullptr || result->frames <= 0 || result->frames != frames || std::abs(result->sampleRate - sampleRate) >= 0.5)
    {
        failed[key] = signature;
        if (onFailed)
            onFailed(sourceId);
        return;
    }
    memory[key] = std::move(result);
    if (onAnalysed)
        onAnalysed(sourceId);
}
} // namespace sampler::io
