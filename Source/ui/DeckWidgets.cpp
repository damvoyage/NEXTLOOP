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
        mix (std::floor (ang * 180.0 / juce::MathConstants<double>::pi));   // one degree steps
        const double total = (double) juce::jmax ((juce::int64) 1, t->totalFrames.load());
        mix (std::floor (d.uiPosition.load() / total * 720.0));
        mix (d.uiPlaying.load() ? 1.0 : 0.0);
        mix (d.uiLoopActive.load() ? d.uiLoopStart.load() + d.uiLoopEnd.load() : -1.0);
        for (auto& c : t->hotCues) mix (c.load());
        mix (engine.uiMasterDeck.load() == deck ? 1.0 : 0.0);
    }
    if (h != lastSignature) { lastSignature = h; repaint(); }
}

namespace
{
    struct PlatterGeometry
    {
        juce::Rectangle<float> sq;
        juce::Point<float> c;
        float R, ringW, ringR, discR, labelR;
        PlatterGeometry (juce::Rectangle<float> bounds)
        {
            const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
            sq = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
            c = sq.getCentre();
            R = size * 0.5f;
            ringW = juce::jmax (3.0f, R * 0.09f);
            ringR = R - ringW * 0.5f - 2.0f;
            discR = ringR - ringW * 0.5f - juce::jmax (2.0f, R * 0.04f);
            labelR = discR * 0.36f;
        }
    };
}

void Platter::buildDisc (int)
{
    // everything that doesn't move: housing, ring channel, vinyl, label
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    disc = juce::Image (juce::Image::ARGB, w, h, true);
    discTheme = Theme::key() * 2 + (engine.getTrack (deck) != nullptr ? 1 : 0);
    juce::Graphics g (disc);
    const PlatterGeometry geo (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h));
    const auto& sq = geo.sq;
    const auto c = geo.c;

    if (Theme::tech)
    {
        g.setGradientFill (juce::ColourGradient (Theme::panelTop.brighter (0.1f), sq.getX(), sq.getY(),
                                                 Theme::panelBottom.darker (0.3f), sq.getRight(), sq.getBottom(), false));
        g.fillEllipse (sq);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawEllipse (sq.reduced (0.5f), 1.0f);
    }
    else
    {
        g.setColour (Theme::raised);
        g.fillEllipse (sq);
    }

    {
        juce::Path bg;
        bg.addCentredArc (c.x, c.y, geo.ringR, geo.ringR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (juce::Colours::black.withAlpha (Theme::day && ! Theme::tech ? 0.15f : 0.6f));
        g.strokePath (bg, juce::PathStrokeType (geo.ringW));
    }

    // vinyl with a soft sheen and grooves
    const float r = geo.discR;
    const auto vinylA = Theme::day && ! Theme::tech ? juce::Colour (0xff3a3c40) : juce::Colour (0xff0d0e10);
    const auto vinylB = Theme::day && ! Theme::tech ? juce::Colour (0xff55585e) : juce::Colour (0xff23262b);
    g.setGradientFill (juce::ColourGradient (vinylB, c.x - r * 0.7f, c.y - r * 0.7f, vinylA, c.x + r * 0.6f, c.y + r * 0.8f, true));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    for (float gr = r * 0.40f; gr < r * 0.97f; gr += juce::jmax (1.5f, r * 0.035f))
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f, 0.8f);
    }
    juce::Path sheen;
    sheen.addPieSegment (c.x - r, c.y - r, r * 2.0f, r * 2.0f, -0.5f, 0.25f, 0.42f);
    sheen.addPieSegment (c.x - r, c.y - r, r * 2.0f, r * 2.0f, juce::MathConstants<float>::pi - 0.5f, juce::MathConstants<float>::pi + 0.25f, 0.42f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillPath (sheen);

    // label in the deck colour
    const float labelR = geo.labelR;
    const auto hue = Theme::deck (deck);
    g.setColour (engine.getTrack (deck) != nullptr ? hue.darker (Theme::tech ? 0.2f : 0.0f) : Theme::line);
    g.fillEllipse (c.x - labelR, c.y - labelR, labelR * 2.0f, labelR * 2.0f);
    if (Theme::tech)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.25f), c.x, c.y - labelR,
                                                 juce::Colours::transparentWhite, c.x, c.y + labelR * 0.2f, false));
        g.fillEllipse (c.x - labelR, c.y - labelR, labelR * 2.0f, labelR * 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - labelR, c.y - labelR, labelR * 2.0f, labelR * 2.0f, 1.0f);
}

