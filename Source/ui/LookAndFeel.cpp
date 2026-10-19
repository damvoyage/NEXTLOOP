#include "LookAndFeel.h"

namespace loopdeck
{

using C = juce::Colour;
using Skin = Theme::Skin;

const char* Theme::skinName (Skin s)
{
    switch (s)
    {
        case Skin::Gunmetal: return "Gunmetal";
        case Skin::Flat:     return "Flat";
        case Skin::Aqua:     return "Aero Aqua";
        case Skin::SonyBlack:return "90s Black";
        case Skin::Vaio:     return "Cyber Ocean";
        case Skin::Win98:    return "Desktop 98";
        case Skin::SoftClub: return "Soft Club";
        case Skin::count:    break;
    }
    return "";
}

const char* Theme::skinBlurb (Skin s)
{
    switch (s)
    {
        case Skin::Gunmetal: return "2000s hardware: brushed gunmetal, glossy keys, blue LEDs. Follows Day/Night and Mono";
        case Skin::Flat:     return "Quiet and flat: muted nature colours or clinical greys. Follows Day/Night and Mono";
        case Skin::Aqua:     return "Frutiger Aero / console aqua: sky, bubbles, pinstripes and wet glossy gel keys";
        case Skin::SonyBlack:return "Late-90s black hi-fi: matte and gloss black plastic, silver print, amber LEDs";
        case Skin::Vaio:     return "Cyber ocean: deep-sea wallpaper, smoked glass panels, cyan light";
        case Skin::Win98:    return "The 1998 desktop: grey 3D bevels, navy title bars, teal backdrop";
        case Skin::SoftClub: return "Gen X soft club: metallic corporate blue, chrome, frosted glow";
        case Skin::count:    break;
    }
    return "";
}

// ===========================================================================
// Procedural textures. Everything is generated once per skin from seeded noise,
// so the textures look the same every time and cost nothing per frame.
// ===========================================================================
namespace
{
    juce::Image overlayTile;      // tiled over panels
    int overlayKey = -1;
    juce::Image windowImage;      // full-window background
    int windowKey = -1;
    juce::Rectangle<int> windowSize;

    float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

    /** Brushed metal: long horizontal streaks plus fine grain, as light/dark overlay. */
    juce::Image makeBrushed (int w, int h, float strength, float streakLength, juce::uint32 seed)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Random rnd ((juce::int64) seed);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        std::vector<float> row ((size_t) w);
        for (int y = 0; y < h; ++y)
        {
            const float rowShade = (rnd.nextFloat() - 0.5f) * 0.6f;
            float v = rnd.nextFloat() - 0.5f;
            // two passes so the streak wraps cleanly when tiled
            for (int pass = 0; pass < 2; ++pass)
                for (int x = 0; x < w; ++x)
                {
                    v = v * streakLength + (rnd.nextFloat() - 0.5f) * (1.0f - streakLength) * 4.0f;
                    if (pass == 1) row[(size_t) x] = v;
                }
            for (int x = 0; x < w; ++x)
            {
                const float n = (row[(size_t) x] + rowShade + (rnd.nextFloat() - 0.5f) * 0.35f) * strength;
                const auto a = (juce::uint8) (clamp01 (std::abs (n)) * 255.0f);
                bd.setPixelColour (x, y, (n > 0 ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            }
        }
        return img;
    }

    /** Matte plastic grain. */
    juce::Image makeGrain (int w, int h, float strength, juce::uint32 seed)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Random rnd ((juce::int64) seed);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float n = (rnd.nextFloat() - 0.5f) + (rnd.nextFloat() - 0.5f);
                const auto a = (juce::uint8) (clamp01 (std::abs (n) * strength) * 255.0f);
                bd.setPixelColour (x, y, (n > 0 ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            }
        return img;
    }

    /** Aqua pinstripes: 1 px light, 1 px clear, with a breath of grain. */
    juce::Image makePinstripes (int w, int h, float strength)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Random rnd (98);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const bool light = (y % 4) < 2;
                const float g = (rnd.nextFloat() - 0.5f) * 0.04f;
                bd.setPixelColour (x, y, light ? juce::Colours::white.withAlpha (clamp01 (strength + g))
                                               : juce::Colour (0xff6f8fa8).withAlpha (clamp01 (strength * 0.25f + g)));
            }
        return img;
    }

