#include "AraHost.h"

#if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)

#include <juce_audio_formats/juce_audio_formats.h>

#include <ARA_API/ARAInterface.h>
#include <ARA_Library/Dispatch/ARAHostDispatch.h>

#include <map>
#include <mutex>

namespace kal
{
using namespace juce;

bool araAvailable() { return true; }

namespace
{
//==============================================================================
// What the musical context answers: tempo and bar signatures.
struct Timeline
{
    std::vector<ARA::ARAContentTempoEntry> tempos;
    std::vector<ARA::ARAContentBarSignature> bars;
};

struct Source
{
    Source (ARA::Host::DocumentController& dc, const File& f, const String& id)
        : file (f), persistentId (id.toStdString())
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        reader.reset (fm.createReaderFor (f));
        auto props = ARAHostModel::AudioSource::getEmptyProperties();
        props.name = name.c_str();
        props.persistentID = persistentId.c_str();
        props.sampleCount = reader ? reader->lengthInSamples : 0;
        props.sampleRate = reader ? reader->sampleRate : 48000.0;
        props.channelCount = reader ? (int) reader->numChannels : 1;
        props.merits64BitSamples = false;
        source = std::make_unique<ARAHostModel::AudioSource> (Converter::toHostRef (this), dc, props);
        source->enableAudioSourceSamplesAccess (true);
    }

    bool read (float* const* buffers, int64 start, int64 count)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (! reader || count > std::numeric_limits<int>::max()) return false;
        return reader->read (buffers, (int) reader->numChannels, start, (int) count);
    }

    using Converter = ARAHostModel::ConversionFunctions<Source*, ARA::ARAAudioSourceHostRef>;

    File file;
    std::string persistentId, name { "audio" };
    std::unique_ptr<AudioFormatReader> reader;
    std::mutex mutex;
    std::unique_ptr<ARAHostModel::AudioSource> source;
};

struct AudioAccess final : public ARA::Host::AudioAccessControllerInterface
{
    struct Reader { ARA::ARAAudioSourceHostRef source; bool use64; };
    using Converter = ARAHostModel::ConversionFunctions<Reader*, ARA::ARAAudioReaderHostRef>;

    ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef s, bool use64) noexcept override
    {
        auto r = std::make_unique<Reader> (Reader { s, use64 });
        auto ref = Converter::toHostRef (r.get());
        const std::lock_guard<std::mutex> lock (mutex);
        readers[r.get()] = std::move (r);
        return ref;
    }

    bool readAudioSamples (ARA::ARAAudioReaderHostRef ref, ARA::ARASamplePosition pos, ARA::ARASampleCount count,
                           void* const* buffers) noexcept override
    {
        auto* r = Converter::fromHostRef (ref);
        if (r->use64) return false;
        return Source::Converter::fromHostRef (r->source)->read (reinterpret_cast<float* const*> (buffers), pos, count);
    }

    void destroyAudioReader (ARA::ARAAudioReaderHostRef ref) noexcept override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        readers.erase (Converter::fromHostRef (ref));
    }

    std::mutex mutex;
    std::map<Reader*, std::unique_ptr<Reader>> readers;
};

struct Archiving final : public ARA::Host::ArchivingControllerInterface
{
    using ReaderConverter = ARAHostModel::ConversionFunctions<MemoryBlock*, ARA::ARAArchiveReaderHostRef>;
    using WriterConverter = ARAHostModel::ConversionFunctions<MemoryOutputStream*, ARA::ARAArchiveWriterHostRef>;

    ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef r) noexcept override
    {
        return (ARA::ARASize) ReaderConverter::fromHostRef (r)->getSize();
    }

    bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef r, ARA::ARASize pos, ARA::ARASize length, ARA::ARAByte* buffer) noexcept override
    {
        auto* block = ReaderConverter::fromHostRef (r);
        if (pos + length > block->getSize()) return false;
        std::memcpy (buffer, addBytesToPointer (block->getData(), pos), length);
        return true;
    }

    bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef w, ARA::ARASize pos, ARA::ARASize length, const ARA::ARAByte* buffer) noexcept override
    {
        auto* out = WriterConverter::fromHostRef (w);
        return out->setPosition ((int64) pos) && out->write (buffer, length);
    }

    void notifyDocumentArchivingProgress (float) noexcept override {}
    void notifyDocumentUnarchivingProgress (float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef) noexcept override { return "com.kallumshah.kalibre.ara"; }
};

