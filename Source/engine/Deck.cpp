#include "Deck.h"

namespace loopdeck
{

// ===========================================================================
// LoopLength
// ===========================================================================
static const double beatLadder[] = { 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2,
                                     1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const double barLadder[]  = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 48, 64 };

juce::String LoopLength::toString() const
{
    if (unit == Unit::Bars)
        return juce::String ((int) std::round (value)) + (std::abs (value - 1.0) < 1e-9 ? " BAR" : " BARS");

    if (value < 1.0 - 1e-9)
        return "1/" + juce::String ((int) std::round (1.0 / value)) + " BEAT";

    if (std::abs (value - std::round (value)) < 1e-9)
        return juce::String ((int) std::round (value)) + (std::abs (value - 1.0) < 1e-9 ? " BEAT" : " BEATS");

    return juce::String (value, 2) + " BEATS";
}

template <size_t N>
static double stepLadder (const double (&ladder)[N], double v, int dir)
{
    if (dir > 0)
    {
        for (auto x : ladder) if (x > v + 1e-9) return x;
        return ladder[N - 1];
    }
    for (int i = (int) N - 1; i >= 0; --i) if (ladder[i] < v - 1e-9) return ladder[i];
    return ladder[0];
}

LoopLength LoopLength::next() const
{
    return unit == Unit::Bars ? bars (stepLadder (barLadder, value, 1)) : beats (stepLadder (beatLadder, value, 1));
}

LoopLength LoopLength::previous() const
{
    return unit == Unit::Bars ? bars (stepLadder (barLadder, value, -1)) : beats (stepLadder (beatLadder, value, -1));
}

bool LoopLength::parse (const juce::String& text, Unit defaultUnit, LoopLength& out)
{
    auto t = text.trim().toLowerCase();
    if (t.isEmpty()) return false;

    Unit u = defaultUnit;
    if (t.contains ("bar")) u = Unit::Bars;
    else if (t.contains ("beat") || t.contains ("bt")) u = Unit::Beats;

    auto num = t.retainCharacters ("0123456789./");
    double v = 0.0;
    if (num.contains ("/"))
    {
        const double a = num.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
        const double b = num.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
        if (b <= 0.0) return false;
        v = a / b;
    }
    else
        v = num.getDoubleValue();

    if (v <= 0.0 || v > 512.0) return false;
    if (u == Unit::Bars && v < 1.0) { u = Unit::Beats; v *= 4.0; }
    out = { v, u };
    return true;
}

// ===========================================================================
// Deck
// ===========================================================================
Deck::Deck (int index) : deckIndex (index) {}

Deck::~Deck()
{
    if (track != nullptr)
        track->decReferenceCount();
}

void Deck::prepare (double sr, int maximumBlockSize)
{
    deviceSampleRate = sr;
    maxBlock = juce::jmax (32, maximumBlockSize);

    source.deck = this;

    // (re)build every stretcher that exists, plus the default; the audio device is stopped here
    const juce::ScopedLock sl (stretcherBuildLock);
    for (int a = 0; a < numStretchAlgorithms; ++a)
    {
        const bool wanted = a == 0 || stretcherStore[(size_t) a] != nullptr;
        if (! wanted) continue;
        stretcherSlots[(size_t) a] = nullptr;
        stretcherStore[(size_t) a] = createTimeStretcher (a);
        stretcherStore[(size_t) a]->prepare (sr, maxBlock);
        stretcherSlots[(size_t) a] = stretcherStore[(size_t) a].get();
    }
    stretchPrimed = false;
    activeAlgorithm = -1;
}

void Deck::setStretchAlgorithm (int algorithm)
{
    if (! stretchAlgorithmAvailable (algorithm)) algorithm = 0;
    {
        const juce::ScopedLock sl (stretcherBuildLock);
        if (stretcherStore[(size_t) algorithm] == nullptr)
        {
            auto s = createTimeStretcher (algorithm);
            s->prepare (deviceSampleRate, maxBlock);
            stretcherStore[(size_t) algorithm] = std::move (s);
            stretcherSlots[(size_t) algorithm] = stretcherStore[(size_t) algorithm].get();
        }
    }
    stretchAlgorithm = algorithm;
}

TimeStretcher* Deck::currentStretcher() noexcept
{
    const int a = juce::jlimit (0, numStretchAlgorithms - 1, stretchAlgorithm.load());
    if (auto* s = stretcherSlots[(size_t) a].load()) return s;
    return stretcherSlots[0].load();
}

double Deck::transposeFactor (double rate) const noexcept
{
    const double shift = std::pow (2.0, (double) keyShift.load() / 12.0);
    return keyLock.load() ? shift : shift * rate;   // varispeed + shift: pitch still follows tempo
}

// ---------------------------------------------------------------------------
double Deck::sampleRateRatio() const noexcept
{
    return track != nullptr ? track->sampleRate / deviceSampleRate : 1.0;
}

double Deck::framesPerBeat() const noexcept
{
    return track != nullptr ? track->grid.framesPerBeat (track->sampleRate) : 22050.0;
}

double Deck::baseTempoRatio (const SyncContext& ctx) const noexcept
{
    const bool selfMaster = ctx.deckIsMaster && ctx.masterIndex == deckIndex;
    if (syncEnabled.load() && hasGrid() && ! selfMaster && ctx.masterBpm > 1.0)
    {
        // match at the nearest octave: 174 against 126 plays at 126, not 252
        double r = ctx.masterBpm / track->grid.bpm.load();
        while (r > juce::MathConstants<double>::sqrt2) r *= 0.5;
        while (r < 1.0 / juce::MathConstants<double>::sqrt2) r *= 2.0;
        return r;
    }
    return 1.0 + (double) pitch.load() * (double) pitchRange.load();
}

double Deck::getEffectiveBpm() const noexcept
{
    return hasGrid() ? track->grid.bpm.load() * uiTempoRatio.load() : 0.0;
}

double Deck::computeAudiblePosition() const noexcept
{
    // while stretching, the read head runs ahead; the audible point is tracked separately
    return stretchPrimed ? stretchPlayhead : readPos;
}

double Deck::getBeatPosition() const noexcept
{
    if (! hasGrid()) return 0.0;
    return track->grid.beatAt (computeAudiblePosition(), track->sampleRate);
}

double Deck::quantizeToBeat (double frame, double unitBeats) const noexcept
{
    if (! hasGrid() || unitBeats <= 0.0) return frame;
    const double b = track->grid.beatAt (frame, track->sampleRate) / unitBeats;
    return track->grid.frameOfBeat (std::floor (b + 1e-6) * unitBeats, track->sampleRate);
}

double Deck::phasePreservingTarget (double target) const noexcept
{
    if (! hasGrid()) return target;
    const double cur = track->grid.beatAt (computeAudiblePosition(), track->sampleRate);
    const double frac = cur - std::floor (cur);
    const double tb = track->grid.beatAt (target, track->sampleRate);
    const double aligned = std::floor (tb - frac + 0.5) + frac;
    return track->grid.frameOfBeat (aligned, track->sampleRate);
}

double Deck::snapToZeroCrossing (double frame) const noexcept
{
    if (track == nullptr) return frame;
    const auto centre = (juce::int64) std::llround (frame);
    float pl, pr;
    track->readFrame (centre - 257, pl, pr);
    float prev = pl + pr;
    double best = frame; double bestDist = 1e9;
    for (juce::int64 i = centre - 256; i <= centre + 256; ++i)
    {
        float l, r;
        track->readFrame (i, l, r);
        const float cur = l + r;
        if ((prev <= 0.0f && cur > 0.0f) || (prev >= 0.0f && cur < 0.0f))
        {
            const double d = std::abs ((double) i - frame);
            if (d < bestDist) { bestDist = d; best = (double) i; }
        }
        prev = cur;
    }
    return best;
}

// ---------------------------------------------------------------------------
void Deck::primeStretcher()
{
    auto* st = currentStretcher();
    if (st == nullptr || track == nullptr) return;
    const double rate = juce::jlimit (0.05, 4.0, uiTempoRatio.load());
    stretchPlayhead = readPos;
    xfadeRemaining = 0;
    source.step = sampleRateRatio();
    st->prime (source, rate, transposeFactor (rate));
    activeAlgorithm = juce::jlimit (0, numStretchAlgorithms - 1, stretchAlgorithm.load());
    stretchPrimed = true;
}

void Deck::jumpTo (double newPos, bool allowQuantize)
{
    if (track == nullptr) return;
    if (allowQuantize && quantize.load() && playing && hasGrid())
        newPos = phasePreservingTarget (newPos);

    if (stretchPrimed)
    {
        readPos = newPos;
        primeStretcher();
        return;
    }

    xfadeOldPos = readPos;
    xfadeRemaining = xfadeLength;
    readPos = newPos;
}

void Deck::exitLoopIfOutside (double target)
{
    // clicking or seeking outside an active loop leaves it, as on CDJs and in VirtualDJ
    if (loopActive && (target < loopStart || target >= loopEnd))
    {
        loopActive = false;
        loopInPending = false;
        if (rollActive || slipActive) { rollActive = false; slipActive = false; }
    }
}

void Deck::startTransportEffect()
{
    const double pos = computeAudiblePosition();
    effectSlipPos = pos;
    if (stretchPrimed) { readPos = pos; stretchPrimed = false; }
    xfadeRemaining = 0;
}

void Deck::endTransportEffect()
{
    braking = backspinning = false;
    brakeFactor = 1.0;
    playing = true;
    jumpTo (juce::jmax (0.0, effectSlipPos), false);
}

void Deck::snapPhaseToMaster (const SyncContext& ctx)
{
    if (! hasGrid() || ctx.masterBpm <= 1.0) return;
    const double effBpm = track->grid.bpm.load() * baseTempoRatio (ctx);
    const double target = ctx.masterBeat * (effBpm / ctx.masterBpm);
    const double mine = getBeatPosition();
    const double err = (target - mine) - std::round (target - mine);
    if (std::abs (err) > 0.002)
        jumpTo (computeAudiblePosition() + err * framesPerBeat(), false);
}

void Deck::startPlayback (const SyncContext& ctx)
{
    if (track == nullptr) return;
    const bool selfMaster = ctx.deckIsMaster && ctx.masterIndex == deckIndex;
    playing = true;
    if (syncEnabled.load() && ! selfMaster)
    {
        uiTempoRatio = baseTempoRatio (ctx);
        snapPhaseToMaster (ctx);
    }
}

void Deck::setLoop (double start, double end)
{
    loopStart = start;
    loopEnd = juce::jmax (start + 16.0, end);
}

void Deck::applyLoopLength (double beats)
{
    loopBeats = juce::jlimit (1.0 / 64.0, 512.0, beats);
    if (loopEnd > loopStart)
    {
        loopEnd = loopStart + loopBeats * framesPerBeat();
        if (loopActive && stretchPrimed)
        {
            double a = computeAudiblePosition();
            if (a >= loopEnd) a = loopStart + std::fmod (a - loopStart, loopEnd - loopStart);
            readPos = a;
            primeStretcher();
        }
    }
}

// ---------------------------------------------------------------------------
void Deck::renderSource (float* L, float* R, int count, double step)
{
    const double len = loopEnd - loopStart;
    const bool looping = loopActive && len > 4.0;
    const bool complete = track->decodeComplete.load();
    const double total = complete ? (double) track->totalFrames.load() : 1.0e18;

    for (int i = 0; i < count; ++i)
    {
        if (! looping && step > 0.0 && readPos >= total)
        {
            for (; i < count; ++i) L[i] = R[i] = 0.0f;
            readPos = total;
            playing = false;
            break;
        }

        float l, r;
        track->readInterpolated (readPos, l, r);

        if (xfadeRemaining > 0)
        {
            float ol, orr;
            track->readInterpolated (xfadeOldPos, ol, orr);
            const float g = 1.0f - (float) xfadeRemaining / (float) xfadeLength;
            l = l * g + ol * (1.0f - g);
            r = r * g + orr * (1.0f - g);
            xfadeOldPos += step;
            --xfadeRemaining;
        }

        L[i] = l;
        R[i] = r;
        readPos += step;

        if (looping && step > 0.0 && readPos >= loopEnd)
        {
            xfadeOldPos = readPos;
            xfadeRemaining = xfadeLength;
            readPos = loopStart + std::fmod (readPos - loopStart, len);
        }
        else if (step < 0.0 && readPos < 0.0)
        {
            readPos = 0.0;
        }
    }
}

// ---------------------------------------------------------------------------
void Deck::process (float* outL, float* outR, int n, const SyncContext& ctx)
{
    if (track == nullptr || track->framesReady.load() <= 0)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        uiPlaying = false;
        uiIsMaster = false;
        uiPosition = readPos;
        return;
    }

