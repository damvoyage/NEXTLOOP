#include "MainComponent.h"
#include "SettingsComponent.h"

namespace loopdeck
{

MainComponent::MainComponent (AudioEngine& e, juce::PropertiesFile& p)
    : engine (e), props (p), masterSection (e), browser (e, p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    loadDeckSettings();
    deckSettings.onChanged = [this]
    {
        saveDeckSettings();
        for (auto& d : decks) d->refresh();
    };

    auto loadFn = [this] (int d, const juce::File& f) { loadFile (d, f); };

    for (int i = 0; i < 4; ++i)
    {
        rowWaves[(size_t) i] = std::make_unique<ScrollingWaveform> (engine, i, loadFn);
        addChildComponent (*rowWaves[(size_t) i]);
    }
    for (int i = 0; i < maxDecks; ++i)
    {
        decks[(size_t) i] = std::make_unique<DeckPanel> (engine, i, deckSettings, loadFn);
        addChildComponent (*decks[(size_t) i]);
        channels[(size_t) i] = std::make_unique<ChannelComponent> (engine, i);
        addChildComponent (*channels[(size_t) i]);
    }
    addAndMakeVisible (masterSection);

    addAndMakeVisible (browser);
    browser.loadToNextDeck = [this] (const juce::File& f) { loadToNextDeck (f); };
    browser.loadToDeck = loadFn;

    addAndMakeVisible (divider);
    divider.onDrag = [this] (int dy)
    {
        browserHeight = juce::jlimit (90, juce::jmax (100, getHeight() - 360), browserHeight - dy);
        resized();
    };

    addAndMakeVisible (deckCountLabel);
    deckCountLabel.setColour (juce::Label::textColourId, Theme::textDim);
    deckCountLabel.setFont (Theme::label (13.0f));
    deckCountLabel.setJustificationType (juce::Justification::centredRight);
    for (int i = 0; i < 7; ++i)
    {
        const int n = i + 2;
        deckCountButtons[(size_t) i] = std::make_unique<juce::TextButton> (juce::String (n));
        auto& b = *deckCountButtons[(size_t) i];
        addAndMakeVisible (b);
        b.setRadioGroupId (1001);
        b.setTooltip (juce::String (n) + " decks");
        b.onClick = [this, n] { setNumDecks (n); };
    }

    addAndMakeVisible (masterLabel);
    masterLabel.setFont (Theme::numbers (15.0f));
    masterLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (cpuLabel);
    cpuLabel.setFont (Theme::label (12.0f));
    cpuLabel.setColour (juce::Label::textColourId, Theme::textDim);
    cpuLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (settingsButton);
    settingsButton.onClick = [this] { showSettings(); };

    addAndMakeVisible (themeButton);
    themeButton.setButtonText (Theme::day ? "Night" : "Day");
    themeButton.setTooltip (Theme::day ? "Switch to the dark night look" : "Switch to the white day look");
    themeButton.onClick = [this]
    {
        props.setValue ("dayMode", ! Theme::day);
        Theme::setDay (! Theme::day);
        rebuildLook();
    };

    addAndMakeVisible (monoButton);
    monoButton.setButtonText (Theme::mono ? "Colour" : "Mono");
    monoButton.setTooltip (Theme::mono ? "Bring back the deck colours" : "Clinical look: greys only, no colour");
    monoButton.onClick = [this]
    {
        props.setValue ("monoMode", ! Theme::mono);
        Theme::set (Theme::day, ! Theme::mono);
        rebuildLook();
    };

    addAndMakeVisible (skinButton);
    skinButton.setButtonText (Theme::tech ? "Flat" : "Tech");
    skinButton.setTooltip (Theme::tech ? "Switch to the quiet flat look" : "Switch to the 2000s hardware look: brushed metal, glossy keys, glowing LEDs");
    skinButton.onClick = [this]
    {
        props.setValue ("techSkin", ! Theme::tech);
        Theme::set (Theme::day, Theme::mono, ! Theme::tech);
        rebuildLook();
    };

    addAndMakeVisible (viewLabel);
    viewLabel.setFont (Theme::label (11.0f, true));
    viewLabel.setColour (juce::Label::textColourId, Theme::textDim);
    viewLabel.setJustificationType (juce::Justification::centredRight);
    for (int i = 0; i < numSections; ++i)
    {
        const auto sec = (Section) i;
        viewButtons[(size_t) i] = std::make_unique<juce::TextButton> (sectionName (sec));
        auto& b = *viewButtons[(size_t) i];
        addAndMakeVisible (b);
        b.setTooltip (juce::String (sectionTip (sec)) + ". Hidden parts give their space to the waveforms");
        b.getProperties().set ("fontScale", 1.05);
        b.onClick = [this, sec]
        {
            deckSettings.view.toggle (sec);
            saveViewOptions();
            updateViewButtons();
            resized();
            repaint();
        };
    }
    loadViewOptions();
    loadEffects();

    browserHeight = props.getIntValue ("browserHeight", 230);
    {
        const int algo = props.getIntValue ("stretchAlgorithm", 0);
        for (int d = 0; d < maxDecks; ++d)
            if (engine.getDeck (d).stretchAlgorithm.load() == 0 && algo != 0)
                engine.getDeck (d).setStretchAlgorithm (algo);
    }
    setNumDecks (juce::jlimit (2, 8, props.getIntValue ("numDecks", 4)));

    currentFps = juce::jlimit (15, 60, props.getIntValue ("uiFps", 60));
    startTimerHz (currentFps);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveState();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::saveState()
{
    props.setValue ("numDecks", engine.getNumDecks());
    props.setValue ("browserHeight", browserHeight);
    saveDeckSettings();
    saveViewOptions();
    saveEffects();
    props.saveIfNeeded();
}

void MainComponent::rebuildLook()
{
    saveState();
    // rebuild after this click has finished being handled
    juce::MessageManager::callAsync ([cb = onThemeChanged] { if (cb) cb(); });
}

void MainComponent::loadViewOptions()
{
    auto& v = deckSettings.view;
    for (int i = 0; i < numSections; ++i)
    {
        const juce::String k (sectionKey ((Section) i));
        v.classic[(size_t) i] = props.getBoolValue ("view.classic." + k, v.classic[(size_t) i]);
        v.strips[(size_t) i]  = props.getBoolValue ("view.strips." + k, v.strips[(size_t) i]);
    }
    // a one-off override (used for automated screenshots)
    if (props.containsKey ("viewOverride"))
    {
        const auto on = juce::StringArray::fromTokens (props.getValue ("viewOverride"), ",", "");
        for (int i = 0; i < numSections; ++i)
            v.classic[(size_t) i] = v.strips[(size_t) i] = on.contains (sectionKey ((Section) i));
        props.removeValue ("viewOverride");
    }
}

void MainComponent::saveViewOptions()
{
    auto& v = deckSettings.view;
    for (int i = 0; i < numSections; ++i)
    {
        const juce::String k (sectionKey ((Section) i));
        props.setValue ("view.classic." + k, v.classic[(size_t) i]);
        props.setValue ("view.strips." + k, v.strips[(size_t) i]);
    }
}

void MainComponent::updateViewButtons()
{
    for (int i = 0; i < numSections; ++i)
        viewButtons[(size_t) i]->setToggleState (deckSettings.view.shown ((Section) i), juce::dontSendNotification);
}

void MainComponent::loadEffects()
{
    // each deck remembers which effects sit in its slots
    static const int defaults[fxSlotsPerDeck] = { (int) FxType::Echo, (int) FxType::Filter, (int) FxType::Reverb };
    for (int d = 0; d < maxDecks; ++d)
        for (int s = 0; s < fxSlotsPerDeck; ++s)
        {
            auto& sl = engine.getDeckFx (d).slots[(size_t) s];
            const auto k = "fx." + juce::String (d) + "." + juce::String (s);
            sl.type = juce::jlimit (0, numFxTypes - 1, props.getIntValue (k + ".type", defaults[s]));
            sl.amount = (float) props.getDoubleValue (k + ".amount", 0.5);
            sl.beats = (float) props.getDoubleValue (k + ".beats", s == 0 ? 0.75 : 1.0);
        }
}

void MainComponent::saveEffects()
{
    for (int d = 0; d < maxDecks; ++d)
        for (int s = 0; s < fxSlotsPerDeck; ++s)
        {
            auto& sl = engine.getDeckFx (d).slots[(size_t) s];
            const auto k = "fx." + juce::String (d) + "." + juce::String (s);
            props.setValue (k + ".type", sl.type.load());
            props.setValue (k + ".amount", (double) sl.amount.load());
            props.setValue (k + ".beats", (double) sl.beats.load());
        }
}

void MainComponent::loadDeckSettings()
{
    deckSettings.pitchUpIsFaster = props.getBoolValue ("pitchUpIsFaster", false);
    const auto saved = juce::StringArray::fromTokens (props.getValue ("quickLoops"), "|", "");
    for (int i = 0; i < juce::jmin (8, saved.size()); ++i)
    {
        LoopLength L;
        if (LoopLength::parse (saved[i], LoopLength::Unit::Beats, L))
            deckSettings.quickLoops[(size_t) i] = L;
    }
}

void MainComponent::saveDeckSettings()
{
    juce::StringArray s;
    for (auto& L : deckSettings.quickLoops) s.add (L.toString().toLowerCase());
    props.setValue ("quickLoops", s.joinIntoString ("|"));
}

void MainComponent::setNumDecks (int n)
{
    n = juce::jlimit (2, maxDecks, n);
    engine.setNumDecks (n);
    deckSettings.view.stripLayout = n > 4;
    updateViewButtons();
    for (int i = 0; i < 7; ++i)
        deckCountButtons[(size_t) i]->setToggleState (i + 2 == n, juce::dontSendNotification);
    resized();
    repaint();
}

void MainComponent::loadFile (int deck, const juce::File& file)
{
    if (deck < 0 || deck >= engine.getNumDecks()) return;
    if (! engine.getLoader().isSupportedFile (file))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't load this file",
            file.getFileName() + " isn't a supported audio file. NEXTLOOP plays WAV, AIFF, FLAC, MP3 and Ogg"
           #if JUCE_MAC
            ", plus M4A/AAC and ALAC"
           #endif
            ".");
        return;
    }
    if (engine.getDeck (deck).uiPlaying.load())
    {
        // loading over a playing deck is a common accident; ask first
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Deck " + juce::String (deck + 1) + " is playing",
                                            "Load " + file.getFileNameWithoutExtension() + " anyway?", "Load", "Keep playing", nullptr,
                                            juce::ModalCallbackFunction::create ([this, deck, file] (int r)
                                            {
                                                if (r == 1)
                                                {
                                                    engine.getDeck (deck).keyLock = props.getBoolValue ("keylockDefault", false);
                                                    engine.loadTrack (deck, file);
                                                }
                                            }));
        return;
    }
    engine.getDeck (deck).keyLock = props.getBoolValue ("keylockDefault", false);
    engine.loadTrack (deck, file);
}

