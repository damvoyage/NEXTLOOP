#include "Effects.h"

namespace loopdeck
{

const char* fxName (int type)
{
    switch ((FxType) type)
    {
        case FxType::Echo:      return "Echo";
        case FxType::PingPong:  return "Ping pong";
        case FxType::Reverb:    return "Reverb";
        case FxType::Flanger:   return "Flanger";
        case FxType::Phaser:    return "Phaser";
        case FxType::Filter:    return "Filter sweep";
        case FxType::Gate:      return "Gate";
        case FxType::Crush:     return "Bitcrush";
        case FxType::Granular:  return "Granular";
        case FxType::EchoOut:   return "Echo out";
        case FxType::Stutter:   return "Stutter";
        case FxType::count:     break;
    }
    return "?";
}

bool fxHasTail (int type)
{
    const auto t = (FxType) type;
    return t == FxType::Echo || t == FxType::PingPong || t == FxType::Reverb || t == FxType::EchoOut;
}

juce::String fxBeatsLabel (float beats)
{
    if (beats >= 1.0f - 1e-4f) return juce::String ((int) std::round (beats));
    if (std::abs (beats - 0.75f) < 1e-4f) return "3/4";
    return "1/" + juce::String ((int) std::round (1.0f / beats));
}

// ---------------------------------------------------------------------------
static constexpr double lineSeconds = 3.0;

void FxSlot::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    histSize = (int) (sampleRate * lineSeconds) + 4;
    histL.assign ((size_t) histSize, 0.0f);
    histR.assign ((size_t) histSize, 0.0f);
    echoL.assign ((size_t) juce::jmax (64, maxBlock), 0.0f);   // scratch buffers for the reverb
    echoR.assign ((size_t) juce::jmax (64, maxBlock), 0.0f);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (64, maxBlock), 2 };
    svf.prepare (spec);
    svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    svf.setResonance (2.2f);
    reverb.setSampleRate (sampleRate);
    reset();
    mix = 0.0f;
    wasOn = false;
    activeType = -1;
}

void FxSlot::reset()
{
    std::fill (histL.begin(), histL.end(), 0.0f);
    std::fill (histR.begin(), histR.end(), 0.0f);
    writeIdx = 0;
    delaySm = 0.0;
    fbL = fbR = lpL = lpR = 0.0f;
    lfoPhase = 0.0;
    apState.fill (0.0f);
    phFbL = phFbR = 0.0f;
    svf.reset();
    reverb.reset();
    holdL = holdR = holdCount = 0.0f;
    gateEnv = 1.0f;
    echoOutCaptured = 0.0;
    dryGain = 1.0f;
    stutterCaptured = false;
    for (auto& g : grains) g.active = false;
    grainTimer = 0;
    ringing = false;
    silentBlocks = 0;
}

float FxSlot::readHistory (int ch, double delaySamples) const noexcept
{
    const auto& h = ch == 0 ? histL : histR;
    double p = (double) writeIdx - delaySamples;
    while (p < 0.0) p += histSize;
    const int i0 = (int) p;
    const float f = (float) (p - i0);
    const int i1 = i0 + 1 >= histSize ? 0 : i0 + 1;
    return h[(size_t) i0] + (h[(size_t) i1] - h[(size_t) i0]) * f;
}

