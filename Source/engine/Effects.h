#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <vector>

namespace loopdeck
{

/**
    Deck effects. Each deck has a chain of slots; each slot hosts one effect
    with two controls: amount (wet / depth) and a tempo-synced length in beats.
    Time-based effects (echo, delay, reverb, echo out) ring out after they are
    switched off, like a "trailing stop".
*/
enum class FxType : int
{
    Echo = 0,      // tempo echo, feedback scales with amount
    PingPong,      // stereo ping-pong delay with darkening repeats
    Reverb,        // room / hall
    Flanger,       // LFO period = beats
    Phaser,        // 4-stage, LFO period = beats
    Filter,        // swept resonant band filter (wah), period = beats
    Gate,          // beat-synced trance gate, length = beats
    Crush,         // bit and sample-rate reduction
    Granular,      // grain cloud from the last few beats
    EchoOut,       // captures the next beat into an echo and mutes the dry signal
    Stutter,       // beat repeat of a captured slice, length = beats
    count
};

constexpr int numFxTypes = (int) FxType::count;
const char* fxName (int type);
bool fxHasTail (int type);

/** Length choices for the beats control. */
constexpr std::array<float, 10> fxBeatChoices { 1.0f / 16, 1.0f / 8, 1.0f / 4, 1.0f / 2, 3.0f / 4, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
juce::String fxBeatsLabel (float beats);

struct FxSlotControls
{
    std::atomic<bool> on { false };
    std::atomic<int> type { 0 };
    std::atomic<float> amount { 0.5f };   // 0..1
    std::atomic<float> beats { 0.75f };
    std::atomic<bool> tailActive { false };   // published: still ringing after switch-off
};

constexpr int fxSlotsPerDeck = 3;

struct FxControls
{
    std::array<FxSlotControls, fxSlotsPerDeck> slots;
};

/** Audio-thread state for one slot. */
class FxSlot
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** beatAtEnd = deck beat position at the end of this block (for gate sync). */
    void process (float* L, float* R, int n, FxSlotControls& c, double bpm, double beatAtEnd);

    bool isIdle() const noexcept { return mix <= 0.0f && ! ringing; }

private:
    float readHistory (int ch, double delaySamples) const noexcept;

    double sr = 44100.0;
    int activeType = -1;
    float mix = 0.0f;           // smoothed on/off
    float amountSm = 0.5f;
    bool ringing = false;       // tail mode after switch-off
    int silentBlocks = 0;
    bool wasOn = false;

    // shared history / delay line
    std::vector<float> histL, histR;
    int histSize = 0, writeIdx = 0;
    double delaySm = 0.0;       // smoothed delay time (samples)

    // echo / ping-pong
    float fbL = 0.0f, fbR = 0.0f, lpL = 0.0f, lpR = 0.0f;
    std::vector<float> echoL, echoR;   // separate feedback delay line
    int echoIdx = 0;

    // modulation
    double lfoPhase = 0.0;
    std::array<float, 8> apState {};   // phaser (4 stages x 2 ch)
    float phFbL = 0.0f, phFbR = 0.0f;
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::Reverb reverb;

    // crush
    float holdL = 0.0f, holdR = 0.0f;
    float holdCount = 0.0f;

    // gate
    float gateEnv = 1.0f;

    // echo out
    double echoOutCaptured = 0.0;   // samples of input already captured
    float dryGain = 1.0f;

    // stutter
    double stutterStart = 0.0;      // write index when captured
    double stutterPos = 0.0;
    bool stutterCaptured = false;

    // granular
    struct Grain { double pos = 0.0; double rate = 1.0; int age = 0; int length = 0; bool active = false; float pan = 0.5f; };
    std::array<Grain, 6> grains;
    int grainTimer = 0;
    juce::Random random { 1234 };
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock) { for (auto& s : slots) s.prepare (sampleRate, maxBlock); }
    void process (float* L, float* R, int n, FxControls& c, double bpm, double beatAtEnd)
    {
        for (int i = 0; i < fxSlotsPerDeck; ++i)
            slots[(size_t) i].process (L, R, n, c.slots[(size_t) i], bpm, beatAtEnd);
    }
private:
    std::array<FxSlot, fxSlotsPerDeck> slots;
};

} // namespace loopdeck
