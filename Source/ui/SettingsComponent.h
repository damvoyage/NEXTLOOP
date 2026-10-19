#pragma once

#include "DeckPanel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace loopdeck
{

/** Audio interface, output routing, analysis and cache preferences. */
class SettingsComponent : public juce::Component, private juce::ChangeListener
{
public:
    SettingsComponent (AudioEngine& engine, juce::PropertiesFile& props, DeckSettings& deckSettings);
    ~SettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshRouting();
    void refreshCacheLabel();

    AudioEngine& engine;
    juce::PropertiesFile& props;
    DeckSettings& deckSettings;

    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label masterLabel { {}, "Master output" }, cueLabel { {}, "Headphone output" },
                bpmLabel { {}, "BPM range for detection" }, pitchLabel { {}, "Tempo fader" },
                cacheLabel { {}, "Decoded audio cache" }, cacheInfo,
                stretchLabel { {}, "Key lock sound" }, stretchInfo, fpsLabel { {}, "Screen refresh" };
    juce::ComboBox masterOut, cueOut, bpmRange, pitchDirection, cacheLimit, stretchAlgo, fps;
    juce::TextButton clearCache { "Clear cache" };
    juce::ToggleButton keylockDefault { "Key lock on for newly loaded tracks" };
};

} // namespace loopdeck
