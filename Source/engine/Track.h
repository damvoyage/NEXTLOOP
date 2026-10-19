#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>

namespace loopdeck
{

/** One column of waveform overview data. Each bin covers Track::framesPerBin frames. */
struct WaveBin
{
    juce::uint8 peak = 0;   // full-band peak (0..255 linear)
    juce::uint8 low = 0;    // < ~200 Hz
    juce::uint8 mid = 0;    // ~200 Hz .. 2.5 kHz
    juce::uint8 high = 0;   // > ~2.5 kHz
};

/** Beat grid. Stored as atomics so the audio thread can read while the UI edits. */
struct BeatGrid
{
    std::atomic<double> bpm { 0.0 };          // 0 = unknown
    std::atomic<double> anchorFrame { 0.0 };  // frame position of a downbeat ("1")
    std::atomic<int> beatsPerBar { 4 };

    bool isValid() const noexcept { return bpm.load() > 1.0; }

    double framesPerBeat (double sampleRate) const noexcept
    {
        const auto b = bpm.load();
        return b > 1.0 ? sampleRate * 60.0 / b : sampleRate * 0.5; // fallback 120 BPM
    }

    /** Beat index (fractional) at a frame position. Beat 0 is the anchor downbeat. */
    double beatAt (double frame, double sampleRate) const noexcept
    {
        return (frame - anchorFrame.load()) / framesPerBeat (sampleRate);
    }

    double frameOfBeat (double beat, double sampleRate) const noexcept
    {
        return anchorFrame.load() + beat * framesPerBeat (sampleRate);
    }
};

constexpr int numHotCues = 8;

/**
    A decoded audio file. Audio is held as interleaved stereo float in a
    memory-mapped cache file, so very long files don't consume RAM.
    Decoding happens progressively on a background thread: framesReady grows
    while the deck can already play the decoded part.
*/
class Track : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Track>;

    static constexpr int framesPerBin = 128;
    static constexpr int cacheFormatVersion = 2;

    Track (const juce::File& source);
    ~Track() override;

    // --- Identity / metadata -------------------------------------------------
    juce::File sourceFile;
    juce::String cacheKey;
    juce::String title, artist, album, genre;
    juce::String formatName;     // e.g. "FLAC", "MP3"
    int sourceBitDepth = 0;
    int sourceChannels = 0;
    double sampleRate = 44100.0;

    // --- Audio data ----------------------------------------------------------
    std::atomic<juce::int64> totalFrames { 0 };   // final once decodeComplete
    std::atomic<juce::int64> framesReady { 0 };   // frames safe to read
    std::atomic<bool> decodeComplete { false };
    std::atomic<bool> decodeFailed { false };
    std::atomic<bool> cancelDecode { false };
    juce::String errorMessage;

    /** Interleaved stereo frames (L,R,L,R...). Valid up to framesReady. */
    const float* getData() const noexcept { return data; }

    double getLengthSeconds() const noexcept { return (double) totalFrames.load() / sampleRate; }

    /** 4-point Hermite interpolated read. Returns silence outside the decoded range. */
    inline void readInterpolated (double pos, float& outL, float& outR) const noexcept
    {
        const auto ready = framesReady.load (std::memory_order_acquire);
        const auto i1 = (juce::int64) std::floor (pos);
        if (data == nullptr || i1 < 1 || i1 + 2 >= ready)
        {
            readSafe (pos, ready, outL, outR);
            return;
        }
        const float t = (float) (pos - (double) i1);
        const float* p = data + (i1 - 1) * 2;
        outL = hermite (p[0], p[2], p[4], p[6], t);
        outR = hermite (p[1], p[3], p[5], p[7], t);
    }

    inline void readFrame (juce::int64 i, float& outL, float& outR) const noexcept
    {
        if (data == nullptr || i < 0 || i >= framesReady.load (std::memory_order_acquire)) { outL = outR = 0.0f; return; }
        outL = data[i * 2];
        outR = data[i * 2 + 1];
    }

    // --- Waveform overview ---------------------------------------------------
    std::vector<WaveBin> waveform;            // pre-sized; valid up to binsReady
    std::atomic<juce::int64> binsReady { 0 };

    // --- Analysis --------------------------------------------------------------
    BeatGrid grid;
    std::atomic<bool> analysed { false };
    std::atomic<bool> analysing { false };
    void setKey (const juce::String& k, const juce::String& cam)
    {
        const juce::SpinLock::ScopedLockType sl (textLock);
        key = k; camelot = cam;
    }
    juce::String getKey() const     { const juce::SpinLock::ScopedLockType sl (textLock); return key; }
    juce::String getCamelot() const { const juce::SpinLock::ScopedLockType sl (textLock); return camelot; }
    std::atomic<float> loudnessDb { 0.0f };   // approximate integrated loudness
    std::atomic<float> autoGainDb { 0.0f };

    // --- Cues (persisted with the analysis cache) ------------------------------
    std::array<std::atomic<double>, numHotCues> hotCues;  // -1 = empty
    std::atomic<double> cuePoint { 0.0 };
    std::atomic<bool> cuesDirty { false };

    // --- Cache plumbing (used by TrackLoader) ----------------------------------
    bool allocateCache (const juce::File& audioCacheFile, juce::int64 capacityFrames);
    bool mapExistingCache (const juce::File& audioCacheFile, juce::int64 frames);
    float* getWritableData() noexcept { return writableData; }
    juce::int64 getCapacityFrames() const noexcept { return capacityFrames; }
    void releaseMapping();

private:
    static inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float bNeg = w + a;
        return ((((a * t) - bNeg) * t + c) * t + x0);
    }

    void readSafe (double pos, juce::int64 ready, float& outL, float& outR) const noexcept;

    mutable juce::SpinLock textLock;
    juce::String key, camelot;

    std::unique_ptr<juce::MemoryMappedFile> mapping;
    const float* data = nullptr;
    float* writableData = nullptr;
    juce::int64 capacityFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Track)
};

} // namespace loopdeck
