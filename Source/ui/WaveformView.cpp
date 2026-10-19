#include "WaveformView.h"

namespace loopdeck
{

static void loadFromDragDescription (const juce::var& description, int deck, const LoadFileFn& fn)
{
    const auto path = description.toString();
    if (path.startsWith ("file:") && fn)
        fn (deck, juce::File (path.fromFirstOccurrenceOf ("file:", false, false)));
}

// ===========================================================================
ScrollingWaveform::ScrollingWaveform (AudioEngine& e, int d, LoadFileFn fn)
    : engine (e), deck (d), loadFile (std::move (fn))
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

/** Cheap fingerprint of everything a waveform view draws. */
static juce::uint64 viewSignature (AudioEngine& engine, int deck, double quantum, double extra)
{
    auto t = engine.getTrack (deck);
    auto& d = engine.getDeck (deck);
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (double v) { juce::uint64 bits; std::memcpy (&bits, &v, sizeof bits); h = (h ^ bits) * 1099511628211ull; };
    mix ((double) (juce::pointer_sized_int) t.get());
    mix (extra);
    if (t != nullptr)
    {
        mix (std::floor (d.uiPosition.load() / juce::jmax (1.0, quantum)));
        mix ((double) t->binsReady.load());
        mix ((double) t->framesReady.load());
        mix (d.uiLoopStart.load()); mix (d.uiLoopEnd.load()); mix (d.uiLoopActive.load() ? 1.0 : 0.0);
        mix (d.uiSlipActive.load() ? d.uiSlipPosition.load() : -1.0);
        mix (t->cuePoint.load());
        for (auto& c : t->hotCues) mix (c.load());
        mix (t->grid.bpm.load()); mix (t->grid.anchorFrame.load());
        mix (d.uiPlaying.load() ? 1.0 : 0.0);
    }
    return h;
}

void ScrollingWaveform::refresh()
{
    auto t = engine.getTrack (deck);
    const double quantum = t != nullptr ? framesPerPixel (*t) * 0.5 : 1.0;   // half a pixel
    const auto sig = viewSignature (engine, deck, quantum, secondsVisible + (vertical ? 1000.0 : 0.0) + getWidth() * 7.0 + getHeight() * 13.0);
    if (sig != lastSignature) { lastSignature = sig; repaint(); }
}

void OverviewWaveform::refresh()
{
    auto t = engine.getTrack (deck);
    const double total = t != nullptr ? (double) juce::jmax ((juce::int64) 1, t->totalFrames.load()) : 1.0;
    const double quantum = total / juce::jmax (1, getWidth());   // one pixel
    // the end-of-track warning blinks, so include the blink phase while it is relevant
    double blink = 0.0;
    if (t != nullptr)
    {
        auto& d = engine.getDeck (deck);
        if (d.uiPlaying.load() && (total - d.uiPosition.load()) / t->sampleRate < 30.0)
            blink = (double) ((juce::Time::getMillisecondCounter() / 400) % 2);
    }
    const auto sig = viewSignature (engine, deck, quantum, blink + getWidth() * 7.0 + getHeight() * 13.0);
    if (sig != lastSignature) { lastSignature = sig; repaint(); }
}

double ScrollingWaveform::framesPerPixel (const Track& t) const
{
    const int len = vertical ? getHeight() : getWidth();
    return secondsVisible * t.sampleRate / juce::jmax (1, len);
}

void ScrollingWaveform::paint (juce::Graphics& g)
{
    const auto hue = Theme::deck (deck);
    g.fillAll (Theme::base);

    auto track = engine.getTrack (deck);
    const float w = (float) getWidth(), h = (float) getHeight();

    if (track == nullptr)
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::label (13.0f));
        g.drawFittedText ("Drop a track here to load deck " + juce::String (deck + 1),
                          getLocalBounds().reduced (6), juce::Justification::centred, 3);
        g.setColour (hue.withAlpha (0.5f));
        g.fillRect (vertical ? juce::Rectangle<float> (0, 0, w, 2) : juce::Rectangle<float> (0, 0, 3, h));
        return;
    }

    auto& d = engine.getDeck (deck);
    const Track& t = *track;
    const double pos = d.uiPosition.load();
    const double fpp = framesPerPixel (t);
    const int L = (int) (vertical ? h : w);
    const float C = vertical ? w : h;
    const float ph = vertical ? h * 0.62f : w * 0.5f;

    auto pixOf = [&] (double frame) { return vertical ? ph - (float) ((frame - pos) / fpp) : ph + (float) ((frame - pos) / fpp); };
    auto frameAtPix = [&] (float p) { return vertical ? pos + (ph - p) * fpp : pos + (p - ph) * fpp; };

    const auto bins = t.binsReady.load (std::memory_order_acquire);

    // --- loop region (under the waveform) ---------------------------------
    if (d.uiLoopActive.load() || d.uiLoopEnd.load() > d.uiLoopStart.load())
    {
        const float a = pixOf (d.uiLoopStart.load()), b = pixOf (d.uiLoopEnd.load());
        const bool active = d.uiLoopActive.load();
        auto r = vertical ? juce::Rectangle<float> (0, juce::jmin (a, b), w, std::abs (b - a))
                          : juce::Rectangle<float> (juce::jmin (a, b), 0, std::abs (b - a), h);
        g.setColour (hue.withAlpha (active ? 0.20f : 0.07f));
        g.fillRect (r);
    }

    // --- waveform -------------------------------------------------------------
    // Drawn once into an off-screen strip three screens long, then slid into place
    // each frame. It is only redrawn when the view leaves the strip, the zoom or
    // size changes, or newly decoded audio lands inside it.
    {
        double fA = frameAtPix (0.0f), fB = frameAtPix ((float) L);
        if (fA > fB) std::swap (fA, fB);
        const double cacheEndFrame = cacheStart + cacheLen * cacheFpp;
        const bool decodedIntoCache = bins != cacheBins && (double) cacheBins * Track::framesPerBin < cacheEndFrame;
        const bool valid = cache.isValid() && cacheTrack == &t && std::abs (cacheFpp - fpp) < 1e-9
                           && cacheVertical == vertical && cacheCross == (int) C && cacheTheme == Theme::key()
                           && fA >= cacheStart && fB <= cacheEndFrame && ! decodedIntoCache;
        if (! valid)
            rebuildCache (t, fA - L * fpp, 3 * L, fpp, (int) C, bins);

        g.setOpacity (1.0f);   // images inherit the current colour's alpha otherwise
        if (vertical)
            g.drawImageAt (cache, 0, (int) std::lround (pixOf (cacheStart) - (float) (cacheLen - 1)));
        else
            g.drawImageAt (cache, (int) std::lround (pixOf (cacheStart)), 0);
    }

    // not yet decoded
    if (! t.decodeComplete.load())
    {
        const float p = pixOf ((double) t.framesReady.load());
        g.setColour (Theme::textDim.withAlpha (0.25f));
        if (vertical) { if (p > 0) g.fillRect (0.0f, 0.0f, w, p); }
        else if (p < w) g.fillRect (p, 0.0f, w - p, h);
    }

    // --- beat grid ------------------------------------------------------------
    if (t.grid.isValid())
    {
        const int bpb = juce::jmax (1, t.grid.beatsPerBar.load());
        double fa = frameAtPix (0.0f), fb = frameAtPix ((float) L);
        if (fa > fb) std::swap (fa, fb);
        const double firstBeat = std::ceil (t.grid.beatAt (fa, t.sampleRate));
        const double lastBeat = t.grid.beatAt (fb, t.sampleRate);
        const double pxPerBeat = t.grid.framesPerBeat (t.sampleRate) / fpp;
        g.setFont (Theme::label (10.0f, true));
        for (double b = firstBeat; b <= lastBeat; b += 1.0)
        {
            const auto bi = (long long) b;
            const bool isBar = ((bi % bpb) + bpb) % bpb == 0;
            if (! isBar && pxPerBeat < 6.0) continue;
            const float p = pixOf (t.grid.frameOfBeat (b, t.sampleRate));
            g.setColour (isBar ? Theme::text.withAlpha (0.55f) : Theme::text.withAlpha (0.16f));
            if (vertical) g.fillRect (0.0f, p, w, isBar ? 1.5f : 1.0f);
            else          g.fillRect (p, 0.0f, isBar ? 1.5f : 1.0f, h);

            if (isBar && pxPerBeat * bpb > 40.0)
            {
                const auto barNum = juce::String (bi / bpb + (bi >= 0 ? 1 : 0));
                g.setColour (Theme::textDim);
                if (vertical) g.drawText (barNum, 2, (int) p - 12, 30, 12, juce::Justification::bottomLeft);
                else          g.drawText (barNum, (int) p + 3, 1, 30, 11, juce::Justification::topLeft);
            }
        }
    }

    // --- loop edges -------------------------------------------------------------
    if (d.uiLoopEnd.load() > d.uiLoopStart.load())
    {
        g.setColour (d.uiLoopActive.load() ? hue : hue.withAlpha (0.4f));
        for (double f : { d.uiLoopStart.load(), d.uiLoopEnd.load() })
        {
            const float p = pixOf (f);
            if (vertical) g.fillRect (0.0f, p - 1.0f, w, 2.0f);
            else          g.fillRect (p - 1.0f, 0.0f, 2.0f, h);
        }
    }

    // --- cue + hot cues -------------------------------------------------------
    {
        const float p = pixOf (t.cuePoint.load());
        juce::Path tri;
        if (vertical) tri.addTriangle (0, p - 5, 0, p + 5, 7, p);
        else          tri.addTriangle (p - 5, h, p + 5, h, p, h - 7);
        g.setColour (Theme::text);
        g.fillPath (tri);
    }
    g.setFont (Theme::label (10.0f, true));
    for (int i = 0; i < numHotCues; ++i)
    {
        const double hc = t.hotCues[(size_t) i].load();
        if (hc < 0.0) continue;
        const float p = pixOf (hc);
        if (p < -20.0f || p > (float) L + 20.0f) continue;
        g.setColour (hue);
        if (vertical)
        {
            g.fillRect (0.0f, p, w, 1.0f);
            g.fillRoundedRectangle (w - 15.0f, p - 7.0f, 14.0f, 14.0f, 2.0f);
            g.setColour (Theme::base);
            g.drawText (juce::String (i + 1), (int) w - 15, (int) p - 7, 14, 14, juce::Justification::centred);
        }
        else
        {
            g.fillRect (p, 0.0f, 1.0f, h);
            g.fillRoundedRectangle (p, 0.0f, 14.0f, 14.0f, 2.0f);
            g.setColour (Theme::base);
            g.drawText (juce::String (i + 1), (int) p, 0, 14, 14, juce::Justification::centred);
        }
    }

    // --- slip ghost -------------------------------------------------------------
    if (d.uiSlipActive.load())
    {
        const float p = pixOf (d.uiSlipPosition.load());
        g.setColour (Theme::text.withAlpha (0.45f));
        if (vertical) { for (float x = 0; x < w; x += 6) g.fillRect (x, p, 3.0f, 1.0f); }
        else          { for (float y = 0; y < h; y += 6) g.fillRect (p, y, 1.0f, 3.0f); }
    }

    // --- playhead -------------------------------------------------------------
    g.setColour (Theme::text);
    if (vertical) g.fillRect (0.0f, ph - 1.0f, w, 2.0f);
    else          g.fillRect (ph - 1.0f, 0.0f, 2.0f, h);

    // deck colour edge so stacked rows are easy to tell apart
    g.setColour (hue);
    if (vertical) g.fillRect (0.0f, 0.0f, w, 2.0f);
    else          g.fillRect (0.0f, 0.0f, 3.0f, h);
}

