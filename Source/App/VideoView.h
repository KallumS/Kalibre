/*
    VideoView - the song's video, in the lower zone's Video tab (decision 0046).

    File > Import Video puts a film on the timeline (and its sound on a track
    of its own). The picture follows the playhead: it plays when the song
    plays, jumps when it locates, and shows the frame at the cursor when
    stopped. Its own sound is silent; the track plays it, so it can be mixed.

    The picture is drawn by the Mac (AVFoundation); elsewhere the tab says so.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

#if JUCE_MAC
 #include <juce_video/juce_video.h>
#endif

namespace kal
{

class VideoView : public juce::Component, private juce::Timer
{
public:
    explicit VideoView (Controller&);
    ~VideoView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    Controller& c;
    std::string loaded;
   #if JUCE_MAC
    std::unique_ptr<juce::VideoComponent> video;
   #endif
    void timerCallback() override;
    void load();
};

} // namespace kal
