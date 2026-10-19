#include "DeckPanel.h"

namespace loopdeck
{

static juce::String formatTime (double seconds, bool negative)
{
    seconds = juce::jmax (0.0, seconds);
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return (negative ? "-" : "") + juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
}

/** Lays components across a row with equal widths. */
static void spread (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int gap = 3)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    int i = 0;
    for (auto* c : comps)
    {
        const int x = r.getX() + i * (w + gap);
        c->setBounds (i == n - 1 ? juce::Rectangle<int> (x, r.getY(), r.getRight() - x, r.getHeight())
                                 : juce::Rectangle<int> (x, r.getY(), w, r.getHeight()));
        ++i;
    }
}

static bool camelotCompatible (const juce::String& a, const juce::String& b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    const int na = a.getIntValue(), nb = b.getIntValue();
    const auto la = a.getLastCharacter(), lb = b.getLastCharacter();
    if (na == nb) return true;
    if (la != lb) return false;
    const int diff = std::abs (na - nb);
    return diff == 1 || diff == 11;
}

// ===========================================================================
DeckPanel::DeckPanel (AudioEngine& e, int d, DeckSettings& s, LoadFileFn fn)
    : engine (e), deck (d), settings (s), loadFile (fn),
      hue (Theme::deck (d)),
      overview (e, d, fn), stripWave (e, d, fn)
{
    addAndMakeVisible (overview);
    addChildComponent (stripWave);
    stripWave.setVertical (true);
    stripWave.setSecondsVisible (6.0);

    for (auto* l : { &title, &artist, &timeLabel, &bpmLabel, &pitchLabel, &keyLabel, &loopLabel })
    {
        addAndMakeVisible (*l);
        l->setInterceptsMouseClicks (false, false);
        l->setMinimumHorizontalScale (0.6f);
    }
    title.setFont (Theme::label (15.0f, true));
    artist.setFont (Theme::label (12.0f));
    artist.setColour (juce::Label::textColourId, Theme::textDim);
    timeLabel.setFont (Theme::numbers (17.0f));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setFont (Theme::numbers (24.0f));
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setInterceptsMouseClicks (true, false);
    bpmLabel.setTooltip ("Double-click to type the BPM");
    bpmLabel.addMouseListener (this, false);
    pitchLabel.setFont (Theme::label (12.0f, true));
    pitchLabel.setJustificationType (juce::Justification::centredRight);
    pitchLabel.setColour (juce::Label::textColourId, Theme::textDim);
    keyLabel.setFont (Theme::label (15.0f, true));
    keyLabel.setJustificationType (juce::Justification::centred);
    loopLabel.setFont (Theme::numbers (14.0f));
    loopLabel.setJustificationType (juce::Justification::centred);
    loopLabel.setInterceptsMouseClicks (true, false);
    loopLabel.setTooltip ("Click to type a loop length, e.g. 5 bars, 13, 3 beats, 1/4");
    loopLabel.addMouseListener (this, false);
    loopLabel.setColour (juce::Label::backgroundColourId, Theme::base);

    // --- transport ------------------------------------------------------------
    setupButton (play, "Play", "Play / pause", true);
    play.onClick = [this] { togglePlay(); };
    setupButton (cue, "Cue", "Hold to preview from the cue point; while paused sets a new cue");
    cue.onPress = [this] (const juce::ModifierKeys&) { engine.post (DeckCommand::CueDown, deck); };
    cue.onRelease = [this] { engine.post (DeckCommand::CueUp, deck); };

    setupButton (sync, "Sync", "Lock tempo and beat phase to the master");
    sync.onClick = [this] { engine.post (engine.getDeck (deck).syncEnabled.load() ? DeckCommand::SyncOff : DeckCommand::SyncOn, deck); };
    setupButton (master, "Master", "Make this deck the tempo master");
    master.onClick = [this] { engine.pinnedMaster = engine.pinnedMaster.load() == deck ? -1 : deck; };
    setupButton (keyLock, "Key lock", "Off: varispeed, pitch moves with tempo like vinyl. "
                                      "On: time-stretch keeps the key. Right-click to choose the stretch sound");
    keyLock.onClick = [this] { auto& dk = engine.getDeck (deck); dk.keyLock = ! dk.keyLock.load(); };
    keyLock.onRightClick = [this] { showKeyLockMenu(); };

    setupButton (keyDown, "Key-", "Transpose down a semitone. Right-click to reset");
    keyDown.onClick = [this] { shiftKey (-1); };
    keyDown.onRightClick = [this] { shiftKey (0); };
    setupButton (keyUp, "Key+", "Transpose up a semitone. Right-click to reset");
    keyUp.onClick = [this] { shiftKey (1); };
    keyUp.onRightClick = [this] { shiftKey (0); };
    for (auto* b : std::initializer_list<juce::Button*> { &keyDown, &keyUp, &keyLock }) b->getProperties().set ("fontScale", 0.78);
    setupButton (quant, "Quant", "Snap cues, loops and jumps to the beat grid");
    quant.onClick = [this] { auto& dk = engine.getDeck (deck); dk.quantize = ! dk.quantize.load(); };
    setupButton (slip, "Slip", "Loops and scratches return to where the track would have been");
    slip.onClick = [this] { auto& dk = engine.getDeck (deck); dk.slipMode = ! dk.slipMode.load(); };
    setupButton (eject, "Eject", "Unload this deck");
    eject.onClick = [this] { engine.ejectTrack (deck); };

    // --- hot cues --------------------------------------------------------------
    for (int i = 0; i < numHotCues; ++i)
    {
        hot[(size_t) i] = std::make_unique<PadButton> (juce::String (i + 1));
        auto& b = *hot[(size_t) i];
        setupButton (b, juce::String (i + 1), "Hot cue " + juce::String (i + 1) + ": click to set or jump. Right-click or shift-click to delete", true);
        b.onPress = [this, i] (const juce::ModifierKeys& mods)
        {
            if (mods.isShiftDown()) engine.post (DeckCommand::HotCueDelete, deck, 0.0, i);
            else engine.post (DeckCommand::HotCueDown, deck, 0.0, i);
        };
        b.onRelease = [this, i] { engine.post (DeckCommand::HotCueUp, deck, 0.0, i); };
        b.onRightClick = [this, i] { engine.post (DeckCommand::HotCueDelete, deck, 0.0, i); };
    }

    // --- loops -----------------------------------------------------------------
    setupButton (loopPrev, "<", "Shorter loop");
    loopPrev.onClick = [this] { setCurrentLength (length.previous(), true); };
    setupButton (loopNext, ">", "Longer loop");
    loopNext.onClick = [this] { setCurrentLength (length.next(), true); };
    setupButton (unitToggle, "Bars", "Count loop lengths in bars or beats");
    unitToggle.onClick = [this]
    {
        const int bpb = beatsPerBar();
        if (length.unit == LoopLength::Unit::Beats)
            setCurrentLength (LoopLength::bars (juce::jmax (1.0, std::round (length.value / bpb))), false);
        else
            setCurrentLength (LoopLength::beats (juce::jmin (16.0, length.value * bpb)), false);
    };
    setupButton (loopBtn, "Loop", "Loop the current length from here", true);
    loopBtn.onClick = [this] { loopToggle(); };
    setupButton (halve, "1/2", "Halve the loop");
    halve.onClick = [this]
    {
        engine.post (DeckCommand::LoopHalve, deck);
        if (length.unit == LoopLength::Unit::Bars && length.value <= 1.0) length = LoopLength::beats (beatsPerBar() * 0.5);
        else length.value *= 0.5;
        updateLoopLabel();
    };
    setupButton (dbl, "x2", "Double the loop");
    dbl.onClick = [this] { engine.post (DeckCommand::LoopDouble, deck); length.value *= 2.0; updateLoopLabel(); };
    setupButton (loopIn, "In", "Set loop start here");
    loopIn.onClick = [this] { engine.post (DeckCommand::LoopIn, deck); };
    setupButton (loopOut, "Out", "Set loop end here and start looping");
    loopOut.onClick = [this] { engine.post (DeckCommand::LoopOut, deck); };
    setupButton (reloop, "Reloop", "Exit the loop, or jump back into the last one", true);
    reloop.onClick = [this]
    {
        engine.post (engine.getDeck (deck).uiLoopActive.load() ? DeckCommand::LoopExit : DeckCommand::Reloop, deck);
    };
    setupButton (rollMode, "Roll", "Quick loop buttons become momentary loop rolls (slip)");
    rollMode.setClickingTogglesState (true);

    for (int i = 0; i < 8; ++i)
    {
        quick[(size_t) i] = std::make_unique<PadButton> ("");
        auto& b = *quick[(size_t) i];
        setupButton (b, "", "Loop this length. Right-click to change it", true);
        b.getProperties().set ("fontScale", 0.85);
        b.onPress = [this, i] (const juce::ModifierKeys&)
        {
            const auto L = settings.quickLoops[(size_t) i];
            const double beats = L.toBeats (beatsPerBar());
            if (rollMode.getToggleState())
            {
                engine.post (DeckCommand::LoopRollDown, deck, beats);
                return;
            }
            auto& dk = engine.getDeck (deck);
            const bool same = dk.uiLoopActive.load() && std::abs (dk.uiLoopBeats.load() - beats) < 1e-6;
            length = L;
            updateLoopLabel();
            engine.post (same ? DeckCommand::LoopExit : DeckCommand::AutoLoop, deck, beats);
        };
        b.onRelease = [this] { if (rollMode.getToggleState()) engine.post (DeckCommand::LoopRollUp, deck); };
        b.onRightClick = [this, i] { editQuickLoop (i); };
    }

    setupButton (moveL, "Move <", "Move the loop back by its length (shift: one beat)");
    moveL.onClick = [this] { engine.post (DeckCommand::LoopMove, deck, juce::ModifierKeys::currentModifiers.isShiftDown() ? -1.0 : -currentBeats()); };
    setupButton (moveR, "Move >", "Move the loop forward by its length (shift: one beat)");
    moveR.onClick = [this] { engine.post (DeckCommand::LoopMove, deck, juce::ModifierKeys::currentModifiers.isShiftDown() ? 1.0 : currentBeats()); };
    setupButton (inL, "In <", "Nudge loop start earlier by the fine step");
    inL.onClick = [this] { engine.post (DeckCommand::LoopAdjustIn, deck, -fineStepFrames()); };
    setupButton (inR, "In >", "Nudge loop start later by the fine step");
    inR.onClick = [this] { engine.post (DeckCommand::LoopAdjustIn, deck, fineStepFrames()); };
    setupButton (outL, "Out <", "Nudge loop end earlier by the fine step");
    outL.onClick = [this] { engine.post (DeckCommand::LoopAdjustOut, deck, -fineStepFrames()); };
    setupButton (outR, "Out >", "Nudge loop end later by the fine step");
    outR.onClick = [this] { engine.post (DeckCommand::LoopAdjustOut, deck, fineStepFrames()); };
    setupButton (jumpL, "Jump <", "Jump back by the loop length");
    jumpL.onClick = [this] { engine.post (DeckCommand::BeatJump, deck, -currentBeats()); };
    setupButton (jumpR, "Jump >", "Jump forward by the loop length");
    jumpR.onClick = [this] { engine.post (DeckCommand::BeatJump, deck, currentBeats()); };

    addAndMakeVisible (fineStep);
    fineStep.addItemList ({ "1 sample", "1 ms", "10 ms", "1/64 beat", "1/16 beat" }, 1);
    fineStep.setSelectedId (2, juce::dontSendNotification);
    fineStep.setTooltip ("Fine-adjust step for loop in/out");

    // --- beat grid -----------------------------------------------------------
    setupButton (gridHalf, "/2", "Halve the BPM");
    gridHalf.onClick = [this] { if (auto t = engine.getTrack (deck)) { t->grid.bpm = t->grid.bpm.load() * 0.5; gridChanged(); } };
    setupButton (gridDouble, "x2", "Double the BPM");
    gridDouble.onClick = [this] { if (auto t = engine.getTrack (deck)) { t->grid.bpm = t->grid.bpm.load() * 2.0; gridChanged(); } };
    auto shiftGrid = [this] (double sign)
    {
        if (auto t = engine.getTrack (deck))
        {
            const double ms = juce::ModifierKeys::currentModifiers.isShiftDown() ? 1.0 : 5.0;
            t->grid.anchorFrame = t->grid.anchorFrame.load() + sign * ms * 0.001 * t->sampleRate;
            gridChanged();
        }
    };
    setupButton (gridL, "Grid <", "Shift the beat grid earlier by 5 ms (shift: 1 ms)");
    gridL.onClick = [shiftGrid] { shiftGrid (-1.0); };
    setupButton (gridR, "Grid >", "Shift the beat grid later by 5 ms (shift: 1 ms)");
    gridR.onClick = [shiftGrid] { shiftGrid (1.0); };
    setupButton (gridSet, "Set 1", "Make the current position beat 1 of a bar");
    gridSet.onClick = [this]
    {
        if (auto t = engine.getTrack (deck))
        {
            t->grid.anchorFrame = engine.getDeck (deck).uiPosition.load();
            if (! t->grid.isValid()) t->grid.bpm = 120.0;
            gridChanged();
        }
    };
    setupButton (tap, "Tap", "Tap along to set the BPM");
    tap.onClick = [this] { tapTempo(); };

    // --- pitch -----------------------------------------------------------------
    addAndMakeVisible (pitch);
    pitch.setSliderStyle (juce::Slider::LinearVertical);
    pitch.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitch.setRange (-1.0, 1.0, 0.0);
    pitch.setDoubleClickReturnValue (true, 0.0);
    pitch.getProperties().set ("bipolar", true);
    pitch.setColour (juce::Slider::thumbColourId, hue);
    pitch.setTooltip ("Tempo. Double-click to reset");
    pitch.onValueChange = [this]
    {
        const float v = (float) pitch.getValue();
        engine.getDeck (deck).pitch = pitchIsInverted() ? -v : v;
    };

    addAndMakeVisible (pitchRange);
    pitchRange.addItemList ({ "4%", "8%", "16%", "50%", "0-200%" }, 1);
    pitchRange.setSelectedId (2, juce::dontSendNotification);
    pitchRange.setTooltip ("Tempo fader range. 0-200% runs from a full stop to double speed");
    pitchRange.onChange = [this]
    {
        static const float ranges[] = { 0.04f, 0.08f, 0.16f, 0.5f, 1.0f };
        auto& dk = engine.getDeck (deck);
        const float oldRange = dk.pitchRange.load();
        const float newRange = ranges[juce::jlimit (0, 4, pitchRange.getSelectedItemIndex())];
        // keep the actual tempo when the range changes
        dk.pitch = juce::jlimit (-1.0f, 1.0f, dk.pitch.load() * oldRange / newRange);
        dk.pitchRange = newRange;
    };

    setupButton (nudgeDown, "-", "Hold to slow down briefly (pitch bend)");
    nudgeDown.onPress = [this] (const juce::ModifierKeys& m) { engine.getDeck (deck).bend = m.isShiftDown() ? -0.08f : -0.03f; };
    nudgeDown.onRelease = [this] { engine.getDeck (deck).bend = 0.0f; };
    setupButton (nudgeUp, "+", "Hold to speed up briefly (pitch bend)");
    nudgeUp.onPress = [this] (const juce::ModifierKeys& m) { engine.getDeck (deck).bend = m.isShiftDown() ? 0.08f : 0.03f; };
    nudgeUp.onRelease = [this] { engine.getDeck (deck).bend = 0.0f; };

    updateLoopLabel();
    refresh();
}

