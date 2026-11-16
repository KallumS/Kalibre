/*
    LauncherView - the clip launcher, in the lower zone (decision 0037).

    A column a track, a row a scene. Click a clip to play it, looping, from
    the next bar instead of the track's arrangement; click a scene's name to
    play the whole row; Stop silences a track, and Back to Arrangement gives
    every track its arrangement again. Playing slots are lit in the accent;
    one waiting for its bar is outlined.

    Capture Scene copies what the arrangement plays inside the loop range
    into a new row. Right-click a slot to edit, rename or empty it, or to put
    the chosen arrangement clip in it.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class LauncherView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit LauncherView (Controller&);
    ~LauncherView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Controller& c;
    theme::HouseButton capture { "Capture Scene", "Copy what plays inside the loop range into a new scene." };
    theme::HouseButton addScene { "Add Scene", "An empty row of slots." };
    theme::HouseButton back { "Back to Arrangement", "Every track plays its arrangement again." };

    std::vector<Id> columns;
    int sceneWidth() const;
    int columnWidth() const;
    int rowHeight() const;
    juce::Rectangle<int> grid() const;
    juce::Rectangle<int> cell (int column, int scene) const;
    void slotMenu (Id track, int scene);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override { repaint(); }
};

} // namespace kal
