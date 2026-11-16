/*
    Browser - the panel on the left (View > Browser): plug-ins, files and
    the audio set-up, inside the window like everything else.

    Presets (decision 0039): the saved presets of the chosen track's
    plug-ins; double-click one to load it.
    Plug-ins: search by name or maker; drag one onto a track, or double-click
    to add it to the chosen track. Files: audio and MIDI from your folders;
    click to hear it, drag it onto a track or double-click to put it at the
    cursor. Audio: the interface, sample rate and buffer size.
*/

#pragma once

#include "Controller.h"
#include "KeysPanel.h"
#include "StudioPanel.h"
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

    enum class Tab { Plugins, Files, Audio, Studio, Presets, Keys };
    void showTab (Tab);
    Tab shownTab() const { return tab; }

private:
    Controller& c;
    theme::HouseButton pluginsTab { "Plug-ins", "Every plug-in found on this Mac." };
    theme::HouseButton filesTab { "Files", "Audio and MIDI files." };
    theme::HouseButton audioTab { "Audio", "The audio interface, sample rate and buffer size." };
    theme::HouseButton presetsTab { "Presets", "Saved presets for the chosen track's plug-ins." };
    theme::HouseButton keysTab { "Keys", "Every command and its key: change any of them." };
    theme::HouseButton studioTab { "Studio", "Sync (MIDI clock, timecode, Link) and control surfaces (Mackie Control, OSC)." };
    juce::Viewport studioView;
    StudioPanel studio;
    KeysPanel keysPanel;
    theme::HouseButton scan { ui::u8 ("Scan"), "Look for plug-ins installed since the last scan." };
    juce::TextEditor search;

    struct PluginList;
    struct FileList;
    struct PresetList;
    std::unique_ptr<PresetList> presets;
    juce::ListBox presetBox;
    std::unique_ptr<PluginList> plugins;
    std::unique_ptr<FileList> files;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audio;
    juce::ListBox pluginBox, fileBox;
    theme::HouseButton up { ui::u8 ("\xe2\x86\x91"), "The folder above." };
    Tab tab = Tab::Plugins;

    // Hearing a file before using it.
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioSourcePlayer previewPlayer;
    void preview (const juce::File&);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
