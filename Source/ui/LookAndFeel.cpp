#include "LookAndFeel.h"

namespace loopdeck
{

// ===========================================================================
// Tech skin palette: gunmetal by night, brushed aluminium by day, with a
// cool blue for lit keys (white in mono). Wave and meter colours stay as the
// flat palette sets them.
// ===========================================================================
void Theme::applyTech()
{
    using C = juce::Colour;
    if (mono)
    {
        if (day)
        {
            base = C (0xffb3b5b8); panel = C (0xffdcdde0); panelTop = C (0xffeff0f2); panelBottom = C (0xffc9cbce);
            raised = C (0xffeceef0); line = C (0xff8c8f94); text = C (0xff111214); textDim = C (0xff55585d);
            active = C (0xff2a2b2e); lcd = C (0xffd4d8dc); lcdText = C (0xff111214);
        }
        else
        {
            base = C (0xff0b0b0c); panel = C (0xff1d1d1f); panelTop = C (0xff2b2b2e); panelBottom = C (0xff151517);
            raised = C (0xff2f2f33); line = C (0xff46464b); text = C (0xffededee); textDim = C (0xff8f9095);
            active = C (0xffdcdcdc); lcd = C (0xff060607); lcdText = C (0xfff2f2f2);
        }
        return;
    }

    if (day)
    {
        base = C (0xffaab2bb); panel = C (0xffdde2e7); panelTop = C (0xffeef1f4); panelBottom = C (0xffc6cdd4);
        raised = C (0xffeaedf0); line = C (0xff8a94a0); text = C (0xff14181d); textDim = C (0xff515b66);
        active = C (0xff2c7cc4); lcd = C (0xffcfdde6); lcdText = C (0xff0e2638);
    }
    else
    {
        base = C (0xff0a0c0f); panel = C (0xff1c2026); panelTop = C (0xff2a3038); panelBottom = C (0xff14171b);
        raised = C (0xff2c323a); line = C (0xff3c444e); text = C (0xffe3eaf2); textDim = C (0xff8994a1);
        active = C (0xff3c95dc); lcd = C (0xff05070a); lcdText = C (0xffc4e4ff);
    }
}

void Theme::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    if (! tech)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    // fine brushed-metal streaks
    g.setColour (juce::Colours::white.withAlpha (day ? 0.05f : 0.018f));
    for (float y = r.getY() + 3.0f; y < r.getBottom() - 2.0f; y += 3.0f)
        g.drawHorizontalLine ((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);
    // bevel: light top edge, dark outline
    g.setColour (juce::Colours::white.withAlpha (day ? 0.55f : 0.10f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    g.setColour (juce::Colours::black.withAlpha (day ? 0.35f : 0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void Theme::drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    if (! tech)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    g.setGradientFill (juce::ColourGradient (lcd.darker (0.25f), r.getX(), r.getY(), lcd.brighter (day ? 0.05f : 0.12f),
                                             r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::black.withAlpha (day ? 0.45f : 0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (day ? 0.5f : 0.07f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + radius, r.getRight() - radius);
}

void Theme::drawWaveBackground (juce::Graphics& g, juce::Rectangle<float> r, bool vertical)
{
    if (! tech)
    {
        g.setColour (base);
        g.fillRect (r);
        return;
    }
    const auto a = day ? juce::Colour (0xff1b232c) : juce::Colour (0xff07090c);
    const auto b = day ? juce::Colour (0xff2b3540) : juce::Colour (0xff141a21);
    if (vertical)
        g.setGradientFill (juce::ColourGradient (a, r.getX(), r.getY(), a, r.getRight(), r.getY(), false));
    else
        g.setGradientFill (juce::ColourGradient (a, r.getX(), r.getY(), a, r.getX(), r.getBottom(), false));
    g.fillRect (r);
    // glass: a soft lighter band through the middle
    juce::ColourGradient glass = vertical
        ? juce::ColourGradient (b.withAlpha (0.0f), r.getX(), 0.0f, b.withAlpha (0.0f), r.getRight(), 0.0f, false)
        : juce::ColourGradient (b.withAlpha (0.0f), 0.0f, r.getY(), b.withAlpha (0.0f), 0.0f, r.getBottom(), false);
    glass.addColour (0.5, b);
    g.setGradientFill (glass);
    g.fillRect (r);
}

// ===========================================================================
NEXTLOOPLookAndFeel::NEXTLOOPLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::base);
    setColour (juce::DocumentWindow::backgroundColourId, Theme::base);
    setColour (juce::TextButton::buttonColourId, Theme::raised);
    setColour (juce::TextButton::buttonOnColourId, Theme::active);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, Theme::base);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::Slider::rotarySliderFillColourId, Theme::active);
    setColour (juce::Slider::rotarySliderOutlineColourId, Theme::line);
    setColour (juce::Slider::thumbColourId, Theme::text);
    setColour (juce::Slider::trackColourId, Theme::line);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, Theme::tech ? Theme::lcd : Theme::raised);
    setColour (juce::ComboBox::outlineColourId, Theme::line);
    setColour (juce::ComboBox::textColourId, Theme::tech ? Theme::lcdText : Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::tech ? Theme::active : Theme::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::tech ? juce::Colours::white : Theme::text);
    setColour (juce::ListBox::backgroundColourId, Theme::tech ? Theme::lcd : Theme::panel);
    setColour (juce::ListBox::outlineColourId, Theme::line);
    setColour (juce::TextEditor::backgroundColourId, Theme::tech ? Theme::lcd : Theme::raised);
    setColour (juce::TextEditor::textColourId, Theme::tech ? Theme::lcdText : Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::tech ? Theme::active : Theme::textDim);
    setColour (juce::TextEditor::highlightColourId, Theme::tech ? Theme::active.withAlpha (0.5f) : Theme::line);
    setColour (juce::CaretComponent::caretColourId, Theme::text);
    setColour (juce::ScrollBar::thumbColourId, Theme::line);
    setColour (juce::TableHeaderComponent::backgroundColourId, Theme::raised);
    setColour (juce::TableHeaderComponent::textColourId, Theme::textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, Theme::line);
    setColour (juce::TreeView::backgroundColourId, Theme::panel);
    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::ToggleButton::tickColourId, Theme::text);
    setColour (juce::ToggleButton::tickDisabledColourId, Theme::textDim);
    setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
    setColour (juce::AlertWindow::textColourId, Theme::text);
    setColour (juce::GroupComponent::outlineColourId, Theme::line);
    setColour (juce::GroupComponent::textColourId, Theme::textDim);
}

// ---------------------------------------------------------------------------
void NEXTLOOPLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = juce::jmax (2.0f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::tech ? juce::Colours::black.withAlpha (Theme::day ? 0.35f : 0.8f) : Theme::line);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const auto col = s.isEnabled() ? accent : Theme::textDim;
        if (Theme::tech)
        {
            g.setColour (col.withAlpha (0.25f));   // LED glow
            g.strokePath (val, juce::PathStrokeType (lineW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = arcR - lineW * 1.4f;
    if (Theme::tech)
    {
        // machined cap: radial highlight, knurled edge, dark rim
        auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (body.translated (0.0f, 1.5f).expanded (1.0f));
        const auto hi = Theme::day ? juce::Colour (0xfff7f8fa) : juce::Colour (0xff6c7480);
        const auto lo = Theme::day ? juce::Colour (0xffa9b0b8) : juce::Colour (0xff1c2026);
        g.setGradientFill (juce::ColourGradient (hi, c.x - bodyR * 0.6f, c.y - bodyR * 0.8f, lo, c.x + bodyR * 0.5f, c.y + bodyR, true));
        g.fillEllipse (body);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (body, 1.0f);
        auto top = body.reduced (bodyR * 0.28f);
        g.setGradientFill (juce::ColourGradient (lo.brighter (0.3f), top.getX(), top.getY(), hi.darker (0.15f), top.getRight(), top.getBottom(), false));
        g.fillEllipse (top);
        juce::Path p;
        p.addRoundedRectangle (-lineW * 0.32f, -bodyR + 1.0f, lineW * 0.64f, bodyR * 0.62f, lineW * 0.3f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (Theme::day ? juce::Colour (0xff111418) : juce::Colours::white);
        g.fillPath (p);
        return;
    }

    g.setColour (Theme::raised);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    juce::Path p;
    p.addRoundedRectangle (-lineW * 0.35f, -bodyR, lineW * 0.7f, bodyR * 0.6f, lineW * 0.3f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (Theme::text);
    g.fillPath (p);
}

// ---------------------------------------------------------------------------
static void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, juce::Colour accent, bool enabled)
{
    if (! Theme::tech)
    {
        g.setColour (Theme::raised.brighter (0.15f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (enabled ? accent : Theme::textDim);
        g.fillRect (vertical ? cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 2.0f)
                             : cap.withSizeKeepingCentre (2.0f, cap.getHeight() - 6.0f));
        return;
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
    const auto hi = Theme::day ? juce::Colour (0xfffbfcfd) : juce::Colour (0xff767f8b);
    const auto lo = Theme::day ? juce::Colour (0xff9aa2ab) : juce::Colour (0xff22272e);
    // chrome cap with ridges
    juce::ColourGradient grad = vertical ? juce::ColourGradient (hi, 0.0f, cap.getY(), lo, 0.0f, cap.getBottom(), false)
                                         : juce::ColourGradient (hi, cap.getX(), 0.0f, lo, cap.getRight(), 0.0f, false);
    grad.addColour (0.5, lo.brighter (0.2f));
    grad.addColour (0.52, hi.darker (0.1f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
    const auto col = enabled ? accent : Theme::textDim;
    auto lineR = vertical ? cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 2.0f)
                          : cap.withSizeKeepingCentre (2.0f, cap.getHeight() - 6.0f);
    g.setColour (col.withAlpha (0.35f));
    g.fillRect (lineR.expanded (1.0f));
    g.setColour (col);
    g.fillRect (lineR);
}

void NEXTLOOPLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::thumbColourId);
    const bool vertical = style == juce::Slider::LinearVertical;
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const bool big = (bool) s.getProperties().getWithDefault ("bigCap", false);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);

    auto slot = [&] (juce::Rectangle<float> sr)
    {
        if (Theme::tech)
        {
            g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.6f : 0.9f));
            g.fillRoundedRectangle (sr, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.6f : 0.08f));
            if (vertical) g.drawVerticalLine ((int) sr.getRight(), sr.getY(), sr.getBottom());
            else          g.drawHorizontalLine ((int) sr.getBottom(), sr.getX(), sr.getRight());
        }
        else
        {
            g.setColour (Theme::base);
            g.fillRoundedRectangle (sr, 2.0f);
        }
    };

    if (vertical)
    {
        const float cx = r.getCentreX();
        slot ({ cx - 2.0f, r.getY(), 4.0f, r.getHeight() });
        g.setColour (Theme::tech ? Theme::textDim.withAlpha (0.6f) : Theme::line);
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = r.getY() + r.getHeight() * (float) i / 10.0f;
            const float tw = (i % 5 == 0) ? 7.0f : 4.0f;
            g.drawHorizontalLine ((int) ty, cx - 5.0f - tw, cx - 5.0f);
            if (big) g.drawHorizontalLine ((int) ty, cx + 5.0f, cx + 5.0f + tw);
        }
        if (bipolar)
        {
            g.setColour (Theme::tech ? Theme::active.withAlpha (0.8f) : Theme::textDim);
            g.drawHorizontalLine ((int) r.getCentreY(), cx - 13.0f, cx + 13.0f);
        }
        const float capH = big ? juce::jmin (34.0f, r.getHeight() * 0.12f) : juce::jmin (26.0f, r.getHeight() * 0.18f);
        const float capW = juce::jmin (r.getWidth() - 2.0f, big ? 40.0f : 30.0f);
        drawFaderCap (g, juce::Rectangle<float> (capW, capH).withCentre ({ cx, pos }), true, accent, s.isEnabled());
    }
    else
    {
        const float cy = r.getCentreY();
        slot ({ r.getX(), cy - 2.0f, r.getWidth(), 4.0f });
        if (bipolar)
        {
            g.setColour (Theme::tech ? Theme::active.withAlpha (0.8f) : Theme::textDim);
            g.drawVerticalLine ((int) r.getCentreX(), cy - 9.0f, cy + 9.0f);
        }
        const float capW = juce::jmin (22.0f, r.getWidth() * 0.15f);
        const float capH = juce::jmin (r.getHeight() - 2.0f, 26.0f);
        drawFaderCap (g, juce::Rectangle<float> (capW, capH).withCentre ({ pos, cy }), false, accent, s.isEnabled());
    }
}

// ---------------------------------------------------------------------------
void NEXTLOOPLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    auto fill = on ? onColour : b.findColour (juce::TextButton::buttonColourId);
    const bool tinted = (bool) b.getProperties().getWithDefault ("tinted", false);

