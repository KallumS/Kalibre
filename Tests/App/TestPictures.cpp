// Plug-ins' pictures in the browser, and its headings folded away (decision 0138).
#include "Check.h"

#include "Browser.h"
#include "Controller.h"
#include "DeviceRack.h"
#include "Edit.h"
#include "Engine.h"
#include "MainComponent.h"
#include "PluginManagerView.h"
#include "PluginPictures.h"
#include "Theme.h"

using namespace juce;
using namespace kal;

extern juce::uint32 painterWindowGone, painterGone;   // TestPlugins.cpp
extern int painterWindowGoneIn, painterGoneIn, messageNumber;

namespace
{
File pictureFolder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("Pictures");
    f.deleteRecursively();
    f.createDirectory();
    pictures::setFolder (f);
    return f;
}

PluginDescription fake (const String& name, const String& format = "Fake", bool instrument = true)
{
    PluginDescription d;
    d.name = name;
    d.manufacturerName = "Tests";
    d.pluginFormatName = format;
    d.fileOrIdentifier = "fake:ok:" + name;
    d.uniqueId = d.deprecatedUid = name.hashCode();
    d.isInstrument = instrument;
    d.category = instrument ? "Instrument" : "Effect";
    d.numInputChannels = instrument ? 0 : 2;
    d.numOutputChannels = 2;
    return d;
}

// A plug-in list of the test's own, never Kalibre's saved one.
File listFolder (const String& name)
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile (name);
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

Image twoColours (int w, int h)
{
    Image img (Image::RGB, w, h, true);
    Graphics g (img);
    g.fillAll (Colours::darkblue);
    g.setColour (Colours::orange);
    g.fillRect (w / 4, h / 4, w / 2, h / 2);
    return img;
}

void runFor (int ms, std::function<bool()> until = {})
{
    const auto end = Time::getMillisecondCounter() + (uint32) ms;
    while (Time::getMillisecondCounter() < end && ! (until && until()))
    {
        // Each turn of the loop a message of its own, numbered, so a test can tell whether two things happened in one.
        MessageManager::callAsync ([] { ++messageNumber; });
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

// Near enough: a picture read back from the screen may be a shade off.
bool near (Colour a, Colour b)
{
    return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen()) + std::abs (a.getBlue() - b.getBlue()) < 24;
}

Browser* browserOf (MainComponent& w)
{
    for (auto* ch : w.getChildren()) if (auto* b = dynamic_cast<Browser*> (ch)) return b;
    return nullptr;
}
} // namespace

TEST ("pictures: kept by maker and name, one for every format of a plug-in, read back smaller for the browser (0138)")
{
    const auto folder = pictureFolder();
    const auto vst3 = fake ("Serum", "VST3"), clap = fake ("Serum", "CLAP");
    CHECK_EQ (pictures::fileFor (vst3).getFileName(), String ("Tests - Serum.png"));
    CHECK (pictures::fileFor (vst3) == pictures::fileFor (clap));
    CHECK (pictures::fileFor ("A/B: C?", "Maker").getParentDirectory() == folder);   // no folder made of a slash
    CHECK (! pictures::has (vst3));
    CHECK (pictures::of (vst3).isNull());

    CHECK (pictures::keep (twoColours (2400, 1200), vst3));
    CHECK (pictures::has (clap));
    // Written no wider than 1600 pixels; read for the browser smaller still, its shape kept.
    const auto written = ImageFileFormat::loadFrom (pictures::fileFor (vst3));
    CHECK_EQ (written.getWidth(), 1600);
    CHECK_EQ (written.getHeight(), 800);
    const auto shown = pictures::of (clap);
    CHECK (shown.isValid() && shown.getWidth() <= 720 && shown.getHeight() <= 360);
    CHECK (std::abs (shown.getWidth() - shown.getHeight() * 2) <= 2);

    CHECK (pictures::looksBlank (Image (Image::RGB, 50, 50, true)));
    CHECK (! pictures::looksBlank (twoColours (50, 50)));
    pictures::setFolder ({});
}

