#include "Controller.h"

#include "Capture.h"
#include "Comp.h"
#include "MidiLearn.h"
#include "Mpe.h"
#include "Harmony.h"
#include "LuaHost.h"
#include "MorphProcessor.h"
#include "ProjectFile.h"
#include "Song.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

Controller::Controller (Engine& e) : engine (e)
{
    engine.onTouched = [this] (const Engine::Touch& t) { touched (t); };
    engine.onMessage = [this] (const String& m) { say (m, true); };
    engine.cache.onReady = [this] { syncEngine(); sendChangeMessage(); };
    engine.onAudioReady = [this] { syncEngine(); sendChangeMessage(); };
    engine.onLearned = [this] (int channel, int controller)
    {
        if (! lastTouched.valid()) return;
        auto lt = lastTouched;
        AutoTarget target;
        target.kind = lt.kind;
        target.device = lt.device;
        target.param = lt.param;
        target.paramName = lt.name.toStdString();
        apply ("MIDI Learn", [=] (Project& p) { midimap::learn (p, channel, controller, lt.track, target, lt.name.toStdString()); });
        say ((controller == midimap::pitchBend ? String ("Pitch bend") : "Controller " + String (controller)) + " on channel "
             + String (channel) + " now moves " + lt.name + ".", false, true);
    };
    engine.onMapped = [this] (const MidiMapping& m, double v) { mapped (m, v); };
    // Another machine, or Link, moving the transport and the tempo (decision 0034).
    engine.syncDevices.onCommand = [this] (const SyncDevices::Command& cmd)
    {
        using K = SyncDevices::Command::Kind;
        switch (cmd.kind)
        {
            case K::Tempo:
                // Followed only while the song has one tempo; a tempo map is the song's own.
                if (project.tempo.tempos().size() == 1 && std::abs (project.tempo.bpmAt (0) - cmd.value) > 0.005)
                {
                    project.tempo.setTempoAt (0, cmd.value);
                    refresh();
                }
                break;
            case K::Play:
                if (cmd.value >= 0) locate (project.tempo.tickAt (project.tempo.secondsAt ((Tick) std::llround (cmd.value * PPQ))));
                play();
                break;
            case K::Stop: if (engine.isPlaying()) stop(); break;
            case K::Locate: if (! engine.isPlaying()) locate ((Tick) std::llround (cmd.value * PPQ)); break;
            case K::LocateSeconds: locate (project.tempo.tickAt (std::max (0.0, cmd.value))); break;
            case K::Record: if (! engine.isRecording()) record(); break;
            case K::Chase:
                // Timecode coming in (decision 0043): start there, and keep within a tenth of a second.
                if (! engine.isPlaying()) { locate (project.tempo.tickAt (std::max (0.0, cmd.value))); play(); }
                else if (std::abs (engine.position() - cmd.value) > 0.1) engine.locate (std::max (0.0, cmd.value));
                break;
        }
    };
    if (auto loaded = loadSettings (studioFile().loadFileAsString().toStdString())) studio = *loaded;
    engine.syncDevices.configure (studio);
    engine.onDevicePluginChanged = [this] { dirty = true; };
    MorphProcessor::choosePlugin = nullptr;
    newProject();
    startTimerHz (30);
}

Controller::~Controller()
{
    stopTimer();
    engine.cache.onReady = nullptr;
    engine.onTouched = nullptr;
    engine.onMessage = nullptr;
}

//==============================================================================
void Controller::syncEngine()
{
    lastMix = song::mixOf (project);
    engine.setMappings (project.midiMappings);
    engine.sync (project, focusTrack, projectFolder());
    const double ls = project.tempo.secondsAt (project.loop.start), le = project.tempo.secondsAt (project.loop.end);
    engine.setLoop (project.loop.on, ls, le);
    engine.setCountIn (project.metronome.countIn ? project.tempo.secondsAt (project.tempo.barStart (project.metronome.countInBars)) : 0.0);
}

void Controller::apply (const String& label, std::function<void (Project&)> change)
{
    storeDeviceStates();
    Project before = project;
    change (project);
    // Linked clips and grouped tracks follow (decision 0037).
    song::propagateLinks (before, project);
    song::propagateGroups (song::mixOf (before), project);
    if (project == before) return;
    history.push (before, label.toStdString());
    dirty = true;
    // Choices that point at things no longer there are let go.
    if (! project.clip (selectedClip)) selectedClip = 0;
    for (auto it = selectedClips.begin(); it != selectedClips.end();)
        it = project.clip (*it) ? std::next (it) : selectedClips.erase (it);
    selectedTracks.erase (std::remove_if (selectedTracks.begin(), selectedTracks.end(),
                                          [this] (Id id) { return ! project.track (id); }),
                          selectedTracks.end());
    if (! project.track (focusTrack)) focusTrack = project.tracks.empty() ? 0 : project.tracks.front().id;
    syncEngine();
    sendChangeMessage();
}

std::map<Id, int> Controller::deviceStates() const
{
    std::map<Id, int> out;
    for (auto& t : project.tracks) for (auto& d : t.devices) out[d.id] = d.stateVersion;
    for (auto& d : project.master.devices) out[d.id] = d.stateVersion;
    return out;
}

// Undo and redo put a plug-in's settings back when they were what changed
// (a preset, A/B); a plug-in whose settings didn't change is left alone.
void Controller::reloadChangedStates (const std::map<Id, int>& was)
{
    auto reload = [this, &was] (const Track& t)
    {
        for (auto& d : t.devices)
        {
            auto it = was.find (d.id);
            if (it == was.end() || it->second == d.stateVersion || d.state.empty()) continue;
            if (auto* inst = engine.instance (d.id))
            {
                MemoryBlock m;
                if (m.fromBase64Encoding (d.state)) inst->setStateInformation (m.getData(), (int) m.getSize());
            }
        }
    };
    for (auto& t : project.tracks) reload (t);
    reload (project.master);
}

// What the launcher is playing isn't part of the song's history (decision 0037).
std::map<Id, Track::Launch> Controller::launchStates() const
{
    std::map<Id, Track::Launch> out;
    for (auto& t : project.tracks) out[t.id] = t.launch;
    return out;
}

void Controller::restoreLaunches (const std::map<Id, Track::Launch>& launches)
{
    for (auto& t : project.tracks)
        if (auto it = launches.find (t.id); it != launches.end()) t.launch = it->second;
}

void Controller::launchSlot (Id track, int slot)
{
    // Stopped, it starts at once, from the bar the cursor is in.
    if (! engine.isPlaying()) cursor = project.tempo.barStart (project.tempo.barAt (cursor));
    song::launch (project, track, slot, engine.isPlaying() ? playhead() : cursor);
    refresh();
    if (! engine.isPlaying()) play();
}

