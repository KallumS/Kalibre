#include "PluginPictures.h"
#include "Editors.h"
#include "Theme.h"

namespace kal::pictures
{
using namespace juce;

namespace
{
File& folderOverride()
{
    static File f;
    return f;
}

// Pictures read for the browser: small enough to keep many (each about the
// browser's width at twice the screen's resolution), the oldest let go of.
struct Cache
{
    struct Entry { Image image; uint32 used = 0; };
    std::map<String, Entry> images;     // by file
    std::map<String, bool> exists;      // by file
    uint32 clock = 0;
};
Cache& cache()
{
    static Cache c;
    return c;
}

struct Broadcaster : public ChangeBroadcaster {};
Broadcaster& broadcaster()
{
    static Broadcaster b;
    return b;
}

Image smaller (const Image& img, int widest, int tallest)
{
    if (img.isNull()) return img;
    const float s = jmin (1.0f, (float) widest / (float) img.getWidth(), (float) tallest / (float) img.getHeight());
    if (s >= 0.999f) return img;
    return img.rescaled (jmax (1, roundToInt ((float) img.getWidth() * s)), jmax (1, roundToInt ((float) img.getHeight() * s)),
                         Graphics::highResamplingQuality);
}
} // namespace

File folder()
{
    if (folderOverride() != File()) return folderOverride();
    return File::getSpecialLocation (File::userApplicationDataDirectory)
          #if JUCE_MAC
               .getChildFile ("Application Support")
          #endif
               .getChildFile ("Kalibre").getChildFile ("Plug-in Pictures");
}

void setFolder (const File& f)
{
    folderOverride() = f;
    cache() = {};
}

File fileFor (const String& name, const String& maker)
{
    const auto base = (maker.trim().isNotEmpty() ? maker.trim() + " - " : String()) + name.trim();
    return folder().getChildFile (File::createLegalFileName (base) + ".png");
}

File fileFor (const PluginDescription& d) { return fileFor (d.name, d.manufacturerName); }

bool has (const PluginDescription& d)
{
    const auto f = fileFor (d);
    auto& known = cache().exists;
    auto it = known.find (f.getFullPathName());
    if (it == known.end()) it = known.emplace (f.getFullPathName(), f.existsAsFile()).first;
    return it->second;
}

Image of (const PluginDescription& d)
{
    if (! has (d)) return {};
    auto& c = cache();
    const auto f = fileFor (d);
    auto it = c.images.find (f.getFullPathName());
    if (it == c.images.end())
    {
        // At most this many held at once: the least lately drawn go first.
        if (c.images.size() >= 120)
        {
            auto oldest = c.images.begin();
            for (auto i = c.images.begin(); i != c.images.end(); ++i) if (i->second.used < oldest->second.used) oldest = i;
            c.images.erase (oldest);
        }
        it = c.images.emplace (f.getFullPathName(), Cache::Entry { smaller (ImageFileFormat::loadFrom (f), 720, 360) }).first;
    }
    it->second.used = ++c.clock;
    return it->second.image;
}

bool keep (const Image& img, const PluginDescription& d, int widest)
{
    if (img.isNull() || d.name.isEmpty()) return false;
    const auto f = fileFor (d);
    if (! f.getParentDirectory().createDirectory()) return false;
    const auto picture = smaller (img, widest, widest);
    // Written beside, then moved over: a picture half written is never shown.
    const auto temp = f.getSiblingFile (f.getFileNameWithoutExtension() + ".writing");
    temp.deleteFile();
    {
        FileOutputStream out (temp);
        PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (picture, out)) { temp.deleteFile(); return false; }
    }
    if (! temp.moveFileTo (f)) { temp.deleteFile(); return false; }
    auto& c = cache();
    c.images.erase (f.getFullPathName());
    c.exists[f.getFullPathName()] = true;
    broadcaster().sendChangeMessage();
    return true;
}

ChangeBroadcaster& changes() { return broadcaster(); }