    /** Smoked glass: faint diagonal sheen lines and grain. */
    juce::Image makeGlass (int w, int h)
    {
        juce::Image img = makeGrain (w, h, 0.05f, 7);
        juce::Graphics g (img);
        for (int i = -h; i < w; i += 6)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.drawLine ((float) i, (float) h, (float) (i + h), 0.0f, 1.0f);
        }
        return img;
    }

    const juce::Image& overlay()
    {
        if (overlayKey == Theme::key()) return overlayTile;
        overlayKey = Theme::key();
        switch (Theme::skin)
        {
            case Skin::Gunmetal: overlayTile = makeBrushed (256, 256, Theme::day ? 0.10f : 0.07f, 0.985f, 11); break;
            case Skin::SoftClub: overlayTile = makeBrushed (256, 256, 0.09f, 0.99f, 23); break;
            case Skin::SonyBlack:overlayTile = makeGrain (256, 256, 0.10f, 5); break;
            case Skin::Aqua:     overlayTile = makePinstripes (256, 256, 0.55f); break;
            case Skin::Vaio:     overlayTile = makeGlass (256, 256); break;
            case Skin::Win98:
            case Skin::Flat:
            case Skin::count:    overlayTile = {}; break;
        }
        return overlayTile;
    }

    void tile (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& img = overlay();
        if (! img.isValid()) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.toNearestInt());
        g.setTiledImageFill (img, (int) r.getX(), (int) r.getY(), 1.0f);
        g.fillRect (r);
    }

    // --- full-window backgrounds ------------------------------------------
    void paintAquaWallpaper (juce::Graphics& g, int w, int h)
    {
        // Frutiger Aero: bright sky, glossy green hill, floating bubbles, pinstripes over it all
        g.setGradientFill (juce::ColourGradient (C (0xff3a9fe0), 0.0f, 0.0f, C (0xffe6f6ff), 0.0f, (float) h * 0.75f, false));
        g.fillAll();
        juce::Random rnd (2007);
        // sun glow
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.8f), (float) w * 0.8f, (float) h * 0.08f,
                                                 juce::Colours::white.withAlpha (0.0f), (float) w * 0.8f + (float) h * 0.6f, (float) h * 0.08f, true));
        g.fillAll();
        // hills
        for (int layer = 0; layer < 2; ++layer)
        {
            juce::Path hill;
            const float base = (float) h * (layer == 0 ? 0.80f : 0.88f);
            hill.startNewSubPath (0.0f, (float) h);
            for (int x = 0; x <= w; x += 8)
            {
                const float y = base - std::sin ((float) x / (float) w * 3.4f + layer * 1.7f) * (float) h * 0.06f;
                hill.lineTo ((float) x, y);
            }
            hill.lineTo ((float) w, (float) h);
            hill.closeSubPath();
            const auto top = layer == 0 ? C (0xff9be05a) : C (0xff6cc63c);
            g.setGradientFill (juce::ColourGradient (top, 0.0f, base - (float) h * 0.06f, C (0xff2f8a24), 0.0f, (float) h, false));
            g.fillPath (hill);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.strokePath (hill, juce::PathStrokeType (1.5f));
        }
        // bubbles
        for (int i = 0; i < 26; ++i)
        {
            const float r = 8.0f + rnd.nextFloat() * 46.0f;
            const float x = rnd.nextFloat() * (float) w, y = rnd.nextFloat() * (float) h * 0.85f;
            juce::ColourGradient cg (juce::Colours::white.withAlpha (0.05f), x, y, juce::Colours::white.withAlpha (0.45f), x + r, y, true);
            g.setGradientFill (cg);
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillEllipse (x - r * 0.55f, y - r * 0.6f, r * 0.45f, r * 0.28f);
        }
        g.setTiledImageFill (makePinstripes (64, 64, 0.12f), 0, 0, 1.0f);
        g.fillAll();
    }

    void paintOceanWallpaper (juce::Graphics& g, int w, int h)
    {
        // deep sea: radial glow, flowing light currents, drifting particles
        g.setGradientFill (juce::ColourGradient (C (0xff0b4a8f), (float) w * 0.35f, (float) h * 0.3f,
                                                 C (0xff010614), (float) w * 1.1f, (float) h * 1.2f, true));
        g.fillAll();
        juce::Random rnd (1999);
        for (int k = 0; k < 9; ++k)
        {
            juce::Path wave;
            const float amp = (float) h * (0.04f + rnd.nextFloat() * 0.08f);
            const float yb = (float) h * (0.2f + rnd.nextFloat() * 0.7f);
            const float freq = 1.5f + rnd.nextFloat() * 2.5f, phase = rnd.nextFloat() * 6.28f;
            wave.startNewSubPath (0.0f, yb);
            for (int x = 0; x <= w; x += 6)
                wave.lineTo ((float) x, yb + amp * std::sin ((float) x / (float) w * freq * 6.28f + phase));
            const auto col = (k % 3 == 0) ? C (0xff7ff6ff) : (k % 3 == 1 ? C (0xff2f8cff) : C (0xff8a6dff));
            for (int glow = 4; glow >= 0; --glow)
            {
                g.setColour (col.withAlpha (glow == 0 ? 0.35f : 0.035f));
                g.strokePath (wave, juce::PathStrokeType (glow == 0 ? 1.2f : (float) glow * 6.0f));
            }
        }
        for (int i = 0; i < 220; ++i)
        {
            const float x = rnd.nextFloat() * (float) w, y = rnd.nextFloat() * (float) h, s = rnd.nextFloat() * 1.8f + 0.4f;
            g.setColour (C (0xffbff8ff).withAlpha (0.15f + rnd.nextFloat() * 0.5f));
            g.fillEllipse (x, y, s, s);
        }
    }

    void paintClubWallpaper (juce::Graphics& g, int w, int h)
    {
        // corporate chrome-blue: frosted gradient, lens flare, chrome bands
        g.setGradientFill (juce::ColourGradient (C (0xffb9cde3), 0.0f, 0.0f, C (0xff35567f), 0.0f, (float) h, false));
        g.fillAll();
        const float fx = (float) w * 0.18f, fy = (float) h * 0.12f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), fx, fy, juce::Colours::white.withAlpha (0.0f),
                                                 fx + (float) h * 0.5f, fy, true));
        g.fillAll();
        for (int i = 1; i < 6; ++i)
        {
            const float t = (float) i / 6.0f, r = 10.0f + (float) i * 14.0f;
            const float x = fx + ((float) w * 0.7f - fx) * t, y = fy + ((float) h * 0.6f - fy) * t;
            g.setColour (C (0xffd8f0ff).withAlpha (0.10f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            g.setColour (C (0xffffffff).withAlpha (0.18f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
        }
        for (int i = 0; i < 4; ++i)
        {
            const float y = (float) h * (0.3f + 0.17f * (float) i);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, y, juce::Colours::white.withAlpha (0.0f), (float) w, y, false));
            juce::ColourGradient band (juce::Colours::white.withAlpha (0.0f), 0.0f, y, juce::Colours::white.withAlpha (0.0f), (float) w, y, false);
            band.addColour (0.5, juce::Colours::white.withAlpha (0.18f));
            g.setGradientFill (band);
            g.fillRect (0.0f, y, (float) w, 2.0f);
        }
        g.setTiledImageFill (makeBrushed (256, 256, 0.05f, 0.99f, 3), 0, 0, 1.0f);
        g.fillAll();
    }
}

void Theme::drawWindowBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int w = juce::jmax (1, area.getWidth()), h = juce::jmax (1, area.getHeight());
    if (! windowImage.isValid() || windowKey != key() || windowSize.getWidth() != w || windowSize.getHeight() != h)
    {
        windowImage = juce::Image (juce::Image::RGB, w, h, false);
        windowKey = key();
        windowSize = area;
        juce::Graphics ig (windowImage);
        switch (skin)
        {
            case Skin::Aqua:     paintAquaWallpaper (ig, w, h); break;
            case Skin::Vaio:     paintOceanWallpaper (ig, w, h); break;
            case Skin::SoftClub: paintClubWallpaper (ig, w, h); break;
            case Skin::Win98:    ig.fillAll (C (0xff008080)); break;
            case Skin::SonyBlack:
                ig.setGradientFill (juce::ColourGradient (C (0xff1a1a1c), 0.0f, 0.0f, C (0xff030303), 0.0f, (float) h, false));
                ig.fillAll();
                ig.setTiledImageFill (makeGrain (256, 256, 0.08f, 9), 0, 0, 1.0f);
                ig.fillAll();
                break;
            case Skin::Gunmetal:
                ig.fillAll (base);
                ig.setTiledImageFill (makeBrushed (256, 256, day ? 0.07f : 0.05f, 0.985f, 13), 0, 0, 1.0f);
                ig.fillAll();
                break;
            case Skin::Flat:
            case Skin::count:
                ig.fillAll (base);
                break;
        }
    }
    g.drawImageAt (windowImage, area.getX(), area.getY());
}

