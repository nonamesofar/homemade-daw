#include "EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler::detail
{
namespace
{
/** Runs engine tasks (renders) on a worker thread while pumping the message loop. */
class SamplerUIBehaviour : public te::UIBehaviour
{
public:
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& job) override
    {
        TaskRunner runner(job);
        while (runner.isThreadRunning())
            if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(10))
                break;
    }

private:
    struct TaskRunner : juce::Thread
    {
        explicit TaskRunner(te::ThreadPoolJobWithProgress& j) : juce::Thread(j.getJobName()), task(j) { startThread(); }

        ~TaskRunner() override
        {
            task.signalJobShouldExit();
            waitForThreadToExit(10000);
        }

        void run() override
        {
            while (!threadShouldExit())
                if (task.runJob() == juce::ThreadPoolJob::jobHasFinished)
                    break;
        }

        te::ThreadPoolJobWithProgress& task;
    };
};

/** Input recording arrives at M8; until then no input device is opened. */
class SamplerEngineBehaviour : public te::EngineBehaviour
{
public:
    bool shouldOpenAudioInputByDefault() override { return false; }
};
} // namespace

std::unique_ptr<te::Engine> makeEngine(const juce::String& appName)
{
    return std::make_unique<te::Engine>(appName, std::make_unique<SamplerUIBehaviour>(),
                                        std::make_unique<SamplerEngineBehaviour>());
}
} // namespace sampler::detail
