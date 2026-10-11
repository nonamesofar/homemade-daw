#include "ImportService.h"

#include "AtomicFile.h"
#include "ImportNaming.h"
#include "Mp3Gapless.h"
#include "commands/Commands.h"
#include "model/SamplerState.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace sampler::io
{
namespace
{
/** Our own decoders, exactly the formats we promise. Not registerBasicFormats(): on Windows that adds the OS media reader. */
void registerImportFormats(juce::AudioFormatManager& formats)
{
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    formats.registerFormat(new juce::FlacAudioFormat(), false);
    formats.registerFormat(new juce::OggVorbisAudioFormat(), false);
    formats.registerFormat(new juce::MP3AudioFormat(), false);
}

constexpr int decodeBlock = 65536;
constexpr juce::int64 wavDataStart = 44; // the smallest RIFF/fmt/data header; ours may be longer, never shorter

/** The result of preparing one file on the worker. */
struct Outcome
{
    enum class Kind { ready, duplicate, failed };
    Kind kind = Kind::failed;
    juce::String fileName;
    juce::String text;           // failure reason
    juce::String hash;           // ready and duplicate
    model::SourceData data;      // ready
    bool downmixed = false;      // ready
    std::vector<juce::File> created; // ready: files this import wrote (removed again if the batch is cancelled)
};

/** A file stream that stops delivering bytes once `cancel` is set, so hashing a long file can be abandoned. */
class CancellableFileStream : public juce::FileInputStream
{
public:
    CancellableFileStream(const juce::File& file, const std::atomic<bool>& cancelFlag)
        : juce::FileInputStream(file), cancel(cancelFlag) {}

    int read(void* buffer, int maxBytes) override { return cancel.load() ? 0 : juce::FileInputStream::read(buffer, maxBytes); }
    bool isExhausted() override { return cancel.load() || juce::FileInputStream::isExhausted(); }

private:
    const std::atomic<bool>& cancel;
};

/** The hash read in 64 KiB blocks (never the whole file in memory). Empty "sha256:" if unreadable or cancelled. */
juce::String sha256Of(const juce::File& file, const std::atomic<bool>& cancel)
{
    CancellableFileStream in(file, cancel);
    if (in.failedToOpen())
        return "sha256:";
    const juce::SHA256 hash(in);
    return cancel.load() ? "sha256:" : "sha256:" + hash.toHexString();
}

/** Copies `source` to `destination` through a temporary file next to it, then one replacing rename. False if cancelled or short. */
bool copyFileAtomically(const juce::File& source, const juce::File& destination, const std::atomic<bool>& cancel)
{
    destination.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(destination);
    {
        juce::FileInputStream in(source);
        juce::FileOutputStream out(temporary.getFile());
        if (in.failedToOpen() || out.failedToOpen())
            return false;
        out.setPosition(0);
        out.truncate();

        juce::HeapBlock<char> chunk(1 << 20);
        juce::int64 total = 0;
        while (! in.isExhausted())
        {
            if (cancel.load())
                return false;
            const auto got = in.read(chunk, 1 << 20);
            if (got <= 0)
                break;
            if (! out.write(chunk, static_cast<size_t>(got)))
                return false;
            total += got;
        }
        out.flush(); // FlushFileBuffers: the bytes are on disk before the rename
        if (out.getStatus().failed() || total != source.getSize()) // a short read must not pass as a copy
            return false;
    }
    return renameReplacing(temporary.getFile(), destination);
}

/**
    Decodes the whole reader to a 32-bit float WAV at `destination` (via a temporary file and a rename). Mono and stereo
    keep their channels; only [firstFrame, firstFrame + numFrames) is written (an MP3 without its encoder delay and
    padding); more than two channels are mixed down: even-numbered channels to the left, odd-numbered to the
    right, each side averaged. `onProgress` gets 0..1.
*/
bool decodeToFloatWav(juce::AudioFormatReader& reader, const juce::File& destination, int outChannels,
                      juce::int64 firstFrame, juce::int64 numFrames, const std::function<void(double)>& onProgress,
                      const std::atomic<bool>& cancel)
{
    destination.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(destination);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
        if (stream == nullptr)
            return false;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), reader.sampleRate, static_cast<unsigned int>(outChannels), 32, {}, 0));
        if (writer == nullptr)
            return false;
        auto* rawStream = stream.release(); // owned by the writer now

        const int inChannels = static_cast<int>(reader.numChannels);
        juce::AudioBuffer<float> in(inChannels, decodeBlock);
        juce::AudioBuffer<float> out(outChannels, decodeBlock);
        double lastReported = 0.0;
        for (juce::int64 done = 0; done < numFrames; done += decodeBlock)
        {
            if (cancel.load())
                return false;
            const int count = static_cast<int>(juce::jmin<juce::int64>(decodeBlock, numFrames - done));
            if (! reader.read(&in, 0, count, firstFrame + done, true, inChannels > 1))
                return false;

            if (inChannels == outChannels)
            {
                if (! writer->writeFromAudioSampleBuffer(in, 0, count))
                    return false;
            }
            else
            {
                out.clear();
                int leftCount = 0, rightCount = 0;
                for (int c = 0; c < inChannels; ++c)
                    (c % 2 == 0 ? leftCount : rightCount)++;
                for (int c = 0; c < inChannels; ++c)
                {
                    const bool left = c % 2 == 0;
                    out.addFrom(left ? 0 : 1, 0, in, c, 0, count, 1.0f / static_cast<float>(left ? leftCount : rightCount));
                }
                if (! writer->writeFromAudioSampleBuffer(out, 0, count))
                    return false;
            }

            const double fraction = static_cast<double>(done + count) / static_cast<double>(numFrames);
            if (fraction - lastReported >= 0.02 || fraction >= 1.0)
            {
                lastReported = fraction;
                onProgress(fraction);
            }
        }
        if (! writer->flush())
            return false;
        rawStream->flush(); // the bytes are on disk before the rename
        if (rawStream->getStatus().failed())
            return false;
        writer.reset(); // closes
    }
    return renameReplacing(temporary.getFile(), destination);
}

