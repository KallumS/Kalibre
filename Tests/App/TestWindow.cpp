#include "Check.h"

#include <set>
#include "Demo.h"
#include "MainComponent.h"
#include "MainWindow.h"

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

TEST ("themes: about ten, each with every word 7:1 on what it sits on, buttons' ink 7:1, edges 3:1; the first is the house colours, unchanged (0132)")
{
    const auto& all = theme::palettes();
    CHECK (all.size() >= 9 && all.size() <= 12);
    CHECK_EQ (String (all[0].name), String ("Midnight Yellow"));
    CHECK (all[0].accent == Colour (0xffFFF200) && all[0].ground == Colour (0xff23272E) && all[0].text == Colour (0xffDDE1E7));
    std::set<String> names;
    int light = 0;
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& p = all[(size_t) i];
        names.insert (p.name);
        light += p.light ? 1 : 0;
        const String n (p.name);
        auto ok = [&n] (bool b, const char* what) { if (! b) std::printf ("    %s: %s\n", n.toRawUTF8(), what); CHECK (b); };
        ok (theme::contrast (p.text, p.ground) >= 7.0, "text on ground");
        ok (theme::contrast (p.text, p.roll) >= 7.0, "text on roll");
        ok (theme::contrast (p.text, p.sunken) >= 7.0, "text on sunken");
        ok (theme::contrast (p.textSecondary, p.ground) >= 7.0, "secondary on ground");
        ok (theme::contrast (p.textSecondary, p.sunken) >= 7.0, "secondary on sunken");
        ok (theme::contrast (p.textOnRaised, p.raised) >= 7.0, "secondary on raised");
        ok (theme::contrast (p.warnText, p.ground) >= 7.0, "warning words");
        ok (theme::contrast (p.ink, p.control) >= 7.0, "ink on a button");
        ok (theme::contrast (p.ink, p.accent) >= 7.0, "ink on a button that's on");
        ok (theme::contrast (p.ink, p.armAudio) >= 7.0 && theme::contrast (p.ink, p.armMidi) >= 7.0, "ink on armed");
        ok (theme::contrast (p.ink, p.controlHeld) >= 4.5, "ink on a held button");
        ok (theme::contrast (p.edge, p.ground) >= 3.0, "edges");
        ok (theme::contrast (p.scrollGrab, p.sunken) >= 3.0, "scroll bars");
        // Never the warning words' hue for "on" (red never for words, 0023).
        ok (std::abs (p.accent.getHue() - p.warnText.getHue()) > 0.04f || p.warnText.getSaturation() < 0.2f, "accent and warning apart");
    }
    CHECK_EQ (names.size(), all.size());   // each named once
    CHECK (light >= 1);                    // a light one among them
}

TEST ("themes: names on a section, a clip or a note read at 7:1 in every theme, light ones too (0132)")
{
    for (int i = 0; i < (int) theme::palettes().size(); ++i)
    {
        theme::usePalette (i);
        for (auto col : theme::trackColours())
            for (float depth : { 0.35f, 0.5f, 0.55f, 0.6f, 1.0f })
            {
                CHECK (theme::contrast (theme::text, theme::underWords (col, depth)) >= 7.0);
                CHECK (theme::contrast (theme::ink, theme::underWords (col, depth, theme::ink)) >= 7.0);
            }
    }
    theme::usePalette (0);
}

