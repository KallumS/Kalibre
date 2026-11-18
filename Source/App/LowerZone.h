/*
    LowerZone - under the tracks: the chosen track's devices, the chosen
    clip's editor (piano roll, drums, steps, Euclidean, notation), or the
    mixer. One at a time, chosen along its top; the divider above it sets its
    height, and with it the size of every plug-in window shown (decision 0002).
*/

#pragma once

#include "Controller.h"
#include "DeviceRack.h"
#include "LauncherView.h"
#include "MetersView.h"
#include "Mixer.h"
#include "VideoView.h"
#include "OfflineFxView.h"
#include "NotationView.h"
#include "PatternEditors.h"
#include "PianoRoll.h"
#include "SampleEditor.h"
#include "Theme.h"

namespace kal
{

class LowerZone : public juce::Component, private juce::ChangeListener
{
public:
    explicit LowerZone (Controller&);
    ~LowerZone() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    PianoRoll& pianoRoll() { return roll; }

private:
    Controller& c;
    theme::HouseButton devicesTab { "Devices", "The chosen track's plug-ins. (D)" };
    theme::HouseButton editorTab { "Editor", "The chosen clip's notes. (E)" };
    theme::HouseButton mixerTab { "Mixer", "Every track's volume, pan and sends. (X)" };
    theme::HouseButton launcherTab { "Launcher", "Clips to play in loops, scene by scene. (G)" };
    theme::HouseButton metersTab { "Meters", "Spectrum, oscilloscope, phase and a tuner. (Y)" };
    theme::HouseButton videoTab { "Video", "The song's video, following the playhead." };
    theme::HouseButton rollTab { "Piano Roll", "Notes on a piano roll." };
    theme::HouseButton drumsTab { "Drums", "A grid of drum hits." };
    theme::HouseButton stepsTab { "Steps", "A step sequencer: one note a step." };
    theme::HouseButton euclidTab { "Euclidean", "Rhythms spread evenly over a number of steps." };
    theme::HouseButton notationTab { "Notation", "The notes on a staff." };
    theme::HouseButton gridButton { "1/16", "The grid: what notes and clips snap to, and how long a new note is." };
    theme::HouseButton snapButton { "Snap", "Snap to the grid. Hold Command while dragging to move freely.", true };
    theme::HouseButton quantise { "Quantise", "Move the chosen notes (or all of them) onto the grid. (Q)" };
    theme::HouseButton legato { "Legato", "Lengthen the chosen notes up to the next one." };
    theme::HouseButton stepButton { "Step Input", "Hold keys on a MIDI keyboard, press 1 to 8 for the length (9 dotted, 0 triplet), let go: they're written at the cursor.", true };
    theme::HouseButton selectButton { juce::String::fromUTF8 ("Select \xe2\x96\xbe"), "Choose notes by what they are: the chords' roots, thirds or fifths, the notes on the beat, the top line." };
    theme::HouseButton ccLanes { ui::u8 ("CC Lanes\xe2\x80\xa6"), "Choose the controller lanes shown under the notes: modulation, expression, volume, any CC." };
    theme::HouseButton autoCc { "Auto", "Draw the CC lanes as AutoCC would play these notes." };
    void updateAuto();

    DeviceRack devices;
    PianoRoll roll;
    DrumGrid drums;
    juce::Viewport drumsView, euclidView;
    StepSequencer steps;
    EuclidEditor euclid;
    NotationView notation;
    Mixer mixer;
    SampleEditor sample;
    LauncherView launcher;
    OfflineFxView offline;
    MetersView meters;
    VideoView videoView;
    bool shownAudio = false;
    bool audioChosen() const;

    ZoneTab shownTab = ZoneTab::Devices;
    EditorKind shownEditor = EditorKind::PianoRoll;
    void show();
    void gridMenu();
    void selectMenu();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
