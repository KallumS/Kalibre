// Kalibre Drums (decision 0105): its pads, its pattern with the song, choke,
// plug-ins on pads, its state, undo, and its beat dragged to the timeline.
#include "Check.h"
#include "BuiltinInstruments.h"
#include "Controller.h"
#include "DrumMachine.h"
#include "Edit.h"
#include "Engine.h"

#include <set>

using namespace kal;
using namespace juce;

namespace
{
File drumsFolder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("drums");
    f.createDirectory();
    return f;
}

// A sound: `length` seconds at 48 kHz, every sample `level` (a hit, plainly measured).
File flatSound (const String& name, double seconds, float level, int rate = 48000)
{
    const auto f = drumsFolder().getChildFile (name + ".wav");
    f.deleteFile();
    AudioBuffer<float> b (2, (int) (seconds * rate));
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, level);
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

// The song's position, as the engine tells a device: playing at 120 BPM, maybe looping.
struct Head : AudioPlayHead
{
    double ppq = 0, bpm = 120;
    bool playing = true, looping = false;
    double loopStart = 0, loopEnd = 0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setIsLooping (looping);
        p.setLoopPoints (LoopPoints { loopStart, loopEnd });
        return p;
    }
};

// Plays the device for `blocks` blocks of `n`, moving the head on (round its loop); the left channel.
std::vector<float> play (DrumMachine& d, Head& h, int blocks, int n = 512, MidiBuffer first = {})
{
    std::vector<float> out;
    const double perQ = 48000.0 * 60.0 / h.bpm;
    for (int b = 0; b < blocks; ++b)
    {
        AudioBuffer<float> buf (2, n);
        MidiBuffer m;
        if (b == 0) m = first;
        d.processBlock (buf, m);
        for (int i = 0; i < n; ++i) out.push_back (buf.getSample (0, i));
        h.ppq += n / perQ;
        if (h.looping && h.ppq >= h.loopEnd) h.ppq = h.loopStart + (h.ppq - h.loopEnd);
    }
    return out;
}

// Where each sound starts: a sample that isn't silent after one that is.
std::vector<int> onsets (const std::vector<float>& x)
{
    std::vector<int> at;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > 1e-6f && (i == 0 || std::abs (x[i - 1]) <= 1e-6f)) at.push_back ((int) i);
    return at;
}
} // namespace

TEST ("drums: a pad's sample plays on its note, as loud as the hit and the pad's volume, panned; a muted pad doesn't (0105)")
{
    PluginManager plugins;
    DrumMachine d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = drumkit::standard();
    k.playsPattern = false;
    k.pads[0].sample = flatSound ("kick", 0.1, 0.5f).getFullPathName().toStdString();
    k.pads[1].sample = flatSound ("snare", 0.1, 0.5f).getFullPathName().toStdString();
    k.pads[1].mute = true;
    k.pads[0].volumeDb = -6.0;
    k.pads[0].pan = -1.0;   // all to the left
    d.setKit (k);
    CHECK (d.padSounds (0));
    AudioBuffer<float> buf (2, 512);
    MidiBuffer m;
    m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 127), 100);
    m.addEvent (MidiMessage::noteOn (1, 38, (uint8) 127), 100);
    d.processBlock (buf, m);
    CHECK_EQ (buf.getSample (0, 99), 0.0f);
    // Full left at constant power: the left at sqrt 2, the pad 6 dB down, the hit at full velocity.
    const float want = 0.5f * Decibels::decibelsToGain (-6.0f) * MathConstants<float>::sqrt2;
    CHECK (std::abs (buf.getSample (0, 100) - want) < 1e-4f);
    CHECK (std::abs (buf.getSample (1, 100)) < 1e-6f);
    // A softer hit, softer.
    d.reset();
    AudioBuffer<float> soft (2, 512);
    MidiBuffer m2;
    m2.addEvent (MidiMessage::noteOn (1, 36, (uint8) 64), 0);
    d.processBlock (soft, m2);
    CHECK (std::abs (soft.getSample (0, 0) - want * 64.0f / 127.0f) < 1e-4f);
}

