/*
    ClapFormat - CLAP plug-ins, hosted as if JUCE knew them (decision 0021).

    JUCE hosts AU and VST3 but not CLAP, so this wraps a CLAP plug-in as a
    juce::AudioPluginInstance: the engine, the device rack and the scanner
    treat all three formats alike. It speaks CLAP's C API directly (the SDK
    is headers only, MIT licensed).

    Supported: audio in and out (the main stereo ports), notes and MIDI in
    and out, parameters (shown, automated, modulated), state, latency (so
    plug-in delay compensation includes CLAP), transport, the plug-in's own
    window embedded in the lower zone (Cocoa on the Mac, X11 on Linux) with
    resizing and scaling, and timers. Not yet: note expressions, voice info,
    surround layouts.

    A plug-in's id (com.maker.thing) is kept by hashing it into the
    description's uniqueId; the file holds the bundle.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kal::clap
{

class ClapPluginFormat : public juce::AudioPluginFormat
{
public:
    ClapPluginFormat();
    ~ClapPluginFormat() override;

    static juce::String formatName() { return "CLAP"; }

    juce::String getName() const override { return formatName(); }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& file) override;
    bool fileMightContainThisPluginType (const juce::String& file) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& file) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;

private:
    void createPluginInstance (const juce::PluginDescription&, double sampleRate, int blockSize,
                               PluginCreationCallback) override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
};

// Where a CLAP plug-in's own window lives: a native view the plug-in draws
// into. Made per platform (ClapGuiMac.mm on the Mac).
class NativeHost
{
public:
    virtual ~NativeHost() = default;
    virtual juce::Component& component() = 0;
    virtual void* handle() = 0;           // NSView* on the Mac
    virtual unsigned long x11Window() { return 0; }
    virtual const char* api() const = 0;  // CLAP_WINDOW_API_*
};

std::unique_ptr<NativeHost> makeNativeHost();

} // namespace kal::clap