void Controller::launchScene (int scene)
{
    if (! engine.isPlaying()) cursor = project.tempo.barStart (project.tempo.barAt (cursor));
    song::launchScene (project, scene, engine.isPlaying() ? playhead() : cursor);
    refresh();
    if (! engine.isPlaying()) play();
}

void Controller::backToArrangement()
{
    song::backToArrangement (project);
    refresh();
}

void Controller::refresh()
{
    song::propagateGroups (lastMix, project);
    dirty = true;
    syncEngine();
    sendChangeMessage();
}

bool Controller::undo()
{
    storeDeviceStates();
    const auto label = history.undoLabel();
    const auto launches = launchStates();
    const auto states = deviceStates();
    if (! history.undo (project)) return false;
    restoreLaunches (launches);
    dirty = true;
    syncEngine();
    reloadChangedStates (states);
    say ("Undid " + String (label) + ".");
    return true;
}

bool Controller::redo()
{
    storeDeviceStates();
    const auto label = history.redoLabel();
    const auto launches = launchStates();
    const auto states = deviceStates();
    if (! history.redo (project)) return false;
    restoreLaunches (launches);
    dirty = true;
    syncEngine();
    reloadChangedStates (states);
    say ("Redid " + String (label) + ".");
    return true;
}

void Controller::say (const String& m, bool warning, bool canUndo)
{
    message = m;
    messageIsWarning = warning;
    messageCanUndo = canUndo;
    sendChangeMessage();
}

void Controller::scanPlugins (std::vector<std::pair<String, String>> files)
{
    auto& pm = engine.plugins();
    if (pm.scanning()) { say ("Plug-ins are being scanned already."); return; }
    say ("Scanning plug-ins" + ui::u8 ("\xe2\x80\xa6"));
    auto progress = [this] (float p, const String& what)
    {
        if (p >= 1.0f) say ("Scan finished: " + what + ".", what.contains ("Turned off") || what.contains ("couldn't"));
        else say ("Scanning plug-ins, " + String (roundToInt (p * 100)) + "%: " + what);
    };
    if (files.empty()) pm.scan (progress);
    else pm.rescan (files, progress);
}

void Controller::dismiss()
{
    message.clear();
    messageCanUndo = false;
    sendChangeMessage();
}

//==============================================================================
void Controller::focusOn (Id track)
{
    if (focusTrack == track) return;
    focusTrack = track;
    if (std::find (selectedTracks.begin(), selectedTracks.end(), track) == selectedTracks.end())
        selectedTracks = { track };
    syncEngine();   // the live MIDI follows the focus
    sendChangeMessage();
}

void Controller::selectClip (Id id, bool openEditor)
{
    selectedClip = id;
    selectedClips = { id };
    selectedNotes.clear();
    const Track* owner = nullptr;
    if (project.clip (id, &owner) && owner) focusOn (owner->id);
    if (openEditor && id != 0) zoneTab = ZoneTab::Editor;
    sendChangeMessage();
}

void Controller::selectTracks (std::vector<Id> ids)
{
    selectedTracks = std::move (ids);
    if (! selectedTracks.empty()) focusOn (selectedTracks.back());
    sendChangeMessage();
}

//==============================================================================
void Controller::togglePlay() { if (engine.isPlaying()) stop(); else play(); }

void Controller::play()
{
    if (engine.isPlaying()) return;
    engine.locate (project.tempo.secondsAt (cursor));
    engine.play();
    if (onTransportChanged) onTransportChanged();
    sendChangeMessage();
}

void Controller::stop()
{
    finishWriting();   // Latch and Write hold until playing stops
    const bool wasPlaying = engine.isPlaying();
    engine.stop();
    if (wasRecording) finishRecording();
    // Stopping twice goes back to the start.
    if (! wasPlaying) cursor = 0;
    engine.locate (project.tempo.secondsAt (cursor));
    if (onTransportChanged) onTransportChanged();
    sendChangeMessage();
}