struct ContentAccess final : public ARA::Host::ContentAccessControllerInterface
{
    explicit ContentAccess (const Timeline& t) : timeline (t) {}

    using Converter = ARAHostModel::ConversionFunctions<intptr_t, ARA::ARAContentReaderHostRef>;

    bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type) noexcept override
    {
        return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
    }
    ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef, ARA::ARAContentType) noexcept override
    {
        return ARA::kARAContentGradeAdjusted;
    }
    ARA::ARAContentReaderHostRef createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type,
                                                                    const ARA::ARAContentTimeRange*) noexcept override
    {
        return Converter::toHostRef ((intptr_t) type);
    }
    bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return false; }
    ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return 0; }
    ARA::ARAContentReaderHostRef createAudioSourceContentReader (ARA::ARAAudioSourceHostRef, ARA::ARAContentType,
                                                                 const ARA::ARAContentTimeRange*) noexcept override { return nullptr; }
    ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef r) noexcept override
    {
        const auto type = (ARA::ARAContentType) Converter::fromHostRef (r);
        if (type == ARA::kARAContentTypeTempoEntries) return (ARA::ARAInt32) timeline.tempos.size();
        if (type == ARA::kARAContentTypeBarSignatures) return (ARA::ARAInt32) timeline.bars.size();
        return 0;
    }
    const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef r, ARA::ARAInt32 i) noexcept override
    {
        const auto type = (ARA::ARAContentType) Converter::fromHostRef (r);
        if (type == ARA::kARAContentTypeTempoEntries) return &timeline.tempos[(size_t) i];
        if (type == ARA::kARAContentTypeBarSignatures) return &timeline.bars[(size_t) i];
        return nullptr;
    }
    void destroyContentReader (ARA::ARAContentReaderHostRef) noexcept override {}

    const Timeline& timeline;
};

struct ModelUpdates final : public ARA::Host::ModelUpdateControllerInterface
{
    void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState, float) noexcept override {}
    void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    void notifyDocumentDataChanged() noexcept override {}
};

struct Playback final : public ARA::Host::PlaybackControllerInterface
{
    void requestStartPlayback() noexcept override {}
    void requestStopPlayback() noexcept override {}
    void requestSetPlaybackPosition (ARA::ARATimePosition) noexcept override {}
    void requestSetCycleRange (ARA::ARATimePosition, ARA::ARATimeDuration) noexcept override {}
    void requestEnableCycle (bool) noexcept override {}
};

//==============================================================================
class Session final : public AraSession
{
public:
    Session (AudioPluginInstance& p, const std::string& archiveText, std::function<void()> ready)
        : plugin (p), onReady (std::move (ready))
    {
        if (! archiveText.empty()) pendingArchive.fromBase64Encoding (archiveText);
        createARAFactoryAsync (plugin, [this, alive = std::weak_ptr<int> (token)] (ARAFactoryWrapper factory)
        {
            if (alive.expired() || factory.get() == nullptr) return;
            document = ARAHostDocumentController::create (std::move (factory), "Kalibre",
                                                          std::make_unique<AudioAccess>(), std::make_unique<Archiving>(),
                                                          std::make_unique<ContentAccess> (timeline), std::make_unique<ModelUpdates>(),
                                                          std::make_unique<Playback>());
            if (! document) return;
            const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
            auto extension = document->bindDocumentToPluginInstance (plugin, roles, roles);
            playbackRenderer = extension.getPlaybackRendererInterface();
            editorRenderer = extension.getEditorRendererInterface();
            if (onReady) MessageManager::callAsync (onReady);
        });
    }

