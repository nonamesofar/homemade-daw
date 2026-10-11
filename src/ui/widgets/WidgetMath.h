#pragma once

#include <juce_core/juce_core.h>

/** The logic of the widgets on plain values, so it is unit tested without any Component. */
namespace sampler::ui
{
/** A range of values with an optional step and a straight or logarithmic feel (for frequencies). */
struct ValueRange
{
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    double step = 0.0;        // 0 = continuous
    bool logarithmic = false; // needs min > 0

    double clamp(double value) const;
    /** Snaps to the step (counted from `min`) and clamps. */
    double snap(double value) const;
    double toNormalised(double value) const;      // 0..1
    double fromNormalised(double fraction) const; // snapped
};

/** Vertical drags: moving up raises the value. `pixelsForFullRange` is the drag that sweeps 0..1; `fine` (Shift) divides it by ten. */
double normalisedAfterDrag(double startNormalised, double pixelsMovedUp, double pixelsForFullRange, bool fine);

/** Meter maths. */
double dbToFraction(double db, double floorDb = -60.0);
double gainToDb(double gain);

/** Holds the highest recent level and lets it fall at a fixed speed after a hold time. */
class PeakHold
{
public:
    explicit PeakHold(double holdMs = 800.0, double fallDbPerSecond = 20.0) : hold(holdMs), fall(fallDbPerSecond) {}
    /** `levelDb` is the current level; `elapsedMs` since the last call. Returns the held peak. */
    double update(double levelDb, double elapsedMs);
    double peakDb() const { return peak; }
    void reset()
    {
        peak = -120.0;
        sinceHigh = 0.0;
    }

private:
    double hold, fall;
    double peak = -120.0;
    double sinceHigh = 0.0;
};

/** Which of `count` equal segments contains x (negative or past the end gives -1). */
int segmentAt(double x, double width, int count);
} // namespace sampler::ui
