#include "LibraryScanner.h"

#include <fstream>
#include "AudioTools.h"
#include "Loudness.h"

namespace kal
{
using namespace juce;

double audioFileTempo (const String& fileName, const StringPairArray& metadata)
{
    if (const double named = library::tempoFromName (File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension().toStdString()); named > 0)
        return named;
    if (metadata.getValue (WavAudioFormat::acidOneShot, "0").getIntValue() != 0) return 0;
    const double acid = metadata.getValue (WavAudioFormat::acidTempo, "0").getDoubleValue();
    return acid >= 40 && acid <= 300 ? acid : 0;
}

LibraryScanner::LibraryScanner (File f, int64 openLaterOver) : Thread ("Kalibre library scan"), indexFile (std::move (f))
{
    // A big index (tens of thousands of sounds) is opened on the scan's thread when
    // it starts, so Kalibre isn't held up opening; a small one at once.
    if (indexFile.existsAsFile())
    {
        if (indexFile.getSize() > openLaterOver) pendingOpen = true;
        else
        {
            std::ifstream in (indexFile.getFullPathName().toStdString(), std::ios::binary);
            library::readIndex (in, live);
        }
    }
}

// Written beside it and then put in its place, so a save cut short (Kalibre quit,
// the Mac turned off) leaves the last one whole.
bool LibraryScanner::saveIndex (const library::Index& index, const File& to)
{
    to.getParentDirectory().createDirectory();
    const auto part = to.getSiblingFile (to.getFileName() + ".part");
    {
        std::ofstream out (part.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
        if (! out || ! library::writeIndex (index, out)) return false;
    }
    return part.moveFileTo (to);
}

LibraryScanner::~LibraryScanner()
{
    // Time for the scan to save what it has read since its last save (a few
    // seconds with half a million sounds); cut off, the last save stays whole.
    stopThread (15000);
    cancelPendingUpdate();
}

String LibraryScanner::wildcard()
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    return fm.getWildcardForAllFormats() + ";*.mid;*.midi";
}

void LibraryScanner::scan (const std::vector<std::string>& toScan)
{
    {
        // Still opening the index: it scans these folders once it's open.
        const ScopedLock sl (lock);
        if (pendingOpen && isThreadRunning()) { folders = toScan; return; }
    }
    stopThread (4000);
    folders = toScan;
    found = 0;
    read = 0;
    analysed = 0;
    toRead = 0;
    startThread (Thread::Priority::low);
}

// What the scan hears in a sample (0095): its loudness (BS.1770, as the
// master's meter), its loudest sample, its note, and, when its name and file
// don't say, its tempo. The first minute is enough.
void LibraryScanner::listen (AudioFormatReader& r, library::Item& it)
{
    const int n = (int) jmin<int64> (r.lengthInSamples, (int64) (r.sampleRate * 60.0));
    AudioBuffer<float> buf (jmax (1, jmin (2, (int) r.numChannels)), n);
    if (! r.read (&buf, 0, n, 0, true, buf.getNumChannels() > 1)) return;
    if (buf.getNumChannels() == 2 && r.numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);

    const float top = buf.getMagnitude (0, n);
    it.peakDb = top > 0 ? 20.0 * std::log10 ((double) top) : library::silentDb;
    loudness::Meter meter;
    meter.prepare (r.sampleRate);
    const float* left = buf.getReadPointer (0);
    const float* right = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
    for (int at = 0; at < n; at += 4096) meter.process (left + at, right + at, jmin (4096, n - at));
    // A mono file was measured on both sides: counted once, as BS.1770 counts its one channel.
    const double lufs = meter.integrated() - (r.numChannels == 1 ? 3.0103 : 0.0);
    it.lufs = lufs > -70 ? lufs : library::silentDb;

    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = buf.getNumChannels() > 1 ? 0.5f * (left[i] + right[i]) : left[i];
    it.note = audiotools::noteOf (mono.data(), n, r.sampleRate, &it.cents);
    const auto shape = audiotools::hitShape (mono.data(), n, r.sampleRate);   // 0106
    it.onsetMs = shape.onsetMs;
    it.attackMs = shape.attackMs;
    it.tailDb = shape.tailDb;
    it.zcr = shape.zcr;
    if (it.bpm <= 0)
    {
        const auto g = audiotools::tempoOf (mono.data(), n, r.sampleRate);
        if (g.bpm > 0) { it.bpm = g.bpm; it.bpmFrom = library::Item::TempoFrom::Heard; it.loop = g.loop; }
    }
}

// A sound read by the last version of the scan (0095): only its hit's shape (0106)
// added, from as much as a drum could be (8.5 seconds), not all of it heard again.
bool LibraryScanner::topUp (const File& f, library::Item& it, AudioFormatManager& fm)
{
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->sampleRate <= 0 || r->lengthInSamples <= 0) return false;
    const int n = (int) jmin<int64> (r->lengthInSamples, (int64) (r->sampleRate * 8.5));
    AudioBuffer<float> buf (jmax (1, jmin (2, (int) r->numChannels)), n);
    if (! r->read (&buf, 0, n, 0, true, buf.getNumChannels() > 1)) return false;
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = buf.getNumChannels() > 1 && r->numChannels > 1 ? 0.5f * (buf.getSample (0, i) + buf.getSample (1, i)) : buf.getSample (0, i);
    const auto shape = audiotools::hitShape (mono.data(), n, r->sampleRate);
    it.onsetMs = shape.onsetMs;
    it.attackMs = shape.attackMs;
    it.tailDb = shape.tailDb;
    it.zcr = shape.zcr;
    it.analysis = library::analysisVersion;
    return true;
}