    const bool selfMaster = ctx.deckIsMaster && ctx.masterIndex == deckIndex;
    const double srr = sampleRateRatio();
    const double base = baseTempoRatio (ctx);
    const bool audible = playing || cueHeld || hotCueHeld >= 0;

    // keep pitch fader display in step with sync
    if (syncEnabled.load() && hasGrid() && ! selfMaster && ctx.masterBpm > 1.0)
    {
        const float range = juce::jmax (0.01f, pitchRange.load());
        pitch = juce::jlimit (-1.0f, 1.0f, (float) ((base - 1.0) / range));
    }

    // --- phase lock --------------------------------------------------------
    double correction = 0.0;
    float phaseErr = 0.0f;
    if (syncEnabled.load() && hasGrid() && ! selfMaster && playing && ! scratching && ctx.masterBpm > 1.0)
    {
        const double effBpm = track->grid.bpm.load() * base;
        const double target = ctx.masterBeat * (effBpm / ctx.masterBpm);
        const double mine = getBeatPosition();
        const double err = (target - mine) - std::round (target - mine);
        phaseErr = (float) err;
        const double beatsPerSecond = effBpm / 60.0;
        correction = juce::jlimit (-0.03, 0.03, err / (0.25 * beatsPerSecond));
        if (std::abs (err) < 0.0005) correction = 0.0;
    }
    uiPhaseError = phaseErr;
    uiBraking = braking || backspinning;