TEST ("drums: its pattern plays with the song, each hit on its very sample, swung, round the loop on the sample it wraps (0105)")
{
    PluginManager plugins;
    DrumMachine d (plugins);
    d.prepareToPlay (48000, 512);
    Head h;
    d.setPlayHead (&h);
    auto k = drumkit::standard();
    k.pads[0].sample = flatSound ("click", 0.001, 0.5f).getFullPathName().toStdString();
    k.pattern.lanes[0].velocity[0] = 127;
    k.pattern.lanes[0].velocity[4] = 127;   // the second beat
    d.setKit (k);
    // 120 BPM: a beat is 24 000 samples; the bar repeats at 96 000.
    auto x = play (d, h, 200);
    auto at = onsets (x);
    CHECK (at.size() >= 3);
    if (at.size() >= 3) { CHECK_EQ (at[0], 0); CHECK_EQ (at[1], 24000); CHECK_EQ (at[2], 96000); }

    // Swing: the second sixteenth a half-step late (6000 + 3000).
    k.pattern.lanes[0].velocity = std::vector<int> (16, 0);
    k.pattern.lanes[0].velocity[1] = 127;
    k.pattern.swing = 0.5;
    d.setKit (k);
    d.reset();
    h.ppq = 0;
    at = onsets (play (d, h, 40));
    CHECK (! at.empty());
    if (! at.empty()) CHECK_EQ (at[0], 9000);

    // Round a loop of half a bar from 0.9 beats: the first step again on the sample it wraps.
    k.pattern.swing = 0;
    k.pattern.lanes[0].velocity = std::vector<int> (16, 0);
    k.pattern.lanes[0].velocity[0] = 127;
    d.setKit (k);
    d.reset();
    h.ppq = 0.9;
    h.looping = true;
    h.loopStart = 0;
    h.loopEnd = 2;
    at = onsets (play (d, h, 100));
    CHECK (! at.empty());
    if (! at.empty()) CHECK_EQ (at[0], (int) std::lround (1.1 * 24000));
    // Stopped: nothing.
    h.playing = false;
    h.looping = false;
    d.reset();
    const auto quiet = play (d, h, 100);
    CHECK (onsets (quiet).empty());
    // Play Pattern off: nothing either, though the song plays.
    h.playing = true;
    h.ppq = 0;
    k.playsPattern = false;
    d.setKit (k);
    CHECK (onsets (play (d, h, 100)).empty());
}

TEST ("drums: in a choke group the closed hat stops the open one on its very sample (0105)")
{
    PluginManager plugins;
    DrumMachine d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = drumkit::standard();
    k.playsPattern = false;
    k.pads[4].sample = flatSound ("open hat", 1.0, 0.3f).getFullPathName().toStdString();   // Open Hat
    d.setKit (k);
    AudioBuffer<float> buf (2, 512);
    MidiBuffer m;
    m.addEvent (MidiMessage::noteOn (1, 46, (uint8) 127), 0);
    m.addEvent (MidiMessage::noteOn (1, 42, (uint8) 127), 300);   // the closed hat: no sound of its own here
    d.processBlock (buf, m);
    CHECK (std::abs (buf.getSample (0, 299)) > 0.1f);
    CHECK_EQ (buf.getSample (0, 300), 0.0f);
    CHECK_EQ (buf.getSample (0, 511), 0.0f);
}

