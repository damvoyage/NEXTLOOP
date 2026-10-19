#pragma once

#include "Track.h"
#include "TimeStretch.h"

namespace loopdeck
{

/** A loop length expressed musically. Bars can be any whole number (5, 7, 13...). */
struct LoopLength
{
    enum class Unit { Beats, Bars };
    double value = 4.0;
    Unit unit = Unit::Beats;

    double toBeats (int beatsPerBar) const noexcept { return unit == Unit::Bars ? value * beatsPerBar : value; }
    juce::String toString() const;

    bool operator== (const LoopLength& o) const noexcept { return unit == o.unit && std::abs (value - o.value) < 1e-9; }

    static LoopLength beats (double v) { return { v, Unit::Beats }; }
    static LoopLength bars (double v)  { return { v, Unit::Bars }; }

    /** Steps through the length ladder: 1/32..1/2 beat, 1..16 beats in Beats mode; 1..32 bars, 48, 64 in Bars mode. */
    LoopLength next() const;
    LoopLength previous() const;

    /** Parses text like "5", "5 bars", "7b", "3 beats", "1/4". Bare numbers default to the given unit. */
    static bool parse (const juce::String& text, Unit defaultUnit, LoopLength& out);
};

/** Commands sent from the UI / MIDI to the audio thread. */
struct DeckCommand
{
    enum Type : int
    {
        LoadTrack, Eject,
        Play, Pause, TogglePlay,
        CueDown, CueUp, SetCuePoint,
        HotCueDown, HotCueUp, HotCueDelete,
        SeekFrames, SeekFraction, SeekBeats,
        AutoLoop,        // a = beats
        ToggleLoop,      // a = beats
        SetLoopLength,   // a = beats (resizes an active loop)
        LoopHalve, LoopDouble,
        LoopMove,        // a = beats (+/-)
        LoopAdjustIn,    // a = frames (+/-)
        LoopAdjustOut,   // a = frames (+/-)
        LoopIn, LoopOut, LoopExit, Reloop,
        LoopRollDown,    // a = beats
        LoopRollUp,
        SyncOn, SyncOff,
        ScratchBegin, ScratchMove /* a = target frame */, ScratchEnd,
        BeatJump,        // a = beats (+/-)
        Brake,           // a = seconds: turntable power-off slowdown while held
        BrakeEnd,        // release: carries on from where the track would have been
        Backspin,        // a = seconds: record spun backwards while held
        BackspinEnd,     // release: carries on from where the track would have been
    };

    Type type = Play;
    int deck = 0;
    int index = 0;
    double a = 0.0;
    Track* track = nullptr;   // LoadTrack: reference already incremented by sender
};

struct SyncContext
{
    bool deckIsMaster = false;   // true when masterIndex refers to a playing deck
    int masterIndex = -1;
    double masterBpm = 120.0;
    double masterBeat = 0.0;     // master beat position at the start of this block
};

class Deck
{
public:
    explicit Deck (int index);
    ~Deck();

    void prepare (double deviceSampleRate, int maximumBlockSize);

    /** Audio thread. Writes this deck's stereo output (pre-mixer). */
    void process (float* outL, float* outR, int numSamples, const SyncContext& ctx);

    /** Audio thread. Returns a track that must be released on the message thread (or nullptr). */
    Track* handleCommand (const DeckCommand& cmd, const SyncContext& ctx);

    /** Audio thread helpers used by the engine for master clock logic. */
    bool isPlayingAudio() const noexcept   { return playing || scratching || cueHeld || hotCueHeld >= 0; }
    bool hasGrid() const noexcept          { return track != nullptr && track->grid.isValid(); }
    double getEffectiveBpm() const noexcept;
    double getBeatPosition() const noexcept;   // fractional beat at the audible position
    juce::int64 playStartedAt = 0;             // engine sample counter when playback started

    // --- Controls written by UI / MIDI (read by the audio thread) -------------
    std::atomic<float> pitch { 0.0f };        // -1..+1 (fader position)
    std::atomic<float> pitchRange { 0.08f };  // 0.08 = ±8 %
    std::atomic<float> bend { 0.0f };         // temporary nudge, e.g. ±0.04
    std::atomic<bool> keyLock { false };          // off = varispeed (vinyl), on = time-stretch keeps the key
    std::atomic<float> keyShift { 0.0f };         // semitones, works in either mode
    std::atomic<int> stretchAlgorithm { 0 };      // StretchAlgorithm used for key lock / key shift

