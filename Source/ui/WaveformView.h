#pragma once

#include "engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace loopdeck
{

/** Callback used by views that accept dropped files. */
using LoadFileFn = std::function<void (int deck, const juce::File&)>;

/** Scrolling, zoomable waveform around the playhead, coloured by frequency band.
    Horizontal (1–4 deck layout) or vertical (5–8 deck strips). Drag to scratch. */
class ScrollingWaveform : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget
{
public:
    ScrollingWaveform (AudioEngine& engine, int deckIndex, LoadFileFn loadFn);

    void setVertical (bool v) { vertical = v; repaint(); }

    /** Repaints only if something visible changed (saves CPU when decks are idle). */
    void refresh();

    void setSecondsVisible (double s) { secondsVisible = juce::jlimit (1.0, 60.0, s); }
    double getSecondsVisible() const { return secondsVisible; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

private:
    double framesPerPixel (const Track& t) const;
    void rebuildCache (const Track& t, double startFrame, int length, double fpp, int cross, juce::int64 bins);

    AudioEngine& engine;
    const int deck;
    LoadFileFn loadFile;
    bool vertical = false;
    double secondsVisible = 8.0;

    bool scratching = false;
    double scratchStartPos = 0.0;

    juce::RectangleList<float> lowRects, midRects, highRects;
    juce::uint64 lastSignature = 0;

    // off-screen waveform strip
    juce::Image cache;
    double cacheStart = 0.0, cacheFpp = 0.0;
    int cacheLen = 0, cacheCross = 0;
    juce::int64 cacheBins = -1;
    const Track* cacheTrack = nullptr;
    bool cacheVertical = false;
    int cacheTheme = -1;
    juce::Image background;
    int backgroundKey = -1;
};

/** Whole-track overview with position, cues and loop. Click/drag to seek. */
class OverviewWaveform : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropTarget
{
public:
    OverviewWaveform (AudioEngine& engine, int deckIndex, LoadFileFn loadFn);

    void paint (juce::Graphics&) override;
    void resized() override { cachedBins = -1; }

    /** Repaints only if something visible changed (saves CPU when decks are idle). */
    void refresh();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;

private:
    void rebuildImage (const Track& t);
    void seekTo (float x);

    AudioEngine& engine;
    const int deck;
    LoadFileFn loadFile;
    juce::Image image, background;
    int backgroundKey = -1;
    juce::int64 cachedBins = -1;
    const Track* cachedTrack = nullptr;
    juce::uint64 lastSignature = 0;
};

} // namespace loopdeck
