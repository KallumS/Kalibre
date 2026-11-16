#include "VideoView.h"

namespace kal
{
using namespace juce;

VideoView::VideoView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Video");
}

VideoView::~VideoView() { stopTimer(); }

void VideoView::visibilityChanged()
{
    if (isVisible()) { load(); startTimerHz (30); }
    else
    {
        stopTimer();
       #if JUCE_MAC
        if (video) video->stop();
       #endif
    }
}

void VideoView::load()
{
    if (c.project.video.file == loaded) return;
    loaded = c.project.video.file;
   #if JUCE_MAC
    video.reset();
    if (loaded.empty()) { repaint(); return; }
    video = std::make_unique<VideoComponent> (false);
    addAndMakeVisible (*video);
    const auto r = video->load (c.resolve (loaded));
    if (r.failed()) { c.say ("The video couldn't be opened. " + r.getErrorMessage(), true); video.reset(); }
    else video->setAudioVolume (0.0f);   // its sound is on a track of its own
    resized();
   #endif
    repaint();
}

void VideoView::timerCallback()
{
    load();
   #if JUCE_MAC
    if (! video || ! video->isVideoOpen()) return;
    // Where the picture should be: the song's time less where the film starts.
    const double offset = c.project.tempo.secondsAt (c.project.video.start);
    const double at = (c.engine.isPlaying() ? c.engine.heardPosition() : c.project.tempo.secondsAt (c.cursor)) - offset;
    const bool inside = at >= 0 && at < video->getVideoDuration();
    if (c.engine.isPlaying() && inside)
    {
        if (! video->isPlaying()) { video->setPlayPosition (at); video->play(); }
        else if (std::abs (video->getPlayPosition() - at) > 0.08) video->setPlayPosition (at);
    }
    else
    {
        if (video->isPlaying()) video->stop();
        const double still = jlimit (0.0, video->getVideoDuration(), at);
        if (std::abs (video->getPlayPosition() - still) > 0.02) video->setPlayPosition (still);
    }
   #endif
}

void VideoView::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    String note;
    if (c.project.video.file.empty()) note = "No video yet: File > Import Video.";
   #if ! JUCE_MAC
    else note = "The video plays in Kalibre on the Mac.";
   #endif
    if (note.isNotEmpty())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText (note, getLocalBounds().reduced (20), Justification::centred, 2);
    }
}

void VideoView::resized()
{
   #if JUCE_MAC
    if (video)
    {
        // The film's own shape, as big as fits.
        auto r = getLocalBounds().reduced (6);
        const auto size = video->getVideoNativeSize();
        if (! size.isEmpty())
        {
            const float k = jmin ((float) r.getWidth() / (float) size.getWidth(), (float) r.getHeight() / (float) size.getHeight());
            r = r.withSizeKeepingCentre (roundToInt ((float) size.getWidth() * k), roundToInt ((float) size.getHeight() * k));
        }
        video->setBounds (r);
    }
   #endif
}

} // namespace kal
