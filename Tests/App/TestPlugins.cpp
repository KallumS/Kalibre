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
    void createPluginInstance (const PluginDescription&, double, int, PluginCreationCallback cb) override
    {
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
