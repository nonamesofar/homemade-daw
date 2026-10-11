#pragma once

#include "ArrangeLogic.h"
#include "commands/ArrangeCommands.h"
#include "commands/CommandService.h"
#include "engine/Arrangement.h"
#include "engine/LevelMeters.h"
#include "engine/Transport.h"
#include "ui/widgets/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace sampler::ui
{
struct HeaderContext
{
    const engine::Arrangement& arrangement;
    engine::Transport& transport;
    engine::LevelMeters& meters;
    commands::CommandService& commands;
    SelectionModel& selection;
    std::function<SnapValue()> snap;
    std::function<void(SnapValue)> setSnap;
};

/** Stop, Play and Record buttons: drawn icons; Play is lit only while the transport really plays. */
class TransportButton : public juce::Button
{
public:
    enum class Icon
    {
        stop,
        play,
        record
    };
    TransportButton(const juce::String& name, Icon);
    void setLit(bool);
    bool isLit() const { return lit; }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    bool lit = false;
};

/** "bar . beat . sixteenth" of the playhead; click to type a position, Enter moves there, Escape cancels. */
class PositionDisplay : public juce::Component, private juce::TextEditor::Listener
{
public:
    std::function<engine::TimeSig()> timeSig;
    std::function<void(double beats)> onLocate;

    void setPosition(double beats);
    juce::String text() const { return shown; }
    void startEditing();
    bool isEditing() const { return editor != nullptr; }
    juce::TextEditor* textEditor() { return editor.get(); }
    /** Applies typed text; false (editor stays open) if it is not a position. */
    bool commit(const juce::String&);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override { startEditing(); }

private:
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { close(); }
    void textEditorFocusLost(juce::TextEditor&) override { close(); }
    void close();

    juce::String shown = "1 . 1 . 1";
    std::unique_ptr<juce::TextEditor> editor;
};

/** "4 / 4": the left number opens the numerators (1 to 32), the right one the denominators (2, 4, 8, 16). */
class TimeSigField : public juce::Component
{
public:
    std::function<void(int numerator, int denominator)> onChange;
    void setTimeSig(engine::TimeSig s)
    {
        sig = s;
        repaint();
    }
    engine::TimeSig timeSig() const { return sig; }
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    engine::TimeSig sig;
};

/** The master output: a stereo meter and the over light (click clears it). */
class MasterMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void push(const engine::LevelReading&, double elapsedMs);
    bool isOverLit() const { return left.isOver() || right.isOver(); }
    void clearOver()
    {
        left.clearOver();
        right.clearOver();
        repaint();
    }
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::Rectangle<int> overLightArea() const { return getLocalBounds().removeFromRight(12).withSizeKeepingCentre(10, 10); }

private:
    MeterBallistics left, right;
};

/**
    The transport and project controls of the header (app-shell spec, left to right): view toggle, Stop, Play, Record
    (disabled), TEMPO, TIME SIG, POSITION, Click, Loop, the snap menu and the master volume and meter.
*/
class HeaderBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit HeaderBar(HeaderContext);
    ~HeaderBar() override;

    void refresh();
    /** One display frame: Play highlight, position, master meter. */
    void tick(double elapsedMs);

    /** Stop: stops when playing, else goes back (loop start or song start). */
    void stopPressed();
    void togglePlay();
    void returnToStart();

    SegmentedControl viewToggle;
    TransportButton stopButton{"Stop", TransportButton::Icon::stop}, playButton{"Play", TransportButton::Icon::play},
        recordButton{"Record", TransportButton::Icon::record};
    ValueField tempo;
    TimeSigField timeSig;
    PositionDisplay position;
    ToggleButton clickButton{"Click"}, loopButton{"Loop"};
    juce::TextButton snapButton;
    ValueField masterVolume;
    MasterMeter masterMeter;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void showSnapMenu();

    HeaderContext ctx;
    std::unique_ptr<commands::SetTempo> tempoGesture; // one command for the whole tempo drag
};
} // namespace sampler::ui