TEST ("themes: View > Themes sets every colour and the window's look, and draws it again; kept for next time (0132)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        MainComponent w (c);
        w.setSize (1200, 800);
        const auto light = [] { for (int i = 0; i < (int) theme::palettes().size(); ++i) if (theme::palettes()[(size_t) i].light) return i; return 0; }();
        w.setTheme (light);
        CHECK_EQ (theme::currentPalette(), light);
        CHECK (theme::ground == theme::palettes()[(size_t) light].ground);
        CHECK (theme::text == theme::palettes()[(size_t) light].text);
        CHECK (look.findColour (ResizableWindow::backgroundColourId) == theme::ground);
        CHECK (look.findColour (PopupMenu::backgroundColourId) == theme::raised);
        CHECK (theme::luminance (theme::ground) > theme::luminance (theme::text));   // dark words on a light ground
        // The View menu lists them, the one in use ticked.
        const auto view = w.getMenuForIndex (5, "View");
        PopupMenu::MenuItemIterator it (view);
        const PopupMenu* themes = nullptr;
        while (it.next()) if (it.getItem().text == "Themes") themes = it.getItem().subMenu.get();
        CHECK (themes != nullptr);
        if (themes)
        {
            int count = 0, ticked = -1;
            for (PopupMenu::MenuItemIterator t (*themes); t.next(); ++count) if (t.getItem().isTicked) ticked = count;
            CHECK_EQ (count, (int) theme::palettes().size());
            CHECK_EQ (ticked, light);
        }
        w.setTheme (0);
        CHECK (theme::ground == Colour (0xff23272E));
        CHECK (look.findColour (ResizableWindow::backgroundColourId) == Colour (0xff23272E));
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
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

TEST ("window: a shortcut works with nothing focused, as after a dialog closes (found in the UAT: ⇧⌘E did nothing until a click)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    edit::addTrack (c.project, TrackKind::Track, "Track 1");
    c.refresh();
    MainWindow window (c, false);
    Component::unfocusAllComponents();
    CHECK (Component::getCurrentlyFocusedComponent() == nullptr);
    // M mutes the chosen track: handled though nothing has the focus.
    CHECK (window.keyPressed (KeyPress ('m')));
    window.setVisible (false);
}

TEST ("window: the keys are under Help and the studio in the File menu, each a window of its own, not tabs of the browser (the musician's choice)")
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
        w.showBrowser (Browser::Tab::Plugins);
        // The browser has Plug-ins, Presets, Sounds and Audio; no Keys, no Studio.
        std::function<void (Component&, StringArray&)> buttons = [&] (Component& comp, StringArray& found)
        {
            if (auto* b = dynamic_cast<Button*> (&comp)) found.add (b->getButtonText());
            for (auto* ch : comp.getChildren()) buttons (*ch, found);
        };
        StringArray inBrowser;
        for (auto* ch : w.getChildren()) if (dynamic_cast<Browser*> (ch) != nullptr) buttons (*ch, inBrowser);
        CHECK (inBrowser.contains ("Plug-ins") && inBrowser.contains ("Presets") && inBrowser.contains ("Sounds") && inBrowser.contains ("Audio"));
        CHECK (! inBrowser.contains ("Keys"));
        CHECK (! inBrowser.contains ("Studio"));
        // The menus: File > Studio > Sync and Control Surfaces..., AI Agents...; Help > Keys...
        auto names = [] (const PopupMenu& m)
        {
            StringArray s;
            for (PopupMenu::MenuItemIterator it (m); it.next();) s.add (it.getItem().text);
            return s;
        };
        const auto bar = w.getMenuBarNames();
        const auto file = w.getMenuForIndex (bar.indexOf ("File"), "File");
        const PopupMenu* studio = nullptr;
        for (PopupMenu::MenuItemIterator it (file); it.next();)
            if (it.getItem().text == "Studio") studio = it.getItem().subMenu.get();
        CHECK (studio != nullptr);
        if (studio)
        {
            const auto items = names (*studio);
            CHECK (items.contains (ui::u8 ("Sync and Control Surfaces\xe2\x80\xa6")));
            CHECK (items.contains (ui::u8 ("AI Agents\xe2\x80\xa6")));
        }
        CHECK (names (w.getMenuForIndex (bar.indexOf ("Help"), "Help")).contains (ui::u8 ("Keys\xe2\x80\xa6")));
        // Not in the Kalibre menu as well: one place each.
        const auto app = names (w.appMenu());
        CHECK (! app.contains (ui::u8 ("AI Agents\xe2\x80\xa6")));
        CHECK (! app.contains (ui::u8 ("Sync and Control Surfaces\xe2\x80\xa6")));
        // Each opens a window of its own.
        auto& keys = w.showKeys();
        CHECK (keys.isVisible());
        CHECK (keys.getName() == "Keys");
        auto& st = w.showStudio();
        CHECK (st.isVisible());
        CHECK (st.getName() == "Studio");
        keys.setVisible (false);
        st.setVisible (false);
        // A workspace saved showing the old Keys tab (5) or Studio tab (3) opens on Plug-ins.
        CHECK (Browser::tabFromNumber (5) == Browser::Tab::Plugins);
        CHECK (Browser::tabFromNumber (3) == Browser::Tab::Plugins);
        CHECK (Browser::tabFromNumber (1) == Browser::Tab::Files);
        CHECK (Browser::tabFromNumber (4) == Browser::Tab::Presets);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("questions: shown near the top, under the top bar, across the middle (the musician's choice: a plug-in covered Save lower down)")
{
    const Rectangle<int> host (0, 0, 1440, 900), top (0, 0, 1440, 80), q (0, 0, 400, 200);
    const auto r = ui::questionBounds (host, top, q);
    CHECK_EQ (r.getY(), 92);
    CHECK_EQ (r.getCentreX(), 720);
    CHECK_EQ (r.getWidth(), 400);
    // Too tall for the room under the bar: kept inside the window.
    const auto tall = ui::questionBounds ({ 0, 0, 800, 300 }, top, q);
    CHECK (tall.getBottom() <= 300);
    CHECK (tall.getY() >= 0);
}

TEST ("browser: plug-ins under MIDI Effects, Instruments and Audio Effects; right-click Add to a New Track is one undo step (0125)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        auto describe = [] (const String& name, bool instrument)
        {
            PluginDescription d;
            d.name = name;
            d.pluginFormatName = "Kalibre";
            d.fileOrIdentifier = "test-gain:0";
            d.manufacturerName = "Tests";
            d.isInstrument = instrument;
            d.category = instrument ? "Instrument" : "Effect";
            d.numInputChannels = instrument ? 0 : 2;
            d.numOutputChannels = 2;
            d.uniqueId = name.hashCode();
            return d;
        };
        plugins.known().addType (describe ("Zeta Verb", false));
        plugins.known().addType (describe ("Alpha Synth", true));
        plugins.known().addType (describe ("Beta Comp", false));
        MainComponent w (c);
        w.setSize (1440, 900);
        w.showBrowser (Browser::Tab::Plugins);
        Browser* browser = nullptr;
        for (auto* ch : w.getChildren()) if (auto* b = dynamic_cast<Browser*> (ch)) browser = b;
        CHECK (browser != nullptr);
        if (! browser) return;
        auto* model = browser->pluginList().getListBoxModel();
        StringArray rows;
        for (int i = 0; i < model->getNumRows(); ++i) rows.add (model->getNameForRow (i));
        // Instruments before Audio Effects, each A to Z under its heading.
        // Instruments (Kalibre's own among them) before Audio Effects, each A to Z under its heading.
        const int inst = rows.indexOf ("Instruments"), fx = rows.indexOf ("Audio Effects");
        CHECK (inst >= 0 && fx > inst);
        CHECK (rows.indexOf ("Alpha Synth") == inst + 1);
        CHECK (rows.indexOf ("Beta Comp") == fx + 1 && rows.indexOf ("Zeta Verb") == fx + 2);
        CHECK (! rows.contains ("MIDI Effects"));   // none of those here: no heading
        // A heading can't be dragged; a plug-in can.
        SparseSet<int> heading, plugin;
        heading.addRange ({ inst, inst + 1 });
        plugin.addRange ({ inst + 1, inst + 2 });
        CHECK (model->getDragSourceDescription (heading).isVoid());
        CHECK (model->getDragSourceDescription (plugin).toString().startsWith ("plugin:"));

        // Add to a New Track: a track named for it, with it on, one undo step.
        const auto tracks = c.project.tracks.size();
        const auto steps = c.history.size();
        const Id t = c.addPluginOnNewTrack (describe ("Beta Comp", false));
        CHECK (t != 0);
        CHECK_EQ (c.project.tracks.size(), tracks + 1);
        CHECK_EQ (c.history.size(), steps + 1);
        if (auto* track = c.project.track (t))
        {
            CHECK_EQ (track->name, std::string ("Beta Comp"));
            CHECK_EQ (track->devices.size(), size_t (1));
        }
        CHECK_EQ (c.focusTrack, t);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "BuiltinInstruments.h"

TEST ("browser: a preset's Load on a New Track puts its plug-in on a new track with the preset loaded, one undo step (0125)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    c.refresh();
    Id dev = 0;
    c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
    // A preset: the synth's state with its cutoff turned down.
    KalibreSynth synth;
    if (auto* prm = dynamic_cast<RangedAudioParameter*> (synth.getParameters()[0])) prm->setValueNotifyingHost (0.1f);
    MemoryBlock state;
    synth.getStateInformation (state);
    DynamicObject::Ptr o = new DynamicObject();
    o->setProperty ("state", state.toBase64Encoding());
    const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("Kalibre Test Dark.kpreset");
    f.replaceWithText (JSON::toString (var (o.get())));
    const auto tracks = c.project.tracks.size();
    const auto steps = c.history.size();
    const Id nt = c.presetOnNewTrack (dev, f);
    CHECK (nt != 0);
    CHECK_EQ (c.project.tracks.size(), tracks + 1);
    CHECK_EQ (c.history.size(), steps + 1);
    if (auto* track = c.project.track (nt))
    {
        CHECK_EQ (track->devices.size(), size_t (1));
        if (! track->devices.empty())
        {
            CHECK (track->devices[0].plugin == synthRef());
            CHECK_EQ (track->devices[0].state, state.toBase64Encoding().toStdString());
        }
    }
    f.deleteFile();
}

TEST ("window: Space plays even when a button holds the keyboard focus, as one does after clicking in a plug-in's own view on the Mac; only a button reached with Tab takes it (the musician's report: Space switched to Devices)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        edit::addTrack (c.project, TrackKind::Track, "Drums");
        c.refresh();
        MainComponent w (c);
        w.setSize (1440, 900);
        w.addToDesktop (0);
        w.setVisible (true);
        c.zoneTab = ZoneTab::Editor;
        c.viewChanged();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        // The lower zone's Devices tab, given the focus as the window gives it back (not by Tab).
        Button* devices = nullptr;
        std::function<void (Component&)> find = [&] (Component& comp)
        {
            if (auto* b = dynamic_cast<Button*> (&comp); b && b->getButtonText() == "Devices") devices = b;
            for (auto* ch : comp.getChildren()) find (*ch);
        };
        find (w);
        CHECK (devices != nullptr);
        if (devices && w.getPeer())
        {
            devices->grabKeyboardFocus();
            CHECK (devices->hasKeyboardFocus (false));
            w.getPeer()->handleKeyPress (KeyPress (KeyPress::spaceKey));
            CHECK (c.engine.isPlaying());               // Space played
            CHECK (c.zoneTab == ZoneTab::Editor);       // and didn't press Devices
            c.stop();
            // Reached with Tab, the button is the keyboard's: Space presses it.
            static_cast<Component*> (devices)->focusGained (Component::focusChangedByTabKey);
            w.getPeer()->handleKeyPress (KeyPress (KeyPress::spaceKey));
            // The click is posted: a loaded Mac runner can take a while to deliver it.
            for (int i = 0; i < 40 && c.zoneTab != ZoneTab::Devices; ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (25);
            CHECK (c.zoneTab == ZoneTab::Devices);
            CHECK (! c.engine.isPlaying());
        }
        w.removeFromDesktop();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("window: View > Text Size offers 50%, 75%, 90%, 100%, 110%, 125%, 150% and 200%; each is used, laid out with nothing off the edge; Bigger and Smaller step through them (the musician's ask)")
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
        // The View menu's Text Size lists them in order.
        const auto names = w.getMenuBarNames();
        StringArray sizes;
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == "View")
            {
                const auto view = w.getMenuForIndex (i, names[i]);   // kept: the iterator holds on to it
                for (PopupMenu::MenuItemIterator it (view); it.next();)
                    if (it.getItem().text == "Text Size" && it.getItem().subMenu)
                        for (PopupMenu::MenuItemIterator s (*it.getItem().subMenu); s.next();) sizes.add (s.getItem().text);
            }
        CHECK_EQ (sizes.joinIntoString (" "), String ("50% 75% 90% 100% 110% 125% 150% 200%"));

        for (float scale : { 0.5f, 0.75f, 0.9f, 1.1f })
            for (auto size : { Rectangle<int> (900, 560), Rectangle<int> (1440, 900) })
            {
                w.setTextSize (scale);
                CHECK (std::abs (theme::scale() - scale) < 0.001f);
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
                            CHECK (b.getHeight() >= roundToInt (18 * scale));
                        }
                        if (dynamic_cast<Viewport*> (child) == nullptr) walk (*child);
                    }
                };
                walk (w);
            }
        // Bigger and Smaller (Command-= and Command--) go to the next size along.
        auto press = [&c] (const String& id) { for (auto& k : c.keys.all()) if (k.id == id && k.run) k.run(); };
        w.setTextSize (1.0f);
        press ("bigger");
        CHECK (std::abs (theme::scale() - 1.1f) < 0.001f);
        press ("smaller");
        press ("smaller");
        CHECK (std::abs (theme::scale() - 0.9f) < 0.001f);
        w.setTextSize (0.5f);
        press ("smaller");
        CHECK (std::abs (theme::scale() - 0.5f) < 0.001f);   // the smallest stays
        w.setTextSize (1.0f);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("window: Kalibre is version 1 (the musician's main DAW from 25 November); About Kalibre, first in the Kalibre menu, says which")
{
    CHECK (String (KALIBRE_VERSION).startsWith ("1."));
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    MainComponent w (c);
    const auto app = w.appMenu();
    PopupMenu::MenuItemIterator it (app);
    CHECK (it.next());
    CHECK_EQ (it.getItem().text, String ("About Kalibre"));
    CHECK (MainComponent::aboutText().contains ("Version " + String (KALIBRE_VERSION)));
}