void Controller::record()
{
    if (wasRecording) { stop(); return; }
    std::vector<Id> armed;
    for (auto& t : project.tracks) if (t.armed) armed.push_back (t.id);
    if (armed.empty() && focus() && focus()->holdsClips())
    {
        // Recording with nothing armed records onto the track being played.
        apply ("Arm Track", [id = focusTrack] (Project& p) { if (auto* t = p.track (id)) t->armed = true; });
        armed.push_back (focusTrack);
    }
    if (armed.empty()) { say ("Choose a track to record onto first.", true); return; }

    Tick from = cursor;
    if (project.punch.on) from = project.punch.start;
    recordFromSeconds = project.tempo.secondsAt (from);
    const auto folder = recordingsFolder();
    for (auto id : armed)
        if (auto* s = engine.recordSource (id))
        {
            const auto* t = project.track (id);
            // A Summing track records the master (decision 0049); others their input.
            if (t && (t->kind == TrackKind::Summing || t->input.audioChannel >= 0))
                s->startRecording (folder.getNonexistentChildFile (String (t->name).replaceCharacters ("/\\:", "---") + " "
                                                                   + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav"),
                                   engine.sampleRate());
        }
    wasRecording = true;
    engine.setRecording (true);
    if (project.punch.on)
        cursor = std::max<Tick> (0, project.punch.start - project.tempo.barStart (project.metronome.preRollBars) );
    play();
}

void Controller::finishRecording()
{
    wasRecording = false;
    engine.setRecording (false);
    // What each armed track recorded: one clip, or a take per pass of the loop (decision 0029).
    struct Recorded { Id track; std::vector<Clip> passes; };
    std::vector<Recorded> made;
    const bool looping = project.loop.on && ! project.punch.on && project.loop.end > project.loop.start;
    for (auto& t : project.tracks)
    {
        if (! t.armed) continue;
        auto* s = engine.source (t.id);
        if (! s) continue;
        // Audio: a Summing track's is the master's (decision 0049).
        if (auto* rs = engine.recordSource (t.id); rs && rs->isRecording())
        {
            auto* s = rs;
            const double startSeconds = s->recordStartSeconds();
            const auto f = s->stopRecording();
            double seconds = 0, sr = 0;
            int channels = 0;
            if (startSeconds >= 0 && AudioCache::describe (f, seconds, sr, channels) && seconds > 0.05)
            {
                Clip c;
                c.kind = Clip::Kind::Audio;
                c.name = t.name;
                c.start = project.tempo.tickAt (startSeconds);
                c.length = std::max<Tick> (1, project.tempo.tickAt (startSeconds + seconds) - c.start);
                c.audio.file = f.getFullPathName().toStdString();
                c.audio.sourceLength = seconds;
                c.audio.fileSampleRate = sr;
                c.audio.fileChannels = channels;
                // A Summing track's first moments belong before where recording began
                // (the master arrives after the graph's delay): they're left out (decision 0049).
                if (const double lead = s->recordLatencySeconds.load(); lead > 0 && lead < seconds)
                {
                    c.audio.sourceStart = lead;
                    c.start = project.tempo.tickAt (startSeconds + lead);
                    c.length = std::max<Tick> (1, project.tempo.tickAt (startSeconds + seconds) - c.start);
                }
                if (project.punch.on)
                {
                    // Only what fell inside the punch range is kept.
                    const Tick a = std::max (c.start, project.punch.start), b = std::min (c.end(), project.punch.end);
                    if (b <= a) continue;
                    c.audio.sourceStart = project.tempo.secondsAt (a) - startSeconds;
                    c.start = a;
                    c.length = b - a;
                    c.offset = 0;
                }
                made.push_back ({ t.id, looping ? comp::audioPasses (c, seconds, project.tempo, project.loop.start, project.loop.end)
                                                : std::vector<Clip> { c } });
            }
            else f.deleteFile();
        }
        if (t.kind == TrackKind::Summing) continue;   // it records sound only
        // MIDI played in.
        std::vector<capture::Event> events;
        int s1, n1, s2, n2;
        s->playedFifo.prepareToRead (s->playedFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1 + n2; ++i)
        {
            const auto& p = s->played[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
            capture::Event e;
            e.songTicks = p.ticks;
            e.wallSeconds = project.tempo.secondsAt ((Tick) p.ticks);
            std::memcpy (e.bytes, p.bytes, 3);
            events.push_back (e);
        }
        s->playedFifo.finishedRead (n1 + n2);
        if (events.empty()) continue;
        Recorded midi { t.id, {} };
        for (auto& pass : looping ? comp::midiPasses (events) : std::vector<std::vector<capture::Event>> { events })
        {
            const auto r = capture::lastPerformance (pass, project.tempo, cursor, 1.0e9);
            if (r.notes.empty()) continue;
            Clip c;
            c.kind = Clip::Kind::Midi;
            c.name = t.name;
            c.start = looping ? project.loop.start : r.start;
            c.length = looping ? project.loop.end - project.loop.start : r.length;
            c.midi.notes = r.notes;
            c.midi.events = r.events;
            if (t.mpe) mpe::gatherExpression (c.midi);   // each note's own bends and pressure (decision 0042)
            if (looping)
            {
                // Each pass is placed on the loop.
                for (auto& n : c.midi.notes) n.start += r.start - project.loop.start;
                for (auto& e : c.midi.events) e.tick += r.start - project.loop.start;
            }
            if (project.punch.on)
            {
                auto& n = c.midi.notes;
                n.erase (std::remove_if (n.begin(), n.end(), [&] (const Note& x)
                {
                    const Tick at = c.start + x.start;
                    return at < project.punch.start || at >= project.punch.end;
                }), n.end());
            }
            if (! c.midi.notes.empty()) midi.passes.push_back (c);
        }
        if (! midi.passes.empty()) made.push_back (midi);
    }
    if (made.empty()) return;
    int takes = 0;
    apply ("Record", [&made, &takes] (Project& p)
    {
        for (auto& rec : made)
        {
            auto* t = p.track (rec.track);
            if (! t) continue;
            auto& v = t->version();
            const bool audio = ! rec.passes.front().isMidi();
            const Tick a = rec.passes.front().start, b = rec.passes.back().end();
            // Recording audio over audio, or going round a loop, makes takes to comp from.
            bool overlaps = false;
            for (auto& c : v.clips)
                if (c.fromTake == 0 && ! c.isMidi() && audio && c.end() > a && c.start < std::max (b, rec.passes.front().end())) overlaps = true;
            const bool asTakes = rec.passes.size() > 1 || ! v.takes.empty() || overlaps;
            if (! asTakes)
            {
                edit::addClip (p, rec.track, rec.passes.front());
                continue;
            }
            if (v.takes.empty() && overlaps)
            {
                // What was there becomes the first take.
                std::vector<Clip> before;
                for (auto& c : v.clips) if (c.fromTake == 0 && ! c.isMidi() && c.end() > a && c.start < b) before.push_back (c);
                v.clips.erase (std::remove_if (v.clips.begin(), v.clips.end(), [&] (const Clip& c)
                {
                    return c.fromTake == 0 && ! c.isMidi() && c.end() > a && c.start < b;
                }), v.clips.end());
                comp::addTake (p, rec.track, before);
                ++takes;
            }
            for (auto& pass : rec.passes) { comp::addTake (p, rec.track, { pass }); ++takes; }
            v.takesShown = true;
        }
    });
    if (takes > 0) say ("Recorded " + String (takes) + " takes. The newest plays; drag across another take's lane to use it there instead.", false, true);
    else say ("Recorded " + String ((int) made.size()) + (made.size() == 1 ? " take." : " takes."), false, true);
}

void Controller::locate (Tick t)
{
    cursor = std::max<Tick> (0, t);
    engine.locate (project.tempo.secondsAt (cursor));
    sendChangeMessage();
}

void Controller::goToStart() { locate (0); }

void Controller::toggleLoop()
{
    apply (project.loop.on ? "Loop Off" : "Loop On", [] (Project& p) { p.loop.on = ! p.loop.on; });
}

Tick Controller::playhead() const
{
    return engine.isPlaying() ? project.tempo.tickAt (const_cast<Engine&> (engine).heardPosition()) : cursor;
}

void Controller::tapTempo()
{
    const double now = Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 8) taps.erase (taps.begin());
    if (taps.size() < 3) { say ("Keep tapping."); return; }
    const double beat = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    const double bpm = std::round (60.0 / beat * 10.0) / 10.0;
    apply ("Tap Tempo", [bpm] (Project& p) { p.tempo.setTempoAt (0, bpm); });
    say ("Tempo set to " + String (bpm, 1) + " BPM.");
}

//==============================================================================
void Controller::newProject()
{
    engine.stop();
    project = Project();
    history.clear();
    file = File();
    dirty = false;
    const Id t = edit::addTrack (project, TrackKind::Track, "Track 1");
    focusTrack = t;
    selectedTracks = { t };
    selectedClip = 0;
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    sendChangeMessage();
}

File Controller::projectFolder() const
{
    return file.existsAsFile() ? file.getParentDirectory()
                               : File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Kalibre");
}

File Controller::studioFile() const
{
    return scriptsFolder().getParentDirectory().getChildFile ("Studio.json");
}

void Controller::setStudio (const ControlSettings& s)
{
    studio = s;
    studioFile().replaceWithText (String (saveSettings (s)));
    engine.syncDevices.configure (studio);
    if (onStudioChanged) onStudioChanged();
    sendChangeMessage();
}

File Controller::autosaveFile() const
{
    auto folder = scriptsFolder().getParentDirectory().getChildFile ("Autosave");
    folder.createDirectory();
    return folder.getChildFile ((file.existsAsFile() ? file.getFileNameWithoutExtension() : String ("Untitled")) + ".kalibre");
}

void Controller::autosave()
{
    if (! dirty || project.tracks.empty()) return;
    storeDeviceStates();
    autosaveFile().replaceWithText (String (file::save (project)));
}

File Controller::recordingsFolder() const
{
    auto f = projectFolder().getChildFile ("Audio");
    f.createDirectory();
    return f;
}

bool Controller::open (const File& f)
{
    std::string error;
    auto loaded = file::load (f.loadFileAsString().toStdString(), &error);
    if (! loaded) { say (String (error), true); return false; }
    engine.stop();
    project = std::move (*loaded);
    history.clear();
    file = f;
    dirty = false;
    focusTrack = project.tracks.empty() ? 0 : project.tracks.front().id;
    selectedTracks.clear();
    if (focusTrack) selectedTracks.push_back (focusTrack);
    selectedClip = 0;
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    say ("Opened " + f.getFileNameWithoutExtension() + ".");
    // Files that have moved since (decision 0041).
    if (const auto missing = missingFiles(); ! missing.empty())
        say ("Opened " + f.getFileNameWithoutExtension() + ". " + String ((int) missing.size()) + (missing.size() == 1 ? " audio file is" : " audio files are")
             + " missing: File > Relink Missing Files finds them in a folder.", true);
    return true;
}

void Controller::storeDeviceStates()
{
    auto store = [this] (Track& t)
    {
        for (auto& d : t.devices)
            if (d.active)
            {
                auto s = engine.stateOf (d.id);
                if (! s.empty()) d.state = s;
                auto a = engine.araStateOf (d.id);
                if (! a.empty()) d.araState = a;
            }
        // The other versions' plug-ins, waiting with their own settings (decision 0050).
        for (size_t v = 0; v < t.versions.size(); ++v)
            if ((int) v != t.activeVersion)
                for (auto& d : t.versions[v].devices)
                    if (d.active)
                    {
                        auto s = engine.stateOf (d.id, t.versions[v].id);
                        if (! s.empty()) d.state = s;
                    }
    };
    for (auto& t : project.tracks) store (t);
    store (project.master);
    for (auto& t : project.cue) store (t);
}

bool Controller::save()
{
    if (! file.getFullPathName().endsWith (".kalibre")) return false;
    storeDeviceStates();
    project.name = file.getFileNameWithoutExtension().toStdString();
    if (! file.replaceWithText (String (file::save (project))))
    {
        say ("The project couldn't be saved. Check the folder can be written to.", true);
        return false;
    }
    dirty = false;
    say ("Saved " + file.getFileNameWithoutExtension() + ".");
    return true;
}

bool Controller::saveAs (const File& f)
{
    file = f.withFileExtension (".kalibre");
    return save();
}

bool Controller::importFile (const File& f, Id trackId, Tick at)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi")
    {
        FileInputStream in (f);
        MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) { say ("That MIDI file couldn't be read.", true); return false; }
        const int fileTpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
        apply ("Import MIDI", [&] (Project& p)
        {
            Id target = trackId;
            for (int i = 0; i < mf.getNumTracks(); ++i)
            {
                auto seq = *mf.getTrack (i);
                seq.updateMatchedPairs();
                Clip c;
                c.kind = Clip::Kind::Midi;
                c.name = f.getFileNameWithoutExtension().toStdString();
                c.start = at;
                for (int e = 0; e < seq.getNumEvents(); ++e)
                {
                    const auto& m = seq.getEventPointer (e)->message;
                    const Tick t = (Tick) std::llround (m.getTimeStamp() * PPQ / fileTpq);
                    if (m.isNoteOn())
                    {
                        const auto offIndex = seq.getIndexOfMatchingKeyUp (e);
                        const Tick end = offIndex >= 0 ? (Tick) std::llround (seq.getEventPointer (offIndex)->message.getTimeStamp() * PPQ / fileTpq) : t + PPQ;
                        c.midi.notes.push_back ({ 0, t, std::max<Tick> (1, end - t), m.getNoteNumber(), m.getVelocity(), m.getChannel() - 1 });
                    }
                    else if (m.isController())
                        c.midi.events.push_back ({ t, EventType::Controller, m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue() });
                    else if (m.isPitchWheel())
                        c.midi.events.push_back ({ t, EventType::PitchBend, m.getChannel() - 1, 0, m.getPitchWheelValue() });
                    else if (m.isTempoMetaEvent() && i == 0 && at == 0)
                        p.tempo.setTempoAt (t, 60.0 / m.getTempoSecondsPerQuarterNote());
                    else if (m.isTimeSignatureMetaEvent() && i == 0 && at == 0)
                    {
                        int num = 4, den = 4;
                        m.getTimeSignatureInfo (num, den);
                        p.tempo.setMeterAt (p.tempo.barAt (t), num, den);
                    }
                }
                if (c.midi.notes.empty()) continue;
                Tick end = 0;
                for (auto& n : c.midi.notes) end = std::max (end, n.end());
                c.length = p.tempo.barStart (p.tempo.barAt (at + end - 1) + 1) - at;
                edit::sortNotes (c.midi);
                if (! p.track (target) || ! p.track (target)->holdsClips())
                    target = edit::addTrack (p, TrackKind::Track, f.getFileNameWithoutExtension().toStdString());
                edit::addClip (p, target, c);
                target = 0;   // each further MIDI track on a track of its own
            }
        });
        return true;
    }
    double seconds = 0, sr = 0;
    int channels = 0;
    if (! AudioCache::describe (f, seconds, sr, channels)) { say ("Kalibre can't read that file.", true); return false; }
    apply ("Import Audio", [&] (Project& p)
    {
        Id target = trackId;
        if (! p.track (target) || ! p.track (target)->holdsClips())
            target = edit::addTrack (p, TrackKind::Track, f.getFileNameWithoutExtension().toStdString());
        Clip c;
        c.kind = Clip::Kind::Audio;
        c.name = f.getFileNameWithoutExtension().toStdString();
        c.start = at;
        c.length = std::max<Tick> (1, p.tempo.tickAt (p.tempo.secondsAt (at) + seconds) - at);
        c.audio.file = f.getFullPathName().toStdString();
        c.audio.sourceLength = seconds;
        c.audio.fileSampleRate = sr;
        c.audio.fileChannels = channels;
        // A loop named with its tempo ("Drums 96bpm") is warped to the project.
        const auto name = f.getFileNameWithoutExtension().toLowerCase();
        const auto bpmAt = name.indexOf ("bpm");
        if (bpmAt > 0)
        {
            int s = bpmAt;
            while (s > 0 && (CharacterFunctions::isDigit (name[s - 1]) || name[s - 1] == '.')) --s;
            const auto bpm = name.substring (s, bpmAt).getDoubleValue();
            if (bpm >= 40 && bpm <= 300)
            {
                c.audio.sourceBpm = bpm;
                c.audio.warp = WarpMode::Spectral;
                c.length = (Tick) std::llround (seconds * bpm / 60.0 * PPQ);
            }
        }
        edit::addClip (p, target, c);
    });
    return true;
}

