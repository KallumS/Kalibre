/*
    Controller - the project, its undo history, what is chosen, and every
    command the window can give. Every view reads from it and asks it for
    changes; none of them edits the project itself.

    A change goes through `apply`, which keeps the project as it was for
    Undo, makes the change, tells the engine (sync) and repaints whatever
    listens. One gesture is one call, so one undo step (decision 0008).
*/

#pragma once

#include "WorkClock.h"
#include "Edit.h"
#include "AutoCc.h"
#include "Engine.h"
#include "ControlSettings.h"
#include "Patterns.h"
#include "NoteSelect.h"
#include "StepInput.h"
#include "Song.h"
#include "Keys.h"
#include "Toolbox.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{
// VST3 effects once filed as MIDI effects, put right on opening (decision 0058).
void refileEffects (Project&, const PluginManager&);


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
    Tick grid = PPQ / 4;            // the editors' grid (the lower zone's Grid)
    Tick arrangeGrid = PPQ / 4;     // the arrangement's (the top bar's Grid, 0070)
    bool snap = true;
    bool followPlayback = true;
    bool followPages = false;       // Shift-click Follow: turn the page at the edge, not scroll (0070)
    juce::StringArray hiddenInTopBar;   // the top bar's items hidden by right-click (0070)
    int cpuEverySeconds = 2;        // how often the CPU reading changes
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
    // The tools (decision 0092): what a left click is, in the arrangement and
    // in the piano roll, chosen from the toolbox (Control-right-click). Not saved.
    tools::State arrangeTool, rollTool;
    tools::State& toolsFor (tools::Area a) { return a == tools::Area::Arrange ? arrangeTool : rollTool; }
    void chooseTool (tools::Area, tools::Tool, std::optional<int> mode = std::nullopt);
    void chooseToolEverywhere (tools::Tool);   // a tool's key: in each area that has it
    // The Range tool's stretch of time on some tracks: Delete clears what's in it.
    struct Range
    {
        std::vector<Id> tracks;
        Tick from = 0, to = 0;
        bool empty() const { return tracks.empty() || to <= from; }
    };
    Range range;

    //==========================================================================
    // Changes.
    // False when refused (more than 1000 tracks, decision 0088): nothing changed.
    bool apply (const juce::String& label, std::function<void (Project&)> change);
    // A drag that showed its change in the project as it went (a tool's): the
    // project as it was before is put back, and the change made with apply.
    bool applyLive (const Project& before, const juce::String& label);

    // The top bar's timer (decision 0091): how long the user has made music in this
    // song, DAW Battle Pass's active-time clock. The window tells it when anything
    // happens; the Controller's timer ticks it; it goes into the song when saved.
    WorkClock workClock;
    void noteActivity() { activity = true; }
    void tickWorkClock (double nowSeconds);
    void beginWorkClock();      // a song opened or started: its own time so far
    const Project& stampedForSaving();
    bool activity = false;
    juce::uint32 midiHeard = 0;
    void refuseTooManyTracks();
    juce::uint32 refusedAt = 0;
    // The project changed in a way the engine needs to hear about but that
    // isn't an undo step (a fader being dragged): sync and repaint.
    void refresh();
    // Only the view changed (selection, zoom): repaint.
    void viewChanged() { sendChangeMessage(); }

    bool undo();
    bool redo();

    //==========================================================================
    // The message line under the transport (decisions 0025, 0070): what just
    // happened, until something else is chosen or pointed at (a warning,
    // until something else is chosen, or it's clicked); then what's chosen,
    // or what the pointer is over when nothing is.
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

    // Step input (decision 0076): keys held on a MIDI keyboard, a number for the
    // length, written at the cursor as they're let go, the cursor moving on.
    bool stepInput = false;
    StepInput stepper;
    void setStepInput (bool on);
    void stepKey (int pitch, int velocity, bool on);
    bool stepDigit (int digit);     // false when step input is off
    bool stepRest();                // the - key: a rest as long as the length chosen
    void sayStepInput();

    // Choose the chosen clip's notes by what they are (decision 0075); `only`, one editor's.
    void selectNotes (noteselect::What, std::optional<NoteFrom> only = std::nullopt);
    // Each voice of the clips' chords to a track of its own, one undo step (0075).
    void explodeChords (const std::set<Id>& clips);

    // Articulation maps (decision 0072): a library kept between songs, read
    // from Cubase, Logic or Reaticulate maps in the Articulation Manager.
    juce::File articulationFolder;             // empty: beside the plug-in list (tests set it)
    juce::File articulationLibraryFile() const;
    std::vector<ArticulationMap> articulationLibrary() const;
    void saveArticulationLibrary (const std::vector<ArticulationMap>&);
    // Each file's maps into the library (one of the same name replaced); says what came in.
    int importArticulationMaps (const juce::Array<juce::File>&);
    // The map on the track, copied into the song first if it isn't there. One undo step.
    void useArticulationMap (Id track, const ArticulationMap&);
    // Articulations' own delays (decision 0082), set in the Articulation Manager: in the
    // library's map of that name and the song's (one undo step). False if none was found.
    bool setArticulationDelay (const std::string& map, const std::set<int>& articulations, double ms);
    void showArticulationManager() { if (onShowArticulationManager) onShowArticulationManager(); }
    std::function<void()> onShowArticulationManager;     // the window sets this
    // What the pointer is over, told by the arrangement and the piano roll.
    Id hoveredClip = 0, hoveredNote = 0;
    void hover (Id clip, Id note = 0) { hoveredClip = clip; hoveredNote = note; }
    // What's chosen (notes, else clips), or else what the pointer is over, in words.
    juce::String infoLine() const;
    juce::String describeClip (const Clip&) const;
    juce::String describeNotes (const Clip&, const std::set<Id>& notes) const;
    // Lets a message give way to the info once the choice or the pointer moves on.
    juce::String messageChoice, messagePointer;
    juce::String choiceKey() const;
    juce::String pointerKey() const { return juce::String ((juce::int64) hoveredClip) + ":" + juce::String ((juce::int64) hoveredNote); }
    // Called often (the top bar's timer): lets a message go once things moved on.
    void retireMessage();

    //==========================================================================
    // Transport.
    void togglePlay();
    void play();
    void stop();
    void record();
    // Arms a track (decisions 0064, 0065): for audio (click, red), MIDI
    // (right-click, blue: what its instrument is played, arpeggiators and MIDI
    // effects included), both (Shift-click) or MIDI input (Shift-right-click).
    // The same again disarms it; another kind switches to it.
    void arm (Id track, ArmFor);
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
    // Backups (decision 0073): every 15 minutes while it has changed, a copy
    // of its own, dated, in the project folder's Backups (or Kalibre's own,
    // for a song not yet saved); the newest 48 kept.
    juce::File backupsFolder() const;
    juce::File backupsRoot;                    // empty: Application Support/Kalibre/Backups (tests set it)
    juce::Array<juce::File> backups() const;   // newest first
    bool backup();                             // false when nothing changed since the last
    bool openBackup (const juce::File&);       // as this song, unsaved
    static constexpr int backupMinutes = 15, backupsKept = 48;
    juce::File recordingsFolder() const;
    // Audio or MIDI. With `makeTrack`, it runs first, in the same undo step, and
    // the clip goes on the track it returns (the browser's New Track with..., 0094).
    bool importFile (const juce::File&, Id track, Tick at, std::function<Id (Project&)> makeTrack = {}, const juce::String& label = {},
                     bool splitAtTransients = false);
    // A sample on the timeline split into a clip for each hit (0095): one undo step.
    bool importSplit (const juce::File& f, Id track, Tick at) { return importFile (f, track, at, {}, "Import Split at Transients", true); }
    // The tempo an audio file was made at: from its name ("Drums 96bpm") or its ACID chunk; 0 when unknown.
    static double tempoOfAudioFile (const juce::File&);
    // A device's printed sound (a JSFX's export_buffer_to_project) on a new track under its own.
    bool printFromDevice (juce::AudioPluginInstance&, const juce::AudioBuffer<float>&, double sampleRate);
    bool importVideo (const juce::File&, Tick at);           // the picture, and its sound on a track (decision 0046)
    bool exportMidi (const juce::File&);
    // MusicXML (decision 0057): the chosen tracks (or every track with MIDI) out;
    // a score (.musicxml, .xml or zipped .mxl) in, a track for each part.
    bool exportMusicXml (const juce::File&);
    bool importMusicXml (const juce::File&);
    // DAWproject (decision 0077): the song as a .dawproject for Bitwig, Studio One,
    // Cubase and others, its audio inside; and one opened as a new song.
    bool exportDawProject (const juce::File&);
    bool importDawProject (const juce::File&);
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
    struct Batch { bool mixWav = true, mixMp3 = true, stems = false, midi = false; double loudness = 0;
                   bool ringOut = true, secondPass = false; };   // Record the Tail, Export the Second Pass (0097)
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
    // Freeze (decision 0080): the track rendered, quietly, through its own plug-ins to a
    // 32-bit float WAV, and its plug-ins unloaded with their states kept. Unfreezing
    // deletes the render and loads them again as they were.
    void freeze (Id track, bool);
    // Bounce in Place (decision 0080): the clips rendered through their track's plug-ins
    // onto a new audio track under it, made like it; the clips muted. One undo step.
    void bounceInPlace (const std::set<Id>& clips);
    // Clips on one track into one (decision 0081): MIDI clips' notes into one clip;
    // audio clips into a new file, normalised, the clip's gain making up for it.
    void consolidate (const std::set<Id>& clips);
    // The track alone through its own plug-ins at unity (its clips, or just `onlyClips`), from
    // `start` to `end` seconds and `tail` more, into a 32-bit float WAV. Nothing else is unloaded.
    bool renderTrackAudio (Id track, const std::set<Id>& onlyClips, double start, double end, double tail,
                           const juce::File& out, juce::String& error, bool instrumentsOnly = false,
                           std::function<void (Project&)> adjust = {});   // changes to the copy rendered, never the song

    // Fix Note Timings and Volume (decision 0085): Note Position & Volume Fixer,
    // built in. The clip's track rendered quietly (its instruments, no effects),
    // each note found in the render, the render deleted, and each note moved so
    // its sound lands where it's written and turned up or down to match the
    // other notes of its articulation. One undo step.
    struct NoteFixOptions
    {
        enum class Timing { NoteDelay, MoveNotes, Off } timing = Timing::NoteDelay;
        enum class Volume { Cc7, Cc11, Velocity, Off } volume = Volume::Cc7;
        bool toAttack = true;   // line up the attack; false: the very start of the sound
    };
    bool fixNoteTimingsAndVolume (Id clip, const NoteFixOptions&);
    NoteFixOptions lastNoteFix;   // what the dialog chose last
    // After Undo or Redo: a frozen track whose render was deleted by Unfreeze is rendered again.
    void refreezeMissing();
    Id newMidiClip (Id track, Tick start, Tick length);
    void deleteSelection();
    void duplicateSelection();
    void splitAtCursor();
    void quantiseSelection();
    // AutoCC (decision 0060): the family the chosen MIDI clip's track names,
    // if any; and its CC lanes filled as AutoCC would play the clip.
    autocc::Found autoCcFor() const;
    void autoCc();
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
    int splitAtTransients (Id clip, double sensitivity = 0.5);        // the pieces made (0095)
    void renderOfflineFx (Id clip);

    //==========================================================================
    // The clip launcher (decision 0037): not undo steps.
    void launchSlot (Id track, int slot);   // -1 stops the track
    void launchScene (int scene);
    void backToArrangement();

    //==========================================================================
    // Devices.
    // `position`: where in the track's devices (its role's place still kept); -1, after its kind.
    void addPlugin (Id track, const juce::PluginDescription&, std::optional<DeviceRole> as = {}, int position = -1);
    void addBuiltIn (Id track, const PluginRef&, DeviceRole);
    // The Note Splitter (decision 0083): how many outputs, the track each goes to,
    // and a new track for each output without one. Each one undo step.
    void setSplitterOutputs (Id device, int count);
    void setSplitterTarget (Id device, int output, Id track);
    void makeSplitterTracks (Id device);
    bool placeToolBlock (juce::AudioPluginInstance& from, const struct ToolBlock&);   // decision 0068
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
    Tick snapTick (Tick) const;     // nearest grid line, from the bar line
    Tick snapDown (Tick) const;
    Tick arrangeSnap (Tick) const;  // the same, to the arrangement's grid

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

    // The family's REAPER scripts that hand phrases to Midular and MIDI Production
    // Center, run as they are (decision 0093). The chosen MIDI clips into the device.
    std::unique_ptr<class ReaperScripts> reaperScripts;
    bool loadChosenClipsInto (Id device);

private:
    std::vector<double> taps;
    bool wasRecording = false;
    juce::uint32 lastAutosave = 0, lastBackup = 0;
    juce::String lastBackupText;
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
