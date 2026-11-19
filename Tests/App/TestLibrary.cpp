// The browser's Sounds tab (decision 0094): scanning the chosen folders,
// hearing a sound in time with the song, and bringing it into the song.
#include "Check.h"
#include "BuiltinInstruments.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "LibraryFolders.h"
#include "LibraryView.h"
#include "PluginManager.h"

using namespace kal;
using namespace juce;

namespace
{
File libraryFolder (const String& name)
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-library-tests").getChildFile (name);
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

// `seconds` of a 220 Hz tone, loud on every beat at `bpm`, in `format`.
void writeAudio (const File& f, AudioFormat& format, double seconds, double bpm = 120, std::unordered_map<String, String> meta = {})
{
    const double rate = 44100;
    AudioBuffer<float> b (2, (int) (seconds * rate));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / rate;
        const double beat = std::fmod (t * bpm / 60.0, 1.0);
        const float v = (float) (0.6 * std::exp (-beat * 6.0) * std::sin (2 * MathConstants<double>::pi * 220 * t));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto opts = AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (16).withMetadataValues (meta);
    if (auto w = format.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Four quarter notes at `bpm`, C E G C.
void writeMidi (const File& f, double bpm = 100)
{
    MidiFile mf;
    mf.setTicksPerQuarterNote (480);
    MidiMessageSequence seq;
    auto tempo = MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm));
    tempo.setTimeStamp (0);
    seq.addEvent (tempo);
    const int pitches[] { 60, 64, 67, 72 };
    for (int i = 0; i < 4; ++i)
    {
        seq.addEvent (MidiMessage::noteOn (1, pitches[i], (uint8) 100).withTimeStamp (i * 480));
        seq.addEvent (MidiMessage::noteOff (1, pitches[i]).withTimeStamp (i * 480 + 400));
    }
    seq.updateMatchedPairs();
    mf.addTrack (seq);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    FileOutputStream out (f);
    mf.writeTo (out);
}

// A folder of sounds: a named loop, a FLAC, an Ogg, a MIDI file, a loop with
// its tempo in an ACID chunk, and things that aren't sounds.
File makeSounds (const String& name)
{
    const auto dir = libraryFolder (name);
    WavAudioFormat wav;
    FlacAudioFormat flac;
    OggVorbisAudioFormat ogg;
    writeAudio (dir.getChildFile ("Drums/Beat 120bpm.wav"), wav, 2.0);
    writeAudio (dir.getChildFile ("Drums/Groove.wav"), wav, 2.0, 90, { { WavAudioFormat::acidTempo, "90" }, { WavAudioFormat::acidBeats, "3" } });
    writeAudio (dir.getChildFile ("Keys/Pad.flac"), flac, 1.0);
    writeAudio (dir.getChildFile ("Keys/Hit.ogg"), ogg, 0.5);
    writeMidi (dir.getChildFile ("Keys/Chords.mid"));
    dir.getChildFile ("Keys/Read Me.txt").replaceWithText ("not a sound");
    writeAudio (dir.getChildFile (".Hidden.wav"), wav, 0.5);
    return dir;
}

const library::Item* find (const library::Index& ix, const File& f)
{
    const auto it = ix.items.find (f.getFullPathName().toStdString());
    return it != ix.items.end() ? &it->second : nullptr;
}

float loudest (const AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    float m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) m = jmax (m, std::abs (b.getSample (c, i)));
    return m;
}

// The sounds' view with its settings and index in a folder of their own.
struct Library
{
    explicit Library (const File& sounds) : engine (plugins), c (engine), view (c)
    {
        store = libraryFolder ("store-" + sounds.getFileName());
        library::Settings s;
        s.folders = { sounds.getFullPathName().toStdString() };
        s.asked = true;
        s.open = { s.folders[0] };
        store.getChildFile ("Library Settings.json").replaceWithText (library::toJson (s));
        c.project = Project();
        view.useStore (store);
        view.setSize (320, 700);
        CHECK (view.scanner().waitUntilDone (20000));
    }
    PluginManager plugins;
    Engine engine;
    Controller c;
    LibraryView view;
    File store;
};
} // namespace