/** Prepares one file; never throws. Files created here are listed in `created` so a failure can remove them. */
Outcome prepareFile(const juce::File& file, const juce::File& bundle, juce::AudioFormatManager& formats,
                    const std::map<juce::String, juce::String>& knownHashes, std::set<juce::String>& seenInBatch,
                    const std::function<void(double)>& onProgress, const std::atomic<bool>& cancel)
{
    Outcome result;
    result.fileName = file.getFileName();
    const auto fail = [&result](const juce::String& reason) {
        result.kind = Outcome::Kind::failed;
        result.text = result.fileName + ": " + reason;
        return result;
    };

    const auto extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    if (! ImportService::supportedExtensions().contains(extension))
        return fail("this file type is not supported");
    if (! file.existsAsFile())
        return fail("the file was not found");

    if (cancel.load())
        return fail("cancelled");
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return fail("this is not a readable audio file");
    if (static_cast<double>(reader->lengthInSamples) / reader->sampleRate > ImportService::maxSeconds)
        return fail("longer than 30 minutes");

    // An MP3 starts with the encoder delay and ends with padding: keep only the audio that was in the original.
    juce::int64 firstFrame = 0, numFrames = reader->lengthInSamples;
    if (extension == "mp3")
    {
        const auto tag = readLameTag(file); // a few KiB at most
        if (cancel.load())
            return fail("cancelled");
        const auto cut = mp3CutFor(tag);
        firstFrame = juce::jmin(cut.start, reader->lengthInSamples);
        numFrames = reader->lengthInSamples - firstFrame;
        if (cut.length >= 0)
            numFrames = juce::jmin(numFrames, cut.length);
        if (numFrames <= 0)
            return fail("this is not a readable audio file");
    }

    if (cancel.load())
        return fail("cancelled");
    const auto hash = sha256Of(file, cancel);
    if (cancel.load())
        return fail("cancelled");
    if (hash == "sha256:")
        return fail("the file could not be read");
    result.hash = hash;
    if (knownHashes.count(hash) > 0 || seenInBatch.count(hash) > 0) // only files that went in count: a failed twin gets its own try
    {
        result.kind = Outcome::Kind::duplicate;
        return result;
    }

    const auto displayName = file.getFileNameWithoutExtension();
    const auto originalRel = originalRelativePath(displayName, hash, extension);
    const auto decodedRel = decodedRelativePath(displayName, hash);
    const auto originalFile = bundle.getChildFile(originalRel);
    const auto decodedFile = bundle.getChildFile(decodedRel);

    // Files left by an earlier import of the same bytes (the import was undone) are reused.
    std::vector<juce::File> created;
    const auto cleanUp = [&created] {
        for (const auto& f : created)
            f.deleteFile();
    };

    if (! originalFile.existsAsFile() || originalFile.getSize() != file.getSize())
    {
        if (! copyFileAtomically(file, originalFile, cancel))
        {
            cleanUp();
            return fail(cancel.load() ? "cancelled" : "it could not be copied into the project");
        }
        created.push_back(originalFile);
    }

    const int outChannels = static_cast<int>(juce::jmin<unsigned>(reader->numChannels, 2u));
    result.downmixed = reader->numChannels > 2;
    if (cancel.load())
    {
        cleanUp();
        return fail("cancelled");
    }
    if (decodedFile.existsAsFile())
    {
        // A copy left by an earlier import is only reused if it is what a fresh decode would give.
        // The WAV writer rounds the rate to an integer, so compare rounded rates; the file holds the header before the samples too.
        std::unique_ptr<juce::AudioFormatReader> existing(formats.createReaderFor(decodedFile));
        if (existing == nullptr || juce::roundToInt(existing->sampleRate) != juce::roundToInt(reader->sampleRate)
            || static_cast<int>(existing->numChannels) != outChannels || existing->lengthInSamples != numFrames
            || decodedFile.getSize() < wavDataStart + numFrames * outChannels * static_cast<juce::int64>(sizeof(float))) // header says more than the file holds
            decodedFile.deleteFile();
    }
    if (! decodedFile.existsAsFile())
    {
        if (! decodeToFloatWav(*reader, decodedFile, outChannels, firstFrame, numFrames, onProgress, cancel))
        {
            decodedFile.deleteFile();
            cleanUp();
            return fail(cancel.load() ? "cancelled" : "it could not be decoded");
        }
        created.push_back(decodedFile);
    }

    // Describe what is on disk now.
    std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(decodedFile));
    if (decoded == nullptr)
    {
        cleanUp();
        return fail("the decoded copy could not be read back");
    }

    auto& d = result.data;
    d.name = displayName;
    d.original = originalRel;
    d.decoded = decodedRel;
    d.originalPath = file.getFullPathName();
    d.hash = hash;
    d.sampleRate = decoded->sampleRate;
    d.channels = static_cast<int>(decoded->numChannels);
    d.frames = decoded->lengthInSamples;
    d.category = d.sampleRate > 0.0 && static_cast<double>(d.frames) / d.sampleRate < 2.0 ? model::Category::oneShot
                                                                                         : model::Category::loop;
    result.kind = Outcome::Kind::ready;
    result.created = created;
    seenInBatch.insert(hash);
    return result;
}
} // namespace