void MainComponent::loadToNextDeck (const juce::File& file)
{
    const int n = engine.getNumDecks();
    for (int i = 0; i < n; ++i)
        if (engine.getTrack (i) == nullptr) { loadFile (i, file); return; }
    for (int i = 0; i < n; ++i)
        if (! engine.getDeck (i).uiPlaying.load()) { loadFile (i, file); return; }
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Every deck is playing",
                                            "Drag the track onto a deck, or right-click it and choose a deck.");
}

void MainComponent::showSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsComponent (engine, props, deckSettings));
    o.dialogTitle = "Audio and settings";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // number keys 1-8 toggle play on that deck
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '8' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        const int d = c - '1';
        if (d < engine.getNumDecks()) decks[(size_t) d]->togglePlay();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    ++frameCounter;
    const int n = engine.getNumDecks();
    const bool strips = n > 4;

    for (int i = 0; i < 4; ++i)
        if (! strips && i < n && rowWaves[(size_t) i]->isVisible()) rowWaves[(size_t) i]->refresh();
    for (int i = 0; i < n; ++i)
        decks[(size_t) i]->repaintWaveforms();

    // labels and meters update ~20 times a second whatever the frame rate
    if (frameCounter % juce::jmax (1, currentFps / 20) == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            decks[(size_t) i]->refresh();
            channels[(size_t) i]->refresh();
        }
        masterSection.refresh();

        const int md = engine.uiMasterDeck.load();
        const auto bpm = juce::String (engine.uiMasterBpm.load(), 2);
        masterLabel.setText (md >= 0 ? "Master deck " + juce::String (md + 1) + ":  " + bpm + " BPM"
                                     : "Clock:  " + bpm + " BPM", juce::dontSendNotification);
        masterLabel.setColour (juce::Label::textColourId, md >= 0 ? Theme::deck (md) : Theme::textDim);
    }

    if (frameCounter % 30 == 0)
    {
        auto* dev = engine.getDeviceManager().getCurrentAudioDevice();
        if (dev == nullptr)
            cpuLabel.setText ("No audio output. Open settings to choose one", juce::dontSendNotification);
        else
        {
            const double latencyMs = 1000.0 * (dev->getOutputLatencyInSamples() + dev->getCurrentBufferSizeSamples())
                                     / juce::jmax (1.0, dev->getCurrentSampleRate());
            cpuLabel.setText (dev->getName() + "   " + juce::String ((int) dev->getCurrentSampleRate()) + " Hz   "
                              + juce::String (latencyMs, 1) + " ms   CPU " + juce::String ((int) (engine.getCpuLoad() * 100.0f)) + "%",
                              juce::dontSendNotification);
        }
    }

    if (frameCounter % 60 == 0)
    {
        engine.housekeeping();
        const int fps = props.getIntValue ("uiFps", 60);
        if (fps != currentFps) { currentFps = juce::jlimit (15, 60, fps); startTimerHz (currentFps); }
    }
}

