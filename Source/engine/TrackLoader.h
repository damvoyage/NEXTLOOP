#pragma once

#include "Track.h"
#include "Analysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

namespace loopdeck
{

/**
    Opens audio files, decodes them into memory-mapped cache files on a
    background thread, builds waveform overviews and runs analysis.

    Decoded audio, waveforms and analysis (BPM, grid, key, cues) are cached on
    disk keyed by file path + size + modification time, so loading a track a
    second time is instant.
*/
class TrackLoader
{
public:
    TrackLoader();
    ~TrackLoader();

    /** Starts loading. Returns immediately with a Track that fills in progressively. */
    Track::Ptr load (const juce::File& file);

    /** Re-run beat/key analysis, discarding saved grid edits. */
    void reanalyse (Track::Ptr track);

    /** Persist grid edits and cue points for a track. */
    void saveTrackData (const Track& track);

    bool isSupportedFile (const juce::File& file) const;
    juce::String getWildcard() const;
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    AnalysisSettings analysisSettings;

    juce::File getCacheDirectory() const { return cacheDir; }
    void setCacheDirectory (const juce::File& dir);

    /** Maximum disk space used by decoded audio, in bytes. */
    void setCacheLimitBytes (juce::int64 bytes) { cacheLimitBytes = bytes; }
    juce::int64 getCacheLimitBytes() const { return cacheLimitBytes; }
    juce::int64 getCacheSizeBytes() const;
    void clearCache();
    void trimCache();

    /** Blocks until every queued decode/analysis job is finished (used by tests). */
    void waitUntilIdle (int timeoutMs = 60000);

    static juce::String makeCacheKey (const juce::File& f);

private:
    class DecodeJob;
    class AnalysisJob;
    friend class DecodeJob;
    friend class AnalysisJob;

    bool tryLoadFromCache (Track& t);
    bool decodeIntoCache (Track& t, juce::ThreadPoolJob& job);
    void writeMeta (const Track& t);
    void applyAnalysisJson (Track& t, const juce::var& json);
    void queueAnalysis (Track::Ptr t);

    juce::File audioFileFor (const Track& t) const { return cacheDir.getChildFile (t.cacheKey + ".pcm"); }
    juce::File waveFileFor (const Track& t) const  { return cacheDir.getChildFile (t.cacheKey + ".wave"); }
    juce::File metaFileFor (const Track& t) const  { return cacheDir.getChildFile (t.cacheKey + ".json"); }

    juce::AudioFormatManager formatManager;
    juce::File cacheDir;
    juce::int64 cacheLimitBytes = (juce::int64) 20 * 1024 * 1024 * 1024; // 20 GB

    juce::ThreadPool decodePool { 2 };
    juce::ThreadPool analysisPool { 1 };

    juce::CriticalSection inUseLock;
    juce::StringArray keysInUse;

    JUCE_DECLARE_NON_COPYABLE (TrackLoader)
};

} // namespace loopdeck
