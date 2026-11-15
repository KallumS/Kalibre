/*
    Mixer - every track's channel strip, in the lower zone: volume, pan,
    mute, solo, its meter, its sends, where its sound goes. Folders and
    returns are strips like any other, the master last.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class ChannelStrip;

class Mixer : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit Mixer (Controller&);
    ~Mixer() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Controller& c;
    juce::Viewport viewport;
    juce::Component strip;
    juce::OwnedArray<ChannelStrip> strips;
    juce::String shape;
    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace kal
