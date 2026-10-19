#include "TrackLoader.h"

namespace loopdeck
{

static juce::File defaultCacheDirectory()
{
   #if JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Application Support");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile ("NEXTLOOP").getChildFile ("Cache");
}

// ---------------------------------------------------------------------------
class TrackLoader::AnalysisJob : public juce::ThreadPoolJob
{
public:
    AnalysisJob (TrackLoader& l, Track::Ptr t) : juce::ThreadPoolJob ("analyse"), loader (l), track (std::move (t)) {}

    JobStatus runJob() override
    {
        track->analysing = true;
        const auto result = Analysis::analyse (*track, loader.analysisSettings,
                                               [this] { return shouldExit() || track->cancelDecode.load(); });
        if (result.ok)
        {
            track->grid.bpm = result.bpm;
            track->grid.anchorFrame = result.anchorFrame;
            track->setKey (result.key, result.camelot);
            track->loudnessDb = result.loudnessDb;
            track->autoGainDb = juce::jlimit (-12.0f, 12.0f, -10.0f - result.loudnessDb);
            if (track->cuePoint.load() <= 0.0)
                track->cuePoint = result.anchorFrame;
        }
        track->analysed = true;
        track->analysing = false;
        if (! shouldExit())
            loader.writeMeta (*track);
        return jobHasFinished;
    }

private:
    TrackLoader& loader;
    Track::Ptr track;
};

// ---------------------------------------------------------------------------
class TrackLoader::DecodeJob : public juce::ThreadPoolJob
{
public:
    DecodeJob (TrackLoader& l, Track::Ptr t) : juce::ThreadPoolJob ("decode"), loader (l), track (std::move (t)) {}

    JobStatus runJob() override
    {
        if (! loader.tryLoadFromCache (*track))
        {
            if (! loader.decodeIntoCache (*track, *this))
            {
                if (! track->cancelDecode && ! shouldExit())
                    track->decodeFailed = true;
                return jobHasFinished;
            }
        }

        if (! track->analysed && ! shouldExit())
            loader.queueAnalysis (track);

        return jobHasFinished;
    }

private:
    TrackLoader& loader;
    Track::Ptr track;
};

// ---------------------------------------------------------------------------
TrackLoader::TrackLoader()
{
    formatManager.registerBasicFormats();
    setCacheDirectory (defaultCacheDirectory());
}

TrackLoader::~TrackLoader()
{
    decodePool.removeAllJobs (true, 4000);
    analysisPool.removeAllJobs (true, 4000);
}

void TrackLoader::setCacheDirectory (const juce::File& dir)
{
    cacheDir = dir;
    cacheDir.createDirectory();
}

juce::String TrackLoader::makeCacheKey (const juce::File& f)
{
    const auto id = f.getFullPathName() + "|" + juce::String (f.getSize()) + "|"
                  + juce::String (f.getLastModificationTime().toMilliseconds()) + "|v"
                  + juce::String (Track::cacheFormatVersion);
    return juce::MD5 (id.toUTF8()).toHexString();
}

bool TrackLoader::isSupportedFile (const juce::File& file) const
{
    if (! file.existsAsFile()) return false;
    const auto ext = file.getFileExtension().toLowerCase();
    return ext.isNotEmpty() && formatManager.getWildcardForAllFormats().toLowerCase().contains ("*" + ext);
}

juce::String TrackLoader::getWildcard() const
{
    return const_cast<juce::AudioFormatManager&> (formatManager).getWildcardForAllFormats();
}

Track::Ptr TrackLoader::load (const juce::File& file)
{
    Track::Ptr t (new Track (file));
    t->cacheKey = makeCacheKey (file);
    {
        const juce::ScopedLock sl (inUseLock);
        keysInUse.addIfNotAlreadyThere (t->cacheKey);
    }
    decodePool.addJob (new DecodeJob (*this, t), true);
    return t;
}

void TrackLoader::reanalyse (Track::Ptr track)
{
    if (track == nullptr || ! track->decodeComplete) return;
    track->analysed = false;
    queueAnalysis (track);
}

void TrackLoader::queueAnalysis (Track::Ptr t)
{
    analysisPool.addJob (new AnalysisJob (*this, std::move (t)), true);
}

void TrackLoader::waitUntilIdle (int timeoutMs)
{
    const auto start = juce::Time::getMillisecondCounter();
    while ((decodePool.getNumJobs() > 0 || analysisPool.getNumJobs() > 0)
           && juce::Time::getMillisecondCounter() - start < (juce::uint32) timeoutMs)
        juce::Thread::sleep (10);
}

// ---------------------------------------------------------------------------
void TrackLoader::applyAnalysisJson (Track& t, const juce::var& json)
{
    if (auto* a = json["analysis"].getDynamicObject())
    {
        const double bpm = a->getProperty ("bpm");
        if (bpm > 1.0)
        {
            t.grid.bpm = bpm;
            t.grid.anchorFrame = (double) a->getProperty ("anchor");
            t.grid.beatsPerBar = juce::jmax (1, (int) a->getProperty ("beatsPerBar"));
            t.setKey (a->getProperty ("key").toString(), a->getProperty ("camelot").toString());
            t.loudnessDb = (float) (double) a->getProperty ("loudness");
            t.autoGainDb = (float) (double) a->getProperty ("autoGain");
            t.analysed = true;
        }
    }

    if (auto* c = json["cues"].getDynamicObject())
    {
        t.cuePoint = (double) c->getProperty ("cue");
        if (auto* hot = c->getProperty ("hot").getArray())
            for (int i = 0; i < juce::jmin (numHotCues, hot->size()); ++i)
                t.hotCues[(size_t) i] = (double) (*hot)[i];
    }
}

void TrackLoader::writeMeta (const Track& t)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", Track::cacheFormatVersion);
    root->setProperty ("source", t.sourceFile.getFullPathName());
    root->setProperty ("sampleRate", t.sampleRate);
    root->setProperty ("frames", (juce::int64) t.totalFrames.load());
    root->setProperty ("decoded", t.decodeComplete.load());
    root->setProperty ("format", t.formatName);
    root->setProperty ("bitDepth", t.sourceBitDepth);
    root->setProperty ("channels", t.sourceChannels);
    root->setProperty ("title", t.title);
    root->setProperty ("artist", t.artist);

