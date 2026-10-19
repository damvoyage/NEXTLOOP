#include "TimeStretch.h"
#include <juce_dsp/juce_dsp.h>
#include <signalsmith-stretch/signalsmith-stretch.h>


namespace loopdeck
{

juce::String stretchAlgorithmName (int a)
{
    switch (a)
    {
        case (int) StretchAlgorithm::Light:               return "Light";
        case (int) StretchAlgorithm::Studio:              return "Studio";
        case (int) StretchAlgorithm::VintageSampler:      return "Vintage sampler";
        case (int) StretchAlgorithm::VintagePhaseVocoder: return "Vintage phase vocoder";
        case (int) StretchAlgorithm::VintageCyclic:       return "Vintage cyclic";
        default:                                          return "Light";
    }
}

juce::String stretchAlgorithmDescription (int a)
{
    switch (a)
    {
        case (int) StretchAlgorithm::Light:               return "Splices the audio at matching points, like classic DJ key lock. Uses almost no CPU. Clean for normal tempo changes; can flam a little on big ones.";
        case (int) StretchAlgorithm::Studio:              return "Signalsmith Stretch. Spectral processing, smoother on big tempo changes and sustained sounds. Several times the CPU of Light.";
        case (int) StretchAlgorithm::VintageSampler:      return "Overlapping grains like a 90s hardware sampler. Warbly and chorused.";
        case (int) StretchAlgorithm::VintagePhaseVocoder: return "A textbook phase vocoder with no transient care. Smeared, phasey, underwater.";
        case (int) StretchAlgorithm::VintageCyclic:       return "Hard-spliced repeating cycles. Stuttery and glitchy, more so the further you push it.";
        default:                                          return {};
    }
}

bool stretchAlgorithmAvailable (int a)
{
    return a >= 0 && a < numStretchAlgorithms;
}

// ===========================================================================
// Signalsmith Stretch
// ===========================================================================
class SignalsmithStretcher : public TimeStretcher
{
public:
    explicit SignalsmithStretcher (bool highQuality) : hq (highQuality) {}

    void prepare (double sr, int maxBlock) override
    {
        if (hq) stretch.presetDefault (2, (float) sr);
        else    stretch.configure (2, (int) (sr * 0.08), (int) (sr * 0.02));

        cap = (size_t) juce::jmax (maxBlock * 8 + 64, (int) stretch.outputSeekLength (4.0f) + 64);
        inL.assign (cap, 0.0f); inR.assign (cap, 0.0f);
        outL.assign (cap, 0.0f); outR.assign (cap, 0.0f);

        // warm up so nothing allocates on the audio thread later
        float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { outL.data(), outR.data() };
        stretch.outputSeek (ins, (int) cap - 32);
        stretch.process (ins, maxBlock * 2, outs, maxBlock);
        stretch.reset();
    }

    void prime (StretchSource& src, double rate, double transpose) override
    {
        setTranspose (transpose);
        const int len = juce::jlimit (1, (int) cap, (int) std::ceil (stretch.outputSeekLength ((float) juce::jlimit (0.05, 4.0, rate))));
        src.read (inL.data(), inR.data(), len);
        float* ins[2] = { inL.data(), inR.data() };
        stretch.outputSeek (ins, len);
        acc = 0.0;
    }

    void process (StretchSource& src, float* L, float* R, int n, double rate, double transpose) override
    {
        setTranspose (transpose);
        acc += (double) n * juce::jlimit (0.0, 4.0, rate);
        int m = (int) acc;
        acc -= m;
        m = juce::jlimit (0, (int) cap, m);
        src.read (inL.data(), inR.data(), m);
        float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { L, R };
        stretch.process (ins, m, outs, n);
    }

private:
    void setTranspose (double t)
    {
        if (std::abs (t - lastTranspose) > 1e-6) { stretch.setTransposeFactor ((float) t); lastTranspose = t; }
    }

    bool hq;
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::vector<float> inL, inR, outL, outR;
    size_t cap = 0;
    double acc = 0.0, lastTranspose = 1.0;
};


// ===========================================================================
// Shared input ring for the vintage algorithms (random access around a read point)
// ===========================================================================
class InputRing
{
public:
    void prepare (int maxBlock)
    {
        L.assign (size, 0.0f); R.assign (size, 0.0f);
        tmpL.assign ((size_t) juce::jmax (1024, maxBlock), 0.0f);
        tmpR.assign (tmpL.size(), 0.0f);
    }

    void reset() { written = 0; }

