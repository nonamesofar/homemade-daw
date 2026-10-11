#include "JobRegistry.h"

#include <algorithm>

namespace sampler::io
{
JobInfo* JobRegistry::find(int id)
{
    const auto it = std::find_if(running.begin(), running.end(), [id](const JobInfo& j) { return j.id == id; });
    return it != running.end() ? &*it : nullptr;
}

int JobRegistry::add(const juce::String& label, const juce::StringArray& pendingNames)
{
    JobInfo job;
    job.id = nextId++;
    job.label = label;
    job.pendingNames = pendingNames;
    running.push_back(job);
    sendChangeMessage();
    return job.id;
}

void JobRegistry::setProgress(int id, double progress)
{
    if (auto* job = find(id))
    {
        job->progress = progress;
        sendChangeMessage();
    }
}

void JobRegistry::nameDone(int id, const juce::String& name)
{
    if (auto* job = find(id))
    {
        if (const int index = job->pendingNames.indexOf(name); index >= 0)
            job->pendingNames.remove(index); // one row: kick.wav and kick.aiff share a stem
        sendChangeMessage();
    }
}

void JobRegistry::remove(int id)
{
    running.erase(std::remove_if(running.begin(), running.end(), [id](const JobInfo& j) { return j.id == id; }), running.end());
    sendChangeMessage();
}

juce::StringArray JobRegistry::allPendingNames() const
{
    juce::StringArray names;
    for (const auto& job : running)
        names.addArray(job.pendingNames);
    return names;
}
} // namespace sampler::io
