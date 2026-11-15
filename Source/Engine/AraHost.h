/*
    AraHost - ARA plug-ins such as Melodyne (decision 0028).

    An ARA plug-in doesn't wait for sound to be played into it: it reads the
    track's audio itself, analyses it, and plays it back edited. So when an
    ARA plug-in is on a track (and its ARA switch is on), Kalibre tells it
    about every audio clip on the track - the file (an audio source), the
    stretch of it the clip uses (a playback region) and where that lands on
    the timeline - and stops playing those clips itself; the plug-in plays
    them. Kalibre's tempo and time signatures are given to it too, so it can
    show bars and beats.

    What the plug-in keeps about its edits (Melodyne's notes) is stored in
    the project with the plug-in's own state, as an ARA archive.

    Built only when Kalibre is configured with KALIBRE_ARA (the ARA SDK is
    fetched then); otherwise every ARA plug-in works as an ordinary effect.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace kal
{

class AraSession
{
public:
    virtual ~AraSession() = default;

    // True once the plug-in has agreed to ARA and the document is made.
    virtual bool ready() const = 0;

    // Makes the document match the track's audio clips (message thread).
    // `whilePluginStopped` runs a change with the plug-in unprepared, as ARA asks.
    virtual void update (const Project&, const Track&, const juce::File& projectFolder,
                         const std::function<void (std::function<void()>)>& whilePluginStopped) = 0;

    // The plug-in's ARA archive (base64), for saving with the project.
    virtual std::string archive() = 0;
};

bool araAvailable();

// Starts ARA with a plug-in that offers it. `archive` restores its edits;
// `onReady` is called on the message thread when the document exists.
std::unique_ptr<AraSession> startAra (juce::AudioPluginInstance&, const std::string& archive, std::function<void()> onReady);

} // namespace kal