TEST ("sounds: the chosen folders are scanned for what Kalibre can read, each once, with its length, tempo, waveform or notes")
{
    const auto dir = makeSounds ("scan");
    const auto indexFile = libraryFolder ("scan-store").getChildFile ("Library Index.json");
    LibraryScanner scanner (indexFile);
    scanner.scan ({ dir.getFullPathName().toStdString() });
    CHECK (scanner.waitUntilDone (20000));
    const auto& ix = scanner.index();
    CHECK_EQ ((int) ix.items.size(), 5);   // not the text file, not the hidden one
    const auto* beat = find (ix, dir.getChildFile ("Drums/Beat 120bpm.wav"));
    const auto* groove = find (ix, dir.getChildFile ("Drums/Groove.wav"));
    const auto* pad = find (ix, dir.getChildFile ("Keys/Pad.flac"));
    const auto* hit = find (ix, dir.getChildFile ("Keys/Hit.ogg"));
    const auto* chords = find (ix, dir.getChildFile ("Keys/Chords.mid"));
    CHECK (beat && groove && pad && hit && chords);
    if (! (beat && groove && pad && hit && chords)) return;
    CHECK (beat->readable && pad->readable && hit->readable && chords->readable);
    CHECK (std::abs (beat->seconds - 2.0) < 0.01);
    CHECK_EQ (beat->bpm, 120.0);             // from its name
    CHECK_EQ (groove->bpm, 90.0);            // from its ACID chunk
    CHECK_EQ (pad->bpm, 0.0);                // not known
    CHECK (std::abs (pad->seconds - 1.0) < 0.01);
    CHECK (std::abs (hit->seconds - 0.5) < 0.05);
    CHECK_EQ ((int) beat->peaks.size(), library::peakColumns);
    CHECK (*std::max_element (beat->peaks.begin(), beat->peaks.end()) > 100);
    CHECK (chords->kind == library::Kind::Midi);
    CHECK_EQ ((int) chords->notes.size(), 4);
    CHECK (std::abs (chords->bpm - 100.0) < 0.01);
    CHECK (std::abs (chords->beats - (3 + 400.0 / 480.0)) < 0.01);
    CHECK_EQ (scanner.filesAnalysed(), 5);

    // Again: nothing new or changed, nothing read. A changed file is read again.
    scanner.scan ({ dir.getFullPathName().toStdString() });
    CHECK (scanner.waitUntilDone (20000));
    CHECK_EQ (scanner.filesAnalysed(), 0);
    CHECK_EQ ((int) scanner.index().items.size(), 5);
    WavAudioFormat wav;
    writeAudio (dir.getChildFile ("Drums/Beat 120bpm.wav"), wav, 3.0);
    dir.getChildFile ("Keys/Pad.flac").deleteFile();
    scanner.scan ({ dir.getFullPathName().toStdString() });
    CHECK (scanner.waitUntilDone (20000));
    CHECK_EQ (scanner.filesAnalysed(), 1);
    CHECK_EQ ((int) scanner.index().items.size(), 4);   // the deleted one forgotten
    CHECK (std::abs (find (scanner.index(), dir.getChildFile ("Drums/Beat 120bpm.wav"))->seconds - 3.0) < 0.01);

    // Kept between runs: a new scanner starts from what the last one saved.
    LibraryScanner later (indexFile);
    CHECK (later.index().items == scanner.index().items);
}

TEST ("sounds: a preview follows the song's tempo with Sync on, a sample when its tempo is known, MIDI always; off, as made")
{
    const auto dir = makeSounds ("preview");
    LibraryScanner scanner (libraryFolder ("preview-store").getChildFile ("Library Index.json"));
    scanner.scan ({ dir.getFullPathName().toStdString() });
    CHECK (scanner.waitUntilDone (20000));
    const auto* beat = find (scanner.index(), dir.getChildFile ("Drums/Beat 120bpm.wav"));
    const auto* pad = find (scanner.index(), dir.getChildFile ("Keys/Pad.flac"));
    const auto* chords = find (scanner.index(), dir.getChildFile ("Keys/Chords.mid"));
    CHECK (beat && pad && chords);
    if (! (beat && pad && chords)) return;
    const double rate = 48000;
    auto seconds = [rate] (const std::unique_ptr<AudioBuffer<float>>& b) { return b ? b->getNumSamples() / rate : -1.0; };

    // A 120 BPM loop in a 60 BPM song: twice as long, synced; as it was, not.
    const auto synced = PreviewPlayer::render (*beat, true, 60, rate);
    const auto asMade = PreviewPlayer::render (*beat, false, 60, rate);
    CHECK (std::abs (seconds (synced) - 4.0) < 0.05);
    CHECK (std::abs (seconds (asMade) - 2.0) < 0.01);
    CHECK (synced && loudest (*synced) > 0.3f);
    // Its tempo unknown: never stretched.
    CHECK (std::abs (seconds (PreviewPlayer::render (*pad, true, 60, rate)) - 1.0) < 0.01);
    // At the song's tempo, the stretch leaves it as it was.
    CHECK (std::abs (seconds (PreviewPlayer::render (*beat, true, 120, rate)) - 2.0) < 0.01);

    // MIDI through Kalibre Synth: 3.83 beats, then a second to ring out.
    const auto at120 = PreviewPlayer::render (*chords, true, 120, rate);
    const auto own = PreviewPlayer::render (*chords, false, 120, rate);   // its own 100 BPM
    CHECK (std::abs (seconds (at120) - (3.8333 * 0.5 + 1.0)) < 0.02);
    CHECK (std::abs (seconds (own) - (3.8333 * 0.6 + 1.0)) < 0.02);
    CHECK (at120 && loudest (*at120) > 0.05f);
    // The second note (at beat 1, 0.5 s) is sounding at 0.6 s.
    CHECK (at120 && loudest (*at120, (int) (0.55 * rate), (int) (0.65 * rate)) > 0.02f);
}