bool Controller::exportMidi (const File& f)
{
    MidiFile mf;
    mf.setTicksPerQuarterNote ((int) PPQ);
    MidiMessageSequence conductor;
    for (auto& tp : project.tempo.tempos())
        conductor.addEvent (MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / tp.bpm)), (double) tp.tick);
    for (auto& m : project.tempo.meters())
        conductor.addEvent (MidiMessage::timeSignatureMetaEvent (m.numerator, m.denominator), (double) project.tempo.barStart (m.bar));
    mf.addTrack (conductor);
    for (auto& t : project.tracks)
    {
        MidiMessageSequence seq;
        seq.addEvent (MidiMessage::textMetaEvent (3, t.name));
        for (auto& c : t.clips())
        {
            if (! c.isMidi() || c.muted) continue;
            const Tick content = c.loopLength > 0 ? c.loopLength : std::numeric_limits<Tick>::max() / 4;
            for (Tick base = c.start - c.offset; base < c.end(); base += content)
            {
                for (auto& n : c.midi.notes)
                {
                    const Tick a = base + n.start;
                    if (a < c.start || a >= c.end()) continue;
                    const Tick b = std::min (a + n.length, c.end());
                    seq.addEvent (MidiMessage::noteOn (n.channel + 1, n.pitch, (uint8) n.velocity), (double) a);
                    seq.addEvent (MidiMessage::noteOff (n.channel + 1, n.pitch), (double) b);
                }
                if (c.loopLength <= 0) break;
            }
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    f.deleteFile();
    FileOutputStream out (f);
    if (! out.openedOk() || ! mf.writeTo (out)) { say ("The MIDI file couldn't be written.", true); return false; }
    say ("Exported " + f.getFileName() + ".");
    return true;
}

bool Controller::exportAudio (const File& f, const Engine::RenderOptions& given, bool stems)
{
    auto options = given;
    options.metadata = project.metadata;   // the song's details go into the file (decision 0041)
    if (options.metadata.title.empty() && file.existsAsFile()) options.metadata.title = file.getFileNameWithoutExtension().toStdString();
    storeDeviceStates();
    String error;
    bool ok = true;
    if (! stems)
        ok = engine.render (project, projectFolder(), f, options, {}, error);
    else
    {
        // A file per track, each soloed in turn.
        for (auto& t : project.tracks)
        {
            if (t.isFolder() && ! project.childrenOf (t.id).empty()) continue;
            Project copy = project;
            for (auto& x : copy.tracks) x.solo = x.id == t.id;
            const auto out = f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " - "
                                                                  + String (t.name).replaceCharacters ("/\\:", "---"))
                                .withFileExtension (f.getFileExtension());
            ok = engine.render (copy, projectFolder(), out, options, {}, error) && ok;
        }
    }
    syncEngine();
    if (ok && options.loudnessTarget < 0 && ! stems)
        say ("Exported " + f.getFileName() + " at " + String (engine.lastExportLoudness, 1) + " LUFS"
             + (engine.lastExportLoudness > options.loudnessTarget + 0.2 || engine.lastExportLoudness < options.loudnessTarget - 0.2
                    ? " (held back so its peaks stay under " + String (options.truePeakCeiling, 0) + " dBTP)." : "."));
    else if (ok) say (stems ? "Exported every track to " + f.getParentDirectory().getFileName() + "." : "Exported " + f.getFileName() + ".");
    else say ("The export didn't finish. " + error, true);
    return ok;
}

