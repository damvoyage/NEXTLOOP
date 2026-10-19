#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <numeric>
#include <cstdio>
#include <cstdlib>

namespace loopdeck
{
namespace Analysis
{

static constexpr double maxAnalysisSeconds = 12.0 * 60.0;

// ---------------------------------------------------------------------------
// Onset envelope: log-magnitude spectral flux, half-wave rectified, with the
// slowly varying part removed.
// ---------------------------------------------------------------------------
std::vector<float> onsetEnvelope (const float* x, juce::int64 numFrames, double sampleRate,
                                  int hop, int fftOrder, std::vector<float>* lowOut)
{
    const int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

    const auto numHops = numFrames > fftSize ? (size_t) ((numFrames - fftSize) / hop + 1) : (size_t) 0;
    std::vector<float> flux (numHops, 0.0f), lowFlux (numHops, 0.0f);
    const int lowBins = juce::jmax (2, (int) (160.0 * fftSize / sampleRate));
    std::vector<float> buf ((size_t) fftSize * 2), prev ((size_t) fftSize / 2 + 1, 0.0f);

    for (size_t h = 0; h < numHops; ++h)
    {
        const auto start = (juce::int64) h * hop;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto idx = (start + i) * 2;
            buf[(size_t) i] = 0.5f * (x[idx] + x[idx + 1]) * window[(size_t) i];
        }
        std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        float sum = 0.0f, lowSum = 0.0f;
        for (int b = 1; b <= fftSize / 2; ++b)
        {
            const float mag = std::log1p (100.0f * buf[(size_t) b]);
            const float d = mag - prev[(size_t) b];
            if (d > 0.0f)
            {
                sum += d;
                if (b <= lowBins) lowSum += d;
            }
            prev[(size_t) b] = mag;
        }
        flux[h] = sum;
        lowFlux[h] = lowSum;
    }

    // remove local mean (≈ 0.4 s) and half-wave rectify
    const int half = juce::jmax (1, (int) (0.2 * sampleRate / hop));
    auto detrend = [half] (const std::vector<float>& in)
    {
        std::vector<double> prefix (in.size() + 1, 0.0);
        for (size_t i = 0; i < in.size(); ++i) prefix[i + 1] = prefix[i] + in[i];
        std::vector<float> outEnv (in.size(), 0.0f);
        for (size_t i = 0; i < in.size(); ++i)
        {
            const auto a = (size_t) juce::jmax (0, (int) i - half);
            const auto b = juce::jmin (in.size(), i + (size_t) half + 1);
            const double mean = (prefix[b] - prefix[a]) / (double) (b - a);
            outEnv[i] = juce::jmax (0.0f, in[i] - (float) mean);
        }
        return outEnv;
    };

    if (lowOut != nullptr)
        *lowOut = detrend (lowFlux);
    return detrend (flux);
}

double foldBpm (double bpm, double minBpm, double maxBpm)
{
    if (bpm <= 0.0) return bpm;
    while (bpm < minBpm) bpm *= 2.0;
    while (bpm >= maxBpm) bpm *= 0.5;
    return bpm;
}

// ---------------------------------------------------------------------------
// Periodicity scoring by folding the envelope onto one beat period.
// Returns peakiness score and the phase (in envelope frames) of the peak.
// ---------------------------------------------------------------------------
struct FoldResult { double score = 0.0; double phase = 0.0; };

static FoldResult foldScore (const std::vector<float>& env, double period)
{
    constexpr int bins = 96;
    std::array<double, bins> hist {};
    const double scale = (double) bins / period;
    for (size_t i = 0; i < env.size(); ++i)
    {
        const double ph = std::fmod ((double) i, period) * scale;
        const int b0 = (int) ph;
        const double f = ph - b0;
        hist[(size_t) (b0 % bins)] += env[i] * (1.0 - f);
        hist[(size_t) ((b0 + 1) % bins)] += env[i] * f;
    }

    // light circular smoothing
    std::array<double, bins> sm {};
    double mean = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        sm[(size_t) b] = 0.25 * hist[(size_t) ((b + bins - 1) % bins)] + 0.5 * hist[(size_t) b] + 0.25 * hist[(size_t) ((b + 1) % bins)];
        mean += sm[(size_t) b];
    }
    mean /= bins;

    int best = 0;
    for (int b = 1; b < bins; ++b)
        if (sm[(size_t) b] > sm[(size_t) best]) best = b;