DeckPanel::~DeckPanel() = default;

void DeckPanel::setupButton (juce::Button& b, const juce::String& text, const juce::String& tip, bool deckColoured)
{
    addAndMakeVisible (b);
    b.setButtonText (text);
    b.setTooltip (tip);
    // colour only where it identifies the deck's state; everything else stays grey
    b.setColour (juce::TextButton::buttonOnColourId, deckColoured ? hue : Theme::active);
}

void DeckPanel::setStripMode (bool strip)
{
    stripMode = strip;
    stripWave.setVisible (strip);
    quant.setButtonText ("Quant");
    resized();
}

int DeckPanel::beatsPerBar() const
{
    if (auto t = engine.getTrack (deck)) return juce::jmax (1, t->grid.beatsPerBar.load());
    return 4;
}

double DeckPanel::currentBeats() const
{
    return length.toBeats (beatsPerBar());
}

double DeckPanel::fineStepFrames() const
{
    auto t = engine.getTrack (deck);
    const double sr = t != nullptr ? t->sampleRate : 44100.0;
    const double fpb = t != nullptr ? t->grid.framesPerBeat (sr) : sr * 0.5;
    switch (fineStep.getSelectedId())
    {
        case 1: return 1.0;
        case 2: return sr * 0.001;
        case 3: return sr * 0.010;
        case 4: return fpb / 64.0;
        case 5: return fpb / 16.0;
        default: return sr * 0.001;
    }
}