// ===========================================================================
// Palettes
// ===========================================================================
void Theme::applySkin()
{
    switch (skin)
    {
        case Skin::Gunmetal:
            if (mono)
            {
                if (day)
                {
                    base = C (0xffb3b5b8); panel = C (0xffdcdde0); panelTop = C (0xffeff0f2); panelBottom = C (0xffc9cbce);
                    raised = C (0xffeceef0); line = C (0xff8c8f94); text = C (0xff111214); textDim = C (0xff55585d);
                    active = C (0xff2a2b2e); lcd = C (0xffd4d8dc); lcdText = C (0xff111214); accent2 = C (0xff5a5c60);
                }
                else
                {
                    base = C (0xff0b0b0c); panel = C (0xff1d1d1f); panelTop = C (0xff2b2b2e); panelBottom = C (0xff151517);
                    raised = C (0xff2f2f33); line = C (0xff46464b); text = C (0xffededee); textDim = C (0xff8f9095);
                    active = C (0xffdcdcdc); lcd = C (0xff060607); lcdText = C (0xfff2f2f2); accent2 = C (0xff9a9a9e);
                }
            }
            else if (day)
            {
                base = C (0xffaab2bb); panel = C (0xffdde2e7); panelTop = C (0xffeef1f4); panelBottom = C (0xffc6cdd4);
                raised = C (0xffeaedf0); line = C (0xff8a94a0); text = C (0xff14181d); textDim = C (0xff515b66);
                active = C (0xff2c7cc4); lcd = C (0xffcfdde6); lcdText = C (0xff0e2638); accent2 = C (0xffd0852a);
            }
            else
            {
                base = C (0xff0a0c0f); panel = C (0xff1c2026); panelTop = C (0xff2a3038); panelBottom = C (0xff14171b);
                raised = C (0xff2c323a); line = C (0xff3c444e); text = C (0xffe3eaf2); textDim = C (0xff8994a1);
                active = C (0xff3c95dc); lcd = C (0xff05070a); lcdText = C (0xffc4e4ff); accent2 = C (0xffe39a3c);
            }
            screenA = C (0xff07090c); screenB = C (0xff141a21);
            break;

        case Skin::Aqua:
            day = true;
            base = C (0xffcfe6f5); panel = C (0xffeef5fa); panelTop = C (0xfffbfdff); panelBottom = C (0xffd6e6f1);
            raised = C (0xffe9f1f7); line = C (0xff8fb0c8); text = C (0xff0b2a44); textDim = C (0xff4b6f8c);
            active = C (0xff1a8be6); lcd = C (0xffeaf6ff); lcdText = C (0xff083a68); accent2 = C (0xff3db84a);
            warn = C (0xffd8432c);
            screenA = C (0xff04213d); screenB = C (0xff0c3b66);
            break;

        case Skin::SonyBlack:
            day = false;
            base = C (0xff050505); panel = C (0xff151516); panelTop = C (0xff1f1f21); panelBottom = C (0xff0a0a0b);
            raised = C (0xff1b1b1d); line = C (0xff36363a); text = C (0xffd9dbde); textDim = C (0xff8a8d92);
            active = C (0xffff8c1a); lcd = C (0xff0b0702); lcdText = C (0xffffa940); accent2 = C (0xffc8ccd2);
            warn = C (0xffff4a2a);
            screenA = C (0xff000000); screenB = C (0xff0d0a06);
            break;

        case Skin::Vaio:
            day = false;
            base = C (0xff03102a); panel = C (0xff0b1d3d); panelTop = C (0xff16345f); panelBottom = C (0xff06142e);
            raised = C (0xff12305a); line = C (0xff2b5d92); text = C (0xffe2f6ff); textDim = C (0xff86b0d2);
            active = C (0xff32e2f2); lcd = C (0xff020a1c); lcdText = C (0xff8af6ff); accent2 = C (0xffd06cf0);
            screenA = C (0xff010818); screenB = C (0xff06204a);
            break;

        case Skin::Win98:
            day = true;
            base = C (0xff008080); panel = C (0xffc0c0c0); panelTop = C (0xffc0c0c0); panelBottom = C (0xffc0c0c0);
            raised = C (0xffc0c0c0); line = C (0xff808080); text = C (0xff000000); textDim = C (0xff404040);
            active = C (0xff000080); lcd = C (0xffffffff); lcdText = C (0xff000000); accent2 = C (0xff008080);
            warn = C (0xffc00000); meterOk = C (0xff00a000); meterHot = C (0xffc0c000);
            screenA = C (0xff000000); screenB = C (0xff000000);
            break;

        case Skin::SoftClub:
            day = true;
            base = C (0xff6d8db2); panel = C (0xffb7cbe0); panelTop = C (0xffe6eef7); panelBottom = C (0xff92aecb);
            raised = C (0xffd5e2ef); line = C (0xff4c6d93); text = C (0xff0a1d33); textDim = C (0xff35577c);
            active = C (0xff1677e6); lcd = C (0xffc4e7f6); lcdText = C (0xff04304f); accent2 = C (0xffe8f2ff);
            screenA = C (0xff061a33); screenB = C (0xff13365e);
            break;

        case Skin::Flat:
        case Skin::count:
            break;
    }
}

