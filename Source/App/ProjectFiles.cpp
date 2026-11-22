/*
    The Controller's file commands (decision 0041): relinking, Collect and
    Save, archiving, templates, Final Cut XML and batch exports. The work on
    the project itself is Core/ProjectTools; this moves the files.
*/

#include "Controller.h"
#include "Articulations.h"
#include "DawProject.h"
#include "AudioFiles.h"
#include "MusicXml.h"
#include "Theme.h"

#include "Edit.h"
#include "ProjectFile.h"
#include "ProjectTools.h"

namespace kal
{
using namespace juce;

File Controller::resolve (const std::string& path) const
{
    return File::isAbsolutePath (path) ? File (path) : projectFolder().getChildFile (String (path));
}

std::vector<std::string> Controller::missingFiles() const
{
    std::vector<std::string> out;
    for (auto& f : projecttools::referencedFiles (project))
        if (! resolve (f).existsAsFile()) out.push_back (f);
    return out;
}

int Controller::relinkFrom (const File& folder)
{
    const auto missing = missingFiles();
    if (missing.empty()) { say ("No files are missing."); return 0; }
    std::vector<std::string> candidates;
    for (const auto& e : RangedDirectoryIterator (folder, true, "*", File::findFiles))
    {
        if (e.getFile().hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf;mov;mp4"))
            candidates.push_back (e.getFile().getFullPathName().toStdString());
        if (candidates.size() > 50000) break;
    }
    const auto found = projecttools::relinkByName (missing, candidates);
    if (! found.empty()) apply ("Relink Files", [found] (Project& p) { projecttools::replaceFiles (p, found); });
    const int still = (int) (missing.size() - found.size());
    say ("Found " + String ((int) found.size()) + " of " + String ((int) missing.size()) + " missing files."
         + (still > 0 ? " " + String (still) + " still missing: try another folder." : String()), still > 0);
    return (int) found.size();
}

bool Controller::collectAndSave()
{
    if (! file.existsAsFile()) { say ("Save the project first, so its files have a folder to go in."); return false; }
    const auto folder = projectFolder();
    const auto audio = folder.getChildFile ("Audio");
    audio.createDirectory();
    std::map<std::string, std::string> moved;
    int copied = 0, missing = 0;
    for (auto& f : projecttools::referencedFiles (project))
    {
        const auto src = resolve (f);
        if (src.isAChildOf (folder)) continue;   // already with the project
        if (! src.existsAsFile()) { ++missing; continue; }
        auto dest = audio.getChildFile (src.getFileName());
        if (dest.existsAsFile() && dest.getSize() != src.getSize()) dest = audio.getNonexistentChildFile (src.getFileNameWithoutExtension(), src.getFileExtension());
        if (! dest.existsAsFile() && ! src.copyFileTo (dest)) { say ("Couldn't copy " + src.getFileName() + ".", true); return false; }
        moved[f] = ("Audio/" + dest.getFileName()).toStdString();
        ++copied;
    }
    if (! moved.empty()) apply ("Collect Files", [moved] (Project& p) { projecttools::replaceFiles (p, moved); });
    const bool ok = save();
    say ("Collected " + String (copied) + (copied == 1 ? " file" : " files") + " into the project's Audio folder and saved."
         + (missing > 0 ? " " + String (missing) + " could not be found." : String()), missing > 0);
    return ok;
}

bool Controller::archive (const File& zip)
{
    storeDeviceStates();
    Project copy = project;
    std::map<std::string, std::string> inside;
    std::vector<std::pair<File, String>> files;
    std::set<String> used;
    for (auto& f : projecttools::referencedFiles (project))
    {
        const auto src = resolve (f);
        if (! src.existsAsFile()) continue;
        String name = src.getFileName();
        for (int n = 2; used.count (name.toLowerCase()); ++n) name = src.getFileNameWithoutExtension() + " " + String (n) + src.getFileExtension();
        used.insert (name.toLowerCase());
        inside[f] = ("Audio/" + name).toStdString();
        files.push_back ({ src, name });
    }
    projecttools::replaceFiles (copy, inside);
    const String song = file.existsAsFile() ? file.getFileNameWithoutExtension() : String ("Untitled");
    ZipFile::Builder builder;
    copy.workSeconds = workClock.active;
    const auto projectText = String (file::save (copy));
    builder.addEntry (new MemoryInputStream (projectText.toRawUTF8(), projectText.getNumBytesAsUTF8(), true), 9, song + "/" + song + ".kalibre", Time::getCurrentTime());
    for (auto& [src, name] : files) builder.addFile (src, 0, song + "/Audio/" + name);   // audio is stored, not squeezed
    zip.deleteFile();
    FileOutputStream out (zip);
    if (! out.openedOk() || ! builder.writeToStream (out, nullptr)) { say ("The archive couldn't be written.", true); return false; }
    say ("Archived the project and " + String ((int) files.size()) + " files into " + zip.getFileName() + ". Unzip it anywhere and open the .kalibre inside.");
    return true;
}

File Controller::templatesFolder() const
{
    auto f = scriptsFolder().getParentDirectory().getChildFile ("Templates");
    f.createDirectory();
    return f;
}

bool Controller::saveTemplate (const String& name, bool keepMusic)
{
    storeDeviceStates();
    const auto t = projecttools::asTemplate (project, keepMusic);
    const auto f = templatesFolder().getChildFile (File::createLegalFileName (name) + ".kalibre");
    if (! f.replaceWithText (String (file::save (t)))) { say ("The template couldn't be saved.", true); return false; }
    say ("Saved the template " + name.quoted() + ". File > New from Template starts a song from it.");
    return true;
}

bool Controller::newFromTemplate (const File& f)
{
    std::string error;
    auto loaded = file::load (f.loadFileAsString().toStdString(), &error);
    if (! loaded) { say (String (error), true); return false; }
    if (edit::overTrackLimit (*loaded)) { refuseTooManyTracks(); return false; }   // 0088
    engine.stop();
    engine.newSong();
    project = std::move (*loaded);
    project.workSeconds = 0;   // a new song from a template starts its own time
    beginWorkClock();
    refileEffects (project, engine.plugins());
    history.clear();
    file = File();
    dirty = false;
    focusTrack = project.tracks.empty() ? 0 : project.tracks.front().id;
    selectedTracks.clear();
    if (focusTrack) selectedTracks.push_back (focusTrack);
    selectedClip = 0;
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    say ("A new song from the template " + f.getFileNameWithoutExtension().quoted() + ".");
    return true;
}

bool Controller::exportXml (const File& f)
{
    auto seconds = [this] (const std::string& path)
    {
        double s = 0, sr = 0;
        int ch = 0;
        return AudioCache::describe (resolve (path), s, sr, ch) ? s : 0.0;
    };
    // Paths in the XML are absolute: the other program doesn't know the project's folder.
    Project copy = project;
    std::map<std::string, std::string> absolute;
    for (auto& p : projecttools::referencedFiles (project)) absolute[p] = resolve (p).getFullPathName().toStdString();
    projecttools::replaceFiles (copy, absolute);
    if (! f.replaceWithText (String (projecttools::fcpXml (copy, seconds, 25)))) { say ("The XML couldn't be written.", true); return false; }
    say ("Exported the audio clips as Final Cut XML: " + f.getFileName() + ". MIDI clips aren't in it; export them as audio first.");
    return true;
}

bool Controller::importXml (const File& f)
{
    auto xml = parseXML (f);
    if (! xml || ! xml->hasTagName ("xmeml")) { say ("That isn't a Final Cut XML file.", true); return false; }
    auto* sequence = xml->getChildByName ("sequence");
    if (! sequence) sequence = xml->getChildByName ("project") ? xml->getChildByName ("project")->getChildByName ("children")->getChildByName ("sequence") : nullptr;
    if (! sequence) { say ("The XML has no sequence in it.", true); return false; }
    const double fps = jmax (1.0, sequence->getChildByName ("rate") ? sequence->getChildByName ("rate")->getChildElementAllSubText ("timebase", "25").getDoubleValue() : 25.0);
    auto* audio = sequence->getChildByName ("media") ? sequence->getChildByName ("media")->getChildByName ("audio") : nullptr;
    if (! audio) { say ("The XML has no audio in it.", true); return false; }
    std::map<String, String> filePaths;
    int clips = 0;
    const bool imported = apply ("Import XML", [&] (Project& p)
    {
        int index = 0;
        for (auto* track : audio->getChildWithTagNameIterator ("track"))
        {
            const Id t = edit::addTrack (p, TrackKind::Track, "XML " + std::to_string (++index));
            for (auto* item : track->getChildWithTagNameIterator ("clipitem"))
            {
                auto* fileEl = item->getChildByName ("file");
                if (! fileEl) continue;
                const auto id = fileEl->getStringAttribute ("id");
                if (auto* url = fileEl->getChildByName ("pathurl"))
                {
                    auto path = URL::removeEscapeChars (url->getAllSubText().trim().fromFirstOccurrenceOf ("file://", false, true));
                    if (path.startsWith ("localhost")) path = path.fromFirstOccurrenceOf ("localhost", false, false);
                    filePaths[id] = path;
                }
                const auto path = filePaths[id];
                if (path.isEmpty()) continue;
                const double start = item->getChildElementAllSubText ("start", "0").getDoubleValue() / fps;
                const double end = item->getChildElementAllSubText ("end", "0").getDoubleValue() / fps;
                const double in = item->getChildElementAllSubText ("in", "0").getDoubleValue() / fps;
                if (end <= start || start < 0) continue;
                Clip c;
                c.kind = Clip::Kind::Audio;
                c.name = item->getChildElementAllSubText ("name", {}).toStdString();
                c.start = p.tempo.tickAt (start);
                c.length = jmax<Tick> (1, p.tempo.tickAt (end) - c.start);
                c.audio.file = path.toStdString();
                c.audio.sourceStart = in;
                c.audio.sourceLength = end - start;
                double seconds = 0, sr = 0;
                int ch = 0;
                if (AudioCache::describe (File (path), seconds, sr, ch)) { c.audio.fileSampleRate = sr; c.audio.fileChannels = ch; }
                edit::addClip (p, t, c);
                ++clips;
            }
        }
    });
    if (! imported) return false;
    say ("Imported " + String (clips) + " clips from " + f.getFileName() + ".", false, true);
    return clips > 0;
}

bool Controller::exportBatch (const File& folder, const Batch& b)
{
    folder.createDirectory();
    const String song = file.existsAsFile() ? file.getFileNameWithoutExtension() : String ("Mix");
    Engine::RenderOptions o;
    o.start = project.loop.on ? project.tempo.secondsAt (project.loop.start) : 0.0;
    o.end = project.loop.on ? project.tempo.secondsAt (project.loop.end) : project.tempo.secondsAt (project.songEnd());
    o.loudnessTarget = b.loudness;
    o.tail = 0;
    o.ringOut = b.ringOut;
    o.secondPass = b.secondPass;
    bool ok = true;
    int made = 0;
    if (b.mixWav) { o.format = "wav"; ok = exportAudio (folder.getChildFile (song + ".wav"), o, false) && ok; ++made; }
    if (b.mixMp3) { o.format = "mp3"; ok = exportAudio (folder.getChildFile (song + ".mp3"), o, false) && ok; ++made; }
    if (b.stems)
    {
        auto stemsFolder = folder.getChildFile ("Tracks");
        stemsFolder.createDirectory();
        o.format = "wav";
        o.loudnessTarget = 0;
        ok = exportAudio (stemsFolder.getChildFile (song + ".wav"), o, true) && ok;
        ++made;
    }
    if (b.midi) { ok = exportMidi (folder.getChildFile (song + ".mid")) && ok; ++made; }
    if (ok) say ("Exported " + String (made) + (made == 1 ? " thing" : " things") + " into " + folder.getFullPathName() + ".");
    return ok;
}

bool Controller::importVideo (const File& f, Tick at)
{
    if (! f.existsAsFile()) return false;
    const auto path = f.getFullPathName().toStdString();
    apply ("Import Video", [path, at] (Project& p) { p.video.file = path; p.video.start = at; });
    // Its sound, on a track of its own, if the Mac can read it.
    const Id t = addTrack (TrackKind::Track, f.getFileNameWithoutExtension() + " Sound");
    const bool sound = importFile (f, t, at);
    if (! sound) apply ("Remove Empty Track", [t] (Project& p) { edit::removeTrack (p, t); });
    zoneTab = ZoneTab::Video;
    say ("The video starts at the cursor, in the Video tab." + String (sound ? " Its sound is on its own track." : " Its sound couldn't be read: export it as WAV from the video program and import that."), ! sound);
    return true;
}

//==============================================================================
// MusicXML (decision 0057).
bool Controller::exportMusicXml (const File& f)
{
    // The chosen tracks that have MIDI, or every track that has.
    auto hasMidi = [] (const Track& t) { for (auto& cl : t.clips()) if (cl.isMidi()) return true; return false; };
    std::vector<Id> ids;
    for (auto& t : project.tracks)
        if (hasMidi (t) && std::find (selectedTracks.begin(), selectedTracks.end(), t.id) != selectedTracks.end()) ids.push_back (t.id);
    if (ids.size() < 2)
    {
        ids.clear();
        for (auto& t : project.tracks) if (hasMidi (t)) ids.push_back (t.id);
    }
    if (ids.empty()) { say ("There are no MIDI clips to write as notation.", true); return false; }
    if (! f.replaceWithText (String::fromUTF8 (musicxml::write (project, ids).c_str())))
    {
        say ("The MusicXML file couldn't be written.", true);
        return false;
    }
    say ("Exported " + String ((int) ids.size()) + (ids.size() == 1 ? " track" : " tracks") + " as MusicXML: " + f.getFileName()
         + ". Notes are squared to 1/32s for the page.");
    return true;
}

bool Controller::importMusicXml (const File& f)
{
    String text;
    if (f.hasFileExtension ("mxl"))
    {
        // Zipped: the score is the file META-INF/container.xml names, or the first one outside META-INF.
        ZipFile zip (f);
        String inner;
        if (auto* c = zip.getEntry ("META-INF/container.xml"))
            if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*c)); in)
                if (auto xml = parseXML (in->readEntireStreamAsString()))
                    if (auto* rootfiles = xml->getChildByName ("rootfiles"))
                        if (auto* rf = rootfiles->getChildByName ("rootfile")) inner = rf->getStringAttribute ("full-path");
        for (int i = 0; inner.isEmpty() && i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry (i)->filename;
            if (! name.startsWith ("META-INF") && (name.endsWithIgnoreCase (".xml") || name.endsWithIgnoreCase (".musicxml"))) inner = name;
        }
        if (auto* e = zip.getEntry (inner))
            if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*e)); in) text = in->readEntireStreamAsString();
        if (text.isEmpty()) { say ("There's no score inside " + f.getFileName() + ".", true); return false; }
    }
    else text = f.loadFileAsString();

    const auto score = musicxml::read (text.toStdString());
    if (! score.ok) { say (String (score.error), true); return false; }

    // Into an empty song, the score's tempo, time signatures and key come too;
    // into one with music in it, the parts go in at the bar the cursor is in.
    bool empty = true;
    for (auto& t : project.tracks) if (! t.clips().empty()) empty = false;
    const Tick at = empty ? 0 : project.tempo.barStart (project.tempo.barAt (cursor));
    std::vector<Id> made;
    const bool done = apply ("Import MusicXML", [&] (Project& p)
    {
        if (empty)
        {
            if (! score.tempos.empty()) p.tempo.setTempos (score.tempos);
            if (! score.meters.empty()) p.tempo.setMeters (score.meters);
            if (score.key) p.scale = *score.key;
            if (! score.title.empty() && p.metadata.title.empty()) p.metadata.title = score.title;
        }
        Id after = focusTrack;
        for (auto& part : score.parts)
        {
            const Id t = edit::addTrack (p, TrackKind::Track, part.name, after);
            after = t;
            if (auto* tr = p.track (t)) tr->colour = theme::trackColours()[(int) (made.size() + p.tracks.size()) % theme::trackColours().size()].getARGB();
            const Id clip = edit::addMidiClip (p, t, at, std::max<Tick> (score.length, PPQ * 4));
            if (auto* cl = p.clip (clip)) { cl->name = part.name; for (auto n : part.notes) edit::addNote (p, clip, n); }
            made.push_back (t);
        }
    });
    if (! done) return false;
    if (! made.empty()) selectTracks (made);
    say ("Imported " + String ((int) made.size()) + (made.size() == 1 ? " part" : " parts") + " from " + f.getFileName()
         + ", each on a track of its own. Add an instrument to hear them" + (empty ? String (". The score's tempo, time signatures and key came too.") : String (".")), false, true);
    return true;
}

