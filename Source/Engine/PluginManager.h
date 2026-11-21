/*
    PluginManager - which plug-ins are installed, and making them.

    AU (on the Mac), VST3 and CLAP side by side (decision 0021). The list of
    plug-ins found is kept in Application Support, so scanning happens once
    and then only for what's new.

    Each plug-in is looked at in a helper process of its own: Kalibre runs
    itself again with `--kalibre-scan`, and that copy loads the one plug-in,
    writes down what it found and quits (decision 0047). A plug-in that
    crashes, hangs or won't open takes only the helper with it: Kalibre keeps
    going, turns that plug-in off and says why. The list is saved after every
    plug-in, so a scan that stops halfway keeps what it found.

    The Plug-in Manager (Kalibre > Plug-in Manager...) shows the list, why a
    plug-in was turned off, and turns plug-ins on and off and scans them again.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace kal
{

class PluginManager : private juce::Timer
{
public:
    // `folder` holds the saved list; empty means Application Support/Kalibre.
    explicit PluginManager (juce::File folder = {});
    ~PluginManager() override;

    juce::AudioPluginFormatManager& formats() { return formatManager; }
    juce::KnownPluginList& known() { return list; }

    // Scans every format's usual folders for what's new. `progress` is told
    // what is being looked at (0-1) and called with 1 and a summary when done.
    using Progress = std::function<void (float, const juce::String&)>;
    void scan (Progress);
    // Scans these files (format name, file or identifier) again, whatever is known of them.
    void rescan (const std::vector<std::pair<juce::String, juce::String>>& files, Progress);
    bool scanning() const { return worker != nullptr; }
    void cancelScan();

    // Why a plug-in's file is turned off.
    enum class Reason { Crashed, TookTooLong, WouldNotOpen, TurnedOff };
    struct Blocked
    {
        juce::String format, file;
        Reason reason = Reason::Crashed;
        juce::String detail;                              // "signal 6", the error, ...
        std::vector<juce::PluginDescription> types;       // what it held, when turned off by hand
    };
    std::vector<Blocked> blocked() const;
    bool isBlocked (const juce::String& file) const { return list.getBlacklistedFiles().contains (file); }
    static juce::String describe (Reason);

    // Turning off by hand keeps what the file held, so turning it on needs no scan.
    void turnOff (const juce::String& format, const juce::String& file);
    void turnOn (const juce::String& file);     // a crashed one is scanned again instead (rescan)
    // Forgets a file: it's looked at again by the next scan.
    void forget (const juce::String& file);

    std::unique_ptr<juce::AudioPluginInstance> create (const PluginRef&, double sampleRate, int blockSize,
                                                       juce::String& error);

    static PluginRef refFor (const juce::PluginDescription&);
    static bool descriptionFor (const PluginRef&, juce::PluginDescription&);
    // What a plug-in's description says it is, before anything Kalibre has learned.
    static DeviceRole roleFor (const juce::PluginDescription&);

    // What the helper found when it opened the plug-in: whether it sends
    // MIDI, and its audio channels. Instruments like Riffer are MIDI
    // generators: they send MIDI and are listed as MIDI effects too.
    struct Traits
    {
        bool known = false;          // opened; false when it couldn't be
        bool sendsMidi = false, midiEffect = false;
        int audioIns = 0, audioOuts = 0;
    };
    Traits traitsOf (const juce::PluginDescription&) const;
    bool sendsMidi (const juce::PluginDescription& d) const { return traitsOf (d).sendsMidi; }
    // The role Kalibre files a plug-in under: the user's choice, else what
    // the helper learned, else its description.
    DeviceRole roleOf (const juce::PluginDescription&) const;
    // File a plug-in under a role by hand, or (nullopt) as Kalibre would.
    void fileAs (const juce::PluginDescription&, std::optional<DeviceRole>);
    bool filedByHand (const juce::PluginDescription&) const;
    // Plug-ins scanned before Kalibre learned their traits: Scan looks at them again.
    int needingAnotherLook() const;
    void setTraits (const juce::PluginDescription&, Traits);   // tests

    void save();
    juce::File listFile() const { return file; }
    juce::File logFile() const { return file.getSiblingFile ("Plug-in Scan.log"); }

    // The Random Settings Report (decision 0104): every scanned plug-in's controls,
    // read by opening each in the helper again, so one that crashes can't close
    // Kalibre; Kalibre's own devices are read here. `done` is called on the
    // message thread with every plug-in, in the list's order.
    struct Control
    {
        juce::String name, unit;
        int category = 0;               // juce::AudioProcessorParameter::Category
        bool automatable = true, meta = false, bypass = false;
    };
    struct PluginControls
    {
        juce::PluginDescription type;
        std::vector<Control> controls;
        juce::String problem;           // why it couldn't be read
    };
    void readControls (Progress, std::function<void (std::vector<PluginControls>)> done);
    bool readingControls() const { return controlsReader != nullptr; }
    static std::vector<Control> controlsOf (juce::AudioProcessor&);

    // The helper. main() hands over when the first argument is workerFlag:
    //   Kalibre --kalibre-scan <format> <file or identifier> <result file> [controls]
    static constexpr const char* workerFlag = "--kalibre-scan";
    static int runWorker (int argc, char* argv[]);
    // Every format, in the app and in the helper. Tests add their own here.
    static void addFormats (juce::AudioPluginFormatManager&);
    static std::function<void (juce::AudioPluginFormatManager&)>& extraFormats();
    // How long a plug-in may take to be looked at before it's given up on.
    int timeoutMs = 120000;
    // The program run as the helper: this one, unless a test says otherwise.
    juce::File helper;

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList list;
    juce::File file;
    std::map<juce::String, Blocked> reasons;      // by file; only blacklisted files
    std::map<juce::String, Traits> traits;        // by description's identifier string
    std::map<juce::String, DeviceRole> filed;     // by hand, by identifier string
    juce::CriticalSection saveLock;

    struct Item { juce::String format, file, name; };
    struct Result
    {
        Item item;
        bool ok = false;
        Reason reason = Reason::Crashed;
        juce::String detail;
        std::vector<juce::PluginDescription> types;
        std::vector<Traits> traits;
    };
    class Worker;
    std::unique_ptr<Worker> worker;
    class ControlsReader;
    std::unique_ptr<ControlsReader> controlsReader;
    std::function<void (std::vector<PluginControls>)> controlsDone;
    void finishControls();
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    std::mutex resultsLock;
    std::vector<Result> results;
    size_t total = 0, done = 0;
    juce::String current;
    juce::StringArray newlyBlocked;
    int found = 0;
    Progress onProgress;

    void start (std::vector<Item>, Progress);
    void apply (const Result&);
    void finish (bool cancelled);
    void timerCallback() override;
    void loadFrom (const juce::XmlElement&);
    bool lacksTraits (const juce::String& file) const;
    void importPedal();
};

} // namespace kal
