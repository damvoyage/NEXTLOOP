// Headless tests for the NEXTLOOP engine.
// Usage: NEXTLOOPTests <test-audio-dir> [big-file]
#include "engine/AudioEngine.h"
#include <juce_events/juce_events.h>
#include <cstdio>

using namespace loopdeck;

static int failures = 0;

#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static double wrapHalf (double x) { return x - std::round (x); }

struct OfflineRig
{
    AudioEngine& engine;
    double sr;
    std::vector<float> l, r;

    OfflineRig (AudioEngine& e, double sampleRate) : engine (e), sr (sampleRate) { engine.prepareOffline (sr, 512); }

    /** Renders `seconds`, keeping the last rendered audio in l/r. */
    void render (double seconds)
    {
        const int total = (int) std::llround (seconds * sr);
        l.assign ((size_t) total, 0.0f);
        r.assign ((size_t) total, 0.0f);
        for (int s = 0; s < total; s += 512)
        {
            const int n = juce::jmin (512, total - s);
            float* outs[2] = { l.data() + s, r.data() + s };
            engine.renderOffline (outs, 2, n);
        }
    }

    void renderSamples (int total)
    {
        l.assign ((size_t) total, 0.0f);
        r.assign ((size_t) total, 0.0f);
        for (int s = 0; s < total; s += 512)
        {
            const int n = juce::jmin (512, total - s);
            float* outs[2] = { l.data() + s, r.data() + s };
            engine.renderOffline (outs, 2, n);
        }
    }

    void tick() { renderSamples (64); }
};

static double zeroCrossFrequency (const std::vector<float>& x, double sr, size_t from)
{
    int crossings = 0;
    for (size_t i = from + 1; i < x.size(); ++i)
        if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f)) ++crossings;
    const double seconds = (double) (x.size() - from) / sr;
    return crossings / 2.0 / seconds;
}

static double rms (const std::vector<float>& x)
{
    double a = 0; for (auto v : x) a += v * v;
    return std::sqrt (a / juce::jmax ((size_t) 1, x.size()));
}

