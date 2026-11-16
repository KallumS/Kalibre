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