//==============================================================================
// Articulation maps (decision 0072).
File Controller::articulationLibraryFile() const
{
    const auto folder = articulationFolder != File() ? articulationFolder : engine.plugins().listFile().getParentDirectory();
    return folder.getChildFile ("Articulation Maps.json");
}

std::vector<ArticulationMap> Controller::articulationLibrary() const
{
    return articulations::loadLibrary (articulationLibraryFile().loadFileAsString().toStdString());
}

void Controller::saveArticulationLibrary (const std::vector<ArticulationMap>& maps)
{
    const auto f = articulationLibraryFile();
    f.getParentDirectory().createDirectory();
    const auto part = f.getSiblingFile (f.getFileName() + ".part");
    if (part.replaceWithText (String (articulations::saveLibrary (maps))) && part.moveFileTo (f)) return;
    say ("The articulation maps couldn't be saved to " + f.getFullPathName() + ".", true);
}

int Controller::importArticulationMaps (const Array<File>& files)
{
    auto library = articulationLibrary();
    int read = 0;
    StringArray names, problems;
    for (auto& f : files)
    {
        const auto r = articulations::import (f.loadFileAsString().toStdString(), f.getFileName().toStdString());
        if (! r.error.empty()) { problems.add (f.getFileName() + ": " + String (r.error)); continue; }
        for (auto m : r.maps)
        {
            if (m.name.empty()) m.name = f.getFileNameWithoutExtension().toStdString();
            m.id = 0;
            library.erase (std::remove_if (library.begin(), library.end(), [&m] (const ArticulationMap& x) { return x.name == m.name; }), library.end());
            library.push_back (m);
            names.add (String (m.name) + " (" + String ((int) m.articulations.size()) + ")");
            ++read;
        }
    }
    std::sort (library.begin(), library.end(), [] (const ArticulationMap& a, const ArticulationMap& b) { return String (a.name).compareNatural (String (b.name)) < 0; });
    if (read > 0) saveArticulationLibrary (library);
    if (read > 0)
        say ("Imported " + names.joinIntoString (", ") + ". Choose one for a track from the piano roll's Velocity menu"
             + (problems.isEmpty() ? String (".") : ". Not read: " + problems.joinIntoString ("; ")), ! problems.isEmpty());
    else if (! problems.isEmpty())
        say ("Nothing was imported. " + problems.joinIntoString ("; "), true);
    return read;
}