//==============================================================================
Id Controller::addTrack (TrackKind kind, const String& name)
{
    Id made = 0;
    const Id after = focusTrack;
    Id parent = 0;
    if (auto* f = focus()) parent = f->isFolder() ? f->id : f->parent;
    auto label = name.isNotEmpty() ? name
               : kind == TrackKind::Folder ? String ("Folder")
               : kind == TrackKind::Summing ? String ("Summing")
               : kind == TrackKind::Cue ? String ("Cue")
               : kind == TrackKind::Return ? String ("Return " + String::charToString ((juce_wchar) ('A' + (int) std::count_if (project.tracks.begin(), project.tracks.end(), [] (const Track& t) { return t.kind == TrackKind::Return; }))))
                                           : String ("Track " + String ((int) project.tracks.size() + 1));
    apply ("Add Track", [&] (Project& p)
    {
        made = edit::addTrack (p, kind, label.toStdString(), kind == TrackKind::Return ? 0 : after,
                               kind == TrackKind::Return ? 0 : parent);
        auto& colours = theme::trackColours();
        if (auto* t = p.track (made); t && ! t->isCue()) t->colour = colours[(int) p.tracks.size() % colours.size()].getARGB();
    });
    if (made) { selectedTracks = { made }; focusOn (made); }
    if (kind == TrackKind::Summing)
        say ("A Summing track records what the master plays. Arm it and record: the mix goes onto it from where recording starts.");
    if (kind == TrackKind::Cue)
    {
        // It's in the mixer, after the master (decision 0051).
        zoneTab = ZoneTab::Mixer;
        say ("The Cue track is after the master in the mixer. Its plug-ins, pan and mono are heard but never exported.");
        viewChanged();
    }
    return made;
}

void Controller::deleteSelectedTracks()
{
    if (selectedTracks.empty()) return;
    auto ids = selectedTracks;
    apply (ids.size() == 1 ? "Delete Track" : "Delete Tracks", [ids] (Project& p) { for (auto id : ids) edit::removeTrack (p, id); });
    say (ids.size() == 1 ? "Deleted the track." : "Deleted " + String ((int) ids.size()) + " tracks.", false, true);
}

void Controller::duplicateSelectedTracks()
{
    auto ids = selectedTracks;
    apply ("Duplicate Track", [ids] (Project& p) { for (auto id : ids) edit::duplicateTrack (p, id); });
}

void Controller::folderFromSelection()
{
    auto ids = selectedTracks;
    if (ids.empty() && focusTrack) ids = { focusTrack };
    Id made = 0;
    apply ("Put in Folder", [&] (Project& p) { made = edit::wrapInFolder (p, ids, "Folder"); });
    if (made == 0) say ("Tracks go into a folder together only when they're at the same level.", true);
    else focusOn (made);
}

