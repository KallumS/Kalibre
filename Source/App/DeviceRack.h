/*
    DeviceRack - the chosen track's plug-ins, side by side in the lower zone,
    each showing its own window (decision 0002): FabFilter Volcano, Pro-Q and
    Pro-C next to each other, ready to use, with nothing floating.

    Every plug-in window is fitted to the zone's height: a resizable one is
    resized (keeping its shape), any other is scaled. Dragging the divider
    above the zone resizes them all. If they don't all fit across, the rack
    scrolls sideways.

    Each plug-in has a header: its name; the power switch, which deactivates
    it - unloading it from memory and the processor, keeping its settings
    (decision 0010); bypass; and a menu to move or remove it. The order is
    always MIDI effects, instruments, audio effects (decision 0007).

    On the left, the track's modulators (decision 0012): each with its shape
    and its targets' amounts.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class DevicePanel;
class ModulatorPanel;

class DeviceRack : public juce::Component, private juce::ChangeListener
{
public:
    explicit DeviceRack (Controller&);
    ~DeviceRack() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();

    // A menu of every plug-in, by role, for adding to `track`.
    static void addMenu (Controller&, Id track, juce::Component* target);

private:
    Controller& c;
    juce::Viewport viewport;
    juce::Component strip;
    juce::OwnedArray<DevicePanel> panels;
    juce::OwnedArray<ModulatorPanel> modulators;
    theme::HouseButton add { "+ Add", "Add a plug-in to this track: a MIDI effect, an instrument or an audio effect." };
    Id shownTrack = 0;
    juce::String shownShape;

    void layoutStrip();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