    // parabolic interpolation of the peak
    const double ym = sm[(size_t) ((best + bins - 1) % bins)], y0 = sm[(size_t) best], yp = sm[(size_t) ((best + 1) % bins)];
    const double denom = ym - 2.0 * y0 + yp;
    const double delta = std::abs (denom) > 1e-12 ? 0.5 * (ym - yp) / denom : 0.0;

    FoldResult r;
    r.score = mean > 0.0 ? y0 / mean : 0.0;
    r.phase = (best + delta) / scale;
    if (r.phase < 0.0) r.phase += period;
    return r;
}

// ---------------------------------------------------------------------------
// Key detection (chroma + Krumhansl-Kessler profiles)
// ---------------------------------------------------------------------------
juce::String keyNameFor (int pc, bool minor)
{
    static const char* names[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* minorNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
    return minor ? juce::String (minorNames[pc % 12]) + "m" : juce::String (names[pc % 12]);
}

juce::String camelotFor (int pc, bool minor)
{
    static const int majorNum[] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    static const int minorNum[] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    return juce::String (minor ? minorNum[pc % 12] : majorNum[pc % 12]) + (minor ? "A" : "B");
}

static void detectKey (const float* x, juce::int64 frames, double sr, AnalysisResult& r,
                       const std::function<bool()>& shouldStop)
{
    constexpr int order = 14;
    constexpr int n = 1 << order;
    if (frames < n) return;

    juce::dsp::FFT fft (order);
    std::vector<float> window (n), buf ((size_t) n * 2);
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);

    // bin -> pitch class map
    std::vector<int> pcOfBin ((size_t) n / 2, -1);
    for (int b = 1; b < n / 2; ++b)
    {
        const double f = b * sr / n;
        if (f < 60.0 || f > 2200.0) continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        pcOfBin[(size_t) b] = ((int) std::lround (midi) % 12 + 12) % 12;
    }

    std::array<double, 12> chroma {};
    const juce::int64 hop = (juce::int64) (sr * 0.25);
    for (juce::int64 s = 0; s + n < frames; s += hop)
    {
        if (shouldStop && shouldStop()) return;
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (s + i) * 2;
            buf[(size_t) i] = 0.5f * (x[idx] + x[idx + 1]) * window[(size_t) i];
        }
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        std::array<double, 12> frame {};
        for (int b = 1; b < n / 2; ++b)
            if (pcOfBin[(size_t) b] >= 0)
                frame[(size_t) pcOfBin[(size_t) b]] += std::sqrt ((double) buf[(size_t) b]);

        const double mx = *std::max_element (frame.begin(), frame.end());
        if (mx > 1e-6)
            for (int i = 0; i < 12; ++i) chroma[(size_t) i] += frame[(size_t) i] / mx;
    }

    static const double majorP[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto corr = [&] (const double* prof, int root)
    {
        double mx = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; mp += prof[i]; }
        mx /= 12; mp /= 12;
        double num = 0, dx = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = chroma[(size_t) ((i + root) % 12)] - mx, b = prof[i] - mp;
            num += a * b; dx += a * a; dp += b * b;
        }
        return (dx > 0 && dp > 0) ? num / std::sqrt (dx * dp) : 0.0;
    };

    double best = -2.0; int bestPc = 0; bool bestMinor = false;
    for (int root = 0; root < 12; ++root)
    {
        const double cM = corr (majorP, root), cm = corr (minorP, root);
        if (cM > best) { best = cM; bestPc = root; bestMinor = false; }
        if (cm > best) { best = cm; bestPc = root; bestMinor = true; }
    }

    r.key = keyNameFor (bestPc, bestMinor);
    r.camelot = camelotFor (bestPc, bestMinor);
}

// ---------------------------------------------------------------------------
// Approximate integrated loudness (gated RMS in 400 ms blocks)
// ---------------------------------------------------------------------------
static float measureLoudness (const float* x, juce::int64 frames, double sr)
{
    const auto block = (juce::int64) (sr * 0.4);
    std::vector<double> powers;
    for (juce::int64 s = 0; s + block <= frames; s += block / 2)
    {
        double acc = 0.0;
        for (juce::int64 i = s; i < s + block; ++i)
        {
            const double l = x[i * 2], r = x[i * 2 + 1];
            acc += l * l + r * r;
        }
        powers.push_back (acc / (double) block);
    }
    if (powers.empty()) return -70.0f;

    auto toDb = [] (double p) { return 10.0 * std::log10 (juce::jmax (1e-12, p)) - 0.691; };
    double sum = 0; int count = 0;
    for (auto p : powers) if (toDb (p) > -70.0) { sum += p; ++count; }
    if (count == 0) return -70.0f;
    const double relGate = toDb (sum / count) - 10.0;
    sum = 0; count = 0;
    for (auto p : powers) if (toDb (p) > relGate) { sum += p; ++count; }
    return (float) (count > 0 ? toDb (sum / count) : -70.0);
}

