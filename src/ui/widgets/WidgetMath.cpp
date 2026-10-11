#include "WidgetMath.h"

#include <cmath>

namespace sampler::ui
{
double ValueRange::clamp(double value) const { return juce::jlimit(min, max, value); }

double ValueRange::snap(double value) const
{
    if (step > 0.0)
        value = min + std::round((value - min) / step) * step;
    return clamp(value);
}

double ValueRange::toNormalised(double value) const
{
    value = clamp(value);
    if (max <= min)
        return 0.0;
    if (logarithmic && min > 0.0)
        return std::log(value / min) / std::log(max / min);
    return (value - min) / (max - min);
}

double ValueRange::fromNormalised(double fraction) const
{
    fraction = juce::jlimit(0.0, 1.0, fraction);
    const auto value = logarithmic && min > 0.0 ? min * std::pow(max / min, fraction) : min + fraction * (max - min);
    return snap(value);
}

double normalisedAfterDrag(double startNormalised, double pixelsMovedUp, double pixelsForFullRange, bool fine)
{
    if (pixelsForFullRange <= 0.0)
        return juce::jlimit(0.0, 1.0, startNormalised);
    const auto scale = fine ? 0.1 : 1.0;
    return juce::jlimit(0.0, 1.0, startNormalised + scale * pixelsMovedUp / pixelsForFullRange);
}

double dbToFraction(double db, double floorDb)
{
    if (db <= floorDb)
        return 0.0;
    return juce::jmin(1.0, (db - floorDb) / (0.0 - floorDb));
}

double gainToDb(double gain) { return gain <= 0.000001 ? -120.0 : 20.0 * std::log10(gain); }

double PeakHold::update(double levelDb, double elapsedMs)
{
    if (levelDb >= peak)
    {
        peak = levelDb;
        sinceHigh = 0.0;
        return peak;
    }
    sinceHigh += elapsedMs;
    if (sinceHigh > hold)
    {
        const auto fallMs = juce::jmin(elapsedMs, sinceHigh - hold);
        peak = juce::jmax(levelDb, peak - fall * fallMs / 1000.0);
    }
    return peak;
}

int segmentAt(double x, double width, int count)
{
    if (count <= 0 || width <= 0.0 || x < 0.0 || x >= width)
        return -1;
    return juce::jmin(count - 1, static_cast<int>(x / (width / count)));
}
} // namespace sampler::ui