    // full tempo range: 0 % (stopped) to 200 % (double speed)
    double rate = juce::jlimit (0.0, 2.0, base * (1.0 + correction) + (double) bend.load());
    if (braking || backspinning)
    {
        // the track keeps moving underneath, so release lands where it would have been
        effectSlipPos += (double) n * rate * srr;
        const double len = loopEnd - loopStart;
        if (loopActive && len > 4.0 && effectSlipPos >= loopEnd)
            effectSlipPos = loopStart + std::fmod (effectSlipPos - loopStart, len);
    }
    if (braking)
    {
        if (! playing) braking = false;
        brakeFactor = juce::jmax (0.0, brakeFactor - brakeStep * n);
        rate *= brakeFactor * brakeFactor;   // turntable platters slow fast, then drift; held at a stop until release
        correction = 0.0;
    }
    uiTempoRatio = juce::jlimit (0.0, 2.0, base + (double) bend.load());

    // varispeed unless key lock or a key shift asks for the stretcher
    const bool useKeyLock = (keyLock.load() || std::abs (keyShift.load()) > 0.001f) && ! scratching && audible
                            && ! braking && ! backspinning;

    // leaving keylock: bring the read head back to the audible position
    if (! useKeyLock && stretchPrimed)
    {
        const double a = computeAudiblePosition();
        stretchPrimed = false;
        readPos = a;
    }