void Controller::useArticulationMap (Id track, const ArticulationMap& map)
{
    apply ("Articulation Map", [&] (Project& p)
    {
        auto* t = p.track (track);
        if (! t) return;
        Id id = 0;
        for (auto& m : p.articulationMaps)
            if (m.name == map.name && m.articulations == map.articulations) id = m.id;
        if (id == 0)
        {
            auto copy = map;
            copy.id = id = p.newId();
            p.articulationMaps.push_back (copy);
        }
        t->articulationMap = id;
    });
}

bool Controller::setArticulationDelay (const std::string& name, const std::set<int>& which, double ms)
{
    ms = jlimit (-1000.0, 1000.0, ms);
    bool found = false;
    auto library = articulationLibrary();
    bool inLibrary = false;
    for (auto& m : library)
        if (m.name == name)
            for (int i : which)
                if (i >= 0 && i < (int) m.articulations.size()) { m.articulations[(size_t) i].delayMs = ms; inLibrary = true; }
    if (inLibrary) { saveArticulationLibrary (library); found = true; }
    bool inSong = false;
    for (auto& m : project.articulationMaps) inSong |= m.name == name;
    if (inSong)
    {
        apply ("Articulation Delay", [&] (Project& p)
        {
            for (auto& m : p.articulationMaps)
                if (m.name == name)
                    for (int i : which)
                        if (i >= 0 && i < (int) m.articulations.size()) m.articulations[(size_t) i].delayMs = ms;
        });
        found = true;
    }
    if (found)
        say (String (which.size() == 1 ? "The articulation" : "The articulations") + " of " + String (name).quoted() + " now play "
             + (ms == 0 ? String ("on time") : ui::number (std::abs (ms), 0) + (ms < 0 ? " ms early" : " ms late")) + ".", false, inSong);
    else sendChangeMessage();
    return found;
}