    if (t.analysed.load())
    {
        auto* a = new juce::DynamicObject();
        a->setProperty ("bpm", t.grid.bpm.load());
        a->setProperty ("anchor", t.grid.anchorFrame.load());
        a->setProperty ("beatsPerBar", t.grid.beatsPerBar.load());
        a->setProperty ("key", t.getKey());
        a->setProperty ("camelot", t.getCamelot());
        a->setProperty ("loudness", (double) t.loudnessDb.load());
        a->setProperty ("autoGain", (double) t.autoGainDb.load());
        root->setProperty ("analysis", juce::var (a));
    }

    auto* c = new juce::DynamicObject();
    c->setProperty ("cue", t.cuePoint.load());
    juce::Array<juce::var> hot;
    for (auto& h : t.hotCues) hot.add (h.load());
    c->setProperty ("hot", hot);
    root->setProperty ("cues", juce::var (c));

    metaFileFor (t).replaceWithText (juce::JSON::toString (juce::var (root)));
}

void TrackLoader::saveTrackData (const Track& track)
{
    writeMeta (track);
}

// ---------------------------------------------------------------------------
bool TrackLoader::tryLoadFromCache (Track& t)
{
    const auto meta = metaFileFor (t);
    if (! meta.existsAsFile()) return false;

    const auto json = juce::JSON::parse (meta);
    if ((int) json["version"] != Track::cacheFormatVersion || ! (bool) json["decoded"])
        return false;

    const juce::int64 frames = (juce::int64) json["frames"];
    const auto pcm = audioFileFor (t);
    const auto wave = waveFileFor (t);
    if (frames <= 0 || ! wave.existsAsFile()) return false;

    t.sampleRate = (double) json["sampleRate"];
    if (t.sampleRate <= 0.0) return false;
    if (! t.mapExistingCache (pcm, frames)) return false;

    juce::MemoryBlock mb;
    if (! wave.loadFileAsData (mb)) return false;
    const auto bins = (juce::int64) (mb.getSize() / sizeof (WaveBin));
    t.waveform.resize ((size_t) bins);
    std::memcpy (t.waveform.data(), mb.getData(), (size_t) bins * sizeof (WaveBin));

    t.formatName = json["format"].toString();
    t.sourceBitDepth = (int) json["bitDepth"];
    t.sourceChannels = (int) json["channels"];
    if (json["title"].toString().isNotEmpty()) t.title = json["title"].toString();
    t.artist = json["artist"].toString();

    applyAnalysisJson (t, json);

    t.totalFrames = frames;
    t.binsReady = bins;
    t.framesReady.store (frames, std::memory_order_release);
    t.decodeComplete = true;

    pcm.setLastModificationTime (juce::Time::getCurrentTime());
    return true;
}