    if (backspinning)
    {
        // a hand spin backwards: fast at first, slowing to a stop
        const double t = juce::jmin (1.0, spinElapsed / spinLength);
        const double v = -3.2 * (1.0 - t) * (1.0 - t);
        if (t >= 1.0 || readPos <= 0.0)
        {
            // spun to a stop: silent until the button is let go
            juce::FloatVectorOperations::clear (outL, n);
            juce::FloatVectorOperations::clear (outR, n);
        }
        else
            renderSource (outL, outR, n, v * srr);
        spinElapsed += (double) n / deviceSampleRate;
    }
    else if (scratching)
    {
        const double desired = (scratchTarget - readPos) / (double) n;
        scratchVelocity += (desired - scratchVelocity) * 0.35;
        renderSource (outL, outR, n, scratchVelocity);
    }
    else if (audible)
    {
        if (useKeyLock)
        {
            // (re)prime when starting, or when the algorithm was switched
            if (stretchPrimed && activeAlgorithm != stretchAlgorithm.load())
                readPos = stretchPlayhead;
            if (! stretchPrimed || activeAlgorithm != stretchAlgorithm.load())
                primeStretcher();

            if (rate < 0.01)
            {
                // tempo at (or next to) zero: a stopped record is silent, not a frozen buzz
                juce::FloatVectorOperations::clear (outL, n);
                juce::FloatVectorOperations::clear (outR, n);
            }
            else if (auto* st = currentStretcher())
            {
                source.step = srr;
                st->process (source, outL, outR, n, rate, transposeFactor (base));
            }

            stretchPlayhead += (double) n * rate * srr;
            const double len = loopEnd - loopStart;
            if (loopActive && len > 4.0 && stretchPlayhead >= loopEnd)
                stretchPlayhead = loopStart + std::fmod (stretchPlayhead - loopStart, len);
        }
        else if (rate < 1e-4)
        {
            juce::FloatVectorOperations::clear (outL, n);
            juce::FloatVectorOperations::clear (outR, n);
        }
        else
        {
            renderSource (outL, outR, n, rate * srr);
        }
    }
    else
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
    }

    if (slipActive && (playing || (scratching && wasPlayingBeforeScratch) || rollActive))
        slipPos += (double) n * rate * srr;

    // --- publish -----------------------------------------------------------
    const double pos = computeAudiblePosition();
    uiPosition = pos;
    uiPlaying = playing;
    uiLoopActive = loopActive;
    uiLoopStart = loopStart;
    uiLoopEnd = loopEnd;
    uiLoopBeats = loopBeats;
    uiIsMaster = selfMaster;
    uiSlipActive = slipActive;
    uiSlipPosition = slipPos;
    uiEffectiveBpm = hasGrid() ? track->grid.bpm.load() * (base + (double) bend.load()) : 0.0;
    if (hasGrid())
    {
        const double b = track->grid.beatAt (pos, track->sampleRate);
        uiBeatPhase = b - std::floor (b);
    }
}

