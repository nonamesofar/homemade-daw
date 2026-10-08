// WASAPI loopback capture, outside JUCE's device layer.
//
//   capture thread (MMCSS "Capture", event driven) -> AbstractFifo ring (10 s) -> writer thread -> WAV
//
// Loopback sends no packets while nothing plays, so gaps are measured from each packet's QPC timestamp
// and filled with silence, keeping the file as long as the elapsed time.
#include "../ICaptureSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <atomic>
#include <cmath>
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

HRESULT activateEndpointLoopback(ComPtr<IAudioClient>& client)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
        return hr;

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr))
        return hr;

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

/** KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled out so no extra library is needed. */
const GUID kSubtypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

juce::int64 qpcNow100ns()
{
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return static_cast<juce::int64>(static_cast<double>(now.QuadPart) * 1.0e7 / static_cast<double>(freq.QuadPart));
}

constexpr int kFifoSeconds = 10;
} // namespace

class WasapiLoopbackCapture final : public ICaptureSource
{
public:
    ~WasapiLoopbackCapture() override { stop(); }

    juce::Result start(const juce::File& destination, const CaptureOptions& options) override
    {
        if (isRecording())
            return juce::Result::fail("already recording");

        destinationFile = destination;
        captureOptions = options;
        startResult = juce::Result::ok();
        started.reset();
        statsData = {};
        peak = 0.0f;
        silenceFrames = 0;
        framesWritten = 0;
        overruns = 0;
        gapFills = 0;
        largestGap = 0;
        captureFinished = false;

        captureThread = std::make_unique<CaptureThread>(*this);
        captureThread->startThread(juce::Thread::Priority::highest);

        // Wait until the capture thread has either started the stream or failed.
        if (!started.wait(10000))
        {
            stop();
            return juce::Result::fail("timed out starting capture");
        }
        if (startResult.failed())
        {
            captureThread->stopThread(2000);
            captureThread.reset();
            return startResult;
        }
        return juce::Result::ok();
    }

    juce::Result stop() override
    {
        if (captureThread == nullptr)
            return juce::Result::ok();

        captureThread->signalThreadShouldExit();
        SetEvent(captureThread->wakeEvent);
        captureThread->stopThread(5000);
        captureThread.reset();

        captureFinished = true;
        if (writerThread != nullptr)
        {
            writerThread->stopThread(10000);
            writerThread.reset();
        }
        return juce::Result::ok();
    }

    bool isRecording() const override { return captureThread != nullptr && captureThread->isThreadRunning(); }

    CaptureStats stats() const override
    {
        const juce::ScopedLock lock(statsLock);
        auto s = statsData;
        s.framesWritten = framesWritten;
        s.silenceFramesInserted = silenceFrames;
        s.overruns = overruns;
        s.gapFills = gapFills;
        s.largestGapFrames = largestGap;
        s.peak = peak;
        return s;
    }

private:
    //==============================================================================
    class WriterThread final : public juce::Thread
    {
    public:
        WriterThread(WasapiLoopbackCapture& o, std::unique_ptr<juce::AudioFormatWriter> w)
            : juce::Thread("Capture writer"), owner(o), writer(std::move(w))
        {
        }

        void run() override
        {
            juce::AudioBuffer<float> block(owner.channels, 4096);
            for (;;)
            {
                const auto ready = owner.fifo->getNumReady();
                if (ready == 0)
                {
                    if (owner.captureFinished)
                        break;
                    wait(10);
                    continue;
                }

                const auto n = juce::jmin(ready, 4096);
                int start1, size1, start2, size2;
                owner.fifo->prepareToRead(n, start1, size1, start2, size2);
                for (int c = 0; c < owner.channels; ++c)
                {
                    block.copyFrom(c, 0, owner.ring, c, start1, size1);
                    if (size2 > 0)
                        block.copyFrom(c, size1, owner.ring, c, start2, size2);
                }
                owner.fifo->finishedRead(size1 + size2);
                writer->writeFromAudioSampleBuffer(block, 0, size1 + size2);
            }
            writer.reset(); // flushes and finalises the WAV header
        }