void Platter::paint (juce::Graphics& g)
{
    const PlatterGeometry geo (getLocalBounds().toFloat());
    if (geo.R < 6.0f) return;
    const auto c = geo.c;
    const auto hue = Theme::deck (deck);
    const auto ringCol = Theme::tech ? Theme::active : hue;
    auto t = engine.getTrack (deck);
    auto& d = engine.getDeck (deck);

    const int key = Theme::key() * 2 + (t != nullptr ? 1 : 0);
    if (! disc.isValid() || disc.getWidth() != getWidth() || disc.getHeight() != getHeight() || discTheme != key)
        buildDisc (0);
    g.setOpacity (1.0f);
    g.drawImageAt (disc, 0, 0);

    const float ringR = geo.ringR, ringW = geo.ringW;
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
        const auto col = warn ? Theme::warn : ringCol;
        if (Theme::tech)
        {
            g.setColour (col.withAlpha (0.3f));
            g.strokePath (arc, juce::PathStrokeType (ringW * 1.8f));
        }
        g.setColour (col);
        g.strokePath (arc, juce::PathStrokeType (ringW));

        for (auto& hc : t->hotCues)
            if (hc.load() >= 0.0)
            {
                const float a = (float) (hc.load() / total) * juce::MathConstants<float>::twoPi;
                g.setColour (hue.brighter (0.3f));
                g.drawLine ({ c.getPointOnCircumference (ringR + ringW * 0.5f, a), c.getPointOnCircumference (ringR - ringW * 0.5f - 1.0f, a) }, 2.0f);
            }
        if (d.uiLoopActive.load())
        {
            juce::Path loopArc;
            const float a0 = (float) (d.uiLoopStart.load() / total) * juce::MathConstants<float>::twoPi;
            const float a1 = (float) (d.uiLoopEnd.load() / total) * juce::MathConstants<float>::twoPi;
            loopArc.addCentredArc (c.x, c.y, ringR - ringW, ringR - ringW, 0.0f, a0, juce::jmax (a0 + 0.03f, a1), true);
            g.setColour (hue);
            g.strokePath (loopArc, juce::PathStrokeType (2.0f));
        }
    }

    // the stripe across the record turns with the audio
    const float angle = t != nullptr ? (float) angleFor (pos, sr) : 0.0f;
    const auto inner = c.getPointOnCircumference (geo.labelR + 1.0f, angle);
    const auto outer = c.getPointOnCircumference (geo.discR - 1.0f, angle);
    const auto stripe = Theme::tech ? Theme::active.brighter (0.4f) : Theme::text;
    g.setColour (stripe.withAlpha (0.35f));
    g.drawLine ({ inner, outer }, juce::jmax (3.0f, geo.R * 0.07f));
    g.setColour (stripe);
    g.drawLine ({ inner, outer }, juce::jmax (1.5f, geo.R * 0.03f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawLine ({ c.getPointOnCircumference (geo.labelR * 0.35f, angle), c.getPointOnCircumference (geo.labelR * 0.92f, angle) }, 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (Theme::numbers (juce::jlimit (9.0f, 20.0f, geo.labelR * 0.75f)));
    g.drawText (juce::String (deck + 1), juce::Rectangle<float> (geo.labelR * 2.0f, geo.labelR * 2.0f).withCentre (c),
                juce::Justification::centred);

    if (engine.uiMasterDeck.load() == deck && Theme::tech)
    {
        g.setColour (Theme::active.withAlpha (0.5f));
        g.drawEllipse (geo.sq.reduced (0.5f), 1.5f);
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
        g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 1.0f);
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
