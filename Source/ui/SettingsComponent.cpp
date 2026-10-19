#include "SettingsComponent.h"

namespace loopdeck
{

static const double bpmMins[] = { 68.0, 78.0, 88.0, 98.0 };
static const int cacheGbs[] = { 5, 10, 20, 50, 100 };

SettingsComponent::SettingsComponent (AudioEngine& e, juce::PropertiesFile& p, DeckSettings& ds)
    : engine (e), props (p), deckSettings (ds),
      deviceSelector (e.getDeviceManager(), 0, 0, 2, 8, false, false, false, false)
{
    addAndMakeVisible (deviceSelector);
    engine.getDeviceManager().addChangeListener (this);

    for (auto* l : { &masterLabel, &cueLabel, &bpmLabel, &pitchLabel, &cacheLabel, &cacheInfo, &stretchLabel, &stretchInfo, &fpsLabel })
    {
        addAndMakeVisible (*l);
        l->setFont (Theme::label (13.0f));
        l->setJustificationType (juce::Justification::centredRight);
    }
    cacheInfo.setJustificationType (juce::Justification::centredLeft);
    stretchInfo.setJustificationType (juce::Justification::topLeft);
    stretchInfo.setColour (juce::Label::textColourId, Theme::textDim);
    stretchInfo.setMinimumHorizontalScale (1.0f);

    addAndMakeVisible (fps);
    fps.addItem ("60 frames per second (smoothest)", 60);
    fps.addItem ("30 frames per second (lighter on CPU)", 30);
    fps.setSelectedId (props.getIntValue ("uiFps", 60) == 30 ? 30 : 60, juce::dontSendNotification);
    fps.onChange = [this] { props.setValue ("uiFps", fps.getSelectedId()); };
    fps.setTooltip ("How often waveforms and meters redraw. Decks that aren't moving don't redraw at all");

    addAndMakeVisible (stretchAlgo);
    for (int a = 0; a < numStretchAlgorithms; ++a)
    {
        if (a == (int) StretchAlgorithm::VintageSampler) stretchAlgo.addSeparator();
        stretchAlgo.addItem (stretchAlgorithmName (a), a + 1);
        stretchAlgo.setItemEnabled (a + 1, stretchAlgorithmAvailable (a));
    }
    stretchAlgo.setSelectedId (props.getIntValue ("stretchAlgorithm", 0) + 1, juce::dontSendNotification);
    stretchAlgo.setTooltip ("Used by every deck when key lock or key shift is on. Right-click a deck's key lock button to override it for that deck");
    auto showInfo = [this] { stretchInfo.setText (stretchAlgorithmDescription (stretchAlgo.getSelectedId() - 1), juce::dontSendNotification); };
    showInfo();
    stretchAlgo.onChange = [this, showInfo]
    {
        const int a = stretchAlgo.getSelectedId() - 1;
        props.setValue ("stretchAlgorithm", a);
        for (int d = 0; d < maxDecks; ++d) engine.getDeck (d).setStretchAlgorithm (a);
        showInfo();
    };
    cacheInfo.setColour (juce::Label::textColourId, Theme::textDim);

    addAndMakeVisible (masterOut);
    masterOut.onChange = [this]
    {
        engine.masterOutChannel = (masterOut.getSelectedId() - 1) * 2;
        props.setValue ("masterOut", engine.masterOutChannel.load());
    };
    addAndMakeVisible (cueOut);
    cueOut.onChange = [this]
    {
        const int id = cueOut.getSelectedId();
        engine.cueOutChannel = id <= 1 ? -1 : (id - 2) * 2;
        props.setValue ("cueOut", engine.cueOutChannel.load());
    };

    addAndMakeVisible (bpmRange);
    for (int i = 0; i < 4; ++i)
        bpmRange.addItem (juce::String ((int) bpmMins[i]) + " - " + juce::String ((int) (bpmMins[i] * 2.0 - 1.0)), i + 1);
    const double currentMin = engine.getLoader().analysisSettings.minBpm;
    for (int i = 0; i < 4; ++i) if (std::abs (bpmMins[i] - currentMin) < 0.5) bpmRange.setSelectedId (i + 1, juce::dontSendNotification);
    bpmRange.onChange = [this]
    {
        const double mn = bpmMins[juce::jlimit (0, 3, bpmRange.getSelectedId() - 1)];
        engine.getLoader().analysisSettings.minBpm = mn;
        engine.getLoader().analysisSettings.maxBpm = mn * 2.0 - 1.0;
        props.setValue ("bpmMin", mn);
    };
    bpmRange.setTooltip ("Detected tempos are folded into this range. Applies to newly analysed tracks");

    addAndMakeVisible (pitchDirection);
    pitchDirection.addItem ("Pull down to speed up (turntable style)", 1);
    pitchDirection.addItem ("Push up to speed up", 2);
    pitchDirection.setSelectedId (deckSettings.pitchUpIsFaster ? 2 : 1, juce::dontSendNotification);
    pitchDirection.onChange = [this]
    {
        deckSettings.pitchUpIsFaster = pitchDirection.getSelectedId() == 2;
        props.setValue ("pitchUpIsFaster", deckSettings.pitchUpIsFaster);
        if (deckSettings.onChanged) deckSettings.onChanged();
    };

    addAndMakeVisible (cacheLimit);
    for (int i = 0; i < 5; ++i) cacheLimit.addItem ("Keep up to " + juce::String (cacheGbs[i]) + " GB", i + 1);
    const auto limitGb = engine.getLoader().getCacheLimitBytes() / ((juce::int64) 1024 * 1024 * 1024);
    for (int i = 0; i < 5; ++i) if (cacheGbs[i] == (int) limitGb) cacheLimit.setSelectedId (i + 1, juce::dontSendNotification);
    cacheLimit.onChange = [this]
    {
        const int gb = cacheGbs[juce::jlimit (0, 4, cacheLimit.getSelectedId() - 1)];
        engine.getLoader().setCacheLimitBytes ((juce::int64) gb * 1024 * 1024 * 1024);
        engine.getLoader().trimCache();
        props.setValue ("cacheLimitGb", gb);
        refreshCacheLabel();
    };
    cacheLimit.setTooltip ("Decoded tracks are kept on disk so they load instantly next time. "
                           "Beat grids and cue points are always kept.");

    addAndMakeVisible (clearCache);
    clearCache.onClick = [this] { engine.getLoader().clearCache(); refreshCacheLabel(); };

    addAndMakeVisible (keylockDefault);
    keylockDefault.setToggleState (props.getBoolValue ("keylockDefault", false), juce::dontSendNotification);
    keylockDefault.onClick = [this] { props.setValue ("keylockDefault", keylockDefault.getToggleState()); };

    refreshRouting();
    refreshCacheLabel();
    setSize (660, 880);
}

SettingsComponent::~SettingsComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
}

void SettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshRouting();
    // remember the device setup
    if (auto xml = engine.getDeviceManager().createStateXml())
        props.setValue ("audioDevice", xml.get());
}

void SettingsComponent::refreshRouting()
{
    const int outs = engine.getNumOutputChannels();
    const int pairs = juce::jmax (1, outs / 2);

    masterOut.clear (juce::dontSendNotification);
    cueOut.clear (juce::dontSendNotification);
    cueOut.addItem ("None (use split cue for one stereo output)", 1);
    for (int i = 0; i < pairs; ++i)
    {
        const auto name = "Outputs " + juce::String (i * 2 + 1) + " + " + juce::String (i * 2 + 2);
        masterOut.addItem (name, i + 1);
        cueOut.addItem (name, i + 2);
    }
    masterOut.setSelectedId (engine.masterOutChannel.load() / 2 + 1, juce::dontSendNotification);
    const int co = engine.cueOutChannel.load();
    cueOut.setSelectedId (co < 0 ? 1 : co / 2 + 2, juce::dontSendNotification);
}

void SettingsComponent::refreshCacheLabel()
{
    const double gb = (double) engine.getLoader().getCacheSizeBytes() / (1024.0 * 1024.0 * 1024.0);
    cacheInfo.setText (juce::String (gb, 2) + " GB in use", juce::dontSendNotification);
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto bottom = r.removeFromBottom (7 * 34 + 34 + 40);
    deviceSelector.setBounds (r);

    auto row = [&] (juce::Label& l, juce::Component& c)
    {
        auto a = bottom.removeFromTop (28);
        bottom.removeFromTop (6);
        l.setBounds (a.removeFromLeft (180));
        a.removeFromLeft (10);
        c.setBounds (a.removeFromLeft (juce::jmin (360, a.getWidth())));
    };
    row (masterLabel, masterOut);
    row (cueLabel, cueOut);
    row (bpmLabel, bpmRange);
    row (pitchLabel, pitchDirection);
    row (fpsLabel, fps);
    row (stretchLabel, stretchAlgo);
    stretchInfo.setBounds (bottom.removeFromTop (38).withTrimmedLeft (190));
    bottom.removeFromTop (2);
    {
        auto a = bottom.removeFromTop (28);
        bottom.removeFromTop (6);
        cacheLabel.setBounds (a.removeFromLeft (180));
        a.removeFromLeft (10);
        cacheLimit.setBounds (a.removeFromLeft (170));
        a.removeFromLeft (6);
        clearCache.setBounds (a.removeFromLeft (100));
        a.removeFromLeft (6);
        cacheInfo.setBounds (a);
    }
    keylockDefault.setBounds (bottom.removeFromTop (28).withTrimmedLeft (190));
}

} // namespace loopdeck
