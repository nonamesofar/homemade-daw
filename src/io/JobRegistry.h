#pragma once

#include <juce_events/juce_events.h>

#include <vector>

namespace sampler::io
{
/** A running background job as the UI shows it: a label, how far along it is, and the names it will produce. */
struct JobInfo
{
    int id = 0;
    juce::String label;                  // "Importing 3 files"
    double progress = -1.0;              // 0..1, or negative when unknown
    juce::StringArray pendingNames;      // sounds still being prepared; the browser shows them as "importing" rows
};

/**
    The list of running background jobs (imports now; renders and exports later). Message thread only: workers report
    through `MessageManager::callAsync`. Broadcasts a change whenever a job is added, updated or removed.
*/
class JobRegistry : public juce::ChangeBroadcaster
{
public:
    int add(const juce::String& label, const juce::StringArray& pendingNames);
    void setProgress(int id, double progress);
    /** One of the job's names is finished (ready, failed or skipped): its "importing" row goes away. */
    void nameDone(int id, const juce::String& name);
    void remove(int id);

    const std::vector<JobInfo>& jobs() const { return running; }
    bool hasJobs() const { return ! running.empty(); }
    /** All names still pending over every job. */
    juce::StringArray allPendingNames() const;

private:
    JobInfo* find(int id);

    std::vector<JobInfo> running;
    int nextId = 1;
};
} // namespace sampler::io