    /** Makes sure frames up to (absolute) index `upTo` have been pulled from the source. */
    void ensure (StretchSource& src, juce::int64 upTo)
    {
        while (written < upTo)
        {
            const int k = (int) juce::jmin ((juce::int64) tmpL.size(), upTo - written);
            src.read (tmpL.data(), tmpR.data(), k);
            for (int i = 0; i < k; ++i)
            {
                const auto idx = (size_t) ((written + i) & mask);
                L[idx] = tmpL[(size_t) i];
                R[idx] = tmpR[(size_t) i];
            }
            written += k;
        }
    }

    inline void at (double pos, float& l, float& r) const noexcept
    {
        if (pos < 0.0) { l = r = 0.0f; return; }
        const auto i0 = (juce::int64) pos;
        const float t = (float) (pos - (double) i0);
        const auto a = (size_t) (i0 & mask), b = (size_t) ((i0 + 1) & mask);
        l = L[a] + (L[b] - L[a]) * t;
        r = R[a] + (R[b] - R[a]) * t;
    }

private:
    static constexpr juce::int64 size = 1 << 18;
    static constexpr juce::int64 mask = size - 1;
    std::vector<float> L, R, tmpL, tmpR;
    juce::int64 written = 0;
};

/** Output accumulator for overlap-add algorithms. */
struct OverlapAdd
{
    std::vector<float> L, R;
    int ready = 0;

    void prepare (int capacity) { L.assign ((size_t) capacity, 0.0f); R.assign ((size_t) capacity, 0.0f); ready = 0; }
    void reset() { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); ready = 0; }

    void take (float* outL, float* outR, int n)
    {
        std::copy (L.begin(), L.begin() + n, outL);
        std::copy (R.begin(), R.begin() + n, outR);
        std::copy (L.begin() + n, L.end(), L.begin());
        std::copy (R.begin() + n, R.end(), R.begin());
        std::fill (L.end() - n, L.end(), 0.0f);
        std::fill (R.end() - n, R.end(), 0.0f);
        ready -= n;
    }
};

// ===========================================================================
// Vintage granular: overlapping windowed grains (sampler) or hard cycles (cyclic)
// ===========================================================================
class GranularStretcher : public TimeStretcher
{
public:
    explicit GranularStretcher (bool cyclicMode) : cyclic (cyclicMode) {}

    void prepare (double sr, int maxBlock) override
    {
        if (cyclic)
        {
            grain = (int) (sr * 0.030);
            fade = juce::jmax (8, (int) (sr * 0.0015));
            hop = grain - fade;
        }
        else
        {
            grain = (int) (sr * 0.050);
            hop = grain / 2;
        }

        window.assign ((size_t) grain, 1.0f);
        for (int i = 0; i < grain; ++i)
        {
            if (cyclic)
            {
                if (i < fade) window[(size_t) i] = (float) i / (float) fade;
                else if (i >= grain - fade) window[(size_t) i] = (float) (grain - i) / (float) fade;
            }
            else
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) grain);
        }

        ring.prepare (maxBlock);
        ola.prepare (maxBlock + grain * 2 + 16);
    }

    void prime (StretchSource&, double, double) override
    {
        ring.reset();
        ola.reset();
        readPoint = 0.0;
    }

    void process (StretchSource& src, float* L, float* R, int n, double rate, double transpose) override
    {
        const double p = juce::jlimit (0.25, 4.0, transpose);
        while (ola.ready < n)
        {
            ring.ensure (src, (juce::int64) std::ceil (readPoint + grain * p) + 4);
            float* aL = ola.L.data() + ola.ready;
            float* aR = ola.R.data() + ola.ready;
            for (int i = 0; i < grain; ++i)
            {
                float l, r;
                ring.at (readPoint + i * p, l, r);
                const float w = window[(size_t) i];
                aL[i] += l * w;
                aR[i] += r * w;
            }
            ola.ready += hop;
            readPoint += hop * juce::jlimit (0.0, 4.0, rate);
        }
        ola.take (L, R, n);
    }

private:
    bool cyclic;
    int grain = 0, hop = 0, fade = 0;
    std::vector<float> window;
    InputRing ring;
    OverlapAdd ola;
    double readPoint = 0.0;
};

