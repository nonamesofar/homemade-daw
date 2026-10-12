// WASAPI loopback capture, outside JUCE's device layer.
//
//   capture thread (MMCSS "Capture", event driven) -> TakeWriter (SPSC ring 10 s -> writer thread -> WAV)
//
// Loopback sends no packets while nothing plays, so gaps are measured from each packet's QPC timestamp
// and filled with silence, keeping the file as long as the elapsed time.
#include "../CaptureGap.h"
#include "../ICaptureSource.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <windows.h>

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#include <propsys.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <cstring>
#include <vector>

namespace sampler::platform
{
using Microsoft::WRL::ComPtr;

namespace
{
juce::String hresultText(const char* what, HRESULT hr)
{
    return juce::String(what) + " (0x" + juce::String::toHexString(static_cast<int>(hr)) + ")";
}

/** Completion handler for ActivateAudioInterfaceAsync (agile: called from a worker thread). */
class ActivationHandler
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler>
{
public:
    ActivationHandler() : done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() override { CloseHandle(done); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation*) override
    {
        SetEvent(done);
        return S_OK;
    }

    HANDLE done;
};

/** Process loopback: everything the device plays except this process. Needs Windows build 20348 or later. */
HRESULT activateProcessLoopback(ComPtr<IAudioClient>& client)
{
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams{};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(params);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                             &activateParams, handler.Get(), &operation);
    if (FAILED(hr))
        return hr;

    if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);

    HRESULT activateResult = E_FAIL;
    ComPtr<IUnknown> unknown;
    hr = operation->GetActivateResult(&activateResult, &unknown);
    if (FAILED(hr))
        return hr;
    if (FAILED(activateResult))
        return activateResult;

    return unknown.As(&client);
}

/** PKEY_Device_FriendlyName spelled out so no extra library is needed. */
const PROPERTYKEY kFriendlyNameKey = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

ComPtr<IMMDeviceEnumerator> createEnumerator()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    return enumerator;
}

/** The device with this endpoint id, or the default render device when `deviceId` is empty. */
HRESULT openDevice(const juce::String& deviceId, ComPtr<IMMDevice>& device)
{
    auto enumerator = createEnumerator();
    if (enumerator == nullptr)
        return E_FAIL;
    if (deviceId.isEmpty())
        return enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    return enumerator->GetDevice(deviceId.toWideCharPointer(), &device);
}

