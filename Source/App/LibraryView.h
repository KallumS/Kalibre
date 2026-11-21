/*
    LibraryView - the browser's Sounds tab (decision 0094): the samples and
    MIDI files in the folders the musician chose, to hear, mark as
    favourites and bring into the song.

        ┌───────────────────────────────┐
        │ Folders | All Items | ★ Only  │  how the list is shown
        │ A to Z ▾ | Sync | Folders…    │  its order; previews at the song's tempo
        │ scanning, or how many         │
        │ ┌───────────────────────────┐ │
        │ │ name, length, tempo       │ │  the chosen one: its waveform,
        │ │ ▁▃▇▅▃▂▁ (or its notes)    │ │  or its notes, and the playhead
        │ └───────────────────────────┘ │
        │ ▾ Samples              412    │  folders open and shut
        │   ☆ Kick 01      ▁▇▃▁         │  click to hear it; ☆ to keep it
        └───────────────────────────────┘

    Click a sound to hear it, then Up and Down to hear the next; Left and
    Right shut and open folders; Return puts it on the chosen track at the
    cursor. Drag it onto a track, or right-click it for a new track with
    Kalibre Sampler or Kalibre Synth.
*/

#pragma once

#include "Controller.h"
#include "LibraryScanner.h"
#include "PreviewPlayer.h"
#include "Theme.h"

namespace kal
{

class LibraryView : public juce::Component, private juce::Timer
{
public:
    explicit LibraryView (Controller&);
    ~LibraryView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void setFilter (const juce::String&);
    // Where the settings and the index live: Kalibre's own folder, or another (tests).
    static juce::File defaultStore();
    void useStore (const juce::File&);

    library::Settings& settings() { return s; }
    void saveSettings();
    void settingsChanged();                      // the folders changed: save, list again, scan
    void rescan();
    LibraryScanner& scanner() { return *scan; }
    PreviewPlayer& player() { return *preview; }

    // Asks for folders and adds them (a chooser, then `then`).
    void chooseFolders (std::function<void()> then = {});
    // The first time Kalibre opens: which folders to look in (asked once).
    void askOnFirstRun (std::function<void()> chose);
    std::function<void()> onEditFolders;         // opens the Library Folders window

    // What's shown, and doing what a click or a key does (also for tests).
    const std::vector<library::Row>& shown() const { return rows; }
    juce::ListBox& list() { return listBox; }
    juce::Button& autoPlayToggle() { return autoPlayButton; }
    void refreshRows();
    // Chooses the row's sound and, with Auto-Play on (or `play`), plays it.
    void hear (int row, bool play = false);
    void playChosen();                           // the chosen sound, or stop it if it's playing
    void toggleOpen (int row);
    void toggleFavourite (const std::string& path);
    enum class NewTrack { Sampler, Synth, Clip };
    bool newTrackFrom (const std::string& path, NewTrack);
    bool putAtCursor (const std::string& path);
    const library::Item* item (const std::string& path) const;
    drumsound::Verdict drumOf (const library::Item& it) { return drumCache.of (it); }
    std::string chosenPath() const;

private:
    Controller& c;
    juce::File store;
    library::Settings s;
    std::unique_ptr<LibraryScanner> scan;
    std::unique_ptr<PreviewPlayer> preview;
    std::vector<library::Row> rows;
    juce::String filter;
    juce::String lastStatus;

    theme::HouseButton foldersView { "Folders", "Show the sounds in their folders." };
    theme::HouseButton flatView { "All Items", "Every sound in one list, without folders." };
    theme::HouseButton favouritesOnly { juce::String::fromUTF8 ("\xe2\x98\x85 Only"), "Show only favourites: sounds and folders marked with a star.", true };
    theme::HouseButton drumsButton { juce::String::fromUTF8 ("Drums \xe2\x96\xbe"), "Show only drum sounds, or one kind of them: kicks, snares, hats... "
                                                                                    "A drum is short, starts with a hit and dies away; its kind is read from its name or folder, "
                                                                                    "or heard when they don't say.", true };
    drumsound::Cache drumCache;          // each sound's verdict, kept between filterings (0106)
    void drumsMenu();
    theme::HouseButton orderButton { juce::String::fromUTF8 ("A to Z \xe2\x96\xbe"), "The order: A to Z, or shuffled (within each folder, or the whole list)." };
    theme::HouseButton syncButton { "Sync", "Previews play at the song's tempo: MIDI always, a sample when its tempo is known "
                                            "(in its name, like \"Drums 96bpm\", or in the file). Off: as they were made.", true };
    theme::HouseButton autoPlayButton { "Auto-Play", "Choosing a sound plays it. Off: choosing is silent; click the sound's picture above the list to hear it.", true };
    theme::HouseButton editFolders { juce::String::fromUTF8 ("Folders\xe2\x80\xa6"), "Choose the folders Kalibre looks in for sounds, or take one out." };
    theme::HouseButton chooseButton { juce::String::fromUTF8 ("Choose Folders\xe2\x80\xa6"), "Choose the folders with your samples and MIDI files." };
    std::unique_ptr<juce::FileChooser> chooser;

    struct Strip;
    struct Model;
    struct List;
    std::unique_ptr<Strip> strip;
    std::unique_ptr<Model> model;
    std::unique_ptr<juce::ListBox> listPtr;
    juce::ListBox& listBox;

    double songBpm() const;
    void orderMenu();
    void itemMenu (int row);
    void folderMenu (int row);
    void timerCallback() override;
    juce::String status() const;
    std::unique_ptr<juce::ListBox> makeList();
    friend struct Model;
    friend struct List;
};

} // namespace kal
