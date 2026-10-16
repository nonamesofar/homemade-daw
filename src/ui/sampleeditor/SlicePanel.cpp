#include "ui/sampleeditor/SlicePanel.h"

#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

#include <cmath>

namespace sampler::ui::editor
{
juce::String positionText(Units units, const Tempo& tempo, juce::int64 frame)
{
    switch (units)
    {
        case Units::bars: return barBeatText(tempo, frame);
        case Units::seconds: return secondsText(tempo.sampleRate, frame);
        case Units::samples: return juce::String(frame);
    }
    return {};
}

juce::String lengthText(Units units, const Tempo& tempo, juce::int64 frames)
{
    switch (units)
    {
        case Units::bars:
        {
            const auto sixteenths = static_cast<juce::int64>(std::llround(static_cast<double>(frames) / tempo.framesPerSixteenth()));
            return juce::String(sixteenths / 16) + "." + juce::String((sixteenths % 16) / 4) + "." + juce::String(sixteenths % 4);
        }
        case Units::seconds: return secondsText(tempo.sampleRate, frames);
        case Units::samples: return juce::String(frames);
    }
    return {};
}

std::optional<juce::int64> parsePosition(Units units, const Tempo& tempo, const juce::String& text)
{
    switch (units)
    {
        case Units::bars: return parseBarBeat(tempo, text);
        case Units::seconds: return parseSeconds(tempo.sampleRate, text);
        case Units::samples: return parseSamples(text);
    }
    return std::nullopt;
}

//==============================================================================
SlicePanel::SlicePanel()
{
    for (auto* child : {static_cast<juce::Component*>(&startField), static_cast<juce::Component*>(&endField),
                        static_cast<juce::Component*>(&unitsControl), static_cast<juce::Component*>(&pitchField),
                        static_cast<juce::Component*>(&gainField), static_cast<juce::Component*>(&reverseButton),
                        static_cast<juce::Component*>(&playModeControl)})
        addAndMakeVisible(child);

    pitchField.setRange({model::minSlicePitch, model::maxSlicePitch, 0.0, 1.0});
    pitchField.setSuffix(" st", 0);
    pitchField.doubleClickResets = true;
    pitchField.clickToType = false;
    pitchField.justification = juce::Justification::centredLeft;
    pitchField.formatter = [](double v) { return (v > 0.0 ? "+" : "") + juce::String(juce::roundToInt(v)) + " st"; };
    pitchField.setTooltip("Pitch of the selected slice in semitones; its length stays the same");
    pitchField.onChange = [this](double v) {
        if (! updating && onParamChanged)
            onParamChanged(SliceParam::pitch, v);
    };

    gainField.setRange({silenceFieldDb, model::maxSliceGainDb, 0.0, 0.1});
    gainField.justification = juce::Justification::centredLeft;
    gainField.doubleClickResets = true;
    gainField.formatter = [](double v) {
        return v <= silenceFieldDb + 0.05 ? juce::String("-inf dB") : juce::String(v, 1) + " dB";
    };
    gainField.parser = [](const juce::String& text) -> std::optional<double> {
        const auto t = text.trim().upToFirstOccurrenceOf("dB", false, true).trim();
        if (t.equalsIgnoreCase("-inf"))
            return silenceFieldDb;
        if (t.isEmpty() || ! t.containsOnly("0123456789.-+"))
            return std::nullopt;
        return t.getDoubleValue();
    };
    gainField.setTooltip("Gain of the selected slice: silence to +24 dB");
    gainField.onChange = [this](double v) {
        if (! updating && onParamChanged)
            onParamChanged(SliceParam::gain, v <= silenceFieldDb + 0.05 ? model::silenceGainDb : v);
    };

    reverseButton.setTooltip("Plays the selected slice backwards");
    reverseButton.onClick = [this] {
        if (! updating && onParamChanged)
            onParamChanged(SliceParam::reverse, reverseButton.getToggleState() ? 1.0 : 0.0);
    };

    playModeControl.setItems({"One-shot", "Gate", "Loop"});
    playModeControl.setTooltip("One-shot plays to the end; Gate plays while the pad is held; Loop repeats while it is held");
    playModeControl.onChange = [this](int index) {
        if (! updating && onParamChanged)
            onParamChanged(SliceParam::playMode, static_cast<double>(index));
    };

    for (auto* field : {&startField, &endField})
    {
        field->clickToType = true;
        field->dragEnabled = false;
        field->justification = juce::Justification::centredLeft;
        field->formatter = [this](double v) { return positionText(shownUnits, view.tempo, static_cast<juce::int64>(v)); };
        field->parser = [this](const juce::String& text) -> std::optional<double> {
            const auto frame = parsePosition(shownUnits, view.tempo, text);
            return frame ? std::optional<double>(static_cast<double>(*frame)) : std::nullopt;
        };
    }
    startField.setTooltip("Where the selected slice starts. Type a new position");
    endField.setTooltip("Where the selected slice ends. Type a new position");
    startField.onChange = [this](double v) {
        if (updating)
            return;
        const bool ok = onBoundsTyped && onBoundsTyped(false, static_cast<juce::int64>(v));
        if (! ok)
            refreshFields();
    };
    endField.onChange = [this](double v) {
        if (updating)
            return;
        const bool ok = onBoundsTyped && onBoundsTyped(true, static_cast<juce::int64>(v));
        if (! ok)
            refreshFields();
    };

    unitsControl.setItems({"Bars", "Seconds", "Samples"});
    unitsControl.onChange = [this](int index) {
        shownUnits = static_cast<Units>(index);
        refreshFields();
        if (onUnitsChanged)
            onUnitsChanged(shownUnits);
    };
}

void SlicePanel::setSlice(const SliceView& v, Units units)
{
    view = v;
    shownUnits = units;
    unitsControl.setSelected(static_cast<int>(units), false);
    refreshFields();
}

void SlicePanel::refreshFields()
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    const auto total = static_cast<double>(juce::jmax<juce::int64>(1, view.totalFrames));
    for (auto* field : {&startField, &endField})
        field->setRange({0.0, total, 0.0, 1.0});
    startField.setValue(static_cast<double>(view.startFrame), false);
    endField.setValue(static_cast<double>(view.endFrame), false);
    lengthShown = lengthText(shownUnits, view.tempo, view.endFrame - view.startFrame);
    pitchField.setValue(view.settings.pitch, false);
    gainField.setValue(view.settings.gainDb <= model::silenceGainDb + 0.05 ? silenceFieldDb : view.settings.gainDb, false);
    reverseButton.setToggleState(view.settings.reverse, juce::dontSendNotification);
    playModeControl.setSelected(static_cast<int>(view.settings.playMode), false);
    // The ends of the sound cannot be moved.
    startField.setEnabled(view.index > 0);
    endField.setEnabled(view.index + 1 < view.count);
    repaint();
}

void SlicePanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);
    auto left = area.removeFromLeft(juce::jmin(300, area.getWidth() / 2));
    area.removeFromLeft(24);
    auto right = area;

    unitsControl.setBounds(left.removeFromTop(30).removeFromRight(240));
    left.removeFromTop(10);
    const auto row = [](juce::Rectangle<int>& column, int height) {
        auto r = column.removeFromTop(height);
        column.removeFromTop(4);
        return r;
    };
    startField.setBounds(row(left, 30).withTrimmedLeft(90));
    endField.setBounds(row(left, 30).withTrimmedLeft(90));

    right.removeFromTop(40);
    pitchField.setBounds(row(right, 30).withTrimmedLeft(90));
    gainField.setBounds(row(right, 30).withTrimmedLeft(90));
    reverseButton.setBounds(row(right, 30).withTrimmedLeft(90).withWidth(110));
    playModeControl.setBounds(row(right, 30).withTrimmedLeft(90));
}

void SlicePanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    auto label = Fonts::sans(10.0f);
    label.setExtraKerningFactor(0.08f);
    g.setFont(label);
    g.setColour(theme::muted);
    g.drawText("SLICE " + juce::String(view.index + 1) + " OF " + juce::String(view.count), getLocalBounds().reduced(16, 12).removeFromTop(30),
               juce::Justification::centredLeft);
    g.drawText("START", startField.getBounds().withX(16).withWidth(80), juce::Justification::centredLeft);
    g.drawText("END", endField.getBounds().withX(16).withWidth(80), juce::Justification::centredLeft);
    const auto lengthRow = endField.getBounds().translated(0, 34);
    g.drawText("LENGTH", lengthRow.withX(16).withWidth(80), juce::Justification::centredLeft);
    g.setColour(theme::text);
    g.setFont(Fonts::mono(15.0f, true));
    g.drawText(lengthShown, lengthRow, juce::Justification::centredLeft);

    g.setFont(label);
    g.setColour(theme::muted);
    g.drawText("PITCH", pitchField.getBounds().withX(pitchField.getX() - 90).withWidth(86), juce::Justification::centredLeft);
    g.drawText("GAIN", gainField.getBounds().withX(gainField.getX() - 90).withWidth(86), juce::Justification::centredLeft);
    g.drawText("PLAY MODE", playModeControl.getBounds().withX(playModeControl.getX() - 90).withWidth(86), juce::Justification::centredLeft);
}
} // namespace sampler::ui::editor
