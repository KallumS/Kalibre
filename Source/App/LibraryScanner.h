/*
    LibraryScanner - finds the samples and MIDI files in the chosen folders
    and learns what the browser shows about each (decision 0094).

    It runs on its own thread. Only files Kalibre can read are looked at
    (audio formats this Mac can decode, and MIDI), and a file already in the
    index is read again only if its size or date has changed, so after the
    first time a scan takes moments. What it learns is handed to the message
    thread in batches (`onChange`), and the index is saved when it's done.
*/

#pragma once

#include "Library.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>

namespace kal
{

// The tempo an audio file was made at: written in its name ("Drums 96bpm")
// or carried in it (a WAV's ACID chunk, unless it's a one-shot). 0 when unknown.
double audioFileTempo (const juce::String& fileName, const juce::StringPairArray& metadata);

class LibraryScanner : private juce::Thread, private juce::AsyncUpdater
{
public:
    explicit LibraryScanner (juce::File indexFile);
    ~LibraryScanner() override;

    // Message thread. The index as found so far.
    const library::Index& index() const { return live; }
    // Looks through these folders (and every folder inside), from the start.
    void scan (const std::vector<std::string>& folders);
    bool scanning() const { return isThreadRunning(); }
    int filesFound() const { return found.load(); }
    int filesRead() const { return read.load(); }
    int filesAnalysed() const { return analysed.load(); }   // read again this scan: new or changed
    std::function<void()> onChange;

    // "*.wav;*.flac;...;*.mid;*.midi": every file this Mac's Kalibre can read.
    static juce::String wildcard();
    // What the browser shows about one file; false if it can't be read.
    static bool analyse (const juce::File&, library::Item&, juce::AudioFormatManager&);

    // Tests: wait for the scan to finish, handing its results over.
    bool waitUntilDone (int milliseconds);

private:
    juce::File indexFile;
    library::Index live;                 // message thread
    juce::CriticalSection lock;
    library::Index shared;               // the thread's latest, handed over
    bool fresh = false;
    std::vector<std::string> folders;    // set before the thread starts
    std::atomic<int> found { 0 }, read { 0 }, analysed { 0 };

    void run() override;
    void handleAsyncUpdate() override;
};

} // namespace kal