// ===========================================================================
// Shared drawing
// ===========================================================================
void Theme::drawBevel (juce::Graphics& g, juce::Rectangle<float> r, bool raisedBevel, bool thick)
{
    const auto lt = raisedBevel ? C (0xffffffff) : C (0xff000000);
    const auto dk = raisedBevel ? C (0xff000000) : C (0xffffffff);
    const auto lt2 = raisedBevel ? C (0xffdfdfdf) : C (0xff808080);
    const auto dk2 = raisedBevel ? C (0xff808080) : C (0xffdfdfdf);
    auto edge = [&] (juce::Rectangle<float> b, juce::Colour tl, juce::Colour br)
    {
        g.setColour (tl);
        g.fillRect (b.getX(), b.getY(), b.getWidth() - 1.0f, 1.0f);
        g.fillRect (b.getX(), b.getY(), 1.0f, b.getHeight() - 1.0f);
        g.setColour (br);
        g.fillRect (b.getX(), b.getBottom() - 1.0f, b.getWidth(), 1.0f);
        g.fillRect (b.getRight() - 1.0f, b.getY(), 1.0f, b.getHeight());
    };
    if (raisedBevel)
    {
        edge (r, lt2, dk);
        if (thick) edge (r.reduced (1.0f), lt, dk2);
    }
    else
    {
        edge (r, dk2, dk);   // sunken: grey / white outside
        if (thick) edge (r.reduced (1.0f), lt, lt2);
    }
}

void Theme::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float)
{
    switch (skin)
    {
        case Skin::Flat:
            g.setColour (panel);
            g.fillRect (r);
            return;

        case Skin::Win98:
            g.setColour (panel);
            g.fillRect (r);
            drawBevel (g, r, true);
            return;

        case Skin::Vaio:
        {
            // smoked glass over the wallpaper
            g.setGradientFill (juce::ColourGradient (panelTop.withAlpha (0.82f), r.getX(), r.getY(), panelBottom.withAlpha (0.9f), r.getX(), r.getBottom(), false));
            g.fillRect (r);
            tile (g, r);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 40.0f, false);
            g.setGradientFill (sheen);
            g.fillRect (r.withHeight (juce::jmin (40.0f, r.getHeight())));
            g.setColour (active.withAlpha (0.45f));
            g.drawRect (r, 1.0f);
            g.setColour (active.withAlpha (0.12f));
            g.drawRect (r.expanded (1.0f), 1.0f);
            return;
        }

        case Skin::Aqua:
        {
            g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBottom, r.getX(), r.getBottom(), false));
            g.fillRect (r);
            tile (g, r);
            g.setColour (juce::Colours::white);
            g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 1.0f);
            g.setColour (C (0xff6c90ad));
            g.drawRect (r, 1.0f);
            return;
        }

        case Skin::SonyBlack:
        {
            g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBottom, r.getX(), r.getBottom(), false));
            g.fillRect (r);
            tile (g, r);
            // a gloss strip along the top edge, like a moulded fascia
            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.09f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 14.0f, false);
            g.setGradientFill (gloss);
            g.fillRect (r.withHeight (juce::jmin (14.0f, r.getHeight())));
            g.setColour (C (0xff3a3a3e));
            g.drawRect (r, 1.0f);
            g.setColour (juce::Colours::black);
            g.drawRect (r.expanded (1.0f), 1.0f);
            return;
        }

        case Skin::Gunmetal:
        case Skin::SoftClub:
        case Skin::count:
            break;
    }

    g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBottom, r.getX(), r.getBottom(), false));
    g.fillRect (r);
    tile (g, r);
    if (skin == Skin::SoftClub)
    {
        // frosted highlight across the upper third
        juce::ColourGradient frost (juce::Colours::white.withAlpha (0.35f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.35f, false);
        g.setGradientFill (frost);
        g.fillRect (r);
    }
    g.setColour (juce::Colours::white.withAlpha (day ? 0.6f : 0.10f));
    g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (day ? 0.4f : 0.8f));
    g.drawRect (r, 1.0f);
}

void Theme::drawInset (juce::Graphics& g, juce::Rectangle<float> r, float)
{
    if (skin == Skin::Flat)
    {
        g.setColour (base);
        g.fillRect (r);
        return;
    }
    if (skin == Skin::Win98)
    {
        g.setColour (lcd);
        g.fillRect (r);
        drawBevel (g, r, false);
        return;
    }
    g.setGradientFill (juce::ColourGradient (lcd.darker (0.2f), r.getX(), r.getY(), lcd.brighter (day ? 0.04f : 0.12f), r.getX(), r.getBottom(), false));
    g.fillRect (r);
    if (skin == Skin::SonyBlack || skin == Skin::Vaio || skin == Skin::Gunmetal)
    {
        // LCD glass: faint scanlines and a reflection
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (float y = r.getY() + 1.0f; y < r.getBottom(); y += 2.0f) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
    juce::ColourGradient refl (juce::Colours::white.withAlpha (day ? 0.35f : 0.06f), r.getX(), r.getY(),
                               juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false);
    g.setGradientFill (refl);
    g.fillRect (r.withHeight (r.getHeight() * 0.5f));
    g.setColour (juce::Colours::black.withAlpha (day ? 0.45f : 0.9f));
    g.drawRect (r, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (day ? 0.6f : 0.07f));
    g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1.0f);
}

void Theme::drawWaveBackground (juce::Graphics& g, juce::Rectangle<float> r, bool vertical)
{
    if (! tech)
    {
        g.setColour (base);
        g.fillRect (r);
        return;
    }
    g.setColour (screenA);
    g.fillRect (r);
    juce::ColourGradient glass = vertical
        ? juce::ColourGradient (screenB.withAlpha (0.0f), r.getX(), 0.0f, screenB.withAlpha (0.0f), r.getRight(), 0.0f, false)
        : juce::ColourGradient (screenB.withAlpha (0.0f), 0.0f, r.getY(), screenB.withAlpha (0.0f), 0.0f, r.getBottom(), false);
    glass.addColour (0.5, screenB);
    g.setGradientFill (glass);
    g.fillRect (r);
}