    if (Theme::tech)
    {
        const float rad = juce::jmin (4.0f, r.getHeight() * 0.25f);
        if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.45f);
        if (down) fill = fill.darker (0.15f);
        else if (highlighted) fill = fill.brighter (0.08f);

        // drop shadow
        g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.25f : 0.6f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), rad);

        if (on)
        {
            // lit key: glow, bright face
            g.setColour (onColour.withAlpha (0.28f));
            g.fillRoundedRectangle (r.expanded (1.5f), rad + 1.5f);
            g.setGradientFill (juce::ColourGradient (fill.brighter (0.35f), r.getX(), r.getY(), fill.darker (0.3f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f).reduced (1.5f, 1.0f), rad);
        }
        else
        {
            const auto top = Theme::day ? fill.brighter (0.25f) : fill.brighter (0.18f);
            const auto bot = Theme::day ? fill.darker (0.12f) : fill.darker (0.45f);
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.6f : 0.12f));
            g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + rad, r.getRight() - rad);
            if (tinted)
            {
                // small unlit LED in the deck's colour
                g.setColour (onColour.withAlpha (0.55f));
                g.fillRect (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getWidth() - 6.0f, 1.5f);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.45f : 0.85f));
        g.drawRoundedRectangle (r, rad, 1.0f);
        return;
    }

    if (down) fill = fill.brighter (0.2f);
    else if (highlighted) fill = fill.brighter (0.07f);
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    if (! on)
    {
        // a thin line in the owner's colour marks deck-coloured buttons even when off
        g.setColour (tinted ? onColour.withAlpha (0.55f) : Theme::line);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
}

void NEXTLOOPLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto bg = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    // on a lit button, use whichever of the theme's lightest/darkest tones contrasts with the fill
    const auto light = Theme::text.getPerceivedBrightness() > Theme::base.getPerceivedBrightness() ? Theme::text : Theme::base;
    const auto dark  = light == Theme::text ? Theme::base : Theme::text;
    auto col = on ? (bg.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff0b0d10) : (Theme::tech ? juce::Colours::white : light))
                  : Theme::text;
    if (on && ! Theme::tech) col = bg.getPerceivedBrightness() > 0.55f ? dark : light;
    if (! b.isEnabled()) col = col.withMultipliedAlpha (0.5f);
    g.setColour (col);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 1), juce::Justification::centred, 2, 0.75f);
}

juce::Font NEXTLOOPLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    const float scale = (float) (double) b.getProperties().getWithDefault ("fontScale", 1.0);
    return Theme::label (juce::jlimit (9.5f, 15.0f, (float) buttonHeight * 0.48f) * scale, true);
}

juce::Font NEXTLOOPLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void NEXTLOOPLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    if (Theme::tech)
        Theme::drawInset (g, r, 3.0f);
    else
    {
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    juce::Path arrow;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Theme::tech ? Theme::active : box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void NEXTLOOPLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 16, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font NEXTLOOPLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::label (juce::jlimit (10.0f, 15.0f, (float) box.getHeight() * 0.55f), Theme::tech);
}

} // namespace loopdeck