TEST ("pictures: Take Pictures opens each plug-in in a window, photographs it, closes it; one with no window is passed over (0138)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    pictureFolder();
    {
        PluginManager plugins (listFolder ("PictureList"));
        const auto painter = fake ("Painter"), riffer = fake ("Riffer"), broken = fake ("Broken");
        for (auto& d : { painter, riffer, broken }) plugins.known().addType (d);
        pictures::Taker::settleMs = 400;
        pictures::Taker taker (plugins, 48000.0);
        bool finished = false;
        pictures::Taker::Summary result;
        std::vector<String> seen;
        taker.start ({ painter, riffer, broken },
                     [&] (int done, int total, const String& name) { CHECK_EQ (total, 3); seen.push_back (String (done) + " " + name); },
                     [&] (const pictures::Taker::Summary& s) { result = s; finished = true; });
        CHECK (taker.busy());
        runFor (15000, [&] { return finished; });
        CHECK (finished);
        CHECK_EQ ((int) seen.size(), 3);
        CHECK_EQ (result.taken, 1);
        CHECK_EQ (result.noWindow, 1);    // Riffer has none
        CHECK_EQ (result.failed, 1);      // Broken doesn't open
        CHECK (! taker.busy());
        // The picture is the plug-in's window as it drew itself: blue, a light square in the middle.
        CHECK (pictures::has (painter));
        const auto img = ImageFileFormat::loadFrom (pictures::fileFor (painter));
        CHECK (img.isValid());
        if (img.isValid())
        {
            CHECK (std::abs (img.getWidth() * 200 - img.getHeight() * 320) <= 400);   // its shape
            CHECK (near (img.getPixelAt (4, 4), Colour (0xff2a7fd4)));
            CHECK (near (img.getPixelAt (img.getWidth() / 2, img.getHeight() / 2), Colour (0xfff0e0a0)));
        }
        CHECK (! pictures::fileFor (riffer).existsAsFile());
        // The plug-in is let go of well after its window, never in the same message: on the Mac
        // the window's last pieces are let go of after the message, and crashed when the
        // plug-in had gone with them (the musician's Mac, 26 November).
        runFor (3000, [&] { return painterGone != 0; });
        CHECK (painterWindowGone != 0 && painterGone != 0);
        CHECK (painterGoneIn > painterWindowGoneIn);
        CHECK ((juce::int32) (painterGone - painterWindowGone) >= 1500);
        // Nothing left noted as having closed Kalibre.
        CHECK (pictures::Taker::skipped().isEmpty());

        // One being photographed when Kalibre closed is left out from then on.
        pictures::folder().getChildFile ("Taking.txt").replaceWithText (painter.createIdentifierString());
        CHECK (pictures::Taker::skipped().contains (painter.createIdentifierString()));
        finished = false;
        taker.start ({ painter }, {}, [&] (const pictures::Taker::Summary& s) { result = s; finished = true; });
        runFor (5000, [&] { return finished; });
        CHECK_EQ (result.skipped, 1);
        CHECK_EQ (result.taken, 0);
        pictures::Taker::settleMs = 1500;
    }
    pictures::setFolder ({});
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("pictures: the camera button on a plug-in's panel photographs its window and puts it back in the rack (0138)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    pictureFolder();
    {
        PluginManager plugins (listFolder ("PictureList"));
        Engine engine (plugins);
        Controller c (engine);
        const auto painter = fake ("Painter");
        plugins.known().addType (painter);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
        c.focusTrack = t;
        c.refresh();
        c.addPlugin (t, painter);
        const auto* track = c.project.track (t);
        CHECK (track != nullptr && track->devices.size() == 1);
        if (track != nullptr && track->devices.size() == 1)
        {
            const Id dev = track->devices.front().id;
            CHECK (engine.instance (dev) != nullptr);
            DeviceRack rack (c);
            rack.setSize (1200, 320);
            rack.rebuild();
            pictures::Taker::settleMs = 400;
            int told = -1;
            rack.onPhotographed = [&] (bool ok) { told = ok ? 1 : 0; };
            rack.photograph (dev);
            CHECK (rack.isFloating (dev));    // shown at its own size meanwhile
            runFor (8000, [&] { return told >= 0; });
            CHECK_EQ (told, 1);
            CHECK (! rack.isFloating (dev));  // and back in the rack
            CHECK (pictures::has (painter));
            pictures::Taker::settleMs = 1500;
        }
    }
    pictures::setFolder ({});
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("browser: a heading folds its plug-ins away and back; while searching, everything found shows; a picture sits under its plug-in (0138)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    pictureFolder();
    {
        PluginManager plugins (listFolder ("PictureList"));
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        plugins.known().addType (fake ("Alpha Synth"));
        plugins.known().addType (fake ("Zeta Verb", "Fake", false));
        MainComponent w (c);
        w.setSize (1440, 900);
        w.showBrowser (Browser::Tab::Plugins);
        auto* browser = browserOf (w);
        CHECK (browser != nullptr);
        if (! browser) return;
        browser->setShutHeadings ({ false, false, false });
        browser->showPictures (true);
        auto& list = browser->pluginList();
        auto* model = list.getListBoxModel();
        auto rows = [&]
        {
            StringArray r;
            for (int i = 0; i < model->getNumRows(); ++i) r.add (model->getNameForRow (i));
            return r;
        };
        CHECK_EQ (rows().joinIntoString ("|"), String ("Instruments|Alpha Synth|Audio Effects|Zeta Verb"));

        // Click Instruments: folded away, the heading stays with its count.
        model->listBoxItemClicked (0, MouseEvent (Desktop::getInstance().getMainMouseSource(), {}, ModifierKeys (ModifierKeys::leftButtonModifier),
                                                   MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                                   Time::getCurrentTime(), {}, Time::getCurrentTime(), 1, false));
        CHECK_EQ (rows().joinIntoString ("|"), String ("Instruments, folded|Audio Effects|Zeta Verb"));
        CHECK (browser->shutHeadings()[1]);
        // Searching shows what's found, folded or not.
        for (auto* ch : browser->getChildren())
            if (auto* ed = dynamic_cast<TextEditor*> (ch)) { ed->setText ("alpha", true); }
        runFor (200);
        CHECK_EQ (rows().joinIntoString ("|"), String ("Instruments|Alpha Synth"));
        for (auto* ch : browser->getChildren())
            if (auto* ed = dynamic_cast<TextEditor*> (ch)) { ed->setText ({}, true); }
        runFor (200);
        browser->toggleHeading (1);
        CHECK_EQ (rows().joinIntoString ("|"), String ("Instruments|Alpha Synth|Audio Effects|Zeta Verb"));

        // A picture kept: shown at once, under its plug-in's name, four rows tall.
        CHECK (pictures::keep (twoColours (600, 400), fake ("Alpha Synth")));
        runFor (200);
        CHECK_EQ (rows().joinIntoString ("|"),
                  String ("Instruments|Alpha Synth|Picture of Alpha Synth|Picture of Alpha Synth|Picture of Alpha Synth|Picture of Alpha Synth|Audio Effects|Zeta Verb"));
        // Choosing the picture chooses the plug-in, and dragging it drags the plug-in.
        list.selectRow (3);
        CHECK (list.isRowSelected (1));
        SparseSet<int> picture;
        picture.addRange ({ 2, 3 });
        CHECK (model->getDragSourceDescription (picture).toString().startsWith ("plugin:"));
        // Pictures off (the Pictures button): names only, and back.
        browser->showPictures (false);
        CHECK_EQ (rows().joinIntoString ("|"), String ("Instruments|Alpha Synth|Audio Effects|Zeta Verb"));
        browser->showPictures (true);
        CHECK (rows().contains ("Picture of Alpha Synth"));
        // Drawn: the picture's colours where it is.
        auto shot = w.createComponentSnapshot (w.getLocalBounds());
        CHECK (shot.isValid());
        browser->setShutHeadings ({ false, false, false });
        browser->showPictures (true);
    }
    pictures::setFolder ({});
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "CrashReports.h"

TEST ("crash reports: a run that ended without quitting and wrote no report brings in macOS's own; quitting leaves nothing to find (0140)")
{
    const auto base = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("SystemReports");
    base.deleteRecursively();
    const auto reports = base.getChildFile ("Crash Reports"), system = base.getChildFile ("DiagnosticReports");
    system.createDirectory();

    // A first start finds nothing; a quit clears the mark, so the next start finds nothing either.
    CHECK (! crash::markRunning (reports).has_value());
    crash::markQuit (reports);
    CHECK (! crash::markRunning (reports).has_value());
    // Then it ends without quitting: the next start knows, and since when.
    const auto started = crash::markRunning (reports);
    CHECK (started.has_value());

    // macOS's report of it, as the musician's Mac wrote it (an .ips: a line about it, then the report).
    Thread::sleep (1100);   // newer than the start, by the file's clock
    system.getChildFile ("Kalibre-2026-11-26-111219.ips").replaceWithText (
        "{\"app_name\":\"Kalibre\",\"app_version\":\"1.3.0\",\"name\":\"Kalibre\"}\n"
        "{\"procName\":\"Kalibre\",\"exception\":{\"type\":\"EXC_BAD_ACCESS\",\"signal\":\"SIGSEGV\",\"subtype\":\"KERN_INVALID_ADDRESS at 0x0\"},"
        "\"faultingThread\":0,\"threads\":[{\"triggered\":true,\"frames\":[{\"imageIndex\":0,\"symbol\":\"objc_msgSend\"},"
        "{\"imageIndex\":1,\"symbol\":\"objc_autoreleasePoolPop\"},{\"imageIndex\":2,\"symbol\":\"juce::MessageQueue::runLoopCallback()\"}]}],"
        "\"usedImages\":[{\"name\":\"libobjcMsgSend.dylib\"},{\"name\":\"libobjc.A.dylib\"},{\"name\":\"Kalibre\"}]}\n");
    // One older than the start isn't this crash.
    auto old = system.getChildFile ("Kalibre-2026-11-20-090000.ips");
    old.replaceWithText ("{}\n{}");
    old.setLastModificationTime (Time::getCurrentTime() - RelativeTime::days (6));

    const auto report = crash::bringInSystemReport (reports, system, *started - RelativeTime::seconds (1), "1.3.1");
    CHECK (report.existsAsFile());
    CHECK (reports.getChildFile ("Kalibre-2026-11-26-111219.ips").existsAsFile());   // macOS's, beside it
    CHECK (! reports.getChildFile ("Kalibre-2026-11-20-090000.ips").existsAsFile());
    // Told on the next start as any crash is: in Kalibre's words, the version that crashed.
    CHECK (crash::since (reports, Time()).contains (report));
    CHECK_EQ (crash::summary (report), String ("a bad memory access"));
    const auto text = report.loadFileAsString();
    CHECK (text.startsWith ("Kalibre 1.3.0 quit unexpectedly"));
    CHECK (text.contains ("objc_autoreleasePoolPop  (libobjc.A.dylib)"));
    // Nothing since: nothing brought in.
    CHECK (crash::bringInSystemReport (reports, system, Time::getCurrentTime() + RelativeTime::minutes (1), "1.3.1") == File());
    base.deleteRecursively();
}

TEST ("pictures: the plug-in being photographed when Kalibre closed is named once, as Kalibre starts (0140)")
{
    pictureFolder();
    pictures::folder().getChildFile ("Taking.txt").replaceWithText ("VST3-Active Equalizer-1-2\nActive Equalizer\n");
    CHECK_EQ (pictures::Taker::closedKalibre(), String ("Active Equalizer"));
    CHECK (pictures::Taker::closedKalibre().isEmpty());   // once
    CHECK (pictures::Taker::skipped().contains ("VST3-Active Equalizer-1-2"));
    pictures::setFolder ({});
}
