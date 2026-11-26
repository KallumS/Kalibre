/*
    PluginPictures - a picture of each plug-in's window, shown in the browser
    under its name (decision 0138).

    Kept in Kalibre's library, Application Support/Kalibre/Plug-in Pictures,
    one PNG for each plug-in by its maker and name ("Xfer Records - Serum.png"),
    so its VST3, AU and CLAP share one picture. Taken three ways:

    - The Plug-in Manager's Take Pictures: every plug-in without one, in turn,
      opened in a window of its own, photographed once it has drawn, and
      closed (`Taker`).
    - The camera button on a plug-in's panel in the rack: the plug-in shown
      at its own size for a moment, photographed, and put back.
    - Tests, straight through `keep`.

    The picture is what's on the screen (`grab`): a plug-in's window is a
    native view Kalibre doesn't draw, so it's read back from the screen
    through each system's own call (PluginPicturesMac.mm, PluginPicturesWin.cpp,
    PluginPicturesLinux.cpp); where that gives nothing, Kalibre draws the
    window itself (its own devices, the tests).
*/

#pragma once

#include "PluginManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace kal::pictures
{

// Where they're kept. Tests give another folder.
juce::File folder();
void setFolder (const juce::File&);

// A plug-in's picture's file, whether there is one or not.
juce::File fileFor (const juce::String& name, const juce::String& maker);
juce::File fileFor (const juce::PluginDescription&);

bool has (const juce::PluginDescription&);
// The picture, read once and kept; a null Image when there's none.
juce::Image of (const juce::PluginDescription&);
// Writes it (no wider than `widest` pixels), and the browser shows it at once.
bool keep (const juce::Image&, const juce::PluginDescription&, int widest = 1600);
// Told when a picture is kept or forgotten.
juce::ChangeBroadcaster& changes();

// What's on the screen where `c` is: a plug-in's window and every native view
// in it. Falls back to Kalibre drawing `c` itself when the screen can't be read.
juce::Image grab (juce::Component& c);
// The system's own reading of the screen: a null Image when it can't.
juce::Image grabNative (juce::Component& c);
// One colour all over: the screen gave nothing worth keeping.
bool looksBlank (const juce::Image&);

// A plug-in's window in a window of its own, for its picture.
class PictureWindow;

// Every plug-in in a list photographed in turn: made, its window opened, the
// picture taken once it has drawn, the window closed and the plug-in let go
// of, then the next. One that closes Kalibre is noted (`Skipped.txt`) and
// passed over the next time.
class Taker : private juce::Timer
{
public:
    Taker (PluginManager&, double sampleRate);
    ~Taker() override;

    struct Summary { int taken = 0, noWindow = 0, failed = 0, skipped = 0; juce::StringArray problems; bool stopped = false; };
    using Progress = std::function<void (int done, int total, const juce::String& name)>;
    void start (std::vector<juce::PluginDescription>, Progress, std::function<void (const Summary&)> finished);
    void stop();
    bool busy() const { return ! queue.empty() || current != nullptr; }

    // How long a plug-in is given to draw before its picture is taken.
    static inline int settleMs = 1500;
    // The plug-ins that closed Kalibre while being photographed: left out next time.
    static juce::StringArray skipped();

private:
    PluginManager& plugins;
    double rate;
    std::vector<juce::PluginDescription> queue;
    int total = 0, done = 0;
    Summary summary;
    Progress onProgress;
    std::function<void (const Summary&)> onFinished;
    struct Current;
    std::unique_ptr<Current> current;
    enum class Stage { Next, Shoot, Close } stage = Stage::Next;

    void timerCallback() override;
    void next();
    void shoot();
    void finish (bool stopped);
};

} // namespace kal::pictures
