#include "PluginManager.h"

#include "ClapFormat.h"
#include "MorphProcessor.h"
#include "Platform.h"

#include <cerrno>
#include <cstdlib>

#if JUCE_WINDOWS
 #include "HelperProcess.h"   // decision 0129
#else
 #include <fcntl.h>
 #include <signal.h>
 #include <spawn.h>
 #include <sys/wait.h>
 #include <unistd.h>

extern char** environ;
#endif

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
    controlsReader.reset();
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
    traits.clear();
    if (auto* t = xml.getChildByName ("KALIBRE_TRAITS"))
        for (auto* e : t->getChildIterator())
        {
            Traits x;
            x.known = e->getBoolAttribute ("known");
            x.sendsMidi = e->getBoolAttribute ("sendsMidi");
            x.midiEffect = e->getBoolAttribute ("midiEffect");
            x.audioIns = e->getIntAttribute ("audioIns");
            x.audioOuts = e->getIntAttribute ("audioOuts");
            traits[e->getStringAttribute ("id")] = x;
        }
    filed.clear();
    if (auto* f = xml.getChildByName ("KALIBRE_FILED"))
        for (auto* e : f->getChildIterator())
            filed[e->getStringAttribute ("id")] = (DeviceRole) jlimit (0, 2, e->getIntAttribute ("role"));
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
    auto* t = xml->createNewChildElement ("KALIBRE_TRAITS");
    for (auto& [id, x] : traits)
    {
        auto* e = t->createNewChildElement ("PLUGIN");
        e->setAttribute ("id", id);
        e->setAttribute ("known", x.known);
        e->setAttribute ("sendsMidi", x.sendsMidi);
        e->setAttribute ("midiEffect", x.midiEffect);
        e->setAttribute ("audioIns", x.audioIns);
        e->setAttribute ("audioOuts", x.audioOuts);
    }
    auto* f = xml->createNewChildElement ("KALIBRE_FILED");
    for (auto& [id, role] : filed)
    {
        auto* e = f->createNewChildElement ("PLUGIN");
        e->setAttribute ("id", id);
        e->setAttribute ("role", (int) role);
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
    int64 code = 0;   // a signal, or on Windows the crash's exception code
};

// Runs the helper and waits for it, without a shell, its output thrown away.
Outcome runHelper (const File& exe, const StringArray& args, const File& log, int timeoutMs, Thread& thread)
{
   #if JUCE_WINDOWS
    std::vector<std::string> a;
    for (auto& s : args) a.push_back (s.toStdString());
    const auto r = helper::run (exe.getFullPathName().toStdString(), a, log.getFullPathName().toStdString(), timeoutMs,
                                [&thread] { return thread.threadShouldExit(); });
    switch (r.how)
    {
        case helper::Result::Exited:        return { Outcome::Exited, (int64) r.code };
        case helper::Result::Crashed:       return { Outcome::Signalled, (int64) r.code };
        case helper::Result::TimedOut:      return { Outcome::TimedOut, 0 };
        case helper::Result::Cancelled:     return { Outcome::Cancelled, 0 };
        case helper::Result::CouldNotStart: break;
    }
    return { Outcome::CouldNotStart, (int64) r.code };
   #else
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
   #endif
}