void ScrollingWaveform::rebuildCache (const Track& t, double startFrame, int length, double fpp, int cross, juce::int64 bins)
{
    const int w = vertical ? cross : length, h = vertical ? length : cross;
    if (! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
        cache = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    else
        cache.clear (cache.getBounds());

    juce::Graphics g (cache);
    const float centre = cross * 0.5f, half = cross * 0.47f;
    const WaveBin* wb = t.waveform.data();
    const double fpb = Track::framesPerBin;
    lowRects.clear(); midRects.clear(); highRects.clear();

    for (int i = 0; i < length; ++i)
    {
        const double f0 = startFrame + i * fpp, f1 = f0 + fpp;
        auto b0 = (juce::int64) std::floor (f0 / fpb);
        auto b1 = juce::jmax (b0 + 1, (juce::int64) std::floor (f1 / fpb));
        if (b1 <= 0 || b0 >= bins) continue;
        b0 = juce::jmax ((juce::int64) 0, b0);
        b1 = juce::jmin (bins, b1);

        int lo = 0, mi = 0, hi = 0;
        for (auto b = b0; b < b1; ++b)
        {
            lo = juce::jmax (lo, (int) wb[b].low);
            mi = juce::jmax (mi, (int) wb[b].mid);
            hi = juce::jmax (hi, (int) wb[b].high);
        }

        const float row = (float) (length - 1 - i);   // vertical: later audio sits higher
        auto bar = [&] (juce::RectangleList<float>& list, int v, float gain)
        {
            const float a = juce::jmin (1.0f, (float) v / 255.0f * gain) * half;
            if (a < 0.5f) return;
            if (vertical) list.addWithoutMerging ({ centre - a, row, a * 2.0f, 1.0f });
            else          list.addWithoutMerging ({ (float) i, centre - a, 1.0f, a * 2.0f });
        };
        bar (lowRects, lo, 1.15f);
        bar (midRects, mi, 1.3f);
        bar (highRects, hi, 1.6f);
    }
    g.setColour (Theme::waveLow.withAlpha (0.92f));  g.fillRectList (lowRects);
    g.setColour (Theme::waveMid.withAlpha (0.85f));  g.fillRectList (midRects);
    g.setColour (Theme::waveHigh.withAlpha (0.9f));  g.fillRectList (highRects);

    cacheStart = startFrame;
    cacheLen = length;
    cacheFpp = fpp;
    cacheCross = cross;
    cacheBins = bins;
    cacheTrack = &t;
    cacheVertical = vertical;
    cacheTheme = Theme::key();
}

void ScrollingWaveform::mouseDown (const juce::MouseEvent&)
{
    if (engine.getTrack (deck) == nullptr) return;
    scratching = true;
    scratchStartPos = engine.getDeck (deck).uiPosition.load();
    engine.post (DeckCommand::ScratchBegin, deck);
}

void ScrollingWaveform::mouseDrag (const juce::MouseEvent& e)
{
    if (! scratching) return;
    auto t = engine.getTrack (deck);
    if (t == nullptr) return;
    const double fpp = framesPerPixel (*t);
    const double delta = vertical ? (double) e.getDistanceFromDragStartY() * fpp
                                  : -(double) e.getDistanceFromDragStartX() * fpp;
    engine.post (DeckCommand::ScratchMove, deck, juce::jmax (0.0, scratchStartPos + delta));
}

void ScrollingWaveform::mouseUp (const juce::MouseEvent&)
{
    if (! scratching) return;
    scratching = false;
    engine.post (DeckCommand::ScratchEnd, deck);
}

void ScrollingWaveform::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    setSecondsVisible (secondsVisible * (wheel.deltaY > 0 ? 0.85 : 1.0 / 0.85));
    refresh();
}

