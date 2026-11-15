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