bool looksBlank (const Image& img)
{
    if (img.isNull() || img.getWidth() < 2 || img.getHeight() < 2) return true;
    const Image::BitmapData px (img, Image::BitmapData::readOnly);
    const auto first = px.getPixelColour (0, 0);
    auto near = [] (Colour a, Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen()) + std::abs (a.getBlue() - b.getBlue()) < 6;
    };
    // A grid of points: one that differs is enough.
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (! near (first, px.getPixelColour (x * (img.getWidth() - 1) / 23, y * (img.getHeight() - 1) / 23))) return false;
    return true;
}

Image grab (Component& c)
{
    auto img = grabNative (c);
    if (img.isValid() && ! looksBlank (img)) return img;
    // What Kalibre draws itself: its own devices, and windows the screen can't be read for.
    const float scale = jmax (1.0f, (float) Component::getApproximateScaleFactorForComponent (&c)
                                    * (float) Desktop::getInstance().getDisplays().getDisplayForRect (c.getScreenBounds())->scale);
    auto drawn = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    if (drawn.isValid() && ! looksBlank (drawn)) return drawn;
    return img.isValid() ? img : drawn;
}

//==============================================================================
// A plug-in's window on its own, in front, for as long as its picture takes.
class PictureWindow : public DocumentWindow
{
public:
    explicit PictureWindow (AudioPluginInstance& p)
        : DocumentWindow ("Taking a Picture: " + p.getName(), theme::ground, 0)
    {
        setUsingNativeTitleBar (true);
        setAlwaysOnTop (true);
        centreWithSize (400, 300);
        setVisible (true);
        // Opened once it has a window on the screen: a plug-in measures the window it's put in (0110).
        editor.reset (editors::open (p));
        setContentNonOwned (editor.get(), true);
        centreWithSize (getWidth(), getHeight());
        toFront (false);
    }
    ~PictureWindow() override { empty(); }
    void empty() { clearContentComponent(); editor.reset(); }
    AudioProcessorEditor* content() const { return editor.get(); }
    bool generic() const { return dynamic_cast<GenericAudioProcessorEditor*> (editor.get()) != nullptr; }
    void closeButtonPressed() override {}

private:
    editors::Owned editor;
};

struct Taker::Current
{
    PluginDescription desc;
    std::unique_ptr<AudioPluginInstance> instance;
    std::unique_ptr<PictureWindow> window;
};

namespace
{
File marker() { return folder().getChildFile ("Taking.txt"); }
File skippedFile() { return folder().getChildFile ("Skipped.txt"); }
String& lastCrashName()
{
    static String n;
    return n;
}

// The plug-in being photographed when Kalibre last closed is left out from now on.
void noteLastCrash()
{
    if (! marker().existsAsFile()) return;
    const auto id = StringArray::fromLines (marker().loadFileAsString())[0].trim();
    // Its name, for the message line as Kalibre starts.
    lastCrashName() = StringArray::fromLines (marker().loadFileAsString())[1].trim();
    marker().deleteFile();
    if (id.isEmpty()) return;
    auto all = StringArray::fromLines (skippedFile().loadFileAsString());
    all.removeEmptyStrings();
    all.addIfNotAlreadyThere (id);
    skippedFile().replaceWithText (all.joinIntoString ("\n") + "\n");
}
} // namespace

StringArray Taker::skipped()
{
    noteLastCrash();
    auto all = StringArray::fromLines (skippedFile().loadFileAsString());
    all.removeEmptyStrings();
    return all;
}

String Taker::closedKalibre()
{
    noteLastCrash();
    auto n = lastCrashName();
    lastCrashName() = {};
    return n;
}

Taker::Taker (PluginManager& pm, double sampleRate) : plugins (pm), rate (sampleRate > 0 ? sampleRate : 48000.0) {}

Taker::~Taker()
{
    stopTimer();
    if (current)
    {
        if (current->window) { current->window->empty(); editors::retire (std::move (current->window), 0); }
        letGo (std::move (current->instance));
        current.reset();
    }
    // Kalibre is closing, or the Plug-in Manager with it: what's left goes now.
    leaving.clear();
    marker().deleteFile();
}

void Taker::letGo (std::unique_ptr<AudioPluginInstance> p)
{
    if (p == nullptr) return;
    p->releaseResources();
    leaving.push_back ({ std::move (p), Time::getMillisecondCounter() + (uint32) letGoAfterMs });
}