// ===========================================================================
NEXTLOOPLookAndFeel::NEXTLOOPLookAndFeel()
{
    const bool t = Theme::tech;
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
    setColour (juce::ComboBox::backgroundColourId, t ? Theme::lcd : Theme::raised);
    setColour (juce::ComboBox::outlineColourId, Theme::line);
    setColour (juce::ComboBox::textColourId, t ? Theme::lcdText : Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::headerTextColourId, Theme::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t ? Theme::active : Theme::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, t ? (Theme::skin == Skin::Vaio ? C (0xff021022) : juce::Colours::white) : Theme::text);
    setColour (juce::ListBox::backgroundColourId, t ? Theme::lcd : Theme::panel);
    setColour (juce::ListBox::outlineColourId, Theme::line);
    setColour (juce::TextEditor::backgroundColourId, t ? Theme::lcd : Theme::raised);
    setColour (juce::TextEditor::textColourId, t ? Theme::lcdText : Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, t ? Theme::active : Theme::textDim);
    setColour (juce::TextEditor::highlightColourId, t ? Theme::active.withAlpha (0.5f) : Theme::line);
    setColour (juce::CaretComponent::caretColourId, t ? Theme::lcdText : Theme::text);
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
    setColour (juce::TooltipWindow::backgroundColourId, Theme::skin == Skin::Win98 ? C (0xffffffe1) : Theme::panel);
    setColour (juce::TooltipWindow::textColourId, Theme::skin == Skin::Win98 ? juce::Colours::black : Theme::text);

    // each skin speaks in the typeface of its era, if this computer has it
    juce::StringArray wanted;
    switch (Theme::skin)
    {
        case Skin::Win98:    wanted = { "Tahoma", "MS Sans Serif", "Verdana" }; break;
        case Skin::Aqua:     wanted = { "Lucida Grande", "Segoe UI", "Trebuchet MS" }; break;
        case Skin::SoftClub: wanted = { "Verdana", "Tahoma" }; break;
        case Skin::Vaio:     wanted = { "Trebuchet MS", "Segoe UI", "Verdana" }; break;
        case Skin::SonyBlack:wanted = { "Arial", "Helvetica" }; break;
        case Skin::Gunmetal:
        case Skin::Flat:
        case Skin::count:    break;
    }
    if (! wanted.isEmpty())
    {
        static const juce::StringArray installed = juce::Font::findAllTypefaceNames();
        for (auto& name : wanted)
            if (installed.contains (name, true))
            {
                setDefaultSansSerifTypefaceName (name);
                break;
            }
    }
}

