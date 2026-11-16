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
#include "ControlSettings.h"
#include "Patterns.h"
#include "Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

enum class EditorKind { PianoRoll, Drums, Steps, Euclid, Notation };
enum class ZoneTab { Devices, Editor, Mixer, Launcher, Meters };

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
    bool showSectionLane = true;
    bool ripple = false;            // deleting clips pulls later ones back (decision 0037)

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
    void freeze (Id track, bool);   // render the track with its plug-ins into audio, and unload them
    Id newMidiClip (Id track, Tick start, Tick length);
    void deleteSelection();
    void duplicateSelection();
    void splitAtCursor();
    void quantiseSelection();
    void selectAllNotes();

    //==========================================================================
    // Audio (decision 0036; AudioEditing.cpp).
    juce::File fileOf (const Clip&, bool rendered = true) const;
    static bool readAudio (const juce::File&, juce::AudioBuffer<float>&, double& rate);
    // A sample edit on [start, end) of the clip's file (its own samples): the
    // result goes into a new file the clip then plays, so Undo brings the old one back.
    bool editAudio (Id clip, const juce::String& label, int start, int end,
                    std::function<void (juce::AudioBuffer<float>&, int start, int end)>);
    void normaliseClip (Id clip);
    void stripSilence (Id clip, double thresholdDb = -48.0);
    void quantiseAudio (Id clip, double strength = 1.0);
    std::vector<double> hitsIn (Id clip, double sensitivity = 0.5);   // seconds into its file

    //==========================================================================
    // The clip launcher (decision 0037): not undo steps.
    void launchSlot (Id track, int slot);   // -1 stops the track
    void launchScene (int scene);
    void backToArrangement();

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
    // A control moved by a hand while playing writes automation in Touch,
    // Latch, Write and Trim. `value` 0-1; gesture 1 began, 0 a change, 2 ended.
    void writeAutomation (Id track, const AutoTarget&, double value, int gesture);
    std::vector<AutoPoint> automationClipboard;   // copied points (decision 0038)
    Tick automationClipboardLength = 0;
    void automateLastTouched();
    void learnLastTouched();             // MIDI learn: the next knob moved is mapped to it
    void mapped (const MidiMapping&, double value);
    void forgetMapping (size_t index);
    void modulateLastTouched (ModKind);

    //==========================================================================
    // Snapping.
    Tick snapTick (Tick) const;       // nearest grid line, from the bar line
    Tick snapDown (Tick) const;

    // The studio's sync and control surface settings (decisions 0034, 0035).
    ControlSettings studio;
    juce::File studioFile() const;
    void setStudio (const ControlSettings&);
    std::function<void()> onStudioChanged;   // the surfaces reconfigure

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
    struct Writing { Id track = 0, lane = 0; Tick lastTick = 0; bool on = false; Project before; double trimFrom = -1; Tick trimStart = 0; };
    void finishWriting();
    Writing writing;

    void syncEngine();
    std::map<Id, Track::Launch> launchStates() const;
    void restoreLaunches (const std::map<Id, Track::Launch>&);
    std::vector<song::TrackMix> lastMix;   // the mix as last synced, for grouped tracks
    void timerCallback() override;
    void finishRecording();
};

} // namespace kal