void Taker::letGoOfOld (bool all)
{
    const auto now = Time::getMillisecondCounter();
    leaving.erase (std::remove_if (leaving.begin(), leaving.end(), [&] (const Leaving& l) { return all || (int32) (now - l.at) >= 0; }),
                   leaving.end());
}

void Taker::start (std::vector<PluginDescription> list, Progress progress, std::function<void (const Summary&)> finished)
{
    if (busy()) return;
    folder().createDirectory();
    noteLastCrash();
    queue = std::move (list);
    total = (int) queue.size();
    done = 0;
    summary = {};
    onProgress = std::move (progress);
    onFinished = std::move (finished);
    stage = Stage::Next;
    startTimer (10);
}

void Taker::stop()
{
    if (! busy()) return;
    queue.clear();
    if (current)
    {
        if (current->window) { current->window->empty(); editors::retire (std::move (current->window), 0); }
        letGo (std::move (current->instance));
        current.reset();
        marker().deleteFile();
    }
    finish (true);
}

void Taker::timerCallback()
{
    // Plug-ins whose time has come are let go of here, in a message of their own.
    letGoOfOld (false);
    switch (stage)
    {
        case Stage::Shoot: stopTimer(); shoot(); break;
        case Stage::Close: stopTimer(); close(); break;
        case Stage::Next:  stopTimer(); next(); break;
        case Stage::Idle:  if (leaving.empty()) stopTimer(); break;
    }
}

void Taker::next()
{
    if (queue.empty()) { finish (false); return; }
    auto desc = queue.front();
    queue.erase (queue.begin());
    ++done;
    if (onProgress) onProgress (done, total, desc.name);
    stage = Stage::Next;

    if (skipped().contains (desc.createIdentifierString()))
    {
        ++summary.skipped;
        summary.problems.add (desc.name + ": left out, as it closed Kalibre the last time its picture was taken");
        startTimer (10);
        return;
    }
    // Noted before it's opened: if it takes Kalibre down, it's left out next time.
    marker().replaceWithText (desc.createIdentifierString() + "\n" + desc.name + "\n");
    current = std::make_unique<Current>();
    current->desc = desc;
    String error;
    current->instance = plugins.create (PluginManager::refFor (desc), rate, 512, error);
    if (! current->instance)
    {
        ++summary.failed;
        summary.problems.add (desc.name + ": " + (error.isNotEmpty() ? error : String ("it didn't open")));
        current.reset();
        marker().deleteFile();
        startTimer (10);
        return;
    }
    current->instance->prepareToPlay (rate, 512);
    if (! current->instance->hasEditor())
    {
        ++summary.noWindow;
        letGo (std::move (current->instance));
        current.reset();
        marker().deleteFile();
        startTimer (10);
        return;
    }
    current->window = std::make_unique<PictureWindow> (*current->instance);
    stage = Stage::Shoot;
    startTimer (settleMs);
}

void Taker::shoot()
{
    stage = Stage::Next;
    if (! current) { startTimer (10); return; }
    auto& w = *current->window;
    if (w.generic() || w.content() == nullptr) ++summary.noWindow;
    else
    {
        const auto img = grab (*w.content());
        if (img.isValid() && ! looksBlank (img) && keep (img, current->desc)) ++summary.taken;
        else
        {
            ++summary.failed;
            summary.problems.add (current->desc.name + ": its window showed nothing Kalibre could read");
        }
    }
    // The plug-in's window closes now, in a message of its own (0056); the window
    // itself a little later (0115); the plug-in later still (letGo).
    current->window->empty();
    editors::retire (std::move (current->window), 1000);
    stage = Stage::Close;
    startTimer (250);
}

void Taker::close()
{
    if (current) letGo (std::move (current->instance));
    current.reset();
    marker().deleteFile();
    stage = Stage::Next;
    startTimer (50);
}

void Taker::finish (bool stopped)
{
    stage = Stage::Idle;
    stopTimer();
    if (! leaving.empty()) startTimer (250);   // still to be let go of
    summary.stopped = stopped;
    auto f = onFinished;
    onFinished = nullptr;
    onProgress = nullptr;
    if (f) f (summary);
}

} // namespace kal::pictures
