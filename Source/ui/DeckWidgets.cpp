#include "DeckWidgets.h"

namespace loopdeck
{

static constexpr double revsPerSecond = (100.0 / 3.0) / 60.0;   // 33 1/3 rpm

// ===========================================================================
Platter::Platter (AudioEngine& e, int d) : engine (e), deck (d)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag round to scratch");
}

double Platter::angleFor (double frames, double sampleRate) const
{
    const double revs = frames / juce::jmax (1.0, sampleRate) * revsPerSecond;
    return (revs - std::floor (revs)) * juce::MathConstants<double>::twoPi;
}

void Platter::refresh()
{
    auto t = engine.getTrack (deck);
    auto& d = engine.getDeck (deck);
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (double v) { juce::uint64 bits; std::memcpy (&bits, &v, sizeof bits); h = (h ^ bits) * 1099511628211ull; };
    mix ((double) (juce::pointer_sized_int) t.get());
    mix ((double) Theme::key());
    if (t != nullptr)
    {
        const double ang = angleFor (d.uiPosition.load(), t->sampleRate);
        mix (std::floor (ang * 90.0 / juce::MathConstants<double>::pi));   // two degree steps
        const double total = (double) juce::jmax ((juce::int64) 1, t->totalFrames.load());
        mix (std::floor (d.uiPosition.load() / total * 720.0));
        mix (d.uiPlaying.load() ? 1.0 : 0.0);
        mix (d.uiLoopActive.load() ? d.uiLoopStart.load() + d.uiLoopEnd.load() : -1.0);
        for (auto& c : t->hotCues) mix (c.load());
        mix (engine.uiMasterDeck.load() == deck ? 1.0 : 0.0);
    }
    if (h != lastSignature) { lastSignature = h; repaint(); }
}

void Platter::buildDisc (int) {}

void Platter::paint (juce::Graphics& g)
{
    // Serato-style: no record, just a ring, a progress arc and the spinning marker
    auto bounds = getLocalBounds().toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    if (size < 12.0f) return;
    const auto c = bounds.getCentre();
    const float R = size * 0.5f;
    const auto hue = Theme::deck (deck);
    const bool lightBg = Theme::day && Theme::skin != Theme::Skin::Win98;
    const auto ink = lightBg ? juce::Colour (0xff1a1d22) : juce::Colours::white;
    auto t = engine.getTrack (deck);
    auto& d = engine.getDeck (deck);

    // a faint disc so the ring reads on any skin
    g.setColour ((lightBg ? juce::Colours::white : juce::Colours::black).withAlpha (0.18f));
    g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);

    const float ringW = juce::jmax (2.0f, R * 0.05f);
    const float ringR = R - ringW;
    juce::Path ring;
    ring.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour (ink.withAlpha (0.16f));
    g.strokePath (ring, juce::PathStrokeType (ringW));

    double pos = 0.0, sr = 44100.0;
    if (t != nullptr)
    {
        const double total = (double) juce::jmax ((juce::int64) 1, t->totalFrames.load());
        sr = t->sampleRate;
        pos = d.uiPosition.load();
        const float frac = (float) juce::jlimit (0.0, 1.0, pos / total);
        const bool warn = d.uiPlaying.load() && (total - pos) / sr < 30.0;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, 0.0f, frac * juce::MathConstants<float>::twoPi, true);
        g.setColour (warn ? Theme::warn : hue);
        g.strokePath (arc, juce::PathStrokeType (ringW));

        for (auto& hc : t->hotCues)
            if (hc.load() >= 0.0)
            {
                const float a = (float) (hc.load() / total) * juce::MathConstants<float>::twoPi;
                g.setColour (ink.withAlpha (0.8f));
                g.drawLine ({ c.getPointOnCircumference (ringR + ringW * 0.5f, a), c.getPointOnCircumference (ringR - ringW * 1.5f, a) }, 1.5f);
            }
        if (d.uiLoopActive.load())
        {
            juce::Path loopArc;
            const float a0 = (float) (d.uiLoopStart.load() / total) * juce::MathConstants<float>::twoPi;
            const float a1 = (float) (d.uiLoopEnd.load() / total) * juce::MathConstants<float>::twoPi;
            loopArc.addCentredArc (c.x, c.y, ringR - ringW * 1.8f, ringR - ringW * 1.8f, 0.0f, a0, juce::jmax (a0 + 0.04f, a1), true);
            g.setColour (hue.withAlpha (0.9f));
            g.strokePath (loopArc, juce::PathStrokeType (1.5f));
        }
    }

    // the spinning marker
    const float angle = t != nullptr ? (float) angleFor (pos, sr) : 0.0f;
    g.setColour (t != nullptr ? ink : ink.withAlpha (0.3f));
    g.drawLine ({ c.getPointOnCircumference (R * 0.30f, angle), c.getPointOnCircumference (ringR - ringW * 2.5f, angle) }, juce::jmax (1.5f, R * 0.035f));

    g.setColour (ink.withAlpha (0.55f));
    g.setFont (Theme::numbers (juce::jlimit (9.0f, 15.0f, R * 0.22f)));
    g.drawText (juce::String (deck + 1), juce::Rectangle<float> (R, R).withCentre (c), juce::Justification::centred);

    if (engine.uiMasterDeck.load() == deck)
    {
        g.setColour (hue.withAlpha (0.45f));
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.0f);
    }
}

float Platter::pointerAngle (juce::Point<float> p) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (p.x - c.x, -(p.y - c.y));   // 0 at 12 o'clock, clockwise positive
}

void Platter::mouseDown (const juce::MouseEvent& e)
{
    if (engine.getTrack (deck) == nullptr) return;
    scratching = true;
    lastAngle = pointerAngle (e.position);
    turned = 0.0;
    scratchStart = engine.getDeck (deck).uiPosition.load();
    engine.post (DeckCommand::ScratchBegin, deck);
}

