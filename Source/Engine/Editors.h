/*
    Editors - opening and closing plug-ins' windows (decision 0056).

    JUCE 9 leaves it to the host to tell a plug-in its window is going
    (editorBeingDeleted) before deleting it. Kalibre didn't, so the plug-in
    went on thinking its old window was open: asked for a new one it gave
    nothing, and a bare generic panel stood in (the "blank" plug-ins when
    another device was added), with a pointer left to a deleted window.
    Every window of a plug-in is opened and closed through here.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace kal::editors
{

// Closes a plug-in's window: the plug-in is told first.
inline void close (juce::AudioProcessorEditor* e)
{
    if (e == nullptr) return;
    e->processor.editorBeingDeleted (e);
    delete e;
}

struct Closer { void operator() (juce::AudioProcessorEditor* e) const { close (e); } };
using Owned = std::unique_ptr<juce::AudioProcessorEditor, Closer>;

// A plug-in's own window, or a generic one if it has none. A window it
// still has open somewhere is closed first: a plug-in has one at a time.
inline juce::AudioProcessorEditor* open (juce::AudioProcessor& p)
{
    if (auto* old = p.getActiveEditor()) close (old);
    juce::AudioProcessorEditor* e = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
    return e != nullptr ? e : new juce::GenericAudioProcessorEditor (p);
}

} // namespace kal::editors

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace kal::editors
{
// A window that closes is emptied and hidden at once, and deleted some seconds
// later (decision 0115): macOS goes on sending a two-finger scroll's glide to the
// window it began over, and Kalibre crashed there when that window had been
// freed (-[NSWindow _latchViewForScrollEvent:]). Empty first: whatever it showed
// (a plug-in's window) is closed by its owner as usual; only the window waits.
class Retired : private juce::Timer, private juce::DeletedAtShutdown
{
public:
    static inline int delayMs = 10000;   // tests shorten it
    static void add (std::unique_ptr<juce::Component> window, int afterMs = -1)
    {
        if (window == nullptr) return;
        if (afterMs < 0) afterMs = delayMs;
        window->setVisible (false);
        auto& r = get();
        r.list.push_back ({ std::move (window), juce::Time::getMillisecondCounter() + (juce::uint32) afterMs });
        r.startTimer (250);
    }
    static size_t waiting() { return instance != nullptr ? instance->list.size() : 0; }   // tests

private:
    struct Entry { std::unique_ptr<juce::Component> window; juce::uint32 at; };
    std::vector<Entry> list;
    static inline Retired* instance = nullptr;
    static Retired& get() { if (instance == nullptr) instance = new Retired(); return *instance; }
    ~Retired() override { stopTimer(); list.clear(); instance = nullptr; }
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        list.erase (std::remove_if (list.begin(), list.end(), [now] (const Entry& e) { return (juce::int32) (now - e.at) >= 0; }), list.end());
        if (list.empty()) stopTimer();
    }
};

inline void retire (std::unique_ptr<juce::Component> window, int afterMs = -1) { Retired::add (std::move (window), afterMs); }
} // namespace kal::editors
