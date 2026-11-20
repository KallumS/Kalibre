// The plug-in scanner (decision 0047): each plug-in is looked at in a helper
// process, so one that crashes or hangs is turned off and Kalibre carries on.
// The helper here is this test program, run again with --kalibre-scan; it
// knows a "Fake" format whose plug-ins open, crash, hang or hold nothing.
#include "Check.h"
#include "PluginManager.h"

#include <cstdlib>

using namespace kal;
using namespace juce;

namespace
{
StringArray& fakeFiles()
{
    static StringArray f;
    return f;
}

// What a fake plug-in is when it's opened: "Riffer" sends MIDI and has
// outputs (an instrument that only sends MIDI); "Arp" sends MIDI and has none.
// A knob as a hosted plug-in has one (the report reads its name and unit, 0104).
struct FakeKnob : public AudioPluginInstance::HostedParameter
{
    FakeKnob (String n, String u) : name (std::move (n)), unit (std::move (u)) {}
    String getParameterID() const override { return name; }
    float getValue() const override { return value; }
    void setValue (float v) override { value = v; }
    float getDefaultValue() const override { return 0.5f; }
    String getName (int) const override { return name; }
    String getLabel() const override { return unit; }
    float getValueForText (const String& t) const override { return t.getFloatValue(); }
    String name, unit;
    float value = 0.5f;
};

struct FakeInstance : public AudioPluginInstance
{
    FakeInstance (PluginDescription d, int outs)
        : AudioPluginInstance (BusesProperties().withOutput ("Out", outs == 2 ? AudioChannelSet::stereo() : AudioChannelSet::disabled(), outs == 2)),
          desc (std::move (d))
    {
        // "Knobs": the names a synth's controls have, Serum's and Sylenth1's among them.
        if (desc.name == "Knobs")
            for (auto [n, u] : { std::pair { "MasterVol", "dB" }, { "A Octave", "" }, { "Fil Cutoff", "Hz" }, { "Note A1", "" },
                                 { "Env1 Atk", "ms" }, { "Spread", "ct" }, { "Next Patch", "" } })
                addHostedParameter (std::make_unique<FakeKnob> (n, u));
    }
    void fillInPluginDescription (PluginDescription& d) const override { d = desc; }
    const String getName() const override { return desc.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    PluginDescription desc;
};

struct FakeFormat : public AudioPluginFormat
{
    String getName() const override { return "Fake"; }
    void findAllTypesForFile (OwnedArray<PluginDescription>& out, const String& id) override
    {
        if (id == "fake:crash") std::abort();
        if (id == "fake:hang") for (;;) Thread::sleep (1000);
        if (id.startsWith ("fake:ok:"))
        {
            auto* d = out.add (new PluginDescription());
            d->name = id.fromLastOccurrenceOf (":", false, false);
            d->manufacturerName = "Tests";
            d->pluginFormatName = getName();
            d->fileOrIdentifier = id;
            d->uniqueId = d->deprecatedUid = d->name.hashCode();
            d->isInstrument = true;
            d->numOutputChannels = 2;
        }
    }
    bool fileMightContainThisPluginType (const String& id) override { return id.startsWith ("fake:"); }
    String getNameOfPluginFromIdentifier (const String& id) override { return id.fromLastOccurrenceOf (":", false, false); }
    bool pluginNeedsRescanning (const PluginDescription&) override { return false; }
    bool doesPluginStillExist (const PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    StringArray searchPathsForPlugins (const FileSearchPath&, bool, bool) override { return fakeFiles(); }
    FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const PluginDescription&) const override { return false; }
    void createPluginInstance (const PluginDescription& d, double, int, PluginCreationCallback cb) override
    {
        if (d.name == "Riffer") { cb (std::make_unique<FakeInstance> (d, 2), {}); return; }
        if (d.name == "Arp") { cb (std::make_unique<FakeInstance> (d, 0), {}); return; }
        if (d.name == "Knobs") { cb (std::make_unique<FakeInstance> (d, 2), {}); return; }
        cb (nullptr, "Fake plug-ins don't make sound.");
    }
};

File freshFolder (const String& name)
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile (name);
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

// Runs a scan to the end, the message loop turning, and returns its summary.
String waitFor (PluginManager& pm, std::function<void (PluginManager::Progress)> begin)
{
    bool finished = false;
    String summary;
    begin ([&] (float p, const String& what) { if (p >= 1.0f) { finished = true; summary = what; } });
    const auto until = Time::getMillisecondCounter() + 60000;
    while (! finished && Time::getMillisecondCounter() < until)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (finished);
    return summary;
}

// Only the fake format's: on the Mac, Apple's own Audio Units are scanned too.
int fakeTypes (PluginManager& pm)
{
    int n = 0;
    for (auto& d : pm.known().getTypes()) n += d.pluginFormatName == "Fake" ? 1 : 0;
    return n;
}

size_t fakeBlocked (PluginManager& pm)
{
    size_t n = 0;
    for (auto& b : pm.blocked()) n += b.format == "Fake" ? 1 : 0;
    return n;
}

const PluginManager::Blocked* find (const std::vector<PluginManager::Blocked>& all, const String& file)
{
    for (auto& b : all) if (b.file == file) return &b;
    return nullptr;
}
} // namespace

void installTestFormats()
{
    PluginManager::extraFormats() = [] (AudioPluginFormatManager& fm) { fm.addFormat (std::make_unique<FakeFormat>()); };
}

TEST ("plug-ins: one that crashes, hangs or holds nothing is turned off with its reason, and the scan carries on")
{
    const auto folder = freshFolder ("scan");
    fakeFiles() = { "fake:ok:Good Synth", "fake:crash", "fake:hang", "fake:empty", "fake:ok:Second Synth" };
    PluginManager pm (folder);
    pm.timeoutMs = 3000;
    const auto summary = waitFor (pm, [&] (auto p) { pm.scan (p); });

    // Kalibre is still here, and found the two that open.
    CHECK_EQ (fakeTypes (pm), 2);
   #if JUCE_MAC
    // Apple's own Audio Units, scanned the same way, open: the helper works on the Mac.
    int apple = 0;
    for (auto& d : pm.known().getTypes()) apple += d.pluginFormatName == "AudioUnit" && d.manufacturerName == "Apple" ? 1 : 0;
    CHECK (apple > 5);
   #endif
    if (fakeTypes (pm) != 2 || fakeBlocked (pm) != 3)
    {
        for (auto& b : pm.blocked()) std::printf ("  off: %s (%s): %s\n", b.file.toRawUTF8(), PluginManager::describe (b.reason).toRawUTF8(), b.detail.toRawUTF8());
        std::printf ("  helper's log:\n%s\n", pm.logFile().loadFileAsString().toRawUTF8());
    }
    CHECK (summary.contains ("crash"));
    CHECK (summary.contains ("hang"));
    const auto blocked = pm.blocked();
    CHECK_EQ (fakeBlocked (pm), size_t (3));
    if (auto* b = find (blocked, "fake:crash")) { CHECK (b->reason == PluginManager::Reason::Crashed); CHECK (b->detail.contains ("signal 6")); }
    else CHECK (false);
    if (auto* b = find (blocked, "fake:hang")) CHECK (b->reason == PluginManager::Reason::TookTooLong);
    else CHECK (false);
    if (auto* b = find (blocked, "fake:empty")) CHECK (b->reason == PluginManager::Reason::WouldNotOpen);
    else CHECK (false);

    // Saved as it went: a new start knows all of it.
    PluginManager again (folder);
    CHECK_EQ (fakeTypes (again), 2);
    CHECK_EQ (fakeBlocked (again), size_t (3));
    const auto againBlocked = again.blocked();
    if (auto* b = find (againBlocked, "fake:hang")) CHECK (b->reason == PluginManager::Reason::TookTooLong);
    else CHECK (false);
}

TEST ("plug-ins: a second scan skips what's known and what's turned off")
{
    const auto folder = freshFolder ("rescan");
    fakeFiles() = { "fake:ok:Good Synth", "fake:crash" };
    PluginManager pm (folder);
    waitFor (pm, [&] (auto p) { pm.scan (p); });
    CHECK_EQ (fakeTypes (pm), 1);
    const auto started = Time::getMillisecondCounter();
    const auto summary = waitFor (pm, [&] (auto p) { pm.scan (p); });
    CHECK (Time::getMillisecondCounter() - started < 1000);    // no helper was run
    CHECK (! summary.contains ("Turned off"));
    CHECK_EQ (fakeTypes (pm), 1);
    CHECK_EQ (fakeBlocked (pm), size_t (1));

    // Scanned again by hand: it still crashes, and stays off.
    waitFor (pm, [&] (auto p) { pm.rescan ({ { "Fake", "fake:crash" } }, p); });
    CHECK_EQ (fakeBlocked (pm), size_t (1));
}

TEST ("plug-ins: turned off by hand, it isn't loaded or listed; turned on, it's back without a scan")
{
    const auto folder = freshFolder ("turnoff");
    fakeFiles() = { "fake:ok:Good Synth" };
    PluginManager pm (folder);
    waitFor (pm, [&] (auto p) { pm.scan (p); });
    CHECK_EQ (fakeTypes (pm), 1);
    const auto ref = PluginManager::refFor (*pm.known().getTypeForFile ("fake:ok:Good Synth"));

    pm.turnOff ("Fake", "fake:ok:Good Synth");
    CHECK_EQ (fakeTypes (pm), 0);
    String error;
    CHECK (pm.create (ref, 48000, 512, error) == nullptr);
    CHECK (error.contains ("turned off"));
    const auto off = pm.blocked();
    if (auto* b = find (off, "fake:ok:Good Synth")) CHECK_EQ (b->types.size(), size_t (1));
    else CHECK (false);

    PluginManager reopened (folder);
    reopened.turnOn ("fake:ok:Good Synth");
    CHECK_EQ (fakeTypes (reopened), 1);
    CHECK (reopened.known().getTypeForFile ("fake:ok:Good Synth") != nullptr);
    CHECK_EQ (fakeBlocked (reopened), size_t (0));
}

TEST ("plug-ins: what the old scanner left in Scanning.txt is turned off as a crash")
{
    const auto folder = freshFolder ("pedal");
    folder.getChildFile ("Scanning.txt").replaceWithText ("/Library/Audio/Plug-Ins/VST3/Crashy.vst3\n");
    PluginManager pm (folder);
    CHECK (pm.isBlocked ("/Library/Audio/Plug-Ins/VST3/Crashy.vst3"));
    CHECK (! folder.getChildFile ("Scanning.txt").exists());
    const auto all = pm.blocked();
    if (auto* b = find (all, "/Library/Audio/Plug-Ins/VST3/Crashy.vst3"))
    {
        CHECK (b->reason == PluginManager::Reason::Crashed);
        CHECK_EQ (b->format, String ("VST3"));
    }
    else CHECK (false);
}

TEST ("plug-ins: the scan opens each one; an instrument that only sends MIDI is listed as a MIDI effect too, and any can be filed by hand (0069)")
{
    const auto folder = freshFolder ("traits");
    fakeFiles() = { "fake:ok:Riffer", "fake:ok:Arp", "fake:ok:Good Synth" };
    PluginManager pm (folder);
    waitFor (pm, [&] (auto p) { pm.scan (p); });
    CHECK_EQ (fakeTypes (pm), 3);
    auto get = [&] (const char* f) { auto d = pm.known().getTypeForFile (f); return d ? *d : PluginDescription(); };
    const auto riffer = get ("fake:ok:Riffer"), arp = get ("fake:ok:Arp"), synth = get ("fake:ok:Good Synth");

    // Every fake says it's an instrument; opened, Arp sends MIDI and no sound.
    CHECK (pm.roleOf (arp) == DeviceRole::MidiFx);
    CHECK (pm.roleOf (riffer) == DeviceRole::Instrument);
    CHECK (pm.sendsMidi (riffer));
    CHECK (! pm.sendsMidi (synth));
    CHECK (pm.roleOf (synth) == DeviceRole::Instrument);
    CHECK (pm.traitsOf (riffer).known);
    CHECK_EQ (pm.traitsOf (riffer).audioOuts, 2);
    CHECK_EQ (pm.needingAnotherLook(), 0);

    // Filed by hand, kept on the next start; and given back.
    pm.fileAs (riffer, DeviceRole::MidiFx);
    CHECK (pm.roleOf (riffer) == DeviceRole::MidiFx);
    PluginManager again (folder);
    CHECK (again.roleOf (riffer) == DeviceRole::MidiFx);
    CHECK (again.filedByHand (riffer));
    CHECK (again.sendsMidi (riffer));
    again.fileAs (riffer, std::nullopt);
    CHECK (again.roleOf (riffer) == DeviceRole::Instrument);

    // A list scanned before Kalibre learned this is looked at again by the next scan.
    const auto list = folder.getChildFile ("Plug-ins.xml");
    auto xml = parseXML (list);
    CHECK (xml != nullptr);
    if (! xml) return;
    xml->removeChildElement (xml->getChildByName ("KALIBRE_TRAITS"), true);
    CHECK (xml->writeTo (list));
    PluginManager old (folder);
    CHECK (old.needingAnotherLook() >= 3);     // on the Mac, Apple's own Audio Units too
    CHECK (old.roleOf (arp) == DeviceRole::Instrument);
    waitFor (old, [&] (auto p) { old.scan (p); });
    CHECK_EQ (old.needingAnotherLook(), 0);
    CHECK (old.roleOf (arp) == DeviceRole::MidiFx);
}

#include "BuiltinInstruments.h"
#include "Controller.h"
#include "DeviceRack.h"
#include "Edit.h"
#include "Engine.h"
#include "Theme.h"

TEST ("devices: a plug-in dragged from the browser onto the rack is added to the shown track where it's dropped (0095)")
{
    theme::LookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    {
        kal::PluginManager plugins;
        kal::Engine engine (plugins);
        kal::Controller c (engine);
        juce::PluginDescription synth, sampler;
        kal::KalibreSynth().fillInPluginDescription (synth);
        kal::KalibreSampler().fillInPluginDescription (sampler);
        plugins.known().addType (synth);
        plugins.known().addType (sampler);
        auto indexOf = [&] (const juce::String& name)
        {
            const auto types = plugins.known().getTypes();
            for (int i = 0; i < types.size(); ++i) if (types[i].name == name) return i;
            return -1;
        };
        c.project = kal::Project();
        const kal::Id t = kal::edit::addTrack (c.project, kal::TrackKind::Track, "Keys");
        c.focusTrack = t;
        c.refresh();
        kal::DeviceRack rack (c);
        rack.setSize (1200, 300);
        rack.rebuild();
        using SD = juce::DragAndDropTarget::SourceDetails;
        CHECK (rack.isInterestedInDragSource (SD (juce::var ("plugin:" + juce::String (indexOf ("Kalibre Synth"))), nullptr, { 600, 150 })));
        CHECK (! rack.isInterestedInDragSource (SD (juce::var ("file:/a.wav"), nullptr, { 600, 150 })));
        rack.itemDropped (SD (juce::var ("plugin:" + juce::String (indexOf ("Kalibre Synth"))), nullptr, { 600, 150 }));
        CHECK_EQ (c.project.track (t)->devices.size(), (size_t) 1);
        rack.rebuild();
        // Dropped at the far left: before the synth.
        rack.itemDropped (SD (juce::var ("plugin:" + juce::String (indexOf ("Kalibre Sampler"))), nullptr, { 2, 150 }));
        const auto& devices = c.project.track (t)->devices;
        CHECK_EQ (devices.size(), (size_t) 2);
        if (devices.size() == 2)
        {
            CHECK_EQ (devices[0].plugin.name, std::string ("Kalibre Sampler"));
            CHECK_EQ (devices[1].plugin.name, std::string ("Kalibre Synth"));
        }
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("plug-ins: the Random Settings Report reads every plug-in's controls in the helper, and marks each random or kept (0104)")
{
    const auto folder = freshFolder ("controls");
    fakeFiles() = { "fake:ok:Knobs", "fake:crash", "fake:ok:Riffer" };
    PluginManager pm (folder);
    pm.timeoutMs = 5000;
    waitFor (pm, [&] (auto p) { pm.scan (p); });
    bool finished = false;
    std::vector<PluginManager::PluginControls> got;
    pm.readControls ({}, [&] (std::vector<PluginManager::PluginControls> all) { got = std::move (all); finished = true; });
    for (int i = 0; i < 3000 && ! finished; ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (finished);
    const PluginManager::PluginControls* knobs = nullptr;
    for (auto& pc : got) if (pc.type.name == "Knobs") knobs = &pc;
    CHECK (knobs != nullptr);
    if (! knobs) return;
    CHECK_EQ (knobs->controls.size(), (size_t) 7);
    std::map<String, String> verdict;
    for (auto& ctl : knobs->controls) verdict[ctl.name] = randomVerdict (ctl, knobs->type.name);
    CHECK_EQ (verdict["MasterVol"], String ("volume"));
    CHECK_EQ (verdict["A Octave"], String ("pitch"));
    CHECK_EQ (verdict["Note A1"], String ("pitch"));
    CHECK_EQ (verdict["Spread"], String ("pitch"));          // measured in cents
    CHECK_EQ (verdict["Next Patch"], String ("not sound"));
    CHECK (verdict["Fil Cutoff"].isEmpty());
    CHECK (verdict["Env1 Atk"].isEmpty());
    // The one that crashed the scan is said to be turned off, not left out.
    bool riffer = false;
    for (auto& pc : got) riffer |= pc.type.name == "Riffer" && pc.problem.isEmpty();
    CHECK (riffer);
}
