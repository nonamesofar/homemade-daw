#pragma once

#include "analysis/OnsetDetector.h"
#include "analysis/TempoEstimator.h"
#include "model/SamplerState.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace sampler::io
{
/** Bump when the detector or the estimator changes in a way that makes old cache files wrong. */
constexpr int analysisAlgorithmVersion = 1;

/** Hits and tempo guesses of one sound: derived data, kept only in the project's cache folder. */
struct SoundAnalysis
{
    int algorithmVersion = analysisAlgorithmVersion;
    double sampleRate = 0.0;
    juce::int64 frames = 0;
    std::vector<analysis::Onset> onsets;           // every candidate hit (sorted), see analysis::selectTransients
    std::vector<analysis::TempoCandidate> tempo;   // best first, at most three; empty when there is no tempo
    juce::int64 firstBeatFrame = 0;

    bool hasTempo() const { return ! tempo.empty(); }
    double bpm() const { return tempo.empty() ? 0.0 : tempo.front().bpm; }
};

juce::String analysisToJson(const SoundAnalysis&);
/** False if the text is not an analysis file. */
bool analysisFromJson(const juce::String& text, SoundAnalysis& out);

/**
    Finds the hits and the tempo of the project's sounds on a worker (design D2). Results live in memory and in
    `cache/analysis/<key>.json`; nothing here ever touches the project model, so analysing marks nothing as unsaved and
    creates no undo step. Message thread only, except the worker job.
*/
class AnalysisService
{
public:
    /** `bundleDir` is asked whenever a path is needed (Save As of an untitled project moves the bundle). */
    explicit AnalysisService(std::function<juce::File()> bundleDir);
    /** Stops the worker (a running job finishes its current block and is dropped). */
    ~AnalysisService();

    /**
        The analysis of a sound if there is one: from memory, else from a fresh cache file (same algorithm version,
        same rate and length as the sound). Null otherwise.
    */
    std::shared_ptr<const SoundAnalysis> find(const model::Source&);

    /**
        Starts a job unless a fresh result exists, one is running for this sound, or the last attempt failed for the
        file as it is now (unreadable, empty, or not the length and rate the project records). A changed file is tried again.
    */
    void request(const model::Source&);

    /** True if the last attempt for this sound failed and nothing about its file has changed since. */
    bool hasFailed(const model::Source&) const;
    bool isAnalysing(const juce::String& sourceId) const;
    bool isBusy() const { return ! running.empty(); }
    /** Number of analyses computed (not read from the cache) since the service was made. */
    int computedCount() const { return computed.load(); }

    /** Message thread. Called when a sound's analysis became available. */
    std::function<void(const juce::String& sourceId)> onAnalysed;
    /** Message thread. Called when a sound could not be analysed (see `hasFailed`). */
    std::function<void(const juce::String& sourceId)> onFailed;

    /** The cache file of a sound: `cache/analysis/<key>.json`. */
    static juce::File cacheFile(const juce::File& bundleDir, const model::Source&);
    /** The first 16 hex characters of the content hash, else a hash of the decoded file's path, size and date. */
    static juce::String cacheKey(const juce::File& bundleDir, const model::Source&);

private:
    void finish(const juce::String& key, const juce::String& sourceId, const juce::String& signature, juce::int64 frames, double sampleRate,
                std::shared_ptr<const SoundAnalysis> result);
    /** What identifies the file and the project's record of it: a failure is remembered for exactly this. */
    juce::String failureSignature(const model::Source&) const;

    std::function<juce::File()> bundleDir;
    std::map<juce::String, std::shared_ptr<const SoundAnalysis>> memory; // by cache key
    std::map<juce::String, juce::String> failed;                         // cache key -> failureSignature
    std::map<juce::String, juce::String> running;                        // cache key -> source id, jobs in flight
    std::atomic<int> computed{0};
    std::shared_ptr<std::atomic<bool>> cancelled;
    juce::ThreadPool pool;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AnalysisService)
};
} // namespace sampler::io