    /** Message thread: builds the stretcher for an algorithm if needed, then selects it. */
    void setStretchAlgorithm (int algorithm);
    std::atomic<bool> quantize { true };
    std::atomic<bool> slipMode { false };
    std::atomic<bool> syncEnabled { false };

    // --- Published state (written by audio thread, read by UI) ----------------
    std::atomic<double> uiPosition { 0.0 };     // audible position in track frames
    std::atomic<bool> uiPlaying { false };
    std::atomic<bool> uiLoopActive { false };
    std::atomic<double> uiLoopStart { 0.0 };
    std::atomic<double> uiLoopEnd { 0.0 };
    std::atomic<double> uiLoopBeats { 4.0 };    // current loop length in beats
    std::atomic<double> uiEffectiveBpm { 0.0 };
    std::atomic<double> uiTempoRatio { 1.0 };
    std::atomic<bool> uiIsMaster { false };
    std::atomic<bool> uiSlipActive { false };
    std::atomic<double> uiSlipPosition { 0.0 };
    std::atomic<double> uiBeatPhase { 0.0 };    // 0..1 within the beat
    std::atomic<float> uiPhaseError { 0.0f };   // beats, vs master (for display)
    std::atomic<bool> uiBraking { false };      // brake or backspin running

    const int deckIndex;

private:
    double sampleRateRatio() const noexcept;   // track frames per device frame at rate 1
    double baseTempoRatio (const SyncContext& ctx) const noexcept;
    double framesPerBeat() const noexcept;
    double quantizeToBeat (double frame, double unitBeats) const noexcept;
    double phasePreservingTarget (double target) const noexcept;
    double computeAudiblePosition() const noexcept;

    void jumpTo (double newPos, bool allowQuantize);
    void startPlayback (const SyncContext& ctx);
    void snapPhaseToMaster (const SyncContext& ctx);
    void primeStretcher();
    void setLoop (double start, double end);
    void applyLoopLength (double beats);
    double snapToZeroCrossing (double frame) const noexcept;

    /** Reads `count` frames advancing by `step` per frame, handling loop wraps with a short crossfade. */
    void renderSource (float* L, float* R, int count, double step);

    Track* track = nullptr;   // owned reference (incRef'd)

    double deviceSampleRate = 44100.0;
    int maxBlock = 512;

    // transport
    double readPos = 0.0;           // where the source is read
    bool playing = false;
    bool cueHeld = false;           // CDJ-style cue preview
    int hotCueHeld = -1;            // index while a hot cue preview is held (paused start)
    double hotCueReturn = 0.0;
    double cueReturn = 0.0;

    // scratch
    bool scratching = false;
    bool wasPlayingBeforeScratch = false;
    double scratchTarget = 0.0;
    double scratchVelocity = 0.0;

    // loops
    bool loopActive = false;
    double loopStart = 0.0, loopEnd = 0.0;
    double loopBeats = 4.0;
    bool loopInPending = false;

    // slip
    bool slipActive = false;
    double slipPos = 0.0;
    bool rollActive = false;

    // declick crossfade after discontinuities
    static constexpr int xfadeLength = 96;
    int xfadeRemaining = 0;
    double xfadeOldPos = 0.0;

    // brake / backspin
    bool braking = false, backspinning = false;
    double brakeFactor = 1.0, brakeStep = 0.0;      // brake: rate multiplier ramp
    double spinElapsed = 0.0, spinLength = 1.0;     // backspin: seconds
    double effectSlipPos = 0.0;                     // where the track would be without the effect
    void exitLoopIfOutside (double target);
    void startTransportEffect();
    void endTransportEffect();

    // sync
    double syncCorrection = 0.0;
    bool lastWasMaster = false;

    // keylock
    struct Source : StretchSource
    {
        Deck* deck = nullptr;
        double step = 1.0;
        void read (float* L, float* R, int n) override { deck->renderSource (L, R, n, step); }
    };
    Source source;
    std::array<std::unique_ptr<TimeStretcher>, numStretchAlgorithms> stretcherStore;
    std::array<std::atomic<TimeStretcher*>, numStretchAlgorithms> stretcherSlots {};
    juce::CriticalSection stretcherBuildLock;
    TimeStretcher* currentStretcher() noexcept;
    double transposeFactor (double rate) const noexcept;
    int activeAlgorithm = -1;
    double stretchPlayhead = 0.0;     // audible position while stretching
    bool stretchPrimed = false;
    bool lastKeyLock = false;


    JUCE_DECLARE_NON_COPYABLE (Deck)
};

} // namespace loopdeck