bool TrackLoader::decodeIntoCache (Track& t, juce::ThreadPoolJob& job)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (t.sourceFile));
    if (reader == nullptr)
    {
        t.errorMessage = "Unsupported or unreadable file";
        return false;
    }

    t.sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    t.formatName = reader->getFormatName();
    t.sourceBitDepth = (int) reader->bitsPerSample;
    t.sourceChannels = (int) reader->numChannels;

    auto meta = [&] (const char* k) { return reader->metadataValues.getValue (k, {}); };
    if (auto s = meta ("title"); s.isNotEmpty()) t.title = s;
    if (auto s = meta ("INAM"); s.isNotEmpty()) t.title = s;
    if (auto s = meta ("artist"); s.isNotEmpty()) t.artist = s;
    if (auto s = meta ("IART"); s.isNotEmpty()) t.artist = s;

    const juce::int64 length = reader->lengthInSamples;
    if (length <= 0)
    {
        t.errorMessage = "File contains no audio";
        return false;
    }

    t.totalFrames = length;
    if (! t.allocateCache (audioFileFor (t), length))
    {
        t.errorMessage = "Could not create cache file (disk full?)";
        return false;
    }

    t.waveform.assign ((size_t) (length / Track::framesPerBin + 2), WaveBin{});

    const int block = 65536;
    const int srcCh = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buffer (srcCh, block);
    float* out = t.getWritableData();

    // waveform band split state
    const float aLow = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 200.0f / (float) t.sampleRate);
    const float aMid = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2500.0f / (float) t.sampleRate);
    float lpLow = 0.0f, lpLow2 = 0.0f, lpMid = 0.0f;
    float pk = 0.0f, pkLow = 0.0f, pkMid = 0.0f, pkHigh = 0.0f;
    int binFill = 0;
    juce::int64 binIndex = 0;

    auto toByte = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) (v * 255.0f + 0.5f)); };

    for (juce::int64 pos = 0; pos < length; pos += block)
    {
        if (job.shouldExit() || t.cancelDecode.load())
            return false;

        const int n = (int) juce::jmin ((juce::int64) block, length - pos);
        reader->read (&buffer, 0, n, pos, true, srcCh > 1);

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (srcCh > 1 ? 1 : 0);
        float* dst = out + pos * 2;

        for (int i = 0; i < n; ++i)
        {
            const float L = l[i], R = r[i];
            dst[i * 2] = L;
            dst[i * 2 + 1] = R;

            const float m = 0.5f * (L + R);
            lpLow += aLow * (m - lpLow);
            lpLow2 += aLow * (lpLow - lpLow2);
            lpMid += aMid * (m - lpMid);
            const float low = lpLow2;
            const float mid = lpMid - lpLow2;
            const float high = m - lpMid;

            pk = juce::jmax (pk, std::abs (L), std::abs (R));
            pkLow = juce::jmax (pkLow, std::abs (low));
            pkMid = juce::jmax (pkMid, std::abs (mid));
            pkHigh = juce::jmax (pkHigh, std::abs (high));

            if (++binFill == Track::framesPerBin)
            {
                auto& b = t.waveform[(size_t) binIndex];
                b.peak = toByte (pk); b.low = toByte (pkLow); b.mid = toByte (pkMid); b.high = toByte (pkHigh);
                ++binIndex;
                binFill = 0;
                pk = pkLow = pkMid = pkHigh = 0.0f;
            }
        }

        t.binsReady.store (binIndex, std::memory_order_release);
        t.framesReady.store (pos + n, std::memory_order_release);
    }

    if (binFill > 0)
    {
        auto& b = t.waveform[(size_t) binIndex];
        b.peak = toByte (pk); b.low = toByte (pkLow); b.mid = toByte (pkMid); b.high = toByte (pkHigh);
        ++binIndex;
    }
    t.waveform.resize ((size_t) binIndex);
    t.binsReady.store (binIndex, std::memory_order_release);

    waveFileFor (t).replaceWithData (t.waveform.data(), t.waveform.size() * sizeof (WaveBin));

    // keep any earlier grid edits / cues for this file
    const auto metaFile = metaFileFor (t);
    if (metaFile.existsAsFile())
        applyAnalysisJson (t, juce::JSON::parse (metaFile));

    t.decodeComplete = true;
    writeMeta (t);

    trimCache();
    return true;
}

// ---------------------------------------------------------------------------
juce::int64 TrackLoader::getCacheSizeBytes() const
{
    juce::int64 total = 0;
    for (const auto& f : cacheDir.findChildFiles (juce::File::findFiles, false, "*.pcm"))
        total += f.getSize();
    return total;
}

void TrackLoader::trimCache()
{
    auto files = cacheDir.findChildFiles (juce::File::findFiles, false, "*.pcm");
    juce::int64 total = 0;
    for (auto& f : files) total += f.getSize();
    if (total <= cacheLimitBytes) return;

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() < b.getLastModificationTime(); });

    juce::StringArray inUse;
    {
        const juce::ScopedLock sl (inUseLock);
        inUse = keysInUse;
    }

    for (auto& f : files)
    {
        if (total <= cacheLimitBytes) break;
        const auto key = f.getFileNameWithoutExtension();
        if (inUse.contains (key)) continue;
        total -= f.getSize();
        f.deleteFile();
        cacheDir.getChildFile (key + ".wave").deleteFile();
        // .json is kept: it holds the beat grid and cue points
    }
}

void TrackLoader::clearCache()
{
    juce::StringArray inUse;
    {
        const juce::ScopedLock sl (inUseLock);
        inUse = keysInUse;
    }
    for (auto& f : cacheDir.findChildFiles (juce::File::findFiles, false, "*.pcm;*.wave"))
        if (! inUse.contains (f.getFileNameWithoutExtension()))
            f.deleteFile();
}

} // namespace loopdeck
