/*
    NoteSplitterEditor - the Note Splitter's panel in the rack (decision 0083):
    how many outputs, and the track each goes to; New Tracks makes a track
    for every output without one, under the splitter's track.
*/

#pragma once

#include "Controller.h"
#include "NoteSplitter.h"
#include "Theme.h"

namespace kal
{

class NoteSplitterEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    NoteSplitterEditor (NoteSplitterProcessor&, Controller&);
    ~NoteSplitterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    // Which output says what (tests): "Top note", "2nd note down", ..., "Bottom note".
    static juce::String noteName (int output, int outputs);

private:
    NoteSplitterProcessor& proc;
    Controller& c;
    Id device() const { return proc.deviceId.load(); }
    theme::HouseButton fewer { "-", "One output fewer." };
    theme::HouseButton more { "+", "One output more, for one more note of the chord." };
    theme::HouseButton makeTracks { "New Tracks", "Make a track for every output without one, just under this track." };
    juce::OwnedArray<theme::HouseButton> targets;
    int shown = -1;
    juce::Rectangle<int> countArea, textArea;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
};

} // namespace kal