struct ImportService::Prepared
{
    model::SourceData data;
    bool downmixed = false;
    juce::String fileName;
    juce::String hash;
};

juce::StringArray ImportService::supportedExtensions()
{
    juce::AudioFormatManager formats;
    registerImportFormats(formats);
    juce::StringArray extensions;
    for (int i = 0; i < formats.getNumKnownFormats(); ++i)
        for (auto extension : formats.getKnownFormat(i)->getFileExtensions())
            extensions.addIfNotAlreadyThere(extension.trimCharactersAtStart(".").toLowerCase());
    return extensions;
}

ImportService::ImportService(juce::ValueTree samplerState, commands::CommandService& commandService,
                             std::function<juce::File()> bundleDirProvider, JobRegistry& jobs)
    : sampler(std::move(samplerState)),
      commands(commandService),
      bundleDir(std::move(bundleDirProvider)),
      registry(jobs),
      cancelled(std::make_shared<std::atomic<bool>>(false)),
      pool(juce::ThreadPoolOptions{}.withThreadName("Import").withNumberOfThreads(1))
{
}

ImportService::~ImportService()
{
    cancelled->store(true); // the worker stops at the next block and removes what it wrote
    pool.removeAllJobs(true, 10000);
    for (const auto id : activeJobIds)
        registry.remove(id); // the window must not keep showing rows for an import that was cut off
}

