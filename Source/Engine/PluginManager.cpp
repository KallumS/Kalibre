#include "PluginManager.h"

#include "ClapFormat.h"
#include "MorphProcessor.h"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace kal
{
using namespace juce;

// What to call a plug-in in messages: a file's name, or what its format says.
static String nameFor (AudioPluginFormat* format, const String& id)
{
    if (File::isAbsolutePath (id)) return File (id).getFileNameWithoutExtension();
    return format != nullptr ? format->getNameOfPluginFromIdentifier (id) : id;
}

static File supportFolder()
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
   #if JUCE_MAC
               .getChildFile ("Application Support")
   #endif
               .getChildFile ("Kalibre");
    f.createDirectory();
    return f;
}

std::function<void (AudioPluginFormatManager&)>& PluginManager::extraFormats()
{
    static std::function<void (AudioPluginFormatManager&)> f;
    return f;
}

void PluginManager::addFormats (AudioPluginFormatManager& fm)
{
    addDefaultFormatsToManager (fm);
    fm.addFormat (std::make_unique<clap::ClapPluginFormat>());
    if (extraFormats()) extraFormats() (fm);
}

String PluginManager::describe (Reason r)
{
    switch (r)
    {
        case Reason::Crashed:      return "Crashed while being scanned";
        case Reason::TookTooLong:  return "Took too long to open";
        case Reason::WouldNotOpen: return "Wouldn't open";
        case Reason::TurnedOff:    return "Turned off by you";
    }
    return {};
}

PluginManager::PluginManager (File folder)
{
    addFormats (formatManager);
    helper = File::getSpecialLocation (File::currentExecutableFile);
    if (folder == File()) folder = supportFolder();
    folder.createDirectory();
    file = folder.getChildFile ("Plug-ins.xml");
    if (auto xml = parseXML (file)) loadFrom (*xml);
    importPedal();
}

PluginManager::~PluginManager()
{
    stopTimer();
    worker.reset();
    save();
}

// Before scanning moved into a helper, the plug-in being scanned was written
// to Scanning.txt in case it took Kalibre down. What's left there crashed it.
void PluginManager::importPedal()
{
    const auto pedal = file.getSiblingFile ("Scanning.txt");
    if (! pedal.existsAsFile()) return;
    StringArray lines;
    pedal.readLines (lines);
    lines.removeEmptyStrings();
    for (auto& l : lines)
    {
        list.addToBlacklist (l);
        Blocked b;
        b.file = l;
        b.format = l.endsWithIgnoreCase (".vst3") ? "VST3" : l.endsWithIgnoreCase (".clap") ? "CLAP" : l.startsWith ("AudioUnit:") ? "AudioUnit" : "";
        b.reason = Reason::Crashed;
        b.detail = "it closed Kalibre while being scanned";
        reasons[l] = b;
    }
    pedal.deleteFile();
    save();
}

void PluginManager::loadFrom (const XmlElement& xml)
{
    list.recreateFromXml (xml);
    reasons.clear();
    if (auto* problems = xml.getChildByName ("KALIBRE_BLOCKED"))
        for (auto* e : problems->getChildIterator())
        {
            Blocked b;
            b.file = e->getStringAttribute ("file");
            b.format = e->getStringAttribute ("format");
            b.reason = (Reason) jlimit (0, 3, e->getIntAttribute ("reason"));
            b.detail = e->getStringAttribute ("detail");
            for (auto* t : e->getChildWithTagNameIterator ("PLUGIN"))
            {
                PluginDescription d;
                if (d.loadFromXml (*t)) b.types.push_back (d);
            }
            if (b.file.isNotEmpty()) reasons[b.file] = b;
        }
}

void PluginManager::save()
{
    const ScopedLock sl (saveLock);
    auto xml = list.createXml();
    if (! xml) return;
    auto* problems = xml->createNewChildElement ("KALIBRE_BLOCKED");
    for (auto& [f, b] : reasons)
    {
        if (! list.getBlacklistedFiles().contains (f)) continue;
        auto* e = problems->createNewChildElement ("FILE");
        e->setAttribute ("file", b.file);
        e->setAttribute ("format", b.format);
        e->setAttribute ("reason", (int) b.reason);
        e->setAttribute ("detail", b.detail);
        for (auto& d : b.types)
            if (auto t = d.createXml()) e->addChildElement (t.release());
    }
    // Written beside and moved over, so a crash mid-write can't lose the list.
    const auto temp = file.getSiblingFile (file.getFileName() + ".part");
    if (xml->writeTo (temp)) temp.moveFileTo (file);
}