    ~Session() override
    {
        token.reset();
        clearRegions();
    }

    bool ready() const override { return document != nullptr; }

    void update (const Project& project, const Track& track, const File& folder,
                 const std::function<void (std::function<void()>)>& whilePluginStopped) override
    {
        if (! document) return;
        // What the document should hold, as text: only rebuilt when it changes.
        String shape;
        shape << project.tempo.tempos().size() << "/" << project.tempo.meters().size() << ":" << track.delayMs << "|";
        for (auto& tp : project.tempo.tempos()) shape << tp.tick << "," << tp.bpm << "," << (int) tp.rampToNext << ";";
        for (auto& c : track.clips())
            if (! c.isMidi() && ! c.muted)
                shape << (int64) c.id << ":" << c.audio.file << ":" << c.start << ":" << c.length << ":" << c.offset << ":" << c.audio.sourceStart << ";";
        if (shape == built) return;
        built = shape;

        whilePluginStopped ([&]
        {
            auto& dc = document->getDocumentController();
            const ARAEditGuard guard (dc);
            clearRegions();
            fillTimeline (project.tempo);

            auto contextProps = ARAHostModel::MusicalContext::getEmptyProperties();
            contextProps.name = "Song";
            contextProps.orderIndex = 0;
            context = std::make_unique<ARAHostModel::MusicalContext> (ARAHostModel::ConversionFunctions<Session*, ARA::ARAMusicalContextHostRef>::toHostRef (this), dc, contextProps);

            auto sequenceProps = ARAHostModel::RegionSequence::getEmptyProperties();
            trackName = track.name;
            sequenceProps.name = trackName.c_str();
            sequenceProps.orderIndex = 0;
            sequenceProps.musicalContextRef = context->getPluginRef();
            sequence = std::make_unique<ARAHostModel::RegionSequence> (ARAHostModel::ConversionFunctions<Session*, ARA::ARARegionSequenceHostRef>::toHostRef (this), dc, sequenceProps);

            for (auto& c : track.clips())
            {
                if (c.isMidi() || c.muted) continue;
                File f = File::isAbsolutePath (c.audio.file) ? File (c.audio.file) : folder.getChildFile (c.audio.file);
                // One audio source per file, shared by every clip made from it.
                auto& src = sources[f.getFullPathName()];
                if (! src) src = std::make_unique<Source> (dc, f, "src:" + f.getFullPathName());
                auto entry = std::make_unique<Region>();
                entry->modificationId = ("mod:" + String ((int64) c.id)).toStdString();
                auto modProps = ARAHostModel::AudioModification::getEmptyProperties();
                modProps.persistentID = entry->modificationId.c_str();
                entry->modification = std::make_unique<ARAHostModel::AudioModification> (ARAHostModel::ConversionFunctions<Region*, ARA::ARAAudioModificationHostRef>::toHostRef (entry.get()), dc, *src->source, modProps);

                const auto& tempo = project.tempo;
                const double start = tempo.secondsAt (c.start);
                const double seconds = tempo.secondsAt (c.end()) - start;
                auto regionProps = ARAHostModel::PlaybackRegion::getEmptyProperties();
                regionProps.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
                regionProps.startInModificationTime = c.audio.sourceStart + (start - tempo.secondsAt (c.start - c.offset));
                regionProps.durationInModificationTime = seconds;
                regionProps.startInPlaybackTime = start + track.delayMs / 1000.0;
                regionProps.durationInPlaybackTime = seconds;
                regionProps.musicalContextRef = context->getPluginRef();
                regionProps.regionSequenceRef = sequence->getPluginRef();
                entry->region = std::make_unique<ARAHostModel::PlaybackRegion> (ARAHostModel::ConversionFunctions<Region*, ARA::ARAPlaybackRegionHostRef>::toHostRef (entry.get()), dc, *entry->modification, regionProps);
                regions.push_back (std::move (entry));
            }

            // The plug-in's edits, put back on the objects with the same ids.
            if (pendingArchive.getSize() > 0)
            {
                dc.restoreObjectsFromArchive (Archiving::ReaderConverter::toHostRef (&pendingArchive), nullptr);
                pendingArchive.reset();
            }

            for (auto& r : regions)
            {
                playbackRenderer.add (*r->region);
                editorRenderer.add (*r->region);
            }
        });
    }

