// What the full UAT of 23 November found (decision 0133), each first as a
// test that failed.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "PluginManager.h"
#include "Theme.h"

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
