#include "MixerPanel.h"

namespace loopdeck
{

// ===========================================================================
void LevelMeter::setLevels (float l, float r)
{
    levelL = juce::jmax (l, levelL * 0.82f);
    levelR = juce::jmax (r, levelR * 0.82f);
    holdL = juce::jmax (l, holdL * 0.97f);
    holdR = juce::jmax (r, holdR * 0.97f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (Theme::tech)
    {
        // segmented LED ladder: green, amber, red
        g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.75f : 0.9f));
        g.fillRoundedRectangle (r, 2.0f);
        auto inner = r.reduced (2.0f);
        const float colW = (inner.getWidth() - 1.0f) * 0.5f;
        const int segs = juce::jlimit (8, 40, (int) (inner.getHeight() / 4.0f));
        const float segH = inner.getHeight() / (float) segs;
        auto toPos = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f); };
        const float yellowAt = toPos (juce::Decibels::decibelsToGain (-9.0f)), redAt = toPos (juce::Decibels::decibelsToGain (-1.0f));
        const auto green = juce::Colour (0xff4fd06a), amber = juce::Colour (0xffe8b83a), red = juce::Colour (0xffe8503a);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float lvl = toPos (ch == 0 ? levelL : levelR);
            const float hold = toPos (ch == 0 ? holdL : holdR);
            const float x = inner.getX() + ch * (colW + 1.0f);
            for (int i = 0; i < segs; ++i)
            {
                const float p = (i + 0.5f) / (float) segs;
                const auto c = p >= redAt ? red : (p >= yellowAt ? amber : green);
                const bool lit = p <= lvl || std::abs (p - hold) < 0.5f / segs;
                g.setColour (lit ? c : c.withAlpha (0.12f));
                g.fillRect (x, inner.getBottom() - (i + 1) * segH + 0.5f, colW, segH - 1.0f);
            }
        }
        return;
    }
    g.setColour (Theme::base);
    g.fillRoundedRectangle (r, 2.0f);

    auto bar = [&] (juce::Rectangle<float> b, float level, float hold)
    {
        auto toPos = [] (float v)
        {
            const float db = juce::Decibels::gainToDecibels (v, -60.0f);
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        const float p = toPos (level);
        auto filled = b.withTrimmedTop (b.getHeight() * (1.0f - p));
        const float yellowY = b.getY() + b.getHeight() * (1.0f - toPos (juce::Decibels::decibelsToGain (-9.0f)));
        const float redY = b.getY() + b.getHeight() * (1.0f - toPos (juce::Decibels::decibelsToGain (-1.0f)));
        g.setColour (Theme::meterOk);
        g.fillRect (filled.withTop (juce::jmax (filled.getY(), yellowY)));
        if (filled.getY() < yellowY)
        {
            g.setColour (Theme::meterHot);
            g.fillRect (filled.withTop (juce::jmax (filled.getY(), redY)).withBottom (yellowY));
        }
        if (filled.getY() < redY)
        {
            g.setColour (Theme::warn);
            g.fillRect (filled.withBottom (redY));
        }
        const float hy = b.getY() + b.getHeight() * (1.0f - toPos (hold));
        g.setColour (Theme::text.withAlpha (0.7f));
        g.fillRect (b.getX(), hy, b.getWidth(), 1.0f);
    };

    auto inner = r.reduced (1.5f);
    const float w = (inner.getWidth() - 1.0f) * 0.5f;
    bar (inner.withWidth (w), levelL, holdL);
    bar (inner.withTrimmedLeft (w + 1.0f), levelR, holdR);
}

// ===========================================================================
CaptionedKnob::CaptionedKnob (const juce::String& c, juce::Colour accent, bool bipolar) : caption (c)
{
    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
}

void CaptionedKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (12);
    slider.setBounds (r);
}

void CaptionedKnob::paint (juce::Graphics& g)
{
    g.setColour (Theme::textDim);
    g.setFont (Theme::label (10.5f));
    g.drawText (caption, getLocalBounds().removeFromBottom (12), juce::Justification::centred);
}

