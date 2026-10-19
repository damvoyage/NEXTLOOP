#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

namespace loopdeck
{

constexpr int maxDecks = 8;

/** One mixer channel: trim, 3-band isolator EQ, one-knob filter, fader, cue. */
struct ChannelControls
{
    std::atomic<float> trimDb { 0.0f };       // -12..+12
    std::atomic<float> eqHigh { 0.5f };       // 0 = kill, 0.5 = flat, 1 = +6 dB
    std::atomic<float> eqMid { 0.5f };
    std::atomic<float> eqLow { 0.5f };
    std::atomic<float> filter { 0.0f };       // -1 = LPF closed, 0 = off, +1 = HPF closed
    std::atomic<float> filterResonance { 0.35f };
    std::atomic<float> fader { 0.8f };        // 0..1
    std::atomic<int> xfaderAssign { 1 };      // 0 = A, 1 = thru, 2 = B
    std::atomic<bool> cue { false };
    std::atomic<bool> autoGain { true };
    std::atomic<float> autoGainDb { 0.0f };   // set from track analysis

    // meters (written by audio thread)
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
};

struct MasterControls
{
    std::atomic<float> crossfader { 0.0f };       // -1 = A, +1 = B
    std::atomic<int> crossfaderCurve { 0 };       // 0 = smooth, 1 = sharp (scratch), 2 = off
    std::atomic<float> masterGain { 0.8f };       // 0..1 (fader)
    std::atomic<float> headphoneGain { 0.8f };
    std::atomic<float> headphoneMix { 0.0f };     // 0 = cue only, 1 = master only
    std::atomic<bool> limiter { true };
    std::atomic<bool> splitCue { false };         // mono cue left / master right on master outputs

    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<float> limiterReduction { 0.0f };
};

class ChannelStrip
{
public:
    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int n, const ChannelControls& c);

private:
    juce::dsp::LinkwitzRileyFilter<float> split1, split2, lowAp;
    juce::dsp::StateVariableTPTFilter<float> svf;

    float gLow = 1.0f, gMid = 1.0f, gHigh = 1.0f, gTrim = 1.0f;
    float smoothedFilter = 0.0f;
    int lastFilterType = 0;   // -1 LP, 0 off, 1 HP
    double sr = 44100.0;
};

/** Sums decks into master and cue buses and routes them to device outputs. */
class Mixer
{
public:
    void prepare (double sampleRate, int maxBlock);

    /** deckL/deckR hold each deck's pre-mixer output and are processed in place. */
    void process (float* const* deckL, float* const* deckR, int numDecks, int n,
                  float* masterL, float* masterR, float* cueL, float* cueR);

    std::array<ChannelControls, maxDecks> channels;
    MasterControls master;

    static float faderCurve (float v) noexcept;
    static void crossfaderGains (float x, int curve, float& a, float& b) noexcept;

private:
    std::array<ChannelStrip, maxDecks> strips;
    std::array<float, maxDecks> lastFaderGain {};
    float limiterEnv = 1.0f;
    float lastMaster = 0.8f;
    double sr = 44100.0;
};

} // namespace loopdeck