    private:
        WasapiLoopbackCapture& owner;
        std::unique_ptr<juce::AudioFormatWriter> writer;
    };

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
                owner.startResult = result;
                owner.started.signal(); // no-op if already signalled
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
            bool processLoopback = false;
            double rate = 0.0;
            int channels = 2;

            if (owner.captureOptions.excludeOwnProcess)
            {
                const HRESULT hr = owner.captureOptions.simulateProcessLoopbackFailure ? E_NOTIMPL : activateProcessLoopback(client);
                if (SUCCEEDED(hr))
                {
                    processLoopback = true;
                    rate = defaultEndpointRate();
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
                const HRESULT hr = activateEndpointLoopback(client);
                if (FAILED(hr))
                    return juce::Result::fail(hresultText("cannot open the default output device", hr));
                if (FAILED(client->GetMixFormat(&mix)) || mix == nullptr)
                    return juce::Result::fail("cannot read the output device format");
                format = mix;
                rate = static_cast<double>(mix->nSamplesPerSec);
                channels = static_cast<int>(mix->nChannels);
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
                channels = 2;
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

            ComPtr<IAudioCaptureClient> captureClient;
            if (FAILED(client->SetEventHandle(bufferEvent)) || FAILED(client->GetService(IID_PPV_ARGS(&captureClient))))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail("cannot get the capture service");
            }

            if (!owner.openOutput(rate, channels))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail("cannot create " + owner.destinationFile.getFullPathName());
            }

            {
                const juce::ScopedLock lock(owner.statsLock);
                owner.statsData.mode = processLoopback ? "process-loopback" : "endpoint-loopback";
                owner.statsData.processLoopbackFailure = processFailure;
                owner.statsData.sampleRate = rate;
                owner.statsData.channels = channels;
            }

            const auto qpcStart = qpcNow100ns();
            hr = client->Start();
            if (FAILED(hr))
            {
                CloseHandle(bufferEvent);
                return juce::Result::fail(hresultText("Start failed", hr));
            }
            owner.startResult = juce::Result::ok();
            owner.started.signal();

            HANDLE handles[2] = {bufferEvent, wakeEvent};
            std::vector<float> floats;

            while (!threadShouldExit())
            {
                // Silent periods raise no event, hence the timeout.
                WaitForMultipleObjects(2, handles, FALSE, 50);
                drain(*captureClient.Get(), qpcStart, rate, channels, isFloat, bitsPerSample, floats);
            }

            client->Stop();
            drain(*captureClient.Get(), qpcStart, rate, channels, isFloat, bitsPerSample, floats);

            // Silence at the end (nothing played until stop) still counts as recorded time.
            const auto elapsedFrames = static_cast<juce::int64>(static_cast<double>(qpcNow100ns() - qpcStart) * rate / 1.0e7);
            owner.fillSilenceUpTo(elapsedFrames);