juce::String friendlyName(IMMDevice& device)
{
    ComPtr<IPropertyStore> store;
    if (FAILED(device.OpenPropertyStore(STGM_READ, &store)))
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    juce::String name;
    if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
        name = juce::String(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

juce::String endpointId(IMMDevice& device)
{
    LPWSTR id = nullptr;
    juce::String text;
    if (SUCCEEDED(device.GetId(&id)) && id != nullptr)
        text = juce::String(id);
    if (id != nullptr)
        CoTaskMemFree(id);
    return text;
}

HRESULT activateEndpointLoopback(ComPtr<IAudioClient>& client, const juce::String& deviceId = {}, juce::String* nameOut = nullptr)
{
    ComPtr<IMMDevice> device;
    HRESULT hr = openDevice(deviceId, device);
    if (FAILED(hr))
        return hr;
    if (nameOut != nullptr)
        *nameOut = friendlyName(*device.Get());

    return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
}

/** Sample rate of the default render endpoint's mix format (used to pick the format for process loopback). */
double defaultEndpointRate()
{
    ComPtr<IAudioClient> client;
    if (FAILED(activateEndpointLoopback(client)))
        return 48000.0;
    WAVEFORMATEX* mix = nullptr;
    double rate = 48000.0;
    if (SUCCEEDED(client->GetMixFormat(&mix)) && mix != nullptr)
    {
        rate = static_cast<double>(mix->nSamplesPerSec);
        CoTaskMemFree(mix);
    }
    return rate;
}

/**
    How each channel of a packet goes into the left or right output channel when there are more than two (the order
    of the channels in a packet follows the bits of the channel mask). Two or fewer channels pass through.
*/
std::vector<float> downmixWeights(int channelCount, DWORD mask)
{
    std::vector<float> w(static_cast<size_t>(channelCount) * 2, 0.0f); // [channel * 2 + side]
    int channel = 0;
    for (int bit = 0; bit < 32 && channel < channelCount; ++bit)
    {
        if (mask != 0 && (mask & (1u << bit)) == 0)
            continue;
        const auto flag = 1u << bit;
        const bool left = flag == SPEAKER_FRONT_LEFT || flag == SPEAKER_BACK_LEFT || flag == SPEAKER_FRONT_LEFT_OF_CENTER
                          || flag == SPEAKER_SIDE_LEFT || flag == SPEAKER_TOP_FRONT_LEFT || flag == SPEAKER_TOP_BACK_LEFT;
        const bool right = flag == SPEAKER_FRONT_RIGHT || flag == SPEAKER_BACK_RIGHT || flag == SPEAKER_FRONT_RIGHT_OF_CENTER
                           || flag == SPEAKER_SIDE_RIGHT || flag == SPEAKER_TOP_FRONT_RIGHT || flag == SPEAKER_TOP_BACK_RIGHT;
        const bool frontPair = flag == SPEAKER_FRONT_LEFT || flag == SPEAKER_FRONT_RIGHT;
        const float gain = frontPair ? 1.0f : 0.7071f;
        const auto base = static_cast<size_t>(channel) * 2;
        if (left)
            w[base] = gain;
        else if (right)
            w[base + 1] = gain;
        else // centre, LFE, top/back centre: into both
        {
            const float g = flag == SPEAKER_LOW_FREQUENCY ? 0.5f : 0.7071f;
            w[base] = w[base + 1] = g;
        }
        ++channel;
    }
    for (; channel < channelCount; ++channel) // no mask: alternate left and right
        w[static_cast<size_t>(channel) * 2 + static_cast<size_t>(channel & 1)] = 1.0f;
    return w;
}

/** KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled out so no extra library is needed. */
const GUID kSubtypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

juce::int64 qpcNow100ns()
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return static_cast<juce::int64>(static_cast<double>(now.QuadPart) * 1.0e7 / static_cast<double>(freq.QuadPart));
}
} // namespace

class WasapiLoopbackCapture final : public ICaptureSource
{
public:
    ~WasapiLoopbackCapture() override { stop(); }

    juce::Result start(const juce::File& destination, const CaptureOptions& options) override
    {
        if (isRecording())
            return juce::Result::fail("already recording");
        stop(); // clean up after a capture that ended by itself (device lost) and was never stopped
        if (takeUsed) // TakeWriter writes one take per instance and never reopens a finished file
            return juce::Result::fail("this capture source was already used: create a new one for each take");

        previousEndFrame = 0;
        destinationFile = destination;
        captureOptions = options;
        startResult = juce::Result::ok();
        started.reset();
        streamStarted = false;
        {
            const juce::ScopedLock lock(statsLock);
            statsData = {};
        }
        peak = 0.0f;
        silenceFrames = 0;
        silenceDropped = 0;
        gapFills = 0;
        largestGap = 0;
        discontinuities = 0;
        finishedTake = {};

        captureThread = std::make_unique<CaptureThread>(*this);
        captureThread->startThread(juce::Thread::Priority::highest);

        // Wait until the capture thread has either started the stream or failed.
        const bool answered = started.wait(10000);
        if (!answered || startResult.failed())
        {
            const auto result = answered ? startResult : juce::Result::fail("timed out starting capture");
            stopThreads();                // also stops the writer if the file was already open
            destinationFile.deleteFile(); // no half-made recording is left behind
            return result;
        }
        return juce::Result::ok();
    }

    juce::Result stop() override
    {
        if (captureThread == nullptr && ! take_.isOpen())
            return juce::Result::ok();

        stopThreads();

        const juce::ScopedLock lock(statsLock);
        if (finishedTake.writeFailed && ! statsData.writeFailed)
        {
            statsData.writeFailed = true;
            appendError("writing " + destinationFile.getFileName() + " failed (disk full?): the recording is incomplete");
        }
        return statsData.error.isEmpty() ? juce::Result::ok() : juce::Result::fail(statsData.error);
    }

