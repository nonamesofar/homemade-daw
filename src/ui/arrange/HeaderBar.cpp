#include "HeaderBar.h"

#include "commands/ArrangeCommands.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui
{
namespace
{
constexpr double silenceFieldDb = -70.0;

juce::Font labelFont()
{
    auto f = Fonts::sans(10.0f);
    f.setExtraKerningFactor(0.08f);
    return f;
}
} // namespace

//==============================================================================
TransportButton::TransportButton(const juce::String& name, Icon i) : juce::Button(name), icon(i)
{
    setWantsKeyboardFocus(false);
    setTooltip(name);
}

void TransportButton::setLit(bool on)
{
    if (lit == on)
        return;
    lit = on;
    repaint();
}

void TransportButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto litColour = icon == Icon::record ? theme::record : theme::accent;
    g.setColour(lit ? litColour : (down || highlighted) && isEnabled() ? theme::selected : theme::raised);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(lit ? litColour : theme::border);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    const auto c = bounds.getCentre();
    juce::Path p;
    switch (icon)
    {
        case Icon::stop: p.addRoundedRectangle(c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 2.0f); break;
        case Icon::play: p.addTriangle(c.x - 5.0f, c.y - 6.5f, c.x - 5.0f, c.y + 6.5f, c.x + 6.0f, c.y); break;
        case Icon::record: p.addEllipse(c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f); break;
    }
    const auto ink = icon == Icon::record ? (lit ? theme::background : theme::record) : (lit ? theme::background : theme::text);
    g.setColour(isEnabled() ? ink : ink.withAlpha(0.35f));
    g.fillPath(p);
}

//==============================================================================
void PositionDisplay::setPosition(double beats)
{
    const auto text = PositionText::format(beats, timeSig ? timeSig() : engine::TimeSig{});
    if (text != shown)
    {
        shown = text;
        repaint();
    }
}

void PositionDisplay::startEditing()
{
    if (editor != nullptr)
        return;
    editor = std::make_unique<juce::TextEditor>("position");
    editor->setFont(Fonts::mono(18.0f, true));
    editor->setText(shown.removeCharacters(" "), false);
    editor->addListener(this);
    addAndMakeVisible(*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
}

bool PositionDisplay::commit(const juce::String& text)
{
    const auto beats = PositionText::parse(text, timeSig ? timeSig() : engine::TimeSig{});
    if (! beats)
        return false;
    if (onLocate)
        onLocate(*beats);
    setPosition(*beats);
    return true;
}

void PositionDisplay::textEditorReturnKeyPressed(juce::TextEditor& e)
{
    if (commit(e.getText()))
        close();
    else
        e.selectAll(); // refused: the editor stays for another try
}

void PositionDisplay::close()
{
    if (editor == nullptr)
        return;
    auto* raw = editor.release();
    raw->removeListener(this);
    raw->setVisible(false);
    juce::MessageManager::callAsync([raw] { delete raw; });
    repaint();
}

void PositionDisplay::resized()
{
    if (editor != nullptr)
        editor->setBounds(getLocalBounds().withTrimmedTop(14));
}

void PositionDisplay::paint(juce::Graphics& g)
{
    g.setColour(theme::muted);
    g.setFont(labelFont());
    g.drawText("POSITION", getLocalBounds().removeFromTop(12), juce::Justification::topLeft);
    if (editor == nullptr)
    {
        g.setColour(theme::text);
        g.setFont(Fonts::mono(20.0f, true));
        g.drawText(shown, getLocalBounds().withTrimmedTop(12), juce::Justification::centredLeft);
    }
}

//==============================================================================
void TimeSigField::paint(juce::Graphics& g)
{
    g.setColour(theme::muted);
    g.setFont(labelFont());
    g.drawText("TIME SIG", getLocalBounds().removeFromTop(12), juce::Justification::topLeft);
    g.setColour(theme::text);
    g.setFont(Fonts::mono(20.0f, true));
    g.drawText(juce::String(sig.numerator) + " / " + juce::String(sig.denominator), getLocalBounds().withTrimmedTop(12),
               juce::Justification::centredLeft);
}

void TimeSigField::mouseUp(const juce::MouseEvent& e)
{
    juce::PopupMenu menu;
    const bool numerator = e.x < getWidth() / 2;
    if (numerator)
        for (int n = 1; n <= 32; ++n)
            menu.addItem(n, juce::String(n), true, n == sig.numerator);
    else
        for (int d : {2, 4, 8, 16})
            menu.addItem(100 + d, juce::String(d), true, d == sig.denominator);
    juce::Component::SafePointer<TimeSigField> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, numerator](int result) {
        if (safe == nullptr || result == 0 || ! safe->onChange)
            return;
        if (numerator)
            safe->onChange(result, safe->sig.denominator);
        else
            safe->onChange(safe->sig.numerator, result - 100);
    });
}

