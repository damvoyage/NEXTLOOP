#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace loopdeck
{

/**
    Key lock / time-stretch algorithms.

    Light and Studio aim for transparency (Light is the cheapest on CPU). The vintage ones are deliberately
    imperfect: they recreate the warble, smear and stutter of old samplers,
    early DJ software and textbook phase vocoders, for when that sound is
    the point.
*/
enum class StretchAlgorithm : int
{
    Light = 0,           // WSOLA: time-domain splicing at matched points; very cheap, the default
    Studio,              // Signalsmith Stretch: spectral, cleaner on big tempo changes
    VintageSampler,      // overlapping grains, like 90s hardware samplers
    VintagePhaseVocoder, // textbook phase vocoder: smeared, phasey
    VintageCyclic,       // hard-spliced cycles: stuttery, glitchy
    count
};

constexpr int numStretchAlgorithms = (int) StretchAlgorithm::count;

juce::String stretchAlgorithmName (int algorithm);
juce::String stretchAlgorithmDescription (int algorithm);
bool stretchAlgorithmAvailable (int algorithm);

/** Where a stretcher pulls its input from: the deck's (loop-aware) source reader. */
struct StretchSource
{
    virtual ~StretchSource() = default;
    virtual void read (float* L, float* R, int numFrames) = 0;
};

class TimeStretcher
{
public:
    virtual ~TimeStretcher() = default;

    /** Allocates everything. Never called on the audio thread. */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Resets and pre-rolls so the next output starts exactly at the source's current position. */
    virtual void prime (StretchSource& source, double rate, double transpose) = 0;

    /** Produces numFrames of output. rate = input frames consumed per output frame; transpose = pitch factor. */
    virtual void process (StretchSource& source, float* L, float* R, int numFrames, double rate, double transpose) = 0;
};

std::unique_ptr<TimeStretcher> createTimeStretcher (int algorithm);

} // namespace loopdeck