// ===========================================================================
// Vintage phase vocoder: no phase locking, no transient handling (on purpose)
// ===========================================================================
class PhaseVocoderStretcher : public TimeStretcher
{
public:
    void prepare (double sr, int maxBlock) override
    {
        order = sr > 60000.0 ? 12 : 11;
        N = 1 << order;
        hop = N / 4;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        for (auto* v : { &buf, &shifted }) v->assign ((size_t) N * 2, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            prevPhase[c].assign ((size_t) N / 2 + 1, 0.0f);
            sumPhase[c].assign ((size_t) N / 2 + 1, 0.0f);
        }
        mag.assign ((size_t) N / 2 + 1, 0.0f);
        phase.assign ((size_t) N / 2 + 1, 0.0f);
        ring.prepare (maxBlock);
        ola.prepare (maxBlock + N * 2 + 16);
    }

    void prime (StretchSource&, double, double) override
    {
        ring.reset();
        ola.reset();
        // the vocoder's smearing delays transients by about one hop; read that far ahead to compensate
        readPoint = (double) hop;
        first = true;
    }

    void process (StretchSource& src, float* L, float* R, int n, double rate, double transpose) override
    {
        const double analysisHop = hop * juce::jlimit (0.05, 4.0, rate);
        const double p = juce::jlimit (0.25, 4.0, transpose);

        while (ola.ready < n)
        {
            ring.ensure (src, (juce::int64) std::ceil (readPoint + N) + 4);
            for (int c = 0; c < 2; ++c)
                frame (c, analysisHop, p, c == 0 ? ola.L.data() + ola.ready : ola.R.data() + ola.ready);
            first = false;
            ola.ready += hop;
            readPoint += analysisHop;
        }
        ola.take (L, R, n);
    }

private:
    void frame (int ch, double analysisHop, double p, float* out)
    {
        for (int i = 0; i < N; ++i)
        {
            float l, r;
            ring.at (readPoint + i, l, r);
            buf[(size_t) i] = (ch == 0 ? l : r) * window[(size_t) i];
        }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft->performRealOnlyForwardTransform (buf.data(), true);

        const int bins = N / 2 + 1;
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (int k = 0; k < bins; ++k)
        {
            const float re = buf[(size_t) k * 2], im = buf[(size_t) k * 2 + 1];
            mag[(size_t) k] = std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);
            const double omega = twoPi * k / N;
            if (first)
                sumPhase[ch][(size_t) k] = ph;
            else
            {
                double delta = ph - prevPhase[ch][(size_t) k] - omega * analysisHop;
                delta -= twoPi * std::round (delta / twoPi);
                const double trueFreq = omega + delta / analysisHop;
                sumPhase[ch][(size_t) k] = (float) std::fmod (sumPhase[ch][(size_t) k] + trueFreq * hop * p, twoPi);
            }
            prevPhase[ch][(size_t) k] = ph;
        }

        // crude bin-shifting transposition: part of the charm
        std::fill (shifted.begin(), shifted.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            const int j = (int) std::lround (k * p);
            if (j < 0 || j >= bins) continue;
            const float m = mag[(size_t) k];
            const float ph = sumPhase[ch][(size_t) k];
            shifted[(size_t) j * 2] += m * std::cos (ph);
            shifted[(size_t) j * 2 + 1] += m * std::sin (ph);
        }
        fft->performRealOnlyInverseTransform (shifted.data());

        const float scale = 1.0f / 1.5f;   // Hann² at 75 % overlap sums to 1.5
        for (int i = 0; i < N; ++i)
            out[i] += shifted[(size_t) i] * window[(size_t) i] * scale;
    }

    int order = 11, N = 2048, hop = 512;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, buf, shifted, mag, phase;
    std::vector<float> prevPhase[2], sumPhase[2];
    InputRing ring;
    OverlapAdd ola;
    double readPoint = 0.0;
    bool first = true;
};