// ===========================================================================
ChannelComponent::ChannelComponent (AudioEngine& e, int d)
    : engine (e), deck (d), hue (Theme::deck (d)),
      trim ("Gain", Theme::active, true), high ("High", Theme::active, true), mid ("Mid", Theme::active, true),
      low ("Low", Theme::active, true), filter ("Filter", Theme::active, true)
{
    auto& ch = engine.getMixer().channels[(size_t) deck];

    auto setupKnob = [this] (CaptionedKnob& k, double lo, double hi, double def, double value,
                             std::function<void (double)> set, const juce::String& tip)
    {
        addAndMakeVisible (k);
        k.slider.setRange (lo, hi, 0.0);
        k.slider.setValue (value, juce::dontSendNotification);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.onValueChange = [&k, set] { set (k.slider.getValue()); };
        k.slider.setTooltip (tip);
    };

    setupKnob (trim, -12.0, 12.0, 0.0, ch.trimDb.load(), [this] (double v) { engine.getMixer().channels[(size_t) deck].trimDb = (float) v; },
               "Channel gain (auto-gain from analysis is applied on top). Double-click to reset");
    setupKnob (high, 0.0, 1.0, 0.5, ch.eqHigh.load(), [this] (double v) { engine.getMixer().channels[(size_t) deck].eqHigh = (float) v; },
               "High EQ. Fully left kills the band");
    setupKnob (mid, 0.0, 1.0, 0.5, ch.eqMid.load(), [this] (double v) { engine.getMixer().channels[(size_t) deck].eqMid = (float) v; },
               "Mid EQ. Fully left kills the band");
    setupKnob (low, 0.0, 1.0, 0.5, ch.eqLow.load(), [this] (double v) { engine.getMixer().channels[(size_t) deck].eqLow = (float) v; },
               "Low EQ. Fully left kills the band");
    setupKnob (filter, -1.0, 1.0, 0.0, ch.filter.load(), [this] (double v) { engine.getMixer().channels[(size_t) deck].filter = (float) v; },
               "Filter: left is low-pass, right is high-pass");

    addAndMakeVisible (fader);
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange (0.0, 1.0, 0.0);
    fader.setValue (ch.fader.load(), juce::dontSendNotification);
    fader.setColour (juce::Slider::thumbColourId, hue);
    fader.onValueChange = [this] { engine.getMixer().channels[(size_t) deck].fader = (float) fader.getValue(); };

    addAndMakeVisible (meter);

    addAndMakeVisible (cue);
    cue.setColour (juce::TextButton::buttonOnColourId, Theme::active);
    cue.setTooltip ("Send this channel to the headphones");
    cue.onClick = [this]
    {
        auto& c = engine.getMixer().channels[(size_t) deck];
        c.cue = ! c.cue.load();
    };

    addAndMakeVisible (assign);
    assign.setTooltip ("Crossfader side: A, through (unaffected), or B");
    assign.onClick = [this]
    {
        auto& c = engine.getMixer().channels[(size_t) deck];
        c.xfaderAssign = (c.xfaderAssign.load() + 1) % 3;
    };
    refresh();
}

void ChannelComponent::refresh()
{
    auto& ch = engine.getMixer().channels[(size_t) deck];
    meter.setLevels (ch.meterL.load(), ch.meterR.load());
    cue.setToggleState (ch.cue.load(), juce::dontSendNotification);
    static const char* sides[] = { "A", "Thru", "B" };
    assign.setButtonText (sides[juce::jlimit (0, 2, ch.xfaderAssign.load())]);
}

void ChannelComponent::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat(), 5.0f);
    g.setColour (hue);
    g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromTop (3.0f), 1.5f);
    g.setFont (Theme::numbers (13.0f));
    g.drawText (juce::String (deck + 1), getLocalBounds().removeFromTop (20), juce::Justification::centred);
}

void ChannelComponent::resized()
{
    auto r = getLocalBounds().reduced (5);
    r.removeFromTop (16);
    const bool tall = getHeight() > getWidth() * 2;

    if (tall)
    {
        // narrow column: knobs stacked, fader + meter below
        auto bottom = r.removeFromBottom (24);
        auto assignArea = bottom.removeFromRight (bottom.getWidth() / 2);
        cue.setBounds (bottom.reduced (1, 0));
        assign.setBounds (assignArea.reduced (1, 0));
        r.removeFromBottom (4);

        const int knobH = juce::jlimit (36, 62, r.getHeight() / 9);
        for (auto* k : { &trim, &high, &mid, &low, &filter })
            k->setBounds (r.removeFromTop (knobH));
        r.removeFromTop (4);
        meter.setBounds (r.removeFromRight (12));
        fader.setBounds (r);
    }
    else
    {
        // short and wide (5–8 deck strips): knobs on the left, fader on the right
        auto right = r.removeFromRight (r.getWidth() * 2 / 5);
        auto buttons = right.removeFromBottom (22);
        cue.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        assign.setBounds (buttons.reduced (1, 0));
        right.removeFromBottom (3);
        meter.setBounds (right.removeFromRight (10));
        fader.setBounds (right);

        const int kw = r.getWidth() / 3;
        const int kh = r.getHeight() / 2;
        auto top = r.removeFromTop (kh);
        high.setBounds (top.removeFromLeft (kw));
        mid.setBounds (top.removeFromLeft (kw));
        low.setBounds (top);
        trim.setBounds (r.removeFromLeft (kw));
        filter.setBounds (r.removeFromLeft (kw));
    }
}