//==============================================================================
void MasterMeter::push(const engine::LevelReading& reading, double elapsedMs)
{
    left.push(reading.peakDb[0], elapsedMs, reading.over && reading.peakDb[0] >= reading.peakDb[1]);
    right.push(reading.peakDb[1], elapsedMs, reading.over && reading.peakDb[1] > reading.peakDb[0]);
    repaint();
}

void MasterMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().withTrimmedRight(16).toFloat();
    const auto barH = (area.getHeight() - 4.0f) / 2.0f;
    for (const auto* m : {&left, &right})
    {
        auto bar = area.removeFromTop(barH);
        area.removeFromTop(4.0f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(bar, 2.0f);
        const auto w = bar.getWidth() * static_cast<float>(dbToFraction(m->levelDb(), MeterBallistics::floorDb));
        g.setColour(m->levelDb() > -1.0 ? theme::record : theme::ok);
        g.fillRect(bar.withWidth(w));
        g.setColour(theme::ok.darker(0.6f));
        g.fillRect(bar.reduced(0.0f, 1.0f).withWidth(bar.getWidth() * static_cast<float>(dbToFraction(m->rmsDb(), MeterBallistics::floorDb))));
        const auto hold = bar.getWidth() * static_cast<float>(dbToFraction(m->heldDb(), MeterBallistics::floorDb));
        if (m->heldDb() > MeterBallistics::floorDb)
        {
            g.setColour(theme::text);
            g.fillRect(bar.withX(bar.getX() + hold - 1.0f).withWidth(1.5f));
        }
    }
    const auto light = overLightArea().toFloat();
    g.setColour(isOverLit() ? theme::record : theme::raised);
    g.fillEllipse(light);
    g.setColour(theme::border);
    g.drawEllipse(light, 1.0f);
}

void MasterMeter::mouseUp(const juce::MouseEvent&) { clearOver(); }