// ---------------------------------------------------------------------------
// Knobs
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
    const auto skin = Theme::skin;

    // value arc
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    const auto trackCol = skin == Skin::Flat ? Theme::line
                        : skin == Skin::Win98 ? C (0xff808080)
                        : juce::Colours::black.withAlpha (Theme::day ? 0.3f : 0.8f);
    g.setColour (trackCol);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const auto col = s.isEnabled() ? accent : Theme::textDim;
        if (Theme::tech && skin != Skin::Win98)
        {
            g.setColour (col.withAlpha (0.25f));
            g.strokePath (val, juce::PathStrokeType (lineW * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    const float bodyR = arcR - lineW * 1.4f;
    auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    auto pointer = [&] (juce::Colour col, float width, float from01, float to01)
    {
        juce::Path p;
        p.addRectangle (-width * 0.5f, -bodyR * to01, width, bodyR * (to01 - from01));
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (col);
        g.fillPath (p);
    };

    switch (skin)
    {
        case Skin::Flat:
            g.setColour (Theme::raised);
            g.fillEllipse (body);
            pointer (Theme::text, lineW * 0.7f, 0.4f, 1.0f);
            return;

        case Skin::Win98:
        {
            g.setColour (C (0xffc0c0c0));
            g.fillEllipse (body);
            juce::Path hiArc, loArc;
            hiArc.addCentredArc (c.x, c.y, bodyR - 0.5f, bodyR - 0.5f, 0.0f, -2.4f, 0.8f, true);
            loArc.addCentredArc (c.x, c.y, bodyR - 0.5f, bodyR - 0.5f, 0.0f, 0.8f, 3.9f, true);
            g.setColour (juce::Colours::white); g.strokePath (hiArc, juce::PathStrokeType (1.5f));
            g.setColour (C (0xff404040));       g.strokePath (loArc, juce::PathStrokeType (1.5f));
            const auto dot = c.getPointOnCircumference (bodyR * 0.62f, angle);
            g.setColour (juce::Colours::black);
            g.fillRect (juce::Rectangle<float> (3.0f, 3.0f).withCentre (dot));
            return;
        }

        case Skin::Aqua:
        {
            // glass marble: coloured core, bright specular cap, soft refraction at the bottom
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillEllipse (body.translated (0.0f, 1.5f));
            g.setGradientFill (juce::ColourGradient (C (0xff9fd8ff), c.x, body.getY(), C (0xff0f5fb3), c.x, body.getBottom(), false));
            g.fillEllipse (body);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), c.x, body.getBottom() - bodyR * 0.2f,
                                                     C (0xffbff0ff).withAlpha (0.7f), c.x, body.getBottom(), false));
            g.fillEllipse (body);
            auto spec = body.reduced (bodyR * 0.22f, 0.0f).withHeight (bodyR * 0.85f).translated (0.0f, bodyR * 0.08f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.9f), c.x, spec.getY(), juce::Colours::white.withAlpha (0.1f), c.x, spec.getBottom(), false));
            g.fillEllipse (spec);
            g.setColour (C (0xff0a3d73));
            g.drawEllipse (body, 1.0f);
            pointer (juce::Colours::white, juce::jmax (2.0f, lineW * 0.6f), 0.35f, 0.95f);
            return;
        }

        case Skin::SonyBlack:
        {
            // knurled black cap with a silver index line
            g.setColour (juce::Colours::black);
            g.fillEllipse (body.expanded (1.0f));
            g.setGradientFill (juce::ColourGradient (C (0xff38383c), c.x - bodyR * 0.5f, c.y - bodyR * 0.7f, C (0xff070708), c.x + bodyR * 0.6f, c.y + bodyR, true));
            g.fillEllipse (body);
            for (int i = 0; i < 36; ++i)
            {
                const float a = (float) i / 36.0f * juce::MathConstants<float>::twoPi + angle;
                g.setColour (juce::Colours::white.withAlpha (i % 2 ? 0.05f : 0.13f));
                g.drawLine ({ c.getPointOnCircumference (bodyR * 0.82f, a), c.getPointOnCircumference (bodyR - 0.5f, a) }, 1.0f);
            }
            auto cap = body.reduced (bodyR * 0.22f);
            g.setGradientFill (juce::ColourGradient (C (0xff2a2a2e), c.x, cap.getY(), C (0xff0c0c0d), c.x, cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawEllipse (cap, 1.0f);
            pointer (C (0xffd9dcdf), juce::jmax (1.5f, lineW * 0.45f), 0.25f, 0.95f);
            return;
        }

        case Skin::Vaio:
        {
            // smoked glass dome with a cyan light ring
            g.setColour (Theme::active.withAlpha (0.25f));
            g.fillEllipse (body.expanded (2.0f));
            g.setGradientFill (juce::ColourGradient (C (0xff27507f), c.x, body.getY(), C (0xff04101f), c.x, body.getBottom(), false));
            g.fillEllipse (body);
            auto spec = body.reduced (bodyR * 0.25f, 0.0f).withHeight (bodyR * 0.7f).translated (0.0f, bodyR * 0.1f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), c.x, spec.getY(), juce::Colours::white.withAlpha (0.0f), c.x, spec.getBottom(), false));
            g.fillEllipse (spec);
            g.setColour (Theme::active.withAlpha (0.8f));
            g.drawEllipse (body, 1.0f);
            pointer (Theme::active.brighter (0.4f), juce::jmax (2.0f, lineW * 0.55f), 0.3f, 0.95f);
            return;
        }

        case Skin::Gunmetal:
        case Skin::SoftClub:
        case Skin::count:
        {
            // machined metal cap
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (body.translated (0.0f, 1.5f).expanded (1.0f));
            const bool club = skin == Skin::SoftClub;
            const auto hi = club ? C (0xffffffff) : (Theme::day ? C (0xfff7f8fa) : C (0xff6c7480));
            const auto lo = club ? C (0xff7f9bbd) : (Theme::day ? C (0xffa9b0b8) : C (0xff1c2026));
            g.setGradientFill (juce::ColourGradient (hi, c.x - bodyR * 0.6f, c.y - bodyR * 0.8f, lo, c.x + bodyR * 0.5f, c.y + bodyR, true));
            g.fillEllipse (body);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (body, 1.0f);
            auto top = body.reduced (bodyR * 0.28f);
            g.setGradientFill (juce::ColourGradient (lo.brighter (0.3f), top.getX(), top.getY(), hi.darker (0.15f), top.getRight(), top.getBottom(), false));
            g.fillEllipse (top);
            pointer (Theme::day ? C (0xff111418) : juce::Colours::white, lineW * 0.64f, 0.38f, 0.98f);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Faders
// ---------------------------------------------------------------------------
static void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, juce::Colour accent, bool enabled)
{
    const auto col = enabled ? accent : Theme::textDim;
    auto indicator = vertical ? cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 2.0f)
                              : cap.withSizeKeepingCentre (2.0f, cap.getHeight() - 6.0f);
    switch (Theme::skin)
    {
        case Skin::Flat:
            g.setColour (Theme::raised.brighter (0.15f));
            g.fillRect (cap);
            g.setColour (col);
            g.fillRect (indicator);
            return;

        case Skin::Win98:
            g.setColour (C (0xffc0c0c0));
            g.fillRect (cap);
            Theme::drawBevel (g, cap, true);
            g.setColour (juce::Colours::black);
            g.fillRect (indicator.withSizeKeepingCentre (indicator.getWidth() - 4.0f, 1.0f));
            return;

        case Skin::Aqua:
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (cap.translated (0.0f, 1.5f));
            g.setGradientFill (juce::ColourGradient (C (0xff1d76c9), 0.0f, cap.getY(), C (0xff7fd0ff), 0.0f, cap.getBottom(), false));
            g.fillRect (cap);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), 0.0f, cap.getY(), juce::Colours::white.withAlpha (0.15f), 0.0f, cap.getCentreY(), false));
            g.fillRect (cap.reduced (1.5f, 1.0f).withHeight (cap.getHeight() * 0.45f));
            g.setColour (C (0xff0b3f75));
            g.drawRect (cap, 1.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (indicator);
            return;
        }

        case Skin::SonyBlack:
            g.setColour (juce::Colours::black);
            g.fillRect (cap.expanded (1.0f).translated (0.0f, 1.0f));
            g.setGradientFill (juce::ColourGradient (C (0xff35353a), 0.0f, cap.getY(), C (0xff0a0a0b), 0.0f, cap.getBottom(), false));
            g.fillRect (cap);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (cap.getX(), cap.getY(), cap.getWidth(), 1.0f);
            g.setColour (col);
            g.fillRect (indicator);
            return;

        case Skin::Vaio:
            g.setColour (Theme::active.withAlpha (0.25f));
            g.fillRect (cap.expanded (2.0f));
            g.setGradientFill (juce::ColourGradient (C (0xff2c5a8c).withAlpha (0.95f), 0.0f, cap.getY(), C (0xff061629), 0.0f, cap.getBottom(), false));
            g.fillRect (cap);
            g.setColour (Theme::active);
            g.drawRect (cap, 1.0f);
            g.fillRect (indicator);
            return;

        case Skin::Gunmetal:
        case Skin::SoftClub:
        case Skin::count:
            break;
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (cap.translated (0.0f, 2.0f));
    const bool club = Theme::skin == Skin::SoftClub;
    const auto hi = club || Theme::day ? C (0xfffbfcfd) : C (0xff767f8b);
    const auto lo = club ? C (0xff6d8db2) : (Theme::day ? C (0xff9aa2ab) : C (0xff22272e));
    juce::ColourGradient grad = vertical ? juce::ColourGradient (hi, 0.0f, cap.getY(), lo, 0.0f, cap.getBottom(), false)
                                         : juce::ColourGradient (hi, cap.getX(), 0.0f, lo, cap.getRight(), 0.0f, false);
    grad.addColour (0.5, lo.brighter (0.2f));
    grad.addColour (0.52, hi.darker (0.1f));
    g.setGradientFill (grad);
    g.fillRect (cap);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRect (cap, 1.0f);
    g.setColour (col.withAlpha (0.35f));
    g.fillRect (indicator.expanded (1.0f));
    g.setColour (col);
    g.fillRect (indicator);
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
        if (Theme::skin == Skin::Win98)
        {
            g.setColour (C (0xffc0c0c0));
            g.fillRect (sr);
            Theme::drawBevel (g, sr.expanded (1.0f), false);
        }
        else if (Theme::tech)
        {
            g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.6f : 0.9f));
            g.fillRect (sr);
            g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.6f : 0.08f));
            if (vertical) g.fillRect (sr.getRight(), sr.getY(), 1.0f, sr.getHeight());
            else          g.fillRect (sr.getX(), sr.getBottom(), sr.getWidth(), 1.0f);
        }
        else
        {
            g.setColour (Theme::base);
            g.fillRect (sr);
        }
    };

    const auto tickCol = Theme::skin == Skin::Win98 ? juce::Colours::black : (Theme::tech ? Theme::textDim.withAlpha (0.6f) : Theme::line);
    const auto centreCol = Theme::tech ? Theme::active.withAlpha (0.8f) : Theme::textDim;

    if (vertical)
    {
        const float cx = r.getCentreX();
        slot ({ cx - 2.0f, r.getY(), 4.0f, r.getHeight() });
        g.setColour (tickCol);
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = r.getY() + r.getHeight() * (float) i / 10.0f;
            const float tw = (i % 5 == 0) ? 7.0f : 4.0f;
            g.fillRect (cx - 5.0f - tw, ty, tw, 1.0f);
            if (big) g.fillRect (cx + 5.0f, ty, tw, 1.0f);
        }
        if (bipolar)
        {
            g.setColour (centreCol);
            g.fillRect (cx - 13.0f, r.getCentreY(), 26.0f, 1.0f);
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
            g.setColour (centreCol);
            g.fillRect (r.getCentreX(), cy - 9.0f, 1.0f, 18.0f);
        }
        const float capW = juce::jmin (22.0f, r.getWidth() * 0.15f);
        const float capH = juce::jmin (r.getHeight() - 2.0f, 26.0f);
        drawFaderCap (g, juce::Rectangle<float> (capW, capH).withCentre ({ pos, cy }), false, accent, s.isEnabled());
    }
}