std::vector<PluginManager::Blocked> PluginManager::blocked() const
{
    std::vector<Blocked> out;
    for (auto& f : list.getBlacklistedFiles())
    {
        auto it = reasons.find (f);
        if (it != reasons.end()) { out.push_back (it->second); continue; }
        Blocked b;
        b.file = f;
        b.reason = Reason::Crashed;
        out.push_back (b);
    }
    return out;
}

static void removeTypesOf (KnownPluginList& list, const String& file)
{
    for (auto& d : list.getTypes())
        if (d.fileOrIdentifier == file) list.removeType (d);
}

void PluginManager::turnOff (const String& format, const String& f)
{
    Blocked b;
    b.file = f;
    b.format = format;
    b.reason = Reason::TurnedOff;
    for (auto& d : list.getTypes())
        if (d.fileOrIdentifier == f) b.types.push_back (d);
    if (auto it = reasons.find (f); it != reasons.end() && b.types.empty()) b.types = it->second.types;
    removeTypesOf (list, f);
    reasons[f] = b;
    list.addToBlacklist (f);
    save();
}

void PluginManager::turnOn (const String& f)
{
    auto it = reasons.find (f);
    if (it != reasons.end())
        for (auto& d : it->second.types) list.addType (d);
    reasons.erase (f);
    list.removeFromBlacklist (f);
    save();
}

void PluginManager::forget (const String& f)
{
    removeTypesOf (list, f);
    reasons.erase (f);
    list.removeFromBlacklist (f);
    save();
}