// ---------------------------------------------------------------------------
// Transient-accurate grid refinement (works in raw samples).
// Finds the actual onset near each predicted beat and fits a line through them,
// which corrects both the phase and any tiny tempo error.
// ---------------------------------------------------------------------------
struct GridFit
{
    bool ok = false;
    double first = 0.0, beatFrames = 0.0;
    std::vector<std::pair<double, double>> onsets;   // (beat index, onset frame)
};

static double medianOf (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

static GridFit refineGrid (const float* x, juce::int64 frames, double sr, double firstBeatFrame, double beatFrames)
{
    GridFit fit;
    const int win = juce::jmax (8, (int) (sr * 0.0005));      // 0.5 ms energy windows
    const int searchWins = (int) (0.025 * sr / win);          // ±25 ms

    const juce::int64 nBeats = (juce::int64) ((double) (frames - firstBeatFrame) / beatFrames);
    const juce::int64 step = juce::jmax ((juce::int64) 1, nBeats / 600);
    std::vector<double> e ((size_t) (2 * searchWins + 4));

    for (juce::int64 k = 1; k < nBeats - 1; k += step)
    {
        const double centre = firstBeatFrame + (double) k * beatFrames;
        const auto from = (juce::int64) centre - (juce::int64) (searchWins + 2) * win;
        if (from < 0 || from + (juce::int64) e.size() * win >= frames) continue;

        // High-pass (~200 Hz) first so slow bass waveforms don't masquerade as onsets
        const double hpA = std::exp (-juce::MathConstants<double>::twoPi * 200.0 / sr);
        const auto warm = juce::jmin (from, (juce::int64) (sr * 0.01));
        double hpY = 0.0, hpX = 0.0;
        for (juce::int64 i = from - warm; i < from; ++i)
        {
            const double m = 0.5 * (x[i * 2] + x[i * 2 + 1]);
            hpY = hpA * (hpY + m - hpX); hpX = m;
        }
        for (size_t w = 0; w < e.size(); ++w)
        {
            double acc = 0.0;
            for (int i = 0; i < win; ++i)
            {
                const auto idx = (from + (juce::int64) w * win + i) * 2;
                const double m = 0.5 * (x[idx] + x[idx + 1]);
                hpY = hpA * (hpY + m - hpX); hpX = m;
                acc += hpY * hpY;
            }
            e[w] = acc;
        }

        double bestRise = 0.0; int bestW = -1;
        for (size_t w = 2; w < e.size(); ++w)
        {
            const double before = 0.5 * (e[w - 1] + e[w - 2]) + 1e-9;
            const double rise = e[w] / before;
            if (rise > bestRise) { bestRise = rise; bestW = (int) w; }
        }

        if (bestW >= 0 && bestRise > 4.0)
            fit.onsets.push_back ({ (double) k, (double) (from + (juce::int64) bestW * win) });
    }

    if (fit.onsets.size() < 16)
        return fit;

    auto leastSquares = [] (const std::vector<std::pair<double, double>>& pts, double& a, double& b)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const double n = (double) pts.size();
        for (auto& p : pts) { sx += p.first; sy += p.second; sxx += p.first * p.first; sxy += p.first * p.second; }
        const double d = n * sxx - sx * sx;
        if (std::abs (d) < 1e-12) return false;
        b = (n * sxy - sx * sy) / d;
        a = (sy - b * sx) / n;
        return true;
    };

    double a = 0, b = 0;
    if (! leastSquares (fit.onsets, a, b)) return fit;

    // drop outliers (fills, missed kicks) and refit
    std::vector<double> res;
    for (auto& p : fit.onsets) res.push_back (std::abs (p.second - (a + b * p.first)));
    const double mad = medianOf (res);
    const double limit = 3.0 * mad + sr * 0.001;
    std::vector<std::pair<double, double>> inliers;
    for (auto& p : fit.onsets)
        if (std::abs (p.second - (a + b * p.first)) <= limit) inliers.push_back (p);
    if (inliers.size() < 16 || ! leastSquares (inliers, a, b)) return fit;

    // only accept small corrections
    if (std::abs (b - beatFrames) > beatFrames * 0.01) return fit;

    fit.onsets = std::move (inliers);
    fit.first = a;
    fit.beatFrames = b;
    fit.ok = true;
    return fit;
}

