#include "Check.h"
#include "Demo.h"
#include "MainComponent.h"

using namespace kal;
using namespace juce;

TEST ("window: every word is 7:1 on what it sits on")
{
    CHECK (theme::contrast (theme::text, theme::ground) >= 7.0);
    CHECK (theme::contrast (theme::textSecondary, theme::ground) >= 7.0);
    CHECK (theme::contrast (theme::textSecondary, theme::sunken) >= 7.0);
    CHECK (theme::contrast (theme::textOnRaised, theme::raised) >= 7.0);
    CHECK (theme::contrast (theme::warnText, theme::ground) >= 7.0);
    CHECK (theme::contrast (theme::ink, theme::control) >= 7.0);
    CHECK (theme::contrast (theme::ink, theme::accent) >= 7.0);
    CHECK (theme::contrast (theme::ink, theme::controlHeld) >= 4.5);
    CHECK (theme::contrast (theme::text, theme::roll) >= 7.0);
    // Things to see and grab: 3:1.
    CHECK (theme::contrast (theme::edge, theme::ground) >= 3.0);
    CHECK (theme::contrast (theme::scrollGrab, theme::sunken) >= 3.0);
}

TEST ("window: every grey leans blue (R < G < B)")
{
    for (auto c : { theme::control, theme::ground, theme::ink, theme::roll, theme::sunken, theme::lane, theme::raised,
                    theme::rule, theme::edge, theme::text, theme::textSecondary, theme::controlHover, theme::controlHeld })
        CHECK (c.getRed() <= c.getGreen() && c.getGreen() <= c.getBlue() && c.getRed() < c.getBlue());
}

TEST ("window: lays out at its smallest and at 200% text without anything off the edge")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = demo::song();
        c.refresh();
        MainComponent w (c);
        for (float scale : { 1.0f, 2.0f })
            for (auto size : { Rectangle<int> (900, 560), Rectangle<int> (1440, 900) })
            {
                w.setTextSize (scale);
                w.setBounds (size);
                std::function<void (Component&)> walk = [&] (Component& comp)
                {
                    for (auto* child : comp.getChildren())
                    {
                        if (! child->isVisible()) continue;
                        if (dynamic_cast<Button*> (child) != nullptr)
                        {
                            const auto b = child->getBounds();
                            if (b.getRight() > comp.getWidth() + 1)
                                std::printf ("  off the edge: %s in %s at %dx%d, %.0f%%\n", child->getTitle().toRawUTF8(),
                                             comp.getTitle().toRawUTF8(), size.getWidth(), size.getHeight(), scale * 100);
                            CHECK (b.getRight() <= comp.getWidth() + 1);
                            CHECK (b.getHeight() >= 18);
                        }
                        if (dynamic_cast<Viewport*> (child) == nullptr) walk (*child);
                    }
                };
                walk (w);
            }
        w.setTextSize (1.0f);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("window: tooltips are broken into lines of about 64 characters")
{
    const auto t = theme::wrapTip (String::repeatedString ("word ", 40));
    for (auto& line : StringArray::fromLines (t)) CHECK (line.length() <= 66);
}

TEST ("window: the menus are all there")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    MainComponent w (c);
    const auto names = w.getMenuBarNames();
    CHECK (names.contains ("File") && names.contains ("Edit") && names.contains ("Track") && names.contains ("Transport")
           && names.contains ("Automation") && names.contains ("View") && names.contains ("Scripts"));
    for (int i = 0; i < names.size(); ++i) CHECK (w.getMenuForIndex (i, names[i]).getNumItems() > 0);
}

TEST ("window: a key can be given to another command, the menus say so, and a workspace brings a layout back")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = demo::song();
        c.refresh();
        MainComponent window (c);
        c.keys.file = File();   // not kept
        // M mutes; give it to the mixer instead, and Mute loses it.
        c.keys.set ("mixer", KeyPress ('m'));
        CHECK (! c.keys.keyFor ("mute").isValid());
        c.zoneTab = ZoneTab::Devices;
        CHECK (window.keyPressed (KeyPress ('m')));
        CHECK (c.zoneTab == ZoneTab::Mixer);
        CHECK_EQ (c.keys.textFor ("mixer"), String ("M"));
        c.keys.resetAll();
        CHECK (c.keys.keyFor ("mute") == KeyPress ('m'));

        // A workspace: the meters at 200 points high, then something else, then back.
        c.zoneTab = ZoneTab::Meters;
        window.lowerHeight = 200;
        CHECK (window.saveWorkspace ("Test Space"));
        c.zoneTab = ZoneTab::Editor;
        window.lowerHeight = 400;
        CHECK (window.loadWorkspace (window.workspacesFolder().getChildFile ("Test Space.json")));
        CHECK (c.zoneTab == ZoneTab::Meters);
        CHECK_EQ (window.lowerHeight, 200);
        window.workspacesFolder().getChildFile ("Test Space.json").deleteFile();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