    std::string archive() override
    {
        if (! document) return pendingArchive.getSize() > 0 ? pendingArchive.toBase64Encoding().toStdString() : std::string();
        MemoryOutputStream out;
        if (! document->getDocumentController().storeObjectsToArchive (Archiving::WriterConverter::toHostRef (&out), nullptr))
            return {};
        return out.getMemoryBlock().toBase64Encoding().toStdString();
    }

private:
    struct Region
    {
        std::string modificationId;
        std::unique_ptr<ARAHostModel::AudioModification> modification;
        std::unique_ptr<ARAHostModel::PlaybackRegion> region;
    };

    void clearRegions()
    {
        for (auto& r : regions)
        {
            if (! r->region) continue;
            playbackRenderer.remove (*r->region);
            editorRenderer.remove (*r->region);
        }
        regions.clear();
        sequence.reset();
        context.reset();
    }

    void fillTimeline (const TempoMap& tempo)
    {
        timeline.tempos.clear();
        timeline.bars.clear();
        // A ramp is given as an entry every beat; held tempos at their points.
        const auto& points = tempo.tempos();
        for (size_t i = 0; i < points.size(); ++i)
        {
            const Tick end = i + 1 < points.size() ? points[i + 1].tick : points[i].tick + PPQ;
            const Tick step = points[i].rampToNext ? PPQ : std::max<Tick> (1, end - points[i].tick);
            for (Tick t = points[i].tick; t < end; t += step)
                timeline.tempos.push_back ({ tempo.secondsAt (t), (double) t / PPQ });
        }
        const Tick last = points.back().tick + PPQ * 4;
        timeline.tempos.push_back ({ tempo.secondsAt (last), (double) last / PPQ });
        for (auto& m : tempo.meters())
            timeline.bars.push_back ({ m.numerator, m.denominator, (double) tempo.barStart (m.bar) / PPQ });
    }

    AudioPluginInstance& plugin;
    std::function<void()> onReady;
    std::shared_ptr<int> token = std::make_shared<int> (0);
    std::unique_ptr<ARAHostDocumentController> document;
    ARAHostModel::PlaybackRendererInterface playbackRenderer;
    ARAHostModel::EditorRendererInterface editorRenderer;
    Timeline timeline;
    std::unique_ptr<ARAHostModel::MusicalContext> context;
    std::unique_ptr<ARAHostModel::RegionSequence> sequence;
    std::map<String, std::unique_ptr<Source>> sources;
    std::vector<std::unique_ptr<Region>> regions;
    std::string trackName;
    MemoryBlock pendingArchive;
    String built;
};
} // namespace

std::unique_ptr<AraSession> startAra (AudioPluginInstance& plugin, const std::string& archive, std::function<void()> onReady)
{
    PluginDescription d;
    plugin.fillInPluginDescription (d);
    if (! d.hasARAExtension) return nullptr;
    return std::make_unique<Session> (plugin, archive, std::move (onReady));
}

} // namespace kal

#else

namespace kal
{
bool araAvailable() { return false; }
std::unique_ptr<AraSession> startAra (juce::AudioPluginInstance&, const std::string&, std::function<void()>) { return nullptr; }
} // namespace kal

#endif