//==============================================================================
HeaderBar::HeaderBar(HeaderContext context) : ctx(std::move(context))
{
    for (auto* child : {static_cast<juce::Component*>(&viewToggle), static_cast<juce::Component*>(&stopButton),
                        static_cast<juce::Component*>(&playButton), static_cast<juce::Component*>(&recordButton),
                        static_cast<juce::Component*>(&tempo), static_cast<juce::Component*>(&timeSig),
                        static_cast<juce::Component*>(&position), static_cast<juce::Component*>(&clickButton),
                        static_cast<juce::Component*>(&loopButton), static_cast<juce::Component*>(&snapButton),
                        static_cast<juce::Component*>(&masterVolume), static_cast<juce::Component*>(&masterMeter)})
        addAndMakeVisible(child);

    viewToggle.setItems({"Session", "Arrange"});
    viewToggle.setSelected(1);
    viewToggle.onChange = [this](int index) {
        if (index != 1)
        {
            viewToggle.setSelected(showingEditor ? -1 : 1); // the Session view arrives in M6
            return;
        }
        if (onArrangeChosen)
            onArrangeChosen();
    };
    viewToggle.setTooltip("The Session view arrives in a later version");

    stopButton.onClick = [this] { stopPressed(); };
    playButton.onClick = [this] { togglePlay(); };
    recordButton.setEnabled(static_cast<bool>(ctx.toggleRecord));
    recordButton.setTooltip("Record on the armed tracks (Ctrl+R)");
    recordButton.onClick = [this] {
        if (ctx.toggleRecord)
            ctx.toggleRecord();
    };

    tempo.setRange({20.0, 999.0, 120.0, 0.01});
    tempo.setSuffix({}, 2);
    tempo.drawBox = false;
    tempo.fontHeight = 20.0f;
    tempo.justification = juce::Justification::bottomLeft;
    tempo.setTooltip("Tempo: drag up or down, double-click to type");
    tempo.onDragStart = [this] {
        ctx.commands.begin("Set tempo");
        tempoGesture = std::make_unique<commands::SetTempo>(ctx.arrangement.tempo());
    };
    tempo.onDragEnd = [this] {
        ctx.commands.commit();
        tempoGesture.reset();
    };
    tempo.onChange = [this](double bpm) {
        if (tempo.isDragging() && tempoGesture != nullptr)
        {
            tempoGesture->setBpm(bpm); // one command for the whole drag, so its clip lengths baseline survives
            ctx.commands.update(*tempoGesture);
        }
        else
        {
            commands::SetTempo command(bpm);
            ctx.commands.apply(command);
        }
    };

    timeSig.onChange = [this](int n, int d) {
        commands::SetTimeSig command(n, d);
        ctx.commands.apply(command);
    };

    position.timeSig = [this] { return ctx.arrangement.timeSig(); };
    position.onLocate = [this](double beats) { ctx.transport.locateBeats(beats); };

    clickButton.setWantsKeyboardFocus(false);
    loopButton.setWantsKeyboardFocus(false);
    clickButton.setTooltip("Metronome");
    loopButton.setTooltip("Loop (L)");
    clickButton.onClick = [this] { ctx.transport.setClickOn(clickButton.getToggleState()); };
    loopButton.onClick = [this] {
        ctx.transport.setLooping(loopButton.getToggleState());
        refresh();
    };
    snapButton.setWantsKeyboardFocus(false);
    snapButton.onClick = [this] { showSnapMenu(); };

    masterVolume.setRange({silenceFieldDb, 6.0, 0.0, 0.1});
    masterVolume.formatter = [](double v) { return formatDb(v <= silenceFieldDb + 0.05 ? engine::Arrangement::silenceDb : v) + " dB"; };
    masterVolume.detentValue = 0.0;
    masterVolume.detentWidth = 0.6;
    masterVolume.doubleClickResets = true;
    masterVolume.fontHeight = 12.0f;
    masterVolume.setTooltip("Master volume: drag up or down, double-click for 0 dB");
    masterVolume.onDragStart = [this] { ctx.commands.begin("Set volume"); };
    masterVolume.onDragEnd = [this] { ctx.commands.commit(); };
    masterVolume.onChange = [this](double v) {
        commands::SetTrackParam command({}, commands::SetTrackParam::Control::volume,
                                        v <= silenceFieldDb + 0.05 ? engine::Arrangement::silenceDb : v);
        if (masterVolume.isDragging())
            ctx.commands.update(command);
        else
            ctx.commands.apply(command);
    };
    masterMeter.setTooltip("Master level; the light shows the output went over 0 dB (click to clear)");

    const_cast<engine::Arrangement&>(ctx.arrangement).addChangeListener(this);
    refresh();
}

HeaderBar::~HeaderBar() { const_cast<engine::Arrangement&>(ctx.arrangement).removeChangeListener(this); }

void HeaderBar::refresh()
{
    if (! tempo.isDragging() && ! tempo.isEditing())
        tempo.setValue(ctx.arrangement.tempo(), false);
    timeSig.setTimeSig(ctx.arrangement.timeSig());
    clickButton.setToggleState(ctx.transport.isClickOn(), juce::dontSendNotification);
    loopButton.setToggleState(ctx.transport.isLooping(), juce::dontSendNotification);
    snapButton.setButtonText("Snap: " + Snap::name(ctx.snap ? ctx.snap() : SnapValue::bar));
    if (! masterVolume.isDragging())
        masterVolume.setValue(std::max(silenceFieldDb, ctx.arrangement.masterVolumeDb()), false);
    repaint();
}

