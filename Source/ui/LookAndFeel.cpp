#include "LookAndFeel.h"

namespace loopdeck
{

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
    setColour (juce::ComboBox::backgroundColourId, Theme::raised);
    setColour (juce::ComboBox::outlineColourId, Theme::line);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::text);
    setColour (juce::ListBox::backgroundColourId, Theme::panel);
    setColour (juce::ListBox::outlineColourId, Theme::line);
    setColour (juce::TextEditor::backgroundColourId, Theme::raised);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::textDim);
    setColour (juce::TextEditor::highlightColourId, Theme::line);
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

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::line);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = startAngle + pos * (endAngle - startAngle);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (s.isEnabled() ? accent : Theme::textDim);
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // knob body + pointer
    const float bodyR = arcR - lineW * 1.4f;
    g.setColour (Theme::raised);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    juce::Path p;
    p.addRoundedRectangle (-lineW * 0.35f, -bodyR, lineW * 0.7f, bodyR * 0.6f, lineW * 0.3f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (Theme::text);
    g.fillPath (p);
}

void NEXTLOOPLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::thumbColourId);
    const bool vertical = style == juce::Slider::LinearVertical;
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);

    if (vertical)
    {
        const float cx = r.getCentreX();
        g.setColour (Theme::base);
        g.fillRoundedRectangle (cx - 2.0f, r.getY(), 4.0f, r.getHeight(), 2.0f);
        // scale ticks
        g.setColour (Theme::line);
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = r.getY() + r.getHeight() * (float) i / 10.0f;
            const float tw = (i % 5 == 0) ? 9.0f : 5.0f;
            g.drawHorizontalLine ((int) ty, cx - 6.0f - tw, cx - 6.0f);
        }
        if (bipolar)
        {
            g.setColour (Theme::textDim);
            g.drawHorizontalLine ((int) r.getCentreY(), cx - 12.0f, cx + 12.0f);
        }
        const float capH = juce::jmin (26.0f, r.getHeight() * 0.18f);
        const float capW = juce::jmin (r.getWidth() - 4.0f, 30.0f);
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, pos });
        g.setColour (Theme::raised.brighter (0.15f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (s.isEnabled() ? accent : Theme::textDim);
        g.fillRect (cap.withSizeKeepingCentre (capW - 6.0f, 2.0f));
    }
    else
    {
        const float cy = r.getCentreY();
        g.setColour (Theme::base);
        g.fillRoundedRectangle (r.getX(), cy - 2.0f, r.getWidth(), 4.0f, 2.0f);
        if (bipolar)
        {
            g.setColour (Theme::textDim);
            g.drawVerticalLine ((int) r.getCentreX(), cy - 9.0f, cy + 9.0f);
        }
        const float capW = juce::jmin (22.0f, r.getWidth() * 0.15f);
        const float capH = juce::jmin (r.getHeight() - 2.0f, 26.0f);
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ pos, cy });
        g.setColour (Theme::raised.brighter (0.15f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (s.isEnabled() ? accent : Theme::textDim);
        g.fillRect (cap.withSizeKeepingCentre (2.0f, capH - 6.0f));
    }
}

void NEXTLOOPLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    auto fill = on ? onColour : b.findColour (juce::TextButton::buttonColourId);
    if (down) fill = fill.brighter (0.2f);
    else if (highlighted) fill = fill.brighter (0.07f);
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    if (! on)
    {
        // a thin line in the owner's colour marks deck-coloured buttons even when off
        const bool tinted = (bool) b.getProperties().getWithDefault ("tinted", false);
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
    auto col = on ? (bg.getPerceivedBrightness() > 0.55f ? dark : light) : Theme::text;
    if (! b.isEnabled()) col = col.withMultipliedAlpha (0.5f);
    g.setColour (col);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 1), juce::Justification::centred, 2, 0.8f);
}

juce::Font NEXTLOOPLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    const float scale = (float) (double) b.getProperties().getWithDefault ("fontScale", 1.0);
    return Theme::label (juce::jlimit (10.0f, 15.0f, (float) buttonHeight * 0.48f) * scale, true);
}

juce::Font NEXTLOOPLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void NEXTLOOPLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

} // namespace loopdeck