void DeckPanel::setCurrentLength (LoopLength l, bool resizeActiveLoop)
{
    length = l;
    updateLoopLabel();
    if (resizeActiveLoop && engine.getDeck (deck).uiLoopActive.load())
        engine.post (DeckCommand::SetLoopLength, deck, currentBeats());
}

void DeckPanel::updateLoopLabel()
{
    auto& dk = engine.getDeck (deck);
    juce::String text = length.toString();

    if (dk.uiLoopActive.load())
    {
        // show the real loop length (it may have been fine-adjusted off the grid)
        const double beats = dk.uiLoopBeats.load();
        if (std::abs (beats - currentBeats()) > 1e-3)
        {
            const int bpb = beatsPerBar();
            if (length.unit == LoopLength::Unit::Bars && std::abs (beats / bpb - std::round (beats / bpb)) < 1e-3)
                text = LoopLength::bars (std::round (beats / bpb)).toString();
            else
                text = juce::String (beats, 3) + " BEATS";
        }
    }
    loopLabel.setText (text.toLowerCase(), juce::dontSendNotification);
    unitToggle.setButtonText (length.unit == LoopLength::Unit::Bars ? "Bars" : "Beats");

    for (int i = 0; i < 8; ++i)
    {
        const auto& L = settings.quickLoops[(size_t) i];
        juce::String s;
        if (L.unit == LoopLength::Unit::Bars) s = juce::String ((int) L.value) + (stripMode ? "b" : " bar");
        else if (L.value < 1.0) s = "1/" + juce::String ((int) std::round (1.0 / L.value));
        else s = juce::String ((int) L.value) + (stripMode ? "bt" : " bt");
        quick[(size_t) i]->setButtonText (s);
    }
}

