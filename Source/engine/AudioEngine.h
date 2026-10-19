#pragma once

#include "Deck.h"
#include "Mixer.h"
#include "Effects.h"
#include "TrackLoader.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace loopdeck
{

/**
    Owns the audio device, decks, master clock and mixer.

    Threading: the UI/MIDI side talks to decks only by posting DeckCommands
    (lock-free for the audio thread) and by writing the atomic controls on
    Deck / ChannelControls / MasterControls. The audio thread publishes its
    state back through the deck's ui* atomics.
*/
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the audio device (restoring a saved state if given). Returns an error string or empty. */
    juce::String initialiseAudio (const juce::XmlElement* savedState);
    void shutdownAudio();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    TrackLoader& getLoader() { return loader; }
    Mixer& getMixer() { return mixer; }
    Deck& getDeck (int i) { return *decks[(size_t) juce::jlimit (0, maxDecks - 1, i)]; }
    FxControls& getDeckFx (int i) { return deckFx[(size_t) juce::jlimit (0, maxDecks - 1, i)]; }

    int getNumDecks() const { return numDecks.load(); }
    void setNumDecks (int n) { numDecks = juce::jlimit (1, maxDecks, n); }

    // --- Message-thread API --------------------------------------------------
    bool loadTrack (int deck, const juce::File& file);
    void loadTrack (int deck, Track::Ptr track);
    void ejectTrack (int deck);
    Track::Ptr getTrack (int deck) const { return uiTracks[(size_t) juce::jlimit (0, maxDecks - 1, deck)]; }

    void post (DeckCommand::Type type, int deck, double a = 0.0, int index = 0);
    void post (const DeckCommand& cmd);

    /** Call regularly from the message thread: frees unloaded tracks and saves edited cues. */
    void housekeeping();

    // --- Master clock --------------------------------------------------------
    std::atomic<double> internalBpm { 120.0 };
    std::atomic<int> pinnedMaster { -1 };     // user-selected master deck (-1 = automatic)
    std::atomic<double> uiMasterBpm { 120.0 };
    std::atomic<int> uiMasterDeck { -1 };
    std::atomic<double> uiMasterBeat { 0.0 };

    // --- Output routing ------------------------------------------------------
    std::atomic<int> masterOutChannel { 0 };   // first channel of the stereo master pair
    std::atomic<int> cueOutChannel { -1 };     // first channel of the headphone pair, -1 = none
    int getNumOutputChannels() const { return numOutputs.load(); }
    double getSampleRate() const { return currentSampleRate.load(); }
    float getCpuLoad() { return (float) deviceManager.getCpuUsage(); }

    // --- Offline rendering (tests / export) ----------------------------------
    void prepareOffline (double sampleRate, int blockSize);
    void renderOffline (float* const* outputs, int numOutputChannels, int numSamples);

    // --- AudioIODeviceCallback -----------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    static constexpr int internalBlock = 256;

private:
    void prepare (double sampleRate);
    void processChunk (float* const* outputs, int numOutputChannels, int startSample, int n);
    SyncContext buildSyncContext();
    void drainCommands (SyncContext& ctx);

    juce::AudioDeviceManager deviceManager;
    TrackLoader loader;
    Mixer mixer;
    std::array<std::unique_ptr<Deck>, maxDecks> decks;
    std::array<Track::Ptr, maxDecks> uiTracks;
    std::array<FxControls, maxDecks> deckFx;
    std::array<FxChain, maxDecks> fxChains;
    std::atomic<int> numDecks { 4 };

    // command queue (multi-producer via lock, single lock-free consumer)
    static constexpr int commandCapacity = 4096;
    juce::AbstractFifo commandFifo { commandCapacity };
    std::array<DeckCommand, commandCapacity> commandBuffer;
    juce::CriticalSection postLock;

    // tracks released by the audio thread, freed on the message thread
    static constexpr int garbageCapacity = 64;
    juce::AbstractFifo garbageFifo { garbageCapacity };
    std::array<Track*, garbageCapacity> garbage {};

    // audio-thread state
    std::array<std::vector<float>, maxDecks> deckL, deckR;
    std::vector<float> masterL, masterR, cueL, cueR;
    int currentMaster = -1;
    double internalBeat = 0.0;
    juce::int64 sampleCounter = 0;
    std::array<bool, maxDecks> wasPlaying {};

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> numOutputs { 2 };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace loopdeck
