#pragma once

#include "AppController.h"
#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sampler::app
{
/** The one main window. Closing it asks the application to quit. */
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(AppController&);
    ~MainWindow() override;

    MainComponent& content() { return mainComponent; }

    void closeButtonPressed() override;

private:
    MainComponent mainComponent;
};
} // namespace sampler::app
