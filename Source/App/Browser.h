/*
    Browser - the panel on the left (View > Browser): plug-ins, files and
    the audio set-up, inside the window like everything else.

    Presets (decision 0039): the saved presets of the chosen track's
    plug-ins; double-click one to load it.
    Plug-ins: search by name or maker; drag one onto a track, or double-click
    to add it to the chosen track. Sounds (decision 0094): the samples and
    MIDI files in the folders chosen, to hear, keep as favourites, and drag
    onto a track (LibraryView). Audio: the interface, sample rate and buffer size.
    The keys and the studio were tabs here too; they're settings windows of
    their own now (Help > Keys..., File > Studio..., SettingsWindows.h).
*/

#pragma once

#include "Controller.h"
#include "LibraryView.h"
#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace kal
{

class Browser : public juce::Component, private juce::ChangeListener
{
public:
    explicit Browser (Controller&);
    ~Browser() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    // The numbers are saved in workspaces: 3 and 5 (Studio and Keys, now windows) aren't used again.
    enum class Tab { Plugins = 0, Files = 1, Audio = 2, Presets = 4 };
    static Tab tabFromNumber (int);
    void showTab (Tab);
    Tab shownTab() const { return tab; }
    LibraryView& library() { return *sounds; }

private:
    Controller& c;
    theme::HouseButton pluginsTab { "Plug-ins", "Every plug-in found on this Mac." };
    theme::HouseButton filesTab { "Sounds", "Samples and MIDI files from your folders, to hear and drag into the song." };
    theme::HouseButton audioTab { "Audio", "The audio interface, sample rate and buffer size." };
    theme::HouseButton presetsTab { "Presets", "Saved presets for the chosen track's plug-ins." };
    theme::HouseButton scan { ui::u8 ("Scan"), "Look for plug-ins installed since the last scan." };
    juce::TextEditor search;

    struct PluginList;
    struct PresetList;
    std::unique_ptr<PresetList> presets;
    juce::ListBox presetBox;
    std::unique_ptr<PluginList> plugins;
    std::unique_ptr<LibraryView> sounds;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audio;
    // Every performance core, or the audio thread alone (decision 0078).
    theme::HouseButton cores { "Use Every Core", "Play the tracks on all the Mac's performance cores at once, not one after another. "
                                                  "Off: everything on one core, as before.", true };
    juce::ListBox pluginBox;
    Tab tab = Tab::Plugins;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