void Controller::freeze (Id id, bool on)
{
    auto* t = project.track (id);
    if (! t || ! t->holdsClips()) return;
    if (! on)
    {
        apply ("Unfreeze", [id] (Project& p) { if (auto* x = p.track (id)) x->frozen = false; });
        say ("Unfrozen: its plug-ins are loaded again.");
        return;
    }
    storeDeviceStates();
    // The track alone, through its own plug-ins, at unity: what it puts into its fader.
    Project solo = project;
    Track only = *solo.track (id);
    only.parent = 0;
    only.output = 0;
    only.sends.clear();
    only.volumeDb = 0;
    only.pan = 0;
    only.mute = only.solo = false;
    only.automation.erase (std::remove_if (only.automation.begin(), only.automation.end(), [] (const AutomationLane& a)
    {
        return a.target.kind == AutoTarget::Kind::Volume || a.target.kind == AutoTarget::Kind::Pan;
    }), only.automation.end());
    solo.tracks = { only };
    solo.master.devices.clear();
    solo.master.automation.clear();
    solo.master.volumeDb = 0;
    solo.metronome.on = false;
    Tick end = 0;
    for (auto& c : only.clips()) end = std::max (end, c.end());
    Engine::RenderOptions o;
    o.start = 0;
    o.end = project.tempo.secondsAt (end);
    o.tail = 4.0;
    o.bitDepth = 32;
    o.dither = false;
    const auto out = recordingsFolder().getNonexistentChildFile (String (t->name) + " frozen", ".wav");
    String error;
    if (! engine.render (solo, projectFolder(), out, o, {}, error)) { say ("The track couldn't be frozen. " + error, true); syncEngine(); return; }
    const auto path = out.getFullPathName().toStdString();
    apply ("Freeze", [id, path] (Project& p) { if (auto* x = p.track (id)) { x->frozen = true; x->frozenFile = path; } });
    say ("Frozen: it plays as audio and its plug-ins are unloaded. Unfreeze to change it.", false, true);
}

void Controller::newVersion (Id track)
{
    const auto* t = project.track (track);
    if (t && (int) t->versions.size() >= maxVersions)
    {
        say ("A track has four versions at most. Delete one to make another.", true);
        return;
    }
    // The copy starts with the plug-ins' settings as they are now.
    storeDeviceStates();
    int index = -1;
    apply ("New Version", [&] (Project& p) { index = edit::newVersion (p, track); });
    if (index >= 0) say ("Version " + String (index + 1) + " made, with its own copy of the plug-ins. The others are kept: "
                         "choose one with the 1 to 4 buttons in the mixer, or the track's version number.");
}

void Controller::chooseVersion (const std::vector<Id>& ids, int index)
{
    std::vector<Id> changing, missing;
    for (auto id : ids)
        if (const auto* t = project.track (id); t && t->holdsClips())
        {
            if (index >= (int) t->versions.size()) missing.push_back (id);
            else if (t->activeVersion != index) changing.push_back (id);
        }
    if (! changing.empty())
    {
        // A version from before versions had their own plug-ins takes copies with today's settings.
        storeDeviceStates();
        apply ("Version " + String (index + 1), [&] (Project& p) { for (auto id : changing) edit::selectVersion (p, id, index); });
    }
    if (! missing.empty())
        say (String ((int) missing.size()) + (missing.size() == 1 ? " track has" : " tracks have") + " no version " + String (index + 1)
             + ", so " + (missing.size() == 1 ? "it stays" : "they stay") + " as " + (missing.size() == 1 ? "it is." : "they are."));
}

Id Controller::newMidiClip (Id track, Tick start, Tick length)
{
    Id made = 0;
    apply ("New Clip", [&] (Project& p) { made = edit::addMidiClip (p, track, start, length); });
    if (made) selectClip (made);
    return made;
}

void Controller::deleteSelection()
{
    if (! selectedNotes.empty() && selectedClip)
    {
        auto notes = selectedNotes;
        const Id c = selectedClip;
        apply ("Delete Notes", [c, notes] (Project& p) { edit::removeNotes (p, c, notes); });
        selectedNotes.clear();
        return;
    }
    if (! selectedClips.empty())
    {
        auto clips = selectedClips;
        const bool rippling = ripple;
        apply ("Delete Clips", [clips, rippling] (Project& p)
        {
            if (rippling) song::rippleRemove (p, clips);   // later clips close the gap
            else edit::removeClips (p, clips);
        });
        selectedClips.clear();
        selectedClip = 0;
        return;
    }
    deleteSelectedTracks();
}

void Controller::duplicateSelection()
{
    if (! selectedNotes.empty() && selectedClip)
    {
        const auto* c = clip();
        Tick lo = std::numeric_limits<Tick>::max(), hi = 0;
        for (auto& n : c->midi.notes)
            if (selectedNotes.count (n.id)) { lo = std::min (lo, n.start); hi = std::max (hi, n.end()); }
        const Tick span = std::max (grid, hi - lo);
        std::set<Id> made;
        const Id cid = selectedClip;
        auto notes = selectedNotes;
        apply ("Duplicate Notes", [&] (Project& p) { made = edit::duplicateNotes (p, cid, notes, span); });
        selectedNotes = made;
        sendChangeMessage();
        return;
    }
    auto clips = selectedClips;
    std::set<Id> made;
    apply ("Duplicate Clips", [&] (Project& p) { for (auto c : clips) made.insert (edit::duplicateClip (p, c)); });
    if (! made.empty()) { selectedClips = made; selectedClip = *made.begin(); }
}

void Controller::splitAtCursor()
{
    auto clips = selectedClips;
    if (clips.empty())
        for (auto& t : project.tracks)
            if (std::find (selectedTracks.begin(), selectedTracks.end(), t.id) != selectedTracks.end())
                for (auto& c : t.clips()) if (c.start < cursor && c.end() > cursor) clips.insert (c.id);
    const Tick at = cursor;
    apply ("Split", [clips, at] (Project& p) { for (auto c : clips) edit::splitClip (p, c, at); });
}

void Controller::quantiseSelection()
{
    if (! selectedClip) { say ("Open a MIDI clip to quantise its notes."); return; }
    const Id c = selectedClip;
    auto notes = selectedNotes;
    const Tick g = grid;
    apply ("Quantise", [=] (Project& p) { edit::quantise (p, c, notes, g); });
}

void Controller::selectAllNotes()
{
    if (auto* c = clip())
    {
        selectedNotes.clear();
        for (auto& n : c->midi.notes) selectedNotes.insert (n.id);
        sendChangeMessage();
    }
}

//==============================================================================
void Controller::addPlugin (Id track, const PluginDescription& d)
{
    const auto ref = PluginManager::refFor (d);
    const auto role = PluginManager::roleFor (d);
    if (const auto* t = project.track (track); t && t->isCue() && role != DeviceRole::AudioFx)
    {
        say ("The Cue track takes audio effects only: nothing plays into it but the master.", true);
        return;
    }
    Id made = 0;
    apply ("Add " + d.name, [&] (Project& p) { made = edit::addDevice (p, track, ref, role); });
    if (made && ! engine.instance (made))
        say (d.name + " couldn't be loaded.", true);
    zoneTab = ZoneTab::Devices;
    sendChangeMessage();
}

