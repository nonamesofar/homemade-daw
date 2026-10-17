#include "WarpService.h"

#include "Arrangement.h"
#include "render/BeatsRenderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

namespace sampler::engine
{
namespace
{
/** What the worker threads and the message thread share. Outlives the service while a job still holds it. */
struct Shared
{
    std::mutex mutex;
    std::set<juce::String> wanted; // keys that some clip still points at
    std::atomic<bool> alive{true};
};
} // namespace

struct WarpService::Impl
{
    Impl(WarpService& o, Arrangement& a, std::function<double()> playhead)
        : owner(o), arrangement(a), playheadBeats(std::move(playhead)), pool(numWorkers)
    {
    }

    class Job final : public juce::ThreadPoolJob
    {
    public:
        Job(Impl& i, std::shared_ptr<Shared> s, BeatsRenderRequest r)
            : juce::ThreadPoolJob("Beats render"), impl(i), shared(std::move(s)), request(std::move(r))
        {
        }

        JobStatus runJob() override
        {
            bool wanted = false;
            {
                const std::lock_guard<std::mutex> lock(shared->mutex);
                wanted = shared->wanted.count(request.key) > 0;
            }
            juce::String error;
            bool ok = true;
            if (wanted && ! shouldExit() && shared->alive)
                ok = render::renderBeatsToFile(request.decodedWav, request.destination, request.params, error,
                                               [this] { return shouldExit() || ! shared->alive; });
            else
                error = {}; // skipped: a newer request replaced this one

            const bool skipped = ! wanted || shouldExit() || ! shared->alive;
            juce::MessageManager::callAsync(
                [shared = shared, impl = &impl, key = request.key, destination = request.destination, ok, skipped, error] {
                    if (shared->alive)
                        impl->finished(key, destination, ok, skipped, error);
                });
            return jobHasFinished;
        }

    private:
        Impl& impl;
        std::shared_ptr<Shared> shared;
        BeatsRenderRequest request;
    };

    void scan()
    {
        auto requests = arrangement.missingBeatsRenders();
        {
            const std::lock_guard<std::mutex> lock(shared->mutex);
            shared->wanted.clear();
            for (const auto& r : requests)
                shared->wanted.insert(r.key);
        }
        // A clip that changed may now ask for a render that failed before: failures only stand for the keys still wanted.
        for (auto it = failed.begin(); it != failed.end();)
            it = std::any_of(requests.begin(), requests.end(), [&](const BeatsRenderRequest& r) { return r.key == it->first; })
                     ? std::next(it)
                     : failed.erase(it);

        const auto playhead = playheadBeats ? playheadBeats() : 0.0;
        std::stable_sort(requests.begin(), requests.end(), [&](const BeatsRenderRequest& a, const BeatsRenderRequest& b) {
            return std::abs(a.startBeat - playhead) < std::abs(b.startBeat - playhead);
        });
        for (const auto& r : requests)
        {
            if (inflight.count(r.key) || failed.count(r.key))
                continue;
            inflight.insert(r.key);
            pool.addJob(new Job(*this, shared, r), true);
        }
    }

    /** On the message thread, when a job is over. */
    void finished(const juce::String& key, const juce::File& destination, bool ok, bool skipped, const juce::String& error)
    {
        inflight.erase(key);
        if (! ok && ! skipped)
        {
            failed[key] = error;
            lastErrorText = error;
            juce::Logger::writeToLog("Beats render failed: " + error);
        }
        bool stillWanted = false;
        {
            const std::lock_guard<std::mutex> lock(shared->mutex);
            stillWanted = shared->wanted.count(key) > 0;
        }
        // A finished file is handed over even if a scan in between no longer lists it as missing (it exists now).
        if (ok && ! skipped && destination.existsAsFile())
            arrangement.reloadFile(destination);
        else if (skipped && stillWanted)
            scan(); // skipped by a scan that has since wanted it again
    }

    WarpService& owner;
    Arrangement& arrangement;
    std::function<double()> playheadBeats;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    juce::ThreadPool pool;
    std::set<juce::String> inflight;        // queued or running (message thread)
    std::map<juce::String, juce::String> failed; // key -> error
    juce::String lastErrorText;
};

WarpService::WarpService(Arrangement& a, std::function<double()> playheadBeats)
    : impl(std::make_unique<Impl>(*this, a, std::move(playheadBeats)))
{
    impl->arrangement.addChangeListener(this);
    impl->scan();
}

WarpService::~WarpService()
{
    impl->arrangement.removeChangeListener(this);
    impl->shared->alive = false;
    {
        const std::lock_guard<std::mutex> lock(impl->shared->mutex);
        impl->shared->wanted.clear();
    }
    impl->pool.removeAllJobs(true, 10000);
}

void WarpService::scan() { impl->scan(); }
bool WarpService::isIdle() const { return impl->inflight.empty(); }
int WarpService::numFailures() const { return static_cast<int>(impl->failed.size()); }
juce::String WarpService::lastError() const { return impl->lastErrorText; }

void WarpService::changeListenerCallback(juce::ChangeBroadcaster*) { impl->scan(); }
} // namespace sampler::engine
