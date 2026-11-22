#include "SettingsWindows.h"

namespace kal
{
using namespace juce;

SettingsWindow::SettingsWindow (const String& title, Component* content, int w, int h)
    : DocumentWindow (title, theme::ground, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (content, false);
    setResizable (true, false);
    setResizeLimits (360, 300, 10000, 10000);
    setSize (w, h);
}

bool SettingsWindow::keyPressed (const KeyPress& k)
{
    if (k == KeyPress ('w', ModifierKeys::commandModifier, 0) || k.getKeyCode() == KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return DocumentWindow::keyPressed (k);
}

//==============================================================================
// The studio's settings are taller than most screens: they scroll.
struct StudioWindow::View : public Component
{
    explicit View (Controller& c) : studio (c)
    {
        setTitle ("Studio");
        viewport.setViewedComponent (&studio, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }
    void paint (Graphics& g) override { g.fillAll (theme::ground); }
    void resized() override
    {
        viewport.setBounds (getLocalBounds().reduced (theme::px (8)));
        studio.setSize (viewport.getWidth() - viewport.getScrollBarThickness(),
                        jmax (viewport.getHeight(), studio.preferredHeight()));
    }

    StudioPanel studio;
    Viewport viewport;
};

StudioWindow::StudioWindow (Controller& c)
    : SettingsWindow ("Studio", new View (c), 560, 760)
{
    view = dynamic_cast<View*> (getContentComponent());
    studio = &view->studio;
}

void StudioWindow::showAgents()
{
    view->resized();
    view->viewport.setViewPosition (0, jmax (0, studio->agentsTop()));
}

//==============================================================================
namespace
{
struct KeysView : public Component
{
    explicit KeysView (Keys& k) : keys (k)
    {
        setTitle ("Keys");
        addAndMakeVisible (keys);
    }
    void paint (Graphics& g) override { g.fillAll (theme::ground); }
    void resized() override { keys.setBounds (getLocalBounds().reduced (theme::px (8))); }
    KeysPanel keys;
};
} // namespace

KeysWindow::KeysWindow (Keys& k)
    : SettingsWindow ("Keys", new KeysView (k), 520, 680)
{
    keys = &dynamic_cast<KeysView*> (getContentComponent())->keys;
}

} // namespace kal
