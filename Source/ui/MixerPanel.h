#pragma once

#include "engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace loopdeck
{

/** Stereo peak meter fed from the engine's atomics. */
class LevelMeter : public juce::Component
{
public:
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;
private:
    float levelL = 0.0f, levelR = 0.0f, holdL = 0.0f, holdR = 0.0f;
};

/** Knob with a caption underneath. */
class CaptionedKnob : public juce::Component
{
public:
    CaptionedKnob (const juce::String& caption, juce::Colour accent, bool bipolar);
    juce::Slider slider;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::String caption;
};

/** One mixer channel: trim, isolator EQ, filter, fader, cue, crossfader side. */
class ChannelComponent : public juce::Component
{
public:
    ChannelComponent (AudioEngine& engine, int deckIndex);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    const int deck;
    juce::Colour hue;
    CaptionedKnob trim, high, mid, low, filter;
    juce::Slider fader;
    LevelMeter meter;
    juce::TextButton cue { "Cue" }, assign { "A" };
};

/** Crossfader, master and headphone controls. */
class MasterSection : public juce::Component
{
public:
    explicit MasterSection (AudioEngine& engine);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    CaptionedKnob masterVol, phonesMix, phonesVol;
    juce::Slider crossfader;
    juce::ComboBox curve;
    juce::TextButton limiter { "Limiter" }, splitCue { "Split cue" };
    LevelMeter meter;
};

} // namespace loopdeck
