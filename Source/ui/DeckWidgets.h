#pragma once

#include "engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace loopdeck
{

/**
    Spinning platter, Serato / VirtualDJ style. Turns at 33 1/3 rpm with the
    audio (so it slows with the tempo fader, stops when paused, runs backwards
    on a backspin). The outer ring shows how far through the track you are,
    with hot cues and the loop marked on it. Drag it round to scratch.
*/
class Platter : public juce::Component, public juce::SettableTooltipClient
{
public:
    Platter (AudioEngine& engine, int deckIndex);

    /** Repaints only when the picture would change. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override { disc = {}; }   // static parts are redrawn at the new size
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    double angleFor (double frames, double sampleRate) const;
    float pointerAngle (juce::Point<float> p) const;
    void buildDisc (int size);

    AudioEngine& engine;
    const int deck;
    juce::Image disc;      // static vinyl grooves, drawn once per size / theme
    int discTheme = -1;
    juce::uint64 lastSignature = 0;

    bool scratching = false;
    float lastAngle = 0.0f;
    double turned = 0.0;   // radians since the drag started
    double scratchStart = 0.0;
};

/**
    One effect slot: a lit power key carrying the effect name (right-click or
    the arrow to choose the effect), an amount knob, and a beat length.
*/
class FxSlotComponent : public juce::Component
{
public:
    FxSlotComponent (AudioEngine& engine, int deckIndex, int slotIndex);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showTypeMenu();
    void stepBeats (int dir);
    FxSlotControls& slot();

    AudioEngine& engine;
    const int deck, slotIndex;

    class PowerButton : public juce::TextButton
    {
    public:
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
            juce::TextButton::mouseDown (e);
        }
    };
    /** Small key with a drawn down-arrow (no font glyph needed, so it looks the same on every system). */
    class ArrowButton : public juce::TextButton
    {
    public:
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            juce::TextButton::paintButton (g, over, down);
            auto c = getLocalBounds().toFloat().getCentre();
            juce::Path p;
            p.addTriangle (c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y + 2.5f);
            g.setColour (Theme::tech ? Theme::active.brighter (0.2f) : Theme::textDim);
            g.fillPath (p);
        }
    };
    PowerButton power;
    ArrowButton chooser;
    juce::TextButton beatsDown { "<" }, beatsUp { ">" };
    juce::Slider amount;
    juce::Label beatsLabel;
};

} // namespace loopdeck
