/*
    BattlePass - DAW Battle Pass, the whole of it, inside Kalibre (decision 0111).

    The musician's REAPER script (KallumS/DAW-Battle-Pass), unchanged in
    Engines/tools/daw-battle-pass/: the window with its eight pages, the
    celebrations, the rules, the loot, the effects, and its watcher,
    bp_watch.lua, which only ever talks to `reaper`. Here `reaper` is answered
    from Kalibre's song: its tracks, clips, notes, devices, markers and
    sections, the transport, the cursor, the mouse, whether Kalibre is in
    front; an idea won goes into the song at the cursor as one undo step,
    an arrangement as sections, a tempo as the song's.

    It runs from when Kalibre opens to when it quits, unseen, so music made
    with its window shut still counts, and its rewards wait. It's a secret:
    the only way to see it is to click the top bar's Making Music timer seven
    times in a row (TopBar::onSecret). Its window is one of its own.

    Progress is the script's own file, in Kalibre's folder (Application
    Support/Kalibre/Data/DAW Battle Pass/progress.lua). The first time, the
    progress REAPER's copy of the battle pass saved is brought over, if any.
*/

#pragma once

#include "Controller.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{
namespace tools { class ToolProcessor; }

class BattlePass : private juce::Timer
{
public:
    explicit BattlePass (Controller&);
    ~BattlePass() override;

    // The window: opened by the secret, closed with its close button, ⌘W or Esc
    // (Esc first skips a celebration, as in REAPER). The game goes on either way.
    void show();
    void hide();
    bool isShown() const;
    juce::DocumentWindow* window() { return shownIn.get(); }

    // Where REAPER's resource folder is, for the script: Kalibre's own folder (tests set it).
    static juce::File& resourceFolder();
    // REAPER's own, where a save from REAPER would be (tests set it).
    static juce::File& reaperFolder();

    // Tests: the error the script stopped with ("" while it runs), a line of Lua run
    // in its state, and one unseen frame now.
    juce::String error() const;
    juce::String run (const juce::String& lua);
    void frameNow();

private:
    Controller& c;
    std::unique_ptr<tools::ToolProcessor> tool;
    std::unique_ptr<juce::DocumentWindow> shownIn;
    void timerCallback() override;
    static void bringOverReapersSave();
};

// Seven left clicks in a row on the timer, each within a second and a half of the last.
struct SecretClicks
{
    static constexpr int needed = 7;
    static constexpr juce::uint32 within = 1500;
    int count = 0;
    juce::uint32 last = 0;
    bool click (juce::uint32 nowMs)
    {
        count = (count > 0 && nowMs - last <= within) ? count + 1 : 1;
        last = nowMs;
        if (count < needed) return false;
        count = 0;
        return true;
    }
};

} // namespace kal