void ScrollingWaveform::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty() && loadFile) loadFile (deck, juce::File (files[0]));
}

void ScrollingWaveform::itemDropped (const SourceDetails& details)
{
    loadFromDragDescription (details.description, deck, loadFile);
}

// ===========================================================================
OverviewWaveform::OverviewWaveform (AudioEngine& e, int d, LoadFileFn fn)
    : engine (e), deck (d), loadFile (std::move (fn))
{
    setOpaque (true);
}

void OverviewWaveform::rebuildImage (const Track& t)
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    image = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (image);

    const auto total = juce::jmax ((juce::int64) 1, t.totalFrames.load());
    const auto bins = t.binsReady.load (std::memory_order_acquire);
    const double binsPerPx = (double) total / Track::framesPerBin / w;
    const float centre = h * 0.5f, half = h * 0.48f;
    juce::RectangleList<float> lo, mi, hi;

    for (int x = 0; x < w; ++x)
    {
        const auto b0 = (juce::int64) (x * binsPerPx);
        const auto b1 = juce::jmin (bins, juce::jmax (b0 + 1, (juce::int64) ((x + 1) * binsPerPx)));
        if (b0 >= bins) break;
        int l = 0, m = 0, hh = 0;
        for (auto b = b0; b < b1; ++b)
        {
            l = juce::jmax (l, (int) t.waveform[(size_t) b].low);
            m = juce::jmax (m, (int) t.waveform[(size_t) b].mid);
            hh = juce::jmax (hh, (int) t.waveform[(size_t) b].high);
        }
        auto add = [&] (juce::RectangleList<float>& list, int v, float gain)
        {
            const float a = juce::jmin (1.0f, v / 255.0f * gain) * half;
            if (a >= 0.5f) list.addWithoutMerging ({ (float) x, centre - a, 1.0f, a * 2.0f });
        };
        add (lo, l, 1.15f); add (mi, m, 1.3f); add (hi, hh, 1.6f);
    }
    g.setColour (Theme::waveLow.withAlpha (0.85f)); g.fillRectList (lo);
    g.setColour (Theme::waveMid.withAlpha (0.8f));  g.fillRectList (mi);
    g.setColour (Theme::waveHigh.withAlpha (0.85f)); g.fillRectList (hi);

    cachedBins = bins;
    cachedTrack = &t;
}

