#include "AudioEngine.h"

namespace loopdeck
{

AudioEngine::AudioEngine()
{
    for (int i = 0; i < maxDecks; ++i)
        decks[(size_t) i] = std::make_unique<Deck> (i);

    // Decks 1/3/5/7 default to crossfader side A, 2/4/6/8 to side B
    for (int i = 0; i < maxDecks; ++i)
        mixer.channels[(size_t) i].xfaderAssign = (i % 2 == 0) ? 0 : 2;

    prepare (44100.0);
}

AudioEngine::~AudioEngine()
{
    shutdownAudio();

    // release anything still in the garbage queue
    housekeeping();
}

juce::String AudioEngine::initialiseAudio (const juce::XmlElement* savedState)
{
    auto err = deviceManager.initialise (0, 8, savedState, true);
    deviceManager.addAudioCallback (this);
    return err;
}

void AudioEngine::shutdownAudio()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

// ---------------------------------------------------------------------------
void AudioEngine::prepare (double sampleRate)
{
    currentSampleRate = sampleRate;
    for (auto& d : decks) d->prepare (sampleRate, internalBlock);
    for (auto& f : fxChains) f.prepare (sampleRate, internalBlock);
    for (int i = 0; i < maxDecks; ++i)
    {
        deckL[(size_t) i].assign (internalBlock, 0.0f);
        deckR[(size_t) i].assign (internalBlock, 0.0f);
    }
    masterL.assign (internalBlock, 0.0f);
    masterR.assign (internalBlock, 0.0f);
    cueL.assign (internalBlock, 0.0f);
    cueR.assign (internalBlock, 0.0f);
    mixer.prepare (sampleRate, internalBlock);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const int outs = device->getActiveOutputChannels().countNumberOfSetBits();
    numOutputs = outs;
    if (outs >= 4 && cueOutChannel.load() < 0)
        cueOutChannel = 2;
    if (cueOutChannel.load() + 1 >= outs)
        cueOutChannel = outs >= 4 ? 2 : -1;
    if (masterOutChannel.load() + 1 >= outs)
        masterOutChannel = 0;

    prepare (device->getCurrentSampleRate());
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::prepareOffline (double sampleRate, int)
{
    numOutputs = 2;
    prepare (sampleRate);
}

void AudioEngine::renderOffline (float* const* outputs, int numOutputChannels, int numSamples)
{
    for (int start = 0; start < numSamples; start += internalBlock)
        processChunk (outputs, numOutputChannels, start, juce::jmin (internalBlock, numSamples - start));
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputs, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int start = 0; start < numSamples; start += internalBlock)
        processChunk (outputs, numOutputChannels, start, juce::jmin (internalBlock, numSamples - start));
}

// ---------------------------------------------------------------------------
SyncContext AudioEngine::buildSyncContext()
{
    const int nd = numDecks.load();
    auto eligible = [&] (int i) { return i >= 0 && i < nd && decks[(size_t) i]->isPlayingAudio() && decks[(size_t) i]->hasGrid(); };

    int m = -1;
    const int pinned = pinnedMaster.load();
    if (eligible (pinned))
        m = pinned;
    else if (eligible (currentMaster))
        m = currentMaster;
    else
    {
        juce::int64 earliest = std::numeric_limits<juce::int64>::max();
        for (int i = 0; i < nd; ++i)
            if (eligible (i) && decks[(size_t) i]->playStartedAt < earliest)
            {
                earliest = decks[(size_t) i]->playStartedAt;
                m = i;
            }
    }
    currentMaster = m;

    SyncContext ctx;
    ctx.masterIndex = m;
    if (m >= 0)
    {
        auto& d = *decks[(size_t) m];
        ctx.deckIsMaster = true;
        ctx.masterBpm = d.getEffectiveBpm();
        ctx.masterBeat = d.getBeatPosition();
        internalBpm = ctx.masterBpm;
        internalBeat = ctx.masterBeat;
    }
    else
    {
        ctx.deckIsMaster = false;
        ctx.masterBpm = internalBpm.load();
        ctx.masterBeat = internalBeat;
    }
    return ctx;
}

void AudioEngine::drainCommands (SyncContext& ctx)
{
    const int ready = commandFifo.getNumReady();
    if (ready == 0) return;

    int s1, n1, s2, n2;
    commandFifo.prepareToRead (ready, s1, n1, s2, n2);

    auto run = [&] (const DeckCommand& cmd)
    {
        if (cmd.deck < 0 || cmd.deck >= maxDecks) return;
        if (Track* old = decks[(size_t) cmd.deck]->handleCommand (cmd, ctx))
        {
            if (garbageFifo.getFreeSpace() > 0)
            {
                const auto scope = garbageFifo.write (1);
                if (scope.blockSize1 > 0) garbage[(size_t) scope.startIndex1] = old;
                else garbage[(size_t) scope.startIndex2] = old;
            }
            // if the garbage queue is full we leak one reference rather than free on the audio thread
        }
    };

    for (int i = 0; i < n1; ++i) run (commandBuffer[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) run (commandBuffer[(size_t) (s2 + i)]);
    commandFifo.finishedRead (n1 + n2);
}

void AudioEngine::processChunk (float* const* outputs, int numOutputChannels, int start, int n)
{
    // Silence makes filter states decay into denormal numbers, which are very slow on x86.
    // Flushing them to zero keeps CPU flat when tracks end or channels are idle.
    juce::ScopedNoDenormals noDenormals;

    const double sr = currentSampleRate.load();
    const int nd = numDecks.load();

    auto ctx = buildSyncContext();
    drainCommands (ctx);

    for (int i = 0; i < maxDecks; ++i)
    {
        const bool p = decks[(size_t) i]->isPlayingAudio();
        if (p && ! wasPlaying[(size_t) i]) decks[(size_t) i]->playStartedAt = sampleCounter;
        wasPlaying[(size_t) i] = p;
    }
    ctx = buildSyncContext();

    // master deck first, so its published state is fresh; slaves use the block-start snapshot
    if (ctx.masterIndex >= 0)
        decks[(size_t) ctx.masterIndex]->process (deckL[(size_t) ctx.masterIndex].data(),
                                                  deckR[(size_t) ctx.masterIndex].data(), n, ctx);
    for (int i = 0; i < nd; ++i)
        if (i != ctx.masterIndex)
            decks[(size_t) i]->process (deckL[(size_t) i].data(), deckR[(size_t) i].data(), n, ctx);

    // deck effects (pre-EQ, pre-fader, as on a DJ mixer's channel FX)
    for (int i = 0; i < nd; ++i)
    {
        auto& d = *decks[(size_t) i];
        const double bpm = d.hasGrid() ? d.uiEffectiveBpm.load() : 0.0;
        fxChains[(size_t) i].process (deckL[(size_t) i].data(), deckR[(size_t) i].data(), n,
                                      deckFx[(size_t) i], bpm, d.getBeatPosition());
    }

    if (! ctx.deckIsMaster)
        internalBeat += (double) n * internalBpm.load() / 60.0 / sr;

    std::array<float*, maxDecks> lPtrs {}, rPtrs {};
    for (int i = 0; i < nd; ++i) { lPtrs[(size_t) i] = deckL[(size_t) i].data(); rPtrs[(size_t) i] = deckR[(size_t) i].data(); }
    mixer.process (lPtrs.data(), rPtrs.data(), nd, n, masterL.data(), masterR.data(), cueL.data(), cueR.data());

    // --- route to device outputs --------------------------------------------
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch] + start, n);