void DeckPanel::togglePlay()
{
    engine.post (DeckCommand::TogglePlay, deck);
}

void DeckPanel::loopToggle()
{
    engine.post (DeckCommand::ToggleLoop, deck, currentBeats());
}

void DeckPanel::gridChanged()
{
    if (auto t = engine.getTrack (deck))
    {
        t->analysed = true;
        t->cuesDirty = true;   // grid is saved together with cues
        if (engine.getDeck (deck).uiLoopActive.load())
            engine.post (DeckCommand::SetLoopLength, deck, engine.getDeck (deck).uiLoopBeats.load());
    }
}

void DeckPanel::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    if (taps.size() > 8) taps.remove (0);
    if (taps.size() >= 4)
        if (auto t = engine.getTrack (deck))
        {
            const double interval = (taps.getLast() - taps.getFirst()) / (taps.size() - 1);
            t->grid.bpm = std::round (60000.0 / interval * 10.0) / 10.0;
            gridChanged();
        }
}

void DeckPanel::shiftKey (int semitones)
{
    auto& dk = engine.getDeck (deck);
    dk.keyShift = semitones == 0 ? 0.0f : juce::jlimit (-12.0f, 12.0f, std::round (dk.keyShift.load()) + (float) semitones);
}

void DeckPanel::showKeyLockMenu()
{
    auto& dk = engine.getDeck (deck);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Tempo changes");
    menu.addItem (1, "Varispeed: pitch follows tempo, like vinyl", true, ! dk.keyLock.load());
    menu.addItem (2, "Key lock: keep the key", true, dk.keyLock.load());
    menu.addSectionHeader ("Stretch sound for this deck");
    for (int a = 0; a < numStretchAlgorithms; ++a)
    {
        if (a == (int) StretchAlgorithm::VintageSampler) menu.addSeparator();
        menu.addItem (100 + a, stretchAlgorithmName (a), stretchAlgorithmAvailable (a), dk.stretchAlgorithm.load() == a);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyLock), [this] (int r)
    {
        auto& d = engine.getDeck (deck);
        if (r == 1) d.keyLock = false;
        else if (r == 2) d.keyLock = true;
        else if (r >= 100) d.setStretchAlgorithm (r - 100);
    });
}

