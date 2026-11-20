#include "Controller.h"
#include "LibraryScanner.h"
#include "ReaperScripts.h"
#include "NoteSplitter.h"
#include "AudioFiles.h"
#include "JsfxDevice.h"

#include "AudioTools.h"
#include "Capture.h"
#include "Comp.h"
#include "MidiLearn.h"
#include "Mpe.h"
#include "Harmony.h"
#include "LuaHost.h"
#include "LuaTool/LuaTool.h"
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
    insertToolBlock = [this] (AudioPluginInstance& from, const ToolBlock& b) { return placeToolBlock (from, b); };
    setJsfxExportHandler ([this] (AudioPluginInstance& from, const AudioBuffer<float>& audio, double rate) { printFromDevice (from, audio, rate); });
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
    NoteSplitterProcessor::makeEditor = nullptr;
    newProject();
    beginWorkClock();
    reaperScripts = std::make_unique<ReaperScripts> (*this);
    startTimerHz (30);
}

bool Controller::loadChosenClipsInto (Id device)
{
    std::vector<Id> clips;
    for (auto id : selectedClips) if (auto* cl = project.clip (id); cl && cl->isMidi()) clips.push_back (id);
    if (clips.empty() && ! (clip() && clip()->isMidi()))
    {
        say ("Choose a MIDI clip first (click it in the arrangement), then load it.", true);
        return false;
    }
    return reaperScripts->loadChosenClips (device);
}