    auto copyTo = [&] (int ch, const float* src)
    {
        if (ch >= 0 && ch < numOutputChannels && outputs[ch] != nullptr)
            juce::FloatVectorOperations::add (outputs[ch] + start, src, n);
    };

    const int mo = masterOutChannel.load();
    const int co = cueOutChannel.load();

    if (mixer.master.splitCue.load() && numOutputChannels >= 2)
    {
        // headphone split: left = cue (mono), right = master (mono)
        for (int i = 0; i < n; ++i)
        {
            const float c = 0.5f * (cueL[(size_t) i] + cueR[(size_t) i]);
            const float m = 0.5f * (masterL[(size_t) i] + masterR[(size_t) i]);
            if (outputs[mo] != nullptr) outputs[mo][start + i] = c;
            if (mo + 1 < numOutputChannels && outputs[mo + 1] != nullptr) outputs[mo + 1][start + i] = m;
        }
    }
    else
    {
        copyTo (mo, masterL.data());
        copyTo (mo + 1, masterR.data());
        if (co >= 0 && co != mo)
        {
            copyTo (co, cueL.data());
            copyTo (co + 1, cueR.data());
        }
    }

    uiMasterBpm = ctx.masterBpm;
    uiMasterDeck = ctx.deckIsMaster ? ctx.masterIndex : -1;
    uiMasterBeat = ctx.deckIsMaster ? decks[(size_t) ctx.masterIndex]->getBeatPosition() : internalBeat;
    sampleCounter += n;
}