void Controller::addBuiltIn (Id track, const PluginRef& ref, DeviceRole role)
{
    if (const auto* t = project.track (track); t && t->isCue() && role != DeviceRole::AudioFx)
    {
        say ("The Cue track takes audio effects only: nothing plays into it but the master.", true);
        return;
    }
    apply ("Add " + String (ref.name), [&] (Project& p) { edit::addDevice (p, track, ref, role); });
    zoneTab = ZoneTab::Devices;
    sendChangeMessage();
}

void Controller::setDeviceActive (Id device, bool active)
{
    const auto state = active ? std::string() : engine.stateOf (device);
    apply (active ? "Activate Plug-in" : "Deactivate Plug-in", [=] (Project& p) { edit::setDeviceActive (p, device, active, state); });
    if (! active) say ("Deactivated: unloaded from memory and the processor. Its settings are kept for when it's switched back on.");
}

//==============================================================================
void Controller::captureMidi()
{
    auto* t = focus();
    if (! t || ! t->holdsClips()) { say ("Choose a track to put the captured MIDI on.", true); return; }
    const auto r = capture::lastPerformance (engine.shared.midiCapture.snapshot(), project.tempo, cursor);
    if (r.notes.empty()) { say ("There's nothing played to capture yet."); return; }
    Clip c;
    c.kind = Clip::Kind::Midi;
    c.name = t->name + " Capture";
    c.start = r.start;
    c.length = r.length;
    c.midi.notes = r.notes;
    c.midi.events = r.events;
    if (t->mpe) mpe::gatherExpression (c.midi);
    Id made = 0;
    const Id tid = t->id;
    apply ("Capture MIDI", [&] (Project& p) { made = edit::addClip (p, tid, c); });
    selectClip (made);
    say ("Captured " + String ((int) r.notes.size()) + " notes" + (r.wasPlaying ? " where they were played." : " at the cursor."), false, true);
}

void Controller::captureAudio (double seconds)
{
    auto* t = focus();
    auto* s = t ? engine.source (t->id) : nullptr;
    if (! s || ! t->holdsClips()) return;
    const auto audio = s->recentInput (seconds);
    if (audio.getNumSamples() == 0 || audio.getMagnitude (0, audio.getNumSamples()) < 1.0e-4f)
    {
        say ("Nothing has come in on this track's input. Arm it or turn its monitoring on, and Kalibre keeps the last minute.");
        return;
    }
    // Trim silence at the front.
    int first = 0;
    while (first < audio.getNumSamples() && std::abs (audio.getSample (0, first)) < 1.0e-3f && std::abs (audio.getSample (1, first)) < 1.0e-3f) ++first;
    const auto f = recordingsFolder().getNonexistentChildFile (String (t->name) + " Capture", ".wav");
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = os ? wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (engine.sampleRate()).withNumChannels (2).withBitsPerSample (24)) : nullptr;
    if (! w) { say ("The captured audio couldn't be saved.", true); return; }
    w->writeFromAudioSampleBuffer (audio, first, audio.getNumSamples() - first);
    w.reset();
    importFile (f, t->id, cursor);
    say ("Captured the last " + String ((audio.getNumSamples() - first) / engine.sampleRate(), 1) + " seconds at the cursor.", false, true);
}

//==============================================================================
void Controller::touched (const Engine::Touch& t)
{
    Track* owner = nullptr;
    if (! project.device (t.device, &owner)) return;
    const bool changed = lastTouched.device != t.device || lastTouched.param != t.param;
    lastTouched = { owner->id, t.device, t.param, AutoTarget::Kind::DeviceParam, t.name };

    // Writing automation while playing, for Touch, Latch, Write and Trim.
    AutoTarget target;
    target.kind = AutoTarget::Kind::DeviceParam;
    target.device = t.device;
    target.param = t.param;
    target.paramName = t.name.toStdString();
    writeAutomation (owner->id, target, t.value, t.gesture);
    if (changed) sendChangeMessage();
}

void Controller::writeAutomation (Id trackId, const AutoTarget& target, double value, int gesture)
{
    auto* owner = project.track (trackId);
    if (! owner || ! engine.isPlaying() || owner->autoMode == AutoMode::Read || owner->autoMode == AutoMode::Off) return;
    if (gesture == 1 || ! writing.on || writing.track != trackId)
    {
        if (writing.on) finishWriting();
        writing.on = true;
        writing.before = project;
        writing.track = trackId;
        writing.lastTick = playhead();
    }
    auto& lane = edit::laneFor (*owner, target, value);
    if (lane.id == 0) lane.id = project.newId();
    const Tick now = playhead();
    if (owner->autoMode == AutoMode::Trim)
    {
        // Trim (decision 0038): the lane keeps its shape, moved by how far the hand moved
        // the control from what the lane said when the touch began.
        const AutomationLane* original = nullptr;
        if (auto* bt = writing.before.track (trackId))
            for (auto& l : bt->automation) if (l.target == target) original = &l;
        if (original && ! original->points.empty())
        {
            if (gesture == 1 || writing.trimFrom < 0) { writing.trimFrom = edit::valueAt (*original, now, value); writing.trimStart = now; }
            const double delta = value - writing.trimFrom;
            edit::writeTrim (lane, *original, writing.trimStart, now, delta);
        }
        else edit::writeValue (lane, writing.lastTick, now, value);
    }
    else edit::writeValue (lane, writing.lastTick, now, value);
    writing.lastTick = now;
    if (gesture == 2 && (owner->autoMode == AutoMode::Touch || owner->autoMode == AutoMode::Trim)) finishWriting();
}

void Controller::finishWriting()
{
    if (! writing.on) return;
    writing.on = false;
    writing.trimFrom = -1;
    history.push (writing.before, "Write Automation");
    dirty = true;
    refresh();
}

void Controller::touchedMix (Id track, AutoTarget::Kind kind, const String& name)
{
    const auto* t = project.track (track);
    lastTouched = { track, 0, -1, kind, (t ? String (t->name) + " " : String()) + name };
    sendChangeMessage();
}

void Controller::learnLastTouched()
{
    if (! lastTouched.valid()) { say ("Move a control first, on a plug-in or in the mixer; then choose MIDI Learn and move a knob."); return; }
    engine.learning = true;
    say ("Move a knob, fader or button on your MIDI controller to map it to " + lastTouched.name + ui::u8 ("\xe2\x80\xa6"));
}