TEST ("sounds: the player plays the preview through the audio device, starting on the next beat when synced")
{
    const auto dir = makeSounds ("player");
    Library lib (dir);
    auto& player = lib.view.player();
    player.audioDeviceAboutToStart (nullptr);
    const auto* beat = lib.view.item (dir.getChildFile ("Drums/Beat 120bpm.wav").getFullPathName().toStdString());
    CHECK (beat != nullptr);
    if (! beat) return;
    player.nextBeat = [] { return 0.1; };   // the song's next beat: 0.1 s away
    player.play (*beat, true, 120);
    for (int i = 0; i < 200 && player.progress() < 0; ++i) MessageManager::getInstance()->runDispatchLoopUntil (10);
    CHECK (player.progress() >= 0);
    CHECK_EQ (player.playingPath(), dir.getChildFile ("Drums/Beat 120bpm.wav").getFullPathName());
    AudioBuffer<float> out (2, 2400);   // 0.05 s at 48 kHz
    auto block = [&]
    {
        out.clear();
        player.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, out.getNumSamples(), {});
        return loudest (out);
    };
    CHECK_EQ (block(), 0.0f);    // waiting for the beat
    CHECK_EQ (block(), 0.0f);
    CHECK (block() > 0.1f);      // there
    player.stop();
    block();
    CHECK_EQ (block(), 0.0f);
    CHECK (! player.isPlaying());
}

TEST ("sounds: Up and Down hear the next sound; folders open and shut; a star marks a favourite; one list or folders; shuffled")
{
    const auto dir = makeSounds ("keys");
    Library lib (dir);
    auto& view = lib.view;
    view.settings().open = { dir.getFullPathName().toStdString(), dir.getChildFile ("Drums").getFullPathName().toStdString(),
                             dir.getChildFile ("Keys").getFullPathName().toStdString() };
    view.refreshRows();
    // [sounds] [Drums] Beat Groove [Keys] Chords Hit Pad
    CHECK_EQ ((int) view.shown().size(), 8);
    const int first = 2;
    CHECK (view.shown()[(size_t) first].type == library::Row::Type::Item);
    view.hear (first);
    CHECK_EQ (view.list().getSelectedRow(), first);
    CHECK_EQ (view.player().playingPath(), dir.getChildFile ("Drums/Beat 120bpm.wav").getFullPathName());
    view.list().keyPressed (KeyPress (KeyPress::downKey));
    CHECK_EQ (view.list().getSelectedRow(), first + 1);
    CHECK_EQ (view.player().playingPath(), dir.getChildFile ("Drums/Groove.wav").getFullPathName());
    view.list().keyPressed (KeyPress (KeyPress::downKey));   // onto the Keys folder: nothing new heard
    CHECK_EQ (view.player().playingPath(), dir.getChildFile ("Drums/Groove.wav").getFullPathName());
    view.list().keyPressed (KeyPress (KeyPress::leftKey));   // shuts it
    CHECK_EQ ((int) view.shown().size(), 5);
    view.list().keyPressed (KeyPress (KeyPress::rightKey));  // opens it again
    CHECK_EQ ((int) view.shown().size(), 8);
    view.list().keyPressed (KeyPress (KeyPress::downKey));
    CHECK_EQ (view.player().playingPath(), dir.getChildFile ("Keys/Chords.mid").getFullPathName());
    view.list().keyPressed (KeyPress (KeyPress::upKey));
    view.list().keyPressed (KeyPress (KeyPress::upKey));
    CHECK_EQ (view.player().playingPath(), dir.getChildFile ("Drums/Groove.wav").getFullPathName());

    // A favourite, kept in the settings saved; Favourites Only shows just it.
    const auto groove = dir.getChildFile ("Drums/Groove.wav").getFullPathName().toStdString();
    view.toggleFavourite (groove);
    CHECK (library::isFavourite (view.settings(), groove));
    library::Settings saved;
    CHECK (library::fromJson (lib.store.getChildFile ("Library Settings.json").loadFileAsString().toStdString(), saved));
    CHECK (saved.favourites.count (groove) == 1);
    view.settings().favouritesOnly = true;
    view.refreshRows();
    CHECK_EQ ((int) library::itemsInOrder (view.shown()).size(), 1);
    view.settings().favouritesOnly = false;

    // Every item in one list; then shuffled, the same items.
    view.settings().flat = true;
    view.refreshRows();
    CHECK_EQ ((int) view.shown().size(), 5);
    const auto az = library::itemsInOrder (view.shown());
    view.settings().shuffled = true;
    view.settings().seed = 12345;
    view.refreshRows();
    auto shuffled = library::itemsInOrder (view.shown());
    std::sort (shuffled.begin(), shuffled.end());
    auto sortedAz = az;
    std::sort (sortedAz.begin(), sortedAz.end());
    CHECK (shuffled == sortedAz);

    // A search keeps what matches.
    view.setFilter ("groove");
    CHECK_EQ ((int) view.shown().size(), 1);
    view.setFilter ({});
}

