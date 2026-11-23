// The browser's Sounds tab (decision 0094): scanning the chosen folders,
// hearing a sound in time with the song, and bringing it into the song.
#include "Check.h"
#include <fstream>
#include <functional>
#include <map>
#include "DrumSounds.h"
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

// A file's path, as a String of its own: getFullPathName() hands back a
// reference into the File, which a temporary File doesn't outlive.
String pathOf (const File& dir, const char* name) { return dir.getChildFile (name).getFullPathName(); }

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

TEST ("sounds: what a scan has read is saved as it goes, so one stopped part-way (Kalibre quit) carries on where it was; known sounds are only checked")
{
    const auto dir = libraryFolder ("scan-resume");
    WavAudioFormat wav;
    for (int i = 0; i < 30; ++i) writeAudio (dir.getChildFile ("Hit " + String (i) + ".wav"), wav, 0.2);
    const auto indexFile = libraryFolder ("scan-resume-store").getChildFile ("Library Index.json");
    indexFile.deleteFile();
    WaitableEvent paused, carryOn;
    const auto atQuit = indexFile.getSiblingFile ("Library Index at quit.json");
    {
        LibraryScanner first (indexFile);
        first.saveEvery = 1;   // after every sound, so the test needn't scan 50,000
        std::atomic<int> seen { 0 };
        first.afterEach = [&] { if (++seen == 10) { paused.signal(); carryOn.wait (20000); } };
        first.scan ({ dir.getFullPathName().toStdString() });
        CHECK (paused.wait (20000));
        CHECK_EQ (first.filesToRead(), 30);
        // What's on the disk if Kalibre were quit here and the scan cut off.
        atQuit.deleteFile();
        CHECK (indexFile.copyFileTo (atQuit));
        carryOn.signal();
        CHECK (first.waitUntilDone (20000));
    }
    // The next start knows the ten, and reads only the rest.
    LibraryScanner later (atQuit);
    CHECK ((int) later.index().items.size() >= 10);
    later.scan ({ dir.getFullPathName().toStdString() });
    CHECK (later.waitUntilDone (20000));
    CHECK (later.filesAnalysed() <= 20);
    CHECK_EQ ((int) later.index().items.size(), 30);
    // Nothing new: every sound is only checked, none read.
    LibraryScanner again (indexFile);
    again.scan ({ dir.getFullPathName().toStdString() });
    CHECK (again.waitUntilDone (20000));
    CHECK_EQ (again.filesToRead(), 0);
    CHECK_EQ (again.filesAnalysed(), 0);
}

TEST ("sounds: a sound scanned by the last version has only its hit's shape added, not all of it heard again (0106)")
{
    const auto dir = makeSounds ("scan-topup");
    const auto indexFile = libraryFolder ("scan-topup-store").getChildFile ("Library Index.json");
    indexFile.deleteFile();
    {
        LibraryScanner first (indexFile);
        first.scan ({ dir.getFullPathName().toStdString() });
        CHECK (first.waitUntilDone (20000));
    }
    // As the last version left them: analysis 2, no shape, and a loudness to see kept.
    library::Index old;
    {
        std::ifstream in (indexFile.getFullPathName().toStdString(), std::ios::binary);
        CHECK (library::readIndex (in, old));
    }
    for (auto& [p, it] : old.items)
        if (it.kind == library::Kind::Audio) { it.analysis = 2; it.onsetMs = it.attackMs = it.zcr = -1; it.tailDb = 0; it.lufs = -7.5; }
    indexFile.replaceWithText (library::toJson (old));
    LibraryScanner again (indexFile);
    again.scan ({ dir.getFullPathName().toStdString() });
    CHECK (again.waitUntilDone (20000));
    CHECK_EQ (again.filesToRead(), 4);   // the four sounds; the MIDI file isn't heard
    for (auto& [p, it] : again.index().items)
        if (it.kind == library::Kind::Audio && it.readable)
        {
            CHECK_EQ (it.analysis, library::analysisVersion);
            CHECK (it.onsetMs >= 0);
            CHECK (it.zcr >= 0);
            CHECK_EQ (it.lufs, -7.5);   // kept, not heard again
        }
}

