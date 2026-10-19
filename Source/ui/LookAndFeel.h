#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace loopdeck
{

namespace Theme
{
    // Mutable so the whole UI can switch between day and night.
    // Mostly greys; colour is kept for what needs identifying at a glance.
    /** Skins. Gunmetal and Flat follow the Day/Night and Mono switches; the others are fixed looks. */
    enum class Skin : int { Gunmetal = 0, Flat, Aqua, SonyBlack, Vaio, Win98, SoftClub, count };
    constexpr int numSkins = (int) Skin::count;
    const char* skinName (Skin);
    const char* skinBlurb (Skin);
    inline bool skinUsesDayNight (Skin s) { return s == Skin::Gunmetal || s == Skin::Flat; }

    inline Skin skin = Skin::Gunmetal;
    inline bool userDay = false, userMono = false;   // the switches, as the user set them
    inline bool day = false;    // effective: a light look
    inline bool mono = false;   // clinical look: neutral greys only, no hues anywhere
    inline bool tech = true;    // any textured skin (everything except Flat)
    inline juce::Colour base, panel, raised, line, text, textDim, active, accent2,
                        waveLow, waveMid, waveHigh, warn, meterOk, meterHot,
                        panelTop, panelBottom, lcd, lcdText, screenA, screenB;

    /** Changes when any part of the look changes (used to invalidate cached drawings). */
    inline int key() { return (day ? 1 : 0) + (mono ? 2 : 0) + (int) skin * 4; }

    void applySkin();   // LookAndFeel.cpp: palette and textures for the textured skins

    /** The flat palettes (muted nature tones, or clinical greys). */
    inline void setFlat (bool isDay, bool isMono)
    {
        day = isDay;
        mono = isMono;

        if (mono)
        {
            // Clinical: pure neutral greys, no warmth, contrast does all the work.
            if (isDay)
            {
                base = juce::Colour (0xfff4f4f4); panel = juce::Colour (0xffffffff); raised = juce::Colour (0xffe8e8e8);
                line = juce::Colour (0xffcfcfcf); text = juce::Colour (0xff141414); textDim = juce::Colour (0xff767676);
                active = juce::Colour (0xff3c3c3c);
                waveLow = juce::Colour (0xff2e2e2e); waveMid = juce::Colour (0xff6e6e6e); waveHigh = juce::Colour (0xffa6a6a6);
                warn = juce::Colour (0xff000000); meterOk = juce::Colour (0xff5a5a5a); meterHot = juce::Colour (0xff2a2a2a);
            }
            else
            {
                base = juce::Colour (0xff141414); panel = juce::Colour (0xff1c1c1c); raised = juce::Colour (0xff282828);
                line = juce::Colour (0xff383838); text = juce::Colour (0xffeaeaea); textDim = juce::Colour (0xff8a8a8a);
                active = juce::Colour (0xffc8c8c8);
                waveLow = juce::Colour (0xffd8d8d8); waveMid = juce::Colour (0xff9c9c9c); waveHigh = juce::Colour (0xff626262);
                warn = juce::Colour (0xffffffff); meterOk = juce::Colour (0xffa8a8a8); meterHot = juce::Colour (0xffdedede);
            }
            return;
        }

        if (isDay)
        {
            base     = juce::Colour (0xfff7f7f6);
            panel    = juce::Colour (0xffffffff);
            raised   = juce::Colour (0xffececea);
            line     = juce::Colour (0xffd5d5d2);
            text     = juce::Colour (0xff2b2c2e);
            textDim  = juce::Colour (0xff7c7e82);
            active   = juce::Colour (0xff5c5e62);   // neutral "on" state
            waveLow  = juce::Colour (0xffb06e5c);   // blood orange
            waveMid  = juce::Colour (0xffbf9b5f);   // ember
            waveHigh = juce::Colour (0xff7699aa);   // sea
            warn     = juce::Colour (0xffa9573f);
            meterOk  = juce::Colour (0xff7d9871);   // forest
            meterHot = juce::Colour (0xffc4a062);
        }
        else
        {
            base     = juce::Colour (0xff1d1e20);
            panel    = juce::Colour (0xff25272a);
            raised   = juce::Colour (0xff303236);
            line     = juce::Colour (0xff3c3f44);
            text     = juce::Colour (0xffd8d6d1);
            textDim  = juce::Colour (0xff878a8f);
            active   = juce::Colour (0xff8e9196);
            waveLow  = juce::Colour (0xffb5786a);
            waveMid  = juce::Colour (0xffc4a878);
            waveHigh = juce::Colour (0xff8fb0c0);
            warn     = juce::Colour (0xffc0745f);
            meterOk  = juce::Colour (0xff7f9a76);
            meterHot = juce::Colour (0xffc7ab76);
        }
    }

    /** Picks the skin; Day/Night and Mono apply to the skins that use them. */
    inline void set (bool isDay, bool isMono, Skin newSkin)
    {
        skin = newSkin;
        userDay = isDay;
        userMono = isMono;
        tech = skin != Skin::Flat;
        if (skinUsesDayNight (skin)) setFlat (isDay, isMono);
        else setFlat (false, false);   // fixed skins draw waveforms on dark screens: use the bright wave colours
        panelTop = panelBottom = panel;
        lcd = base;
        lcdText = text;
        accent2 = mono ? textDim : warn;
        screenA = screenB = base;
        if (tech) applySkin();
    }
    inline void set (bool isDay, bool isMono) { set (isDay, isMono, skin); }

    inline void setDay (bool isDay) { set (isDay, userMono); }

    /** Each deck owns a muted nature hue (ocean, ember, forest, blood orange, heather, moss,
        sand, lake). In mono every deck uses the same neutral; numbers identify decks. */
    inline juce::Colour deck (int index)
    {
        if (mono) return tech ? (day ? juce::Colour (0xff3a3a3a) : juce::Colour (0xffd2d2d2)) : active;
        if (skin == Skin::Win98)
        {
            // the classic 16-colour system palette
            static const juce::uint32 w98[] = { 0xff000080, 0xff800000, 0xff008000, 0xff808000,
                                                0xff800080, 0xff008080, 0xff0000ff, 0xffa0522d };
            return juce::Colour (w98[(size_t) ((index % 8 + 8) % 8)]);
        }
        static const juce::uint32 night[] = { 0xff7096ab, 0xffc99a63, 0xff819c77, 0xffbb7563,
                                              0xff9a8db0, 0xffa6a573, 0xffbda786, 0xff73a19c };
        static const juce::uint32 dayHues[] = { 0xff52798f, 0xffaf7d43, 0xff607f57, 0xffa5584a,
                                                0xff7b6d96, 0xff83824d, 0xff9a8159, 0xff4f8580 };
        const auto i = (size_t) ((index % 8 + 8) % 8);
        const auto c = juce::Colour (day ? dayHues[i] : night[i]);
        // the tech skin lights its keys a little more vividly
        if (skin == Skin::SonyBlack) return c.withMultipliedSaturation (1.5f).brighter (0.25f);   // LED-bright
        if (skin == Skin::Aqua || skin == Skin::SoftClub) return c.withMultipliedSaturation (1.45f);
        if (skin == Skin::Vaio) return c.withMultipliedSaturation (1.4f).brighter (0.2f);
        return tech ? c.withMultipliedSaturation (day ? 1.15f : 1.3f).brighter (day ? 0.0f : 0.08f) : c;
    }

    inline juce::Font numbers (float height)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (height, juce::Font::bold));
       #else
        return juce::Font (height, juce::Font::bold);
       #endif
    }

    inline juce::Font label (float height, bool bold = false)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
       #else
        return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
       #endif
    }
}

namespace Theme
{
    /** Panel body in the current skin's material (sharp corners; the radius is ignored). */
    void drawPanel (juce::Graphics&, juce::Rectangle<float>, float radius = 0.0f);
    /** Recessed display window (LCD) for numbers. */
    void drawInset (juce::Graphics&, juce::Rectangle<float>, float radius = 0.0f);
    /** Background behind waveforms. */
    void drawWaveBackground (juce::Graphics&, juce::Rectangle<float>, bool vertical);
    /** The window background (wallpaper, pinstripes, desktop...) for the whole area. */
    void drawWindowBackground (juce::Graphics&, juce::Rectangle<int> area);
    /** Win98-style 3D bevel. */
    void drawBevel (juce::Graphics&, juce::Rectangle<float>, bool raised, bool thick = true);
}

class NEXTLOOPLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NEXTLOOPLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
};

} // namespace loopdeck