//==============================================================================
// DAWproject (decision 0077).
bool Controller::exportDawProject (const File& f)
{
    storeDeviceStates();
    const auto w = dawproject::write (project, KALIBRE_VERSION);
    ZipFile::Builder zip;
    auto text = [&zip] (const std::string& s, const String& path)
    {
        zip.addEntry (new MemoryInputStream (MemoryBlock (s.data(), s.size()), true), 9, path, Time::getCurrentTime());
    };
    text (w.projectXml, "project.xml");
    text (w.metadataXml, "metadata.xml");
    int missing = 0;
    for (auto& [path, file] : w.audio)
    {
        Clip probe;
        probe.kind = Clip::Kind::Audio;
        probe.audio.file = file;
        const auto src = fileOf (probe, false);
        if (src.existsAsFile()) zip.addFile (src, 0, path);
        else ++missing;
    }
    if (! w.kalibreAutomation.empty())
    {
        MemoryBlock m (w.kalibreAutomation.data(), w.kalibreAutomation.size());
        zip.addEntry (new MemoryInputStream (m, true), 9, dawproject::kalibreAutomationPath, Time::getCurrentTime());
    }
    for (auto& [path, state] : w.states)
    {
        MemoryBlock m;
        if (m.fromBase64Encoding (state)) zip.addEntry (new MemoryInputStream (m, true), 9, path, Time::getCurrentTime());
    }
    const auto part = f.getSiblingFile (f.getFileName() + ".part");
    part.deleteFile();
    {
        FileOutputStream out (part);
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
        {
            say ("The DAWproject couldn't be written to " + f.getFullPathName() + ".", true);
            return false;
        }
    }
    if (! part.moveFileTo (f)) { say ("The DAWproject couldn't be written to " + f.getFullPathName() + ".", true); return false; }
    say ("Exported " + f.getFileName() + ": the tracks, mix, tempo, markers, clips, audio, automation and warping. Plug-ins go by name; "
         "other programs' plug-in settings aren't carried yet"
         + (missing > 0 ? ". " + String (missing) + " audio files were missing and left out." : String (".")), missing > 0);
    return true;
}