void Platter::mouseDrag (const juce::MouseEvent& e)
{
    if (! scratching) return;
    auto t = engine.getTrack (deck);
    if (t == nullptr) return;
    const float a = pointerAngle (e.position);
    float delta = a - lastAngle;
    if (delta > juce::MathConstants<float>::pi) delta -= juce::MathConstants<float>::twoPi;
    if (delta < -juce::MathConstants<float>::pi) delta += juce::MathConstants<float>::twoPi;
    lastAngle = a;
    turned += delta;
    const double frames = turned / juce::MathConstants<double>::twoPi / revsPerSecond * t->sampleRate;
    engine.post (DeckCommand::ScratchMove, deck, juce::jmax (0.0, scratchStart + frames));
}

void Platter::mouseUp (const juce::MouseEvent&)
{
    if (! scratching) return;
    scratching = false;
    engine.post (DeckCommand::ScratchEnd, deck);
}

// ===========================================================================
FxSlotComponent::FxSlotComponent (AudioEngine& e, int d, int s) : engine (e), deck (d), slotIndex (s)
{
    addAndMakeVisible (power);
    power.setTooltip ("Switch the effect on or off. Right-click to choose a different effect");
    power.onClick = [this] { auto& sl = slot(); sl.on = ! sl.on.load(); refresh(); };
    power.onRightClick = [this] { showTypeMenu(); };
    power.getProperties().set ("fontScale", 0.9);

    addAndMakeVisible (chooser);
    chooser.setTooltip ("Choose the effect");
    chooser.onClick = [this] { showTypeMenu(); };

    addAndMakeVisible (amount);
    amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    amount.setRange (0.0, 1.0, 0.0);
    amount.setDoubleClickReturnValue (true, 0.5);
    amount.setValue (slot().amount.load(), juce::dontSendNotification);
    amount.setColour (juce::Slider::rotarySliderFillColourId, Theme::tech ? Theme::active : Theme::deck (d));
    amount.setTooltip ("Effect amount");
    amount.onValueChange = [this] { slot().amount = (float) amount.getValue(); };

    for (auto* b : { &beatsDown, &beatsUp })
    {
        addAndMakeVisible (*b);
        b->getProperties().set ("fontScale", 0.85);
    }
    beatsDown.setTooltip ("Shorter (beats)");
    beatsUp.setTooltip ("Longer (beats)");
    beatsDown.onClick = [this] { stepBeats (-1); };
    beatsUp.onClick = [this] { stepBeats (1); };

    addAndMakeVisible (beatsLabel);
    beatsLabel.setJustificationType (juce::Justification::centred);
    beatsLabel.setFont (Theme::numbers (12.0f));
    beatsLabel.setColour (juce::Label::textColourId, Theme::tech ? Theme::lcdText : Theme::text);
    beatsLabel.setTooltip ("Effect length in beats");
    refresh();
}

FxSlotControls& FxSlotComponent::slot()
{
    return engine.getDeckFx (deck).slots[(size_t) slotIndex];
}

void FxSlotComponent::showTypeMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Deck " + juce::String (deck + 1) + " effect " + juce::String (slotIndex + 1));
    for (int i = 0; i < numFxTypes; ++i)
        m.addItem (i + 1, fxName (i), true, slot().type.load() == i);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&power), [this] (int r)
    {
        if (r > 0) { slot().type = r - 1; refresh(); }
    });
}

void FxSlotComponent::stepBeats (int dir)
{
    const float cur = slot().beats.load();
    int idx = 0;
    for (int i = 0; i < (int) fxBeatChoices.size(); ++i)
        if (std::abs (fxBeatChoices[(size_t) i] - cur) < std::abs (fxBeatChoices[(size_t) idx] - cur)) idx = i;
    idx = juce::jlimit (0, (int) fxBeatChoices.size() - 1, idx + dir);
    slot().beats = fxBeatChoices[(size_t) idx];
    refresh();
}

void FxSlotComponent::refresh()
{
    auto& sl = slot();
    const bool on = sl.on.load();
    power.setButtonText (fxName (sl.type.load()));
    power.setToggleState (on, juce::dontSendNotification);
    power.setColour (juce::TextButton::buttonOnColourId, Theme::tech ? Theme::active : Theme::deck (deck));
    beatsLabel.setText (fxBeatsLabel (sl.beats.load()), juce::dontSendNotification);
    if (! amount.isMouseButtonDown())
        amount.setValue (sl.amount.load(), juce::dontSendNotification);
    const bool tail = sl.tailActive.load();
    if (tail != (bool) getProperties().getWithDefault ("tail", false))
    {
        getProperties().set ("tail", tail);
        repaint();
    }
}

void FxSlotComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Theme::drawInset (g, r, 3.0f);
    if ((bool) getProperties().getWithDefault ("tail", false))
    {
        g.setColour (Theme::active.withAlpha (0.6f));
        g.drawRect (r.reduced (1.0f), 1.0f);
    }
}

void FxSlotComponent::resized()
{
    auto r = getLocalBounds().reduced (2);
    const int h = r.getHeight();
    auto beats = r.removeFromRight (juce::jmin (64, r.getWidth() / 3));
    beatsDown.setBounds (beats.removeFromLeft (14));
    beatsUp.setBounds (beats.removeFromRight (14));
    beatsLabel.setBounds (beats);
    r.removeFromRight (2);
    amount.setBounds (r.removeFromRight (juce::jmin (h + 4, 30)));
    r.removeFromRight (2);
    chooser.setBounds (r.removeFromRight (14));
    power.setBounds (r);
}

} // namespace loopdeck
