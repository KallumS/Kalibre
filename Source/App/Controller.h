/*
    Controller - the project, its undo history, what is chosen, and every
    command the window can give. Every view reads from it and asks it for
    changes; none of them edits the project itself.

    A change goes through `apply`, which keeps the project as it was for
    Undo, makes the change, tells the engine (sync) and repaints whatever
    listens. One gesture is one call, so one undo step (decision 0008).
*/

#pragma once

#include "Edit.h"
#include "Engine.h"
#include "Patterns.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

enum class EditorKind { PianoRoll, Drums, Steps, Euclid, Notation };
enum class ZoneTab { Devices, Editor, Mixer };

class Controller : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit Controller (Engine&);
    ~Controller() override;

    Engine& engine;
    Project project;
    edit::History history;
    juce::File file;
    bool dirty = false;

    //==========================================================================
    // What is chosen (never saved).
    std::vector<Id> selectedTracks;
    Id focusTrack = 0;              // the track being played and edited
    Id selectedClip = 0;            // the clip in the editor
    std::set<Id> selectedClips;
    std::set<Id> selectedNotes;
    Tick cursor = 0;                // where things go in, and where Play starts
    Tick grid = PPQ / 4;
    bool snap = true;
    bool followPlayback = true;
    EditorKind editor = EditorKind::PianoRoll;
    ZoneTab zoneTab = ZoneTab::Devices;
    bool showChordLane = true;
    bool showTempoLane = false;

    Track* focus() { return project.track (focusTrack); }
    Clip* clip() { return project.clip (selectedClip); }
    void focusOn (Id track);
    void selectClip (Id clip, bool openEditor = true);
    void selectTracks (std::vector<Id>);

    //==========================================================================
    // Changes.
    void apply (const juce::String& label, std::function<void (Project&)> change);
    // The project changed in a way the engine needs to hear about but that
    // isn't an undo step (a fader being dragged): sync and repaint.
    void refresh();
    // Only the view changed (selection, zoom): repaint.
    void viewChanged() { sendChangeMessage(); }

    bool undo();
    bool redo();

    //==========================================================================
    // The message line under the transport (decision 0025): stays until
    // dismissed or replaced, and can carry an Undo.
    juce::String message;
    bool messageIsWarning = false;
    bool messageCanUndo = false;
    void say (const juce::String&, bool warning = false, bool canUndo = false);
    void dismiss();

    //==========================================================================
    // Transport.
    void togglePlay();
    void play();
    void stop();
    void record();
    void locate (Tick);
    void goToStart();
    void toggleLoop();
    Tick playhead() const;
    void tapTempo();

    //==========================================================================
    // Files.
    void newProject();
    bool open (const juce::File&);
    bool save();
    bool saveAs (const juce::File&);
    juce::File projectFolder() const;
    juce::File autosaveFile() const;   // a copy every few minutes, never over the project itself
    void autosave();
    juce::File recordingsFolder() const;
    bool importFile (const juce::File&, Id track, Tick at);   // audio, MIDI
    bool exportMidi (const juce::File&);
    bool exportAudio (const juce::File&, const Engine::RenderOptions&, bool stems);

    //==========================================================================
    // Tracks and clips.
    Id addTrack (TrackKind, const juce::String& name = {});
    void deleteSelectedTracks();
    void duplicateSelectedTracks();
    void folderFromSelection();
    void newVersion (Id track);
    Id newMidiClip (Id track, Tick start, Tick length);
    void deleteSelection();
    void duplicateSelection();
    void splitAtCursor();
    void quantiseSelection();
    void selectAllNotes();

    //==========================================================================
    // Devices.
    void addPlugin (Id track, const juce::PluginDescription&);
    void addBuiltIn (Id track, const PluginRef&, DeviceRole);
    void setDeviceActive (Id device, bool);
    void storeDeviceStates();   // every loaded plug-in's state into the project, before a save

    //==========================================================================
    // Capture (decision 0017).
    void captureMidi();
    void captureAudio (double seconds = 30);

    //==========================================================================
    // Last touched (decision 0024).
    struct Touched
    {
        Id track = 0, device = 0;
        int param = -1;
        AutoTarget::Kind kind = AutoTarget::Kind::DeviceParam;
        juce::String name;
        bool valid() const { return (device != 0 && param >= 0) || (track != 0 && kind != AutoTarget::Kind::DeviceParam); }
        bool isPlugin() const { return kind == AutoTarget::Kind::DeviceParam; }
    };
    Touched lastTouched;
    void touched (const Engine::Touch&);
    // A control of Kalibre's own was moved (a fader, a pan knob).
    void touchedMix (Id track, AutoTarget::Kind, const juce::String& name);
    void automateLastTouched();
    void modulateLastTouched (ModKind);

    //==========================================================================
    // Snapping.
    Tick snapTick (Tick) const;       // nearest grid line, from the bar line
    Tick snapDown (Tick) const;

    // Scripts.
    juce::File scriptsFolder() const;
    void runScript (const juce::File&);

    // The engine's state of mind, for the window.
    std::function<void()> onTransportChanged;

private:
    std::vector<double> taps;
    bool wasRecording = false;
    juce::uint32 lastAutosave = 0;
    double recordFromSeconds = 0;
    struct Writing { Id track = 0, lane = 0; Tick lastTick = 0; bool on = false; Project before; };
    Writing writing;

    void syncEngine();
    void timerCallback() override;
    void finishRecording();
};

} // namespace kal