void OverviewWaveform::paint (juce::Graphics& g)
{
    g.fillAll (Theme::base);
    auto track = engine.getTrack (deck);
    if (track == nullptr)
    {
        g.setColour (Theme::line);
        g.drawHorizontalLine (getHeight() / 2, 4.0f, (float) getWidth() - 4.0f);
        return;
    }

    const Track& t = *track;
    if (cachedTrack != &t || cachedBins != t.binsReady.load() || image.getWidth() != getWidth() || image.getHeight() != getHeight())
        rebuildImage (t);
    g.drawImageAt (image, 0, 0);

    auto& d = engine.getDeck (deck);
    const auto hue = Theme::deck (deck);
    const double total = (double) juce::jmax ((juce::int64) 1, t.totalFrames.load());
    const float w = (float) getWidth(), h = (float) getHeight();
    auto xOf = [&] (double f) { return (float) (f / total) * w; };

    if (d.uiLoopEnd.load() > d.uiLoopStart.load())
    {
        const float a = xOf (d.uiLoopStart.load()), b = xOf (d.uiLoopEnd.load());
        g.setColour (hue.withAlpha (d.uiLoopActive.load() ? 0.45f : 0.2f));
        g.fillRect (a, 0.0f, juce::jmax (1.5f, b - a), h);
    }

    const float px = xOf (d.uiPosition.load());
    g.setColour (Theme::base.withAlpha (0.5f));
    g.fillRect (0.0f, 0.0f, px, h);

    for (int i = 0; i < numHotCues; ++i)
    {
        const double hc = t.hotCues[(size_t) i].load();
        if (hc < 0.0) continue;
        g.setColour (hue);
        g.fillRect (xOf (hc), h - 5.0f, 2.0f, 5.0f);
    }
    g.setColour (Theme::text);
    g.fillRect (xOf (t.cuePoint.load()), 0.0f, 1.0f, 4.0f);

    // end-of-track warning: last 30 s flash the playhead red
    const double remaining = (total - d.uiPosition.load()) / t.sampleRate;
    const bool warn = d.uiPlaying.load() && remaining < 30.0 && t.decodeComplete.load()
                      && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    g.setColour (warn ? Theme::warn : Theme::text);
    g.fillRect (px - 1.0f, 0.0f, 2.0f, h);
}

void OverviewWaveform::seekTo (float x)
{
    if (engine.getTrack (deck) == nullptr) return;
    engine.post (DeckCommand::SeekFraction, deck, juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1, getWidth())));
}

void OverviewWaveform::mouseDown (const juce::MouseEvent& e) { seekTo ((float) e.x); }
void OverviewWaveform::mouseDrag (const juce::MouseEvent& e) { seekTo ((float) e.x); }

void OverviewWaveform::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty() && loadFile) loadFile (deck, juce::File (files[0]));
}

void OverviewWaveform::itemDropped (const SourceDetails& details)
{
    loadFromDragDescription (details.description, deck, loadFile);
}

} // namespace loopdeck