void ImportService::importFiles(const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    juce::StringArray names;
    for (const auto& f : files)
        names.add(f.getFileNameWithoutExtension());
    const auto label = files.size() == 1 ? "Importing " + files[0].getFileName() : "Importing " + juce::String(files.size()) + " files";
    const int jobId = registry.add(label, names);
    ++pendingBatches;
    activeJobIds.push_back(jobId);

    // The worker must not read the model: hand it what it needs to recognise duplicates.
    std::map<juce::String, juce::String> knownHashes;
    {
        model::SamplerState state(sampler);
        for (int i = 0; i < state.numSources(); ++i)
        {
            const auto source = state.sourceAt(i);
            knownHashes[source.hash()] = source.id();
        }
    }

    juce::WeakReference<ImportService> self(this);
    const auto bundle = bundleDir();

    pool.addJob([self, files, bundle, jobId, knownHashes, cancel = cancelled]() {
        juce::AudioFormatManager formats;
        registerImportFormats(formats);
        std::set<juce::String> seenInBatch;
        std::vector<Prepared> ready;
        std::vector<ImportMessage> messages;
        std::vector<std::pair<juce::String, juce::String>> duplicates; // file name, hash
        std::vector<juce::File> written;

        const auto removeWritten = [&written] {
            for (const auto& f : written)
                f.deleteFile();
        };

        for (int i = 0; i < files.size(); ++i)
        {
            if (cancel->load())
            {
                removeWritten();
                return;
            }
            const auto& file = files.getReference(i);
            const auto onProgress = [self, jobId, i, count = files.size()](double fraction) {
                juce::MessageManager::callAsync([self, jobId, overall = (i + fraction) / count] {
                    if (self != nullptr)
                        self->registry.setProgress(jobId, overall);
                });
            };
            auto outcome = prepareFile(file, bundle, formats, knownHashes, seenInBatch, onProgress, *cancel);

            switch (outcome.kind)
            {
                case Outcome::Kind::ready:
                {
                    Prepared p;
                    p.data = outcome.data;
                    p.downmixed = outcome.downmixed;
                    p.fileName = outcome.fileName;
                    p.hash = outcome.hash;
                    written.insert(written.end(), outcome.created.begin(), outcome.created.end());
                    ready.push_back(std::move(p));
                    break;
                }
                case Outcome::Kind::duplicate: duplicates.emplace_back(outcome.fileName, outcome.hash); break;
                case Outcome::Kind::failed:
                    messages.push_back({ImportMessage::Kind::error, outcome.text, {}});
                    break;
            }

            juce::MessageManager::callAsync([self, jobId, name = file.getFileNameWithoutExtension(), done = i + 1,
                                             count = files.size()] {
                if (self == nullptr)
                    return;
                self->registry.nameDone(jobId, name);
                self->registry.setProgress(jobId, static_cast<double>(done) / count);
            });
        }

        if (cancel->load())
        {
            removeWritten();
            return;
        }

        // Duplicates are reported after the new sounds are recorded, when their ids can be looked up by hash.
        for (const auto& [fileName, hash] : duplicates)
            messages.push_back({ImportMessage::Kind::duplicate, fileName + " is already in the project", hash});

        juce::MessageManager::callAsync([self, jobId, ready = std::move(ready), messages = std::move(messages),
                                         written = std::move(written)]() mutable {
            if (self != nullptr)
                self->finishBatch(jobId, std::move(ready), std::move(messages));
            else
                for (const auto& f : written) // the importer is gone: nothing will ever record these files
                    f.deleteFile();
        });
    });
}

void ImportService::finishBatch(int jobId, std::vector<Prepared> ready, std::vector<ImportMessage> messages)
{
    --pendingBatches;
    registry.remove(jobId);
    activeJobIds.erase(std::remove(activeJobIds.begin(), activeJobIds.end(), jobId), activeJobIds.end());

    model::SamplerState state(sampler);
    std::vector<model::SourceData> toAdd;
    std::set<juce::String> batchIds;
    std::vector<ImportMessage> notes;
    for (auto& item : ready)
    {
        if (state.findSourceByHash(item.hash).isValid())
        {
            // Another batch recorded the same bytes meanwhile.
            messages.push_back({ImportMessage::Kind::duplicate, item.fileName + " is already in the project", item.hash});
            continue;
        }
        item.data.id = model::makeSourceId(item.hash, [&](const juce::String& candidate) {
            const auto other = state.findSourceById(candidate);
            return (other.isValid() && other.hash() != item.hash) || batchIds.count(candidate) > 0;
        });
        batchIds.insert(item.data.id);
        toAdd.push_back(item.data);
        if (item.downmixed)
            notes.push_back({ImportMessage::Kind::info, item.fileName + ": mixed down to stereo", item.data.id});
    }

    if (! toAdd.empty())
        commands.apply(std::make_unique<commands::AddSources>(std::move(toAdd)));

    if (! onMessage)
        return;
    for (auto& message : messages)
    {
        if (message.kind == ImportMessage::Kind::duplicate)
        {
            // sourceId held the hash until now. The sound may be gone again (undone while the batch ran).
            const auto existing = state.findSourceByHash(message.sourceId);
            if (! existing.isValid())
            {
                message.kind = ImportMessage::Kind::error;
                message.text = message.text.upToLastOccurrenceOf(" is already in the project", false, false)
                               + " was not added: its sound was removed while importing. Import it again.";
                message.sourceId = {};
            }
            else
                message.sourceId = existing.id();
        }
        onMessage(message);
    }
    for (const auto& note : notes)
        onMessage(note);
}
} // namespace sampler::io
