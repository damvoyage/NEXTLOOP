#include "Track.h"

namespace loopdeck
{

Track::Track (const juce::File& source) : sourceFile (source)
{
    for (auto& c : hotCues)
        c.store (-1.0);

    title = source.getFileNameWithoutExtension();
}

Track::~Track()
{
    releaseMapping();
}

void Track::releaseMapping()
{
    data = nullptr;
    writableData = nullptr;
    mapping.reset();
}

/** Grows a file to an exact size (sparse where the OS allows). Works on macOS 10.13+ and Windows 7+. */
static bool resizeFileOnDisk (const juce::File& f, juce::int64 bytes)
{
    juce::FileOutputStream os (f);
    if (os.failedToOpen()) return false;
    if (! os.setPosition (bytes)) return false;
    return os.truncate().wasOk() && f.getSize() == bytes;
}

bool Track::allocateCache (const juce::File& audioCacheFile, juce::int64 frames)
{
    releaseMapping();
    frames = juce::jmax ((juce::int64) 1, frames);

    audioCacheFile.deleteFile();
    if (! audioCacheFile.create())
        return false;

    if (! resizeFileOnDisk (audioCacheFile, frames * 2 * (juce::int64) sizeof (float)))
        return false;

    mapping = std::make_unique<juce::MemoryMappedFile> (audioCacheFile, juce::MemoryMappedFile::readWrite, false);
    if (mapping->getData() == nullptr)
    {
        mapping.reset();
        return false;
    }

    writableData = static_cast<float*> (mapping->getData());
    data = writableData;
    capacityFrames = frames;
    return true;
}

bool Track::mapExistingCache (const juce::File& audioCacheFile, juce::int64 frames)
{
    releaseMapping();
    if (! audioCacheFile.existsAsFile() || audioCacheFile.getSize() < frames * 2 * (juce::int64) sizeof (float))
        return false;

    mapping = std::make_unique<juce::MemoryMappedFile> (audioCacheFile, juce::MemoryMappedFile::readOnly, false);
    if (mapping->getData() == nullptr)
    {
        mapping.reset();
        return false;
    }

    data = static_cast<const float*> (mapping->getData());
    capacityFrames = frames;
    return true;
}

void Track::readSafe (double pos, juce::int64 ready, float& outL, float& outR) const noexcept
{
    outL = outR = 0.0f;
    if (data == nullptr || ready <= 0)
        return;

    const auto i0 = (juce::int64) std::floor (pos);
    const float t = (float) (pos - (double) i0);
    float aL, aR, bL, bR;
    auto get = [&] (juce::int64 i, float& l, float& r)
    {
        if (i < 0 || i >= ready) { l = r = 0.0f; return; }
        l = data[i * 2]; r = data[i * 2 + 1];
    };
    get (i0, aL, aR);
    get (i0 + 1, bL, bR);
    outL = aL + (bL - aL) * t;
    outR = aR + (bR - aR) * t;
}

} // namespace loopdeck
