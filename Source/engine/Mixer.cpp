#include "Mixer.h"

namespace loopdeck
{

static float eqKnobToGain (float v) noexcept
{
    // 0 = full kill, 0.5 = unity, 1 = +6 dB
    if (v <= 0.002f) return 0.0f;
    if (v < 0.5f)
    {
        const float db = -40.0f * (1.0f - v / 0.5f);   // down to -40 dB, then kill at the very end
        return juce::Decibels::decibelsToGain (db) * juce::jmin (1.0f, v / 0.05f);
    }
    return juce::Decibels::decibelsToGain (6.0f * (v - 0.5f) / 0.5f);
}

// ===========================================================================
void ChannelStrip::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };

    split1.prepare (spec);
    split1.setCutoffFrequency (200.0f);
    split2.prepare (spec);
    split2.setCutoffFrequency (2500.0f);
    lowAp.prepare (spec);
    lowAp.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lowAp.setCutoffFrequency (2500.0f);

    svf.prepare (spec);
    svf.setResonance (0.7f);
    svf.setCutoffFrequency (1000.0f);
    lastFilterType = 0;
}

void ChannelStrip::process (float* L, float* R, int n, const ChannelControls& c)
{
    const float targetTrim = juce::Decibels::decibelsToGain (c.trimDb.load()
                                 + (c.autoGain.load() ? c.autoGainDb.load() : 0.0f));
    const float tLow = eqKnobToGain (c.eqLow.load());
    const float tMid = eqKnobToGain (c.eqMid.load());
    const float tHigh = eqKnobToGain (c.eqHigh.load());

    const bool eqFlat = std::abs (tLow - 1.0f) < 1e-4f && std::abs (tMid - 1.0f) < 1e-4f && std::abs (tHigh - 1.0f) < 1e-4f
                        && std::abs (gLow - 1.0f) < 1e-4f && std::abs (gMid - 1.0f) < 1e-4f && std::abs (gHigh - 1.0f) < 1e-4f;

    const float inv = 1.0f / (float) n;
    const float dTrim = (targetTrim - gTrim) * inv;
    const float dLow = (tLow - gLow) * inv, dMid = (tMid - gMid) * inv, dHigh = (tHigh - gHigh) * inv;

    for (int i = 0; i < n; ++i)
    {
        gTrim += dTrim;
        float x[2] = { L[i] * gTrim, R[i] * gTrim };

        if (! eqFlat)
        {
            gLow += dLow; gMid += dMid; gHigh += dHigh;
            for (int ch = 0; ch < 2; ++ch)
            {
                float low, rest, mid, high;
                split1.processSample (ch, x[ch], low, rest);
                split2.processSample (ch, rest, mid, high);
                low = lowAp.processSample (ch, low);
                x[ch] = low * gLow + mid * gMid + high * gHigh;
            }
        }
        else
        {
            gLow = tLow; gMid = tMid; gHigh = tHigh;
            // keep filter states running so re-engaging the EQ is click free
            for (int ch = 0; ch < 2; ++ch)
            {
                float low, rest, mid, high;
                split1.processSample (ch, x[ch], low, rest);
                split2.processSample (ch, rest, mid, high);
                low = lowAp.processSample (ch, low);
                x[ch] = low + mid + high;
            }
        }

        L[i] = x[0];
        R[i] = x[1];
    }

    // --- one-knob filter ---------------------------------------------------
    const float target = c.filter.load();
    const float res = juce::jlimit (0.5f, 6.0f, 0.7f + c.filterResonance.load() * 3.0f);
    constexpr int chunk = 32;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        smoothedFilter += (target - smoothedFilter) * 0.15f;
        const float f = smoothedFilter;
        const int type = f < -0.02f ? -1 : (f > 0.02f ? 1 : 0);

        if (type == 0)
        {
            if (lastFilterType != 0) svf.reset();
            lastFilterType = 0;
            continue;
        }

        if (type != lastFilterType)
        {
            svf.reset();
            svf.setType (type < 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                                  : juce::dsp::StateVariableTPTFilterType::highpass);
            lastFilterType = type;
        }

        const float amt = (std::abs (f) - 0.02f) / 0.98f;   // 0..1
        float cutoff;
        if (type < 0) cutoff = 20000.0f * std::pow (60.0f / 20000.0f, amt);     // 20k -> 60 Hz
        else          cutoff = 20.0f * std::pow (9000.0f / 20.0f, amt);         // 20 Hz -> 9 kHz
        cutoff = juce::jlimit (20.0f, (float) (sr * 0.45), cutoff);

        svf.setCutoffFrequency (cutoff);
        svf.setResonance (res);
        for (int i = start; i < start + len; ++i)
        {
            L[i] = svf.processSample (0, L[i]);
            R[i] = svf.processSample (1, R[i]);
        }
    }
}