// ---------------------------------------------------------------------------
namespace
{
    constexpr int topBarH = 40, viewBarH = 28;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::base);
    auto bar = getLocalBounds().removeFromTop (topBarH + viewBarH);

    if (Theme::tech)
    {
        // machined top bar with a lit logo
        auto b = bar.toFloat();
        g.setGradientFill (juce::ColourGradient (Theme::panelTop.brighter (0.05f), 0.0f, b.getY(), Theme::panelBottom.darker (0.2f), 0.0f, b.getBottom(), false));
        g.fillRect (b);
        g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.6f : 0.08f));
        g.drawHorizontalLine (1, 0.0f, b.getRight());
        g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.3f : 0.8f));
        g.drawHorizontalLine (topBarH, 8.0f, b.getRight() - 8.0f);
        g.drawHorizontalLine ((int) b.getBottom() - 1, 0.0f, b.getRight());
        g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.5f : 0.05f));
        g.drawHorizontalLine (topBarH + 1, 8.0f, b.getRight() - 8.0f);
    }

    auto logo = juce::Rectangle<int> (12, 0, 140, topBarH);
    g.setFont (Theme::numbers (21.0f));
    const int nextW = 58;
    g.setColour (Theme::text);
    g.drawText ("NEXT", logo.withWidth (nextW), juce::Justification::centredLeft);
    const auto accent = Theme::tech ? Theme::active : Theme::text;
    if (Theme::tech)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.drawText ("LOOP", logo.withTrimmedLeft (nextW).translated (0, 1), juce::Justification::centredLeft);
    }
    g.setColour (accent);
    g.drawText ("LOOP", logo.withTrimmedLeft (nextW), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topBarH).reduced (10, 6);
    top.removeFromLeft (140);
    settingsButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    themeButton.setBounds (top.removeFromRight (60));
    top.removeFromRight (4);
    monoButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (4);
    skinButton.setBounds (top.removeFromRight (56));
    top.removeFromRight (10);
    deckCountLabel.setBounds (top.removeFromLeft (50));
    top.removeFromLeft (6);
    for (auto& b : deckCountButtons) { b->setBounds (top.removeFromLeft (28)); top.removeFromLeft (3); }
    top.removeFromLeft (10);
    masterLabel.setBounds (top);

    // the "show" row: every section can be switched off
    auto vb = r.removeFromTop (viewBarH).reduced (10, 3);
    cpuLabel.setBounds (vb.removeFromRight (juce::jmin (430, vb.getWidth() / 3)));
    viewLabel.setBounds (vb.removeFromLeft (42));
    vb.removeFromLeft (6);
    const int bw = juce::jlimit (52, 80, (vb.getWidth() - 3 * numSections) / numSections);
    for (auto& b : viewButtons) { b->setBounds (vb.removeFromLeft (bw)); vb.removeFromLeft (3); }

    r.removeFromTop (6);
    r.reduce (8, 0);
    const auto& view = deckSettings.view;

    const bool showBrowser = view.shown (Section::Browser);
    browser.setVisible (showBrowser);
    divider.setVisible (showBrowser);
    if (showBrowser)
    {
        auto lib = r.removeFromBottom (juce::jlimit (90, juce::jmax (100, getHeight() - 360), browserHeight));
        browser.setBounds (lib.withTrimmedBottom (8));
        divider.setBounds (r.removeFromBottom (10));
    }
    else
        r.removeFromBottom (8);

    const int n = engine.getNumDecks();
    const bool mixer = view.shown (Section::Mixer);
    for (int i = 0; i < maxDecks; ++i)
    {
        decks[(size_t) i]->setVisible (i < n);
        channels[(size_t) i]->setVisible (i < n && mixer);
    }
    masterSection.setVisible (mixer);

    if (n <= 4) layoutClassic (r, n);
    else layoutStrips (r, n);
}