bool Controller::importDawProject (const File& f)
{
    ZipFile zip (f);
    auto entryText = [&zip] (const String& name) -> std::string
    {
        if (auto* e = zip.getEntry (name))
            if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*e)); in)
            {
                MemoryBlock m;
                in->readIntoMemoryBlock (m);
                return m.toString().toStdString();
            }
        return {};
    };
    const auto xml = entryText ("project.xml");
    if (xml.empty()) { say (f.getFileName() + " isn't a DAWproject: it has no project.xml.", true); return false; }
    auto r = dawproject::read (xml, entryText ("metadata.xml"), entryText (dawproject::kalibreAutomationPath));
    if (! r.ok) { say (String (r.error), true); return false; }
    auto& p = r.project;

    // The audio, out of the zip beside it.
    const auto audioFolder = f.getSiblingFile (f.getFileNameWithoutExtension() + " Audio");
    std::map<std::string, std::string> extracted;
    auto fixAudio = [&] (Clip& c)
    {
        if (c.isMidi() || c.audio.file.empty()) return;
        if (auto it = extracted.find (c.audio.file); it != extracted.end()) { c.audio.file = it->second; return; }
        if (auto* e = zip.getEntry (String (c.audio.file)))
        {
            audioFolder.createDirectory();
            auto dest = audioFolder.getChildFile (File::createLegalFileName (String (c.audio.file).fromLastOccurrenceOf ("/", false, false)));
            if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*e)); in)
            {
                dest.deleteFile();
                FileOutputStream out (dest);
                if (out.openedOk()) out.writeFromInputStream (*in, -1);
            }
            // Kept as 32-bit float WAV, as all Kalibre's audio (decision 0079).
            if (! audiofile::isFloatWav (dest))
            {
                const auto wav = dest.getSiblingFile (dest.getFileNameWithoutExtension() + ".wav").getNonexistentSibling (false);
                const auto converted = dest.hasFileExtension ("wav") ? dest.getSiblingFile (dest.getFileNameWithoutExtension() + " float.wav") : wav;
                if (audiofile::convertToFloatWav (dest, converted)) { dest.deleteFile(); dest = converted; }
            }
            extracted[c.audio.file] = dest.getFullPathName().toStdString();
            c.audio.file = dest.getFullPathName().toStdString();
        }
    };
    // Plug-ins by name, as this Mac has them; Kalibre's own states from the file.
    const auto known = engine.plugins().known().getTypes();
    int unmatched = 0;
    auto fixDevices = [&] (Track& t)
    {
        for (auto& d : t.devices)
        {
            auto readState = [&] (const std::string& path)
            {
                if (auto* e = zip.getEntry (String (path)))
                    if (std::unique_ptr<InputStream> in (zip.createStreamForEntry (*e)); in)
                    {
                        MemoryBlock m;
                        in->readIntoMemoryBlock (m);
                        d.state = m.toBase64Encoding().toStdString();
                    }
            };
            if (auto it = r.states.find (d.id); it != r.states.end()) readState (it->second);
            if (auto it = r.kalibreStates.find (d.id); it != r.kalibreStates.end()) readState (it->second);
            if (d.plugin.format == "Kalibre") continue;
            const PluginDescription* best = nullptr;
            for (auto& desc : known)
            {
                if (! desc.name.equalsIgnoreCase (String (d.plugin.name))) continue;
                if (desc.pluginFormatName == String (d.plugin.format)) { best = &desc; break; }
                if (! best) best = &desc;
            }
            if (best) d.plugin = PluginManager::refFor (*best);
            else ++unmatched;
        }
    };
    for (auto& t : p.tracks)
    {
        for (auto& c : t.clips()) fixAudio (c);
        fixDevices (t);
    }
    fixDevices (p.master);
    if (edit::overTrackLimit (p)) { refuseTooManyTracks(); return false; }   // 0088

    engine.stop();
    engine.newSong();
    project = std::move (p);
    beginWorkClock();
    refileEffects (project, engine.plugins());
    history.clear();
    file = File();
    dirty = true;
    focusTrack = project.tracks.empty() ? 0 : project.tracks.front().id;
    selectedTracks.clear();
    if (focusTrack) selectedTracks.push_back (focusTrack);
    selectedClip = 0;
    selectedClips.clear();
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    String msg = "Opened " + f.getFileName() + " as a new song (save it to keep it).";
    if (unmatched > 0) msg << " " << unmatched << (unmatched == 1 ? " plug-in isn't" : " plug-ins aren't") << " on this Mac.";
    if (! r.leftOut.empty())
    {
        StringArray left;
        for (auto& s : r.leftOut) left.add (String (s));
        msg << " Not carried: " << left.joinIntoString ("; ") << ".";
    }
    say (msg, unmatched > 0);
    sendChangeMessage();
    return true;
}

} // namespace kal