TEST ("sounds: right-click makes a new track with Kalibre Sampler or Synth, as one undo step; a sound drags as a file")
{
    const auto dir = makeSounds ("tracks");
    Library lib (dir);
    auto& c = lib.c;
    auto& view = lib.view;
    const auto beat = dir.getChildFile ("Drums/Beat 120bpm.wav").getFullPathName().toStdString();
    const auto chords = dir.getChildFile ("Keys/Chords.mid").getFullPathName().toStdString();

    // A sample: the sampler, the sample loaded in it, and no clip.
    const size_t steps = c.history.size();
    CHECK (view.newTrackFrom (beat, LibraryView::NewTrack::Sampler));
    CHECK_EQ (c.history.size(), steps + 1);
    const auto* t = c.project.track (c.focusTrack);
    CHECK (t != nullptr);
    if (! t) return;
    CHECK_EQ (t->name, std::string ("Beat 120bpm"));
    CHECK_EQ (t->devices.size(), (size_t) 1);
    CHECK_EQ (t->devices[0].plugin.identifier, samplerRef().identifier);
    CHECK (t->clips().empty());
    c.refresh();
    auto* sampler = dynamic_cast<KalibreSampler*> (lib.engine.instance (t->devices[0].id));
    CHECK (sampler != nullptr);
    if (sampler) CHECK_EQ (sampler->file().getFullPathName().toStdString(), beat);
    const Id made = t->id;
    c.undo();
    CHECK (c.project.track (made) == nullptr);

    // A MIDI file: Kalibre Synth, and the notes as a clip at the cursor, one undo step.
    c.cursor = PPQ * 4;
    const size_t before = c.history.size();
    CHECK (view.newTrackFrom (chords, LibraryView::NewTrack::Synth));
    CHECK_EQ (c.history.size(), before + 1);
    const auto* m = c.project.track (c.focusTrack);
    CHECK (m != nullptr);
    if (! m) return;
    CHECK_EQ (m->devices.size(), (size_t) 1);
    CHECK_EQ (m->devices[0].plugin.identifier, synthRef().identifier);
    CHECK_EQ (m->clips().size(), (size_t) 1);
    if (! m->clips().empty())
    {
        CHECK_EQ (m->clips()[0].start, PPQ * 4);
        CHECK_EQ (m->clips()[0].midi.notes.size(), (size_t) 4);
    }

    // A sample as an audio clip on a new track.
    CHECK (view.newTrackFrom (beat, LibraryView::NewTrack::Clip));
    const auto* a = c.project.track (c.focusTrack);
    CHECK (a && a->devices.empty() && a->clips().size() == 1 && ! a->clips()[0].isMidi());

    // Dragged, a sound is a file, as the arrangement takes it.
    view.settings().flat = true;
    view.refreshRows();
    juce::SparseSet<int> rows;
    rows.addRange ({ 0, 1 });
    auto* model = view.list().getListBoxModel();
    CHECK (model->getDragSourceDescription (rows).toString().startsWith ("file:"));
}

TEST ("sounds: Library Folders takes a folder out of the search; its sounds leave the list")
{
    const auto dir = makeSounds ("folders");
    Library lib (dir);
    CHECK (! lib.view.scanner().index().items.empty());
    LibraryFoldersView folders (lib.view);
    folders.setSize (600, 360);
    folders.remove (0);
    CHECK (lib.view.settings().folders.empty());
    CHECK (lib.view.scanner().waitUntilDone (20000));
    CHECK (lib.view.scanner().index().items.empty());
    CHECK (lib.view.shown().empty());
    library::Settings saved;
    CHECK (library::fromJson (lib.store.getChildFile ("Library Settings.json").loadFileAsString().toStdString(), saved));
    CHECK (saved.folders.empty());
}