void MainComponent::layoutClassic (juce::Rectangle<int> area, int n)
{
    const auto& view = deckSettings.view;
    const bool mixer = view.shown (Section::Mixer);
    const bool waves = view.shown (Section::Waves);

    for (int i = 0; i < maxDecks; ++i) decks[(size_t) i]->setStripMode (false);

    const int chW = 82;
    const int mixW = mixer ? n * (chW + 4) + 150 : 0;
    const int colW = mixer ? (area.getWidth() - mixW - 12) / 2 : (area.getWidth() - 6) / 2;
    const int stacked = n <= 2 ? 1 : 2;

    // decks take the height their visible sections need; the waveforms get the rest
    int deckH = decks[0]->preferredWideHeight (colW);
    int deckArea = stacked * deckH + (stacked - 1) * 5;
    if (mixer) deckArea = juce::jmax (deckArea, 330);

    int waveArea = 0;
    if (waves)
    {
        waveArea = area.getHeight() - deckArea - 5;
        const int minWave = n * 44;
        const int maxWave = n * 320;
        if (waveArea < minWave) waveArea = juce::jmin (minWave, area.getHeight() / 2);
        if (waveArea > maxWave) waveArea = maxWave;
    }
    for (int i = 0; i < 4; ++i)
        rowWaves[(size_t) i]->setVisible (waves && i < n);

    if (waves)
    {
        auto wa = area.removeFromTop (waveArea);
        const int each = wa.getHeight() / n;
        for (int i = 0; i < n; ++i)
            rowWaves[(size_t) i]->setBounds (wa.removeFromTop (each).withTrimmedBottom (3));
        area.removeFromTop (5);
    }

    juce::Rectangle<int> leftCol, rightCol;
    if (mixer)
    {
        auto mixArea = area.withSizeKeepingCentre (mixW, area.getHeight());
        leftCol = area.withRight (mixArea.getX() - 6);
        rightCol = area.withLeft (mixArea.getRight() + 6);

        auto m = mixArea;
        masterSection.setBounds (m.removeFromRight (146));
        m.removeFromRight (4);
        // club-mixer channel order: 3 1 2 4
        std::vector<int> chOrder;
        if (n == 2) chOrder = { 0, 1 };
        else if (n == 3) chOrder = { 2, 0, 1 };
        else chOrder = { 2, 0, 1, 3 };
        for (int idx : chOrder)
        {
            channels[(size_t) idx]->setBounds (m.removeFromLeft (chW));
            m.removeFromLeft (4);
        }
    }
    else
    {
        leftCol = area.removeFromLeft ((area.getWidth() - 6) / 2);
        area.removeFromLeft (6);
        rightCol = area;
    }

    if (n <= 2)
    {
        decks[0]->setBounds (leftCol);
        if (n == 2) decks[1]->setBounds (rightCol);
    }
    else
    {
        auto l = leftCol, rc = rightCol;
        const int half = (l.getHeight() - 5) / 2;
        decks[0]->setBounds (l.removeFromTop (half));
        l.removeFromTop (5);
        decks[2]->setBounds (l);
        decks[1]->setBounds (rc.removeFromTop (half));
        rc.removeFromTop (5);
        if (n == 4) decks[3]->setBounds (rc);
    }
}

void MainComponent::layoutStrips (juce::Rectangle<int> area, int n)
{
    const bool mixer = deckSettings.view.shown (Section::Mixer);
    for (int i = 0; i < 4; ++i) rowWaves[(size_t) i]->setVisible (false);

    if (mixer)
    {
        masterSection.setBounds (area.removeFromRight (150).withTrimmedTop (area.getHeight() - juce::jmin (area.getHeight(), 300)));
        area.removeFromRight (5);
    }

    const int gap = 5;
    const int stripW = (area.getWidth() - gap * (n - 1)) / n;
    const int channelH = juce::jlimit (140, 190, area.getHeight() / 5);
    for (int i = 0; i < n; ++i)
    {
        auto s = area.removeFromLeft (stripW);
        area.removeFromLeft (gap);
        decks[(size_t) i]->setStripMode (true);
        if (mixer)
        {
            channels[(size_t) i]->setBounds (s.removeFromBottom (channelH));
            s.removeFromBottom (4);
        }
        decks[(size_t) i]->setBounds (s);
    }
}


} // namespace loopdeck