void DeckPanel::editBpm()
{
    auto t = engine.getTrack (deck);
    if (t == nullptr) return;
    auto* ed = new juce::TextEditor();
    ed->setText (juce::String (t->grid.bpm.load(), 2));
    ed->setSize (90, 26);
    ed->setJustification (juce::Justification::centred);
    ed->setFont (Theme::numbers (16.0f));
    ed->selectAll();
    auto tp = t;
    ed->onReturnKey = [this, ed, tp]
    {
        const double v = ed->getText().getDoubleValue();
        if (v > 20.0 && v < 400.0) { tp->grid.bpm = v; gridChanged(); }
        if (auto* box = ed->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (ed), bpmLabel.getScreenBounds(), nullptr);
}

void DeckPanel::editLoopLength()
{
    auto* ed = new juce::TextEditor();
    ed->setText (length.toString().toLowerCase());
    ed->setSize (120, 26);
    ed->setJustification (juce::Justification::centred);
    ed->selectAll();
    ed->onReturnKey = [this, ed]
    {
        LoopLength L;
        if (LoopLength::parse (ed->getText(), length.unit, L))
            setCurrentLength (L, true);
        if (auto* box = ed->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (ed), loopLabel.getScreenBounds(), nullptr);
}

void DeckPanel::editQuickLoop (int i)
{
    auto* ed = new juce::TextEditor();
    ed->setText (settings.quickLoops[(size_t) i].toString().toLowerCase());
    ed->setSize (120, 26);
    ed->setJustification (juce::Justification::centred);
    ed->selectAll();
    ed->onReturnKey = [this, ed, i]
    {
        LoopLength L;
        if (LoopLength::parse (ed->getText(), LoopLength::Unit::Bars, L))
        {
            settings.quickLoops[(size_t) i] = L;
            if (settings.onChanged) settings.onChanged();
        }
        if (auto* box = ed->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (ed), quick[(size_t) i]->getScreenBounds(), nullptr);
}

// ---------------------------------------------------------------------------
void DeckPanel::refresh()
{
    auto& dk = engine.getDeck (deck);
    auto t = engine.getTrack (deck);

    if (t != nullptr)
    {
        title.setText (t->title, juce::dontSendNotification);
        juce::String sub = t->artist;
        if (t->decodeFailed.load()) sub = "Could not read this file: " + t->errorMessage;
        else if (! t->decodeComplete.load())
            sub = "Loading " + juce::String ((int) (100.0 * (double) t->framesReady.load() / (double) juce::jmax ((juce::int64) 1, t->totalFrames.load()))) + "%";
        else if (! t->analysed.load()) sub = (sub.isNotEmpty() ? sub + "   " : juce::String()) + "Analysing...";
        else if (sub.isEmpty()) sub = t->formatName.upToFirstOccurrenceOf (" ", false, false) + " " + juce::String (t->sampleRate / 1000.0, 1) + " kHz";
        artist.setText (sub, juce::dontSendNotification);

        const double pos = dk.uiPosition.load();
        const double total = (double) t->totalFrames.load();
        const double remaining = (total - pos) / t->sampleRate;
        timeLabel.setText (formatTime (remaining, true), juce::dontSendNotification);
        timeLabel.setColour (juce::Label::textColourId, remaining < 30.0 && dk.uiPlaying.load() ? Theme::warn : Theme::text);

        const double bpm = dk.uiEffectiveBpm.load();
        bpmLabel.setText (t->grid.isValid() ? juce::String (bpm, 2) : "--.--", juce::dontSendNotification);

        const double pct = (dk.uiTempoRatio.load() - 1.0) * 100.0;
        pitchLabel.setText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", juce::dontSendNotification);

        const auto cam = t->getCamelot();
        const int shift = (int) std::round (dk.keyShift.load());
        juce::String shownKey = cam;
        if (shift != 0 && cam.isNotEmpty())
        {
            // a semitone moves 7 steps round the Camelot wheel
            const int num = ((cam.getIntValue() - 1 + 7 * shift) % 12 + 12) % 12 + 1;
            shownKey = juce::String (num) + cam.getLastCharacter() + (shift > 0 ? " +" : " ") + juce::String (shift);
        }
        keyLabel.setText (shownKey, juce::dontSendNotification);
        bool compatible = false;
        const int m = engine.uiMasterDeck.load();
        if (m >= 0 && m != deck)
            if (auto mt = engine.getTrack (m)) compatible = camelotCompatible (cam, mt->getCamelot());
        keyLabel.setColour (juce::Label::textColourId, compatible ? Theme::meterOk : Theme::text);
        keyLabel.setTooltip (t->getKey().isNotEmpty() ? t->getKey() + (compatible ? " (mixes with the master deck)" : "") : juce::String());

        if (! wasLoaded) { wasLoaded = true; updateLoopLabel(); }
    }
    else
    {
        title.setText ("Deck " + juce::String (deck + 1), juce::dontSendNotification);
        artist.setText ("Empty", juce::dontSendNotification);
        timeLabel.setText ("", juce::dontSendNotification);
        bpmLabel.setText ("", juce::dontSendNotification);
        pitchLabel.setText ("", juce::dontSendNotification);
        keyLabel.setText ("", juce::dontSendNotification);
        wasLoaded = false;
    }

    play.setToggleState (dk.uiPlaying.load(), juce::dontSendNotification);
    play.setButtonText (dk.uiPlaying.load() ? "Pause" : "Play");
    sync.setToggleState (dk.syncEnabled.load(), juce::dontSendNotification);
    master.setToggleState (engine.uiMasterDeck.load() == deck, juce::dontSendNotification);
    master.setButtonText (engine.pinnedMaster.load() == deck ? "Master *" : "Master");
    keyLock.setToggleState (dk.keyLock.load(), juce::dontSendNotification);
    keyLock.setButtonText (dk.keyLock.load() ? "Key lock" : "Varispeed");
    keyDown.setToggleState (dk.keyShift.load() < -0.01f, juce::dontSendNotification);
    keyUp.setToggleState (dk.keyShift.load() > 0.01f, juce::dontSendNotification);
    quant.setToggleState (dk.quantize.load(), juce::dontSendNotification);
    slip.setToggleState (dk.slipMode.load(), juce::dontSendNotification);
    loopBtn.setToggleState (dk.uiLoopActive.load(), juce::dontSendNotification);
    reloop.setButtonText (dk.uiLoopActive.load() ? "Exit" : "Reloop");
    reloop.setToggleState (dk.uiLoopActive.load(), juce::dontSendNotification);

    for (int i = 0; i < numHotCues; ++i)
        hot[(size_t) i]->setToggleState (t != nullptr && t->hotCues[(size_t) i].load() >= 0.0, juce::dontSendNotification);

    const double activeBeats = dk.uiLoopActive.load() ? dk.uiLoopBeats.load() : -1.0;
    for (int i = 0; i < 8; ++i)
        quick[(size_t) i]->setToggleState (std::abs (settings.quickLoops[(size_t) i].toBeats (beatsPerBar()) - activeBeats) < 1e-6,
                                           juce::dontSendNotification);
    updateLoopLabel();

    // pitch fader follows the engine (sync moves it)
    if (! pitch.isMouseButtonDown())
    {
        const float p = dk.pitch.load();
        pitch.setValue (pitchIsInverted() ? -p : p, juce::dontSendNotification);
    }
}

void DeckPanel::repaintWaveforms()
{
    overview.refresh();
    if (stripMode) stripWave.refresh();
}

void DeckPanel::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    // deck badge
    auto badge = juce::Rectangle<float> (8.0f, 8.0f, 26.0f, 26.0f);
    g.setColour (hue);
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (Theme::base);
    g.setFont (Theme::numbers (16.0f));
    g.drawText (juce::String (deck + 1), badge, juce::Justification::centred);
}

void DeckPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    if (stripMode) layoutStrip (r);
    else layoutWide (r);
    updateLoopLabel();
}

void DeckPanel::layoutWide (juce::Rectangle<int> r)
{
    // header
    auto header = r.removeFromTop (30);
    header.removeFromLeft (32);
    auto right = header.removeFromRight (juce::jmin (300, header.getWidth() / 2));
    auto bpmArea = right.removeFromRight (90);
    bpmLabel.setBounds (bpmArea);
    pitchLabel.setBounds (right.removeFromRight (64));
    keyLabel.setBounds (right.removeFromRight (40));
    timeLabel.setBounds (right);
    title.setBounds (header.removeFromTop (17));
    artist.setBounds (header);

    r.removeFromTop (6);
    overview.setBounds (r.removeFromTop (juce::jlimit (22, 40, r.getHeight() / 8)));
    r.removeFromTop (6);

    // pitch column on the right
    pitch.setSliderStyle (juce::Slider::LinearVertical);
    auto pitchCol = r.removeFromRight (62);
    pitchRange.setBounds (pitchCol.removeFromTop (22));
    auto nudges = pitchCol.removeFromBottom (24);
    spread (nudges, { &nudgeDown, &nudgeUp }, 2);
    pitchCol.removeFromBottom (3);
    spread (pitchCol.removeFromBottom (24), { &keyDown, &keyUp }, 2);
    pitch.setBounds (pitchCol.reduced (0, 4));
    r.removeFromRight (6);

    // big transport on the left
    auto transport = r.removeFromLeft (juce::jlimit (60, 90, r.getWidth() / 8));
    const int th = transport.getHeight();
    cue.setBounds (transport.removeFromTop (th / 2 - 2));
    transport.removeFromTop (4);
    play.setBounds (transport);
    r.removeFromLeft (6);

    // rows
    const int rows = 6;
    const int gap = 4;
    const int rh = (r.getHeight() - gap * (rows - 1)) / rows;
    auto row = [&] { auto x = r.removeFromTop (rh); r.removeFromTop (gap); return x; };

    {
        auto a = row();
        spread (a, { hot[0].get(), hot[1].get(), hot[2].get(), hot[3].get(), hot[4].get(), hot[5].get(), hot[6].get(), hot[7].get() });
    }
    {
        auto a = row();
        auto len = a.removeFromLeft (juce::jmin (220, a.getWidth() * 2 / 5));
        loopPrev.setBounds (len.removeFromLeft (24));
        loopNext.setBounds (len.removeFromRight (24));
        loopLabel.setBounds (len.reduced (3, 0));
        a.removeFromLeft (3);
        spread (a, { &unitToggle, &loopBtn, &halve, &dbl, &reloop });
    }
    spread (row(), { quick[0].get(), quick[1].get(), quick[2].get(), quick[3].get(), quick[4].get(), quick[5].get(), quick[6].get(), quick[7].get(), &rollMode });
    {
        auto a = row();
        spread (a, { &loopIn, &loopOut, &moveL, &moveR, &jumpL, &jumpR });
    }
    {
        auto a = row();
        auto combo = a.removeFromRight (juce::jmin (110, a.getWidth() / 5));
        fineStep.setBounds (combo);
        a.removeFromRight (3);
        spread (a, { &inL, &inR, &outL, &outR, &eject });
    }
    {
        auto a = r;
        spread (a, { &sync, &master, &keyLock, &quant, &slip, &gridHalf, &gridDouble, &gridL, &gridR, &gridSet, &tap });
    }
}

void DeckPanel::layoutStrip (juce::Rectangle<int> r)
{
    auto header = r.removeFromTop (44);
    auto top = header.removeFromTop (28);
    top.removeFromLeft (32);
    title.setBounds (top);
    auto line2 = header;
    bpmLabel.setBounds (line2.removeFromLeft (line2.getWidth() / 2).withTrimmedTop (-4));
    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    keyLabel.setBounds (line2.removeFromLeft (line2.getWidth() / 3));
    timeLabel.setBounds (line2);
    artist.setBounds ({});
    pitchLabel.setBounds ({});

    r.removeFromTop (4);
    overview.setBounds (r.removeFromTop (20));
    r.removeFromTop (5);

    auto wave = r.removeFromLeft ((int) (r.getWidth() * 0.36f));
    stripWave.setBounds (wave);
    r.removeFromLeft (5);

    const int gap = 3;
    const int rows = 19;
    const int rh = juce::jlimit (16, 30, (r.getHeight() - gap * (rows - 1)) / rows);
    auto row = [&] { auto x = r.removeFromTop (rh); r.removeFromTop (gap); return x; };

    spread (row(), { &cue, &play });
    spread (row(), { &sync, &master });
    spread (row(), { &keyLock, &quant, &slip });
    spread (row(), { hot[0].get(), hot[1].get(), hot[2].get(), hot[3].get() });
    spread (row(), { hot[4].get(), hot[5].get(), hot[6].get(), hot[7].get() });
    {
        auto a = row();
        loopPrev.setBounds (a.removeFromLeft (20));
        loopNext.setBounds (a.removeFromRight (20));
        loopLabel.setBounds (a.reduced (2, 0));
    }
    spread (row(), { &loopBtn, &halve, &dbl, &unitToggle });
    spread (row(), { quick[0].get(), quick[1].get(), quick[2].get(), quick[3].get() });
    spread (row(), { quick[4].get(), quick[5].get(), quick[6].get(), quick[7].get() });
    spread (row(), { &loopIn, &loopOut, &reloop, &rollMode });
    spread (row(), { &moveL, &moveR });
    spread (row(), { &inL, &inR, &outL, &outR });
    fineStep.setBounds (row());
    spread (row(), { &jumpL, &jumpR });
    {
        auto a = row();
        pitchRange.setBounds (a.removeFromRight (56));
        a.removeFromRight (3);
        spread (a, { &nudgeDown, &nudgeUp });
    }
    spread (row(), { &keyDown, &keyUp });
    spread (row(), { &gridHalf, &gridDouble, &tap });
    spread (row(), { &gridL, &gridR, &gridSet });
    {
        auto a = row();
        eject.setBounds (a.removeFromRight (44));
        a.removeFromRight (3);
        pitch.setSliderStyle (juce::Slider::LinearHorizontal);
        pitch.setBounds (a);
    }

    for (auto* b : std::initializer_list<juce::TextButton*> { &moveL, &moveR, &inL, &inR, &outL, &outR, &jumpL, &jumpR, &gridL, &gridR })
        b->getProperties().set ("fontScale", 0.8);
}

void DeckPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent == &bpmLabel) editBpm();
}

void DeckPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &loopLabel) editLoopLength();
}

// ---------------------------------------------------------------------------
void DeckPanel::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty() && loadFile) loadFile (deck, juce::File (files[0]));
}

void DeckPanel::itemDropped (const SourceDetails& details)
{
    const auto path = details.description.toString();
    if (path.startsWith ("file:") && loadFile)
        loadFile (deck, juce::File (path.fromFirstOccurrenceOf ("file:", false, false)));
}

} // namespace loopdeck