void HeaderBar::tick(double elapsedMs)
{
    playButton.setLit(ctx.transport.isPlaying());
    recordButton.setLit(ctx.isRecording && ctx.isRecording());
    if (! position.isEditing())
        position.setPosition(ctx.transport.positionBeats());
    masterMeter.push(ctx.meters.readMaster(), elapsedMs);
}

void HeaderBar::stopPressed()
{
    if (ctx.stopPressed)
        ctx.stopPressed();
    if (ctx.transport.isPlaying() || ctx.transport.isPlayPending())
        ctx.transport.stop();
    else if (! (ctx.isRecording && ctx.isRecording()))
        returnToStart(); // while a recording runs the playhead is locked: a second Stop only waits for it to end
}

void HeaderBar::returnToStart()
{
    ctx.transport.locateBeats(TransportLogic::returnPosition(ctx.transport.isLooping(), ctx.arrangement.loopRange()));
}

void HeaderBar::togglePlay()
{
    if (ctx.transport.isPlaying() || ctx.transport.isPlayPending())
        ctx.transport.stop();
    else
        ctx.transport.play();
}

void HeaderBar::showSnapMenu()
{
    juce::PopupMenu menu;
    const auto current = ctx.snap ? ctx.snap() : SnapValue::bar;
    int id = 1;
    for (auto value : Snap::all())
    {
        if (value == SnapValue::quarterTriplet)
            menu.addSeparator();
        menu.addItem(id++, Snap::name(value), true, value == current);
    }
    juce::Component::SafePointer<HeaderBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&snapButton), [safe](int result) {
        if (safe == nullptr || result <= 0 || ! safe->ctx.setSnap)
            return;
        safe->ctx.setSnap(Snap::all()[static_cast<size_t>(result - 1)]);
        safe->refresh();
    });
}

void HeaderBar::resized()
{
    auto area = getLocalBounds();
    const auto gap = 12;
    viewToggle.setBounds(area.removeFromLeft(170).withSizeKeepingCentre(170, 40));
    area.removeFromLeft(gap);
    for (auto* b : {&stopButton, &playButton, &recordButton})
    {
        b->setBounds(area.removeFromLeft(44).withSizeKeepingCentre(44, 44));
        area.removeFromLeft(8);
    }
    area.removeFromLeft(gap - 8);
    tempo.setBounds(area.removeFromLeft(84).withSizeKeepingCentre(84, 40).withTrimmedTop(12));
    area.removeFromLeft(gap);
    timeSig.setBounds(area.removeFromLeft(72).withSizeKeepingCentre(72, 40));
    area.removeFromLeft(gap);
    position.setBounds(area.removeFromLeft(130).withSizeKeepingCentre(130, 40));
    area.removeFromLeft(gap);
    clickButton.setBounds(area.removeFromLeft(60).withSizeKeepingCentre(60, 36));
    area.removeFromLeft(8);
    loopButton.setBounds(area.removeFromLeft(60).withSizeKeepingCentre(60, 36));
    area.removeFromLeft(8);
    snapButton.setBounds(area.removeFromLeft(100).withSizeKeepingCentre(100, 36));
    area.removeFromLeft(gap);
    masterVolume.setBounds(area.removeFromLeft(74).withSizeKeepingCentre(74, 26).translated(0, 6));
    area.removeFromLeft(6);
    masterMeter.setBounds(area.removeFromLeft(84).withSizeKeepingCentre(84, 18).translated(0, 6));
}

void HeaderBar::paint(juce::Graphics& g)
{
    g.setColour(theme::muted);
    g.setFont(labelFont());
    g.drawText("TEMPO", tempo.getBounds().withY(tempo.getY() - 12).withHeight(12), juce::Justification::topLeft);
    g.drawText("MASTER", masterVolume.getBounds().getUnion(masterMeter.getBounds()).withY(masterVolume.getY() - 14).withHeight(12),
               juce::Justification::topLeft);
}
} // namespace sampler::ui