TEST ("drums: a pad can be a plug-in, sent its note on each hit; its settings are kept with the kit (0105)")
{
    PluginManager plugins;
    DrumMachine d (plugins);
    d.prepareToPlay (48000, 512);
    auto k = drumkit::standard();
    k.playsPattern = false;
    k.pads[2].plugin = synthRef();
    k.pads[2].sample = flatSound ("unused", 0.1, 0.5f).getFullPathName().toStdString();   // the plug-in wins
    d.setKit (k);
    auto* synth = d.padPlugin (2);
    CHECK (synth != nullptr);
    if (! synth) return;
    CHECK (d.padSounds (2));
    synth->getParameters()[3]->setValueNotifyingHost (0.81f);
    float loudest = 0;
    for (int b = 0; b < 10; ++b)
    {
        AudioBuffer<float> buf (2, 512);
        MidiBuffer m;
        if (b == 0) m.addEvent (MidiMessage::noteOn (1, 39, (uint8) 110), 0);   // the clap's note
        d.processBlock (buf, m);
        loudest = jmax (loudest, buf.getMagnitude (0, 512));
    }
    CHECK (loudest > 0.01f);
    // Saved and opened in another: the synth there, with its setting.
    MemoryBlock state;
    d.getStateInformation (state);
    DrumMachine again (plugins);
    again.prepareToPlay (48000, 512);
    again.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (again.kit().pads[2].hasPlugin());
    CHECK (again.padPlugin (2) != nullptr);
    if (again.padPlugin (2)) CHECK (std::abs (again.padPlugin (2)->getParameters()[3]->getValue() - 0.81f) < 0.001f);
    CHECK_EQ (again.kit().pads.size(), k.pads.size());
}

TEST ("drums: an edit in its window is one undo step; its beat dragged to its track is a drum-grid clip, its own pattern switched off, one undo step (0105)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
    const Id dev = edit::addDevice (c.project, t, drumsRef(), DeviceRole::Instrument);
    c.refresh();
    auto* d = dynamic_cast<DrumMachine*> (engine.instance (dev));
    CHECK (d != nullptr);
    if (! d) return;
    CHECK_EQ (c.deviceOf (d), dev);
    const auto steps = c.history.size();
    auto k = d->kit();
    k.pattern.lanes[0].velocity[0] = 120;
    k.pattern.lanes[1].velocity[4] = 100;
    d->perform ("Steps", [d, k] { d->setKit (k); });
    CHECK_EQ (c.history.size(), steps + 1);
    c.undo();
    CHECK_EQ (d->kit().pattern.lanes[0].velocity[0], 0);
    c.redo();
    CHECK_EQ (d->kit().pattern.lanes[0].velocity[0], 120);

    const Id made = c.dropDrumPattern (d, t, PPQ * 8);
    CHECK (made != 0);
    const auto* clip = c.project.clip (made);
    CHECK (clip != nullptr);
    if (clip)
    {
        CHECK_EQ (clip->start, PPQ * 8);
        CHECK_EQ (clip->length, PPQ * 4);
        CHECK_EQ (clip->midi.notes.size(), (size_t) 2);
        for (auto& n : clip->midi.notes) CHECK (n.from == NoteFrom::Drums && n.id != 0);
    }
    CHECK (! d->kit().playsPattern);
    CHECK_EQ (c.history.size(), steps + 2);
    c.undo();
    CHECK (c.project.clip (made) == nullptr);
    CHECK (d->kit().playsPattern);
}

#include "PatternEditors.h"
#include "Theme.h"

namespace
{
Button* buttonNamed (Component& in, const String& text)
{
    for (auto* ch : in.getChildren())
    {
        if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == text) return b;
        if (auto* b = buttonNamed (*ch, text)) return b;
    }
    return nullptr;
}
} // namespace

