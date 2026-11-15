/*
    PluginManager - which plug-ins are installed, and making them.

    AU (on the Mac), VST3 and CLAP side by side (decision 0021). The list of
    plug-ins found is kept in Application Support, so scanning happens once
    and then only for what's new. Scanning goes one file at a time on the
    message thread with the progress in the window's message line - no extra
    window (decision 0002) - and a plug-in that crashes the scan is noted
    (JUCE's "dead man's pedal") and skipped next time, never retried.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace kal
{

class PluginManager : private juce::Timer
{
public:
    PluginManager();
    ~PluginManager() override;

    juce::AudioPluginFormatManager& formats() { return formatManager; }
    juce::KnownPluginList& known() { return list; }

    // Scans every format's usual folders. `progress` is told what is being
    // looked at (0-1) and called with 1 when done.
    void scan (std::function<void (float, const juce::String&)> progress);
    bool scanning() const { return isTimerRunning(); }
    void cancelScan();

    std::unique_ptr<juce::AudioPluginInstance> create (const PluginRef&, double sampleRate, int blockSize,
                                                       juce::String& error);

    static PluginRef refFor (const juce::PluginDescription&);
    static bool descriptionFor (const PluginRef&, juce::PluginDescription&);
    static DeviceRole roleFor (const juce::PluginDescription&);

    void save();

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList list;
    juce::File listFile, pedalFile;

    struct Pending { juce::AudioPluginFormat* format; std::unique_ptr<juce::PluginDirectoryScanner> scanner; };
    std::vector<Pending> queue;
    size_t queueIndex = 0;
    std::function<void (float, const juce::String&)> onProgress;

    void timerCallback() override;
};

} // namespace kal