void Controller::forgetMapping (size_t index)
{
    if (index >= project.midiMappings.size()) return;
    apply ("Forget Mapping", [index] (Project& p) { p.midiMappings.erase (p.midiMappings.begin() + (long) index); });
}

void Controller::mapped (const MidiMapping& m, double v)
{
    auto* t = project.track (m.track);
    if (! t) return;
    switch (m.target.kind)
    {
        case AutoTarget::Kind::Volume:
            t->volumeDb = midimap::volumeFromNormal (v);
            if (auto* f = engine.fader (m.track)) f->volumeDb = (float) t->volumeDb;
            dirty = true;
            sendChangeMessage();
            break;
        case AutoTarget::Kind::Pan:
            t->pan = midimap::panFromNormal (v);
            if (auto* f = engine.fader (m.track)) f->pan = (float) t->pan;
            dirty = true;
            sendChangeMessage();
            break;
        case AutoTarget::Kind::Mute:
            if ((v >= 0.5) != t->mute)
            {
                const Id id = m.track;
                const bool on = v >= 0.5;
                apply (on ? "Mute" : "Unmute", [id, on] (Project& p) { if (auto* x = p.track (id)) x->mute = on; });
            }
            break;
        case AutoTarget::Kind::DeviceParam:
            // As if a hand moved it: it writes automation in Touch, Latch and Write.
            if (auto* inst = engine.instance (m.target.device))
                if (auto* p = inst->getParameters()[m.target.param]) p->setValueNotifyingHost ((float) v);
            break;
        case AutoTarget::Kind::SendLevel:
        case AutoTarget::Kind::MorphX:
        case AutoTarget::Kind::MorphY:
            break;
    }
}

void Controller::automateLastTouched()
{
    if (! lastTouched.valid()) { say ("Move a control first, on a plug-in or in the mixer: its automation lane appears here."); return; }
    if (! lastTouched.isPlugin())
    {
        auto lt = lastTouched;
        apply ("Automate " + lt.name, [lt] (Project& p)
        {
            auto* t = p.track (lt.track);
            if (! t) return;
            AutoTarget target;
            target.kind = lt.kind;
            const double now = lt.kind == AutoTarget::Kind::Pan ? (t->pan + 1.0) / 2.0 : (t->volumeDb + 60.0) / 66.0;
            auto& lane = edit::laneFor (*t, target, now);
            if (lane.id == 0) lane.id = p.newId();
            lane.shown = true;
        });
        say ("Automation lane for " + lt.name + " added under its track.");
        return;
    }
    auto* inst = engine.instance (lastTouched.device);
    const float v = inst && inst->getParameters()[lastTouched.param] ? inst->getParameters()[lastTouched.param]->getValue() : 0.5f;
    auto lt = lastTouched;
    apply ("Automate " + lt.name, [lt, v] (Project& p)
    {
        auto* t = p.track (lt.track);
        if (! t) return;
        AutoTarget target;
        target.kind = AutoTarget::Kind::DeviceParam;
        target.device = lt.device;
        target.param = lt.param;
        target.paramName = lt.name.toStdString();
        auto& lane = edit::laneFor (*t, target, v);
        if (lane.id == 0) lane.id = p.newId();
        lane.shown = true;
    });
    say ("Automation lane for " + lt.name + " added under its track.");
}

void Controller::modulateLastTouched (ModKind kind)
{
    if (! lastTouched.valid() || ! lastTouched.isPlugin()) { say ("Move a control on a plug-in first, then choose what modulates it."); return; }
    auto lt = lastTouched;
    apply ("Modulate " + lt.name, [lt, kind] (Project& p)
    {
        auto* t = p.track (lt.track);
        if (! t) return;
        Modulator m;
        m.id = p.newId();
        m.kind = kind;
        static const char* names[] { "LFO", "ADSR", "ADR", "Follower", "Wavetable" };
        m.name = names[(int) kind];
        if (kind == ModKind::Wavetable)
            for (int i = 0; i < 64; ++i) m.table.push_back ((float) std::sin (i / 64.0 * MathConstants<double>::twoPi));
        m.targets.push_back ({ lt.device, lt.param, 0.25, lt.name.toStdString() });
        t->modulators.push_back (m);
    });
    say ("Modulating " + lt.name + ". Its amount and shape are under Modulators in the device rack.");
}

//==============================================================================
Tick Controller::snapTick (Tick t) const
{
    if (! snap || grid <= 0) return t;
    const int bar = project.tempo.barAt (t);
    const Tick start = project.tempo.barStart (bar);
    return start + (Tick) std::llround ((double) (t - start) / (double) grid) * grid;
}

Tick Controller::snapDown (Tick t) const
{
    if (! snap || grid <= 0) return t;
    const int bar = project.tempo.barAt (t);
    const Tick start = project.tempo.barStart (bar);
    return start + ((t - start) / grid) * grid;
}

//==============================================================================
File Controller::scriptsFolder() const
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
           #if JUCE_MAC
               .getChildFile ("Application Support")
           #endif
               .getChildFile ("Kalibre/Scripts");
    f.createDirectory();
    return f;
}

void Controller::runScript (const File& f)
{
    script::Context ctx;
    ctx.selectedTracks = selectedTracks;
    ctx.selectedClip = selectedClip;
    ctx.selectedNotes = selectedNotes;
    ctx.cursor = cursor;
    ctx.playhead = playhead();
    ctx.message = [this] (const std::string& m) { say (String (m)); };
    ctx.action = [this] (const std::string& a)
    {
        if (a == "play") { play(); return true; }
        if (a == "stop") { stop(); return true; }
        if (a == "record") { record(); return true; }
        return false;
    };
    storeDeviceStates();
    Project before = project;
    const auto r = script::run (project, f.loadFileAsString().toStdString(), f.getFileName().toStdString(), ctx);
    if (! r.ok) { say (f.getFileNameWithoutExtension() + ": " + String (r.error), true); return; }
    selectedClip = r.after.selectedClip;
    selectedNotes = r.after.selectedNotes;
    if (r.changed)
    {
        history.push (before, f.getFileNameWithoutExtension().toStdString());
        dirty = true;
        syncEngine();
    }
    if (! r.printed.empty()) say (String (r.printed).trim());
    sendChangeMessage();
}

//==============================================================================
void Controller::timerCallback()
{
    // Every three minutes, while not recording.
    const auto now = Time::getMillisecondCounter();
    if (now - lastAutosave > 180000 && ! wasRecording)
    {
        lastAutosave = now;
        autosave();
    }
    // Follow playback: the cursor stays where Play started; the view follows the playhead.
    static bool lastPlaying = false;
    const bool playing = engine.isPlaying();
    if (playing != lastPlaying)
    {
        lastPlaying = playing;
        if (! playing && wasRecording) finishRecording();
        if (onTransportChanged) onTransportChanged();
    }
}

} // namespace kal