String signalName (int64 sig)
{
   #if JUCE_WINDOWS
    return helper::crashName ((unsigned long) sig);
   #else
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
   #endif
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
                    if (! d.loadFromXml (*e)) continue;
                    r.types.push_back (d);
                    Traits t;
                    t.known = e->getBoolAttribute ("kalibreKnown");
                    t.sendsMidi = e->getBoolAttribute ("kalibreSendsMidi");
                    t.midiEffect = e->getBoolAttribute ("kalibreMidiEffect");
                    t.audioIns = e->getIntAttribute ("kalibreAudioIns");
                    t.audioOuts = e->getIntAttribute ("kalibreAudioOuts");
                    r.traits.push_back (t);
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

//==============================================================================
std::vector<PluginManager::Control> PluginManager::controlsOf (AudioProcessor& p)
{
    std::vector<Control> out;
    auto* bypass = dynamic_cast<AudioPluginInstance*> (&p) ? static_cast<AudioPluginInstance&> (p).getBypassParameter() : nullptr;
    for (auto* prm : p.getParameters())
    {
        Control c;
        c.name = prm->getName (100);
        c.unit = prm->getLabel();
        c.category = (int) prm->getCategory();
        c.automatable = prm->isAutomatable();
        c.meta = prm->isMetaParameter();
        c.bypass = prm == bypass;
        out.push_back (c);
    }
    return out;
}

class PluginManager::ControlsReader : public Thread
{
public:
    ControlsReader (PluginManager& o, std::vector<Item> i, Progress p)
        : Thread ("Kalibre plug-in controls"), owner (o), items (std::move (i)), progress (std::move (p)), alive (o.alive) {}
    ~ControlsReader() override { stopThread (10000); }

    void run() override
    {
        const auto exe = owner.helper;
        const auto log = owner.logFile();
        for (size_t n = 0; n < items.size(); ++n)
        {
            if (threadShouldExit()) return;
            const auto& item = items[n];
            tell ((float) n / (float) jmax<size_t> (1, items.size()), item.name);
            const auto out = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Kalibre controls", ".xml", false);
            const auto how = runHelper (exe, { PluginManager::workerFlag, item.format, item.file, out.getFullPathName(), "controls" },
                                        log, owner.timeoutMs, *this);
            if (how.how == Outcome::Cancelled) { out.deleteFile(); return; }
            auto xml = parseXML (out);
            out.deleteFile();
            FileResult r;
            r.file = item.file;
            if (xml && xml->hasTagName ("KALIBRE_SCAN"))
                for (auto* e : xml->getChildWithTagNameIterator ("PLUGIN"))
                {
                    PluginControls pc;
                    if (! pc.type.loadFromXml (*e)) continue;
                    for (auto* x : e->getChildWithTagNameIterator ("KALIBRE_CONTROL"))
                        pc.controls.push_back ({ x->getStringAttribute ("name"), x->getStringAttribute ("unit"), x->getIntAttribute ("category"),
                                                 x->getBoolAttribute ("automatable", true), x->getBoolAttribute ("meta"), x->getBoolAttribute ("bypass") });
                    if (! e->getBoolAttribute ("kalibreKnown")) pc.problem = "it wouldn't open";
                    r.found.push_back (std::move (pc));
                }
            if (r.found.empty())
                r.problem = how.how == Outcome::TimedOut ? "it was still opening after " + String (owner.timeoutMs / 1000) + " seconds"
                          : how.how == Outcome::Signalled ? "it crashed (" + signalName (how.code) + ")"
                          : "it wouldn't open";
            results.push_back (std::move (r));
        }
        tell (1.0f, {});
        MessageManager::callAsync ([a = std::weak_ptr<bool> (alive), &o = owner] { if (a.lock()) o.finishControls(); });
    }

    void tell (float p, const String& what)
    {
        MessageManager::callAsync ([a = std::weak_ptr<bool> (alive), cb = progress, p, what] { if (a.lock() && cb) cb (p, what); });
    }

    PluginManager& owner;
    std::vector<Item> items;
    Progress progress;
    std::shared_ptr<bool> alive;
    struct FileResult { String file; std::vector<PluginControls> found; String problem; };
    std::vector<FileResult> results;
};

void PluginManager::readControls (Progress progress, std::function<void (std::vector<PluginControls>)> done)
{
    if (controlsReader != nullptr) return;
    controlsDone = std::move (done);
    std::vector<Item> items;
    for (auto& d : list.getTypes())
    {
        if (d.pluginFormatName == "Kalibre" || list.getBlacklistedFiles().contains (d.fileOrIdentifier)) continue;
        if (std::none_of (items.begin(), items.end(), [&] (const Item& i) { return i.file == d.fileOrIdentifier; }))
            items.push_back ({ d.pluginFormatName, d.fileOrIdentifier, d.name });
    }
    controlsReader = std::make_unique<ControlsReader> (*this, std::move (items), std::move (progress));
    controlsReader->startThread();
}

void PluginManager::finishControls()
{
    if (! controlsReader) return;
    controlsReader->stopThread (10000);
    auto results = std::move (controlsReader->results);
    controlsReader.reset();
    std::vector<PluginControls> out;
    for (auto& d : list.getTypes())
    {
        PluginControls pc;
        pc.type = d;
        if (d.pluginFormatName == "Kalibre")
        {
            // Kalibre's own devices: read here, they can't crash it.
            String error;
            if (auto inst = create (refFor (d), 48000.0, 512, error)) pc.controls = controlsOf (*inst);
            else pc.problem = error.isNotEmpty() ? error : "it wouldn't open";
        }
        else if (list.getBlacklistedFiles().contains (d.fileOrIdentifier)) pc.problem = "it's turned off";
        else
        {
            pc.problem = "it wasn't read";
            for (auto& r : results)
                if (r.file == d.fileOrIdentifier)
                {
                    pc.problem = r.problem;
                    for (auto& f : r.found)
                        if (f.type.isDuplicateOf (d) || f.type.name == d.name) { pc.controls = f.controls; pc.problem = f.problem; break; }
                }
        }
        out.push_back (std::move (pc));
    }
    if (auto cb = std::move (controlsDone)) cb (std::move (out));
}

int PluginManager::runWorker (int argc, char* argv[])
{
    if (argc < 5) return 2;
    const bool readingControls = argc > 5 && String::fromUTF8 (argv[5]) == "controls";   // the Random Settings Report (0104)
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
    auto writeOut = [&]
    {
        const auto part = out.getSiblingFile (out.getFileName() + ".part");
        if (! (xml.writeTo (part) && part.moveFileTo (out))) { say ("couldn't write " + out.getFullPathName()); std::_Exit (3); }
    };
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
            // Written now, so a plug-in that crashes as it's opened below is
            // still listed, as it was before Kalibre opened them.
            writeOut();
            // Then each is opened, to learn what its description doesn't say:
            // whether it sends MIDI (Riffer, an instrument that only sends
            // MIDI, is a MIDI effect), and its real channels (a VST3's
            // description has none, decision 0058).
            int i = 0;
            for (auto* d : found)
            {
                auto* e = xml.getChildElement (i++);
                if (e == nullptr) break;
                String error;
                auto p = fm.createPluginInstance (*d, 48000.0, 512, error);
                if (! p) { say ("couldn't open " + d->name + " to learn more: " + error); continue; }
                e->setAttribute ("kalibreKnown", true);
                e->setAttribute ("kalibreSendsMidi", p->producesMidi());
                e->setAttribute ("kalibreMidiEffect", p->isMidiEffect());
                e->setAttribute ("kalibreAudioIns", p->getTotalNumInputChannels());
                e->setAttribute ("kalibreAudioOuts", p->getTotalNumOutputChannels());
                if (readingControls)
                    for (auto& ctl : controlsOf (*p))
                    {
                        auto* x = e->createNewChildElement ("KALIBRE_CONTROL");
                        x->setAttribute ("name", ctl.name);
                        x->setAttribute ("unit", ctl.unit);
                        x->setAttribute ("category", ctl.category);
                        x->setAttribute ("automatable", ctl.automatable);
                        x->setAttribute ("meta", ctl.meta);
                        x->setAttribute ("bypass", ctl.bypass);
                    }
                say (d->name + (p->producesMidi() ? " sends MIDI" : " sends no MIDI") + ", "
                     + String (p->getTotalNumInputChannels()) + " in, " + String (p->getTotalNumOutputChannels()) + " out");
                writeOut();
            }
        }
    if (! known)
    {
        say ("no format called " + format);
        xml.setAttribute ("error", "Kalibre has no format called " + format);
    }
    writeOut();
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
            if (isBlocked (id)) continue;
            // Up to date, and looked at since Kalibre learned what plug-ins send (0069).
            if (list.isListingUpToDate (id, *format) && ! lacksTraits (id)) continue;
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
        for (size_t i = 0; i < r.types.size(); ++i)
        {
            list.addType (r.types[i]);
            // Even one that couldn't be opened is marked, so it isn't scanned again and again.
            traits[r.types[i].createIdentifierString()] = i < r.traits.size() ? r.traits[i] : Traits {};
        }
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
                << ". " << String (platform::inSettings ("Plug-in Manager")) << " shows them";
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

bool PluginManager::lacksTraits (const String& file) const
{
    for (auto& d : list.getTypes())
        if (d.fileOrIdentifier == file && traits.count (d.createIdentifierString()) == 0) return true;
    return false;
}

int PluginManager::needingAnotherLook() const
{
    int n = 0;
    for (auto& d : list.getTypes())
        if (d.pluginFormatName != "Kalibre" && traits.count (d.createIdentifierString()) == 0) ++n;
    return n;
}

PluginManager::Traits PluginManager::traitsOf (const PluginDescription& d) const
{
    auto it = traits.find (d.createIdentifierString());
    return it == traits.end() ? Traits {} : it->second;
}

void PluginManager::setTraits (const PluginDescription& d, Traits t) { traits[d.createIdentifierString()] = t; }

bool PluginManager::filedByHand (const PluginDescription& d) const { return filed.count (d.createIdentifierString()) > 0; }

void PluginManager::fileAs (const PluginDescription& d, std::optional<DeviceRole> role)
{
    if (role) filed[d.createIdentifierString()] = *role;
    else filed.erase (d.createIdentifierString());
    save();
}

DeviceRole PluginManager::roleOf (const PluginDescription& d) const
{
    if (auto it = filed.find (d.createIdentifierString()); it != filed.end()) return it->second;
    const auto t = traitsOf (d);
    if (t.known)
    {
        // A MIDI effect by its own say, or something that sends MIDI and no sound.
        if (t.midiEffect || (t.sendsMidi && t.audioOuts == 0)) return DeviceRole::MidiFx;
        if (! d.isInstrument && t.audioIns == 0 && t.audioOuts == 0) return DeviceRole::MidiFx;
    }
    return roleFor (d);
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
              + " Turn it on in " + String (platform::inSettings ("Plug-in Manager")) + ".";
        return nullptr;
    }
    return formatManager.createPluginInstance (d, sampleRate, blockSize, error);
}

} // namespace kal
