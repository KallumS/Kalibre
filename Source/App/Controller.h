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
#include "Keys.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

enum class EditorKind { PianoRoll, Drums, Steps, Euclid, Notation };
enum class ZoneTab { Devices, Editor, Mixer, Launcher, Meters, Video };

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
    bool zeroCrossings = false;     // audio edges snap to zero crossings (decision 0054)

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
    // Looks for plug-ins installed since the last scan, the progress in the
    // message line (decision 0047); `files` scans just those again.
    void scanPlugins (std::vector<std::pair<juce::String, juce::String>> files = {});
    void showPluginManager() { if (onShowPluginManager) onShowPluginManager(); }
    std::function<void()> onShowPluginManager;     // the window sets this
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
    bool importVideo (const juce::File&, Tick at);           // the picture, and its sound on a track (decision 0046)
    bool exportMidi (const juce::File&);
    bool exportAudio (const juce::File&, const Engine::RenderOptions&, bool stems);

    // A project's files (decision 0041; ProjectFiles.cpp).
    juce::File resolve (const std::string& path) const;   // absolute, or from the project's folder
    std::vector<std::string> missingFiles() const;
    int relinkFrom (const juce::File& folder);             // returns how many were found
    bool collectAndSave();                                 // every file into the project's folder, then save
    bool archive (const juce::File& zip);                  // the project and its files in one .zip
    juce::File templatesFolder() const;
    bool saveTemplate (const juce::String& name, bool keepMusic);
    bool newFromTemplate (const juce::File&);
    bool exportXml (const juce::File&);
    bool importXml (const juce::File&);
    // Several exports at once into a folder: the mix as WAV and MP3, every track, the MIDI.
    struct Batch { bool mixWav = true, mixMp3 = true, stems = false, midi = false; double loudness = 0; };
    bool exportBatch (const juce::File& folder, const Batch&);

    //==========================================================================
    // Tracks and clips.
    Id addTrack (TrackKind, const juce::String& name = {});
    void deleteSelectedTracks();
    void duplicateSelectedTracks();
    void folderFromSelection();
    // Clicking to choose (decision 0053), the same wherever tracks or clips are
    // shown: a plain click chooses only this one; Shift-click everything from
    // the last one clicked to this one (in `order`, as that view lists the
    // tracks); Option-click (Alt) adds it; Shift-Option-click takes it out;
    // Command-click adds or takes it away.
    void chooseTrack (Id, const juce::ModifierKeys&, const std::vector<Id>& order);
    void chooseClip (Id, const juce::ModifierKeys&);
    Id trackAnchor = 0, clipAnchor = 0;   // the last clicked without Shift
    // Option-click on a track's button (mute, solo, a version...) adds the
    // track to the selection instead of pressing it. True when it did.
    bool optionAdds (Id track);
    std::vector<Id> arrangeOrder() const;   // the tracks as the arrangement shows them
    // Where an edge of an audio clip goes when it's put at `at` (decision 0054):
    // with Snap to Zero Crossings on, the nearest zero crossing, as a tick
    // and the seconds past it; otherwise `at` itself.
    struct Edge { Tick tick = 0; double fine = 0; };
    Edge zeroEdge (const Clip&, Tick at) const;
    void newVersion (Id track);
    // Chooses version `index` (0-3) on each of these tracks that has it (decision 0050).
    void chooseVersion (const std::vector<Id>& tracks, int index);
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
    void renderOfflineFx (Id clip);

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

    // Presets and A/B (decision 0039; Presets.cpp).
    juce::File presetsFolder (const PluginRef&) const;
    juce::Array<juce::File> presetsFor (const PluginRef&) const;
    bool savePreset (Id device, const juce::String& name);
    bool loadPreset (Id device, const juce::File&);
    void loadProgram (Id device, int index);
    void switchAB (Id device);           // the other of the two settings being compared
    void copyToOther (Id device);        // the one heard now becomes the other too
    void pluginChanged (const Project& before, const juce::String& label);
    Id changedDevice = 0;

    // Offline effects being edited in the Devices tab (decision 0039), or 0.
    Id offlineClip = 0;

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

    // The window's keys (decision 0045): registered by the window, kept with the app.
    Keys keys;

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
    std::map<Id, int> deviceStates() const;
    void reloadChangedStates (const std::map<Id, int>&);
    void restoreLaunches (const std::map<Id, Track::Launch>&);
    std::vector<song::TrackMix> lastMix;   // the mix as last synced, for grouped tracks
    void timerCallback() override;
    void finishRecording();
};

} // namespace kal