    bool isRecording() const override { return captureThread != nullptr && captureThread->isThreadRunning(); }

    CaptureStats stats() const override
    {
        const juce::ScopedLock lock(statsLock);
        auto s = statsData;
        s.framesWritten = take_.framesKept();
        s.silenceFramesInserted = silenceFrames;
        s.silenceFramesDropped = silenceDropped;
        s.overruns = take_.overruns();
        s.gapFills = gapFills;
        s.largestGapFrames = largestGap;
        s.discontinuities = discontinuities;
        s.writeFailed = s.writeFailed || take_.writeFailed();
        s.limitReached = take_.limitReached();
        s.peak = peak;
        return s;
    }

    void discardBefore(double clockMs) override
    {
        double startedAt = 0.0;
        {
            const juce::ScopedLock lock(statsLock);
            startedAt = statsData.startedAtMs;
        }
        if (std::isinf(clockMs))
        {
            take_.keepFrom((std::numeric_limits<juce::int64>::max)());
            return;
        }
        const auto frames = static_cast<juce::int64>((clockMs - startedAt) * take_.options().sampleRate / 1000.0);
        take_.keepFrom(juce::jmax<juce::int64>(0, frames));
    }

    const TakeWriter& take() const override { return take_; }
    TakeResult takeResult() const override { return finishedTake; }

private:
    //==============================================================================
    class CaptureThread final : public juce::Thread
    {
    public:
        explicit CaptureThread(WasapiLoopbackCapture& o)
            : juce::Thread("WASAPI loopback capture"), owner(o), wakeEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr))
        {
        }
        ~CaptureThread() override { CloseHandle(wakeEvent); }

        void run() override
        {
            const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            DWORD taskIndex = 0;
            HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);

            const auto result = capture();

            if (result.failed())
            {
                if (!owner.streamStarted)
                {
                    owner.startResult = result;
                    owner.started.signal();
                }
                else
                {
                    const juce::ScopedLock lock(owner.statsLock);
                    owner.appendError(result.getErrorMessage());
                }
            }

            if (mmcss != nullptr)
                AvRevertMmThreadCharacteristics(mmcss);
            if (SUCCEEDED(coInit))
                CoUninitialize();
        }

        HANDLE wakeEvent;

    private:
        juce::Result capture()
        {
            ComPtr<IAudioClient> client;
            juce::String processFailure;
            juce::String deviceName;
            bool processLoopback = false;
            double rate = 0.0;
            int channelCount = 2;
            DWORD channelMask = 0;
            const bool defaultDevice = owner.captureOptions.deviceId.isEmpty();

            // Process loopback follows the Windows default device, so it is only used for "the default device".
            if (owner.captureOptions.excludeOwnProcess && defaultDevice)
            {
                const HRESULT hr = owner.captureOptions.simulateProcessLoopbackFailure ? E_NOTIMPL : activateProcessLoopback(client);
                if (SUCCEEDED(hr))
                {
                    processLoopback = true;
                    rate = defaultEndpointRate();
                    ComPtr<IMMDevice> device;
                    if (SUCCEEDED(openDevice({}, device)))
                        deviceName = friendlyName(*device.Get());
                }
                else
                {
                    processFailure = hresultText("process loopback unavailable", hr);
                    client.Reset();
                }
            }

            WAVEFORMATEXTENSIBLE wanted{};
            WAVEFORMATEX* mix = nullptr;
            const WAVEFORMATEX* format = nullptr;

            if (!processLoopback)
            {
                const HRESULT hr = activateEndpointLoopback(client, owner.captureOptions.deviceId, &deviceName);
                if (FAILED(hr))
                    return juce::Result::fail(hresultText(defaultDevice ? "cannot open the default output device" : "cannot open the output device", hr));
                if (FAILED(client->GetMixFormat(&mix)) || mix == nullptr)
                    return juce::Result::fail("cannot read the output device format");
                format = mix;
                rate = static_cast<double>(mix->nSamplesPerSec);
                channelCount = static_cast<int>(mix->nChannels);
                if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                    channelMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;
            }
            else
            {
                // Process loopback has no mix format: ask for 32-bit float stereo at the device rate.
                wanted.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                wanted.Format.nChannels = 2;
                wanted.Format.nSamplesPerSec = static_cast<DWORD>(rate);
                wanted.Format.wBitsPerSample = 32;
                wanted.Format.nBlockAlign = 8;
                wanted.Format.nAvgBytesPerSec = wanted.Format.nSamplesPerSec * 8;
                wanted.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                wanted.Samples.wValidBitsPerSample = 32;
                wanted.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                wanted.SubFormat = kSubtypeIeeeFloat;
                format = &wanted.Format;
                channelCount = 2;
            }

            const bool isFloat = [&]
            {
                if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                    return true;
                if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                    return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat == kSubtypeIeeeFloat;
                return false;
            }();
            const int bitsPerSample = format->wBitsPerSample;
            if (!isFloat && bitsPerSample != 16)
            {
                if (mix != nullptr)
                    CoTaskMemFree(mix);
                return juce::Result::fail("unsupported device format (" + juce::String(bitsPerSample) + " bit, tag "
                                          + juce::String(format->wFormatTag) + ")");
            }

            HANDLE bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            if (processLoopback)
                flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

            HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000 /* 200 ms */, 0, format, nullptr);
            if (mix != nullptr)
            {
                CoTaskMemFree(mix);
                mix = nullptr;
            }
            if (FAILED(hr))
            {
                CloseHandle(bufferEvent);
                if (processLoopback)
                    return juce::Result::fail(hresultText("process loopback Initialize failed", hr));
                return juce::Result::fail(hresultText("loopback Initialize failed", hr));
            }

            REFERENCE_TIME latency = 0;
            double latencySeconds = 0.0;
            if (SUCCEEDED(client->GetStreamLatency(&latency)))
                latencySeconds = static_cast<double>(latency) / 1.0e7;

            ComPtr<IAudioCaptureClient> captureClient;
            if (FAILED(client->SetEventHandle(bufferEvent)) || FAILED(client->GetService(IID_PPV_ARGS(&captureClient))))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail("cannot get the capture service");
            }

            const int outChannels = juce::jmin(channelCount, 2);
            if (!owner.openOutput(rate, outChannels))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail("cannot create " + owner.destinationFile.getFullPathName());
            }

            {
                const juce::ScopedLock lock(owner.statsLock);
                owner.statsData.mode = processLoopback ? "process-loopback" : "endpoint-loopback";
                owner.statsData.processLoopbackFailure = processFailure;
                owner.statsData.deviceName = deviceName;
                owner.statsData.sampleRate = rate;
                owner.statsData.channels = outChannels;
                owner.statsData.streamLatencySeconds = latencySeconds;
            }

            std::vector<float> weights;
            if (channelCount > 2)
                weights = downmixWeights(channelCount, channelMask);

            const auto qpcStart = qpcNow100ns();
            const auto startedAtMs = juce::Time::getMillisecondCounterHiRes();
            hr = owner.captureOptions.simulateStreamStartFailure ? E_FAIL : client->Start();
            if (FAILED(hr))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail(hresultText("Start failed", hr)); // start() stops the writer, deletes the file
            }
            {
                const juce::ScopedLock lock(owner.statsLock);
                owner.statsData.startedAtMs = startedAtMs;
            }
            owner.streamStarted = true;
            owner.startResult = juce::Result::ok();
            owner.started.signal();

            HANDLE handles[2] = {bufferEvent, wakeEvent};
            std::vector<float> floats, mixed;

            while (!threadShouldExit() && SUCCEEDED(hr))
            {
                // Silent periods raise no event, hence the timeout.
                WaitForMultipleObjects(2, handles, FALSE, 50);
                hr = drain(*captureClient.Get(), qpcStart, rate, channelCount, weights, isFloat, floats, mixed);
            }

            client->Stop();
            if (SUCCEEDED(hr))
                hr = drain(*captureClient.Get(), qpcStart, rate, channelCount, weights, isFloat, floats, mixed);
            CloseHandle(bufferEvent);

            if (FAILED(hr))
            {
                // The device went away (or the stream broke): what was recorded so far stays in the file.
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
                {
                    const juce::ScopedLock lock(owner.statsLock);
                    owner.statsData.deviceLost = true;
                    return juce::Result::fail(hresultText("the output device went away; capture stopped", hr));
                }
                return juce::Result::fail(hresultText("capture failed; capture stopped", hr));
            }

            // Silence at the end (nothing played until stop) still counts as recorded time.
            const auto elapsedFrames = static_cast<juce::int64>(static_cast<double>(qpcNow100ns() - qpcStart) * rate / 1.0e7);
            owner.addSilence(CaptureGap::framesToInsert(owner.previousEndFrame, elapsedFrames, false, rate));
            return juce::Result::ok();
        }

        /** Reads every packet the device has. Returns the first failure (e.g. device lost) or S_OK. */
        HRESULT drain(IAudioCaptureClient& captureClient, juce::int64 qpcStart, double rate, int channelCount,
                      const std::vector<float>& weights, bool isFloat, std::vector<float>& floats, std::vector<float>& mixed)
        {
            for (;;)
            {
                UINT32 packetFrames = 0;
                HRESULT hr = captureClient.GetNextPacketSize(&packetFrames);
                if (FAILED(hr))
                    return hr;
                if (packetFrames == 0)
                    return S_OK;

                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                UINT64 devicePosition = 0, qpcPosition = 0;
                hr = captureClient.GetBuffer(&data, &frames, &bufferFlags, &devicePosition, &qpcPosition);
                if (FAILED(hr))
                    return hr;
                if (hr == AUDCLNT_S_BUFFER_EMPTY)
                    return S_OK;

                if ((bufferFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0)
                    ++owner.discontinuities;

                if ((bufferFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0)
                {
                    // The QPC position of this packet is not valid: no gap can be measured, the packet follows the last.
                    owner.previousEndFrame += static_cast<juce::int64>(frames);
                }
                else
                {
                    // Fill the hole before this packet (nothing was playing) with silence; clock drift is not a hole.
                    const auto packetFrame =
                        static_cast<juce::int64>(static_cast<double>(static_cast<juce::int64>(qpcPosition) - qpcStart) * rate / 1.0e7);
                    owner.addSilence(CaptureGap::framesToInsert(owner.previousEndFrame, packetFrame,
                                                                (bufferFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0, rate));
                    owner.previousEndFrame = packetFrame + static_cast<juce::int64>(frames);
                }

                floats.assign(static_cast<size_t>(frames) * static_cast<size_t>(channelCount), 0.0f);
                if ((bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr)
                {
                    const auto count = floats.size();
                    if (isFloat)
                    {
                        std::memcpy(floats.data(), data, count * sizeof(float));
                    }
                    else
                    {
                        const auto* s = reinterpret_cast<const juce::int16*>(data);
                        for (size_t i = 0; i < count; ++i)
                            floats[i] = static_cast<float>(s[i]) / 32768.0f;
                    }
                }

                if (channelCount > 2)
                {
                    mixed.assign(static_cast<size_t>(frames) * 2, 0.0f);
                    for (size_t f = 0; f < frames; ++f)
                        for (int c = 0; c < channelCount; ++c)
                        {
                            const auto v = floats[f * static_cast<size_t>(channelCount) + static_cast<size_t>(c)];
                            mixed[f * 2] += v * weights[static_cast<size_t>(c) * 2];
                            mixed[f * 2 + 1] += v * weights[static_cast<size_t>(c) * 2 + 1];
                        }
                    owner.push(mixed.data(), 2, static_cast<int>(frames));
                }
                else
                {
                    owner.push(floats.data(), channelCount, static_cast<int>(frames));
                }
                hr = captureClient.ReleaseBuffer(frames);
                if (FAILED(hr))
                    return hr;
            }
        }

        WasapiLoopbackCapture& owner;
    };

    //==============================================================================
    /** Joins the capture thread, then lets the writer drain the ring and finish the file. */
    void stopThreads()
    {
        if (captureThread != nullptr)
        {
            captureThread->signalThreadShouldExit();
            SetEvent(captureThread->wakeEvent);
            captureThread->stopThread(5000);
            captureThread.reset();
        }

        if (take_.isOpen())
        {
            finishedTake = take_.finish();
            const juce::ScopedLock lock(statsLock);
            statsData.limitReached = finishedTake.limitReached;
        }
    }

    /** Adds a message to statsData.error (caller holds statsLock). */
    void appendError(const juce::String& message)
    {
        statsData.error = statsData.error.isEmpty() ? message : statsData.error + "; " + message;
    }

    bool openOutput(double rate, int channelCount)
    {
        TakeOptions options;
        options.file = destinationFile;
        options.sampleRate = rate;
        options.channels = channelCount;
        options.maxSeconds = captureOptions.maxSeconds;
        options.trimSilence = captureOptions.trimSilence;
        options.silenceFramesPerCall = static_cast<int>(rate * options.ringSeconds); // as much as the ring has room for
        takeUsed = take_.open(options).wasOk();
        return takeUsed;
    }

    /** Appends interleaved float frames to the take (capture thread only). */
    void push(const float* interleaved, int channels, int frames)
    {
        if (frames <= 0)
            return;
        float framePeak = 0.0f;
        for (int i = 0; i < frames * channels; ++i)
            framePeak = juce::jmax(framePeak, std::abs(interleaved[i]));
        if (framePeak > peak)
            peak = framePeak;
        take_.pushInterleaved(interleaved, channels, frames);
    }

    /**
        Owes the take `missing` frames of silence (the capture thread decided it is a real hole, see CaptureGap). Never
        blocks: the take writes it in bounded steps (a long pause must not stall the thread that empties the 200 ms
        WASAPI buffer), and before the first sound of a trimmed take it is only counted.
    */
    void addSilence(juce::int64 missing)
    {
        if (missing <= 0)
            return;
        take_.addPendingSilence(missing);
        silenceFrames += missing;
        ++gapFills;
        largestGap = juce::jmax<juce::int64>(largestGap, missing);
    }

    //==============================================================================
    juce::File destinationFile;
    CaptureOptions captureOptions;
    juce::Result startResult = juce::Result::ok();
    juce::WaitableEvent started;
    std::atomic<bool> streamStarted{false};

    bool takeUsed = false;            // single-use: set once the take file was opened
    juce::int64 previousEndFrame = 0; // capture thread only: where the previous packet ended on the QPC clock
    TakeWriter take_;
    TakeResult finishedTake;
    std::unique_ptr<CaptureThread> captureThread;

    mutable juce::CriticalSection statsLock;
    CaptureStats statsData;
    std::atomic<float> peak{0.0f};
    std::atomic<juce::int64> silenceFrames{0};
    std::atomic<juce::int64> silenceDropped{0};
    std::atomic<int> gapFills{0};
    std::atomic<juce::int64> largestGap{0};
    std::atomic<int> discontinuities{0};
};

std::unique_ptr<ICaptureSource> createCaptureSource() { return std::make_unique<WasapiLoopbackCapture>(); }

std::vector<PlaybackDevice> listPlaybackDevices()
{
    std::vector<PlaybackDevice> devices;
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED); // S_FALSE / RPC_E_CHANGED_MODE: already initialised
    {
        auto enumerator = createEnumerator();
        if (enumerator != nullptr)
        {
            juce::String defaultId;
            ComPtr<IMMDevice> def;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def)))
                defaultId = endpointId(*def.Get());

            ComPtr<IMMDeviceCollection> collection;
            UINT count = 0;
            if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) && SUCCEEDED(collection->GetCount(&count)))
            {
                for (UINT i = 0; i < count; ++i)
                {
                    ComPtr<IMMDevice> device;
                    if (FAILED(collection->Item(i, &device)))
                        continue;
                    PlaybackDevice d;
                    d.id = endpointId(*device.Get());
                    d.name = friendlyName(*device.Get());
                    d.isDefault = d.id == defaultId;
                    if (d.id.isNotEmpty())
                        devices.push_back(d);
                }
            }
        }
    }
    if (coInit == S_OK || coInit == S_FALSE)
        CoUninitialize();
    return devices;
}
} // namespace sampler::platform
