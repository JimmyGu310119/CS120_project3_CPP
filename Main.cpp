#include "Node.h"
#include <JuceHeader.h>

class Application : public juce::JUCEApplication {
public:
    Application() = default;
    const juce::String getApplicationName() override { return "Mine Project 3"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    void initialise(const juce::String &) override { 
        mainWindow.reset(new MainWindow(getApplicationName(), new MainContentComponent, *this)); 
    }
    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String &name, juce::Component *c, JUCEApplication &a) 
            : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::allButtons), app(a) {
            setUsingNativeTitleBar(true);
            setContentOwned(c, true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { app.systemRequestedQuit(); }
    private:
        JUCEApplication &app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(Application)