// ---------------------------------------------------------------------------
void FxSlot::process (float* L, float* R, int n, FxSlotControls& c, double bpm, double beatAtEnd)
{
    const int type = juce::jlimit (0, numFxTypes - 1, c.type.load());
    const bool on = c.on.load();

    if (type != activeType)
    {
        if (activeType >= 0 || on) reset();
        activeType = type;
        mix = 0.0f;
        wasOn = false;
    }

    if (on && ! wasOn)
    {
        ringing = false;
        silentBlocks = 0;
        echoOutCaptured = 0.0;
        stutterCaptured = false;
        stutterPos = 0.0;
    }
    if (! on && wasOn && fxHasTail (type))
    {
        ringing = true;
        silentBlocks = 0;
    }
    wasOn = on;

    const auto t = (FxType) type;
    const bool usesHistory = t == FxType::Granular || t == FxType::Stutter;

    if (! on && ! ringing && mix <= 0.0f)
    {
        c.tailActive = false;
        dryGain = 1.0f;
        if (usesHistory)
        {
            // keep recent audio so grains / stutter have material the moment they're switched on
            for (int i = 0; i < n; ++i)
            {
                histL[(size_t) writeIdx] = L[i];
                histR[(size_t) writeIdx] = R[i];
                if (++writeIdx >= histSize) writeIdx = 0;
            }
        }
        return;
    }

    const bool hasBeat = bpm > 1.0;
    const double tempo = hasBeat ? bpm : 120.0;
    const double beatSamples = 60.0 / tempo * sr;
    const float beats = juce::jlimit (1.0f / 32.0f, 32.0f, c.beats.load());
    const double lengthSamples = juce::jlimit (16.0, (double) histSize - 8.0, (double) beats * beatSamples);
    const float amtTarget = juce::jlimit (0.0f, 1.0f, c.amount.load());
    const float mixTarget = on ? 1.0f : 0.0f;
    const float mixStep = (float) (1.0 / (0.012 * sr));
    const float amtCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    const double beatPerSample = hasBeat ? tempo / 60.0 / sr : 0.0;
    const double beatAtStart = beatAtEnd - beatPerSample * n;

    // LFO phase in 0..1 for a period in beats; free-running without a beat grid
    auto lfoAt = [&] (int i, double periodBeats)
    {
        if (hasBeat && beatPerSample > 0.0)
        {
            const double b = (beatAtStart + beatPerSample * i) / periodBeats;
            return b - std::floor (b);
        }
        return lfoPhase;
    };
    const double freeStep = 1.0 / juce::jmax (1.0, (double) beats * beatSamples);

    float peak = 0.0f;

    switch (t)
    {
        // ------------------------------------------------------------------ delays
        case FxType::Echo:
        case FxType::EchoOut:
        case FxType::PingPong:
        {
            const double target = juce::jlimit (8.0, (double) histSize - 8.0, lengthSamples);
            if (delaySm <= 0.0) delaySm = target;
            const double dCoef = 1.0 - std::exp (-1.0 / (0.08 * sr));
            const bool pingPong = t == FxType::PingPong;
            const bool echoOut = t == FxType::EchoOut;

            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                delaySm += (target - delaySm) * dCoef;

                const float x = L[i], y = R[i];
                float inGain = mix;
                if (echoOut)
                {
                    // feed one length of audio in, then cut the dry signal and let the echoes ring
                    if (on && echoOutCaptured < lengthSamples) { echoOutCaptured += 1.0; inGain = 1.0f; }
                    else inGain = 0.0f;
                    const float dryTarget = (on && echoOutCaptured >= lengthSamples) ? 0.0f : 1.0f;
                    dryGain += juce::jlimit (-mixStep, mixStep, dryTarget - dryGain);
                }

                const float dl = readHistory (0, delaySm);
                const float dr = readHistory (1, delaySm);
                const float fb = echoOut ? 0.55f + 0.35f * amountSm : 0.2f + 0.65f * amountSm;
                const float wet = echoOut ? 0.9f : 0.3f + 0.7f * amountSm;

                lpL += (dl - lpL) * (pingPong ? 0.45f : 0.65f);
                lpR += (dr - lpR) * (pingPong ? 0.45f : 0.65f);

                if (pingPong)
                {
                    const float inMono = 0.5f * (x + y) * inGain;
                    histL[(size_t) writeIdx] = inMono + fb * lpR;
                    histR[(size_t) writeIdx] = fb * lpL;
                }
                else
                {
                    histL[(size_t) writeIdx] = x * inGain + fb * lpL;
                    histR[(size_t) writeIdx] = y * inGain + fb * lpR;
                }
                if (++writeIdx >= histSize) writeIdx = 0;

                const float ol = dl * wet, orr = dr * wet;
                peak = juce::jmax (peak, std::abs (ol), std::abs (orr));
                L[i] = x * dryGain + ol;
                R[i] = y * dryGain + orr;
            }
            break;
        }

        case FxType::Reverb:
        {
            juce::Reverb::Parameters p;
            const float size01 = juce::jlimit (0.0f, 1.0f, (std::log2 (beats) + 4.0f) / 8.0f);
            p.roomSize = 0.45f + 0.53f * size01;
            p.damping = 0.45f;
            p.width = 1.0f;
            p.dryLevel = 0.0f;
            p.freezeMode = 0.0f;
            int done = 0;
            while (done < n)
            {
                const int m = juce::jmin (n - done, (int) echoL.size());
                for (int i = 0; i < m; ++i)
                {
                    mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                    echoL[(size_t) i] = L[done + i] * mix;
                    echoR[(size_t) i] = R[done + i] * mix;
                }
                amountSm += (amtTarget - amountSm) * juce::jmin (1.0f, amtCoef * (float) m);
                p.wetLevel = 0.1f + 0.45f * amountSm;
                reverb.setParameters (p);
                reverb.processStereo (echoL.data(), echoR.data(), m);
                for (int i = 0; i < m; ++i)
                {
                    peak = juce::jmax (peak, std::abs (echoL[(size_t) i]));
                    L[done + i] += echoL[(size_t) i];
                    R[done + i] += echoR[(size_t) i];
                }
                done += m;
            }
            break;
        }

        // ------------------------------------------------------------------ modulation
        case FxType::Flanger:
        {
            const double period = juce::jmax (0.25, (double) beats * 4.0);   // flanger sweeps are long
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                const double ph = lfoAt (i, period);
                lfoPhase += freeStep * 0.25; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                const double tri = ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
                const double d = sr * (0.0006 + 0.0045 * tri);
                const float dl = readHistory (0, d), dr = readHistory (1, d + sr * 0.0004);
                const float fb = 0.35f + 0.4f * amountSm;
                histL[(size_t) writeIdx] = L[i] + fb * dl;
                histR[(size_t) writeIdx] = R[i] + fb * dr;
                if (++writeIdx >= histSize) writeIdx = 0;
                const float w = mix * (0.5f + 0.5f * amountSm) * 0.5f;
                L[i] = L[i] * (1.0f - w) + dl * w;
                R[i] = R[i] * (1.0f - w) + dr * w;
            }
            break;
        }

        case FxType::Phaser:
        {
            const double period = juce::jmax (0.25, (double) beats * 4.0);
            float a = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                if ((i & 7) == 0)
                {
                    const double ph = lfoAt (i, period);
                    const double tri = ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
                    const double f = 180.0 * std::pow (18.0, tri);
                    const double tn = std::tan (juce::MathConstants<double>::pi * juce::jmin (f, sr * 0.45) / sr);
                    a = (float) ((tn - 1.0) / (tn + 1.0));
                }
                lfoPhase += freeStep * 0.25; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                const float fb = 0.2f + 0.5f * amountSm;
                float xl = L[i] + fb * phFbL, xr = R[i] + fb * phFbR;
                for (int s = 0; s < 4; ++s)
                {
                    float& sl = apState[(size_t) s];
                    float& srr = apState[(size_t) (s + 4)];
                    const float yl = a * xl + sl; sl = xl - a * yl; xl = yl;
                    const float yr = a * xr + srr; srr = xr - a * yr; xr = yr;
                }
                phFbL = xl; phFbR = xr;
                const float w = mix * 0.5f;
                L[i] = L[i] * (1.0f - w) + xl * w;
                R[i] = R[i] * (1.0f - w) + xr * w;
            }
            break;
        }

        case FxType::Filter:
        {
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                if ((i & 15) == 0)
                {
                    const double ph = lfoAt (i, (double) beats);
                    const double tri = ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
                    const double lo = 250.0, hi = 250.0 * std::pow (2.0, 2.0 + 4.5 * amountSm);
                    svf.setCutoffFrequency ((float) juce::jmin (sr * 0.45, lo * std::pow (hi / lo, tri)));
                }
                lfoPhase += freeStep; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                const float fl = svf.processSample (0, L[i]);
                const float fr = svf.processSample (1, R[i]);
                L[i] = L[i] * (1.0f - mix) + fl * mix;
                R[i] = R[i] * (1.0f - mix) + fr * mix;
            }
            break;
        }

        case FxType::Gate:
        {
            const float envCoef = (float) (1.0 - std::exp (-1.0 / (0.002 * sr)));
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                const double ph = lfoAt (i, (double) beats);
                lfoPhase += freeStep; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                const float target = ph < 0.5 ? 1.0f : 1.0f - (0.4f + 0.6f * amountSm);
                gateEnv += (target - gateEnv) * envCoef;
                const float g = 1.0f + mix * (gateEnv - 1.0f);
                L[i] *= g;
                R[i] *= g;
            }
            break;
        }

        case FxType::Crush:
        {
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                const float bits = 16.0f - 12.5f * amountSm;
                const float q = std::pow (2.0f, bits - 1.0f);
                const float hold = 1.0f + 24.0f * amountSm * amountSm;
                holdCount += 1.0f;
                if (holdCount >= hold)
                {
                    holdCount -= hold;
                    holdL = std::round (L[i] * q) / q;
                    holdR = std::round (R[i] * q) / q;
                }
                L[i] = L[i] * (1.0f - mix) + holdL * mix;
                R[i] = R[i] * (1.0f - mix) + holdR * mix;
            }
            break;
        }

        // ------------------------------------------------------------------ buffer based
        case FxType::Granular:
        {
            const int grainLen = juce::jmax (64, (int) (sr * (0.04 + 0.12 * (1.0 - amountSm))));
            const double spray = juce::jlimit (sr * 0.05, (double) histSize - sr * 0.6, lengthSamples);
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                amountSm += (amtTarget - amountSm) * amtCoef;
                histL[(size_t) writeIdx] = L[i];
                histR[(size_t) writeIdx] = R[i];
                if (++writeIdx >= histSize) writeIdx = 0;

                if (--grainTimer <= 0)
                {
                    grainTimer = juce::jmax (16, grainLen / 3);
                    for (auto& g : grains)
                        if (! g.active)
                        {
                            static const double rates[] = { 1.0, 1.0, 1.0, 0.5, 2.0, -1.0 };
                            const int pick = random.nextInt (amountSm > 0.5f ? 6 : 3);
                            g.rate = rates[pick] * (1.0 + (random.nextDouble() - 0.5) * 0.01);
                            g.length = grainLen;
                            g.age = 0;
                            // start far enough back that a forward grain never overtakes the write head
                            g.pos = random.nextDouble() * spray + (double) grainLen * juce::jmax (1.0, g.rate) + 4.0;
                            g.pan = 0.2f + 0.6f * random.nextFloat();
                            g.active = true;
                            break;
                        }
                }

                float gl = 0.0f, gr = 0.0f;
                for (auto& g : grains)
                {
                    if (! g.active) continue;
                    const float w = std::sin (juce::MathConstants<float>::pi * (float) g.age / (float) g.length);
                    const float win = w * w;
                    const float sl = readHistory (0, g.pos), sr2 = readHistory (1, g.pos);
                    gl += sl * win * (1.0f - g.pan) * 2.0f * 0.8f;
                    gr += sr2 * win * g.pan * 2.0f * 0.8f;
                    g.pos += 1.0 - g.rate;   // delay grows when slower than real time
                    if (++g.age >= g.length) g.active = false;
                }
                const float w = mix * (0.55f + 0.45f * amountSm);
                L[i] = L[i] * (1.0f - w) + gl * w;
                R[i] = R[i] * (1.0f - w) + gr * w;
            }
            break;
        }

        case FxType::Stutter:
        {
            const double len = juce::jlimit (64.0, (double) histSize * 0.5, lengthSamples);
            const double fade = juce::jmin (len * 0.1, sr * 0.003);
            for (int i = 0; i < n; ++i)
            {
                mix += juce::jlimit (-mixStep, mixStep, mixTarget - mix);
                if (stutterPos <= len)   // record the slice once, then freeze the buffer
                {
                    histL[(size_t) writeIdx] = L[i];
                    histR[(size_t) writeIdx] = R[i];
                    if (++writeIdx >= histSize) writeIdx = 0;
                }

                if (! stutterCaptured)
                {
                    stutterCaptured = true;
                    stutterStart = (double) writeIdx - 1.0;
                    stutterPos = 0.0;
                }
                const double p = std::fmod (stutterPos, len);
                stutterPos += 1.0;
                // delay from the write head back to the slice position
                double d = (double) writeIdx - (stutterStart + p);
                while (d < 0.0) d += histSize;
                while (d >= histSize) d -= histSize;
                float gEdge = 1.0f;
                if (stutterPos > len)   // repeats: soften the splice
                    gEdge = (float) juce::jmin (1.0, juce::jmin (p, len - p) / fade);
                const float sl = readHistory (0, d) * gEdge, sr2 = readHistory (1, d) * gEdge;
                L[i] = L[i] * (1.0f - mix) + sl * mix;
                R[i] = R[i] * (1.0f - mix) + sr2 * mix;
            }
            break;
        }

        case FxType::count:
            break;
    }

    // tails end when they fall silent
    if (ringing)
    {
        if (peak < 2.0e-4f) ++silentBlocks; else silentBlocks = 0;
        if (silentBlocks > 8) { ringing = false; mix = 0.0f; dryGain = 1.0f; }
    }
    c.tailActive = ringing;
}

} // namespace loopdeck