Controller::~Controller()
{
    stopTimer();
    setJsfxExportHandler ({});
    reaperScripts.reset();
    engine.cache.onReady = nullptr;
    engine.onTouched = nullptr;
    engine.onMessage = nullptr;
    insertToolBlock = nullptr;
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

bool Controller::applyLive (const Project& before, const String& label)
{
    auto after = std::make_shared<Project> (std::move (project));
    project = before;
    return apply (label, [after] (Project& p) { p = *after; });
}

void Controller::chooseTool (tools::Area area, tools::Tool tool, std::optional<int> mode)
{
    auto& state = toolsFor (area);
    if (! state.choose (tool, area, mode)) return;
    if (tool != tools::Tool::Range && area == tools::Area::Arrange) range = {};
    say (String (area == tools::Area::Arrange ? "Arrangement" : "Piano roll") + " tool: " + String (state.describe()) + ". "
         + String (tools::info (tool).help));
    viewChanged();
}

void Controller::chooseToolEverywhere (tools::Tool tool)
{
    const bool inArrange = arrangeTool.choose (tool, tools::Area::Arrange);
    const bool inRoll = rollTool.choose (tool, tools::Area::Roll);
    if (! inArrange && ! inRoll) return;
    if (tool != tools::Tool::Range) range = {};
    const auto& state = inArrange ? arrangeTool : rollTool;
    say ("Tool: " + String (state.describe()) + (inArrange && inRoll ? "" : inArrange ? " (in the arrangement)" : " (in the piano roll)")
         + ". " + String (tools::info (tool).help));
    viewChanged();
}

bool Controller::apply (const String& label, std::function<void (Project&)> change)
{
    storeDeviceStates();
    Project before = project;
    change (project);
    activity = true;   // an edit is music-making (0091)
    // Never more than 1000 tracks (0088): the whole change is refused.
    if (edit::overTrackLimit (project) && project.tracks.size() > before.tracks.size())
    {
        project = std::move (before);
        refuseTooManyTracks();
        return false;
    }
    // Linked clips and grouped tracks follow (decision 0037).
    song::propagateLinks (before, project);
    song::propagateGroups (song::mixOf (before), project);
    if (project == before) return true;
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
    return true;
}

void Controller::refuseTooManyTracks()
{
    say (edit::tooManyTracks, true);
    refusedAt = Time::getMillisecondCounter();
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

void Controller::carryPlayingStates (const Project& was)
{
    auto carry = [] (const Track& before, Track& now)
    {
        if (before.version().id == now.version().id) return;
        for (auto& v : now.versions)
            if (v.id == before.version().id)
                for (auto& d : v.devices)
                    for (auto& playing : before.devices)
                        if (playing.id == d.id && playing.plugin.identifier == d.plugin.identifier && ! playing.state.empty())
                        {
                            d.state = playing.state;
                            if (! playing.araState.empty()) d.araState = playing.araState;
                        }
    };
    for (auto& t : project.tracks)
        if (auto* before = was.track (t.id)) carry (*before, t);
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
    const auto states = deviceStates();
    const Project was = project;
    if (! history.undo (project)) return false;
    carryPlayingStates (was);
    dirty = true;
    syncEngine();
    reloadChangedStates (states);
    refreezeMissing();
    say ("Undid " + String (label) + ".");
    return true;
}

bool Controller::redo()
{
    storeDeviceStates();
    const auto label = history.redoLabel();
    const auto states = deviceStates();
    const Project was = project;
    if (! history.redo (project)) return false;
    carryPlayingStates (was);
    dirty = true;
    syncEngine();
    reloadChangedStates (states);
    refreezeMissing();
    say ("Redid " + String (label) + ".");
    return true;
}

void Controller::say (const String& m, bool warning, bool canUndo)
{
    // What a refused command would have said next doesn't cover the refusal (0088).
    if (refusedAt != 0 && Time::getMillisecondCounter() - refusedAt < 250) return;
    message = m;
    messageIsWarning = warning;
    messageCanUndo = canUndo;
    messageChoice = choiceKey();
    messagePointer = pointerKey();
    sendChangeMessage();
}

void Controller::setStepInput (bool on)
{
    stepInput = on;
    stepper.reset();
    engine.stepInputOn = on;
    engine.onStepKey = [this] (int pitch, int velocity, bool down) { stepKey (pitch, velocity, down); };
    if (on) sayStepInput();
    else say ("Step input is off.");
    sendChangeMessage();
}

void Controller::sayStepInput()
{
    say ("Step input: hold keys on a MIDI keyboard, press 1 to 8 for the length (1 = 1/128, 4 = 1/16, 6 = 1/4, 8 = 1/1; 9 then a number: dotted; "
         "0 then a number: triplet), let go to write them; - for a rest that long. Now " + String (stepper.lengthName()) + ".");
}

bool Controller::stepDigit (int d)
{
    if (! stepInput) return false;
    stepper.digit (d);
    sayStepInput();
    return true;
}

bool Controller::stepRest()
{
    if (! stepInput) return false;
    const Tick len = stepper.rest();
    const Tick to = cursor + len;
    // The chosen clip grows to hold the rest, so what's written next lands in it.
    const auto* cl = clip();
    if (cl && cl->isMidi() && cl->loopLength == 0 && to > cl->end())
    {
        const Id cid = selectedClip;
        apply ("Step Input Rest", [&] (Project& p)
        {
            if (auto* c = p.clip (cid))
                c->length = std::max (c->length, p.tempo.barStart (p.tempo.barAt (to - 1) + 1) - c->start);
        });
    }
    locate (to);
    say ("Rest. Step input: now " + String (stepper.lengthName()) + "; - for another rest.");
    return true;
}

void Controller::stepKey (int pitch, int velocity, bool on)
{
    if (! stepInput) return;
    if (on) { stepper.keyDown (pitch, velocity); return; }
    auto notes = stepper.keyUp (pitch);
    if (notes.empty()) return;
    const Tick len = stepper.length();
    const auto from = editor == EditorKind::Drums ? NoteFrom::Drums : NoteFrom::Roll;
    const Tick at = cursor;
    Id cid = clip() && clip()->isMidi() ? selectedClip : 0;
    const Id track = focusTrack;
    std::set<Id> made;
    apply ("Step Input", [&] (Project& p)
    {
        auto* cl = p.clip (cid);
        if (! cl)
        {
            // No clip chosen: a new one on the track, from the bar the cursor is in.
            const auto* t = p.track (track);
            if (! t || ! t->holdsClips()) return;
            const int bar = p.tempo.barAt (at);
            cid = edit::addMidiClip (p, track, p.tempo.barStart (bar), p.tempo.meterAtBar (bar).barTicks());
            cl = p.clip (cid);
            if (! cl) return;
        }
        Tick rel = at - (cl->start - cl->offset);
        if (cl->loopLength > 0) rel = ((rel % cl->loopLength) + cl->loopLength) % cl->loopLength;
        for (auto n : notes)
        {
            n.start = rel;
            n.from = from;
            made.insert (edit::addNote (p, cid, n));
        }
        // Past its end, the clip grows to the bar the notes end in.
        if (cl->loopLength == 0 && rel + len > cl->offset + cl->length)
        {
            const Tick songEnd = cl->start - cl->offset + rel + len;
            cl->length = std::max (cl->length, p.tempo.barStart (p.tempo.barAt (songEnd - 1) + 1) - cl->start);
        }
    });
    if (made.empty()) { say ("Choose a track for step input to write into.", true); return; }
    if (selectedClip != cid) { selectedClip = cid; selectedClips = { cid }; }
    selectedNotes = made;
    locate (at + len);
}

void Controller::selectNotes (noteselect::What what, std::optional<NoteFrom> only)
{
    const auto* cl = clip();
    if (! cl || ! cl->isMidi()) { say ("Choose a MIDI clip first."); return; }
    selectedNotes = noteselect::choose (*cl, project.tempo, what, only);
    static const char* names[] { "roots", "thirds", "fifths", "sevenths", "ninths", "elevenths", "thirteenths",
                                 "notes on the beat", "notes off the beat", "notes along the bottom", "notes along the top" };
    const int n = (int) selectedNotes.size();
    if (n == 0) say (String ("There are no ") + names[(int) what] + " in this clip.");
    else say ("Chose the " + String (names[(int) what]) + ": " + String (n) + (n == 1 ? " note." : " notes."));
    sendChangeMessage();
}

void Controller::explodeChords (const std::set<Id>& clips)
{
    int tracks = 0;
    std::vector<Id> first;
    apply ("Explode Chords", [&] (Project& p)
    {
        for (auto id : clips)
        {
            const auto made = noteselect::explode (p, id);
            tracks += (int) made.size();
            if (first.empty()) first = made;
        }
    });
    if (tracks == 0) { say ("There are no chords to explode: a clip needs notes played together."); return; }
    selectedClip = 0;
    selectedClips.clear();
    selectedNotes.clear();
    if (! first.empty()) focusOn (first.front());
    say ("Exploded the chords into " + String (tracks) + " tracks, a voice each, top to bottom, with the track's plug-ins.", false, true);
}

String Controller::choiceKey() const
{
    String k;
    for (auto id : selectedNotes) k << (int64) id << ",";
    k << "|";
    for (auto id : selectedClips) k << (int64) id << ",";
    return k;
}

void Controller::retireMessage()
{
    if (message.isEmpty()) return;
    const bool choiceMoved = choiceKey() != messageChoice;
    const bool pointerMoved = pointerKey() != messagePointer && hoveredClip != 0;
    if (choiceMoved || (! messageIsWarning && pointerMoved)) dismiss();
}

String Controller::describeNotes (const Clip& clip, const std::set<Id>& ids) const
{
    std::vector<const Note*> ns;
    for (auto& n : clip.midi.notes) if (ids.count (n.id)) ns.push_back (&n);
    if (ns.empty()) return {};
    const auto& tempo = project.tempo;
    const Tick base = clip.start - clip.offset;
    const String dot = ui::u8 (" \xc2\xb7 ");
    if (ns.size() == 1)
    {
        const auto& n = *ns.front();
        return String (harmony::noteName (project.scale, n.pitch)) + dot + "Velocity " + String (n.velocity)
             + dot + "Length " + String (tempo.describeLength (base + n.start, n.length))
             + dot + "At " + String (tempo.formatBarBeat (base + n.start));
    }
    int lo = 127, hi = 0, vlo = 127, vhi = 0;
    Tick llo = std::numeric_limits<Tick>::max(), lhi = 0;
    for (auto* n : ns)
    {
        lo = std::min (lo, n->pitch); hi = std::max (hi, n->pitch);
        vlo = std::min (vlo, n->velocity); vhi = std::max (vhi, n->velocity);
        llo = std::min (llo, n->length); lhi = std::max (lhi, n->length);
    }
    const String dash = ui::u8 ("\xe2\x80\x93");
    String s = String ((int) ns.size()) + " notes" + dot;
    s << (lo == hi ? String (harmony::noteName (project.scale, lo))
                   : String (harmony::noteName (project.scale, lo)) + " to " + String (harmony::noteName (project.scale, hi)));
    s << dot << "Velocity " << (vlo == vhi ? String (vlo) : String (vlo) + dash + String (vhi));
    s << dot << (llo == lhi ? "Length " + String (tempo.describeLength (base, llo))
                            : "Lengths " + String (tempo.describeLength (base, llo)) + " to " + String (tempo.describeLength (base, lhi)));
    return s;
}

String Controller::describeClip (const Clip& c) const
{
    const auto& tempo = project.tempo;
    const String dot = ui::u8 (" \xc2\xb7 ");
    String s = String (c.name).isNotEmpty() ? String (c.name) : String (c.isMidi() ? "MIDI clip" : "Audio clip");
    s << dot << String (tempo.describeLength (c.start, c.length));
    if (c.isMidi())
    {
        const auto w = patterns::windowOf (c);
        int count = 0;
        for (auto& n : c.midi.notes) count += n.start >= w.from && n.start < w.from + w.length ? 1 : 0;
        s << dot << count << (count == 1 ? " note" : " notes");
        if (c.loopLength > 0) s << dot << "Looped every " << String (tempo.describeLength (c.start, c.loopLength));
    }
    else
    {
        const double secs = tempo.secondsAt (c.end()) - tempo.secondsAt (c.start);
        s << " (" << String (secs, 1) << " s)";
        if (! c.audio.file.empty()) s << dot << File (String (c.audio.file)).getFileName();
    }
    s << dot << "From " << String (tempo.formatBarBeat (c.start));
    return s;
}

String Controller::infoLine() const
{
    if (const auto* clip = project.clip (selectedClip); clip && clip->isMidi() && ! selectedNotes.empty())
    {
        const auto s = describeNotes (*clip, selectedNotes);
        if (s.isNotEmpty()) return s;
    }
    if (selectedClips.size() == 1)
        if (const auto* clip = project.clip (*selectedClips.begin())) return describeClip (*clip);
    if (selectedClips.size() > 1)
    {
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        int notes = 0, n = 0;
        for (auto id : selectedClips)
            if (const auto* clip = project.clip (id))
            {
                ++n;
                from = std::min (from, clip->start);
                to = std::max (to, clip->end());
                if (clip->isMidi())
                {
                    const auto w = patterns::windowOf (*clip);
                    for (auto& x : clip->midi.notes) notes += x.start >= w.from && x.start < w.from + w.length ? 1 : 0;
                }
            }
        if (n > 0)
        {
            const String dot = ui::u8 (" \xc2\xb7 ");
            String s = String (n) + " clips" + dot + String (project.tempo.describeLength (from, to - from)) + " in all"
                     + dot + "From " + String (project.tempo.formatBarBeat (from));
            if (notes > 0) s << dot << notes << " notes";
            return s;
        }
    }
    if (const auto* clip = project.clip (hoveredClip))
    {
        if (hoveredNote != 0 && clip->isMidi())
        {
            const auto s = describeNotes (*clip, { hoveredNote });
            if (s.isNotEmpty()) return s;
        }
        return describeClip (*clip);
    }
    return {};
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

void Controller::arm (Id id, ArmFor how)
{
    const auto* t = project.track (id);
    if (! t || ! t->holdsClips()) return;
    if (how != ArmFor::Audio && t->kind == TrackKind::Summing)
    {
        say ("A Summing track records the master's sound, so it's armed for audio.");
        how = ArmFor::Audio;
    }
    const bool same = t->armed && t->armFor == how;
    const char* names[] { "Arm for Audio", "Arm for MIDI", "Arm for Audio and MIDI", "Arm for MIDI Input" };
    apply (names[(int) how], [=] (Project& p)
    {
        if (auto* x = p.track (id)) { x->armed = ! same; x->armFor = how; }
    });
}

void Controller::record()
{
    if (wasRecording) { stop(); return; }
    std::vector<Id> armed;
    for (auto& t : project.tracks) if (t.armed) armed.push_back (t.id);
    if (armed.empty() && focus() && focus()->holdsClips())
    {
        // Recording with nothing armed records onto the track being played: for
        // MIDI if it has an instrument or MIDI effect or no sound coming in.
        const auto* f = focus();
        const bool plays = std::any_of (f->devices.begin(), f->devices.end(), [] (const Device& d) { return d.role != DeviceRole::AudioFx; });
        const bool midi = f->kind != TrackKind::Summing && (plays || f->input.audioChannel < 0);
        apply ("Arm Track", [id = focusTrack, midi] (Project& p) { if (auto* t = p.track (id)) { t->armed = true; t->armFor = midi ? ArmFor::Midi : ArmFor::Audio; } });
        armed.push_back (focusTrack);
    }
    if (armed.empty()) { say ("Choose a track to record onto first.", true); return; }

    Tick from = cursor;
    if (project.punch.on) from = project.punch.start;
    recordFromSeconds = project.tempo.secondsAt (from);
    const auto folder = recordingsFolder();
    for (auto id : armed)
    {
        // Nothing left over from before.
        if (auto* s = engine.source (id))
        {
            s->tappedFifo.reset();
            s->playedFifo.reset();
        }
        if (auto* s = engine.recordSource (id))
        {
            const auto* t = project.track (id);
            // A Summing track records the master (decision 0052); others armed for audio their input.
            if (t && (t->kind == TrackKind::Summing || (t->input.audioChannel >= 0 && t->armedAudio())))
                s->startRecording (folder.getNonexistentChildFile (String (t->name).replaceCharacters ("/\\:", "---") + " "
                                                                   + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav"),
                                   engine.sampleRate());
        }
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
        // Audio: a Summing track's is the master's (decision 0052).
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
                // (the master arrives after the graph's delay): they're left out (decision 0052).
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
        // Armed for MIDI (decision 0064): what the instrument was played, caught on it
        // (or, if it sent notes of its own, what it sent); with no instrument or MIDI
        // effect, what was played in.
        std::vector<capture::Event> events, tappedIn, tappedOut;
        {
            int s1, n1, s2, n2;
            s->tappedFifo.prepareToRead (s->tappedFifo.getNumReady(), s1, n1, s2, n2);
            for (int i = 0; i < n1 + n2; ++i)
            {
                const auto& p = s->tapped[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
                capture::Event e;
                e.wallSeconds = p.seconds;
                e.songTicks = project.tempo.ticksAt (p.seconds);
                std::memcpy (e.bytes, p.bytes, 3);
                (p.fromOutput ? tappedOut : tappedIn).push_back (e);
            }
            s->tappedFifo.finishedRead (n1 + n2);
        }
        const bool sentNotes = std::any_of (tappedOut.begin(), tappedOut.end(), [] (const capture::Event& e) { return (e.bytes[0] & 0xF0) == 0x90; });
        if (t.armedPlugins()) events = sentNotes ? tappedOut : tappedIn;
        // Armed for MIDI input (decision 0065): what was played in, before any plug-in.
        int s1, n1, s2, n2;
        s->playedFifo.prepareToRead (s->playedFifo.getNumReady(), s1, n1, s2, n2);
        if (events.empty() && (t.armedPlugins() || t.armedMidiIn()))
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
        if (! (t.armedPlugins() || t.armedMidiIn()) || events.empty()) continue;
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
    engine.newSong();
    project = Project();
    beginWorkClock();
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
    autosaveFile().replaceWithText (String (file::save (stampedForSaving())));
}

File Controller::backupsFolder() const
{
    if (file.existsAsFile()) return file.getParentDirectory().getChildFile ("Backups");
    const auto root = backupsRoot != File() ? backupsRoot : scriptsFolder().getParentDirectory().getChildFile ("Backups");
    return root.getChildFile ("Untitled");
}

Array<File> Controller::backups() const
{
    const auto stem = file.existsAsFile() ? file.getFileNameWithoutExtension() : String ("Untitled");
    auto found = backupsFolder().findChildFiles (File::findFiles, false, stem + " 2???-??-?? *.kalibre");
    // Newest first: their names are when they were made, to the millisecond.
    std::sort (found.begin(), found.end(), [] (const File& a, const File& b) { return a.getFileName() > b.getFileName(); });
    return found;
}

bool Controller::backup()
{
    if (project.tracks.empty()) return false;
    storeDeviceStates();
    const String text (file::save (stampedForSaving()));
    if (text == lastBackupText) return false;
    auto folder = backupsFolder();
    folder.createDirectory();
    const auto stem = file.existsAsFile() ? file.getFileNameWithoutExtension() : String ("Untitled");
    // Named by when it was made, so they sort by name; never two the same.
    auto when = Time::getCurrentTime();
    auto nameAt = [&] (Time t) { return folder.getChildFile (stem + " " + t.formatted ("%Y-%m-%d %H.%M.%S.") + String (t.getMilliseconds()).paddedLeft ('0', 3) + ".kalibre"); };
    while (nameAt (when).exists()) when += RelativeTime::milliseconds (1);
    const auto f = nameAt (when);
    if (! f.replaceWithText (text)) return false;
    lastBackupText = text;
    auto all = backups();
    for (int i = backupsKept; i < all.size(); ++i) all.getReference (i).deleteFile();
    return true;
}

bool Controller::openBackup (const File& f)
{
    const auto keep = file;
    if (! open (f)) return false;
    // It's this song as it was: saved where the song is, not over the backup.
    file = keep;
    dirty = true;
    syncEngine();
    say ("Opened the backup from " + f.getLastModificationTime().toString (true, true, false) + ". Save to keep it.");
    return true;
}

File Controller::recordingsFolder() const
{
    auto f = projectFolder().getChildFile ("Audio");
    f.createDirectory();
    return f;
}

// VST3 effects were filed as MIDI effects, before the instrument, where they
// heard nothing (decision 0058): put them with the audio effects.
void refileEffects (Project& p, const PluginManager& plugins)
{
    auto refile = [&plugins] (std::vector<Device>& devices)
    {
        bool moved = false;
        for (auto& d : devices)
            if (d.role == DeviceRole::MidiFx && d.plugin.format == "VST3")
                if (PluginDescription desc; PluginManager::descriptionFor (d.plugin, desc) && plugins.roleOf (desc) == DeviceRole::AudioFx)
                {
                    d.role = DeviceRole::AudioFx;
                    moved = true;
                }
        return moved;
    };
    auto track = [&] (Track& t)
    {
        if (refile (t.devices)) edit::normaliseChain (t);
        for (auto& v : t.versions)
            if (refile (v.devices))
                std::stable_sort (v.devices.begin(), v.devices.end(), [] (const Device& a, const Device& b) { return (int) a.role < (int) b.role; });
    };
    for (auto& t : p.tracks) track (t);
    track (p.master);
    for (auto& t : p.cue) track (t);
}

bool Controller::open (const File& f)
{
    std::string error;
    auto loaded = file::load (f.loadFileAsString().toStdString(), &error);
    if (! loaded) { say (String (error), true); return false; }
    if (edit::overTrackLimit (*loaded)) { refuseTooManyTracks(); return false; }   // 0088
    engine.stop();
    engine.newSong();
    project = std::move (*loaded);
    beginWorkClock();   // the song's own time so far (0091)
    refileEffects (project, engine.plugins());
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
    if (! file.replaceWithText (String (file::save (stampedForSaving()))))
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

// A device's sound printed into the song (Transient Shuffler's Print, 0094):
// a 32-bit float WAV in the song's Audio folder, as a clip at the cursor on a
// new track under the device's own, one undo step.
bool Controller::printFromDevice (AudioPluginInstance& from, const AudioBuffer<float>& audio, double rate)
{
    Id track = 0;
    String deviceName;
    for (auto& t : project.tracks)
        for (auto& d : t.devices)
            if (engine.instance (d.id) == &from) { track = t.id; deviceName = String (d.plugin.name); }
    const auto* owner = project.track (track);
    if (! owner || audio.getNumSamples() == 0 || rate <= 0) { say ("The printed sound couldn't be put in the song: its device wasn't found.", true); return false; }
    const String name = String (owner->name) + " " + deviceName;
    const auto file = recordingsFolder().getNonexistentChildFile (File::createLegalFileName (name), ".wav");
    file.getParentDirectory().createDirectory();
    {
        auto writer = audiofile::floatWav (file, rate, audio.getNumChannels());
        if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        {
            say ("The printed sound couldn't be written to the song's Audio folder.", true);
            return false;
        }
    }
    Id made = 0;
    const bool ok = importFile (file, 0, cursor, [&] (Project& p)
    {
        made = edit::addTrack (p, TrackKind::Track, name.toStdString(), track);
        return made;
    }, "Print " + deviceName);
    if (! ok) return false;
    focusOn (made);
    selectedTracks = { made };
    say ("Printed " + deviceName + "'s sound onto a new track, " + name.quoted() + ", at the cursor.");
    sendChangeMessage();
    return true;
}

double Controller::tempoOfAudioFile (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    return audioFileTempo (f.getFileName(), r ? r->metadataValues : StringPairArray());
}

bool Controller::importFile (const File& original, Id trackId, Tick at, std::function<Id (Project&)> makeTrack, const String& label,
                             bool splitAtTransients)
{
    auto f = original;
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi")
    {
        FileInputStream in (f);
        MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) { say ("That MIDI file couldn't be read.", true); return false; }
        const int fileTpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
        apply (label.isNotEmpty() ? label : String ("Import MIDI"), [&] (Project& p)
        {
            Id target = makeTrack ? makeTrack (p) : trackId;
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
    const double fileTempo = tempoOfAudioFile (original);
    // Split at Transients as it comes in (0095): the hits, in seconds into the file.
    std::vector<double> hits;
    size_t pieces = 0;
    if (splitAtTransients)
    {
        AudioBuffer<float> buf;
        double rate = 0;
        if (readAudio (original, buf, rate))
            for (int s : audiotools::transients (buf.getArrayOfReadPointers(), buf.getNumChannels(), buf.getNumSamples(), rate, 0.5))
                hits.push_back (s / rate);
    }
    // Kept as a 32-bit float WAV, copied into the song's Audio folder if it isn't one (decision 0079).
    if (! audiofile::isFloatWav (f))
    {
        const auto copy = recordingsFolder().getNonexistentChildFile (f.getFileNameWithoutExtension(), ".wav");
        if (! audiofile::convertToFloatWav (f, copy)) { say ("Kalibre couldn't copy " + f.getFileName() + " into the song's Audio folder.", true); return false; }
        f = copy;
    }
    apply (label.isNotEmpty() ? label : String ("Import Audio"), [&] (Project& p)
    {
        Id target = makeTrack ? makeTrack (p) : trackId;
        if (! p.track (target) || ! p.track (target)->holdsClips())
            target = edit::addTrack (p, TrackKind::Track, original.getFileNameWithoutExtension().toStdString());
        Clip c;
        c.kind = Clip::Kind::Audio;
        c.name = original.getFileNameWithoutExtension().toStdString();
        c.start = at;
        c.length = std::max<Tick> (1, p.tempo.tickAt (p.tempo.secondsAt (at) + seconds) - at);
        c.audio.file = f.getFullPathName().toStdString();
        c.audio.sourceLength = seconds;
        c.audio.fileSampleRate = sr;
        c.audio.fileChannels = channels;
        // A loop named with its tempo ("Drums 96bpm"), or carrying it (an ACID chunk), is warped to the project.
        const double bpm = fileTempo;
        {
            if (bpm >= 40 && bpm <= 300)
            {
                c.audio.sourceBpm = bpm;
                c.audio.warp = WarpMode::Spectral;
                c.length = (Tick) std::llround (seconds * bpm / 60.0 * PPQ);
            }
        }
        const Id made = edit::addClip (p, target, c);
        if (! hits.empty()) pieces = edit::splitAtSource (p, made, hits).size();
    });
    if (splitAtTransients) say (pieces < 2 ? String ("No hits were found in it: it's one clip.")
                                           : "Split into " + String ((int) pieces) + " clips, one for each hit.", false, true);
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
    if (! apply ("Add Track", [&] (Project& p)
    {
        made = edit::addTrack (p, kind, label.toStdString(), kind == TrackKind::Return ? 0 : after,
                               kind == TrackKind::Return ? 0 : parent);
        auto& colours = theme::trackColours();
        if (auto* t = p.track (made); t && ! t->isCue()) t->colour = colours[(int) p.tracks.size() % colours.size()].getARGB();
    })) return 0;
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

bool Controller::renderTrackAudio (Id id, const std::set<Id>& onlyClips, double start, double end, double tail,
                                   const File& out, String& error, bool instrumentsOnly, std::function<void (Project&)> adjust)
{
    storeDeviceStates();
    // The whole song, so no other plug-in is unloaded; only this track goes out, at unity.
    Project copy = project;
    for (auto& t : copy.tracks) t.solo = false;
    auto* t = copy.track (id);
    if (! t) { error = "There's no such track."; return false; }
    t->frozen = false;
    t->mute = false;
    t->volumeDb = 0;
    t->pan = 0;
    t->sends.clear();
    t->automation.erase (std::remove_if (t->automation.begin(), t->automation.end(), [] (const AutomationLane& a)
    {
        return a.target.kind == AutoTarget::Kind::Volume || a.target.kind == AutoTarget::Kind::Pan || a.target.kind == AutoTarget::Kind::Mute;
    }), t->automation.end());
    if (! onlyClips.empty())
        t->clips().erase (std::remove_if (t->clips().begin(), t->clips().end(), [&] (const Clip& c) { return onlyClips.count (c.id) == 0; }), t->clips().end());
    copy.metronome.on = false;
    // Only what the instruments play: effects (a reverb) bypassed, their delay kept.
    if (instrumentsOnly)
        for (auto& d : t->devices) if (d.role == DeviceRole::AudioFx) d.bypassed = true;
    if (adjust) adjust (copy);
    const bool ok = engine.renderTrack (copy, id, projectFolder(), out, start, end, tail, error);
    syncEngine();
    return ok;
}

void Controller::freeze (Id id, bool on)
{
    auto* t = project.track (id);
    if (! t || ! t->holdsClips()) return;
    if (! on)
    {
        const File render (String (t->frozenFile));
        apply ("Unfreeze", [id] (Project& p) { if (auto* x = p.track (id)) { x->frozen = false; x->frozenFile.clear(); } });
        // The render goes (Undo renders it again); the plug-ins come back with their states.
        if (render.existsAsFile()) render.deleteFile();
        say ("Unfrozen: the render is deleted and its plug-ins are loaded again, as they were.", false, true);
        return;
    }
    if (t->frozen) return;
    Tick end = 0;
    for (auto& c : t->clips()) end = std::max (end, c.end());
    const auto out = recordingsFolder().getNonexistentChildFile (String (t->name) + " frozen", ".wav");
    String error;
    if (! renderTrackAudio (id, {}, 0, project.tempo.secondsAt (end), 4.0, out, error))
    {
        say ("The track couldn't be frozen. " + error, true);
        return;
    }
    const auto path = out.getFullPathName().toStdString();
    apply ("Freeze", [id, path] (Project& p) { if (auto* x = p.track (id)) { x->frozen = true; x->frozenFile = path; } });
    say ("Frozen: it plays as audio and its plug-ins are unloaded, their states kept. Unfreeze to change it.", false, true);
}

void Controller::refreezeMissing()
{
    for (auto& t : project.tracks)
    {
        if (! t.frozen || (! t.frozenFile.empty() && File (String (t.frozenFile)).existsAsFile())) continue;
        Tick end = 0;
        for (auto& c : t.clips()) end = std::max (end, c.end());
        const auto out = recordingsFolder().getNonexistentChildFile (String (t.name) + " frozen", ".wav");
        String error;
        const Id id = t.id;
        if (renderTrackAudio (id, {}, 0, project.tempo.secondsAt (end), 4.0, out, error))
        {
            if (auto* x = project.track (id)) x->frozenFile = out.getFullPathName().toStdString();
            syncEngine();
        }
        else say ("The track couldn't be frozen again. " + error, true);
    }
}

void Controller::consolidate (const std::set<Id>& chosen)
{
    const Track* owner = nullptr;
    int midi = 0, audio = 0;
    Tick from = std::numeric_limits<Tick>::max(), to = 0;
    std::string name;
    for (auto cid : chosen)
    {
        const Track* o = nullptr;
        const auto* c = project.clip (cid, &o);
        if (! c) continue;
        if (owner && o != owner) { say ("Choose clips on one track to consolidate.", true); return; }
        owner = o;
        (c->isMidi() ? midi : audio)++;
        if (c->start < from) { from = c->start; name = c->name; }
        to = std::max (to, c->end());
    }
    if (! owner) return;
    if (midi > 0 && audio > 0) { say ("Choose only MIDI clips or only audio clips to consolidate.", true); return; }
    if (midi > 0)
    {
        apply ("Consolidate", [&] (Project& p) { edit::consolidateClips (p, chosen); });
        return;
    }
    if (owner->frozen) { say ("Unfreeze " + String (owner->name) + " to consolidate its clips.", true); return; }
    const Id tid = owner->id;
    if (name.empty()) name = owner->name;
    const auto out = recordingsFolder().getNonexistentChildFile (String (name) + " consolidated", ".wav");
    double peak = 1;
    String error;
    if (! engine.renderClips (project, tid, chosen, from, to, projectFolder(), out, peak, error))
    {
        say ("The clips couldn't be consolidated. " + error, true);
        return;
    }
    const double seconds = project.tempo.secondsAt (to) - project.tempo.secondsAt (from);
    const double rate = engine.sampleRate();
    Id made = 0;
    apply ("Consolidate", [&] (Project& p)
    {
        edit::removeClips (p, chosen);
        Clip c;
        c.kind = Clip::Kind::Audio;
        c.name = name;
        c.start = from;
        c.length = std::max<Tick> (1, to - from);
        c.audio.file = out.getFullPathName().toStdString();
        c.audio.sourceLength = seconds;
        c.audio.fileSampleRate = rate;
        c.audio.fileChannels = 2;
        c.audio.gainDb = 20.0 * std::log10 (peak);   // as loud as before
        made = edit::addClip (p, tid, c);
    });
    selectedClip = made;
    selectedClips = { made };
    say ("Consolidated into one clip: a new file, normalised, the clip turned down by "
         + String (-20.0 * std::log10 (peak), 1) + " dB to play as before.", false, true);
}

void Controller::bounceInPlace (const std::set<Id>& chosen)
{
    // By track: each track's clips rendered together, from the first one's start.
    std::map<Id, std::set<Id>> byTrack;
    for (auto cid : chosen)
    {
        const Track* owner = nullptr;
        if (project.clip (cid, &owner) && owner) byTrack[owner->id].insert (cid);
    }
    if (byTrack.empty()) return;
    struct Made { Id track; Tick start; double seconds; std::string file, name; double rate; };
    std::vector<Made> made;
    for (auto& [tid, clips] : byTrack)
    {
        const auto* t = project.track (tid);
        if (! t) continue;
        if (t->frozen) { say ("Unfreeze " + String (t->name) + " to bounce its clips: its plug-ins are unloaded.", true); return; }
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        for (auto cid : clips) if (auto* c = project.clip (cid)) { from = std::min (from, c->start); to = std::max (to, c->end()); }
        const double start = project.tempo.secondsAt (from), end = project.tempo.secondsAt (to);
        const auto out = recordingsFolder().getNonexistentChildFile (String (t->name) + " bounce", ".wav");
        String error;
        if (! renderTrackAudio (tid, clips, start, end, 4.0, out, error)) { say ("The clips couldn't be bounced. " + error, true); return; }
        // Its tail kept to where it goes quiet, never shorter than the clips.
        double seconds = end - start, rate = engine.sampleRate();
        {
            AudioFormatManager fm;
            fm.registerBasicFormats();
            if (std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (out)); r)
            {
                rate = r->sampleRate;
                AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                int last = 0;
                for (int i = b.getNumSamples(); --i >= 0;)
                {
                    bool loud = false;
                    for (int ch = 0; ch < b.getNumChannels(); ++ch) loud |= std::abs (b.getSample (ch, i)) > 1.0e-4f;
                    if (loud) { last = i + 1; break; }
                }
                seconds = jmax (seconds, (double) last / rate);
            }
        }
        made.push_back ({ tid, from, seconds, out.getFullPathName().toStdString(), t->name + " Bounce", rate });
    }
    std::set<Id> newClips;
    apply ("Bounce in Place", [&] (Project& p)
    {
        for (auto& m : made)
        {
            const auto* src = p.track (m.track);
            if (! src) continue;
            const Track source = *src;
            const Id nt = edit::addTrack (p, TrackKind::Track, m.name, m.track, source.parent);
            if (auto* t = p.track (nt))
            {
                t->volumeDb = source.volumeDb;
                t->pan = source.pan;
                t->output = source.output;
                t->sends = source.sends;
                t->colour = source.colour;
            }
            Clip c;
            c.kind = Clip::Kind::Audio;
            c.name = m.name;
            c.start = m.start;
            c.length = std::max<Tick> (1, p.tempo.tickAt (p.tempo.secondsAt (m.start) + m.seconds) - m.start);
            c.audio.file = m.file;
            c.audio.sourceLength = m.seconds;
            c.audio.fileSampleRate = m.rate;
            c.audio.fileChannels = 2;
            newClips.insert (edit::addClip (p, nt, c));
            for (auto cid : byTrack[m.track]) if (auto* x = p.clip (cid)) x->muted = true;
        }
    });
    selectedClips = newClips;
    if (! newClips.empty()) selectedClip = *newClips.begin();
    say ("Bounced to audio on a new track under each, through its plug-ins; the clips are muted, not gone.", false, true);
}

void Controller::chooseTrack (Id id, const ModifierKeys& mods, const std::vector<Id>& order)
{
    auto sel = selectedTracks;
    const auto at = [&order] (Id x) { return std::find (order.begin(), order.end(), x); };
    // Shift-Option-click takes it out of what's chosen.
    if (mods.isShiftDown() && mods.isAltDown())
    {
        sel.erase (std::remove (sel.begin(), sel.end(), id), sel.end());
        if (sel.empty()) { selectedTracks.clear(); sendChangeMessage(); return; }
        selectTracks (sel);
        return;
    }
    if (mods.isShiftDown() && trackAnchor != 0 && at (trackAnchor) != order.end() && at (id) != order.end())
    {
        // From the last one clicked to this one; this one last, so it takes the focus.
        const auto a = at (trackAnchor) - order.begin(), b = at (id) - order.begin();
        std::vector<Id> range;
        for (auto i = a; i != b; i += (b > a ? 1 : -1)) range.push_back (order[(size_t) i]);
        range.push_back (id);
        selectTracks (range);
        return;
    }
    trackAnchor = id;
    const auto it = std::find (sel.begin(), sel.end(), id);
    if (mods.isAltDown())
    {
        if (it != sel.end()) sel.erase (it);
        sel.push_back (id);
    }
    else if (mods.isCommandDown())
    {
        if (it != sel.end()) sel.erase (it);
        else sel.push_back (id);
        if (sel.empty()) { selectedTracks.clear(); sendChangeMessage(); return; }
    }
    else sel = { id };
    selectTracks (sel);
}

bool Controller::optionAdds (Id track)
{
    const auto mods = ModifierKeys::currentModifiers;
    if (! mods.isAltDown()) return false;
    // Option adds it; Shift-Option takes it out.
    chooseTrack (track, ModifierKeys (ModifierKeys::altModifier | (mods.isShiftDown() ? ModifierKeys::shiftModifier : 0)), {});
    return true;
}

std::vector<Id> Controller::arrangeOrder() const
{
    std::vector<Id> order;
    for (auto& t : project.tracks) if (! t.hidden && ! project.hiddenByFolder (t.id)) order.push_back (t.id);
    return order;
}

void Controller::chooseClip (Id id, const ModifierKeys& mods)
{
    const Track* owner = nullptr;
    const auto* clip = project.clip (id, &owner);
    if (! clip || ! owner) return;
    if (mods.isShiftDown() && mods.isAltDown())
    {
        // Shift-Option-click takes it out of what's chosen.
        selectedClips.erase (id);
        if (selectedClip == id) selectedClip = selectedClips.empty() ? 0 : *selectedClips.begin();
        viewChanged();
        return;
    }
    const Track* anchorOwner = nullptr;
    const auto* anchor = clipAnchor != 0 ? project.clip (clipAnchor, &anchorOwner) : nullptr;
    if (mods.isShiftDown() && anchor && anchorOwner)
    {
        // Every clip on the tracks from the anchor's to this one's, from the
        // earlier start to the later end: the rectangle the two make.
        const auto order = arrangeOrder();
        const auto a = std::find (order.begin(), order.end(), anchorOwner->id), b = std::find (order.begin(), order.end(), owner->id);
        if (a != order.end() && b != order.end())
        {
            const Tick from = std::min (anchor->start, clip->start), to = std::max (anchor->end(), clip->end());
            std::set<Id> range;
            for (auto i = std::min (a, b); i <= std::max (a, b); ++i)
                if (const auto* t = project.track (*i))
                    for (auto& x : t->clips())
                        if (x.start < to && x.end() > from) range.insert (x.id);
            selectedClips = range;
            selectedClip = id;
            viewChanged();
            return;
        }
    }
    clipAnchor = id;
    if (mods.isAltDown()) { selectedClips.insert (id); selectedClip = id; viewChanged(); return; }
    if (mods.isCommandDown())
    {
        if (selectedClips.count (id)) selectedClips.erase (id);
        else selectedClips.insert (id);
        viewChanged();
        return;
    }
    if (! selectedClips.count (id) || selectedClip != id) selectClip (id, clip->isMidi());
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
    if (! range.empty())
    {
        const auto r = range;
        range = {};
        apply ("Clear the Range", [r] (Project& p) { tools::clearRange (p, r.tracks, r.from, r.to); });
        return;
    }
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
    // Each audio clip is cut at its own nearest zero crossing when that's on (decision 0054).
    std::map<Id, Edge> cuts;
    for (auto id : clips)
        if (const auto* cl = project.clip (id))
        {
            auto e = zeroEdge (*cl, cursor);
            if (e.tick <= cl->start || e.tick >= cl->end()) e = { cursor, 0 };
            cuts[id] = e;
        }
    apply ("Split", [cuts] (Project& p) { for (auto& [id, e] : cuts) edit::splitClip (p, id, e.tick, e.fine); });
}

Controller::Edge Controller::zeroEdge (const Clip& clip, Tick at) const
{
    if (! zeroCrossings || clip.isMidi()) return { at, 0 };
    // The audio as it plays (the file, or its warped render), and where `at` falls in it.
    auto audio = engine.cache.forClip (clip, project.tempo, projectFolder());
    if (! audio || audio->buffer.getNumSamples() == 0) return { at, 0 };
    const auto& tempo = project.tempo;
    const bool plain = clip.audio.warp == WarpMode::Off && ! clip.audio.reversed && std::abs (clip.audio.transpose) < 1e-6;
    const double base = (plain ? clip.audio.sourceStart : 0.0) - tempo.secondsAt (clip.start - clip.offset);
    const double rate = audio->sampleRate;
    const int want = (int) std::llround ((base + tempo.secondsAt (at)) * rate);
    // Ten milliseconds either way reaches a crossing of anything above 50 Hz.
    const int z = audiotools::nearestZeroCrossing (audio->buffer.getArrayOfReadPointers(), audio->buffer.getNumChannels(),
                                                   audio->buffer.getNumSamples(), want, (int) (rate * 0.01));
    const double song = z / rate - base;
    const Tick tick = tempo.tickAt (song);
    return { tick, song - tempo.secondsAt (tick) };
}

autocc::Found Controller::autoCcFor() const
{
    const Track* owner = nullptr;
    const auto* cl = project.clip (selectedClip, &owner);
    if (! cl || ! cl->isMidi() || ! owner) return {};
    return autocc::familyFor (*owner);
}

void Controller::autoCc()
{
    const auto found = autoCcFor();
    if (found.family == autocc::Family::None)
    {
        say ("Auto needs a track named for its instrument, such as Violins, Horns or Flute (or an instrument named so).");
        return;
    }
    const Id cid = selectedClip;
    apply ("Auto CC", [&] (Project& p) { autocc::fill (p, cid, found.family); });
    say ("Auto: CC1 (dynamics), CC11 (expression), CC7 (volume) and CC21 (vibrato) drawn as AutoCC plays "
         + String (autocc::familyName (found.family)) + ", from " + String (found.from).quoted() + ".", false, true);
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
void Controller::addPlugin (Id track, const PluginDescription& d, std::optional<DeviceRole> as, int position)
{
    const auto ref = PluginManager::refFor (d);
    const auto role = as ? *as : engine.plugins().roleOf (d);
    if (const auto* t = project.track (track); t && t->isCue() && role != DeviceRole::AudioFx)
    {
        say ("The Cue track takes audio effects only: nothing plays into it but the master.", true);
        return;
    }
    Id made = 0;
    apply ("Add " + d.name, [&] (Project& p) { made = edit::addDevice (p, track, ref, role, position); });
    if (made && ! engine.instance (made))
        say (d.name + " couldn't be loaded.", true);
    zoneTab = ZoneTab::Devices;
    sendChangeMessage();
}

// A Lua tool's block, into the song (decision 0068): one part on the
// device's own track at the playhead; several, a new track each under it,
// named for its instrument. One undo step either way.
bool Controller::placeToolBlock (AudioPluginInstance& from, const ToolBlock& b)
{
    Id track = 0;
    for (auto& t : project.tracks)
        for (auto& d : t.devices)
            if (engine.instance (d.id) == &from) track = t.id;
    if (track == 0 || b.parts.empty()) return false;

    const Tick at = playhead();
    const Tick length = std::max<Tick> (PPQ / 4, (Tick) std::llround (b.beats * PPQ));
    auto clipOf = [&] (const ToolBlock::Part& part, const std::string& name)
    {
        Clip c;
        c.kind = Clip::Kind::Midi;
        c.start = at;
        c.length = length;
        c.name = name;
        for (const auto& n : part.notes)
        {
            Note note;
            note.start = (Tick) std::llround (n.start * PPQ);
            note.length = std::max<Tick> (1, (Tick) std::llround (n.length * PPQ));
            note.pitch = n.pitch;
            note.velocity = n.velocity;
            note.channel = n.channel;
            c.midi.notes.push_back (note);
        }
        edit::sortNotes (c.midi);
        return c;
    };

    Id made = 0;
    const auto label = String (from.getName()) + ": " + String (b.name);
    apply (label, [&] (Project& p)
    {
        if (b.parts.size() == 1) { made = edit::addClip (p, track, clipOf (b.parts.front(), b.name)); return; }
        const auto* t = p.track (track);
        const Id parent = t ? t->parent : 0;
        Id after = track;
        for (const auto& part : b.parts)
        {
            after = edit::addTrack (p, TrackKind::Track, part.name, after, parent);
            const Id c = edit::addClip (p, after, clipOf (part, b.name + " - " + part.name));
            if (made == 0) made = c;
        }
    });
    if (made == 0) return false;
    selectClip (made);
    return true;
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

void Controller::setSplitterOutputs (Id device, int count)
{
    count = jlimit (1, NoteSplit::maxOutputs, count);
    apply ("Note Splitter Outputs", [&] (Project& p) { if (auto* d = p.device (device)) d->midiTo.resize ((size_t) count, 0); });
}

void Controller::setSplitterTarget (Id device, int output, Id track)
{
    apply ("Note Splitter Output", [&] (Project& p)
    {
        if (auto* d = p.device (device); d && output >= 0 && output < (int) d->midiTo.size()) d->midiTo[(size_t) output] = track;
    });
}

void Controller::makeSplitterTracks (Id device)
{
    Track* owner = nullptr;
    if (! project.device (device, &owner) || ! owner) return;
    const Id own = owner->id;
    int made = 0;
    apply ("New Tracks for the Note Splitter", [&] (Project& p)
    {
        Track* o = nullptr;
        auto* d = p.device (device, &o);
        if (! d || ! o) return;
        const std::string base = o->name;
        const Id parent = o->parent;
        Id after = own;
        for (size_t k = 0; k < d->midiTo.size(); ++k)
        {
            if (p.track (d->midiTo[k])) { after = d->midiTo[k]; continue; }
            const Id t = edit::addTrack (p, TrackKind::Track, base + " " + std::to_string (k + 1), after, parent);
            if (auto* dd = p.device (device)) dd->midiTo[k] = t;
            after = t;
            ++made;
        }
    });
    if (made > 0) say ("Made " + String (made) + (made == 1 ? " track" : " tracks") + " for the Note Splitter's outputs. Give each an instrument.", false, true);
}

void Controller::setDeviceActive (Id device, bool active)
{
    const auto state = active ? std::string() : engine.stateOf (device);
    // Its delay stays where it was (0086), so switching it off doesn't pull its track early.
    auto* inst = active ? nullptr : engine.instance (device);
    const int latency = inst ? inst->getLatencySamples() : 0;
    const double rate = engine.sampleRate();
    apply (active ? "Activate Plug-in" : "Deactivate Plug-in", [=] (Project& p) { edit::setDeviceActive (p, device, active, state, latency, rate); });
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
    auto w = audiofile::floatWav (f, engine.sampleRate(), 2);   // decision 0079
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

Tick Controller::arrangeSnap (Tick t) const
{
    if (! snap || arrangeGrid <= 0) return t;
    const int bar = project.tempo.barAt (t);
    const Tick start = project.tempo.barStart (bar);
    return start + (Tick) std::llround ((double) (t - start) / (double) arrangeGrid) * arrangeGrid;
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
    String said;
    if (! runScriptCode (f.loadFileAsString(), f.getFileNameWithoutExtension(), said)) { say (f.getFileNameWithoutExtension() + ": " + said, true); return; }
    if (said.isNotEmpty()) say (said);
}

// A script's text run as one undo step named `name`: false (and why, in `said`)
// when it failed; what it printed in `said` when it ran (0019, and agents, 0099).
bool Controller::runScriptCode (const String& code, const String& name, String& said)
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
    const auto r = script::run (project, code.toStdString(), name.toStdString(), ctx);
    if (! r.ok) { said = String (r.error); return false; }
    if (edit::overTrackLimit (project) && project.tracks.size() > before.tracks.size())
    {
        project = std::move (before);
        refuseTooManyTracks();
        sendChangeMessage();
        said = "It would make more than 1000 tracks.";
        return false;
    }
    selectedClip = r.after.selectedClip;
    selectedNotes = r.after.selectedNotes;
    if (r.changed)
    {
        history.push (before, name.toStdString());
        dirty = true;
        syncEngine();
    }
    said = String (r.printed).trim();
    sendChangeMessage();
    return true;
}

//==============================================================================
// The song as saved: with how long it has been worked on so far.
const Project& Controller::stampedForSaving()
{
    project.workSeconds = workClock.active;
    return project;
}

void Controller::beginWorkClock()
{
    workClock.begin (project.workSeconds, Time::getMillisecondCounterHiRes() / 1000.0);
    activity = false;
}

void Controller::tickWorkClock (double now)
{
    // A note played on a MIDI keyboard counts as much as a click.
    const auto heard = engine.notesPlayedIn.load();
    if (heard != midiHeard) { midiHeard = heard; activity = true; }
    workClock.tick (now, activity, engine.isPlaying(), engine.isRecording());
    activity = false;
}

void Controller::timerCallback()
{
    tickWorkClock (Time::getMillisecondCounterHiRes() / 1000.0);
    if (reaperScripts) reaperScripts->sync();   // MIDI Production Center's loader while there's one (0093)
    // Every three minutes, while not recording.
    const auto now = Time::getMillisecondCounter();
    if (now - lastAutosave > 180000 && ! wasRecording)
    {
        lastAutosave = now;
        autosave();
    }
    // A backup every 15 minutes, while it has changed (decision 0073).
    if (lastBackup == 0) lastBackup = now;
    if (now - lastBackup > (uint32) backupMinutes * 60000 && ! wasRecording)
    {
        lastBackup = now;
        if (dirty) backup();
    }
    // Follow playback: the cursor stays where Play started; the view follows the playhead.
    static bool lastPlaying = false;
    const bool playing = engine.isPlaying();
    if (playing != lastPlaying)
    {
        lastPlaying = playing;
        activity = true;
        if (! playing && wasRecording) finishRecording();
        if (onTransportChanged) onTransportChanged();
    }
}

} // namespace kal
