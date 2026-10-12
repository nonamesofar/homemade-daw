#include "RecordingPolicy.h"

namespace sampler::app
{
bool RecordingPolicy::clearlyOtherDevice(const juce::String& recordedName, const juce::String& engineName, const juce::String& engineType)
{
    // Only Windows audio types give a friendly name that can be compared with the recorded endpoint's. ASIO and unknown
    // types may still play to the same hardware.
    const bool windowsType = engineType.containsIgnoreCase("Windows Audio") || engineType.containsIgnoreCase("DirectSound");
    if (! windowsType || recordedName.isEmpty() || engineName.isEmpty())
        return false;
    return ! (recordedName.containsIgnoreCase(engineName) || engineName.containsIgnoreCase(recordedName));
}

bool RecordingPolicy::shouldMuteOutput(const std::vector<SystemRecording>& systemTakes, const juce::String& engineDeviceName,
                                       const juce::String& engineDeviceType)
{
    for (const auto& take : systemTakes)
        if (take.endpointMode && ! clearlyOtherDevice(take.deviceName, engineDeviceName, engineDeviceType))
            return true;
    return false;
}

juce::String RecordingPolicy::noSignalText(const juce::String& deviceName)
{
    return deviceName.isEmpty() ? juce::String("No sound is coming in") : "No sound is coming in (" + deviceName + ")";
}
} // namespace sampler::app