// ===========================================================================
// Light: WSOLA (waveform-similarity overlap-add)
// Each new segment is spliced in where it best matches what would naturally
// have followed the previous one, so the joins are smooth. Purely time-domain:
// a few multiply-adds per sample.
// ===========================================================================
class WsolaStretcher : public TimeStretcher
{
public:
    void prepare (double sr, int maxBlock) override
    {
        seg = (int) (sr * 0.040);       // 40 ms segments
        overlap = (int) (sr * 0.010);   // 10 ms crossfades
        search = (int) (sr * 0.012);    // ±12 ms search for the best splice point
        hop = seg - overlap;
        fadeIn.assign ((size_t) overlap, 0.0f);
        for (int i = 0; i < overlap; ++i)
            fadeIn[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i + 0.5f) / (float) overlap);
        ref.assign ((size_t) overlap, 0.0f);
        ring.prepare (maxBlock);
        ola.prepare (maxBlock + seg * 2 + 16);
    }

    void prime (StretchSource&, double, double) override
    {
        ring.reset();
        ola.reset();
        nominal = 0.0;
        prevStart = -1.0;
    }

    void process (StretchSource& src, float* L, float* R, int n, double rate, double transpose) override
    {
        const double p = juce::jlimit (0.25, 4.0, transpose);
        const double r = juce::jlimit (0.0, 4.0, rate);

        while (ola.ready < n)
        {
            double start = nominal;
            if (prevStart >= 0.0)
            {
                // what would naturally follow the previous segment
                const double natural = prevStart + hop * p;
                ring.ensure (src, (juce::int64) std::ceil (natural + overlap * p) + 4);
                for (int i = 0; i < overlap; ++i)
                {
                    float l, rr;
                    ring.at (natural + i * p, l, rr);
                    ref[(size_t) i] = l + rr;
                }
                start = bestMatch (src, nominal, p);
            }

            ring.ensure (src, (juce::int64) std::ceil (start + seg * p) + 4);
            float* aL = ola.L.data() + ola.ready;
            float* aR = ola.R.data() + ola.ready;
            for (int i = 0; i < seg; ++i)
            {
                float l, rr;
                ring.at (start + i * p, l, rr);
                float w = 1.0f;
                if (i < overlap) w = prevStart < 0.0 ? 1.0f : fadeIn[(size_t) i];
                else if (i >= hop) w = 1.0f - fadeIn[(size_t) (i - hop)];
                aL[i] += l * w;
                aR[i] += rr * w;
            }
            ola.ready += hop;
            prevStart = start;
            nominal += hop * r;
        }
        ola.take (L, R, n);
    }

private:
    /** Searches around the nominal position for the start that best continues the waveform. */
    double bestMatch (StretchSource& src, double centre, double p)
    {
        const double lo = juce::jmax (0.0, centre - search * p);
        ring.ensure (src, (juce::int64) std::ceil (centre + (search + overlap) * p) + 4);

        auto score = [&] (double s, int step)
        {
            double xy = 0.0, yy = 1e-9;
            for (int i = 0; i < overlap; i += step)
            {
                float l, r;
                ring.at (s + i * p, l, r);
                const double y = l + r;
                xy += y * ref[(size_t) i];
                yy += y * y;
            }
            return xy / std::sqrt (yy);
        };

        // Coarse pass on a decimated grid, scanning outward from the nominal point so that
        // ties (silence, steady tones) keep the nominal timing; then refine around the winner.
        // A tiny distance penalty keeps the timing honest when matches are equally good.
        const double refEnergy = [&] { double e = 0; for (auto v : ref) e += (double) v * v; return std::sqrt (e) + 1e-9; }();
        const double penalty = 0.02 * refEnergy / (search * p);
        double best = centre, bestScore = score (centre, 4);
        for (double d = 4.0 * p; d <= search * p; d += 4.0 * p)
            for (double s : { centre - d, centre + d })
            {
                if (s < lo) continue;
                const double sc = score (s, 4) - penalty * d;
                if (sc > bestScore + 1e-9) { bestScore = sc; best = s; }
            }
        const double coarse = best;
        bestScore = score (coarse, 2) - penalty * std::abs (coarse - centre);
        for (double d = p; d <= 3.0 * p; d += p)
            for (double s : { coarse - d, coarse + d })
            {
                if (s < 0.0) continue;
                const double sc = score (s, 2) - penalty * std::abs (s - centre);
                if (sc > bestScore + 1e-9) { bestScore = sc; best = s; }
            }
        return best;
    }

    int seg = 0, overlap = 0, search = 0, hop = 0;
    std::vector<float> fadeIn, ref;
    InputRing ring;
    OverlapAdd ola;
    double nominal = 0.0, prevStart = -1.0;
};

// ===========================================================================
std::unique_ptr<TimeStretcher> createTimeStretcher (int a)
{
    switch (a)
    {
        case (int) StretchAlgorithm::Studio:              return std::make_unique<SignalsmithStretcher> (true);
        case (int) StretchAlgorithm::VintageSampler:      return std::make_unique<GranularStretcher> (false);
        case (int) StretchAlgorithm::VintagePhaseVocoder: return std::make_unique<PhaseVocoderStretcher>();
        case (int) StretchAlgorithm::VintageCyclic:       return std::make_unique<GranularStretcher> (true);
        case (int) StretchAlgorithm::Light:
        default:                                          return std::make_unique<WsolaStretcher>();
    }
}

} // namespace loopdeck
