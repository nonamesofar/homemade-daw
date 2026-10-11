#pragma once

#include <juce_core/juce_core.h>

namespace sampler::io
{
/** What the first frame of an MP3 says about the encoder delay and padding (the Xing/Info + LAME tag). */
struct LameTag
{
    bool found = false;
    int encoderDelay = 0;     // samples of silence the encoder put at the start
    int encoderPadding = 0;   // samples of padding at the end
    juce::int64 audioFrames = 0; // MPEG frames of audio (not counting the tag frame), 0 if the tag does not say
    int samplesPerFrame = 1152;
};

/** Reads the LAME tag from the start of an MP3 file (after an ID3v2 tag, if any). `found` is false without one. */
LameTag readLameTag(const juce::File&);
LameTag parseLameTag(const void* data, size_t size);

/**
    Which part of the decoded audio to keep, in frames of the decoded stream.
    `start`: frames to cut at the start. `length`: frames to keep, or -1 to keep everything after `start`.
*/
struct Mp3Cut
{
    juce::int64 start = 0;
    juce::int64 length = -1;
};

/**
    The cut that removes the encoder delay and padding. With a LAME tag: the delay plus the decoder's own delay at the
    start, and exactly `audioFrames * samplesPerFrame - delay - padding` frames kept. Without one: a fixed cut of
    `tagless` frames at the start.
*/
Mp3Cut mp3CutFor(const LameTag&);

/**
    Decoder delay of JUCE's MP3 reader, in frames, on top of the encoder delay. Measured in tests/io/mp3.test.cpp
    against a LAME-encoded copy of drums-loop-120.wav: the decoded audio lines up with the WAV at 576 + 529 = 1105.
    (The 529 + 1 in the design document is mpg123's value; JUCE's decoder has no extra frame, and it does not output
    the tag frame.) See docs/m0-findings.md.
*/
constexpr int decoderDelayFrames = 529;
/** Frames cut from the start of an MP3 that has no LAME tag (LAME default 576 encoder delay + 529 decoder delay). */
constexpr int taglessStartCut = 1105;
} // namespace sampler::io
