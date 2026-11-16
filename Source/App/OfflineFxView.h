/*
    OfflineFxView - a clip's offline effects, open in the Devices tab
    (decision 0039). Each plug-in of the chain is loaded on its own (it
    isn't playing) with its window side by side, so its settings can be
    set before rendering. Done keeps the settings; Render keeps them and
    renders the clip through the chain.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class OfflineFxView : public juce::Component
{
public:
    explicit OfflineFxView (Controller&);
    ~OfflineFxView() override;

    void open (Id clip);   // loads the chain's plug-ins
    void close();          // keeps their settings and lets them go
    Id shown() const { return clip; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Controller& c;
    Id clip = 0;
    struct Slot
    {
        juce::String name;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::Rectangle<int> natural;
    };
    std::vector<std::unique_ptr<Slot>> slots;
    juce::Viewport viewport;
    juce::Component strip;
    theme::HouseButton render { "Render", "Keep these settings and render the clip through its effects." };
    theme::HouseButton done { "Done", "Keep these settings and go back to the track's plug-ins." };
    void keepSettings();
};

} // namespace kal
