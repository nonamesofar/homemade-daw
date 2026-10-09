#include "MediaFoundationProbe.h"

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <cmath>

namespace sampler::platform
{
using Microsoft::WRL::ComPtr;

namespace
{
juce::String hr(const char* what, HRESULT code)
{
    return juce::String(what) + " failed (0x" + juce::String::toHexString(static_cast<int>(code)) + ")";
}
} // namespace

MfDecodeResult decodeWithMediaFoundation(const juce::File& file)
{
    MfDecodeResult result;

    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        result.error = "MFStartup failed";
        return result;
    }

    {
        ComPtr<IMFSourceReader> reader;
        auto hrc = MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(), nullptr, &reader);
        if (FAILED(hrc))
        {
            result.error = hr("MFCreateSourceReaderFromURL", hrc);
        }
        else
        {
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

            PROPVARIANT dur;
            PropVariantInit(&dur);
            if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                           MF_PD_DURATION, &dur)))
            {
                ULONGLONG hundredNs = 0;
                if (SUCCEEDED(PropVariantToUInt64(dur, &hundredNs)))
                    result.declaredDurationSeconds = static_cast<double>(hundredNs) / 1.0e7;
            }
            PropVariantClear(&dur);

            // Ask for 32-bit float PCM; the source rate and channel count are kept by the decoder.
            ComPtr<IMFMediaType> native;
            reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &native);
            UINT32 rate = 44100, channels = 2;
            if (native != nullptr)
            {
                native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            }

            ComPtr<IMFMediaType> wanted;
            MFCreateMediaType(&wanted);
            wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
            wanted->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
            wanted->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
            wanted->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4);
            wanted->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, channels * 4 * rate);

            hrc = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, wanted.Get());
            if (FAILED(hrc))
            {
                result.error = hr("SetCurrentMediaType", hrc);
            }
            else
            {
                result.sampleRate = rate;
                result.channels = static_cast<int>(channels);

                for (;;)
                {
                    DWORD flags = 0;
                    ComPtr<IMFSample> sample;
                    hrc = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags,
                                             nullptr, &sample);
                    if (FAILED(hrc))
                    {
                        result.error = hr("ReadSample", hrc);
                        break;
                    }
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                    {
                        result.ok = true;
                        break;
                    }
                    if (sample == nullptr)
                        continue;

                    ComPtr<IMFMediaBuffer> buffer;
                    sample->ConvertToContiguousBuffer(&buffer);
                    BYTE* data = nullptr;
                    DWORD bytes = 0;
                    if (buffer != nullptr && SUCCEEDED(buffer->Lock(&data, nullptr, &bytes)))
                    {
                        const auto* f = reinterpret_cast<const float*>(data);
                        const auto frames = static_cast<juce::int64>(bytes / (channels * 4));
                        if (result.firstAudibleFrame < 0)
                            for (juce::int64 i = 0; i < frames * static_cast<juce::int64>(channels); ++i)
                                if (std::abs(f[i]) > 1.0e-4f)
                                {
                                    result.firstAudibleFrame = result.frames + i / static_cast<juce::int64>(channels);
                                    break;
                                }
                        result.frames += frames;
                        buffer->Unlock();
                    }
                }
            }
        }
    }

    MFShutdown();
    if (SUCCEEDED(coInit))
        CoUninitialize();
    return result;
}
} // namespace sampler::platform