// ---------------------------------------------------------------------------
void AudioEngine::post (const DeckCommand& cmd)
{
    const juce::ScopedLock sl (postLock);
    if (commandFifo.getFreeSpace() < 1)
    {
        if (cmd.track != nullptr) cmd.track->decReferenceCount();
        return;
    }
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0) commandBuffer[(size_t) scope.startIndex1] = cmd;
    else commandBuffer[(size_t) scope.startIndex2] = cmd;
}

void AudioEngine::post (DeckCommand::Type type, int deck, double a, int index)
{
    DeckCommand c;
    c.type = type; c.deck = deck; c.a = a; c.index = index;
    post (c);
}

bool AudioEngine::loadTrack (int deck, const juce::File& file)
{
    if (! loader.isSupportedFile (file)) return false;
    loadTrack (deck, loader.load (file));
    return true;
}

void AudioEngine::loadTrack (int deck, Track::Ptr t)
{
    if (deck < 0 || deck >= maxDecks || t == nullptr) return;

    // save anything pending on the outgoing track
    if (auto old = uiTracks[(size_t) deck]; old != nullptr && old->cuesDirty.load())
    {
        old->cuesDirty = false;
        loader.saveTrackData (*old);
    }

    uiTracks[(size_t) deck] = t;
    mixer.channels[(size_t) deck].autoGainDb = 0.0f;

    t->incReferenceCount();   // ownership passes to the audio thread
    DeckCommand c;
    c.type = DeckCommand::LoadTrack;
    c.deck = deck;
    c.track = t.get();
    post (c);
}

void AudioEngine::ejectTrack (int deck)
{
    if (deck < 0 || deck >= maxDecks) return;
    if (auto old = uiTracks[(size_t) deck]; old != nullptr && old->cuesDirty.load())
    {
        old->cuesDirty = false;
        loader.saveTrackData (*old);
    }
    uiTracks[(size_t) deck] = nullptr;
    post (DeckCommand::Eject, deck);
}

void AudioEngine::housekeeping()
{
    // free tracks released by the audio thread
    for (;;)
    {
        Track* t = nullptr;
        {
            const auto scope = garbageFifo.read (1);
            if (scope.blockSize1 > 0) t = garbage[(size_t) scope.startIndex1];
            else if (scope.blockSize2 > 0) t = garbage[(size_t) scope.startIndex2];
            else break;
        }
        if (t != nullptr)
        {
            if (t->cuesDirty.load()) { t->cuesDirty = false; loader.saveTrackData (*t); }
            t->decReferenceCount();
        }
    }

    for (int i = 0; i < maxDecks; ++i)
    {
        auto& t = uiTracks[(size_t) i];
        if (t == nullptr) continue;

        if (t->cuesDirty.load() && t->decodeComplete.load())
        {
            t->cuesDirty = false;
            loader.saveTrackData (*t);
        }
        if (t->analysed.load())
            mixer.channels[(size_t) i].autoGainDb = t->autoGainDb.load();
    }
}

} // namespace loopdeck
