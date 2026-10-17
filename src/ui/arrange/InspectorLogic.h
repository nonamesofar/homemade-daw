#pragma once

#include "engine/Arrangement.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace sampler::ui
{
/**
    Which warp fields of the clip inspector can be used, and how their values are written and read (clip-warp and
    clip-inspector specs, design D7). Plain functions, so that Catch2 tests every state without a Component.
*/
namespace InspectorLogic
{
/** The mode choice, in the order the mock shows it. Texture is shown and never available (M4a). */
enum ModeIndex
{
    modeBeats = 0,
    modeTones = 1,
    modeTexture = 2,
    modeRepitch = 3
};

inline juce::StringArray modeNames() { return {"Beats", "Tones", "Texture", "Re-pitch"}; }

inline int indexOf(engine::WarpMode mode)
{
    switch (mode)
    {
        case engine::WarpMode::beats: return modeBeats;
        case engine::WarpMode::tones: return modeTones;
        case engine::WarpMode::repitch: return modeRepitch;
    }
    return modeBeats;
}

/** The mode a segment stands for; none for Texture (not available) and for an index out of range. */
inline std::optional<engine::WarpMode> modeAt(int index)
{
    switch (index)
    {
        case modeBeats: return engine::WarpMode::beats;
        case modeTones: return engine::WarpMode::tones;
        case modeRepitch: return engine::WarpMode::repitch;
        default: return std::nullopt;
    }
}

inline bool isModeEnabled(int index) { return modeAt(index).has_value(); }

inline bool isTempoInRange(double bpm) { return bpm >= engine::WarpSettings::minBpm && bpm <= engine::WarpSettings::maxBpm; }

constexpr const char* tempoNotKnownHint = "Tempo not known";

struct State
{
    bool warpOn = false;
    /** The WARP toggle can be pressed: it can always be turned off, and on only when a tempo is known. */
    bool warpEnabled = false;
    /** Tooltip / hint under the toggle: "Tempo not known" while WARP is off and cannot be turned on, else empty. */
    juce::String warpHint;
    bool modeEnabled = false;
    bool bpmEnabled = false;
    bool halveEnabled = false;
    bool doubleEnabled = false;
    bool transposeEnabled = false;
    /** The tempo WARP gets when it is turned on: the clip's own, else the sound's; 0 when neither is known. */
    double tempoForWarpOn = 0.0;
};

/**
    The state of the warp fields for a clip with `warp`, whose sound has the tempo `soundBpm` (the user's correction if
    there is one, else the detected one; 0 when not known). MODE, BPM and the halve / double buttons need WARP on;
    TRANSPOSE needs WARP on and Beats or Tones (Re-pitch plays without it, its value stays shown).
*/
inline State stateFor(const engine::WarpSettings& warp, double soundBpm)
{
    State s;
    s.warpOn = warp.on;
    s.tempoForWarpOn = isTempoInRange(warp.sourceBpm) ? warp.sourceBpm : (isTempoInRange(soundBpm) ? soundBpm : 0.0);
    const bool canTurnOn = s.tempoForWarpOn > 0.0;
    s.warpEnabled = warp.on || canTurnOn;
    if (! warp.on && ! canTurnOn)
        s.warpHint = tempoNotKnownHint;
    s.modeEnabled = warp.on;
    s.bpmEnabled = warp.on;
    s.halveEnabled = warp.on && isTempoInRange(warp.sourceBpm / 2.0);
    s.doubleEnabled = warp.on && isTempoInRange(warp.sourceBpm * 2.0);
    s.transposeEnabled = warp.on && warp.mode != engine::WarpMode::repitch;
    return s;
}

/** "90.00": the clip's tempo with two decimals. */
inline juce::String bpmText(double bpm) { return juce::String(bpm, 2); }

/** A typed tempo, clamped to 20..999; none for text that is not a number. */
inline std::optional<double> parseBpm(const juce::String& text)
{
    const auto t = text.trim().upToFirstOccurrenceOf("bpm", false, true).trim();
    if (t.isEmpty() || ! t.containsOnly("0123456789.,"))
        return std::nullopt;
    const auto value = t.replaceCharacter(',', '.').getDoubleValue();
    if (! (value > 0.0))
        return std::nullopt;
    return juce::jlimit(engine::WarpSettings::minBpm, engine::WarpSettings::maxBpm, value);
}

/** The tempo after pressing ½ or ×2, or none when the result would leave 20..999 (the button is disabled then). */
inline std::optional<double> halved(double bpm) { return isTempoInRange(bpm / 2.0) ? std::optional<double>(bpm / 2.0) : std::nullopt; }
inline std::optional<double> doubled(double bpm) { return isTempoInRange(bpm * 2.0) ? std::optional<double>(bpm * 2.0) : std::nullopt; }

/** "+7 st", "-12 st", "0 st"; a fraction (a slice pitch) keeps one decimal. */
inline juce::String transposeText(double semitones)
{
    const auto rounded = std::round(semitones);
    const auto text = std::abs(semitones - rounded) < 0.005 ? juce::String(static_cast<int>(rounded)) : juce::String(semitones, 1);
    return (semitones > 0.005 ? "+" : "") + text + " st";
}

/** A typed transpose ("7", "+7", "-3 st"), clamped to -24..+24; none for text that is not a number. */
inline std::optional<double> parseTranspose(const juce::String& text)
{
    const auto t = text.trim().upToFirstOccurrenceOf("st", false, true).trim();
    if (t.isEmpty() || ! t.containsOnly("0123456789.-+"))
        return std::nullopt;
    if (! t.containsAnyOf("0123456789"))
        return std::nullopt;
    return juce::jlimit(-engine::WarpSettings::maxTranspose, engine::WarpSettings::maxTranspose, t.getDoubleValue());
}

/**
    The clip's length as bars and beats: "4 bars", "1 bar", "1 bar 2 beats", "3 beats"; a part of a beat keeps up to two
    decimals ("5 bars 1.33 beats"). A bar has `beatsPerBar` beats.
*/
inline juce::String lengthText(double beats, int beatsPerBar)
{
    const auto perBar = static_cast<double>(std::max(1, beatsPerBar));
    auto bars = static_cast<int>(std::floor(beats / perBar + 1.0e-9));
    auto rest = std::max(0.0, beats - bars * perBar);
    if (rest > perBar - 0.005) // rounds up to a whole bar
    {
        ++bars;
        rest = 0.0;
    }
    const auto wholeBeats = std::abs(rest - std::round(rest)) < 0.005;
    juce::String text;
    if (bars > 0)
        text << bars << (bars == 1 ? " bar" : " bars");
    if (rest >= 0.005 || bars == 0)
    {
        if (text.isNotEmpty())
            text << " ";
        const auto shown = wholeBeats ? juce::String(static_cast<int>(std::round(rest))) : juce::String(rest, 2);
        text << shown << (wholeBeats && std::round(rest) == 1.0 ? " beat" : " beats");
    }
    return text;
}

/**
    A typed length in beats. "8" and "8 bars" are bars, "1.2" and "1 bar 2 beats" are one bar and two beats, "3 beats" and
    "3b" are beats. None for text that is not a length, or for zero.
*/
inline std::optional<double> parseLength(const juce::String& text, int beatsPerBar)
{
    const auto perBar = static_cast<double>(std::max(1, beatsPerBar));
    const auto lower = text.trim().toLowerCase();
    if (lower.isEmpty())
        return std::nullopt;

    // Numbers, each optionally followed by a unit word ("bar", "bars", "beat", "beats", "b").
    juce::StringArray numbers;
    juce::StringArray units;
    const auto isNumberChar = [](juce::juce_wchar ch) { return juce::CharacterFunctions::isDigit(ch) || ch == '.' || ch == ','; };
    for (int i = 0; i < lower.length();)
    {
        const auto ch = lower[i];
        if (juce::CharacterFunctions::isWhitespace(ch))
        {
            ++i;
            continue;
        }
        int j = i;
        if (isNumberChar(ch))
        {
            while (j < lower.length() && isNumberChar(lower[j]))
                ++j;
            numbers.add(lower.substring(i, j).replaceCharacter(',', '.'));
            units.add({});
        }
        else
        {
            while (j < lower.length() && juce::CharacterFunctions::isLetter(lower[j]))
                ++j;
            if (j == i || units.isEmpty() || units[units.size() - 1].isNotEmpty())
                return std::nullopt;
            units.set(units.size() - 1, lower.substring(i, j));
        }
        i = j;
    }
    if (numbers.isEmpty() || numbers.size() > 2)
        return std::nullopt;

    const auto isBars = [](const juce::String& u) { return u == "bar" || u == "bars"; };
    const auto isBeats = [](const juce::String& u) { return u == "beat" || u == "beats" || u == "b"; };
    double beats = 0.0;
    if (numbers.size() == 1)
    {
        const auto& n = numbers[0];
        const auto dot = n.indexOfChar('.');
        if (isBeats(units[0]))
            beats = n.getDoubleValue();
        else if (units[0].isNotEmpty() && ! isBars(units[0]))
            return std::nullopt;
        else if (units[0].isEmpty() && dot > 0 && dot < n.length() - 1 && n.indexOfChar(dot + 1, '.') < 0)
            beats = n.substring(0, dot).getIntValue() * perBar + n.substring(dot + 1).getIntValue(); // "bars.beats"
        else
            beats = n.getDoubleValue() * perBar;
    }
    else
    {
        if (! (isBars(units[0]) && isBeats(units[1])))
            return std::nullopt;
        beats = numbers[0].getDoubleValue() * perBar + numbers[1].getDoubleValue();
    }
    if (! (beats > 0.0) || ! std::isfinite(beats))
        return std::nullopt;
    return beats;
}

/** "500 ms": a fade in milliseconds. */
inline juce::String msText(double milliseconds) { return juce::String(static_cast<int>(std::round(milliseconds))) + " ms"; }

/** A typed fade in milliseconds ("500", "500 ms", "0.5 s"); none for text that is not a number. */
inline std::optional<double> parseMs(const juce::String& text)
{
    auto t = text.trim().toLowerCase();
    double scale = 1.0;
    if (t.endsWith("ms"))
        t = t.dropLastCharacters(2).trim();
    else if (t.endsWith("s"))
    {
        t = t.dropLastCharacters(1).trim();
        scale = 1000.0;
    }
    if (t.isEmpty() || ! t.containsOnly("0123456789.,"))
        return std::nullopt;
    return t.replaceCharacter(',', '.').getDoubleValue() * scale;
}

/**
    The fades to send when the user sets one of them (milliseconds): the one being edited is cut so that both fit in the
    clip (a 1000 ms clip with a fade out of 600 and a typed fade in of 800 gives 400), the other stays.
*/
struct FadeValues
{
    double inMs = 0.0;
    double outMs = 0.0;
};
inline FadeValues withFadeIn(double typedMs, double currentOutMs, double clipMs)
{
    const auto out = std::clamp(currentOutMs, 0.0, std::max(0.0, clipMs));
    return {std::clamp(typedMs, 0.0, std::max(0.0, clipMs - out)), out};
}
inline FadeValues withFadeOut(double typedMs, double currentInMs, double clipMs)
{
    const auto in = std::clamp(currentInMs, 0.0, std::max(0.0, clipMs));
    return {in, std::clamp(typedMs, 0.0, std::max(0.0, clipMs - in))};
}
} // namespace InspectorLogic
} // namespace sampler::ui