// ---------------------------------------------------------------------------
// Buttons (keys)
// ---------------------------------------------------------------------------
void NEXTLOOPLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    auto fill = on ? onColour : b.findColour (juce::TextButton::buttonColourId);
    const bool tinted = (bool) b.getProperties().getWithDefault ("tinted", false);
    const float dim = b.isEnabled() ? 1.0f : 0.45f;

    switch (Theme::skin)
    {
        case Skin::Flat:
        {
            r = r.reduced (0.5f);
            if (down) fill = fill.brighter (0.2f);
            else if (highlighted) fill = fill.brighter (0.07f);
            g.setColour (fill.withMultipliedAlpha (dim));
            g.fillRect (r);
            if (! on)
            {
                g.setColour (tinted ? onColour.withAlpha (0.55f) : Theme::line);
                g.drawRect (r, 1.0f);
            }
            return;
        }

        case Skin::Win98:
        {
            const bool sunken = on || down;
            g.setColour (C (0xffc0c0c0));
            g.fillRect (r);
            if (on)
            {
                // the classic "latched" dither
                static const juce::Image checker = []
                {
                    juce::Image im (juce::Image::ARGB, 2, 2, true);
                    im.setPixelAt (0, 0, juce::Colours::white);
                    im.setPixelAt (1, 1, juce::Colours::white);
                    return im;
                }();
                g.setTiledImageFill (checker, 0, 0, 1.0f);
                g.fillRect (r.reduced (2.0f));
                if (onColour != Theme::active)   // deck-coloured keys show their colour as a bar
                {
                    g.setColour (onColour);
                    g.fillRect (r.getX() + 3.0f, r.getBottom() - 5.0f, r.getWidth() - 6.0f, 2.0f);
                }
            }
            Theme::drawBevel (g, r, ! sunken);
            if (highlighted && ! sunken)
            {
                g.setColour (juce::Colours::black.withAlpha (0.06f));
                g.fillRect (r.reduced (2.0f));
            }
            return;
        }

        case Skin::Aqua:
        {
            // wet gel key
            if (! on) fill = C (0xffe6eef5);
            if (down) fill = fill.darker (0.12f);
            fill = fill.withMultipliedAlpha (dim);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect (r.translated (0.0f, 1.0f));
            r = r.reduced (0.5f);
            g.setGradientFill (juce::ColourGradient (fill.darker (on ? 0.1f : 0.04f), 0.0f, r.getY(), fill.brighter (on ? 0.45f : 0.06f), 0.0f, r.getBottom(), false));
            g.fillRect (r);
            auto glossR = r.reduced (1.5f, 1.0f).withHeight (r.getHeight() * 0.48f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.8f : 0.95f), 0.0f, glossR.getY(),
                                                     juce::Colours::white.withAlpha (on ? 0.18f : 0.35f), 0.0f, glossR.getBottom(), false));
            g.fillRect (glossR);
            if (highlighted) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.fillRect (r); }
            g.setColour (on ? onColour.darker (0.55f) : C (0xff7a98b0));
            g.drawRect (r, 1.0f);
            if (tinted && ! on)
            {
                g.setColour (onColour.withAlpha (0.8f));
                g.fillRect (r.getX() + 2.0f, r.getBottom() - 3.0f, r.getWidth() - 4.0f, 2.0f);
            }
            return;
        }

        case Skin::SonyBlack:
        {
            // black keys; the state shows as an LED along the top edge and lit print
            g.setColour (juce::Colours::black);
            g.fillRect (r);
            auto face = r.reduced (1.0f);
            g.setGradientFill (juce::ColourGradient (C (down ? 0xff141416 : 0xff2a2a2d), 0.0f, face.getY(), C (0xff0d0d0e), 0.0f, face.getBottom(), false));
            g.fillRect (face);
            g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.22f : 0.13f));
            g.fillRect (face.getX(), face.getY(), face.getWidth(), 1.0f);
            const auto led = on ? onColour : (tinted ? onColour.withAlpha (0.18f) : C (0xff2a1a0a));
            auto ledR = face.reduced (3.0f, 0.0f).withY (face.getY() + 2.0f).withHeight (2.0f);
            if (on)
            {
                g.setColour (onColour.withAlpha (0.3f));
                g.fillRect (ledR.expanded (1.5f, 1.5f));
            }
            g.setColour (led.withMultipliedAlpha (dim));
            g.fillRect (ledR);
            return;
        }

        case Skin::Vaio:
        {
            if (on)
            {
                g.setColour (onColour.withAlpha (0.25f));
                g.fillRect (r);
            }
            r = r.reduced (1.0f);
            const auto top = on ? onColour.withAlpha (0.75f) : C (0xff1d3f6c).withAlpha (0.9f);
            const auto bot = on ? onColour.darker (0.5f).withAlpha (0.85f) : C (0xff061428).withAlpha (0.95f);
            g.setGradientFill (juce::ColourGradient (down ? bot : top, 0.0f, r.getY(), down ? top : bot, 0.0f, r.getBottom(), false));
            g.fillRect (r);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.35f : 0.16f), 0.0f, r.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
            g.fillRect (r.withHeight (r.getHeight() * 0.5f));
            g.setColour ((on ? onColour.brighter (0.4f) : (tinted ? onColour.withAlpha (0.6f) : Theme::active.withAlpha (highlighted ? 0.7f : 0.35f))).withMultipliedAlpha (dim));
            g.drawRect (r, 1.0f);
            return;
        }

        case Skin::Gunmetal:
        case Skin::SoftClub:
        case Skin::count:
            break;
    }

    // Gunmetal / Soft Club: machined keys
    const bool club = Theme::skin == Skin::SoftClub;
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.45f);
    if (down) fill = fill.darker (0.15f);
    else if (highlighted) fill = fill.brighter (0.08f);

    g.setColour (juce::Colours::black.withAlpha (Theme::day ? 0.25f : 0.6f));
    g.fillRect (r.translated (0.0f, 1.0f));
    r = r.reduced (0.5f);
    if (on)
    {
        g.setColour (onColour.withAlpha (0.28f));
        g.fillRect (r.expanded (1.5f));
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.35f), 0.0f, r.getY(), fill.darker (0.3f), 0.0f, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillRect (r.withHeight (r.getHeight() * 0.45f).reduced (1.5f, 1.0f));
    }
    else
    {
        juce::ColourGradient grad (club ? C (0xfff7fbff) : (Theme::day ? fill.brighter (0.25f) : fill.brighter (0.18f)), 0.0f, r.getY(),
                                   club ? C (0xff9db6d1) : (Theme::day ? fill.darker (0.12f) : fill.darker (0.45f)), 0.0f, r.getBottom(), false);
        if (club) grad.addColour (0.5, C (0xffd2e0ee));
        g.setGradientFill (grad);
        g.fillRect (r);
        g.setColour (juce::Colours::white.withAlpha (Theme::day ? 0.6f : 0.12f));
        g.fillRect (r.getX() + 1.0f, r.getY() + 1.0f, r.getWidth() - 2.0f, 1.0f);
        if (tinted)
        {
            g.setColour (onColour.withAlpha (0.6f));
            g.fillRect (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getWidth() - 6.0f, 1.5f);
        }
    }
    g.setColour (club ? C (0xff3a5a80) : juce::Colours::black.withAlpha (Theme::day ? 0.45f : 0.85f));
    g.drawRect (r, 1.0f);
}

void NEXTLOOPLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool on = b.getToggleState();
    const auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    juce::Colour col = Theme::text;
    auto area = b.getLocalBounds().reduced (2, 1);

    switch (Theme::skin)
    {
        case Skin::Win98:
            col = juce::Colours::black;
            if (on || down) area.translate (1, 1);
            break;
        case Skin::SonyBlack:
            col = on ? onColour.brighter (0.3f) : C (0xffcfd2d6);
            area.translate (0, 1);
            break;
        case Skin::Aqua:
            col = on ? juce::Colours::white : C (0xff0b2a44);
            break;
        case Skin::Vaio:
            col = on ? C (0xffffffff) : Theme::text;
            break;
        case Skin::SoftClub:
            col = on ? juce::Colours::white : C (0xff0a1d33);
            break;
        case Skin::Gunmetal:
            col = on ? (onColour.getPerceivedBrightness() > 0.6f ? C (0xff0b0d10) : juce::Colours::white) : Theme::text;
            break;
        case Skin::Flat:
        case Skin::count:
        {
            const auto light = Theme::text.getPerceivedBrightness() > Theme::base.getPerceivedBrightness() ? Theme::text : Theme::base;
            const auto dark  = light == Theme::text ? Theme::base : Theme::text;
            col = on ? (onColour.getPerceivedBrightness() > 0.55f ? dark : light) : Theme::text;
            break;
        }
    }
    if (! b.isEnabled()) col = col.withMultipliedAlpha (0.5f);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    if (Theme::skin == Skin::Aqua && on)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawFittedText (b.getButtonText(), area.translated (0, 1), juce::Justification::centred, 2, 0.75f);
    }
    if (Theme::skin == Skin::Win98 && ! b.isEnabled())
    {
        g.setColour (juce::Colours::white);   // etched
        g.drawFittedText (b.getButtonText(), area.translated (1, 1), juce::Justification::centred, 2, 0.75f);
        col = C (0xff808080);
    }
    g.setColour (col);
    g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 2, 0.75f);
}

juce::Font NEXTLOOPLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    const float scale = (float) (double) b.getProperties().getWithDefault ("fontScale", 1.0);
    const bool bold = Theme::skin != Skin::Win98;
    return Theme::label (juce::jlimit (9.5f, 15.0f, (float) buttonHeight * 0.48f) * scale, bold);
}

juce::Font NEXTLOOPLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void NEXTLOOPLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    juce::Path arrow;
    const float ax = (float) w - 9.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.0f);

    if (Theme::skin == Skin::Win98)
    {
        g.setColour (juce::Colours::white);
        g.fillRect (r);
        Theme::drawBevel (g, r, false);
        auto btn = r.reduced (2.0f).removeFromRight (16.0f);
        g.setColour (C (0xffc0c0c0));
        g.fillRect (btn);
        Theme::drawBevel (g, btn, true);
        g.setColour (juce::Colours::black);
        juce::Path a2;
        const auto bc = btn.getCentre();
        a2.addTriangle (bc.x - 3.5f, bc.y - 1.5f, bc.x + 3.5f, bc.y - 1.5f, bc.x, bc.y + 2.0f);
        g.fillPath (a2);
        return;
    }
    if (Theme::tech)
        Theme::drawInset (g, r);
    else
    {
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (r);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRect (r, 1.0f);
    }
    g.setColour (Theme::tech ? Theme::active : box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void NEXTLOOPLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - (Theme::skin == Skin::Win98 ? 20 : 16), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font NEXTLOOPLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::label (juce::jlimit (10.0f, 15.0f, (float) box.getHeight() * 0.55f), Theme::tech && Theme::skin != Skin::Win98);
}

void NEXTLOOPLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    if (Theme::skin == Skin::Win98)
    {
        g.fillAll (C (0xffc0c0c0));
        Theme::drawBevel (g, r, true);
        return;
    }
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (Theme::line);
    g.drawRect (r, 1.0f);
}

} // namespace loopdeck
