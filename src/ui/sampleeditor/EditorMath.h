#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

/** Plain maths of the sample editor: musical positions, the waveform viewport and slice hit-testing. No Components. */
namespace sampler::ui::editor
{
//==============================================================================
// Musical positions of a sound: bars of 4 beats counted from the sound's first beat ("1.1.1").
struct Tempo
{
    double bpm = 120.0;
    juce::int64 firstBeatFrame = 0;
    double sampleRate = 44100.0;

    double framesPerBeat() const { return sampleRate * 60.0 / juce::jmax(1.0e-6, bpm); }
    double framesPerSixteenth() const { return framesPerBeat() / 4.0; }
};

constexpr int beatsPerBar = 4;

/** Beats from the first beat to `frame` (negative before the first beat). */
double beatsAt(const Tempo&, juce::int64 frame);

/** "bar.beat.sixteenth", 1-based from the first beat, floored to a sixteenth ("1.1.1" at the first beat, "0.4.4" just before). */
juce::String barBeatText(const Tempo&, juce::int64 frame);

/** The frame of a typed position: "1.3.3", "1.3" (sixteenth 1) or "2" (bar 2, beat 1). Nothing if it is not a position. */
std::optional<juce::int64> parseBarBeat(const Tempo&, const juce::String&);

/** Length of the sound in bars at the sound's tempo. */
double barsOf(const Tempo&, juce::int64 numFrames);

/** "4 bars · 90 BPM · 44.1 kHz · stereo" (bars rounded to one decimal). */
juce::String headerText(const Tempo&, juce::int64 numFrames, int channels);
/** Just the number parts of the header, for tests: "4 bars", "90 BPM", "44.1 kHz". */
juce::String barsText(double bars);
juce::String bpmText(double bpm);
juce::String rateText(double sampleRate);

/** Seconds and samples as shown in the slice panel's START and END. */
juce::String secondsText(double sampleRate, juce::int64 frame);
std::optional<juce::int64> parseSeconds(double sampleRate, const juce::String&);
std::optional<juce::int64> parseSamples(const juce::String&);

//==============================================================================
/** What part of the sound is on screen. */
struct Viewport
{
    juce::int64 totalFrames = 0;
    int widthPixels = 1;
    double startFrame = 0.0;
    double framesPerPixel = 1.0;

    /** Closest zoom: 1/16 frame per pixel (a sample is 16 pixels wide). */
    static constexpr double minFramesPerPixel = 1.0 / 16.0;

    /** At or below one frame per pixel the waveform is drawn as a line through the single samples. */
    bool showsSamples() const { return framesPerPixel <= 1.0; }

    double visibleFrames() const { return framesPerPixel * widthPixels; }
    /** framesPerPixel that shows the whole sound. */
    double fitFramesPerPixel() const;

    double frameToX(double frame) const { return (frame - startFrame) / framesPerPixel; }
    juce::int64 xToFrame(double x) const;

    void fit();
    void setWidth(int pixels);
    /** Zoom by `factor` (> 1 zooms in) keeping the frame under pixel `x` where it is. */
    void zoomAround(double x, double factor);
    void scrollByPixels(double pixels);
    void scrollToFrame(double frame);
    /** Keeps the start inside the sound and the zoom inside its limits. */
    void clamp();
};

//==============================================================================
/** One tick of the bar.beat ruler. */
struct RulerTick
{
    double x = 0.0;
    juce::String label; // empty for an unlabelled beat tick
    bool isBar = false;
};

/** Ticks for the visible part: a label every bar (or every few bars when zoomed out), beat ticks when there is room. */
std::vector<RulerTick> rulerTicks(const Viewport&, const Tempo&);

//==============================================================================
/** Slices of a sound: slice i spans [marker i-1, marker i) with 0 before the first and the sound's end after the last. */
struct SliceGeometry
{
    Viewport view;
    std::vector<juce::int64> markers; // increasing, excluding 0

    int numSlices() const { return static_cast<int>(markers.size()) + 1; }
    juce::int64 sliceStart(int slice) const;
    juce::int64 sliceEnd(int slice) const;
    /** The slice a frame belongs to. */
    int sliceAtFrame(juce::int64 frame) const;
    /** The slice under pixel x (clamped to the first and last slice). */
    int sliceAtX(double x) const { return sliceAtFrame(view.xToFrame(x)); }
    /** Index of the marker within `tolerancePixels` of x (the nearest), or -1. */
    int markerAtX(double x, double tolerancePixels) const;
    /** Left and right pixel of a slice, clipped to [0, width]. Empty (right <= left) if it is off screen. */
    std::pair<double, double> sliceSpan(int slice) const;
};

//==============================================================================
/** Formats a frame position of the strip: "1" .. "256" (slice numbers). */
inline juce::String sliceLabel(int sliceIndex) { return juce::String(sliceIndex + 1); }
} // namespace sampler::ui::editor
