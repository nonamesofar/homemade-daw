#pragma once

#include "render/SliceBank.h"

#include <memory>

namespace sampler
{
class EngineHost;

/**
    The pad player (design D4): a preview edit of its own with one track and a SlicePadPlugin, kept from its first use until
    the project closes and playing to the same device as the project edit (Tracktion mixes them). Pads never touch the project
    edit, its undo history or its dirty state. Tracktion-free interface; message thread only.
    Must be destroyed before the EngineHost.
*/
class PadEngine
{
public:
    explicit PadEngine(EngineHost&);
    ~PadEngine();

    /** `slot` is the slice index in the current bank (0..255). */
    void padOn(int slot);
    void padOff(int slot);
    /** Lets go of every pad (Gate and Loop slices fade out; One-shot slices play on). */
    void allOff();

    /** Fades out every voice, One-shots included (the editor closed, the project changed). */
    void stopAll();

    /** Replaces what the pads can play. Sounding slices keep their old buffers. */
    void setBank(render::SliceBank::Ptr);
    void clearBank();

    /** Voices sounding slice `slot` (for the pad lights; safe to poll at 60 Hz). */
    int soundingCount(int slot) const;
    bool isAnySounding() const;

    /** Blocks the pad plugin has processed since it was created (diagnostics and tests). */
    long long blocksProcessed() const;
    /** True if the preview transport had to be kept playing for the plugin to run while nothing else plays (spike 1.1). */
    bool usesKeepPlayingFallback() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
