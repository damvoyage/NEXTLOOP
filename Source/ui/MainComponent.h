#pragma once

#include "DeckPanel.h"
#include "MixerPanel.h"
#include "FileBrowserPanel.h"

namespace loopdeck
{

/** Drag bar between the decks and the library. */
class DividerBar : public juce::Component
{
public:
    std::function<void (int deltaY)> onDrag;
    DividerBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::line);
        g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (40.0f, 3.0f), 1.5f);
    }
    void mouseDown (const juce::MouseEvent&) override { last = 0; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = e.getDistanceFromDragStartY() - last;
        last = e.getDistanceFromDragStartY();
        if (onDrag) onDrag (dy);
    }
private:
    int last = 0;
};

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    MainComponent (AudioEngine& engine, juce::PropertiesFile& props);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void setNumDecks (int n);

    /** Called (asynchronously) after the user switches day/night; the window rebuilds the UI. */
    std::function<void()> onThemeChanged;
    void showSettings();
    void saveState();

private:
    void timerCallback() override;
    void loadFile (int deck, const juce::File& file);
    void loadToNextDeck (const juce::File& file);
    void layoutClassic (juce::Rectangle<int> area, int n);
    void layoutStrips (juce::Rectangle<int> area, int n);
    void loadDeckSettings();
    void saveDeckSettings();
    void loadViewOptions();
    void saveViewOptions();
    void updateViewButtons();
    void loadEffects();
    void saveEffects();
    void rebuildLook();

    AudioEngine& engine;
    juce::PropertiesFile& props;
    NEXTLOOPLookAndFeel lnf;
    DeckSettings deckSettings;
    juce::TooltipWindow tooltips { this, 700 };

    std::array<std::unique_ptr<ScrollingWaveform>, 4> rowWaves;
    std::array<std::unique_ptr<DeckPanel>, maxDecks> decks;
    std::array<std::unique_ptr<ChannelComponent>, maxDecks> channels;
    MasterSection masterSection;
    FileBrowserPanel browser;
    DividerBar divider;

    juce::Label deckCountLabel { {}, "Decks" }, masterLabel, cpuLabel;
    std::array<std::unique_ptr<juce::TextButton>, 7> deckCountButtons;   // 2..8
    juce::TextButton settingsButton { "Audio and settings" }, themeButton, monoButton, skinButton;
    juce::Label viewLabel { {}, "SHOW" };
    std::array<std::unique_ptr<juce::TextButton>, numSections> viewButtons;

    int browserHeight = 230;
    int frameCounter = 0;
    int currentFps = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace loopdeck
