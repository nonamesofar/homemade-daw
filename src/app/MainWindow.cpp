#include "MainWindow.h"

namespace sampler::app
{
MainWindow::MainWindow(AppController& controller)
    : DocumentWindow("Sampler", juce::Colour(0xff1b1e23), DocumentWindow::allButtons), mainComponent(controller)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&mainComponent, true);
    setResizable(true, true);
    setResizeLimits(720, 480, 4000, 3000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    mainComponent.refresh(); // sets the title now that the window exists
}

MainWindow::~MainWindow() { clearContentComponent(); }

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
} // namespace sampler::app