// ---------------------------------------------------------------------------

AnalysisResult analyse (const Track& track, const AnalysisSettings& settings, std::function<bool()> shouldStop)
{
    AnalysisResult r;
    const float* x = track.getData();
    const double sr = track.sampleRate;
    const auto totalFrames = track.framesReady.load();
    if (x == nullptr || totalFrames < (juce::int64) (sr * 4.0))
        return r;

    const auto frames = juce::jmin (totalFrames, (juce::int64) (maxAnalysisSeconds * sr));

    r.loudnessDb = measureLoudness (x, frames, sr);

    // --- onset envelope ------------------------------------------------------
    const int hop = juce::jmax (64, (int) std::lround (256.0 * sr / 44100.0));
    const int fftOrder = sr > 60000.0 ? 11 : 10;
    const double fps = sr / hop;
    std::vector<float> lowEnv;
    const auto env = onsetEnvelope (x, frames, sr, hop, fftOrder, &lowEnv);
    if (shouldStop && shouldStop()) return r;
    if (env.size() < (size_t) (fps * 4.0)) return r;

    // --- coarse tempo via autocorrelation + comb ----------------------------
    const int maxLag = (int) (fps * 60.0 / 40.0) + 4;
    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        const size_t n = env.size() - (size_t) lag;
        for (size_t i = 0; i < n; ++i) acc += (double) env[i] * env[i + (size_t) lag];
        acf[(size_t) lag] = acc / (double) n;
    }
    auto acfAt = [&] (double lag)
    {
        if (lag < 1.0 || lag >= maxLag) return 0.0;
        const int i = (int) lag; const double f = lag - i;
        return acf[(size_t) i] * (1.0 - f) + acf[(size_t) i + 1] * f;
    };

    double bestCoarse = 0.0, bestCoarseScore = -1.0;
    for (double bpm = settings.minBpm; bpm < settings.maxBpm; bpm += 0.25)
    {
        const double lag = fps * 60.0 / bpm;
        double s = 0.0;
        for (int k = 1; k <= 4; ++k) s += acfAt (lag * k) / std::sqrt ((double) k);
        s += 0.5 * acfAt (lag * 0.5);
        if (s > bestCoarseScore) { bestCoarseScore = s; bestCoarse = bpm; }
    }
    if (bestCoarse <= 0.0) return r;
    if (shouldStop && shouldStop()) return r;

    // --- fine tempo by folding ------------------------------------------------
    double bestBpm = bestCoarse, bestScore = -1.0, bestPhase = 0.0;
    for (double bpm = bestCoarse - 1.0; bpm <= bestCoarse + 1.0; bpm += 0.01)
    {
        const auto fr = foldScore (env, fps * 60.0 / bpm);
        if (fr.score > bestScore) { bestScore = fr.score; bestBpm = bpm; bestPhase = fr.phase; }
    }

    if (settings.snapToWholeBpm)
    {
        const double rounded = std::round (bestBpm);
        if (std::abs (rounded - bestBpm) < 0.08)
        {
            const auto fr = foldScore (env, fps * 60.0 / rounded);
            if (fr.score >= bestScore * 0.97) { bestBpm = rounded; bestPhase = fr.phase; bestScore = fr.score; }
        }
        else
        {
            // also try snapping to x.5
            const double half = std::round (bestBpm * 2.0) / 2.0;
            if (std::abs (half - bestBpm) < 0.04)
            {
                const auto fr = foldScore (env, fps * 60.0 / half);
                if (fr.score >= bestScore * 0.98) { bestBpm = half; bestPhase = fr.phase; bestScore = fr.score; }
            }
        }
    }

    bestBpm = foldBpm (bestBpm, settings.minBpm, settings.maxBpm);
    const double beatFrames = sr * 60.0 / bestBpm;
    const double periodEnv = fps * 60.0 / bestBpm;

    // Beat phase: favour low-frequency onsets (kicks / bass) over broadband hats,
    // which otherwise pull the grid onto the off-beat.
    {
        double meanFull = 0.0, meanLow = 0.0;
        for (size_t i = 0; i < env.size(); ++i) { meanFull += env[i]; meanLow += lowEnv[i]; }
        meanFull /= (double) env.size();
        meanLow /= (double) env.size();

        std::vector<float> combined (env.size());
        // only trust the low band if it actually pulses at this tempo
        const double lowPeakiness = meanLow > 1e-9 ? foldScore (lowEnv, periodEnv).score : 0.0;
        const double wLow = lowPeakiness > 1.3 ? 2.0 : 0.0;
        for (size_t i = 0; i < env.size(); ++i)
            combined[i] = (float) (env[i] / juce::jmax (1e-9, meanFull)
                                   + wLow * lowEnv[i] / juce::jmax (1e-9, meanLow));
        bestPhase = foldScore (combined, periodEnv).phase;
        if (std::getenv ("LOOPDECK_DEBUG_ANALYSIS"))
            std::printf ("[analysis] fold phases: full %.2f low %.2f combined %.2f (wLow %.1f meanFull %.4f meanLow %.4f)\n",
                         foldScore (env, periodEnv).phase, foldScore (lowEnv, periodEnv).phase, bestPhase, wLow, meanFull, meanLow);
    }
    bestPhase = std::fmod (bestPhase, periodEnv);

    // envelope frame -> audio frame: flux frame i covers [i*hop, i*hop + fft)
    const int fftSize = 1 << fftOrder;
    double firstBeat = bestPhase * hop + fftSize * 0.5;
    firstBeat = std::fmod (firstBeat, beatFrames);

    if (std::getenv ("LOOPDECK_DEBUG_ANALYSIS"))
        std::printf ("[analysis] bpm %.3f phaseEnv %.2f periodEnv %.2f firstBeat(pre-refine) %.1f ms\n", bestBpm, bestPhase, periodEnv, firstBeat / sr * 1000.0);
    double beatLen = beatFrames;
    const auto fit = refineGrid (x, frames, sr, firstBeat, beatFrames);
    if (fit.ok)
    {
        firstBeat = fit.first;
        beatLen = fit.beatFrames;
        bestBpm = sr * 60.0 / beatLen;
    }

    // Prefer whole (or half) BPM values when the drift over the analysed audio stays tiny
    if (settings.snapToWholeBpm)
    {
        const double nBeats = (double) frames / beatLen;
        for (double candidate : { std::round (bestBpm), std::round (bestBpm * 2.0) / 2.0 })
        {
            const double bf = sr * 60.0 / candidate;
            if (std::abs (bf - beatLen) * nBeats < sr * 0.012)
            {
                bestBpm = candidate;
                beatLen = bf;
                if (fit.ok)
                {
                    std::vector<double> intercepts;
                    for (auto& p : fit.onsets) intercepts.push_back (p.second - p.first * bf);
                    firstBeat = medianOf (intercepts);
                }
                break;
            }
        }
    }

    if (std::getenv ("LOOPDECK_DEBUG_ANALYSIS"))
        std::printf ("[analysis] refined bpm %.4f (fit %s, %d onsets) fitFirst %.2f ms fitBeat %.3f final first %.2f ms beat %.3f\n", bestBpm, fit.ok ? "ok" : "failed", (int) fit.onsets.size(),
                     fit.first / sr * 1000.0, fit.beatFrames, firstBeat / sr * 1000.0, beatLen);

    const double beatFramesFinal = beatLen;
    firstBeat = std::fmod (firstBeat, beatFramesFinal);
    if (firstBeat < 0) firstBeat += beatFramesFinal;

    // --- anchor: first strong beat becomes the downbeat ---------------------
    std::vector<float> beatStrength;
    for (double f = firstBeat; f < (double) frames; f += beatFramesFinal)
    {
        const auto c = (long) std::lround ((f - fftSize * 0.5) / hop);
        float mx = 0.0f;
        for (long i = c - 2; i <= c + 2; ++i)
            if (i >= 0 && i < (long) env.size()) mx = juce::jmax (mx, env[(size_t) i] + lowEnv[(size_t) i]);
        beatStrength.push_back (mx);
    }
    double anchor = firstBeat;
    if (! beatStrength.empty())
    {
        auto sorted = beatStrength;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        const float median = sorted[sorted.size() / 2];
        for (size_t k = 0; k < beatStrength.size(); ++k)
            if (beatStrength[k] > 0.4f * median && median > 0.0f) { anchor = firstBeat + (double) k * beatFramesFinal; break; }
    }

    r.bpm = bestBpm;
    r.anchorFrame = anchor;
    r.confidence = bestScore;
    r.ok = true;

    if (shouldStop && shouldStop()) return r;
    detectKey (x, frames, sr, r, shouldStop);
    return r;
}

} // namespace Analysis
} // namespace loopdeck