//==============================================================================
namespace
{
struct Outcome
{
    enum How { Exited, Signalled, TimedOut, Cancelled, CouldNotStart } how = CouldNotStart;
    int code = 0;
};

// Runs the helper and waits for it, without a shell, its output thrown away.
Outcome runHelper (const File& exe, const StringArray& args, const File& log, int timeoutMs, Thread& thread)
{
    std::vector<std::string> strings { exe.getFullPathName().toStdString() };
    for (auto& a : args) strings.push_back (a.toStdString());
    std::vector<char*> argv;
    for (auto& s : strings) argv.push_back (s.data());
    argv.push_back (nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_addopen (&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    // What the helper says goes to a log beside the list, for when something goes wrong.
    const auto logPath = log.getFullPathName().toStdString();
    posix_spawn_file_actions_addopen (&actions, STDERR_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    pid_t pid = 0;
    const int rc = posix_spawn (&pid, strings[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy (&actions);
    if (rc != 0) return { Outcome::CouldNotStart, rc };

    const auto started = Time::getMillisecondCounter();
    for (;;)
    {
        int status = 0;
        const pid_t r = waitpid (pid, &status, WNOHANG);
        if (r == pid)
        {
            if (WIFSIGNALED (status)) return { Outcome::Signalled, WTERMSIG (status) };
            return { Outcome::Exited, WIFEXITED (status) ? WEXITSTATUS (status) : -1 };
        }
        if (r < 0 && errno != EINTR) return { Outcome::Exited, -1 };
        const bool stop = thread.threadShouldExit();
        if (stop || (int) (Time::getMillisecondCounter() - started) > timeoutMs)
        {
            ::kill (pid, SIGKILL);
            while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
            return { stop ? Outcome::Cancelled : Outcome::TimedOut, 0 };
        }
        Thread::sleep (15);
    }
}

String signalName (int sig)
{
    switch (sig)
    {
        case SIGABRT: return "it aborted, signal 6";
        case SIGSEGV: return "a bad memory access, signal 11";
        case SIGBUS:  return "a bad memory access, signal 10";
        case SIGILL:  return "an illegal instruction, signal 4";
        case SIGTRAP: return "a trap, signal 5";
        case SIGKILL: return "it was killed, signal 9";
        default:      return "signal " + String (sig);
    }
}
} // namespace

class PluginManager::Worker : public Thread
{
public:
    Worker (PluginManager& o, std::vector<Item> i) : Thread ("Kalibre plug-in scan"), owner (o), items (std::move (i)) {}
    ~Worker() override { stopThread (10000); }

    void run() override
    {
        const auto exe = owner.helper;
        const auto log = owner.logFile();
        if (log.getSize() > 1024 * 1024) log.deleteFile();
        const int timeout = owner.timeoutMs;
        for (auto& item : items)
        {
            if (threadShouldExit()) return;
            {
                std::lock_guard<std::mutex> l (owner.resultsLock);
                owner.current = item.format + ": " + item.name;
            }
            const auto out = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Kalibre scan", ".xml", false);
            const auto how = runHelper (exe, { PluginManager::workerFlag, item.format, item.file, out.getFullPathName() }, log, timeout, *this);
            if (how.how == Outcome::Cancelled) { out.deleteFile(); return; }
            if (how.how == Outcome::CouldNotStart) { helperFailed = true; return; }

            Result r;
            r.item = item;
            auto xml = parseXML (out);
            out.deleteFile();
            if (xml && xml->hasTagName ("KALIBRE_SCAN"))
            {
                for (auto* e : xml->getChildWithTagNameIterator ("PLUGIN"))
                {
                    PluginDescription d;
                    if (d.loadFromXml (*e)) r.types.push_back (d);
                }
                r.ok = ! r.types.empty();
                r.reason = Reason::WouldNotOpen;
                r.detail = xml->getStringAttribute ("error");
            }
            else if (how.how == Outcome::TimedOut)
            {
                r.reason = Reason::TookTooLong;
                r.detail = "still opening after " + String (timeout / 1000) + " seconds";
            }
            else
            {
                r.reason = Reason::Crashed;
                r.detail = how.how == Outcome::Signalled ? signalName (how.code)
                                                         : "it quit with code " + String (how.code);
            }
            std::lock_guard<std::mutex> l (owner.resultsLock);
            owner.results.push_back (std::move (r));
        }
    }

    PluginManager& owner;
    std::vector<Item> items;
    std::atomic<bool> helperFailed { false };
};

int PluginManager::runWorker (int argc, char* argv[])
{
    if (argc < 5) return 2;
    const String format = String::fromUTF8 (argv[2]);
    const String id = String::fromUTF8 (argv[3]);
    const File out (String::fromUTF8 (argv[4]));
    auto say = [&] (const String& what)
    {
        std::fprintf (stderr, "%s  %s: %s\n", Time::getCurrentTime().toString (true, true).toRawUTF8(), id.toRawUTF8(), what.toRawUTF8());
        std::fflush (stderr);
    };
    say ("looking at it (" + format + ")");

    ScopedJuceInitialiser_GUI gui;
   #if JUCE_MAC
    Process::setDockIconVisible (false);
   #endif
    AudioPluginFormatManager fm;
    addFormats (fm);
    XmlElement xml ("KALIBRE_SCAN");
    bool known = false;
    for (auto* f : fm.getFormats())
        if (f->getName() == format)
        {
            known = true;
            OwnedArray<PluginDescription> found;
            f->findAllTypesForFile (found, id);
            for (auto* d : found)
                if (auto e = d->createXml()) xml.addChildElement (e.release());
            say ("found " + String (found.size()));
        }
    if (! known)
    {
        say ("no format called " + format);
        xml.setAttribute ("error", "Kalibre has no format called " + format);
    }
    const auto part = out.getSiblingFile (out.getFileName() + ".part");
    if (! (xml.writeTo (part) && part.moveFileTo (out))) { say ("couldn't write " + out.getFullPathName()); std::_Exit (3); }
    // Gone at once: some plug-ins crash or hang as they're unloaded, and
    // what they held has been written down.
    std::_Exit (0);
}

//==============================================================================
void PluginManager::scan (Progress progress)
{
    if (worker) return;
    std::vector<Item> items;
    for (auto* format : formatManager.getFormats())
    {
        if (! format->canScanForPlugins()) continue;
        for (auto& id : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
        {
            if (isBlocked (id) || list.isListingUpToDate (id, *format)) continue;
            items.push_back ({ format->getName(), id, nameFor (format, id) });
        }
    }
    start (std::move (items), std::move (progress));
}

void PluginManager::rescan (const std::vector<std::pair<String, String>>& files, Progress progress)
{
    if (worker) return;
    std::vector<Item> items;
    for (auto& [format, f] : files)
    {
        AudioPluginFormat* fmt = nullptr;
        for (auto* x : formatManager.getFormats())
            if (x->getName() == format) fmt = x;
        items.push_back ({ format, f, nameFor (fmt, f) });
    }
    start (std::move (items), std::move (progress));
}

void PluginManager::start (std::vector<Item> items, Progress progress)
{
    onProgress = std::move (progress);
    total = items.size();
    done = 0;
    found = 0;
    newlyBlocked.clear();
    current = {};
    results.clear();
    worker = std::make_unique<Worker> (*this, std::move (items));
    worker->startThread();
    startTimer (50);
}

void PluginManager::cancelScan()
{
    if (! worker) return;
    worker->signalThreadShouldExit();
    worker->stopThread (10000);
    timerCallback();
    finish (true);
}

void PluginManager::apply (const Result& r)
{
    removeTypesOf (list, r.item.file);
    if (r.ok)
    {
        for (auto& d : r.types) list.addType (d);
        reasons.erase (r.item.file);
        list.removeFromBlacklist (r.item.file);
        found += (int) r.types.size();
        return;
    }
    Blocked b;
    b.file = r.item.file;
    b.format = r.item.format;
    b.reason = r.reason;
    b.detail = r.detail;
    reasons[b.file] = b;
    list.addToBlacklist (b.file);
    newlyBlocked.add (r.item.name);
}

void PluginManager::timerCallback()
{
    std::vector<Result> ready;
    String now;
    {
        std::lock_guard<std::mutex> l (resultsLock);
        ready.swap (results);
        now = current;
    }
    for (auto& r : ready) apply (r);
    done += ready.size();
    if (! ready.empty()) save();
    if (! worker) return;
    if (! worker->isThreadRunning())
    {
        bool empty;
        {
            std::lock_guard<std::mutex> l (resultsLock);
            empty = results.empty();
        }
        if (empty) { finish (false); return; }
    }
    if (onProgress && ! ready.empty())
        onProgress (total == 0 ? 0.0f : jmin (0.99f, (float) done / (float) total), now);
}

void PluginManager::finish (bool cancelled)
{
    stopTimer();
    const bool helperFailed = worker && worker->helperFailed;
    worker.reset();
    save();
    String msg;
    if (helperFailed)
        msg = "the scan couldn't start its helper (" + helper.getFullPathName() + "), so nothing new was looked at";
    else
    {
        msg = String (list.getNumTypes()) + " plug-ins" + (cancelled ? " (stopped before the end)" : "");
        if (! newlyBlocked.isEmpty())
            msg << ". Turned off because they crashed, hung or wouldn't open: " << newlyBlocked.joinIntoString (", ")
                << ". Kalibre > Plug-in Manager shows them";
    }
    auto progress = std::move (onProgress);
    onProgress = nullptr;
    if (progress) progress (1.0f, msg);
}

//==============================================================================
PluginRef PluginManager::refFor (const PluginDescription& d)
{
    PluginRef r;
    r.format = d.pluginFormatName.toStdString();
    r.identifier = d.fileOrIdentifier.toStdString();
    r.name = d.name.toStdString();
    r.manufacturer = d.manufacturerName.toStdString();
    r.uid = d.uniqueId;
    r.isInstrument = d.isInstrument;
    if (auto xml = d.createXml()) r.description = xml->toString (XmlElement::TextFormat().singleLine().withoutHeader()).toStdString();
    return r;
}

bool PluginManager::descriptionFor (const PluginRef& r, PluginDescription& d)
{
    if (! r.description.empty())
        if (auto xml = parseXML (String (r.description)))
            if (d.loadFromXml (*xml)) return true;
    d.pluginFormatName = r.format;
    d.fileOrIdentifier = r.identifier;
    d.name = r.name;
    d.manufacturerName = r.manufacturer;
    d.uniqueId = d.deprecatedUid = r.uid;
    d.isInstrument = r.isInstrument;
    return ! r.identifier.empty();
}

DeviceRole PluginManager::roleFor (const PluginDescription& d)
{
    if (d.isInstrument) return DeviceRole::Instrument;
    const auto cat = d.category.toLowerCase();
    if (cat.contains ("note effect") || cat.contains ("midi")) return DeviceRole::MidiFx;
    // No audio in or out means MIDI only, except for a VST3: JUCE describes one
    // from its moduleinfo.json, which has no channel counts, so every VST3
    // effect looked like that and was filed as a MIDI effect (decision 0058).
    if (d.pluginFormatName != "VST3" && d.numInputChannels == 0 && d.numOutputChannels == 0)
        return DeviceRole::MidiFx;
    return DeviceRole::AudioFx;
}

std::unique_ptr<AudioPluginInstance> PluginManager::create (const PluginRef& ref, double sampleRate, int blockSize,
                                                            String& error)
{
    if (ref.format == "Kalibre")
        return makeBuiltIn (ref, *this);

    PluginDescription d;
    if (! descriptionFor (ref, d))
    {
        error = "Nothing is known about this plug-in.";
        return nullptr;
    }
    // The installed copy may have moved since the project was saved: find it by its id.
    if (! formatManager.doesPluginStillExist (d))
        for (auto& k : list.getTypes())
            if (k.uniqueId == d.uniqueId && k.pluginFormatName == d.pluginFormatName) { d = k; break; }
    // A plug-in turned off isn't loaded: one that crashed a scan would crash Kalibre too.
    if (isBlocked (d.fileOrIdentifier))
    {
        auto it = reasons.find (d.fileOrIdentifier);
        const auto why = it != reasons.end() ? it->second.reason : Reason::Crashed;
        error = d.name + (why == Reason::TurnedOff ? String (" is turned off.")
                                                   : " is turned off: it " + describe (why).replace ("Wouldn't", "wouldn't").replace ("Crashed", "crashed").replace ("Took", "took") + ".")
              + " Turn it on in Kalibre > Plug-in Manager.";
        return nullptr;
    }
    return formatManager.createPluginInstance (d, sampleRate, blockSize, error);
}

} // namespace kal