            CloseHandle(bufferEvent);
            return juce::Result::ok();
        }

        void drain(IAudioCaptureClient& captureClient, juce::int64 qpcStart, double rate, int channels, bool isFloat,
                   int bitsPerSample, std::vector<float>& floats)
        {
            UINT32 packetFrames = 0;
            while (SUCCEEDED(captureClient.GetNextPacketSize(&packetFrames)) && packetFrames > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                UINT64 devicePosition = 0, qpcPosition = 0;
                if (FAILED(captureClient.GetBuffer(&data, &frames, &bufferFlags, &devicePosition, &qpcPosition)))
                    return;

                // Fill the gap before this packet (nothing was playing) with silence.
                const auto packetFrame = static_cast<juce::int64>(static_cast<double>(static_cast<juce::int64>(qpcPosition) - qpcStart) * rate / 1.0e7);
                owner.fillSilenceUpTo(packetFrame);

                floats.assign(static_cast<size_t>(frames) * static_cast<size_t>(channels), 0.0f);
                if (!(bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) && data != nullptr)
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
                (void) bitsPerSample;

                owner.push(floats.data(), static_cast<int>(frames));
                captureClient.ReleaseBuffer(frames);
            }
        }

        WasapiLoopbackCapture& owner;
    };

    //==============================================================================
    bool openOutput(double rate, int channelCount)
    {
        channels = channelCount;
        fifoSize = static_cast<int>(rate) * kFifoSeconds;
        fifo = std::make_unique<juce::AbstractFifo>(fifoSize);
        ring.setSize(channels, fifoSize);
        ring.clear();

        destinationFile.deleteFile();
        destinationFile.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(destinationFile.createOutputStream());
        if (stream == nullptr)
            return false;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), rate, static_cast<unsigned int>(channels), 32, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();

        writerThread = std::make_unique<WriterThread>(*this, std::move(writer));
        writerThread->startThread();
        return true;
    }

    /** Appends interleaved float frames to the ring (capture thread only). */
    void push(const float* interleaved, int frames)
    {
        if (frames <= 0)
            return;

        float framePeak = 0.0f;
        for (int i = 0; i < frames * channels; ++i)
            framePeak = juce::jmax(framePeak, std::abs(interleaved[i]));
        if (framePeak > peak)
            peak = framePeak;

        if (fifo->getFreeSpace() < frames)
        {
            ++overruns; // the writer cannot keep up: drop rather than block the capture thread
            return;
        }

        int start1, size1, start2, size2;
        fifo->prepareToWrite(frames, start1, size1, start2, size2);
        for (int c = 0; c < channels; ++c)
        {
            auto* d1 = ring.getWritePointer(c, start1);
            for (int i = 0; i < size1; ++i)
                d1[i] = interleaved[i * channels + c];
            if (size2 > 0)
            {
                auto* d2 = ring.getWritePointer(c, start2);
                for (int i = 0; i < size2; ++i)
                    d2[i] = interleaved[(size1 + i) * channels + c];
            }
        }
        fifo->finishedWrite(size1 + size2);
        framesWritten += frames;
    }

    /** If fewer than `targetFrame` frames have been recorded, pads with silence (ignores gaps under 10 ms: timestamp jitter). */
    void fillSilenceUpTo(juce::int64 targetFrame)
    {
        const auto tolerance = static_cast<juce::int64>(statsData.sampleRate / 100.0);
        auto missing = targetFrame - framesWritten;
        if (missing <= tolerance)
            return;
        const juce::int64 before = framesWritten;

        std::vector<float> zeros(static_cast<size_t>(juce::jmin<juce::int64>(missing, 8192)) * static_cast<size_t>(channels), 0.0f);
        while (missing > 0)
        {
            const auto n = static_cast<int>(juce::jmin<juce::int64>(missing, 8192));
            if (fifo->getFreeSpace() < n)
            {
                juce::Thread::sleep(2); // let the writer catch up; the silence must not be dropped
                continue;
            }
            push(zeros.data(), n);
            silenceFrames += n;
            missing -= n;
        }
        ++gapFills;
        largestGap = juce::jmax<juce::int64>(largestGap, targetFrame - before);
    }

    //==============================================================================
    juce::File destinationFile;
    CaptureOptions captureOptions;
    juce::Result startResult = juce::Result::ok();
    juce::WaitableEvent started;

    int channels = 2;
    int fifoSize = 0;
    std::unique_ptr<juce::AbstractFifo> fifo;
    juce::AudioBuffer<float> ring;

    std::unique_ptr<CaptureThread> captureThread;
    std::unique_ptr<WriterThread> writerThread;
    std::atomic<bool> captureFinished{false};

    mutable juce::CriticalSection statsLock;
    CaptureStats statsData;
    std::atomic<float> peak{0.0f};
    std::atomic<juce::int64> framesWritten{0};
    std::atomic<juce::int64> silenceFrames{0};
    std::atomic<int> overruns{0};
    std::atomic<int> gapFills{0};
    std::atomic<juce::int64> largestGap{0};
};

std::unique_ptr<ICaptureSource> createCaptureSource() { return std::make_unique<WasapiLoopbackCapture>(); }
} // namespace sampler::platform