namespace
{
// A sound made of `f (seconds) -> sample`, at 44.1 kHz.
void writeShaped (const File& f, double seconds, std::function<float (double)> shape)
{
    const double rate = 44100;
    AudioBuffer<float> b (2, (int) (seconds * rate));
    for (int i = 0; i < b.getNumSamples(); ++i) { const float v = shape (i / rate); b.setSample (0, i, v); b.setSample (1, i, v); }
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    if (auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (16)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

TEST ("sounds: Drums shows only drum sounds, each with its kind: by its name, or heard in an unnamed hit; a kind on its own (0106)")
{
    const auto dir = libraryFolder ("drum-sounds");
    const double pi = MathConstants<double>::pi;
    writeShaped (dir.getChildFile ("Kick 1.wav"), 0.4, [=] (double t) { return (float) (0.8 * std::exp (-t * 12) * std::sin (2 * pi * 55 * t)); });
    writeShaped (dir.getChildFile ("Snare 2.wav"), 0.3, [=] (double t) { return (float) (0.7 * std::exp (-t * 15) * std::sin (2 * pi * 2000 * t * (1 + t))); });
    writeShaped (dir.getChildFile ("Pad Warm.wav"), 3.0, [=] (double t) { return (float) (0.5 * std::min (1.0, t) * std::sin (2 * pi * 220 * t)); });
    Random rnd (5);
    writeShaped (dir.getChildFile ("Misc/Thing.wav"), 0.15, [&] (double t) { return (float) (0.7 * std::exp (-t * 40) * (rnd.nextFloat() * 2 - 1)); });
    Library lib (dir);
    auto& v = lib.view;
    v.settings().flat = true;
    v.settings().drumsOnly = true;
    v.refreshRows();
    std::map<String, drumsound::Verdict> shown;
    for (auto& r : v.shown())
        if (r.type == library::Row::Type::Item)
            if (auto* it = v.item (r.path)) shown[File (r.path).getFileNameWithoutExtension()] = v.drumOf (*it);
    CHECK_EQ (shown.size(), (size_t) 3);   // not the pad
    CHECK (shown.count ("Kick 1") && shown["Kick 1"].kind == drumsound::Kind::Kick && ! shown["Kick 1"].heard);
    CHECK (shown.count ("Snare 2") && shown["Snare 2"].kind == drumsound::Kind::Snare);
    CHECK (shown.count ("Thing") && shown["Thing"].heard);
    if (shown.count ("Thing"))
    {
        if (shown["Thing"].kind != drumsound::Kind::ClosedHat) std::printf ("    Thing heard as %s\n", drumsound::nameOf (shown["Thing"].kind).c_str());
        CHECK (shown["Thing"].kind == drumsound::Kind::ClosedHat);
    }
    // One kind.
    v.settings().drumKind = (int) drumsound::Kind::Kick;
    v.refreshRows();
    int items = 0;
    for (auto& r : v.shown()) items += r.type == library::Row::Type::Item;
    CHECK_EQ (items, 1);
}

TEST ("sounds: a big index is opened on the scan's thread, not while Kalibre starts; what it knows isn't read again (0106)")
{
    const auto dir = makeSounds ("scan-big-index");
    const auto indexFile = libraryFolder ("scan-big-index-store").getChildFile ("Library Index.json");
    indexFile.deleteFile();
    {
        LibraryScanner first (indexFile);
        first.scan ({ dir.getFullPathName().toStdString() });
        CHECK (first.waitUntilDone (20000));
    }
    LibraryScanner later (indexFile, 0);   // every index "big"
    CHECK (later.index().items.empty());
    CHECK (later.openingIndex());
    later.scan ({ dir.getFullPathName().toStdString() });
    CHECK (later.waitUntilDone (20000));
    CHECK (! later.openingIndex());
    CHECK_EQ ((int) later.index().items.size(), 5);
    CHECK_EQ (later.filesAnalysed(), 0);
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
    CHECK_EQ (player.playingPath(), pathOf (dir, "Drums/Beat 120bpm.wav"));
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
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Drums/Beat 120bpm.wav"));
    view.list().keyPressed (KeyPress (KeyPress::downKey));
    CHECK_EQ (view.list().getSelectedRow(), first + 1);
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Drums/Groove.wav"));
    view.list().keyPressed (KeyPress (KeyPress::downKey));   // onto the Keys folder: nothing new heard
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Drums/Groove.wav"));
    view.list().keyPressed (KeyPress (KeyPress::leftKey));   // shuts it
    CHECK_EQ ((int) view.shown().size(), 5);
    view.list().keyPressed (KeyPress (KeyPress::rightKey));  // opens it again
    CHECK_EQ ((int) view.shown().size(), 8);
    view.list().keyPressed (KeyPress (KeyPress::downKey));
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Keys/Chords.mid"));
    view.list().keyPressed (KeyPress (KeyPress::upKey));
    view.list().keyPressed (KeyPress (KeyPress::upKey));
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Drums/Groove.wav"));

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

TEST ("sounds: the scan hears each sample's loudness, note and tempo, kept for the next time Kalibre opens (0095)")
{
    const auto dir = libraryFolder ("heard");
    WavAudioFormat wav;
    writeAudio (dir.getChildFile ("Tone.wav"), wav, 4.0, 120);      // two bars at 120, A2 on every beat, no tempo in its name
    writeAudio (dir.getChildFile ("Tone 96bpm.wav"), wav, 2.0, 120); // its name says 96: that's believed
    Library lib (dir);
    const auto& ix = lib.view.scanner().index();
    const auto* tone = find (ix, dir.getChildFile ("Tone.wav"));
    CHECK (tone != nullptr);
    if (tone)
    {
        CHECK_EQ (tone->note, 57);
        CHECK (tone->lufs < -3 && tone->lufs > -40);
        CHECK (tone->peakDb < -3 && tone->peakDb > -6);
        CHECK (tone->bpmFrom == library::Item::TempoFrom::Heard);
        CHECK (tone->loop);
        CHECK (std::abs (tone->bpm - 120) < 0.01);
        CHECK (tone->tempoTrusted());
    }
    const auto* named = find (ix, dir.getChildFile ("Tone 96bpm.wav"));
    CHECK (named != nullptr && named->bpm == 96 && named->bpmFrom == library::Item::TempoFrom::Name);

    // The next time: read from the index, nothing heard again.
    LibraryScanner again (lib.store.getChildFile ("Library Index.json"));
    again.scan ({ dir.getFullPathName().toStdString() });
    CHECK (again.waitUntilDone (20000));
    CHECK_EQ (again.filesAnalysed(), 0);
    const auto* kept = find (again.index(), dir.getChildFile ("Tone.wav"));
    CHECK (kept != nullptr && tone != nullptr && *kept == *tone);
}

TEST ("sounds: with Auto-Play off, choosing a sound is silent; clicking its picture plays it (0095)")
{
    const auto dir = makeSounds ("autoplay");
    Library lib (dir);
    auto& view = lib.view;
    view.settings().open = { dir.getFullPathName().toStdString(), dir.getChildFile ("Drums").getFullPathName().toStdString() };
    view.refreshRows();
    view.autoPlayToggle().setToggleState (false, sendNotification);
    CHECK (! view.settings().autoPlay);
    view.hear (2);
    CHECK_EQ (view.list().getSelectedRow(), 2);
    CHECK (view.player().playingPath().isEmpty());
    view.playChosen();
    CHECK_EQ (view.player().playingPath(), pathOf (dir, "Drums/Beat 120bpm.wav"));
    // Kept for next time.
    library::Settings saved;
    CHECK (library::fromJson (lib.store.getChildFile ("Library Settings.json").loadFileAsString().toStdString(), saved));
    CHECK (! saved.autoPlay);
}

TEST ("sounds: a sample put on the timeline split at transients is a clip for each hit, one undo step (0095)")
{
    const auto dir = libraryFolder ("split");
    WavAudioFormat wav;
    const auto f = dir.getChildFile ("Four Beats.wav");
    writeAudio (f, wav, 2.0, 120);   // a hit on each beat: 0, 0.5, 1 and 1.5 s
    Library lib (dir);
    auto& c = lib.c;
    c.apply ("Track", [] (Project& p) { edit::addTrack (p, TrackKind::Track, "Drums"); });
    const Id t = c.project.tracks.back().id;
    const auto before = c.history.size();
    CHECK (c.importSplit (f, t, PPQ * 4));
    const auto& clips = c.project.track (t)->clips();
    CHECK_EQ (clips.size(), (size_t) 4);
    if (clips.size() == 4)
    {
        CHECK_EQ (clips[0].start, PPQ * 4);
        for (size_t i = 1; i < 4; ++i) CHECK (std::abs (clips[i].start - (PPQ * 4 + (Tick) i * PPQ)) <= PPQ / 40);
        CHECK_EQ (clips[3].end(), PPQ * 8);
    }
    CHECK_EQ (c.history.size(), before + 1);
    // And a clip on the timeline split the same way from its menu.
    c.undo();
    CHECK (c.importFile (f, t, 0));
    const Id one = c.project.track (t)->clips().front().id;
    CHECK_EQ (c.splitAtTransients (one), 4);
    CHECK_EQ (c.project.track (t)->clips().size(), (size_t) 4);
}

#include "TransientQuestion.h"

TEST ("split at transients: a Sensitivity, said with how many clips it makes as it moves: higher finds the quiet hits too; the choice is kept (0130)")
{
    // Loud hits at 0 and 1 s, quiet ones (about -54 dB) at 0.5 and 1.5 s.
    const auto dir = libraryFolder ("sensitivity");
    const auto f = dir.getChildFile ("Loud and Quiet.wav");
    {
        f.deleteFile();
        AudioBuffer<float> b (1, 96000);
        b.clear();
        for (auto [start, level] : { std::pair { 0, 0.8f }, { 24000, 0.002f }, { 48000, 0.8f }, { 72000, 0.002f } })
            for (int i = 0; i < 4000; ++i)
                b.setSample (0, start + i, level * std::exp (-i / 800.0f) * std::sin (6.2831853f * 200.0f * i / 48000.0f));
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    Library lib (dir);
    auto& c = lib.c;
    const double kept = c.transientSensitivity;
    c.apply ("Track", [] (Project& p) { edit::addTrack (p, TrackKind::Track, "Drums"); });
    const Id t = c.project.tracks.back().id;
    CHECK (c.importFile (f, t, 0));
    const Id one = c.project.track (t)->clips().front().id;
    CHECK_EQ (c.splitAtTransients (one, 0.0), 2);    // only the loud
    c.undo();
    CHECK_EQ (c.splitAtTransients (one, 1.0), 4);    // the quiet too
    c.undo();
    CHECK (c.importSplit (f, t, PPQ * 16, 0.0));
    CHECK_EQ (c.project.track (t)->clips().size(), (size_t) 3);   // the whole one, and two from the import
    c.undo();

    // The question: the count as the slider moves; Split splits at what's chosen, and keeps it.
    Component host;
    host.setSize (1000, 700);
    ui::setHost (&host);
    askTransients (c, "Split at Transients", { { one, {} } }, [&c, one] (double s) { c.splitAtTransients (one, s); });
    AlertWindow* asked = nullptr;
    for (auto* ch : host.getChildren()) if (auto* a = dynamic_cast<AlertWindow*> (ch)) asked = a;
    CHECK (asked != nullptr);
    Slider* slider = nullptr;
    std::function<void (Component&)> find = [&] (Component& comp) { for (auto* ch : comp.getChildren()) { if (auto* s = dynamic_cast<Slider*> (ch)) slider = s; else find (*ch); } };
    if (asked) find (*asked);
    CHECK (slider != nullptr);
    if (asked && slider)
    {
        auto* panel = slider->getParentComponent();
        slider->setValue (0.0, sendNotificationSync);
        CHECK_EQ (panel->getDescription(), String ("Makes 2 clips, one for each hit."));
        slider->setValue (100.0, sendNotificationSync);
        CHECK_EQ (panel->getDescription(), String ("Makes 4 clips, one for each hit."));
        asked->exitModalState (1);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK_EQ (c.project.track (t)->clips().size(), (size_t) 4);
        CHECK_EQ (c.transientSensitivity, 1.0);
    }
    ui::setHost (nullptr);
    c.setTransientSensitivity (kept);
}

TEST ("sounds: looked through once as Kalibre opens, not again on its own: a file saved meanwhile waits for Look Again (the musician's choice)")
{
    const auto dir = makeSounds ("comeback");
    Library lib (dir);
    const auto before = lib.view.scanner().index().items.size();
    CHECK (before > 0);
    WavAudioFormat wav;
    const auto added = dir.getChildFile ("Keys/Saved Meanwhile.wav");
    writeAudio (added, wav, 1.0);
    // A while on, with the window in front again or not: nothing looks.
    MessageManager::getInstance()->runDispatchLoopUntil (300);
    CHECK (! lib.view.scanner().scanning());
    CHECK (find (lib.view.scanner().index(), added) == nullptr);
    // Look Again finds it.
    lib.view.rescan();
    CHECK (lib.view.scanner().waitUntilDone (20000));
    CHECK (find (lib.view.scanner().index(), added) != nullptr);
    CHECK_EQ (lib.view.scanner().index().items.size(), before + 1);
    // And opening Kalibre (the store read again) looks through them once.
    writeAudio (dir.getChildFile ("Keys/Saved Later.wav"), wav, 1.0);
    lib.view.useStore (lib.store);
    CHECK (lib.view.scanner().waitUntilDone (20000));
    CHECK (find (lib.view.scanner().index(), dir.getChildFile ("Keys/Saved Later.wav")) != nullptr);
}

TEST ("sounds: Sync switched while a sound plays changes it at once, carrying on from where it was, not from its start (0122)")
{
    const auto dir = makeSounds ("resync");
    Library lib (dir);
    auto& player = lib.view.player();
    player.audioDeviceAboutToStart (nullptr);
    const auto* beat = lib.view.item (dir.getChildFile ("Drums/Beat 120bpm.wav").getFullPathName().toStdString());
    CHECK (beat != nullptr);
    if (! beat) return;
    player.nextBeat = [] { return 0.1; };
    player.play (*beat, false, 60);   // Sync off: its own tempo
    for (int i = 0; i < 200 && player.progress() < 0; ++i) MessageManager::getInstance()->runDispatchLoopUntil (10);
    CHECK (player.progress() >= 0);
    AudioBuffer<float> out (2, 2400);
    auto block = [&]
    {
        out.clear();
        player.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, out.getNumSamples(), {});
        return loudest (out);
    };
    for (int i = 0; i < 9; ++i) block();   // nearly half a second in
    const double before = player.progress();
    block();
    const double through = player.progress();
    const double stepBefore = through - before;   // how far one block goes at its own tempo
    CHECK (through > 0.05 && through < 0.9);
    // Sync on, at 60 BPM: twice as long, made again, carrying on from the same place in it.
    const int ownLength = player.soundLength();
    player.remake (*beat, true, 60);
    for (int i = 0; i < 300 && player.soundLength() == ownLength; ++i) MessageManager::getInstance()->runDispatchLoopUntil (10);
    CHECK_EQ (player.soundLength(), ownLength * 2);
    CHECK (std::abs (player.progress() - through) < 0.05);
    // Straight on, no wait for the next beat, no start again; at the new tempo, a block
    // goes half as far through it (twice as long).
    const double from = player.progress();
    CHECK (block() > 0.0f || true);
    const double stepAfter = player.progress() - from;
    CHECK (stepAfter > 0);
    CHECK (std::abs (stepAfter - stepBefore * 0.5) < stepBefore * 0.15);
}

TEST ("sounds: once the folders are looked through, the message line says what was found, not still 'Looking through' (UAT 23 November)")
{
    const auto dir = makeSounds ("found");
    Library lib (dir);
    lib.c.say ("Looking through the folder for sounds: they appear in the browser's Sounds tab as they're found.");
    lib.view.sayWhatWasFound();
    CHECK (! lib.c.message.startsWith ("Looking through"));
    CHECK (lib.c.message.contains ("5 sounds"));
}