// ---------------------------------------------------------------------------
Track* Deck::handleCommand (const DeckCommand& cmd, const SyncContext& ctx)
{
    using C = DeckCommand;

    if (cmd.type == C::LoadTrack)
    {
        Track* old = track;
        track = cmd.track;
        playing = cueHeld = scratching = false;
        braking = backspinning = false;
        hotCueHeld = -1;
        loopActive = loopInPending = slipActive = rollActive = false;
        loopStart = loopEnd = 0.0;
        xfadeRemaining = 0;
        stretchPrimed = false;
        readPos = track != nullptr ? juce::jmax (0.0, track->cuePoint.load()) : 0.0;
        return old;
    }

    if (cmd.type == C::Eject)
    {
        Track* old = track;
        track = nullptr;
        playing = false;
        loopActive = false;
        stretchPrimed = false;
        braking = backspinning = false;
        readPos = 0.0;
        return old;
    }

    if (track == nullptr)
        return nullptr;

    const double fpb = framesPerBeat();
    const double sr = track->sampleRate;
    auto audiblePos = [this] { return computeAudiblePosition(); };
    auto nearestBeat = [&] (double f)
    {
        if (! quantize.load() || ! hasGrid()) return f;
        const double b = track->grid.beatAt (f, sr);
        return track->grid.frameOfBeat (std::round (b), sr);
    };
    auto autoLoopAt = [&] (double beats)
    {
        const double pos = audiblePos();
        double start = pos;
        if (quantize.load() && hasGrid())
            start = quantizeToBeat (pos, beats >= 1.0 ? 1.0 : beats);
        loopBeats = juce::jlimit (1.0 / 64.0, 512.0, beats);
        setLoop (start, start + loopBeats * fpb);
        loopActive = true;
        loopInPending = false;
        if (stretchPrimed) { readPos = pos; primeStretcher(); }
    };

    switch (cmd.type)
    {
        case C::Play:
            if (braking || backspinning) { endTransportEffect(); break; }
            if (cueHeld || hotCueHeld >= 0) { cueHeld = false; hotCueHeld = -1; playing = true; }
            else if (! playing) startPlayback (ctx);
            break;

        case C::Pause:
            playing = false;
            braking = backspinning = false;
            break;

        case C::TogglePlay:
            if (braking || backspinning) { endTransportEffect(); break; }
            if (cueHeld || hotCueHeld >= 0) { cueHeld = false; hotCueHeld = -1; playing = true; }
            else if (playing) playing = false;
            else startPlayback (ctx);
            break;

        case C::CueDown:
        {
            if (playing)
            {
                playing = false;
                stretchPrimed = false;
                jumpTo (track->cuePoint.load(), false);
            }
            else
            {
                const double pos = audiblePos();
                if (std::abs (pos - track->cuePoint.load()) > 2.0)
                {
                    const double c = nearestBeat (pos);
                    track->cuePoint = c;
                    track->cuesDirty = true;
                    jumpTo (c, false);
                }
                else
                {
                    cueHeld = true;
                    cueReturn = track->cuePoint.load();
                    jumpTo (cueReturn, false);
                    if (syncEnabled.load()) snapPhaseToMaster (ctx);
                }
            }
            break;
        }

        case C::CueUp:
            if (cueHeld)
            {
                cueHeld = false;
                if (! playing) { stretchPrimed = false; jumpTo (cueReturn, false); }
            }
            break;

        case C::SetCuePoint:
            track->cuePoint = nearestBeat (audiblePos());
            track->cuesDirty = true;
            break;

        case C::HotCueDown:
        {
            if (cmd.index < 0 || cmd.index >= numHotCues) break;
            auto& hc = track->hotCues[(size_t) cmd.index];
            if (hc.load() < 0.0)
            {
                hc = nearestBeat (audiblePos());
                track->cuesDirty = true;
            }
            else if (playing)
            {
                jumpTo (hc.load(), true);
            }
            else
            {
                stretchPrimed = false;
                jumpTo (hc.load(), false);
                hotCueHeld = cmd.index;
                hotCueReturn = hc.load();
                if (syncEnabled.load()) snapPhaseToMaster (ctx);
            }
            break;
        }

        case C::HotCueUp:
            if (hotCueHeld == cmd.index)
            {
                hotCueHeld = -1;
                if (! playing) { stretchPrimed = false; jumpTo (hotCueReturn, false); }
            }
            break;

        case C::HotCueDelete:
            if (cmd.index >= 0 && cmd.index < numHotCues)
            {
                track->hotCues[(size_t) cmd.index] = -1.0;
                track->cuesDirty = true;
            }
            break;

        case C::SeekFrames:
            exitLoopIfOutside (juce::jmax (0.0, cmd.a));
            jumpTo (juce::jmax (0.0, cmd.a), false);
            break;

        case C::SeekFraction:
        {
            const double target = juce::jlimit (0.0, 1.0, cmd.a) * (double) track->totalFrames.load();
            exitLoopIfOutside (target);
            jumpTo (target, false);
            break;
        }

        case C::Brake:
            if (playing && ! backspinning && ! braking)
            {
                startTransportEffect();
                braking = true;
                brakeFactor = 1.0;
                brakeStep = 1.0 / (juce::jlimit (0.05, 10.0, cmd.a) * deviceSampleRate);
            }
            break;

        case C::Backspin:
            if (playing && ! backspinning)
            {
                startTransportEffect();
                braking = false;
                backspinning = true;
                spinElapsed = 0.0;
                spinLength = juce::jlimit (0.1, 10.0, cmd.a);
            }
            break;

        case C::BrakeEnd:
            if (braking) endTransportEffect();
            break;

        case C::BackspinEnd:
            if (backspinning) endTransportEffect();
            break;

        case C::SeekBeats:
            if (hasGrid()) jumpTo (track->grid.frameOfBeat (cmd.a, sr), false);
            break;

        case C::BeatJump:
            if (loopActive)
            {
                loopStart += cmd.a * fpb;
                loopEnd += cmd.a * fpb;
            }
            jumpTo (juce::jmax (0.0, audiblePos() + cmd.a * fpb), false);
            break;

        case C::AutoLoop:
            autoLoopAt (cmd.a);
            if (slipMode.load() && ! slipActive) { slipActive = true; slipPos = audiblePos(); }
            break;

        case C::ToggleLoop:
            if (loopActive)
            {
                loopActive = false;
                if (slipActive && ! rollActive) { jumpTo (slipPos, false); slipActive = false; }
            }
            else
            {
                if (slipMode.load()) { slipActive = true; slipPos = audiblePos(); }
                autoLoopAt (cmd.a);
            }
            break;

        case C::SetLoopLength:
            applyLoopLength (cmd.a);
            break;

        case C::LoopHalve:
            applyLoopLength (loopBeats * 0.5);
            break;

        case C::LoopDouble:
            applyLoopLength (loopBeats * 2.0);
            break;

        case C::LoopMove:
            if (loopEnd > loopStart)
            {
                const double d = cmd.a * fpb;
                loopStart += d;
                loopEnd += d;
                if (loopActive) jumpTo (juce::jmax (0.0, audiblePos() + d), false);
            }
            else
            {
                jumpTo (juce::jmax (0.0, audiblePos() + cmd.a * fpb), false);
            }
            break;

        case C::LoopAdjustIn:
            if (loopEnd > loopStart)
            {
                double s = snapToZeroCrossing (loopStart + cmd.a);
                if (std::abs (cmd.a) < 4.0) s = loopStart + cmd.a;   // sample-level nudges stay exact
                loopStart = juce::jlimit (0.0, loopEnd - 16.0, s);
                loopBeats = (loopEnd - loopStart) / fpb;
                if (loopActive && audiblePos() < loopStart) jumpTo (loopStart, false);
            }
            break;

        case C::LoopAdjustOut:
            if (loopEnd > loopStart)
            {
                double e = snapToZeroCrossing (loopEnd + cmd.a);
                if (std::abs (cmd.a) < 4.0) e = loopEnd + cmd.a;
                loopEnd = juce::jmax (loopStart + 16.0, e);
                loopBeats = (loopEnd - loopStart) / fpb;
            }
            break;

        case C::LoopIn:
        {
            const double s = nearestBeat (audiblePos());
            if (loopActive && s < loopEnd - 16.0)
            {
                loopStart = s;
                loopBeats = (loopEnd - loopStart) / fpb;
            }
            else
            {
                loopStart = s;
                loopEnd = s;
                loopActive = false;
                loopInPending = true;
            }
            break;
        }

        case C::LoopOut:
        {
            const double pos = audiblePos();
            const double e = nearestBeat (pos);
            if ((loopInPending || loopActive) && e > loopStart + 16.0)
            {
                loopEnd = e;
                loopActive = true;
                loopInPending = false;
                loopBeats = (loopEnd - loopStart) / fpb;
                if (pos >= loopEnd)
                    jumpTo (loopStart + (pos - loopEnd), false);
                else if (stretchPrimed) { readPos = pos; primeStretcher(); }
            }
            break;
        }

        case C::LoopExit:
            loopActive = false;
            loopInPending = false;
            if (slipActive && ! rollActive) { jumpTo (slipPos, false); slipActive = false; }
            if (stretchPrimed) { readPos = audiblePos(); primeStretcher(); }
            break;

        case C::Reloop:
            if (loopEnd > loopStart + 16.0)
            {
                loopActive = true;
                const double pos = audiblePos();
                if (pos < loopStart || pos >= loopEnd) jumpTo (loopStart, true);
                else if (stretchPrimed) { readPos = pos; primeStretcher(); }
            }
            break;

        case C::LoopRollDown:
            if (! rollActive)
            {
                slipPos = audiblePos();
                slipActive = true;
                rollActive = true;
            }
            autoLoopAt (cmd.a);
            break;

        case C::LoopRollUp:
            if (rollActive)
            {
                rollActive = false;
                loopActive = false;
                slipActive = false;
                jumpTo (slipPos, false);
            }
            break;

        case C::SyncOn:
            syncEnabled = true;
            if (playing && ! (ctx.deckIsMaster && ctx.masterIndex == deckIndex))
            {
                uiTempoRatio = baseTempoRatio (ctx);
                snapPhaseToMaster (ctx);
            }
            break;

        case C::SyncOff:
        {
            const double ratio = uiTempoRatio.load() - (double) bend.load();
            syncEnabled = false;
            const float range = juce::jmax (0.01f, pitchRange.load());
            pitch = juce::jlimit (-1.0f, 1.0f, (float) ((ratio - 1.0) / range));
            break;
        }

        case C::ScratchBegin:
        {
            const double pos = audiblePos();
            stretchPrimed = false;
            readPos = pos;
            scratching = true;
            wasPlayingBeforeScratch = playing;
            scratchTarget = pos;
            scratchVelocity = 0.0;
            if (slipMode.load() && playing && ! slipActive) { slipActive = true; slipPos = pos; }
            break;
        }

        case C::ScratchMove:
            scratchTarget = cmd.a;
            break;

        case C::ScratchEnd:
            if (scratching)
            {
                scratching = false;
                playing = wasPlayingBeforeScratch;
                if (slipActive && ! rollActive && slipMode.load())
                {
                    slipActive = false;
                    jumpTo (slipPos, false);
                }
            }
            break;

        case C::LoadTrack:
        case C::Eject:
        default:
            break;
    }

    return nullptr;
}

} // namespace loopdeck