bool LibraryScanner::analyse (const File& f, library::Item& it, AudioFormatManager& fm)
{
    it.path = f.getFullPathName().toStdString();
    it.size = f.getSize();
    it.modified = f.getLastModificationTime().toMilliseconds();
    it.readable = false;
    if (library::isMidiExtension (library::extensionOf (it.path)))
    {
        it.kind = library::Kind::Midi;
        FileInputStream in (f);
        MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) return false;
        const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960.0;
        it.notes.clear();
        it.beats = 0;
        it.bpm = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            auto seq = *mf.getTrack (t);
            seq.updateMatchedPairs();
            for (int e = 0; e < seq.getNumEvents(); ++e)
            {
                const auto& m = seq.getEventPointer (e)->message;
                if (m.isTempoMetaEvent() && it.bpm <= 0) it.bpm = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (! m.isNoteOn()) continue;
                const double start = m.getTimeStamp() / tpq;
                const int off = seq.getIndexOfMatchingKeyUp (e);
                const double end = off >= 0 ? seq.getEventPointer (off)->message.getTimeStamp() / tpq : start + 1;
                it.beats = jmax (it.beats, end);
                if (it.notes.size() < 4000) it.notes.push_back ({ (float) start, (float) jmax (0.01, end - start), m.getNoteNumber() });
            }
        }
        if (it.bpm < 20 || it.bpm > 400) it.bpm = 0;
        it.readable = ! it.notes.empty();
        return it.readable;
    }

    it.kind = library::Kind::Audio;
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->sampleRate <= 0 || r->lengthInSamples <= 0) return false;
    it.sampleRate = r->sampleRate;
    it.channels = (int) r->numChannels;
    it.seconds = (double) r->lengthInSamples / r->sampleRate;
    it.bpm = audioFileTempo (f.getFileName(), r->metadataValues);
    // The waveform: the loudest of each of twice as many stretches as columns.
    // A long file is sampled a window at a time, so a whole album scans quickly.
    const int stretches = library::peakColumns * 2;
    const auto total = r->lengthInSamples;
    const bool sampled = it.seconds > 60.0;
    std::vector<float> loudest;
    loudest.reserve ((size_t) stretches);
    const int channels = jmin (2, (int) r->numChannels);
    for (int i = 0; i < stretches; ++i)
    {
        const auto from = total * i / stretches;
        auto count = total * (i + 1) / stretches - from;
        if (sampled) count = jmin<int64> (count, 4096);
        juce::Range<float> levels[2];
        r->readMaxLevels (from, count, levels, channels);
        float m = 0;
        for (int c = 0; c < channels; ++c) m = jmax (m, std::abs (levels[c].getStart()), std::abs (levels[c].getEnd()));
        loudest.push_back (m);
    }
    it.peaks = library::peaksFrom (loudest);
    it.bpmFrom = it.bpm > 0 ? (library::tempoFromName (f.getFileNameWithoutExtension().toStdString()) > 0 ? library::Item::TempoFrom::Name
                                                                                                       : library::Item::TempoFrom::File)
                            : library::Item::TempoFrom::Unknown;
    listen (*r, it);
    it.analysis = library::analysisVersion;
    it.readable = true;
    return true;
}

