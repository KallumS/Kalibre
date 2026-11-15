/*
    LowerZone - under the tracks: the chosen track's devices, the chosen
    clip's editor (piano roll, drums, steps, Euclidean, notation), or the
    mixer. One at a time, chosen along its top; the divider above it sets its
    height, and with it the size of every plug-in window shown (decision 0002).
*/

#pragma once

#include "Controller.h"
#include "DeviceRack.h"
#include "Mixer.h"
#include "NotationView.h"
#include "PatternEditors.h"
#include "PianoRoll.h"
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
    theme::HouseButton rollTab { "Piano Roll", "Notes on a piano roll." };
    theme::HouseButton drumsTab { "Drums", "A grid of drum hits." };
    theme::HouseButton stepsTab { "Steps", "A step sequencer: one note a step." };
    theme::HouseButton euclidTab { "Euclidean", "Rhythms spread evenly over a number of steps." };
    theme::HouseButton notationTab { "Notation", "The notes on a staff." };
    theme::HouseButton gridButton { "1/16", "The grid: what notes and clips snap to, and how long a new note is." };
    theme::HouseButton snapButton { "Snap", "Snap to the grid. Hold Command while dragging to move freely.", true };
    theme::HouseButton quantise { "Quantise", "Move the chosen notes (or all of them) onto the grid. (Q)" };
    theme::HouseButton legato { "Legato", "Lengthen the chosen notes up to the next one." };

    DeviceRack devices;
    PianoRoll roll;
    DrumGrid drums;
    juce::Viewport drumsView, euclidView;
    StepSequencer steps;
    EuclidEditor euclid;
    NotationView notation;
    Mixer mixer;

    ZoneTab shownTab = ZoneTab::Devices;
    EditorKind shownEditor = EditorKind::PianoRoll;
    void show();
    void gridMenu();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
