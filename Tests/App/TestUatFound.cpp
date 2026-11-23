// What the full UAT of 23 November found (decision 0133), each first as a
// test that failed.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "PluginManager.h"
#include "Theme.h"
#include "BuiltinInstruments.h"

using namespace kal;
using namespace juce;

TEST ("questions: a question with a field to type in has the field ready, its text chosen, so typing replaces it and no key reaches the shortcuts (UAT)")
{
    // The UAT: Rename the Section opened, "Verse" was typed, and its letters
    // went to the window: S soloed a track, R started recording.
    Component host;
    host.setSize (800, 600);
    host.addToDesktop (0);
    host.setVisible (true);
    ui::setHost (&host);

    auto* w = new AlertWindow ("Rename the Section", {}, MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Section 1");
    w->addButton ("Rename", 1);
    w->addButton ("Cancel", 0);
    ui::present (w, ModalCallbackFunction::create ([] (int) {}), true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto* field = w->getTextEditor ("name");
    CHECK (field != nullptr);
    if (field != nullptr)
    {
        CHECK (field->hasKeyboardFocus (false));
        CHECK_EQ (field->getHighlightedText(), String ("Section 1"));
    }
    w->exitModalState (0);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    ui::setHost (nullptr);
    host.removeFromDesktop();
}

TEST ("last touched: switching to B and back, loading a preset or a state, isn't touching: the control last moved by hand stays the one automated (UAT)")
{
    // The UAT: Cutoff turned by hand, then Set B, Set A; Automate the Last
    // Touched Control made a lane for Volume, the last parameter A's state set.
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Bass");
    const Id dev = edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    c.refresh();
    auto* inst = engine.instance (dev);
    CHECK (inst != nullptr);
    if (! inst) return;
    int cutoff = -1;
    for (int i = 0; i < inst->getParameters().size(); ++i)
        if (inst->getParameters()[i]->getName (32) == "Cutoff") cutoff = i;
    CHECK (cutoff >= 0);
    if (cutoff < 0) return;
    auto* p = inst->getParameters()[cutoff];
    p->beginChangeGesture();
    p->setValueNotifyingHost (0.3f);
    p->endChangeGesture();
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
    c.switchAB (dev);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    c.switchAB (dev);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
    c.undo();
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK_EQ (c.lastTouched.param, cutoff);
}