static Track::Ptr loadAndWait (AudioEngine& e, const juce::File& f)
{
    auto t = e.getLoader().load (f);
    e.getLoader().waitUntilIdle (120000);
    return t;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2) { std::printf ("usage: NEXTLOOPTests <test-audio-dir> [big-file]\n"); return 2; }
    const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));

    auto cache = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NEXTLOOPTestCache");
    cache.deleteRecursively();

    AudioEngine engine;
    auto& loader = engine.getLoader();
    loader.setCacheDirectory (cache);

    // =======================================================================
    std::printf ("\n== 1. Decoding + analysis across formats ==\n");
    std::printf ("%-28s %-6s %-7s %9s %8s %8s %8s %6s %7s\n", "file", "fmt", "bits", "frames", "bpm", "bpmErr", "phaseMs", "key", "decode");

    const auto manifest = juce::JSON::parse (dir.getChildFile ("manifest.json"));
    for (auto& entry : *manifest.getArray())
    {
        const auto file = dir.getChildFile (entry["file"].toString());
        const double expectBpm = entry["bpm"];
        const double offset = entry["offset"];
        const double seconds = entry["seconds"];
        const auto fmt = entry["format"].toString();
        const bool isMp3 = fmt.startsWith ("mp3");
        // MP3 tops out at 48 kHz; the encoder resamples anything higher
        const double sr = isMp3 ? juce::jmin (48000.0, (double) entry["sr"]) : (double) entry["sr"];

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto t = loadAndWait (engine, file);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

        CHECK (t->decodeComplete.load(), "%s did not decode (%s)", file.getFileName().toRawUTF8(), t->errorMessage.toRawUTF8());
        if (! t->decodeComplete.load()) continue;

        const auto frames = t->totalFrames.load();
        const double expectFrames = seconds * sr;
        const double frameTol = isMp3 ? 3000.0 : 1.0;
        CHECK (std::abs ((double) frames - expectFrames) <= frameTol, "%s frames %lld expected %.0f",
               file.getFileName().toRawUTF8(), (long long) frames, expectFrames);
        CHECK (std::abs (t->sampleRate - sr) < 1.0, "%s sample rate %.0f", file.getFileName().toRawUTF8(), t->sampleRate);

        const double bpm = t->grid.bpm.load();
        const double bpmErr = bpm - expectBpm;
        const double fpb = t->grid.framesPerBeat (t->sampleRate);
        const double phaseBeats = wrapHalf ((t->grid.anchorFrame.load() - offset * t->sampleRate) / fpb);
        const double phaseMs = phaseBeats * fpb / t->sampleRate * 1000.0;

        std::printf ("%-28s %-6s %-7d %9lld %8.3f %8.3f %8.2f %6s %6.0fms\n",
                     file.getFileName().substring (0, 28).toRawUTF8(), t->formatName.substring (0, 6).toRawUTF8(),
                     t->sourceBitDepth, (long long) frames, bpm, bpmErr, phaseMs, t->getCamelot().toRawUTF8(), ms);

        CHECK (std::abs (bpmErr) < 0.05, "%s BPM %.3f expected %.2f", file.getFileName().toRawUTF8(), bpm, expectBpm);
        // MP3 adds encoder/decoder delay (~25-50 ms); the grid follows the decoded audio, so allow for it
        const double phaseTol = isMp3 ? 60.0 : 5.0;
        CHECK (std::abs (phaseMs) < phaseTol, "%s grid phase off by %.2f ms", file.getFileName().toRawUTF8(), phaseMs);
        CHECK (t->getCamelot() == entry["camelot"].toString(), "%s key %s expected %s", file.getFileName().toRawUTF8(),
               t->getCamelot().toRawUTF8(), entry["camelot"].toString().toRawUTF8());

        // waveform overview present and complete
        CHECK (t->binsReady.load() >= frames / Track::framesPerBin, "%s waveform incomplete", file.getFileName().toRawUTF8());
    }

    // =======================================================================
    std::printf ("\n== 2. Cache reload ==\n");
    {
        const auto f = dir.getChildFile ("house126_flac24.flac");
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto t = loadAndWait (engine, f);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  reload from cache: %.1f ms, bpm %.2f, key %s\n", ms, t->grid.bpm.load(), t->getCamelot().toRawUTF8());
        CHECK (t->decodeComplete.load() && t->analysed.load(), "cached reload incomplete");
        CHECK (ms < 250.0, "cached reload too slow (%.1f ms)", ms);
        CHECK (std::abs (t->grid.bpm.load() - 126.0) < 0.05, "cached bpm wrong");
    }

    // =======================================================================
    std::printf ("\n== 3. Loops (odd bar lengths, fine adjust) ==\n");
    const double sr = 48000.0;
    OfflineRig rig (engine, sr);
    engine.setNumDecks (4);

    auto house = loadAndWait (engine, dir.getChildFile ("house126_wav16.wav"));
    engine.loadTrack (0, house);
    engine.getDeck (0).quantize = true;
    engine.post (DeckCommand::Play, 0);
    rig.render (1.3);

    auto& d0 = engine.getDeck (0);
    const double fpb = house->grid.framesPerBeat (house->sampleRate);
    const double srr = house->sampleRate / sr;

    for (double bars : { 5.0, 7.0, 13.0, 12.0 })
    {
        engine.post (DeckCommand::AutoLoop, 0, bars * 4.0);
        rig.tick();
        const double ls = d0.uiLoopStart.load(), le = d0.uiLoopEnd.load();
        const double expectLen = bars * 4.0 * fpb;
        CHECK (d0.uiLoopActive.load(), "%.0f-bar loop not active", bars);
        CHECK (std::abs ((le - ls) - expectLen) < 0.5, "%.0f-bar loop length %.1f expected %.1f", bars, le - ls, expectLen);
        CHECK (std::abs (wrapHalf (house->grid.beatAt (ls, house->sampleRate))) < 1e-6, "%.0f-bar loop start not on beat", bars);

        const double p0 = d0.uiPosition.load();
        rig.renderSamples ((int) std::llround ((le - ls) / srr));
        const double p1 = d0.uiPosition.load();
        double diff = p1 - p0;
        CHECK (std::abs (diff) < 2.0, "%.0f-bar loop period mismatch: drift %.2f frames", bars, diff);
        std::printf ("  %2.0f-bar loop: length %.3f s, one cycle drift %.3f frames\n", bars, (le - ls) / house->sampleRate, diff);
        engine.post (DeckCommand::LoopExit, 0);
        rig.tick();
    }

    // fractional loops
    for (double beats : { 1.0 / 32.0, 1.0 / 4.0, 3.0 })
    {
        engine.post (DeckCommand::AutoLoop, 0, beats);
        rig.render (0.5);
        const double ls = d0.uiLoopStart.load(), le = d0.uiLoopEnd.load(), pos = d0.uiPosition.load();
        CHECK (std::abs ((le - ls) - beats * fpb) < 0.5, "%.4f-beat loop length wrong", beats);
        CHECK (pos >= ls - 1.0 && pos < le + 1.0, "%.4f-beat loop: position %.1f outside [%.1f, %.1f)", beats, pos, ls, le);
        engine.post (DeckCommand::LoopExit, 0);
        rig.tick();
    }

    // loop length parser
    {
        LoopLength L;
        CHECK (LoopLength::parse ("13 bars", LoopLength::Unit::Beats, L) && L.unit == LoopLength::Unit::Bars && L.value == 13.0, "parse '13 bars'");
        CHECK (LoopLength::parse ("7", LoopLength::Unit::Bars, L) && L.toBeats (4) == 28.0, "parse '7' as bars");
        CHECK (LoopLength::parse ("1/4", LoopLength::Unit::Beats, L) && std::abs (L.value - 0.25) < 1e-9, "parse '1/4'");
        CHECK (LoopLength::bars (4).next().value == 5.0, "bar ladder steps by one");
    }

    // fine adjust
    {
        engine.post (DeckCommand::AutoLoop, 0, 4.0);
        rig.tick();
        const double le0 = d0.uiLoopEnd.load();
        engine.post (DeckCommand::LoopAdjustOut, 0, 1.0);
        rig.tick();
        CHECK (std::abs (d0.uiLoopEnd.load() - (le0 + 1.0)) < 1e-6, "1-sample loop out nudge");
        const double ls0 = d0.uiLoopStart.load();
        engine.post (DeckCommand::LoopAdjustIn, 0, -1.0);
        rig.tick();
        CHECK (std::abs (d0.uiLoopStart.load() - (ls0 - 1.0)) < 1e-6, "1-sample loop in nudge");
        engine.post (DeckCommand::LoopAdjustOut, 0, house->sampleRate * 0.010);   // +10 ms with zero-cross snap
        rig.tick();
        const double moved = d0.uiLoopEnd.load() - (le0 + 1.0);
        CHECK (std::abs (moved - house->sampleRate * 0.010) <= 257.0, "10 ms nudge moved %.1f frames", moved);
        engine.post (DeckCommand::LoopHalve, 0);
        rig.tick();
        CHECK (std::abs (d0.uiLoopBeats.load() - (d0.uiLoopEnd.load() - d0.uiLoopStart.load()) / fpb) < 1e-6, "halve keeps beats consistent");
        engine.post (DeckCommand::LoopExit, 0);
        rig.tick();
        std::printf ("  fine adjust: 1-sample nudges exact, 10 ms nudge moved %.2f ms after zero-cross snap\n", moved / house->sampleRate * 1000.0);
    }

    // =======================================================================
    std::printf ("\n== 4. Loop roll / slip returns to the right place ==\n");
    {
        rig.render (0.5);
        const double p0 = d0.uiPosition.load();
        engine.post (DeckCommand::LoopRollDown, 0, 0.25);
        rig.render (1.0);
        engine.post (DeckCommand::LoopRollUp, 0);
        rig.renderSamples (512);
        const double expect = p0 + (1.0 * sr + 512) * srr;
        const double errMs = (d0.uiPosition.load() - expect) / house->sampleRate * 1000.0;
        std::printf ("  roll return error: %.2f ms\n", errMs);
        CHECK (std::abs (errMs) < 15.0, "roll return off by %.2f ms", errMs);
        CHECK (! d0.uiLoopActive.load(), "roll left a loop active");
    }

    // =======================================================================
    std::printf ("\n== 5. Hot cues with quantize keep phase ==\n");
    {
        engine.post (DeckCommand::HotCueDown, 0, 0.0, 0);
        engine.post (DeckCommand::HotCueUp, 0, 0.0, 0);
        rig.tick();
        const double hc = house->hotCues[0].load();
        CHECK (hc >= 0.0 && std::abs (wrapHalf (house->grid.beatAt (hc, house->sampleRate))) < 1e-6, "hot cue not on beat");
        rig.render (2.37);
        const double phaseBefore = d0.uiBeatPhase.load();
        engine.post (DeckCommand::HotCueDown, 0, 0.0, 0);
        engine.post (DeckCommand::HotCueUp, 0, 0.0, 0);
        rig.renderSamples (64);
        const double phaseAfter = d0.uiBeatPhase.load();
        const double advance = 64.0 * srr / fpb;
        const double err = wrapHalf (phaseAfter - phaseBefore - advance);
        std::printf ("  phase before %.4f after %.4f (err %.5f beats)\n", phaseBefore, phaseAfter, err);
        CHECK (std::abs (err) < 0.002, "hot cue jump broke phase (%.5f beats)", err);
    }

    // =======================================================================
    std::printf ("\n== 6. Beat sync (tempo + phase) ==\n");
    {
        auto groove = loadAndWait (engine, dir.getChildFile ("groove120_wav16.wav"));
        engine.loadTrack (1, groove);
        rig.tick();
        engine.post (DeckCommand::SyncOn, 1);
        engine.post (DeckCommand::Play, 1);
        rig.render (6.0);

        auto& d1 = engine.getDeck (1);
        const double b0 = house->grid.beatAt (d0.uiPosition.load(), house->sampleRate);
        const double b1 = groove->grid.beatAt (d1.uiPosition.load(), groove->sampleRate);
        const double phaseErr = wrapHalf (b0 - b1);
        std::printf ("  deck2 bpm %.3f (master %.3f), phase error %.4f beats (%.2f ms), master deck %d\n",
                     d1.uiEffectiveBpm.load(), engine.uiMasterBpm.load(), phaseErr,
                     phaseErr * 60.0 / 126.0 * 1000.0, engine.uiMasterDeck.load() + 1);
        CHECK (std::abs (d1.uiEffectiveBpm.load() - 126.0) < 0.02, "synced bpm %.3f", d1.uiEffectiveBpm.load());
        CHECK (std::abs (phaseErr) < 0.01, "synced phase error %.4f beats", phaseErr);
        CHECK (engine.uiMasterDeck.load() == 0, "master should be deck 1");

        // master changes tempo -> slave follows
        d0.pitch = 0.5f;   // +4 % with ±8 % range
        rig.render (4.0);
        const double b0b = house->grid.beatAt (d0.uiPosition.load(), house->sampleRate);
        const double b1b = groove->grid.beatAt (d1.uiPosition.load(), groove->sampleRate);
        std::printf ("  after master +4%%: master %.3f slave %.3f, phase err %.4f beats\n",
                     d0.uiEffectiveBpm.load(), d1.uiEffectiveBpm.load(), wrapHalf (b0b - b1b));
        CHECK (std::abs (d1.uiEffectiveBpm.load() - 131.04) < 0.05, "slave did not follow master tempo");
        CHECK (std::abs (wrapHalf (b0b - b1b)) < 0.01, "phase lost after tempo change");

        // keylock on the slave keeps sync
        d1.keyLock = true;
        rig.render (4.0);
        const double b0c = house->grid.beatAt (d0.uiPosition.load(), house->sampleRate);
        const double b1c = groove->grid.beatAt (d1.uiPosition.load(), groove->sampleRate);
        std::printf ("  slave with keylock: phase err %.4f beats\n", wrapHalf (b0c - b1c));
        CHECK (std::abs (wrapHalf (b0c - b1c)) < 0.015, "keylock slave lost phase");

        engine.post (DeckCommand::SyncOff, 1);
        engine.post (DeckCommand::Pause, 1);
        d1.keyLock = false;
        d0.pitch = 0.0f;
        engine.post (DeckCommand::Pause, 0);
        rig.tick();
    }

    // =======================================================================
    std::printf ("\n== 7. Key lock algorithms: pitch, speed, and real audio alignment ==\n");
    {
        auto tone = loadAndWait (engine, dir.getChildFile ("tone440.wav"));
        engine.loadTrack (2, tone);
        auto& d2 = engine.getDeck (2);
        d2.pitchRange = 0.08f;
        d2.pitch = 1.0f;            // +8 %
        engine.post (DeckCommand::Play, 2);
        rig.render (0.3);

        // varispeed reference
        d2.keyLock = false;
        rig.render (1.0);
        const double fNoLock = zeroCrossFrequency (rig.l, sr, rig.l.size() / 2);
        std::printf ("  varispeed: %.1f Hz (pitch follows tempo)\n", fNoLock);
        CHECK (std::abs (fNoLock - 475.2) < 4.0, "varispeed pitch %.1f Hz", fNoLock);

        // key shift without key lock: +2 semitones on top of varispeed
        d2.keyShift = 2.0f;
        rig.render (1.5);
        const double fShiftVari = zeroCrossFrequency (rig.l, sr, rig.l.size() / 2);
        d2.keyShift = 0.0f;
        std::printf ("  varispeed + 2 st: %.1f Hz (expect %.1f)\n", fShiftVari, 475.2 * std::pow (2.0, 2.0 / 12.0));
        CHECK (std::abs (fShiftVari - 475.2 * std::pow (2.0, 2.0 / 12.0)) < 6.0, "varispeed key shift %.1f Hz", fShiftVari);

        d2.keyLock = true;
        for (int algo = 0; algo < numStretchAlgorithms; ++algo)
        {
            if (! stretchAlgorithmAvailable (algo)) { std::printf ("  %-22s not built\n", stretchAlgorithmName (algo).toRawUTF8()); continue; }
            d2.setStretchAlgorithm (algo);
            engine.post (DeckCommand::SeekFraction, 2, 0.05);
            engine.post (DeckCommand::Play, 2);
            rig.render (0.6);
            const double p0 = d2.uiPosition.load();
            rig.render (2.0);
            const double p1 = d2.uiPosition.load();
            const double f = zeroCrossFrequency (rig.l, sr, rig.l.size() / 2);
            const double speed = (p1 - p0) / (2.0 * tone->sampleRate);
            const double level = rms (std::vector<float> (rig.l.begin() + (long) rig.l.size() / 2, rig.l.end()));

            d2.keyShift = -3.0f;
            rig.render (1.5);
            const double fShift = zeroCrossFrequency (rig.l, sr, rig.l.size() / 2);
            d2.keyShift = 0.0f;

            const bool vintage = algo >= (int) StretchAlgorithm::VintageSampler;
            std::printf ("  %-22s %6.1f Hz  speed x%.4f  level %.3f  -3 st: %6.1f Hz\n",
                         stretchAlgorithmName (algo).toRawUTF8(), f, speed, level, fShift);
            CHECK (std::abs (f - 440.0) < (vintage ? 10.0 : 4.0), "%s pitch %.1f Hz", stretchAlgorithmName (algo).toRawUTF8(), f);
            CHECK (std::abs (speed - 1.08) < 0.01, "%s speed %.4f", stretchAlgorithmName (algo).toRawUTF8(), speed);
            CHECK (level > 0.05, "%s output too quiet (%.3f)", stretchAlgorithmName (algo).toRawUTF8(), level);
            CHECK (std::abs (fShift - 440.0 * std::pow (2.0, -3.0 / 12.0)) < (vintage ? 15.0 : 5.0), "%s key shift %.1f Hz",
                   stretchAlgorithmName (algo).toRawUTF8(), fShift);
        }
        d2.keyLock = false;
        d2.setStretchAlgorithm (0);
        engine.post (DeckCommand::Pause, 2);
        rig.tick();
    }

    // Does the stretched audio really land on the beat grid? Detect kicks in the
    // rendered output and compare with where the deck says it is.
    {
        auto clicks = loadAndWait (engine, dir.getChildFile ("clicks120.wav"));
        clicks->grid.bpm = 120.0;                                  // known exactly
        clicks->grid.anchorFrame = 0.25 * clicks->sampleRate;
        engine.loadTrack (0, clicks);
        rig.tick();

        auto alignmentMs = [&] (bool keyLock, int algo)
        {
            auto& d = engine.getDeck (0);
            d.keyLock = keyLock;
            d.setStretchAlgorithm (algo);
            d.pitch = 0.5f;   // +4 %
            engine.post (DeckCommand::SeekBeats, 0, 4.0);
            engine.post (DeckCommand::Play, 0);
            const int blocks = (int) (5.0 * sr / 512);
            std::vector<float> mono ((size_t) blocks * 512);
            std::vector<double> endPos ((size_t) blocks);
            std::vector<float> bl (512), br (512);
            for (int b = 0; b < blocks; ++b)
            {
                float* outs[2] = { bl.data(), br.data() };
                engine.renderOffline (outs, 2, 512);
                for (int i = 0; i < 512; ++i) mono[(size_t) (b * 512 + i)] = 0.5f * (bl[(size_t) i] + br[(size_t) i]);
                endPos[(size_t) b] = d.uiPosition.load();
            }
            engine.post (DeckCommand::Pause, 0);
            rig.tick();

            // energy-weighted centre of each click burst, mapped through the deck's reported position
            const double rate = 1.04 * clicks->sampleRate / sr;
            const double fpb = clicks->grid.framesPerBeat (clicks->sampleRate);
            std::vector<double> errs;
            const int span = (int) (0.06 * sr);
            for (double t = 2.0 * 0.5 / 1.04; t < 4.6; t += 0.5 / 1.04)
            {
                // search around each expected click for the energy centre
                const int c = (int) (t * sr);
                double wsum = 0, isum = 0;
                for (int i = juce::jmax (0, c - span); i < juce::jmin ((int) mono.size(), c + span); ++i)
                {
                    const double e = (double) mono[(size_t) i] * mono[(size_t) i];
                    wsum += e; isum += e * i;
                }
                if (wsum < 1e-6) continue;
                const double s = isum / wsum;
                const auto blk = (size_t) (s / 512.0);
                if (blk < 2 || blk >= endPos.size()) continue;
                const double pos = endPos[blk] - ((double) (blk + 1) * 512.0 - s) * rate;
                errs.push_back (wrapHalf (clicks->grid.beatAt (pos, clicks->sampleRate)) * fpb / clicks->sampleRate * 1000.0);
            }
            if (errs.empty()) return 999.0;
            std::nth_element (errs.begin(), errs.begin() + (long) errs.size() / 2, errs.end());
            return errs[errs.size() / 2];
        };

        const double reference = alignmentMs (false, 0);
        std::printf ("  kick alignment, varispeed reference: %.2f ms\n", reference);
        for (int algo = 0; algo < numStretchAlgorithms; ++algo)
        {
            if (! stretchAlgorithmAvailable (algo)) continue;
            const double err = alignmentMs (true, algo) - reference;
            const bool vintage = algo >= (int) StretchAlgorithm::VintageSampler;
            std::printf ("  %-22s audio lands %+.2f ms from the grid\n", stretchAlgorithmName (algo).toRawUTF8(), err);
            CHECK (std::abs (err) < (vintage ? 25.0 : 6.0), "%s audio off the grid by %.2f ms", stretchAlgorithmName (algo).toRawUTF8(), err);
        }
        engine.getDeck (0).keyLock = false;
        engine.getDeck (0).pitch = 0.0f;
        engine.getDeck (0).setStretchAlgorithm (0);
        engine.loadTrack (0, house);
        rig.tick();
    }

    // =======================================================================
    std::printf ("\n== 7b. Full tempo range: 0%% to 200%% ==\n");
    {
        auto tone = loadAndWait (engine, dir.getChildFile ("tone440.wav"));
        engine.loadTrack (2, tone);
        auto& d2 = engine.getDeck (2);
        d2.pitchRange = 1.0f;
        for (bool lock : { false, true })
        {
            d2.keyLock = lock;
            d2.setStretchAlgorithm (0);
            for (float fader : { 1.0f, 0.5f, -0.5f, -1.0f })
            {
                d2.pitch = fader;
                engine.post (DeckCommand::SeekFraction, 2, 0.1);
                engine.post (DeckCommand::Play, 2);
                rig.render (0.5);
                const double p0 = d2.uiPosition.load();
                rig.render (2.0);
                const double speed = (d2.uiPosition.load() - p0) / (2.0 * tone->sampleRate);
                const double expect = 1.0 + fader;
                const double f = zeroCrossFrequency (rig.l, sr, rig.l.size() / 2);
                const double level = rms (rig.l);
                const double expectF = lock ? 440.0 : 440.0 * expect;
                std::printf ("  %-10s tempo %3.0f%%: speed x%.3f, %6.1f Hz, level %.3f\n", lock ? "key lock" : "varispeed",
                             expect * 100.0, speed, f, level);
                CHECK (std::abs (speed - expect) < 0.01, "tempo %.0f%% speed %.3f", expect * 100.0, speed);
                if (expect > 0.01)
                    CHECK (std::abs (f - expectF) < expectF * 0.02 + 2.0, "tempo %.0f%% pitch %.1f Hz (expected %.1f)", expect * 100.0, f, expectF);
                else
                    CHECK (level < 1e-4, "tempo 0%% should be silent (level %.5f)", level);
                engine.post (DeckCommand::Pause, 2);
                rig.tick();
            }
        }
        d2.pitch = 0.0f;
        d2.pitchRange = 0.08f;
        d2.keyLock = false;
    }

    // =======================================================================
    std::printf ("\n== 8. Mixer: EQ kill, filter, crossfader, cue routing ==\n");
    {
        engine.post (DeckCommand::Play, 0);
        rig.render (1.0);
        const double base = rms (rig.l);
        auto& ch = engine.getMixer().channels[0];
        ch.eqLow = 0.0f; ch.eqMid = 0.0f; ch.eqHigh = 0.0f;
        rig.render (1.0);
        const double killed = rms (std::vector<float> (rig.l.begin() + (long) rig.l.size() / 2, rig.l.end()));
        ch.eqLow = 0.5f; ch.eqMid = 0.5f; ch.eqHigh = 0.5f;
        ch.filter = 0.95f;   // high-pass nearly closed
        rig.render (1.0);
        const double filtered = rms (rig.l);
        ch.filter = 0.0f;
        engine.getMixer().master.crossfader = 1.0f;   // fully to B; deck 1 is on A
        rig.render (0.5);
        const double xfaded = rms (std::vector<float> (rig.l.begin() + (long) rig.l.size() / 2, rig.l.end()));
        engine.getMixer().master.crossfader = 0.0f;
        std::printf ("  rms: base %.4f, all EQ killed %.6f, HPF closed %.4f, crossfaded out %.6f\n", base, killed, filtered, xfaded);
        CHECK (killed < base * 0.02, "EQ kill not killing");
        CHECK (filtered < base * 0.3, "filter not filtering");
        CHECK (xfaded < base * 0.01, "crossfader not cutting");
        CHECK (base > 0.05, "no output");

        // cue routing to channels 3/4
        engine.cueOutChannel = 2;
        ch.cue = true;
        std::vector<float> o0 (4096), o1 (4096), o2 (4096), o3 (4096);
        float* outs[4] = { o0.data(), o1.data(), o2.data(), o3.data() };
        engine.renderOffline (outs, 4, 4096);
        std::vector<float> cueCh (o2.begin(), o2.end());
        CHECK (rms (cueCh) > 0.01, "cue output silent");
        ch.cue = false;
        engine.post (DeckCommand::Pause, 0);
        rig.tick();
    }

    // =======================================================================
    std::printf ("\n== 9. Performance: 8 decks, keylock on, all playing ==\n");
    {
        engine.setNumDecks (8);
        auto dnb = loadAndWait (engine, dir.getChildFile ("dnb174_wav24.wav"));
        for (int i = 0; i < 8; ++i)
        {
            engine.loadTrack (i, i % 2 == 0 ? house : dnb);
            engine.getDeck (i).keyLock = true;
            engine.getDeck (i).pitch = 0.3f;
            engine.post (DeckCommand::Play, i);
        }
        rig.tick();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        rig.render (10.0);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  first pass (Light): 10 s of audio in %.0f ms\n", ms);
        auto measureVarispeed = [&]
        {
            for (int i = 0; i < 8; ++i) engine.getDeck (i).keyLock = false;
            rig.render (0.5);
            const auto ta = juce::Time::getMillisecondCounterHiRes();
            rig.render (5.0);
            const auto msA = juce::Time::getMillisecondCounterHiRes() - ta;
            std::printf ("  %-22s 8 decks:               %5.1f%% of one core (%.2f%% per deck)\n", "Varispeed", msA / 50.0, msA / 400.0);
            for (int i = 0; i < 8; ++i) engine.getDeck (i).keyLock = true;
        };
        measureVarispeed();
        for (int algo = 0; algo < numStretchAlgorithms; ++algo)
        {
            for (int i = 0; i < 8; ++i) engine.getDeck (i).setStretchAlgorithm (algo);
            rig.render (0.5);
            const auto ta = juce::Time::getMillisecondCounterHiRes();
            rig.render (5.0);
            const auto msA = juce::Time::getMillisecondCounterHiRes() - ta;
            std::printf ("  %-22s 8 decks with key lock: %5.1f%% of one core (%.2f%% per deck)\n",
                         stretchAlgorithmName (algo).toRawUTF8(), msA / 50.0, msA / 400.0);
        }
        measureVarispeed();
        for (int i = 0; i < 8; ++i) engine.getDeck (i).setStretchAlgorithm (0);
        bool finite = true;
        for (auto v : rig.l) if (! std::isfinite (v)) { finite = false; break; }
        CHECK (finite, "non-finite output");
        for (int i = 0; i < 8; ++i) engine.post (DeckCommand::Pause, i);
        rig.tick();
    }

    // =======================================================================
    if (argc > 2)
    {
        std::printf ("\n== 10. Large file ==\n");
        const juce::File big (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto t = engine.getLoader().load (big);
        // playable before decoding finishes
        while (t->framesReady.load() < (juce::int64) (t->sampleRate * 10) && ! t->decodeFailed.load())
            juce::Thread::sleep (2);
        const auto firstAudio = juce::Time::getMillisecondCounterHiRes() - t0;
        engine.getLoader().waitUntilIdle (600000);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  %s: %.1f min, first 10 s playable after %.0f ms, full decode+analysis %.1f s, bpm %.2f\n",
                     big.getFileName().toRawUTF8(), t->getLengthSeconds() / 60.0, firstAudio, ms / 1000.0, t->grid.bpm.load());
        CHECK (t->decodeComplete.load(), "large file failed to decode");
        engine.loadTrack (0, t);
        engine.post (DeckCommand::SeekFraction, 0, 0.98);
        engine.post (DeckCommand::Play, 0);
        rig.render (1.0);
        CHECK (rms (rig.l) > 0.01, "no audio near the end of the large file");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    for (int i = 0; i < maxDecks; ++i) engine.ejectTrack (i);
    juce::Thread::sleep (20);
    engine.housekeeping();
    return failures == 0 ? 0 : 1;
}
