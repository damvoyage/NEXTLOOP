#pragma once

#include "Track.h"

namespace loopdeck
{

struct AnalysisSettings
{
    double minBpm = 88.0;   // detected tempo is folded into [minBpm, maxBpm)
    double maxBpm = 175.0;
    bool snapToWholeBpm = true;   // prefer integer BPMs when the evidence is close
};

struct AnalysisResult
{
    bool ok = false;
    double bpm = 0.0;
    double anchorFrame = 0.0;  // frame of a downbeat
    double confidence = 0.0;
    juce::String key, camelot;
    float loudnessDb = -14.0f;
};

namespace Analysis
{
    /** Full analysis of a decoded track: tempo, beat grid, key, loudness.
        Safe to call from a background thread. shouldStop is polled. */
    AnalysisResult analyse (const Track& track, const AnalysisSettings& settings,
                            std::function<bool()> shouldStop = {});

    /** Lower-level helpers, exposed for tests. */
    std::vector<float> onsetEnvelope (const float* interleavedStereo, juce::int64 numFrames,
                                      double sampleRate, int hop, int fftOrder,
                                      std::vector<float>* lowBandEnvelope = nullptr);

    double foldBpm (double bpm, double minBpm, double maxBpm);

    juce::String camelotFor (int pitchClass, bool minor);
    juce::String keyNameFor (int pitchClass, bool minor);
}

} // namespace loopdeck
