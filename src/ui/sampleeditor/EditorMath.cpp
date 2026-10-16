#include "ui/sampleeditor/EditorMath.h"

#include <algorithm>
#include <cmath>

namespace sampler::ui::editor
{
namespace
{
juce::int64 floorDiv(juce::int64 a, juce::int64 b)
{
    juce::int64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

juce::int64 floorMod(juce::int64 a, juce::int64 b) { return a - floorDiv(a, b) * b; }

/** Number with at most `decimals` places and no trailing zeros ("90", "92.5", "44.1"). */
juce::String trimmed(double value, int decimals)
{
    auto text = juce::String(value, decimals);
    if (text.contains("."))
    {
        text = text.trimCharactersAtEnd("0");
        text = text.trimCharactersAtEnd(".");
    }
    return text;
}
} // namespace

//==============================================================================
double beatsAt(const Tempo& tempo, juce::int64 frame)
{
    return static_cast<double>(frame - tempo.firstBeatFrame) / tempo.framesPerBeat();
}

juce::String barBeatText(const Tempo& tempo, juce::int64 frame)
{
    // Floor to a sixteenth. A sixteenth is rarely a whole number of frames, so the grid point is the nearest frame: half a
    // frame of tolerance keeps a frame typed as a position (parseBarBeat) on that position.
    const auto sixteenths =
        static_cast<juce::int64>(std::floor((static_cast<double>(frame - tempo.firstBeatFrame) + 0.5) / tempo.framesPerSixteenth()));
    const auto bar = floorDiv(sixteenths, beatsPerBar * 4) + 1;
    const auto inBar = floorMod(sixteenths, beatsPerBar * 4);
    return juce::String(bar) + "." + juce::String(inBar / 4 + 1) + "." + juce::String(inBar % 4 + 1);
}

std::optional<juce::int64> parseBarBeat(const Tempo& tempo, const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens(text.trim(), ".", "");
    if (parts.size() < 1 || parts.size() > 3)
        return std::nullopt;
    juce::int64 values[3] = {1, 1, 1};
    for (int i = 0; i < parts.size(); ++i)
    {
        const auto part = parts[i].trim();
        if (part.isEmpty() || ! part.containsOnly("-0123456789") || part.length() > 6)
            return std::nullopt;
        values[i] = part.getLargeIntValue();
    }
    if (values[1] < 1 || values[1] > beatsPerBar || values[2] < 1 || values[2] > 4)
        return std::nullopt;
    const auto sixteenths = (values[0] - 1) * beatsPerBar * 4 + (values[1] - 1) * 4 + (values[2] - 1);
    return tempo.firstBeatFrame + static_cast<juce::int64>(std::llround(static_cast<double>(sixteenths) * tempo.framesPerSixteenth()));
}

double barsOf(const Tempo& tempo, juce::int64 numFrames)
{
    return static_cast<double>(numFrames) / (tempo.framesPerBeat() * beatsPerBar);
}

juce::String barsText(double bars)
{
    const auto rounded = std::round(bars * 10.0) / 10.0;
    return trimmed(rounded, 1) + (std::abs(rounded - 1.0) < 1.0e-9 ? " bar" : " bars");
}

juce::String bpmText(double bpm) { return trimmed(bpm, 2) + " BPM"; }

juce::String rateText(double sampleRate) { return trimmed(sampleRate / 1000.0, 3) + " kHz"; }

juce::String headerText(const Tempo& tempo, juce::int64 numFrames, int channels)
{
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    return barsText(barsOf(tempo, numFrames)) + dot + bpmText(tempo.bpm) + dot + rateText(tempo.sampleRate) + dot
           + (channels == 1 ? "mono" : "stereo");
}

juce::String secondsText(double sampleRate, juce::int64 frame)
{
    return juce::String(static_cast<double>(frame) / juce::jmax(1.0, sampleRate), 4) + " s";
}

std::optional<juce::int64> parseSeconds(double sampleRate, const juce::String& text)
{
    auto t = text.trim();
    if (t.endsWithIgnoreCase("s"))
        t = t.dropLastCharacters(1).trim();
    if (t.isEmpty() || ! t.containsOnly("-0123456789.") || t.length() > 12)
        return std::nullopt;
    return static_cast<juce::int64>(std::llround(t.getDoubleValue() * sampleRate));
}

std::optional<juce::int64> parseSamples(const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty() || ! t.containsOnly("-0123456789") || t.length() > 12)
        return std::nullopt;
    return t.getLargeIntValue();
}

//==============================================================================
double Viewport::fitFramesPerPixel() const
{
    return juce::jmax(minFramesPerPixel, static_cast<double>(totalFrames) / juce::jmax(1, widthPixels));
}

juce::int64 Viewport::xToFrame(double x) const
{
    const auto frame = static_cast<juce::int64>(std::llround(startFrame + x * framesPerPixel));
    return juce::jlimit<juce::int64>(0, juce::jmax<juce::int64>(0, totalFrames), frame);
}

void Viewport::fit()
{
    startFrame = 0.0;
    framesPerPixel = fitFramesPerPixel();
}

void Viewport::setWidth(int pixels)
{
    const bool wasFit = std::abs(framesPerPixel - fitFramesPerPixel()) < 1.0e-9 || widthPixels <= 1;
    widthPixels = juce::jmax(1, pixels);
    if (wasFit)
        fit();
    else
        clamp();
}

void Viewport::clamp()
{
    framesPerPixel = juce::jlimit(minFramesPerPixel, fitFramesPerPixel(), framesPerPixel);
    const auto maxStart = juce::jmax(0.0, static_cast<double>(totalFrames) - visibleFrames());
    startFrame = juce::jlimit(0.0, maxStart, startFrame);
}

void Viewport::zoomAround(double x, double factor)
{
    if (factor <= 0.0)
        return;
    const double anchor = startFrame + x * framesPerPixel;
    framesPerPixel = juce::jlimit(minFramesPerPixel, fitFramesPerPixel(), framesPerPixel / factor);
    startFrame = anchor - x * framesPerPixel;
    clamp();
}

void Viewport::scrollByPixels(double pixels)
{
    startFrame += pixels * framesPerPixel;
    clamp();
}

void Viewport::scrollToFrame(double frame)
{
    startFrame = frame;
    clamp();
}

//==============================================================================
std::vector<RulerTick> rulerTicks(const Viewport& view, const Tempo& tempo)
{
    std::vector<RulerTick> ticks;
    if (view.widthPixels <= 0 || view.framesPerPixel <= 0.0)
        return ticks;

    const double pxPerBeat = tempo.framesPerBeat() / view.framesPerPixel;
    const double pxPerBar = pxPerBeat * beatsPerBar;
    constexpr double labelSpacing = 56.0;
    juce::int64 barStep = 1;
    while (static_cast<double>(barStep) * pxPerBar < labelSpacing && barStep < (1 << 20))
        barStep *= 2;

    const double firstBeat = beatsAt(tempo, static_cast<juce::int64>(std::floor(view.startFrame)));
    const double lastBeat = beatsAt(tempo, static_cast<juce::int64>(std::ceil(view.startFrame + view.visibleFrames())));
    const auto xOfBeat = [&](juce::int64 beat) {
        return view.frameToX(static_cast<double>(tempo.firstBeatFrame) + static_cast<double>(beat) * tempo.framesPerBeat());
    };

    if (pxPerBeat >= 12.0)
    {
        for (auto beat = static_cast<juce::int64>(std::floor(firstBeat)); beat <= static_cast<juce::int64>(std::ceil(lastBeat)); ++beat)
        {
            const auto barNumber = floorDiv(beat, beatsPerBar) + 1;
            const bool isBar = floorMod(beat, beatsPerBar) == 0;
            RulerTick tick;
            tick.x = xOfBeat(beat);
            tick.isBar = isBar;
            if (isBar && floorMod(barNumber - 1, barStep) == 0)
                tick.label = juce::String(barNumber);
            else if (! isBar && pxPerBeat >= 60.0)
                tick.label = juce::String(barNumber) + "." + juce::String(floorMod(beat, beatsPerBar) + 1);
            ticks.push_back(tick);
        }
    }
    else
    {
        // Zoomed far out: bars only, every `barStep` bars.
        const auto firstBar = floorDiv(static_cast<juce::int64>(std::floor(firstBeat)), beatsPerBar);
        const auto lastBar = floorDiv(static_cast<juce::int64>(std::ceil(lastBeat)), beatsPerBar) + 1;
        for (auto bar = floorDiv(firstBar, barStep) * barStep; bar <= lastBar; bar += barStep)
        {
            RulerTick tick;
            tick.x = xOfBeat(bar * beatsPerBar);
            tick.isBar = true;
            tick.label = juce::String(bar + 1);
            ticks.push_back(tick);
        }
    }

    ticks.erase(std::remove_if(ticks.begin(), ticks.end(), [&view](const RulerTick& t) { return t.x < -1.0 || t.x > view.widthPixels + 1.0; }),
                ticks.end());
    return ticks;
}

//==============================================================================
juce::int64 SliceGeometry::sliceStart(int slice) const
{
    if (slice <= 0 || markers.empty())
        return 0;
    return markers[static_cast<size_t>(juce::jmin(slice, static_cast<int>(markers.size())) - 1)];
}

juce::int64 SliceGeometry::sliceEnd(int slice) const
{
    return slice >= static_cast<int>(markers.size()) ? view.totalFrames : markers[static_cast<size_t>(juce::jmax(0, slice))];
}

int SliceGeometry::sliceAtFrame(juce::int64 frame) const
{
    return static_cast<int>(std::upper_bound(markers.begin(), markers.end(), frame) - markers.begin());
}

int SliceGeometry::markerAtX(double x, double tolerancePixels) const
{
    int best = -1;
    double bestDistance = tolerancePixels + 1.0e-9;
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const double distance = std::abs(view.frameToX(static_cast<double>(markers[i])) - x);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::pair<double, double> SliceGeometry::sliceSpan(int slice) const
{
    const double left = juce::jlimit(0.0, static_cast<double>(view.widthPixels), view.frameToX(static_cast<double>(sliceStart(slice))));
    const double right = juce::jlimit(0.0, static_cast<double>(view.widthPixels), view.frameToX(static_cast<double>(sliceEnd(slice))));
    return {left, right};
}
} // namespace sampler::ui::editor
