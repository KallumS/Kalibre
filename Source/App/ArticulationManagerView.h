/*
    ArticulationManagerView - File > Articulation Manager... (decision 0072),
    in a window of its own, as the Plug-in Manager (settings may have windows,
    decision 0002 amended).

    The articulation maps kept between songs (the library) and those in this
    song. Import Cubase expression maps, Logic articulation sets and
    Reaticulate banks (what BabylonWaves Art Conductor makes, and REAPER's);
    see what each articulation sends; use a map on the chosen track; take
    maps out of the library. A map is chosen for a track in the piano roll's
    Velocity menu too.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class ArticulationManagerView : public juce::Component, private juce::ChangeListener
{
public:
    explicit ArticulationManagerView (Controller&);
    ~ArticulationManagerView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    struct Row
    {
        ArticulationMap map;
        bool inLibrary = false, inSong = false;
    };
    const std::vector<Row>& shownRows() const { return rows; }
    void choose (int row);

private:
    Controller& c;
    struct Maps : public juce::ListBoxModel
    {
        ArticulationManagerView& owner;
        explicit Maps (ArticulationManagerView& o) : owner (o) {}
        int getNumRows() override { return (int) owner.rows.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int) override { owner.chosenChanged(); }
    } mapsModel { *this };
    struct Arts : public juce::ListBoxModel
    {
        ArticulationManagerView& owner;
        explicit Arts (ArticulationManagerView& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    } artsModel { *this };
    juce::ListBox maps, arts;
    theme::HouseButton importButton { juce::String::fromUTF8 ("Import\xe2\x80\xa6"), "Read Cubase expression maps (.expressionmap), Logic articulation sets (.plist) or Reaticulate banks (.reabank) into the library." };
    theme::HouseButton useButton { "Use on Track", "Use the chosen map on the chosen track: its notes can then be given articulations in the piano roll." };
    theme::HouseButton removeButton { "Remove from Library", "Take the chosen maps out of the library. Songs that use them keep their own copies." };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<Row> rows;
    juce::Rectangle<int> infoArea;
    const Row* chosenRow() const;
    void chosenChanged();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

class ArticulationManagerWindow : public juce::DocumentWindow
{
public:
    explicit ArticulationManagerWindow (Controller&);
    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress&) override;
};

} // namespace kal