// ===========================================================================
MasterSection::MasterSection (AudioEngine& e)
    : engine (e),
      masterVol ("Master", Theme::active, false), phonesMix ("Cue / mix", Theme::active, true), phonesVol ("Phones", Theme::active, false)
{
    auto& m = engine.getMixer().master;

    auto knob = [this] (CaptionedKnob& k, double def, double v, std::function<void (double)> set, const juce::String& tip)
    {
        addAndMakeVisible (k);
        k.slider.setRange (0.0, 1.0, 0.0);
        k.slider.setValue (v, juce::dontSendNotification);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.onValueChange = [&k, set] { set (k.slider.getValue()); };
        k.slider.setTooltip (tip);
    };
    knob (masterVol, 0.8, m.masterGain.load(), [this] (double v) { engine.getMixer().master.masterGain = (float) v; }, "Master volume");
    knob (phonesMix, 0.0, m.headphoneMix.load(), [this] (double v) { engine.getMixer().master.headphoneMix = (float) v; },
          "Headphones: left hears cued channels, right hears the master");
    knob (phonesVol, 0.8, m.headphoneGain.load(), [this] (double v) { engine.getMixer().master.headphoneGain = (float) v; }, "Headphone volume");

    addAndMakeVisible (crossfader);
    crossfader.setSliderStyle (juce::Slider::LinearHorizontal);
    crossfader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    crossfader.setRange (-1.0, 1.0, 0.0);
    crossfader.setDoubleClickReturnValue (true, 0.0);
    crossfader.getProperties().set ("bipolar", true);
    crossfader.setValue (m.crossfader.load(), juce::dontSendNotification);
    crossfader.onValueChange = [this] { engine.getMixer().master.crossfader = (float) crossfader.getValue(); };
    crossfader.setTooltip ("Crossfader. Double-click to centre");

    addAndMakeVisible (curve);
    curve.addItemList ({ "Smooth", "Sharp cut", "Off" }, 1);
    curve.setSelectedItemIndex (m.crossfaderCurve.load(), juce::dontSendNotification);
    curve.onChange = [this] { engine.getMixer().master.crossfaderCurve = curve.getSelectedItemIndex(); };
    curve.setTooltip ("Crossfader curve");

    addAndMakeVisible (limiter);
    limiter.setTooltip ("Stops the master output from clipping");
    limiter.onClick = [this] { auto& mm = engine.getMixer().master; mm.limiter = ! mm.limiter.load(); };
    addAndMakeVisible (splitCue);
    splitCue.setTooltip ("For a single stereo output: cue in the left ear, master in the right");
    splitCue.onClick = [this] { auto& mm = engine.getMixer().master; mm.splitCue = ! mm.splitCue.load(); };

    addAndMakeVisible (meter);
    refresh();
}

void MasterSection::refresh()
{
    auto& m = engine.getMixer().master;
    meter.setLevels (m.meterL.load(), m.meterR.load());
    const bool limiting = m.limiterReduction.load() < -0.5f;
    limiter.setToggleState (m.limiter.load(), juce::dontSendNotification);
    limiter.setColour (juce::TextButton::buttonOnColourId, limiting ? Theme::warn : Theme::textDim);
    splitCue.setToggleState (m.splitCue.load(), juce::dontSendNotification);
    splitCue.setColour (juce::TextButton::buttonOnColourId, Theme::textDim);
}

void MasterSection::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat(), 5.0f);
    g.setColour (Theme::textDim);
    g.setFont (Theme::label (10.5f));
    g.drawText ("A", crossfader.getBounds().translated (0, 0).withWidth (12).translated (-12, 0), juce::Justification::centred);
    g.drawText ("B", crossfader.getBounds().withX (crossfader.getRight()).withWidth (12), juce::Justification::centred);
}

void MasterSection::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto xf = r.removeFromBottom (30);
    crossfader.setBounds (xf.reduced (14, 0));
    r.removeFromBottom (4);
    curve.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    auto buttons = r.removeFromBottom (22);
    limiter.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
    splitCue.setBounds (buttons.reduced (1, 0));
    r.removeFromBottom (4);

    meter.setBounds (r.removeFromRight (14));
    r.removeFromRight (4);
    if (r.getHeight() > r.getWidth())
    {
        const int kh = r.getHeight() / 3;
        masterVol.setBounds (r.removeFromTop (kh));
        phonesMix.setBounds (r.removeFromTop (kh));
        phonesVol.setBounds (r);
    }
    else
    {
        const int kw = r.getWidth() / 3;
        masterVol.setBounds (r.removeFromLeft (kw));
        phonesMix.setBounds (r.removeFromLeft (kw));
        phonesVol.setBounds (r);
    }
}

} // namespace loopdeck