void LibraryScanner::run()
{
    library::Index work;
    if (pendingOpen)
    {
        opening = true;
        {
            std::ifstream in (indexFile.getFullPathName().toStdString(), std::ios::binary);
            library::readIndex (in, work);
        }
        {
            const ScopedLock sl (lock);
            shared = work;
            fresh = true;
            pendingOpen = false;
        }
        opening = false;
        triggerAsyncUpdate();
    }
    else
    {
        const ScopedLock sl (lock);
        work = live;   // live isn't changed off the message thread while this runs: it's only replaced here, from `shared`
    }
    std::vector<std::string> folders;
    {
        const ScopedLock sl (lock);
        folders = this->folders;
    }
    AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto pattern = wildcard();

    // Every readable file under the folders, with its size and date.
    std::map<std::string, std::pair<int64, int64>> files;
    for (auto& folder : folders)
    {
        const File dir (folder);
        if (! dir.isDirectory()) continue;
        for (const auto& e : RangedDirectoryIterator (dir, true, pattern, File::findFiles, File::FollowSymlinks::noCycles))
        {
            if (threadShouldExit()) return;
            if (e.isHidden()) continue;
            files[e.getFile().getFullPathName().toStdString()] = { e.getFileSize(), e.getModificationTime().toMilliseconds() };
            found = (int) files.size();
        }
    }

    // What's gone, or no longer in a chosen folder, is forgotten.
    for (auto it = work.items.begin(); it != work.items.end();)
        it = files.count (it->first) == 0 ? work.items.erase (it) : std::next (it);

    auto handOver = [this, &work]
    {
        const ScopedLock sl (lock);
        shared = work;
        fresh = true;
        triggerAsyncUpdate();
    };
    auto save = [this, &work] { saveIndex (work, indexFile); };
    // Only what's new or changed is read; the rest was only checked.
    std::vector<std::pair<std::string, std::pair<int64, int64>>> pending;
    for (auto& [path, info] : files)
    {
        const auto cached = work.items.find (path);
        if (library::needsScan (cached != work.items.end() ? &cached->second : nullptr, info.first, info.second)) pending.push_back ({ path, info });
    }
    toRead = (int) pending.size();
    handOver();
    // A big library is handed to the list less often: each hand-over copies it.
    const uint32 handOverEvery = work.items.size() + pending.size() > 20000 ? 15000u : 400u;
    auto lastHandOver = Time::getMillisecondCounter();
    int n = 0, sinceSave = 0;
    for (auto& [path, info] : pending)
    {
        if (threadShouldExit()) break;
        library::Item item;
        const auto cached = work.items.find (path);
        const bool onlyShape = cached != work.items.end() && cached->second.size == info.first && cached->second.modified == info.second
                            && cached->second.kind == library::Kind::Audio && cached->second.analysis == 2;
        if (onlyShape) { item = cached->second; if (! topUp (File (path), item, fm)) item.analysis = library::analysisVersion; }
        else analyse (File (path), item, fm);
        ++analysed;
        item.size = info.first;
        item.modified = info.second;
        work.items[path] = std::move (item);
        read = ++n;
        if (++sinceSave >= jmax (1, saveEvery)) { save(); sinceSave = 0; }
        const auto now = Time::getMillisecondCounter();
        if (now - lastHandOver > handOverEvery) { handOver(); lastHandOver = now; }
        if (afterEach) afterEach();
    }
    handOver();
    save();
}

void LibraryScanner::handleAsyncUpdate()
{
    {
        const ScopedLock sl (lock);
        if (! fresh) return;
        live = std::move (shared);
        shared = {};
        fresh = false;
    }
    if (onChange) onChange();
}

bool LibraryScanner::waitUntilDone (int ms)
{
    const auto until = Time::getMillisecondCounter() + (uint32) ms;
    while (isThreadRunning() && Time::getMillisecondCounter() < until)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    handleUpdateNowIfNeeded();
    return ! isThreadRunning();
}

} // namespace kal
