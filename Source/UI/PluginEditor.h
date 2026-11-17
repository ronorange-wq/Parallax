#pragma once
#include "../PluginProcessor.h"

namespace plxui
{
namespace col
{
    const juce::Colour bg (0xff16171b), panel (0xff1f2228), slot (0xff272a31), line (0xff3a3d46),
                       text (0xffe8e6e1), dim (0xffa3a3a3), green (0xff6fcf97), orange (0xffffb07a), yellow (0xfff2c94c);
    inline juce::Colour chain (int c) { return c == 0 ? juce::Colour (0xffff8a3d) : c == 1 ? juce::Colour (0xff3dd6c4) : juce::Colour (0xffa78bfa); }
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
};

class Browser;

class ChainPanel : public juce::Component,
                   public juce::FileDragAndDropTarget
{
public:
    ChainPanel (ParallaxProcessor& p, int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();
    void refreshSolo();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    std::function<void (int chain)> onAdd;
    std::function<void (const juce::String&)> onMessage;

private:
    struct Row : juce::Component
    {
        juce::TextButton name, remove { "x" };
    };
    void showRowMenu (int i);

    ParallaxProcessor& proc;
    const int c;
    juce::Label title;
    juce::Slider level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt;
    juce::TextButton solo { "S" }, add { "+ Add plug-in" };
    juce::OwnedArray<Row> rows;
    bool dragOver = false;
};

class ParallaxEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit ParallaxEditor (ParallaxProcessor&);
    ~ParallaxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void openBrowser (int chain);
    void message (const juce::String& s) { toast = s; toastUntil = juce::Time::getMillisecondCounter() + 5000; repaint(); }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    ParallaxProcessor& proc;
    LookAndFeel lnf;
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;
    juce::TextButton lockButton, relearn { "Relearn" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAtt;
    juce::OwnedArray<ChainPanel> panels;
    std::unique_ptr<Browser> browser;
    juce::String toast;
    juce::uint32 toastUntil = 0;
    juce::Rectangle<int> lockLight, footer;
};
}

using ParallaxEditor = plxui::ParallaxEditor;