TEST ("drums: on a track playing Kalibre Drums the drum grid's rows are the kit's pads, as named; its Random puts each hit on a pad (0105)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
        const Id dev = edit::addDevice (c.project, t, drumsRef(), DeviceRole::Instrument);
        const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
        c.refresh();
        auto* d = dynamic_cast<DrumMachine*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        auto k = d->kit();
        k.pads[0].name = "Big Kick";
        d->setKit (k);
        c.selectClip (clip);
        DrumGrid grid (c);
        grid.setSize (900, 400);
        CHECK_EQ (grid.preferredHeight(), 8 * theme::px (24) + 8 + theme::px (34));   // eight pads, not General MIDI's eleven
        auto* random = buttonNamed (grid, "Random");
        CHECK (random != nullptr);
        if (! random) return;
        random->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        std::set<int> pads;
        for (auto& p : d->kit().pads) pads.insert (p.note);
        const auto& notes = c.project.clip (clip)->midi.notes;
        CHECK (! notes.empty());
        for (auto& n : notes) CHECK (pads.count (n.pitch) == 1);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("drums: Random Samples gives each pad a sound of its kind from the Sounds folders, one undo step; a sound dropped on a pad's row plays it (0105)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
        const Id dev = edit::addDevice (c.project, t, drumsRef(), DeviceRole::Instrument);
        c.refresh();
        auto* d = dynamic_cast<DrumMachine*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        library::Index ix;
        for (auto* name : { "Kick 1", "Snare 1", "Clap 1", "HH Closed", "HH Open", "Tom Low", "Tom High", "Crash 1", "Pad Long" })
        {
            const auto f = flatSound (name, String (name) == "Pad Long" ? 8.0 : 0.2, 0.3f);
            library::Item it;
            it.path = f.getFullPathName().toStdString();
            it.readable = true;
            it.seconds = String (name) == "Pad Long" ? 8.0 : 0.2;
            ix.items[it.path] = it;
        }
        c.soundsIndex = [&ix] { return &ix; };
        std::unique_ptr<AudioProcessorEditor> ed (d->createEditor());
        ed->setSize (1100, 400);
        auto* fill = buttonNamed (*ed, "Random Samples");
        CHECK (fill != nullptr);
        if (! fill) return;
        const auto steps = c.history.size();
        fill->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK_EQ (c.history.size(), steps + 1);
        const auto& kit = d->kit();
        CHECK (String (kit.pads[0].sample).contains ("Kick"));
        CHECK (String (kit.pads[1].sample).contains ("Snare"));
        CHECK (String (kit.pads[3].sample).contains ("HH Closed"));
        CHECK (String (kit.pads[4].sample).contains ("HH Open"));
        for (auto& p : kit.pads) CHECK (! String (p.sample).contains ("Pad Long"));
        for (int i = 0; i < 8; ++i) CHECK (d->padSounds (i));
        c.undo();
        CHECK (d->kit().pads[0].sample.empty());

        // A sound from the Sounds tab dropped on the snare's row.
        std::vector<DragAndDropTarget*> rows;
        for (auto* ch : ed->getChildren()) if (auto* r = dynamic_cast<DragAndDropTarget*> (ch)) rows.push_back (r);
        CHECK_EQ (rows.size(), (size_t) 8);
        if (rows.size() < 2) return;
        const auto clap = flatSound ("Dropped", 0.2, 0.3f);
        DragAndDropTarget::SourceDetails drop (var ("file:" + clap.getFullPathName()), nullptr, {});
        CHECK (rows[1]->isInterestedInDragSource (drop));
        rows[1]->itemDropped (drop);
        CHECK_EQ (String (d->kit().pads[1].sample), clap.getFullPathName());
        CHECK (d->padSounds (1));
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "Arrange.h"

TEST ("drums: the pattern dragged from its window and let go over the drums' track in the arrangement is a clip where it's let go (0105)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
        const Id dev = edit::addDevice (c.project, t, drumsRef(), DeviceRole::Instrument);
        c.refresh();
        auto* d = dynamic_cast<DrumMachine*> (engine.instance (dev));
        CHECK (d != nullptr);
        if (! d) return;
        Arrange arrange (c);
        arrange.setSize (1200, 600);
        c.refresh();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        // What the window's Drag to Timeline button sends, let go below the tracks: on the device's own track.
        const String what = "drums:" + String::toHexString ((pointer_sized_int) d);
        DragAndDropTarget::SourceDetails drop (var (what), nullptr, Point<int> (700, 590));
        CHECK (arrange.isInterestedInDragSource (drop));
        arrange.itemDropped (drop);
        const auto& clips = c.project.track (t)->clips();
        CHECK_EQ (clips.size(), (size_t) 1);
        if (! clips.empty()) { CHECK (clips.front().start > 0); CHECK_EQ (clips.front().name, std::string ("Beat")); }
        CHECK (! d->kit().playsPattern);
        // Anything else that isn't a device: nothing.
        DragAndDropTarget::SourceDetails junk (var ("drums:1234"), nullptr, Point<int> (700, 590));
        arrange.itemDropped (junk);
        CHECK_EQ (c.project.track (t)->clips().size(), (size_t) 1);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("sampler: Random Sample loads any sound from the Sounds folders, a new one each click, its Root Note the note it is; each one undo step, as is Load Sample (0108)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
        const Id dev = edit::addDevice (c.project, t, samplerRef(), DeviceRole::Instrument);
        c.refresh();
        auto* s = dynamic_cast<KalibreSampler*> (engine.instance (dev));
        CHECK (s != nullptr);
        if (! s) return;
        const auto start = c.history.size();
        // Undo only this test's own steps, and look the Sampler up again after: never one that's gone.
        auto undo = [&]
        {
            CHECK (c.history.size() > start);
            if (c.history.size() <= start) return false;
            c.undo();
            s = dynamic_cast<KalibreSampler*> (engine.instance (dev));
            CHECK (s != nullptr);
            return s != nullptr;
        };
        std::unique_ptr<AudioProcessorEditor> ed (s->createEditor());
        ed->setSize (1000, 300);
        auto* random = buttonNamed (*ed, "Random Sample");
        CHECK (random != nullptr);
        if (! random) return;
        // Clicked at once, not posted: a slow machine can't put the clicks out of step with the checks.
        auto click = [random] { if (random->onClick) random->onClick(); };

        // No sounds scanned: said, nothing changed.
        library::Index ix;
        c.soundsIndex = [&ix] { return &ix; };
        auto steps = c.history.size();
        click();
        CHECK_EQ (c.history.size(), steps);
        CHECK (c.message.contains ("Sounds tab"));
        CHECK (s->sample().getNumSamples() == 0);

        // A pad, heard as A3: loaded, and played in tune from A3.
        auto add = [&ix] (const String& name, double seconds, int note)
        {
            const auto f = flatSound (name, seconds, 0.3f);
            library::Item it;
            it.path = f.getFullPathName().toStdString();
            it.seconds = seconds;
            it.note = note;
            ix.items[it.path] = it;
            return f;
        };
        const auto pad = add ("Pad A3", 2.0, 57);
        click();
        CHECK_EQ (c.history.size(), steps + 1);
        CHECK (s->file() == pad);
        CHECK (s->sample().getNumSamples() > 0);
        CHECK_EQ (roundToInt (s->value (KalibreSampler::Root)), 57);
        CHECK (c.message.contains ("Pad A3"));

        // A loop with no one note: the other one, the Root Note left as it is.
        const auto loop = add ("Break 120bpm", 4.0, -1);
        click();
        CHECK_EQ (c.history.size(), steps + 2);
        CHECK (s->file() == loop);
        CHECK_EQ (roundToInt (s->value (KalibreSampler::Root)), 57);

        // Undo: the pad again; then none, Root back to C4.
        if (! undo()) return;
        CHECK (s->file() == pad);
        if (! undo()) return;
        CHECK (s->sample().getNumSamples() == 0);
        CHECK_EQ (roundToInt (s->value (KalibreSampler::Root)), 60);

        // A sound dropped on it from the browser: one undo step too.
        auto* target = dynamic_cast<DragAndDropTarget*> (ed.get());
        CHECK (target != nullptr);
        if (! target) return;
        steps = c.history.size();
        target->itemDropped (DragAndDropTarget::SourceDetails (var ("file:" + loop.getFullPathName()), nullptr, {}));
        CHECK_EQ (c.history.size(), steps + 1);
        CHECK (s->file() == loop);
        if (! undo()) return;
        CHECK (s->sample().getNumSamples() == 0);
        c.soundsIndex = nullptr;
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}