namespace
{
MouseEvent mouseAt (Component& comp, Point<int> p, Point<int> down, bool dragged)
{
    return MouseEvent (Desktop::getInstance().getMainMouseSource(), p.toFloat(), ModifierKeys (ModifierKeys::leftButtonModifier),
                       MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, Time::getCurrentTime(),
                       down.toFloat(), Time::getCurrentTime(), 1, dragged);
}

void dragBy (Component& comp, Point<int> from, int dx)
{
    comp.mouseDown (mouseAt (comp, from, from, false));
    for (int i = 1; i <= 8; ++i) comp.mouseDrag (mouseAt (comp, from + Point<int> (dx * i / 8, 0), from, true));
    comp.mouseUp (mouseAt (comp, from + Point<int> (dx, 0), from, true));
}
} // namespace

TEST ("browser: dragged narrower than its smallest it shuts, leaving a bar at the left; dragging the bar, or a click, opens it again (0094)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        MainComponent w (c);
        w.setSize (1440, 900);
        w.browserWidth = 320;
        w.showBrowser (Browser::Tab::Plugins);
        auto handle = w.browserHandle();
        CHECK (! handle.isEmpty());
        // Narrower: it stops at its smallest.
        dragBy (w, handle.getCentre(), -100);
        CHECK (w.browserShown);
        CHECK_EQ (w.browserBounds().getWidth(), theme::px (240));
        // Past that, it shuts: a thin bar at the window's left edge is left.
        handle = w.browserHandle();
        dragBy (w, handle.getCentre(), -200);
        CHECK (! w.browserShown);
        handle = w.browserHandle();
        CHECK_EQ (handle.getX(), 0);
        CHECK (handle.getWidth() > 0 && handle.getWidth() <= theme::px (8));
        CHECK (handle.getHeight() > 300);
        // Dragging the bar opens it, as wide as the drag.
        dragBy (w, handle.getCentre(), 330);
        CHECK (w.browserShown);
        CHECK (std::abs (w.browserBounds().getWidth() - 330) <= theme::px (8));
        // Shut with the key, then a click on the bar opens it as wide as it was.
        w.hideBrowser();
        CHECK (! w.browserShown);
        handle = w.browserHandle();
        w.mouseDown (mouseAt (w, handle.getCentre(), handle.getCentre(), false));
        w.mouseUp (mouseAt (w, handle.getCentre(), handle.getCentre(), false));
        CHECK (w.browserShown);
        CHECK (std::abs (w.browserBounds().getWidth() - 330) <= theme::px (8));
        w.hideBrowser();   // as it was: the window saves it for the next one
        w.browserWidth = 320;
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

namespace
{
void dragUpBy (Component& comp, Point<int> from, int dy)
{
    comp.mouseDown (mouseAt (comp, from, from, false));
    for (int i = 1; i <= 8; ++i) comp.mouseDrag (mouseAt (comp, from + Point<int> (0, -dy * i / 8), from, true));
    comp.mouseUp (mouseAt (comp, from + Point<int> (0, -dy), from, true));
}
} // namespace

TEST ("lower zone: dragged lower than its smallest it shuts, leaving a bar at the bottom; dragging the bar, a click, or asking for a tab opens it")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        MainComponent w (c);
        w.setSize (1440, 900);
        w.lowerHeight = 320;
        w.showLowerZone();
        auto handle = w.lowerHandle();
        CHECK (! handle.isEmpty());
        // Lower: it stops at its smallest.
        dragUpBy (w, handle.getCentre(), -250);
        CHECK (w.lowerShown);
        CHECK_EQ (w.lowerBounds().getHeight(), theme::px (120));
        // Past that, it shuts: a thin bar along the bottom is left, and the tracks take the room.
        handle = w.lowerHandle();
        dragUpBy (w, handle.getCentre(), -100);
        CHECK (! w.lowerShown);
        handle = w.lowerHandle();
        CHECK_EQ (handle.getBottom(), w.transportBounds().getY());   // just above the transport (0095)
        CHECK (handle.getHeight() > 0 && handle.getHeight() <= theme::px (8));
        CHECK (handle.getWidth() > 600);
        // Dragging the bar up opens it, as tall as the drag.
        dragUpBy (w, handle.getCentre(), 300);
        CHECK (w.lowerShown);
        CHECK (std::abs (w.lowerBounds().getHeight() - 300) <= theme::px (8));
        // Shut again; a click on the bar opens it as tall as it was.
        w.hideLowerZone();
        handle = w.lowerHandle();
        w.mouseDown (mouseAt (w, handle.getCentre(), handle.getCentre(), false));
        w.mouseUp (mouseAt (w, handle.getCentre(), handle.getCentre(), false));
        CHECK (w.lowerShown);
        CHECK (std::abs (w.lowerBounds().getHeight() - 300) <= theme::px (8));
        // Shut, a tab asked for (the Mixer's key) opens it on that tab.
        w.hideLowerZone();
        w.keyPressed (KeyPress ('x'));
        CHECK (w.lowerShown);
        CHECK (c.zoneTab == ZoneTab::Mixer);
        w.lowerHeight = 320;
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("lower zone and browser: a double-click on their dots opens them at their first size, shut or not (0095)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        MainComponent w (c);
        w.setSize (1440, 900);
        auto doubleClick = [&w] (Point<int> p) { w.mouseDoubleClick (mouseAt (w, p, p, false)); };
        // Shut, from the bar along the bottom.
        w.lowerHeight = 200;
        w.hideLowerZone();
        doubleClick (w.lowerHandle().getCentre());
        CHECK (w.lowerShown);
        CHECK_EQ (w.lowerBounds().getHeight(), MainComponent::defaultLowerHeight);
        // Open but tall: back to its first height.
        w.lowerHeight = 520;
        w.resized();
        doubleClick (w.lowerHandle().getCentre());
        CHECK_EQ (w.lowerBounds().getHeight(), MainComponent::defaultLowerHeight);
        // The browser, shut, from its bar at the left.
        w.browserWidth = 500;
        w.hideBrowser();
        doubleClick (w.browserHandle().getCentre());
        CHECK (w.browserShown);
        CHECK_EQ (w.browserBounds().getWidth(), MainComponent::defaultBrowserWidth);
        w.hideBrowser();   // as it was: the window saves it for the next one
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("transport: Go to End goes to where the last clip ends; Stop stops and goes back to the start; Go to Start, Go to End, Stop, Play, Record in that order (0107)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id a = edit::addTrack (c.project, TrackKind::Track, "A");
        const Id b = edit::addTrack (c.project, TrackKind::Track, "B");
        edit::addMidiClip (c.project, a, 0, PPQ * 4);
        edit::addMidiClip (c.project, b, PPQ * 10, PPQ * 6);
        c.refresh();
        MainComponent w (c);
        w.setSize (1440, 900);
        auto& t = w.transportBar();
        CHECK (t.start.getRight() < t.end.getX());
        CHECK (t.end.getRight() < t.stopButton.getX());
        CHECK (t.stopButton.getRight() < t.playButton.getX());
        CHECK (t.playButton.getRight() < t.recordButton.getX());

        t.end.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK_EQ (c.cursor, PPQ * 16);

        c.locate (PPQ * 2);
        c.play();
        CHECK (engine.isPlaying());
        t.update();
        CHECK (t.playButton.getToggleState());   // lit while playing, still ▶
        t.stopButton.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (! engine.isPlaying());
        CHECK_EQ (c.cursor, (Tick) 0);
        // Stopped already: back to the start too.
        c.locate (PPQ * 3);
        c.stopAndGoToStart();
        CHECK_EQ (c.cursor, (Tick) 0);
        // The keys: End and Home.
        CHECK (c.keys.textFor ("end").isNotEmpty());
        CHECK (c.keys.textFor ("stopStart").isNotEmpty());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("transport: Go to Start, Play, Record, Loop, Punch, Click and Count-in sit in the middle along the bottom, under the lower zone (0095)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1440, 900);
        w.lowerHeight = 320;
        w.showLowerZone();
        const auto bar = w.transportBounds();
        CHECK_EQ (bar.getBottom(), w.getHeight());
        CHECK_EQ (bar.getX(), 0);
        CHECK_EQ (bar.getWidth(), w.getWidth());
        CHECK (w.lowerBounds().getBottom() <= bar.getY());
        auto& t = w.transportBar();
        // Centred: as much room either side.
        const int left = t.start.getX(), right = t.getWidth() - t.countIn.getRight();
        CHECK (std::abs (left - right) <= 2);
        for (auto* b : { &t.start, &t.end, &t.stopButton, &t.playButton, &t.recordButton, &t.loop, &t.punch, &t.click, &t.countIn })
            CHECK (b->isVisible());
        // They do what they did at the top.
        CHECK (! c.project.metronome.on);
        t.click.triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (c.project.metronome.on);
        // Hidden from the bars' right-click menu, it leaves the row, still centred.
        c.hiddenInTopBar = { "Punch", "Count-in" };
        t.update();
        CHECK (! t.punch.isVisible() && ! t.countIn.isVisible());
        CHECK (std::abs (t.start.getX() - (t.getWidth() - t.click.getRight())) <= 2);
        c.hiddenInTopBar.clear();
        // Shut, the lower zone's bar sits just above it.
        w.hideLowerZone();
        CHECK_EQ (w.lowerHandle().getBottom(), bar.getY());
        w.lowerHeight = 320;
        w.showLowerZone();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("export: Export the Mix asks first, with Record the Tail and Export the Second Pass, as last chosen (0097)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1440, 900);
        w.exportAudio (false);
        AlertWindow* asked = nullptr;
        for (auto* ch : w.getChildren()) if (auto* a = dynamic_cast<AlertWindow*> (ch)) asked = a;
        CHECK (asked != nullptr);
        if (asked)
        {
            CHECK_EQ (asked->getName(), String ("Export the Mix"));
            StringArray ticks;
            for (int i = 0; i < asked->getNumCustomComponents(); ++i)
                if (auto* b = dynamic_cast<ToggleButton*> (asked->getCustomComponent (i))) ticks.add (b->getButtonText());
            CHECK (ticks.contains ("Record the Tail"));
            CHECK (ticks.contains ("Export the Second Pass"));
            asked->exitModalState (0);   // Cancel: nothing exported, nothing asked after
        }
        // The question is let go of on the message loop: a busy machine (the Mac on CI) takes longer.
        auto asking = [&w] { for (auto* ch : w.getChildren()) if (dynamic_cast<AlertWindow*> (ch)) return true; return false; };
        for (int i = 0; i < 100 && asking(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (! asking());
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("lower zone: the clip launcher is gone; a workspace saved showing it opens on Devices (0098)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1440, 900);
        // No Launcher tab, and G does nothing to the zone.
        std::function<bool (Component&)> hasLauncher = [&] (Component& comp)
        {
            if (auto* b = dynamic_cast<Button*> (&comp); b && b->getButtonText() == "Launcher") return true;
            for (auto* ch : comp.getChildren()) if (hasLauncher (*ch)) return true;
            return false;
        };
        CHECK (! hasLauncher (w));
        c.zoneTab = ZoneTab::Mixer;
        w.keyPressed (KeyPress ('g'));
        CHECK (c.zoneTab == ZoneTab::Mixer);
        // A workspace from before, saved with the launcher (3) showing.
        const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-old-workspace.json");
        DynamicObject::Ptr o = new DynamicObject();
        o->setProperty ("zoneTab", 3);
        f.replaceWithText (JSON::toString (var (o.get())));
        c.zoneTab = ZoneTab::Mixer;
        CHECK (w.loadWorkspace (f));
        CHECK (c.zoneTab == ZoneTab::Devices);
        o->setProperty ("zoneTab", 4);   // the meters, as numbered before
        f.replaceWithText (JSON::toString (var (o.get())));
        CHECK (w.loadWorkspace (f));
        CHECK (c.zoneTab == ZoneTab::Meters);
        f.deleteFile();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}
