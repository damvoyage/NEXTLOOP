#pragma once

#include "WaveformView.h"
#include "DeckWidgets.h"

namespace loopdeck
{

/** Button with press / release / right-click callbacks (for cues, rolls, nudges). */
class PadButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void (const juce::ModifierKeys&)> onPress;
    std::function<void()> onRelease;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
        held = true;
        if (onPress) onPress (e.mods);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        if (held) { held = false; if (onRelease) onRelease(); }
    }

private:
    bool held = false;
};

/** Parts of the screen that can be shown or hidden. Hidden parts give their space to the waveforms. */
enum class Section : int { Browser = 0, Mixer, Waves, Platter, HotCues, Loops, LoopPads, LoopEdit, Fx, Tempo, Grid, count };
constexpr int numSections = (int) Section::count;
const char* sectionName (Section);
const char* sectionKey (Section);
const char* sectionTip (Section);

/** What is shown. 1-4 decks and 5-8 decks remember their own choices. */
struct ViewOptions
{
    std::array<bool, numSections> classic { true, true, true, true, true, true, true, false, true, true, false };
    std::array<bool, numSections> strips  { true, true, true, true, true, true, false, false, true, true, false };
    bool stripLayout = false;

    bool shown (Section s) const { return (stripLayout ? strips : classic)[(size_t) s]; }
    void toggle (Section s) { auto& a = stripLayout ? strips : classic; a[(size_t) s] = ! a[(size_t) s]; }
};

/** Settings the deck panels share (owned by MainComponent). */
struct DeckSettings
{
    ViewOptions view;
    std::array<LoopLength, 8> quickLoops { LoopLength::beats (0.25), LoopLength::beats (0.5), LoopLength::beats (1),
                                           LoopLength::bars (1), LoopLength::bars (2), LoopLength::bars (4),
                                           LoopLength::bars (5), LoopLength::bars (7) };
    bool pitchUpIsFaster = false;   // Technics convention: pull the fader down to speed up
    std::function<void()> onChanged;
};

/**
    One deck's controls. Two layouts:
      - wide: used for 1–4 decks (scrolling waveform lives in the stacked area above)
      - strip: tall and narrow for 5–8 decks, with its own vertical waveform
*/
class DeckPanel : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget
{
public:
    DeckPanel (AudioEngine& engine, int deckIndex, DeckSettings& settings, LoadFileFn loadFn);
    ~DeckPanel() override;

    void setStripMode (bool strip);
    bool isStripMode() const { return stripMode; }

    /** Height the wide layout needs for the sections currently shown. */
    int preferredWideHeight (int width) const;

    /** Called by MainComponent ~20 times a second. */
    void refresh();
    void repaintWaveforms();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

    /** Keyboard / MIDI friendly entry points. */
    void togglePlay();
    void loopToggle();

private:
    void layoutWide (juce::Rectangle<int> r);
    void layoutStrip (juce::Rectangle<int> r);
    void applyVisibility();
    bool shown (Section s) const { return settings.view.shown (s); }
    juce::Rectangle<int> headerDisplay;   // LCD window behind the numbers
    void setupButton (juce::Button& b, const juce::String& text, const juce::String& tip, bool deckColoured = false);
    void updateLoopLabel();
    void setCurrentLength (LoopLength l, bool resizeActiveLoop);
    double currentBeats() const;
    int beatsPerBar() const;
    double fineStepFrames() const;
    bool pitchIsInverted() const { return ! settings.pitchUpIsFaster; }
    void editBpm();
    void editLoopLength();
    void editQuickLoop (int i);
    void gridChanged();
    void tapTempo();
    void showKeyLockMenu();
    void shiftKey (int semitones);

    AudioEngine& engine;
    const int deck;
    DeckSettings& settings;
    LoadFileFn loadFile;
    bool stripMode = false;
    juce::Colour hue;

    OverviewWaveform overview;
    ScrollingWaveform stripWave;
    Platter platter;
    std::array<std::unique_ptr<FxSlotComponent>, fxSlotsPerDeck> fxSlots;

    juce::Label title, artist, timeLabel, bpmLabel, pitchLabel, keyLabel, loopLabel;
    PadButton cue { "Cue" }, nudgeDown { "-" }, nudgeUp { "+" }, brake { "Brake" }, spin { "Spin" };
    PadButton keyLock { "Key lock" }, keyDown { "Key-" }, keyUp { "Key+" };
    juce::TextButton play { "Play" }, sync { "Sync" }, master { "Master" },
                     quant { "Quant" }, slip { "Slip" }, rollMode { "Roll" };
    std::array<std::unique_ptr<PadButton>, numHotCues> hot;
    std::array<std::unique_ptr<PadButton>, 8> quick;
    juce::TextButton loopPrev { "<" }, loopNext { ">" }, unitToggle { "Bars" }, loopBtn { "Loop" },
                     halve { "1/2" }, dbl { "x2" }, loopIn { "In" }, loopOut { "Out" }, reloop { "Reloop" },
                     moveL { "Move <" }, moveR { "Move >" }, inL { "In <" }, inR { "In >" }, outL { "Out <" }, outR { "Out >" },
                     jumpL { "Jump <" }, jumpR { "Jump >" },
                     gridHalf { "/2" }, gridDouble { "x2" }, gridL { "Grid <" }, gridR { "Grid >" }, gridSet { "Set 1" }, tap { "Tap" },
                     eject { "Eject" };
    juce::ComboBox fineStep, pitchRange;
    juce::Slider pitch;

    LoopLength length = LoopLength::bars (1);
    juce::Array<double> taps;
    bool wasLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckPanel)
};

} // namespace loopdeck