// ===========================================================================
float Mixer::faderCurve (float v) noexcept
{
    v = juce::jlimit (0.0f, 1.0f, v);
    return v * v;   // roughly -12 dB at the midpoint, like most DJ mixer faders
}

void Mixer::crossfaderGains (float x, int curve, float& a, float& b) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (x + 1.0f) * 0.5f);
    switch (curve)
    {
        case 1: // sharp / scratch cut
            a = juce::jlimit (0.0f, 1.0f, (1.0f - t) * 20.0f);
            b = juce::jlimit (0.0f, 1.0f, t * 20.0f);
            break;
        case 2: // off
            a = b = 1.0f;
            break;
        default: // smooth: full level at centre, constant-power style fade to the sides
            a = juce::jmin (1.0f, std::cos (t * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2);
            b = juce::jmin (1.0f, std::sin (t * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2);
            break;
    }
}

void Mixer::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& s : strips) s.prepare (sampleRate, maxBlock);
    for (int i = 0; i < maxDecks; ++i) lastFaderGain[(size_t) i] = faderCurve (channels[(size_t) i].fader.load());
    limiterEnv = 1.0f;
    lastMaster = faderCurve (master.masterGain.load());
}

void Mixer::process (float* const* deckL, float* const* deckR, int numDecks, int n,
                     float* masterL, float* masterR, float* cueL, float* cueR)
{
    juce::FloatVectorOperations::clear (masterL, n);
    juce::FloatVectorOperations::clear (masterR, n);
    juce::FloatVectorOperations::clear (cueL, n);
    juce::FloatVectorOperations::clear (cueR, n);

    float xa, xb;
    crossfaderGains (master.crossfader.load(), master.crossfaderCurve.load(), xa, xb);

    for (int d = 0; d < numDecks; ++d)
    {
        auto& c = channels[(size_t) d];
        float* L = deckL[d];
        float* R = deckR[d];

        strips[(size_t) d].process (L, R, n, c);

        if (c.cue.load())
        {
            juce::FloatVectorOperations::add (cueL, L, n);
            juce::FloatVectorOperations::add (cueR, R, n);
        }

        const int assign = c.xfaderAssign.load();
        const float xg = assign == 0 ? xa : (assign == 2 ? xb : 1.0f);
        const float target = faderCurve (c.fader.load()) * xg;
        const float start = lastFaderGain[(size_t) d];
        lastFaderGain[(size_t) d] = target;

        float pkL = 0.0f, pkR = 0.0f;
        const float step = (target - start) / (float) n;
        float g = start;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            const float l = L[i] * g, r = R[i] * g;
            masterL[i] += l;
            masterR[i] += r;
            pkL = juce::jmax (pkL, std::abs (l));
            pkR = juce::jmax (pkR, std::abs (r));
        }
        c.meterL = pkL;
        c.meterR = pkR;
    }

    // --- master gain + limiter ---------------------------------------------
    const float mTarget = faderCurve (master.masterGain.load()) * 1.25f;
    const float mStep = (mTarget - lastMaster) / (float) n;
    float mg = lastMaster;
    lastMaster = mTarget;

    const bool limit = master.limiter.load();
    const float thr = 0.97f;
    const float release = 1.0f - std::exp (-1.0f / (float) (sr * 0.12));
    float pkL = 0.0f, pkR = 0.0f, minEnv = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        mg += mStep;
        float l = masterL[i] * mg, r = masterR[i] * mg;
        if (limit)
        {
            const float peak = juce::jmax (std::abs (l), std::abs (r));
            if (peak * limiterEnv > thr) limiterEnv = thr / peak;
            else limiterEnv += (1.0f - limiterEnv) * release;
            l *= limiterEnv; r *= limiterEnv;
            minEnv = juce::jmin (minEnv, limiterEnv);
        }
        l = juce::jlimit (-1.0f, 1.0f, l);
        r = juce::jlimit (-1.0f, 1.0f, r);
        masterL[i] = l; masterR[i] = r;
        pkL = juce::jmax (pkL, std::abs (l));
        pkR = juce::jmax (pkR, std::abs (r));
    }
    master.meterL = pkL;
    master.meterR = pkR;
    master.limiterReduction = juce::Decibels::gainToDecibels (minEnv);

    // --- headphones ----------------------------------------------------------
    const float mix = master.headphoneMix.load();
    const float hg = faderCurve (master.headphoneGain.load()) * 1.25f;
    const float cueAmt = std::cos (mix * juce::MathConstants<float>::halfPi);
    const float masAmt = std::sin (mix * juce::MathConstants<float>::halfPi);
    for (int i = 0; i < n; ++i)
    {
        cueL[i] = juce::jlimit (-1.0f, 1.0f, (cueL[i] * cueAmt + masterL[i] * masAmt) * hg);
        cueR[i] = juce::jlimit (-1.0f, 1.0f, (cueR[i] * cueAmt + masterR[i] * masAmt) * hg);
    }
}

} // namespace loopdeck
