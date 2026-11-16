/*
    DeviceRack - the chosen track's plug-ins, side by side in the lower zone,
    each showing its own window (decision 0002): FabFilter Volcano, Pro-Q and
    Pro-C next to each other, ready to use, with nothing floating.

    Every plug-in window is fitted to the zone, its shape kept (decision
    0058): a resizable one is asked to take the zone's height; one that
    can't, or won't, is shown smaller, the same both ways, never stretched
    and never bigger than it is. If they don't all fit across, the rack
    scrolls sideways: its scroll bar, or a two-finger swipe anywhere over
    it. A panel is kept while its plug-in stays, so adding another doesn't
    make its window again (some went blank).

    The window button moves a plug-in's window out into a window of its own,
    and back (decision 0056). Match Loudness keeps an effect's output as loud
    as its input (decision 0056).

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

class PluginWindow;

class DeviceRack : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit DeviceRack (Controller&);
    ~DeviceRack() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();

    // A menu of every plug-in, by role, for adding to `track`.
    static void addMenu (Controller&, Id track, juce::Component* target);

    // A plug-in's window in a window of its own (decision 0056).
    bool isFloating (Id device) const { return windows.count (device) > 0; }
    void float_ (Id device);
    void dock (Id device);
    void relayout() { layoutStrip(); }

    // How wide the rack's view is, and the part of it a panel can be seen in
    // (in the panel's own coordinates).
    int fitWidth() const { return viewport.getMaximumVisibleWidth(); }
    juce::Rectangle<int> shownArea (const juce::Component& panel) const
    {
        return viewport.getViewArea().translated (-panel.getX(), -panel.getY());
    }

private:
    Controller& c;
    struct RackViewport : juce::Viewport
    {
        std::function<void()> moved;
        void visibleAreaChanged (const juce::Rectangle<int>&) override { if (moved) moved(); }
    };
    RackViewport viewport;
    std::shared_ptr<void> swipes;
    juce::Component strip;
    juce::OwnedArray<DevicePanel> panels;
    juce::OwnedArray<ModulatorPanel> modulators;
    theme::HouseButton add { "+ Add", "Add a plug-in to this track: a MIDI effect, an instrument or an audio effect." };
    Id shownTrack = 0;
    juce::String shownShape;
    std::map<Id, std::unique_ptr<PluginWindow>> windows;
    std::map<Id, juce::AudioPluginInstance*> windowFor;

    void layoutStrip();
    void placeEditors();
    void closeStaleWindows();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace kal
